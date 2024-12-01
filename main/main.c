#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#include "secrets.h"
#include "wifi_connect.h"
#include "time_sync.h"
#include "tg.h"


static const char TAG[] = "gatekeeper";

#define TIME_PERIOD (86400000000ULL)

static void handler(char* buf, tg_update_t* update) {
    tg_log_token(buf, "handled update", update->id);
}


static void getekeeper_task(void* pvparameters) {
    ESP_LOGI(TAG, "Starting Gatekeeper");
    tg_start(BOT_TOKEN, handler);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = 1,
            .threshold.authmode = WIFI_AUTH_WPA_PSK,
        },
    };
    ESP_ERROR_CHECK(connect_to_wifi(&wifi_config));

    if (esp_reset_reason() == ESP_RST_POWERON) {
        ESP_LOGI(TAG, "Updating time from NVS");
        ESP_ERROR_CHECK(update_time_from_nvs());
    }

    const esp_timer_create_args_t nvs_update_timer_args = {
            .callback = (void*)&fetch_and_store_time_in_nvs,
    };

    esp_timer_handle_t nvs_update_timer;
    ESP_ERROR_CHECK(esp_timer_create(&nvs_update_timer_args, &nvs_update_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(nvs_update_timer, TIME_PERIOD));

    xTaskCreate(&getekeeper_task, "gatekeeper", 8192, NULL, 5, NULL);
}