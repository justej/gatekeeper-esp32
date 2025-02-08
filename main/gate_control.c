#include "freertos/FreeRTOS.h"

#include "nvs.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "gate_control.h"
#include "handler.h"
#include "tg.h"

#define STORAGE_NAMESPACE "gate"

#define CFG_NAME_POLL "poll"
#define CFG_NAME_OPEN_PULSE_DURATION "openpulsedur"
#define CFG_NAME_OPEN_DURATION "opendur"
#define CFG_NAME_LOCK_DURATION "lockdur"
#define CFG_NAME_OPEN_LEVEL "openlevel"

#define ACTIVE_OUTPUT_IDX_DISABLE_ALL 0
#define ACTIVE_OUTPUT_IDX_MAX (2 * TOTAL_GATES)

static const char TAG[] = "gate_control";

QueueHandle_t gk_open_queue;
QueueHandle_t gk_status_queue;

static gate_control_config_t config;

static esp_err_t store(char* name, uint32_t value);

typedef struct {
    uint32_t pin;
    TickType_t close_gate_at;
} gate_control_t;

typedef struct {
    char* name;
    uint32_t* value;
    uint32_t default_value;
} config_name_value_t;

gate_control_t gates[] = {
    {
        // upper gate
        .pin = GPIO_NUM_15,
        .close_gate_at = 0,
    },
    {
        // lower gate
        .pin = GPIO_NUM_4,
        .close_gate_at = 0,
    },
};

config_name_value_t config_name_value_default[] = {
    {.name = CFG_NAME_POLL, .value = &config.gate_poll, .default_value = pdMS_TO_TICKS(20)},
    {.name = CFG_NAME_OPEN_PULSE_DURATION, .value = &config.gate_open_pulse_duration,.default_value = pdMS_TO_TICKS(500)},
    {.name = CFG_NAME_OPEN_DURATION, .value = &config.gate_open_duration,.default_value = pdMS_TO_TICKS(2000)},
    {.name = CFG_NAME_LOCK_DURATION, .value = &config.gate_lock_duration, .default_value = pdMS_TO_TICKS(3600 * 1000)},
    {.name = CFG_NAME_OPEN_LEVEL, .value = &config.open_gate_level, .default_value = 1},
};

esp_err_t load_gate_config() {
    nvs_handle_t nvs_handle = 0;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    for (size_t i = 0; i < sizeof(config_name_value_default) / sizeof(config_name_value_default[0]); i++) {
        err = nvs_get_u32(nvs_handle, config_name_value_default[i].name, config_name_value_default[i].value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *(config_name_value_default[i].value) = config_name_value_default[i].default_value;
        } else if (err != ESP_OK) {
            goto exit;
        }
    }

    err = ESP_OK;
exit:
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }

    if (nvs_handle != 0) {
        nvs_close(nvs_handle);
    }
    return err;
}

uint32_t cfg_get_gate_poll() {
    return config.gate_poll;
}

uint32_t cfg_get_gate_open_pulse_duration() {
    return config.gate_open_pulse_duration;
}

uint32_t cfg_get_gate_open_duration() {
    return config.gate_open_duration;
}

uint32_t cfg_get_gate_lock_duration() {
    return config.gate_lock_duration;
}

uint32_t cfg_get_open_gate_level() {
    return config.open_gate_level;
}

esp_err_t cfg_set_gate_poll(uint32_t value) {
    if (value == config.gate_poll) return ESP_OK;

    esp_err_t err = store(CFG_NAME_POLL, value);
    if (err == ESP_OK) {
        config.gate_poll = value;
    }

    return err;
}

esp_err_t cfg_set_gate_open_pulse_duration(uint32_t value) {
    if (value == config.gate_open_pulse_duration) return ESP_OK;

    esp_err_t err = store(CFG_NAME_OPEN_PULSE_DURATION, value);
    if (err == ESP_OK) {
        config.gate_open_pulse_duration = value;
    }

    return err;
}

esp_err_t cfg_set_gate_open_duration(uint32_t value) {
    if (value == config.gate_open_duration) return ESP_OK;

    esp_err_t err = store(CFG_NAME_OPEN_DURATION, value);
    if (err == ESP_OK) {
        config.gate_open_duration = value;
    }

    return err;
}

esp_err_t cfg_set_gate_lock_duration(uint32_t value) {
    if (value == config.gate_lock_duration) return ESP_OK;

    esp_err_t err = store(CFG_NAME_LOCK_DURATION, value);
    if (err == ESP_OK) {
        config.gate_lock_duration = value;
    }

    return err;
}

esp_err_t cfg_set_open_gate_level(uint32_t value) {
    if (value == config.open_gate_level) return ESP_OK;

    esp_err_t err = store(CFG_NAME_OPEN_LEVEL, value);
    if (err == ESP_OK) {
        config.open_gate_level = value;
    }

    return err;
}

void startGateControl(QueueHandle_t open_queue, QueueHandle_t status_queue) {
    TickType_t change_level_at = 0;
    gate_delay_t gate_delay;
    size_t active_output_idx = ACTIVE_OUTPUT_IDX_DISABLE_ALL;

    while (42) {
        TickType_t now = xTaskGetTickCount();

        if (xQueueReceive(open_queue, &gate_delay, cfg_get_gate_poll())) {
            ESP_LOGI(TAG, "gate %i: got delay of %li", gate_delay.gate, gate_delay.delay);

            if (gate_delay.delay < 0) {
                gates[gate_delay.gate].close_gate_at = now - 1;
                change_level_at = now + 1;
            } else {
                TickType_t new_close_time = now + gate_delay.delay;
                // In order to make sure wraparound won't affect timestamp difference, the values are casted to signed integer
                if (((int32_t)new_close_time - (int32_t)gates[gate_delay.gate].close_gate_at) > 0) {
                    gates[gate_delay.gate].close_gate_at = new_close_time;
                }
            }
        }

        // The remote doesn't allow pressing both keys simultaneously so press the buttons one at a time
        // Build array of open gate requests: active = 1, inactive = 0
        uint32_t outputs = 0;
        for (size_t i = 0; i < TOTAL_GATES; i++) {
            if ((int32_t)now - (int32_t)gates[i].close_gate_at > 0) {
                gates[i].close_gate_at = now;
                continue;
            }
            outputs |= 1 << (2 * i + 1);
        }

        if (outputs == 0) {
            active_output_idx = ACTIVE_OUTPUT_IDX_DISABLE_ALL;
            change_level_at = now + 1;
        }

        if (((int32_t)now - (int32_t)change_level_at) > 0) {
            change_level_at = now + cfg_get_gate_open_pulse_duration();
            active_output_idx = (active_output_idx + 1) % ACTIVE_OUTPUT_IDX_MAX;
        }

        gpio_set_level(GPIO_LED_NUM, outputs & (1 << active_output_idx));
        for (size_t i = 0; i < TOTAL_GATES; i++) {
            uint32_t gate_mask = 1 << (2 * i + 1);
            uint_fast8_t gate_level = outputs & (1 << active_output_idx) & gate_mask;
            gpio_set_level(gates[i].pin, cfg_get_open_gate_level() ? gate_level : !gate_level);
        }

        int32_t lower_gate_time_left = (int32_t)now - (int32_t)gates[LOWER_GATE].close_gate_at;
        xQueueOverwrite(status_queue, &lower_gate_time_left);
    }
}

static esp_err_t store(char* name, uint32_t value) {
    if (name == NULL) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle = 0;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    err = nvs_set_u32(nvs_handle, name, value);
    if (err != ESP_OK) {
        goto exit;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        goto exit;
    }

exit:
    if (nvs_handle != 0) {
        nvs_close(nvs_handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error updating config '%s' in NVS: %i (%#x)", name, err, err);
    } else {
        ESP_LOGI(TAG, "Updated config '%s' in NVS", name);
    }
    return err;
}