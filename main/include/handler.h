#ifndef _HANDLER_H_
#define _HANDLER_H_

#include "freertos/queue.h"
#include "tg.h"

handler_response_t* gk_handler(char*, tg_update_t*, QueueHandle_t, QueueHandle_t);

#endif // _HANDLER_H_