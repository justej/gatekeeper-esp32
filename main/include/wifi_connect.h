#ifndef _WIFI_CONNECT_H_

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

esp_err_t connect_to_wifi(wifi_config_t*);
void disconnect_from_wifi(void);

#endif // _WIFI_CONNECT_H_