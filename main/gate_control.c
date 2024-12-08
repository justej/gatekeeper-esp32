#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "driver/gpio.h"

#include "gate_control.h"
#include "handler.h"
#include "tg.h"

#define GATE_STATUS_OPENED (1 << 0)
#define LEVEL_GATE_CLOSE 1
#define GATE_PULSE_WIDTH pdMS_TO_TICKS(500)
#define GATE_POLL_TIMEOUT pdMS_TO_TICKS(20)

static const char TAG[] = "gate_control";

QueueHandle_t gk_open_queue;
QueueHandle_t gk_status_queue;

static TickType_t close_gate_at = 0;
static TickType_t change_level_at = 0;

void startGateControl(QueueHandle_t open_queue, QueueHandle_t status_queue) {
    uint32_t level = LEVEL_GATE_CLOSE;
    int32_t delay;

    // In order to make sure wraparound won't affect timestamp difference, the values are casted to signed integer
    while (42) {
        TickType_t now = xTaskGetTickCount();

        if (xQueueReceive(open_queue, &delay, GATE_POLL_TIMEOUT)) {
            ESP_LOGI(TAG, "got delay of %li", delay);

            if (delay < 0) {
                close_gate_at = now - 1;
                change_level_at = now + 1;
            } else {
                TickType_t new_close_time = now + delay;
                if (((int32_t)new_close_time - (int32_t)close_gate_at) > 0) {
                    close_gate_at = new_close_time;
                }
            }
        }

        int32_t diff = (int32_t)now - (int32_t)close_gate_at;
        if (diff > 0) {
            level = LEVEL_GATE_CLOSE;
            change_level_at = now + 1;
        }

        if (((int32_t)now - (int32_t)change_level_at) > 0) {
            change_level_at = now + GATE_PULSE_WIDTH;
            level = !level;
        }

        gpio_set_level(GPIO_GATE_NUM, level);
        xQueueOverwrite(status_queue, &diff);
    }
}