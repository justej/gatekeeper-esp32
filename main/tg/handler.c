#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "handler.h"
#include "users.h"

#define GK_OPEN_QUEUE_TIMEOUT pdMS_TO_TICKS(10000)
#define GK_DELAY_1_HOUR pdMS_TO_TICKS(3600 * 1000)
#define GK_DELAY_1_5_SECONDS pdMS_TO_TICKS(1500)

static const char TAG[] = "handler";

static char resp_buf[512];

typedef char* (*message_handler_t)(const char* const, tg_message_t*, QueueHandle_t, QueueHandle_t);

typedef struct {
    const char* const command;
    message_handler_t handler;
} command_handler_t;

static char* open_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t delay = GK_DELAY_1_5_SECONDS;
    xQueueSend(open_queue, &delay, GK_OPEN_QUEUE_TIMEOUT);
    return "Gate has been opened";
}

static char* status_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t delay;
    xQueuePeek(status_queue, &delay, GK_OPEN_QUEUE_TIMEOUT);
    if (delay < 0) {
        sprintf(resp_buf, "Gate status: %li seconds left till closing\n", pdTICKS_TO_MS(-delay) / 1000);
        return resp_buf;
    } else {
        return "Gate is closed";
    }
}

static char* lock_opened_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t delay = GK_DELAY_1_HOUR;
    xQueueSend(open_queue, &delay, GK_OPEN_QUEUE_TIMEOUT);
    return "Gate has been locked for 1 hour";
}

static char* unlock_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t unlock_command = -1;
    xQueueSend(open_queue, &unlock_command, GK_OPEN_QUEUE_TIMEOUT);
    return "Gate has been unlocked";
}

static char* add_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t id = 0;
    sscanf(&buf[token->start], "%lli", &id);

    if (!is_admin(id)) {
        return "Unauthorized to add user";
    }

    token = message->text;
    sscanf(&buf[token->start], "/adduser %lli", &id);

    switch (user_add(id)) {
    case USER_OK:
        return "Added user";
    case USER_ALREADY_EXIST:
        return "User exists";
    case USER_NO_FREE_SPACE:
        return "Failed to add user: too many users";
    default:
        return "Unknown error";
    }
}

static char* drop_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t id = 0;
    sscanf(&buf[token->start], "%lli", &id);

    if (!is_admin(id)) {
        return "Unauthorized to drop user";
    }

    token = message->text;
    sscanf(&buf[token->start], "/dropuser %lli", &id);

    switch (user_drop(id)) {
    case USER_OK:
        sprintf(resp_buf, "Dropped user %lli", id);
        return resp_buf;
    case USER_NOT_FOUND:
        return "User not found";
    default:
        return "Unknown error";
    }
}

static char* list_users_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t id = 0;
    sscanf(&buf[token->start], "%lli", &id);

    if (!is_admin(id)) {
        return "Unauthorized to list users";
    }

    return users_list(resp_buf, sizeof(resp_buf));
}

static char* add_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t id = 0;
    sscanf(&buf[token->start], "%lli", &id);

    if (!is_admin(id)) {
        return "Unauthorized to add admin";
    }

    token = message->text;
    sscanf(&buf[token->start], "/addadmin %lli", &id);

    switch (admin_add(id)) {
    case USER_OK:
        return "Added admin";
    case USER_ALREADY_EXIST:
        return "Admin exists";
    case USER_NO_FREE_SPACE:
        return "Failed to add admin: too many admins";
    case USER_WRONG_ID:
        return "Wrong ID";
    default:
        return "Unknown error";
    }
}

static char* drop_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t id = 0;
    sscanf(&buf[token->start], "%lli", &id);

    if (!is_admin(id)) {
        return "Unauthorized to drop admin";
    }

    token = message->text;
    sscanf(&buf[token->start], "/dropadmin %lli", &id);

    if (admin_count() < 2) {
        return "At least one admin should remain";
    }

    switch (admin_drop(id)) {
    case USER_OK:
        sprintf(resp_buf, "Dropped admin %lli", id);
        return resp_buf;
    case USER_NOT_FOUND:
        return "Admin not found";
    default:
        return "Unknown error";
    }
}

static char* list_admins_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t id = 0;
    sscanf(&buf[token->start], "%lli", &id);

    if (!is_admin(id)) {
        return "Unauthorized to list admins";
    }

    return admins_list(resp_buf, sizeof(resp_buf));
}

command_handler_t command_handlers[] = {
    {"/open", open_handler},
    {"/status", status_handler},
    {"/lockopened", lock_opened_handler},
    {"/unlock", unlock_handler},
    {"/adduser", add_user_handler},
    {"/dropuser", drop_user_handler},
    {"/users", list_users_handler},
    {"/addadmin", add_admin_handler},
    {"/dropadmin", drop_admin_handler},
    {"/admins", list_admins_handler},
};

char* gk_handler(char* buf, tg_update_t* update, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    tg_log_token(buf, "handling update", update->id);

    jsmntok_t* text = update->message->text;
    if (text == NULL) return NULL;

    int message_size = text->end - text->start;
    for (int i = 0; i < sizeof(command_handlers) / sizeof(command_handlers[0]); i++) {
        int command_size = strlen(command_handlers[i].command);

        if (!strncmp(command_handlers[i].command, &buf[text->start], command_size) && (command_size == message_size || buf[text->start + command_size] == ' ')) {
            return command_handlers[i].handler(buf, update->message, open_queue, status_queue);
        }
    }

    ESP_LOGE(TAG, "unknown command %.*s", message_size, &buf[text->start]);

    return "Unknown command";
}
