/* SPIFFS filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

//系统
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
//网络
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "esp_websocket_client.h"
//freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
//MP3
#include "audio.h"
#include <mp3dec.h>
//板极支持包
#include "esp32_s3_box.h"
#include "esp32_s3_box_lite.h"
#include "bsp_i2s.h"
#include "bsp_codec.h"
/***log相关***/
static const char *TAG_websocket = "WebSocket";
static const char *TAG_spiffs = "SPIFFS";
static const char *TAG_mp3 = "MP3";
/***websocket相关***/
#define NO_DATA_TIMEOUT_SEC 20
static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;
static esp_websocket_client_handle_t client;
/***MP3相关***/
static int index_count = 0;
char file_w_name_mp3[20]; 
FILE *fp_write_mp3 =NULL;
TaskHandle_t xmp3_Handle = NULL;

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGD(TAG_websocket, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGD(TAG_websocket, "WEBSOCKET_EVENT_CONNECTED");

        /***打开文件 Use POSIX and C standard library functions to work with files.***/
        ESP_LOGD(TAG_spiffs, "Opening file");
        FILE* f = fopen("/spiffs/test.wav", "rb");
        if (f == NULL) {
            ESP_LOGE(TAG_spiffs, "Failed to open file for reading");
            return;
        }

        /*** 读取.wav文件***/
        // fseek(f, 0, SEEK_END);
        // int file_size = ftell(f);
        // ESP_LOGD(TAG_spiffs,"file_size : %d", file_size);
        // fseek(f, 0, SEEK_SET);
        char* file_data = calloc(1024, sizeof(char));
        if (file_data == NULL) {
            ESP_LOGE(TAG_spiffs,"Memory allocation failed!");
            return;
        }
        size_t bytes_read =0;
        int websocket_sent_ret=0;
        /***读取wav文件***/
        do{
            bytes_read = fread(file_data, 1, 1024, f);
            /***发送二进制文件***/
            websocket_sent_ret = esp_websocket_client_send_bin(client,file_data,1024,portMAX_DELAY);
            ESP_LOGI(TAG_websocket, "Sending size:%d", websocket_sent_ret);
        }while(bytes_read);
        /***查看wav录音***/
        // printf("======\n");
        // for(int i =0 ;i<44;i++)
        // {
        //     printf("%c", file_data[i]); //%c显示字符串 %x显示16进制
        // }
        // printf("\n======\n");
        esp_websocket_client_send_text(client, "EOF", strlen("EOF"), portMAX_DELAY);
        /***关闭文件***/
        fclose(f);
        /***释放文件buff***/
        free(file_data);
        file_data = NULL;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGD(TAG_websocket, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGD(TAG_websocket, "WEBSOCKET_EVENT_DATA");
        ESP_LOGD(TAG_websocket, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x9) {
            // Received a ping frame, send a pong frame
            esp_websocket_client_send_text(client, "pong", strlen("pong"), portMAX_DELAY);
        } else if (data->op_code == 0xA) {
            // Received a pong frame
            ESP_LOGD(TAG_websocket, "Received a pong frame");
        } else if (data->op_code == 0x1) {
            // Received a text frame
            ESP_LOGD(TAG_websocket, "Received a text frame: %.*s", data->data_len, (char *)data->data_ptr);
            /***JSON解码***/
            cJSON *json = cJSON_Parse((char*)data->data_ptr);
            if (json) { 
                cJSON *index = cJSON_GetObjectItemCaseSensitive(json, "index");
                if (cJSON_IsNumber(index)) {
                    index_count = cJSON_GetNumberValue(index);
                    //ESP_LOGI(TAG_websocket,"mp3_index: %d",index_count);
                    char str[2] = {'a' + index_count, '\0'}; // 将数字转换为字符
                    sprintf(file_w_name_mp3, "%s%s%s", "/spiffs/index", str,".mp3"); 
                }
                cJSON_Delete(json);
            }else {
                ESP_LOGW(TAG_websocket,"json failed");
            }
        } else if (data->op_code == 0x2) {
            /***Received a binary frame***/
            //ESP_LOGI(TAG_websocket, "Received a binary frame into : %s", file_w_name_mp3);
            if(data->payload_offset == 0){
                fp_write_mp3 = fopen(file_w_name_mp3, "wb");
                // ESP_LOGI(TAG_mp3, "mp3:%s",data->data_ptr);
            }
            fwrite(data->data_ptr, 1, data->data_len, fp_write_mp3);
            if(data->payload_len == (data->payload_offset + data->data_len)){
                fclose(fp_write_mp3);
                //ESP_LOGI(TAG_websocket,"Give handler");
                xTaskNotifyGive(xmp3_Handle);
            }
        }else if (data->op_code == 0x08 && data->data_len == 2) {
            ESP_LOGD(TAG_websocket, "Received closed message with code=%d", 256*data->data_ptr[0] + data->data_ptr[1]);
        }
        ESP_LOGD(TAG_websocket, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGD(TAG_websocket, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

static void mp3_task(void *arg)
{
    //char buf[128];
    uint32_t Already_shedule_mp3 = 0;
    char file_r_name_mp3[20]; 
    bsp_board_init();
    bsp_board_power_ctrl(POWER_MODULE_AUDIO, true);
    /*** AUDIO DECODE ***/
    while (1) {
        uint32_t Need_shedule_mp3 = ulTaskNotifyTake( pdFALSE,
                                    portMAX_DELAY);
        ESP_LOGI(TAG_mp3, "Need_shedule_mp3= %d,Already_shedule_mp3=%d" ,Need_shedule_mp3,Already_shedule_mp3);
        if(Need_shedule_mp3 > 0 ){
            char str[2] = {'a' + Already_shedule_mp3, '\0'}; // 将数字转换为字符
            Already_shedule_mp3++;
            sprintf(file_r_name_mp3, "%s%s%s", "/spiffs/index", str,".mp3"); 
            esp_err_t ret_val = aplay_mp3(file_r_name_mp3);
            ESP_LOGD(TAG_mp3,"mp3_file_%s : %d",file_r_name_mp3, ret_val);
            //vTaskDelay(5);
            //播放完删除文件
            remove(file_r_name_mp3);
        }
    }
}

void app_main(void)
{
    /***log***/ 
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("WebSocket", ESP_LOG_INFO);
    esp_log_level_set("SPIFFS", ESP_LOG_INFO);
    esp_log_level_set("MP3", ESP_LOG_INFO);

    /***WI-FI配置***/
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /***websocket配置***/
    esp_websocket_client_config_t websocket_cfg = {};
    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();
    websocket_cfg.uri = "ws://melo-hz-melo-hz-psolhmxcxk.cn-shanghai.fcapp.run/chat";

    /***SPIFFS配置***/
    ESP_LOGI(TAG_spiffs, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 50,
      .format_if_mount_failed = true
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG_spiffs, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG_spiffs, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG_spiffs, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    xTaskCreatePinnedToCore(mp3_task, "mp3_task", 1024*8, NULL, 5, &xmp3_Handle , 1);

    /*** websocket开始连接 ***/
    ESP_LOGI(TAG_websocket, "Connecting to %s...", websocket_cfg.uri);
    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
    xTimerStart(shutdown_signal_timer, portMAX_DELAY);
    /***发送文本***/
    // char data[32];
    // if (esp_websocket_client_is_connected(client)) {
    //     int len = sprintf(data, "hello");
    //     ESP_LOGI(TAG_websocket, "Sending %s", data);
    //     esp_websocket_client_send_text(client, data, len, portMAX_DELAY);
    // }

    while(true)
    {
        /***等待超时信号，关闭websocket***/
        xSemaphoreTake(shutdown_sema, portMAX_DELAY);
        //esp_websocket_client_close(client, portMAX_DELAY);
        ESP_LOGD(TAG_websocket, "Websocket Stopped");
        esp_websocket_client_destroy(client);
        xSemaphoreGive(shutdown_sema);
    }

    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(conf.partition_label);
    ESP_LOGI(TAG_spiffs, "SPIFFS unmounted");
}
