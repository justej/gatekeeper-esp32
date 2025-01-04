#ifndef _HANDLER_H_
#define _HANDLER_H_

#include "freertos/queue.h"
#include "tg.h"

#define GPIO_LED_NUM GPIO_NUM_2
#define GPIO_GATE_UPPER_NUM GPIO_NUM_15
#define GPIO_GATE_LOWER_NUM GPIO_NUM_4
#define GPIO_GATE_MASK ((1 << GPIO_GATE_UPPER_NUM) | (1 << GPIO_GATE_LOWER_NUM) | (1 << GPIO_LED_NUM))

char* gk_handler(char*, tg_update_t*, QueueHandle_t, QueueHandle_t);

#endif // _HANDLER_H_