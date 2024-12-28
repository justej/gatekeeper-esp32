#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "sdkconfig.h"

#include "secrets.h"
#include "wifi_connect.h"
#include "time_sync.h"
#include "tg.h"
#include "handler.h"
#include "gate_control.h"
#include "users.h"

static const char TAG[] = "gatekeeper";

#define TIME_PERIOD (86400000000ULL)

static void gatekeeper_gate_control_task(void* pvparameters) {
    ESP_LOGI(TAG, "Starting gate control task");
    startGateControl(gk_open_queue, gk_status_queue);
}

static void gatekeeper_telegram_task(void* pvparameters) {
    ESP_LOGI(TAG, "Starting Telegram task");
    tg_init(BOT_TOKEN);
    tg_start(gk_handler, gk_open_queue, gk_status_queue);
}

void app_main(void) {
    gpio_config_t gate_gpio = {
        .pin_bit_mask = GPIO_GATE_MASK,
        .pull_up_en = true,
        .pull_down_en = false,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&gate_gpio);

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
        ESP_ERROR_CHECK(load_users());
    }

    const esp_timer_create_args_t nvs_update_timer_args = {
        .callback = (void*)&fetch_and_store_time_in_nvs,
    };

    esp_timer_handle_t nvs_update_timer;
    ESP_ERROR_CHECK(esp_timer_create(&nvs_update_timer_args, &nvs_update_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(nvs_update_timer, TIME_PERIOD));

    gk_open_queue = xQueueCreate(GK_OPEN_QUEUE_LENGTH, GK_OPEN_ITEM_SIZE);
    gk_status_queue = xQueueCreate(GK_STATUS_QUEUE_LENGTH, GK_STATUS_ITEM_SIZE);

    xTaskCreate(&gatekeeper_gate_control_task, "gkControl", 2048, NULL, 5, NULL);
    xTaskCreate(&gatekeeper_telegram_task, "gkTelegram", 8192, NULL, 5, NULL);
}