#ifndef _TIME_SYNC_H_
#define _TIME_SYNC_H_

void initialize_sntp(void);
esp_err_t fetch_and_store_time_in_nvs(void *args);
esp_err_t update_time_from_nvs(void);

#endif // _TIME_SYNC_H_