/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

//系统
#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
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

#define NO_DATA_TIMEOUT_SEC 5

static const char *TAG_websocket = "websocket";

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG_websocket, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG_websocket, "WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_websocket, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG_websocket, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG_websocket, "Received opcode=%d", data->op_code);
        ESP_LOGI(TAG_websocket, "Received opcode=%d", data->data_len);
        ESP_LOGI(TAG_websocket, "Received opcode=%d", data->payload_len);
        // if (data->op_code == 0x08 && data->data_len == 2) {
        //     ESP_LOGW(TAG_websocket, "Received closed message with code=%d", 256*data->data_ptr[0] + data->data_ptr[1]);
        // } else {
        //     ESP_LOGW(TAG_websocket, "Received=%.*s", data->data_len, (char *)data->data_ptr);
        // }
        // ESP_LOGW(TAG_websocket, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG_websocket, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

void app_main(void)
{
    // ESP_LOGI(TAG_websocket, "[APP] Startup..");
    // ESP_LOGI(TAG_websocket, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    // ESP_LOGI(TAG_websocket, "[APP] IDF version: %s", esp_get_idf_version());
    // esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_DEBUG);
    // esp_log_level_set("TRANSPORT_WS", ESP_LOG_DEBUG);
    // esp_log_level_set("TRANS_TCP", ESP_LOG_DEBUG);
   
    //WI-FI配置
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    // websocket配置
    esp_websocket_client_config_t websocket_cfg = {};
    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();
    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;

    //websocket开始传输
    ESP_LOGI(TAG_websocket, "Connecting to %s...", websocket_cfg.uri);
    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
    xTimerStart(shutdown_signal_timer, portMAX_DELAY);
    char data[32];
    if (esp_websocket_client_is_connected(client)) {
        int len = sprintf(data, "hello");
        ESP_LOGI(TAG_websocket, "Sending %s", data);
        esp_websocket_client_send_text(client, data, len, portMAX_DELAY);
    }
    vTaskDelay(5000 / portTICK_RATE_MS);

    //websocket关闭
    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    esp_websocket_client_close(client, portMAX_DELAY);
    ESP_LOGI(TAG_websocket, "Websocket Stopped");
    esp_websocket_client_destroy(client);
}
