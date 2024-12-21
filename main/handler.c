#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "handler.h"

#define GK_OPEN_QUEUE_TIMEOUT pdMS_TO_TICKS(10000)
#define GK_DELAY_1_HOUR pdMS_TO_TICKS(3600 * 1000)
#define GK_DELAY_1_5_SECONDS pdMS_TO_TICKS(1500)

static const char TAG[] = "handler";

typedef char* (*message_handler_t)(const char* const, tg_message_t*, QueueHandle_t, QueueHandle_t);

typedef struct {
    const char* const command;
    message_handler_t handler;
} command_handler_t;

typedef struct {
    long id;
    char username[30];
    char first_name[30];
    char last_name[30];
} user_t;

static user_t admins[10] = { {.id = 842272533,} };
static user_t users[100] = { {.id = 552887516,} };
static char resp_buf[512];

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
    long id;
    sscanf(&buf[token->start], "%li", &id);

    bool unauthorized = true;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            unauthorized = false;
            break;
        }
    }

    if (unauthorized) {
        return "Unauthorized to add user";
    }

    token = message->text;
    sscanf(&buf[token->start], "/adduser %li", &id);

    int empty = -1;
    for (int i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id == id) {
            return "User exists";
        }
        if (users[i].id == 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        users[empty].id = id;
        return "Added user";
    }

    return "Failed to add user: too many users";
}

static char* drop_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    long id;
    sscanf(&buf[token->start], "%li", &id);

    bool unauthorized = true;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            unauthorized = false;
            break;
        }
    }

    if (unauthorized) {
        return "Unauthorized to drop user";
    }

    token = message->text;
    sscanf(&buf[token->start], "/dropuser %li", &id);

    for (int i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id == id) {
            users[i].id = 0;
            sprintf(resp_buf, "Dropped user %li", id);
            return resp_buf;
        }
    }

    return "User not found";
}

static char* list_users_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    long id;
    sscanf(&buf[token->start], "%li", &id);

    bool unauthorized = true;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            unauthorized = false;
            break;
        }
    }

    if (unauthorized) {
        return "Unauthorized to list users";
    }

    uint_fast16_t len = 1;
    for (int i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id != 0) {
            user_t u = users[i];
            len += sprintf(resp_buf + len - 1, "id: %li, username: %s, first name: %s, last name: %s\n", u.id, u.username, u.first_name, u.last_name);
        }
    }

    return resp_buf;
}

static char* add_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    long id;
    sscanf(&buf[token->start], "%li", &id);

    bool unauthorized = true;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            unauthorized = false;
            break;
        }
    }

    if (unauthorized) {
        return "Unauthorized to add admin";
    }

    token = message->text;
    sscanf(&buf[token->start], "/addadmin %li", &id);

    int empty = -1;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            return "Admin exists";
        }
        if (admins[i].id == 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        admins[empty].id = id;
        return "Added admin";
    }

    return "Too many admins";
}

static char* drop_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    long id;
    sscanf(&buf[token->start], "%li", &id);

    bool unauthorized = true;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            unauthorized = false;
            break;
        }
    }

    if (unauthorized) {
        return "Unauthorized to drop admin";
    }

    token = message->text;
    sscanf(&buf[token->start], "/dropadmin %li", &id);

    int count = 0;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id != 0 && admins[i].id != id) {
            count++;
            break;
        }
    }

    if (count < 3) {
        return "There should be at least two admins";
    }

    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            admins[i].id = 0;
            sprintf(resp_buf, "Dropped admin %li\n", id);
            return resp_buf;
        }
    }

    return "Admin not found";
}

static char* list_admins_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    long id;
    sscanf(&buf[token->start], "%li", &id);

    bool unauthorized = true;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            unauthorized = false;
            break;
        }
    }

    if (unauthorized) {
        return "Unauthorized to list admins";
    }

    uint_fast16_t len = 1;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id != 0) {
            user_t a = admins[i];
            len += printf(resp_buf + len - 1, "id: %li, username: %s, first name: %s, last name: %s\n", a.id, a.username, a.first_name, a.last_name);
        }
    }
    return resp_buf;
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
