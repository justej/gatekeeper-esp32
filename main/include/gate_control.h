#ifndef _GATE_CONTROL_H_
#define _GATE_CONTROL_H_

#include <stdint.h>
#include "freertos/queue.h"

#define GK_OPEN_QUEUE_LENGTH 3
#define GK_OPEN_ITEM_SIZE sizeof(int32_t)
#define GK_STATUS_QUEUE_LENGTH 1
#define GK_STATUS_ITEM_SIZE sizeof(int32_t)

typedef struct {
    uint32_t gate_poll;
    uint32_t gate_open_pulse_duration;
    uint32_t gate_open_duration;
    uint32_t gate_lock_duration;
    uint32_t open_gate_level;
} gate_control_config_t;

extern QueueHandle_t gk_open_queue;
extern QueueHandle_t gk_status_queue;

esp_err_t load_gate_config();
uint32_t cfg_get_gate_poll();
uint32_t cfg_get_gate_open_pulse_duration();
uint32_t cfg_get_gate_open_duration();
uint32_t cfg_get_gate_lock_duration();
uint32_t cfg_get_open_gate_level();
esp_err_t cfg_set_gate_poll(uint32_t value);
esp_err_t cfg_set_gate_open_pulse_duration(uint32_t value);
esp_err_t cfg_set_gate_open_duration(uint32_t value);
esp_err_t cfg_set_gate_lock_duration(uint32_t value);
esp_err_t cfg_set_open_gate_level(uint32_t value);
void startGateControl(QueueHandle_t open_queue, QueueHandle_t status_queue);

#endif // _GATE_CONTROL_H_