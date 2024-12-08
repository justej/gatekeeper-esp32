#ifndef _GATE_CONTROL_H_
#define _GATE_CONTROL_H_

#include "freertos/queue.h"

#define GK_OPEN_QUEUE_LENGTH 3
#define GK_OPEN_ITEM_SIZE sizeof(int32_t)
#define GK_STATUS_QUEUE_LENGTH 1
#define GK_STATUS_ITEM_SIZE sizeof(int32_t)

extern QueueHandle_t gk_open_queue;
extern QueueHandle_t gk_status_queue;

void startGateControl(QueueHandle_t open_queue, QueueHandle_t status_queue);

#endif // _GATE_CONTROL_H_