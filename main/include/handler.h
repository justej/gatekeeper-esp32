#ifndef _HANDLER_H_
#define _HANDLER_H_

#include "freertos/queue.h"
#include "tg.h"

#define GPIO_GATE_NUM GPIO_NUM_2
#define GPIO_GATE_MASK (1 << GPIO_GATE_NUM)

char* gk_handler(char*, tg_update_t*, QueueHandle_t, QueueHandle_t);

#endif // _HANDLER_H_