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

typedef void (*message_handler_t)(tg_message_t* message);

typedef struct {
    char* command;
    message_handler_t handler;
} command_handler_t;

void open_handler(tg_message_t* message) {
    puts("gate's open");
}

void close_handler(tg_message_t* message) {
    puts("gate's close");
}

void lock_opened_handler(tg_message_t* message) {
    puts("gate's locked");
}

void unlock_handler(tg_message_t* message) {
    puts("gate's unlocked");
}

command_handler_t command_handlers[] = {
    {"/open", open_handler},
    {"/close", close_handler},
    {"/lockopened", lock_opened_handler},
    {"/unlock", unlock_handler},
};

static void handler(char* buf, tg_update_t* update) {
    tg_log_token(buf, "handling update", update->id);

    jsmntok_t* text = update->message->text;
    if (text == NULL) return;

    for (int i = 0; i < sizeof(command_handlers) / sizeof(command_handlers[0]); i++) {
        if (!strncmp(command_handlers[i].command, &buf[text->start], text->end - text->start)) {
            command_handlers[i].handler(update->message);
            return;
        }
    }

    ESP_LOGE(TAG, "unknown command %.*s", text->end - text->start, &buf[text->start]);
}

static void gatekeeper_gate_closer_task(void* pvparameters) {
    while (42) {
        // TODO: close the gates
    }
}

static void gatekeeper_telegram_task(void* pvparameters) {
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

    xTaskCreate(&gatekeeper_telegram_task, "gatekeeper", 8192, NULL, 5, NULL);
    xTaskCreate(&gatekeeper_gate_closer_task, "gatekeeper", 8192, NULL, 5, NULL);
}