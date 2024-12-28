#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include "esp_log.h"
#include "nvs.h"
#include "users.h"
#include "secrets.h"

#ifndef ADMINS_INITIALIZER
#define ADMINS_INITIALIZER {}
#endif

#ifndef USERS_INITIALIZER
#define USERS_INITIALIZER {}
#endif

#define USER_FORMAT_STRING "id: %lli, username: %s, first name: %s, last name: %s\n"
#define USER_FORMAT_STRING_SIZE (sizeof(USER_FORMAT_STRING) - sizeof("%lli%s%s"))
#define USER_ID_MAX_LEN 16 // 52-bit value

#define STORAGE_NAMESPACE "users"

static char TAG[] = "users";

static user_t admins[10] = ADMINS_INITIALIZER;
static user_t users[100] = USERS_INITIALIZER;

static esp_err_t add(int64_t id, user_t* users, size_t size);
static esp_err_t drop(int64_t id, user_t* users, size_t size);
static char* list(char* buf, size_t buf_size, user_t* users, size_t size);
static size_t count(user_t* usr, size_t usr_size);
static esp_err_t get_nvs_key(char key[3], user_t* usr, size_t idx);
static esp_err_t store_if_changed(user_t* usr, size_t idx);
static esp_err_t erase(user_t* usr, size_t idx);

esp_err_t load_users() {
    nvs_handle_t nvs_handle = 0;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    user_t blob_content;
    size_t blob_size;
    char key[] = { 0, 0, 0 };

    for (size_t i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id != 0) continue;

        err = get_nvs_key(key, admins, i);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to build NVS key");
            goto exit;
        }

        err = nvs_get_blob(nvs_handle, key, &blob_content, &blob_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            admins[i].id = 0;
            continue;
        } else if (err != ESP_OK) {
            goto exit;
        }

        memcpy(&admins[i], &blob_content, sizeof(admins[0]));
    }

    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id != 0) continue;

        err = get_nvs_key(key, users, i);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to build NVS key");
            goto exit;
        }

        err = nvs_get_blob(nvs_handle, key, &blob_content, &blob_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            users[i].id = 0;
            continue;
        } else if (err != ESP_OK) {
            goto exit;
        }

        memcpy(&users[i], &blob_content, sizeof(users[0]));
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }

exit:
    if (nvs_handle != 0) {
        nvs_close(nvs_handle);
    }
    return err;
}

bool is_admin(int64_t id) {
    if (id == 0) {
        return false;
    }

    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            return true;
        }
    }

    return false;
}

esp_err_t user_add(int64_t id) {
    return add(id, users, sizeof(users) / sizeof(users[0]));
}

esp_err_t user_drop(int64_t id) {
    return drop(id, users, sizeof(users) / sizeof(users[0]));
}

char* users_list(char* buf, size_t buf_size) {
    char* resp = list(buf, buf_size, users, sizeof(users) / sizeof(users[0]));

    if (resp == NULL) {
        return "No users";
    }

    return resp;
}

size_t user_count() {
    return count(users, sizeof(users) / sizeof(users[0]));
}

esp_err_t admin_add(int64_t id) {
    return add(id, admins, sizeof(admins) / sizeof(admins[0]));
}

esp_err_t admin_drop(int64_t id) {
    return drop(id, admins, sizeof(admins) / sizeof(admins[0]));
}

char* admins_list(char* buf, size_t buf_size) {
    char* resp = list(buf, buf_size, admins, sizeof(admins) / sizeof(admins[0]));

    if (resp == NULL) {
        return "No admins";
    }

    return resp;
}

size_t admin_count() {
    return count(admins, sizeof(admins) / sizeof(admins[0]));
}

static size_t count(user_t* usr, size_t usr_size) {
    int counter = 0;
    for (int i = 0; i < usr_size; i++) {
        if (usr[i].id != 0) {
            counter++;
        }
    }
    return counter;
}

static esp_err_t add(int64_t id, user_t* usr, size_t usr_size) {
    if (id == 0) return ESP_ERR_USR_WRONG_ID;

    int_fast8_t empty = -1;
    for (int i = 0; i < usr_size; i++) {
        if (usr[i].id == id) {
            return ESP_ERR_USR_ALREADY_EXISTS;
        }
        if (usr[i].id == 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        usr[empty].id = id;
        store_if_changed(usr, empty);
        return ESP_OK;
    }

    return ESP_ERR_USR_NO_SPACE;
}

static esp_err_t drop(int64_t id, user_t* usr, size_t usr_size) {
    if (id == 0) return ESP_ERR_USR_WRONG_ID;

    for (size_t i = 0; i < usr_size; i++) {
        if (usr[i].id == id) {
            erase(usr, i);
            usr[i].id = 0;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static char* list(char* buf, size_t buf_size, user_t* usr, size_t usr_size) {
    size_t len = 1;
    for (size_t i = 0; i < usr_size; i++) {
        if (usr[i].id != 0) {
            user_t u = usr[i];
            if (len + USER_FORMAT_STRING_SIZE + USER_ID_MAX_LEN > buf_size) {
                return buf;
            }

            len += sprintf(buf + len - 1, USER_FORMAT_STRING, u.id, u.username, u.first_name, u.last_name);
        }
    }

    if (len == 1) return NULL;

    return buf;
}

static esp_err_t get_nvs_key(char key[3], user_t* usr, size_t idx) {
    if (idx > UCHAR_MAX) return ESP_ERR_INVALID_SIZE;

    if (usr == admins) {
        if (idx >= sizeof(admins) / sizeof(admins[0])) return ESP_ERR_INVALID_SIZE;
        key[0] = 'a';
    } else if (usr == users) {
        if (idx >= sizeof(users) / sizeof(users[0])) return ESP_ERR_INVALID_SIZE;
        key[0] = 'u';
    } else {
        ESP_LOGE(TAG, "Unknown user privilege");
        return ESP_FAIL;
    }

    key[1] = idx + 1;

    ESP_LOGI(TAG, "User storage key: %s", key);

    return ESP_OK;
}

static esp_err_t erase(user_t* usr, size_t idx) {
    if (usr[idx].id == 0) {
        ESP_LOGE(TAG, "ID can't be 0");
        return ESP_ERR_USR_WRONG_ID;
    }

    nvs_handle_t nvs_handle = 0;
    esp_err_t err;

    char key[] = { 0, 0, 0 };
    err = get_nvs_key(key, usr, idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build NVS key");
        goto exit;
    }

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    err = nvs_erase_key(nvs_handle, key);
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
        ESP_LOGE(TAG, "Error updating value in NVS: %i (%#x)", err, err);
    } else {
        ESP_LOGI(TAG, "Updated value in NVS");
    }
    return err;
}

static esp_err_t store_if_changed(user_t* usr, size_t idx) {
    if (usr[idx].id == 0) {
        ESP_LOGE(TAG, "ID can't be 0");
        return ESP_ERR_USR_WRONG_ID;
    }

    nvs_handle_t nvs_handle = 0;
    esp_err_t err;

    char key[] = { 0, 0, 0 };
    err = get_nvs_key(key, usr, idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build NVS key");
        goto exit;
    }

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        goto exit;
    }

    user_t blob_content;
    size_t blob_size;
    err = nvs_get_blob(nvs_handle, key, &blob_content, &blob_size);
    if ((err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) ||
        (blob_size == sizeof(blob_content) && memcmp(&blob_content, &usr[idx], sizeof(*usr)))) {
        goto exit;
    }

    err = nvs_set_blob(nvs_handle, key, &usr[idx], sizeof(*usr));
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
        ESP_LOGE(TAG, "Error updating value in NVS: %i (%#x)", err, err);
    } else {
        ESP_LOGI(TAG, "Updated value in NVS");
    }
    return err;
}