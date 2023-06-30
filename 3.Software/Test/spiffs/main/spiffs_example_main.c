/* SPIFFS filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"

static const char *TAG_spiffs = "SPIFFS";

void app_main(void)
{
    //配置SPIFFS
    ESP_LOGI(TAG_spiffs, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
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

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    //打开文件
    ESP_LOGI(TAG_spiffs, "Opening file");
    FILE* f = fopen("/spiffs/test.wav", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG_spiffs, "Failed to open file for reading");
        return;
    }

    // 读取.wav文件
    fseek(f, 0, SEEK_END);
    int file_size = ftell(f);
    ESP_LOGI(TAG_spiffs,"file_size : %d", file_size);
    fseek(f, 0, SEEK_SET);
    char* file_data = calloc(file_size, sizeof(char));
    if (NULL == file_data) {
        ESP_LOGI(TAG_spiffs,"Memory allocation failed!");
        return;
    }else{
        fread(file_data, 1, file_size, f);
        ESP_LOGI(TAG_spiffs,"Memory allocation succeeded!");
        //查看wav录音
        printf("======\n");
        for(int i =0 ;i<44;i++)
        {
            printf("%c", file_data[i]); //%c显示字符串 %x显示16进制
        }
        printf("\n======\n");
    }

    // 关闭文件
    fclose(f);
    // 在这里可以将读取到的文件数据发送出去

    // 释放内存
    free(file_data);
    file_data = NULL;

    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(conf.partition_label);
    ESP_LOGI(TAG_spiffs, "SPIFFS unmounted");

}
