/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_board_init.h"
#include "driver/i2s.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "model_path.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_http_client.h"

static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static volatile int task_flag = 0;
SemaphoreHandle_t xSemaphore = 0;
SemaphoreHandle_t xSemaphoreMutex =0;
static const char *TAG = "HTTP_CLIENT";

//给虚拟示波器的命令
#define CMD_WARE     3
uint8_t cmdf[] = {CMD_WARE, ~CMD_WARE };    //前命令
uint8_t cmdr[] = {~CMD_WARE, CMD_WARE };    //后命令

//wav文件
#define WAV_BUFF_BYTES (96 * 1024 + 22)*2
int16_t wav_buff_count = 0;
int16_t *wav_buff =NULL;

//定义WAV文件头结构体
typedef struct {
    uint16_t riff[2];                //RIFF文件头
    uint32_t chunkSize;             //文件大小
    uint16_t wave[2];                //WAVE标识
    uint16_t fmt[2];                 //子块1 fmt标识
    uint32_t subchunk1Size;         //子块1的大小
    uint16_t audioFormat;           //音频格式，1表示PCM
    uint16_t numChannels;           //通道数
    uint32_t sampleRate;            //采样率
    uint32_t byteRate;              //字节率= 16000*16*1/8
    uint16_t blockAlign;            //块对齐
    uint16_t bitsPerSample;         //样本位数
    uint16_t data[2];                //子块2 数据标识
    uint32_t dataSize;              //子块2 数据大小
} WAV_HEADER;

//定义WAV文件头信息,注意字段标识部分是大端存储，大小是小端存储
WAV_HEADER wavHeader = {
    .riff = {'RI', 'FF'},
    .chunkSize = WAV_BUFF_BYTES - 8,
    .wave = {'WA', 'VE'},
    .fmt = {'fm', 't '},
    .subchunk1Size = 16,
    .audioFormat = 1, //PCM
    .numChannels = 1,
    .sampleRate = 16000,
    .byteRate = 32000,
    .bitsPerSample = 16,
    .data = {'da', 'ta'},
    .dataSize = WAV_BUFF_BYTES - 44,
};

void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);   
    printf("audio_chunksize:%d\n",audio_chunksize);   //1024
    int nch = afe_handle->get_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();   //4
    assert(nch<feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);
    //printf("------------detect start---------\n");
    while (task_flag) {
        //显示采集时间
        //int a = xTaskGetTickCount();
        esp_get_feed_data(i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        afe_handle->feed(afe_data, i2s_buff);
        //int b = xTaskGetTickCount();
        //printf("time:%d\n",b-a);

        //显示栈使用情况
        //printf("feed_TaskGetStackHighWaterMark:%d\n",uxTaskGetStackHighWaterMark( NULL )); //6352
    }
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    size_t detect_flag=0;
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);   
    printf("afe_chunksize:%d\n",afe_chunksize);  //512

    //申请buf放录音
    wav_buff = heap_caps_calloc(WAV_BUFF_BYTES/2, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == wav_buff) {
        printf("Memory allocation failed!");
        return;
    }
    assert(wav_buff);
    //将WAV文件头存储在int16_t数组中
    memcpy(wav_buff, &wavHeader, sizeof(wavHeader));
    //打印wav头
    printf("======\n");
    for (int i = 0; i < 44; i++) {
        printf("%02x ", wav_buff[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("======\n");

    while (task_flag) {
        afe_fetch_result_t* res = afe_handle->fetch(afe_data); 
        if(detect_flag == 0){
            if (!res || res->ret_value == ESP_FAIL) {
                printf("fetch error!\n");
            }

            if (res->wakeup_state == WAKENET_DETECTED) {
                printf("wakeword detected\n");
                // Start recording
                printf("------------record start---------\n");
                detect_flag = 1;
            }
        }
        else{
            if (res->ret_value != ESP_FAIL) {
                if( xSemaphoreTake( xSemaphoreMutex, ( TickType_t ) 10 ) == pdTRUE )
                {
                    memcpy(wav_buff + wav_buff_count * 512 +22, res->data, afe_chunksize * sizeof(int16_t));
                    xSemaphoreGive( xSemaphoreMutex );
                }
                wav_buff_count ++;
                xSemaphoreGive( xSemaphore );
                if(wav_buff_count ==144){
                    detect_flag = 0 ;
                    wav_buff_count =0 ;
                }
                // esp_http_client_set_method(client, HTTP_METHOD_POST);     
                // esp_http_client_set_post_field(client, (const char *)buff, 96 * 1024);
                // esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000");  
                // esp_err_t err = esp_http_client_perform(client);
                // if (err == ESP_OK) 
                // {
                //     ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                //             esp_http_client_get_status_code(client),
                //             esp_http_client_get_content_length(client));
                // } 
                // else 
                // {
                //     ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
                // }
                // esp_http_client_cleanup(client);
            }
        }
        //显示栈使用情况
        //printf("detect_TaskGetStackHighWaterMark:%d\n",uxTaskGetStackHighWaterMark( NULL )); //6144
    }
    if (wav_buff) {
        free(wav_buff);
        wav_buff = NULL;
    }
    vTaskDelete(NULL);
}

void sendwave_Task(void *arg)
{
    while (task_flag) {
        if( xSemaphoreTake( xSemaphore, ( TickType_t ) portMAX_DELAY ) )
        {
            //int16_t print_wav_buff_count = *(int16_t *)arg ;
            //printf("print_wav_buff_count %d", print_wav_buff_count); 
            //puts(cmdf);   //先发送前命令
            usb_serial_jtag_write_bytes(cmdf , 2 ,10);
            if( xSemaphoreTake( xSemaphoreMutex, ( TickType_t ) 10 ) == pdTRUE )
            {
                 //uts((char *)wav_buff + (print_wav_buff_count -1 ) * 512 +22 );
                 //printf("Wrote %d bytes\n", txBytes);    //发送数据 
                 xSemaphoreGive( xSemaphoreMutex );
            } 
            usb_serial_jtag_write_bytes(cmdr , 2 ,10);   //发送后命令
            //显示栈使用情况
            //printf("sendwave_TaskGetStackHighWaterMark:%d\n",uxTaskGetStackHighWaterMark( NULL )); //6352
        }
    }
}

void app_main()
{
    ESP_ERROR_CHECK(esp_board_init(AUDIO_HAL_16K_SAMPLES, 1, 16));
    // ESP_ERROR_CHECK(esp_sdcard_init("/sdcard", 10));

    srmodel_list_t *models = esp_srmodel_init("model");
    if (models!=NULL) {
        for (int i=0; i<models->num; i++) {
            printf("Load: %s\n", models->model_name[i]);
        }
    }
    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);

    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.wakenet_init = true;
    afe_config.wakenet_model_name = wn_name;
    afe_config.voice_communication_init = false;

#if defined CONFIG_ESP32_S3_BOX_BOARD || defined CONFIG_ESP32_S3_EYE_BOARD
    afe_config.aec_init = false;
    #if defined CONFIG_ESP32_S3_EYE_BOARD
        afe_config.pcm_config.total_ch_num = 2;
        afe_config.pcm_config.mic_num = 1;
        afe_config.pcm_config.ref_num = 1;
    #endif
#endif
    //esp_afe_sr_data_t结构体的数据初始化
    afe_data = afe_handle->create_from_config(&afe_config);

    //WI-FI初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "Connected to AP, begin http example");
    //http初始化
    esp_http_client_config_t config = 
    {
        .url = "http://vop.baidu.com/server_api?dev_pid=1536&cuid=ESP32_HanChenen521&token=24.5af78a8f13afcd9a592624865bbd5eac.2592000.1562320078.282335-15514068"
    };       
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    xSemaphore = xSemaphoreCreateBinary();
    xSemaphoreMutex = xSemaphoreCreateMutex();
    
    //创建任务
    task_flag = 1;
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void*)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(&sendwave_Task, "sendwave", 8 * 1024, (void*)&wav_buff_count, 4, NULL, 1);
    // // You can call afe_handle->destroy to destroy AFE.
    // task_flag = 0;

    // printf("destroy\n");
    // afe_handle->destroy(afe_data);
    // afe_data = NULL;
    // printf("successful\n");
}
