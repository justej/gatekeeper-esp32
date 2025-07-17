#ifndef _USERS_H_
#define _USERS_H_

#include <stdint.h>
#include "esp_err.h"

#define MAX_ADMINS 10
#define MAX_USERS 100

#define ESP_ERR_USR_ALREADY_EXISTS (-1)
#define ESP_ERR_USR_NO_SPACE (-2)
#define ESP_ERR_USR_WRONG_ID (-3)

typedef struct {
    int64_t id; // 52-bit value
    char username[32];
    char first_name[32];
    char last_name[32];
} user_t;

esp_err_t load_users();
bool is_admin(int64_t id);
bool is_user(int64_t id);
bool is_authorized(int64_t id);
esp_err_t user_add(int64_t id);
esp_err_t user_drop(int64_t id);
char* users_list(char* buf, size_t buf_size);
size_t user_count();
esp_err_t admin_add(int64_t id);
esp_err_t admin_drop(int64_t id);
char* admins_list(char* buf, size_t buf_size);
size_t admin_count();
size_t get_admin_ids(int64_t* buf, size_t buf_size);

#endif // _USERS_H_
