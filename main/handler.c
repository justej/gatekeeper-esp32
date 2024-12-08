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

typedef void (*message_handler_t)(char*, tg_message_t*, QueueHandle_t, QueueHandle_t);

typedef struct {
    char* command;
    message_handler_t handler;
} command_handler_t;

typedef struct {
    long id;
    char username[30];
    char first_name[30];
    char last_name[30];
} user_t;

user_t admins[10] = { {.id = 842272533,} };
user_t users[100] = { {.id = 552887516,} };

static void open_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t delay = GK_DELAY_1_5_SECONDS;
    xQueueSend(open_queue, &delay, GK_OPEN_QUEUE_TIMEOUT);
    puts("gate's opened");
}

static void status_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t delay;
    xQueuePeek(status_queue, &delay, GK_OPEN_QUEUE_TIMEOUT);
    if (delay < 0) {
        printf("gate status: %li seconds left till closing\n", pdTICKS_TO_MS(-delay) / 1000);
    } else {
        puts("gate's closed");
    }
}

static void lock_opened_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t delay = GK_DELAY_1_HOUR;
    xQueueSend(open_queue, &delay, GK_OPEN_QUEUE_TIMEOUT);
    puts("gate's locked for 1 hour");
}

static void unlock_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    int32_t unlock_command = -1;
    xQueueSend(open_queue, &unlock_command, GK_OPEN_QUEUE_TIMEOUT);
    puts("gate's unlocked");
}

static void add_user_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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
        puts("unauthorized to add user");
        return;
    }

    token = message->text;
    sscanf(&buf[token->start], "/adduser %li", &id);

    int empty = -1;
    for (int i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id == id) {
            puts("user exists");
            return;
        }
        if (users[i].id == 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        users[empty].id = id;
        return;
    }

    puts("too many users");
}

static void drop_user_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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
        puts("unauthorized to drop user");
        return;
    }

    token = message->text;
    sscanf(&buf[token->start], "/dropuser %li", &id);

    for (int i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id == id) {
            users[i].id = 0;
            printf("dropped user %li", id);
            return;
        }
    }
}

static void list_users_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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
        puts("unauthorized to list users");
        return;
    }

    for (int i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id != 0) {
            user_t u = users[i];
            printf("id: %li, username: %s, first name: %s, last name: %s\n", u.id, u.username, u.first_name, u.last_name);
        }
    }
}

static void add_admin_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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
        puts("unauthorized to add admin");
        return;
    }

    token = message->text;
    sscanf(&buf[token->start], "/addadmin %li", &id);

    int empty = -1;
    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            puts("admin exists");
            return;
        }
        if (admins[i].id == 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        admins[empty].id = id;
        return;
    }

    puts("too many admins");
}

static void drop_admin_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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
        puts("unauthorized to drop admin");
        return;
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
        puts("There should be at least two admins");
        return;
    }

    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id == id) {
            admins[i].id = 0;
            printf("dropped admin %li\n", id);
            return;
        }
    }
}

static void list_admins_handler(char* buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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
        puts("unauthorized to list admins");
        return;
    }

    for (int i = 0; i < sizeof(admins) / sizeof(admins[0]); i++) {
        if (admins[i].id != 0) {
            user_t a = admins[i];
            printf("id: %li, username: %s, first name: %s, last name: %s\n", a.id, a.username, a.first_name, a.last_name);
        }
    }
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

void handler(char* buf, tg_update_t* update, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    tg_log_token(buf, "handling update", update->id);

    jsmntok_t* text = update->message->text;
    if (text == NULL) return;

    int message_size = text->end - text->start;
    for (int i = 0; i < sizeof(command_handlers) / sizeof(command_handlers[0]); i++) {
        int command_size = strlen(command_handlers[i].command);

        if (!strncmp(command_handlers[i].command, &buf[text->start], command_size) && (command_size == message_size || buf[text->start + command_size] == ' ')) {
            command_handlers[i].handler(buf, update->message, open_queue, status_queue);
            return;
        }
    }

    ESP_LOGE(TAG, "unknown command %.*s", message_size, &buf[text->start]);
}
