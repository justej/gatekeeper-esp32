#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "handler.h"
#include "gate_control.h"
#include "users.h"

#define GK_OPEN_QUEUE_TIMEOUT pdMS_TO_TICKS(10000)

#define CMD_START "/start"
#define CMD_ADDUSER "/adduser"
#define CMD_DROPUSER "/dropuser"
#define CMD_USERS "/users"
#define CMD_ADDADMIN "/addadmin"
#define CMD_DROPADMIN "/dropadmin"
#define CMD_ADMINS "/admins"
#define CMD_CFGGATEPOLL "/cfggatepoll"
#define CMD_CFGOPENPULSEDURATION "/cfgopenpulseduration"
#define CMD_CFGOPENDURATION "/cfgopenduration"
#define CMD_CFGLOCKDURATION "/cfglockduration"
#define CMD_CFGOPENLEVEL "/cfgopenlevel"

static const char TAG[] = "handler";

static handler_response_t resp_batch_buf[MAX_ADMINS + 1];
static char resp_buf[(MAX_ADMINS + 1) * 512];

typedef handler_response_t* (*message_handler_t)(const char* const, tg_message_t*, QueueHandle_t, QueueHandle_t);

typedef struct {
    const char* const command;
    message_handler_t handler;
} command_handler_t;

static uint32_t tick_to_min(uint32_t tick) {
    return ((pdTICKS_TO_MS(tick) / 1000) + 30) / 60;
}

static handler_response_t compose_response(const char* buf, tg_message_t* message, char* text) {
    handler_response_t response = {
        .chat_id = &buf[message->chat->id->start],
        .text = text,
    };

    return response;
}

static handler_response_t* start_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "You're not authorized. Contact house committee");
    } else {
        uint32_t min = tick_to_min(cfg_get_gate_lock_duration());
        sprintf(resp_buf, "Welcome to Gate Keeper!\nHere you can:\n- open upper and lower gates\n- open and lock opened lower gate for %lu minutes. Don't forget to unlock it when you're done", min);
        *resp_batch_buf = compose_response(buf, message, resp_buf);
    }

    return resp_batch_buf;
}

static handler_response_t* help_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "You're not authorized. Contact house committee");
    } else {
        sprintf(resp_buf, "Gate Keeper allows you to:\n- open upper gate\n- open lower gate\n- lock the lower gate opened and unlock later\n- get status of the lower gate.\n\nIf you have any questions contact house committee.");
        *resp_batch_buf = compose_response(buf, message, resp_buf);
    }

    return resp_batch_buf;
}

static handler_response_t* settings_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "You're not authorized. Contact house committee");
    } else {
        uint32_t min = tick_to_min(cfg_get_gate_lock_duration());
        int32_t n = sprintf(resp_buf, "Gate Keeper settings:\n- lower gate lock period: %lu min", min);
        if (is_admin(user)) {
            sprintf(&resp_buf[n], "\n- polling period (" CMD_CFGGATEPOLL "): %lu msec\n- open pulse duration (" CMD_CFGOPENPULSEDURATION "): %lu msec\n- open cycle duration (" CMD_CFGOPENDURATION "): %lu msec\n- lock period duration (" CMD_CFGLOCKDURATION "): %lu msec\n- open level (" CMD_CFGOPENLEVEL "): %s",
                pdTICKS_TO_MS(cfg_get_gate_poll()), pdTICKS_TO_MS(cfg_get_gate_open_pulse_duration()), pdTICKS_TO_MS(cfg_get_gate_open_duration()), pdTICKS_TO_MS(cfg_get_gate_lock_duration()), cfg_get_open_gate_level() ? "high" : "low");
        }
        *resp_batch_buf = compose_response(buf, message, resp_buf);
    }

    return resp_batch_buf;
}

static handler_response_t* open_upper_gate_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized");
    } else {
        gate_delay_t gate_delay = {
            .delay = cfg_get_gate_open_duration(),
            .gate = UPPER_GATE,
        };
        xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);

        *resp_batch_buf = compose_response(buf, message, "Upper gate has been opened");
    }

    return resp_batch_buf;
}

static handler_response_t* open_lower_gate_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized");
    } else {
        gate_delay_t gate_delay = {
            .delay = cfg_get_gate_open_duration(),
            .gate = LOWER_GATE,
        };
        xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);
        *resp_batch_buf = compose_response(buf, message, "Lower gate has been opened");
    }

    return resp_batch_buf;
}

static handler_response_t* open_and_lock_lower_gate_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized");
    } else {
        gate_delay_t gate_delay = {
            .delay = cfg_get_gate_lock_duration(),
            .gate = LOWER_GATE,
        };
        xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);
        uint32_t min = tick_to_min(cfg_get_gate_lock_duration());
        sprintf(resp_buf, "Lower gate has been opened and locked for %lu minutes. Don't forget to unlock it when you're done", min);
        *resp_batch_buf = compose_response(buf, message, resp_buf);
    }

    return resp_batch_buf;
}

static handler_response_t* status_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized");
    } else {
        int32_t lower_gate_time_left;
        xQueuePeek(status_queue, &lower_gate_time_left, GK_OPEN_QUEUE_TIMEOUT);
        if (lower_gate_time_left < 0) {
            int32_t seconds_left = pdTICKS_TO_MS(-lower_gate_time_left) / 1000;
            int32_t min = seconds_left / 60;
            int32_t sec = seconds_left % 60;
            sprintf(resp_buf, "Lower gate status: %li m %li s left till closing\n", min, sec);
            *resp_batch_buf = compose_response(buf, message, resp_buf);
        } else {
            *resp_batch_buf = compose_response(buf, message, "Lower gate is closed");
        }
    }

    return resp_batch_buf;
}

static handler_response_t* unlock_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized");
    } else {
        gate_delay_t unlock_gate = {
            .delay = -1,
            .gate = LOWER_GATE,
        };
        xQueueSend(open_queue, &unlock_gate, GK_OPEN_QUEUE_TIMEOUT);
        *resp_batch_buf = compose_response(buf, message, "Gate has been unlocked");
    }

    return resp_batch_buf;
}

static handler_response_t* add_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to add user");
    } else {
        token = message->text;
        int64_t id = 0;
        sscanf(&buf[token->start], CMD_ADDUSER " %lli", &id);

        switch (user_add(id)) {
        case ESP_OK:
            *resp_batch_buf =  compose_response(buf, message, "Added user");
            break;
        case ESP_ERR_USR_ALREADY_EXISTS:
            *resp_batch_buf =  compose_response(buf, message, "User exists");
            break;
        case ESP_ERR_USR_NO_SPACE:
            *resp_batch_buf =  compose_response(buf, message, "Failed to add user: too many users");
            break;
        default:
            *resp_batch_buf = compose_response(buf, message, "Unknown error");
            break;
        }
    }

    return resp_batch_buf;
}

static handler_response_t* drop_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to drop user");
    } else {
        token = message->text;
        int64_t id = 0;
        sscanf(&buf[token->start], CMD_DROPUSER " %lli", &id);

        switch (user_drop(id)) {
        case ESP_OK:
            sprintf(resp_buf, "Dropped user %lli", id);
            *resp_batch_buf = compose_response(buf, message, resp_buf);
            break;
        case ESP_ERR_NOT_FOUND:
            *resp_batch_buf = compose_response(buf, message, "User not found");
            break;
        default:
            *resp_batch_buf = compose_response(buf, message, "Unknown error");
            break;
        }
    }

    return resp_batch_buf;
}

static handler_response_t* list_users_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to list users");
    } else {
        *resp_batch_buf = compose_response(buf, message, users_list(resp_buf, sizeof(resp_buf)));
    }

    return resp_batch_buf;
}

static handler_response_t* add_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to add admin");
    } else {
        token = message->text;
        int64_t id = 0;
        sscanf(&buf[token->start], CMD_ADDADMIN " %lli", &id);

        switch (admin_add(id)) {
        case ESP_OK:
            *resp_batch_buf = compose_response(buf, message, "Added admin");
            break;
        case ESP_ERR_USR_ALREADY_EXISTS:
            *resp_batch_buf = compose_response(buf, message, "Admin exists");
            break;
        case ESP_ERR_USR_NO_SPACE:
            *resp_batch_buf = compose_response(buf, message, "Failed to add admin: too many admins");
            break;
        case ESP_ERR_USR_WRONG_ID:
            *resp_batch_buf = compose_response(buf, message, "Wrong ID");
            break;
        default:
            *resp_batch_buf = compose_response(buf, message, "Unknown error");
            break;
        }
    }

    return resp_batch_buf;
}

static handler_response_t* drop_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to drop admin");
    } else {
        token = message->text;
        int64_t id = 0;
        sscanf(&buf[token->start], CMD_DROPADMIN " %lli", &id);

        if (admin_count() < 2) {
            *resp_batch_buf = compose_response(buf, message, "At least one admin should remain");
        } else {
            switch (admin_drop(id)) {
            case ESP_OK:
                sprintf(resp_buf, "Dropped admin %lli", id);
                *resp_batch_buf = compose_response(buf, message, resp_buf);
                break;
            case ESP_ERR_NOT_FOUND:
                *resp_batch_buf = compose_response(buf, message, "Admin not found");
                break;
            default:
                *resp_batch_buf = compose_response(buf, message, "Unknown error");
                break;
            }
        }
    }

    return resp_batch_buf;
}

static handler_response_t* list_admins_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to list admins");
    } else {
        *resp_batch_buf = compose_response(buf, message, admins_list(resp_buf, sizeof(resp_buf)));
    }

    return resp_batch_buf;
}

static handler_response_t* gate_poll_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to set duration");
    } else {
        token = message->text;
        uint32_t period = 0;
        sscanf(&buf[token->start], CMD_CFGGATEPOLL " %lu", &period);

        if (period == 0) {
            sprintf(resp_buf, "Gate polling period: %lu msec", pdTICKS_TO_MS(cfg_get_gate_poll()));
            *resp_batch_buf = compose_response(buf, message, resp_buf);
        } else {
            if (cfg_set_gate_poll(pdMS_TO_TICKS(period)) == ESP_OK) {
                sprintf(resp_buf, "Gate polling period set %lu msec", period);
                *resp_batch_buf = compose_response(buf, message, resp_buf);
            } else {
                *resp_batch_buf = compose_response(buf, message, "Failed to set duration");
            }
        }
    }

    return resp_batch_buf;
}

static handler_response_t* open_pulse_duration_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to set duration");
    } else {
        token = message->text;
        uint32_t duration = 0;
        sscanf(&buf[token->start], CMD_CFGOPENPULSEDURATION " %lu", &duration);

        if (duration == 0) {
            sprintf(resp_buf, "Gate open pulse duration: %lu msec", pdTICKS_TO_MS(cfg_get_gate_open_pulse_duration()));
            *resp_batch_buf = compose_response(buf, message, resp_buf);
        } else {
            if (cfg_set_gate_open_pulse_duration(pdMS_TO_TICKS(duration)) == ESP_OK) {
                sprintf(resp_buf, "Gate open pulse duration set %lu msec", duration);
                *resp_batch_buf = compose_response(buf, message, resp_buf);
            } else {
                *resp_batch_buf = compose_response(buf, message, "Failed to set duration");
            }
        }
    }

    return resp_batch_buf;
}

static handler_response_t* open_duration_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to set duration");
    } else {
        token = message->text;
        uint32_t duration = 0;
        sscanf(&buf[token->start], CMD_CFGOPENDURATION " %lu", &duration);

        if (duration == 0) {
            sprintf(resp_buf, "Gate open cycle duration: %lu msec", pdTICKS_TO_MS(cfg_get_gate_open_duration()));
            *resp_batch_buf = compose_response(buf, message, resp_buf);
        } else {
            if (cfg_set_gate_open_duration(pdMS_TO_TICKS(duration)) == ESP_OK) {
                sprintf(resp_buf, "Gate open cycle duration set %lu msec", duration);
                *resp_batch_buf = compose_response(buf, message, resp_buf);
            } else {
                *resp_batch_buf = compose_response(buf, message, "Failed to set duration");
            }
        }
    }

    return resp_batch_buf;
}

static handler_response_t* lock_duration_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to set duration");
    } else {
        token = message->text;
        uint32_t duration = 0;
        sscanf(&buf[token->start], CMD_CFGLOCKDURATION " %lu", &duration);

        if (duration == 0) {
            sprintf(resp_buf, "Gate lock period duration: %lu msec", pdTICKS_TO_MS(cfg_get_gate_lock_duration()));
            *resp_batch_buf = compose_response(buf, message, resp_buf);
        } else {
            if (cfg_set_gate_lock_duration(pdMS_TO_TICKS(duration)) == ESP_OK) {
                sprintf(resp_buf, "Gate lock period duration set %lu msec", duration);
                *resp_batch_buf = compose_response(buf, message, resp_buf);
            } else {
                *resp_batch_buf = compose_response(buf, message, "Failed to set duration");
            }
        }
    }

    return resp_batch_buf;
}

static handler_response_t* open_level_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        *resp_batch_buf = compose_response(buf, message, "Unauthorized to set duration");
    } else {
        token = message->text;
        uint32_t level = 0;
        sscanf(&buf[token->start], CMD_CFGOPENLEVEL " %lu", &level);
        level = level > 0;

        if (token->end - token->start <= sizeof(CMD_CFGOPENLEVEL)) {
            sprintf(resp_buf, "Gate open level: %s", cfg_get_open_gate_level() ? "high" : "low");
            *resp_batch_buf = compose_response(buf, message, resp_buf);
        } else {
            if (cfg_set_open_gate_level(level) == ESP_OK) {
                sprintf(resp_buf, "Gate open level set %s", level ? "high" : "low");
                *resp_batch_buf = compose_response(buf, message, resp_buf);
            } else {
                *resp_batch_buf = compose_response(buf, message, "Failed to set level");
            }
        }
    }

    return resp_batch_buf;
}

command_handler_t command_handlers[] = {
    {"Open upper gate", open_upper_gate_handler},
    {"Open lower gate", open_lower_gate_handler},
    {"Open and lock lower gate", open_and_lock_lower_gate_handler},
    {"Unlock lower gate", unlock_handler},
    {"Lower gate status", status_handler},

    {CMD_START, start_handler},
    {CMD_ADDUSER, add_user_handler},
    {CMD_DROPUSER, drop_user_handler},
    {CMD_USERS, list_users_handler},
    {CMD_ADDADMIN, add_admin_handler},
    {CMD_DROPADMIN, drop_admin_handler},
    {CMD_ADMINS, list_admins_handler},
    {CMD_CFGGATEPOLL, gate_poll_handler},
    {CMD_CFGOPENPULSEDURATION, open_pulse_duration_handler},
    {CMD_CFGOPENDURATION, open_duration_handler},
    {CMD_CFGLOCKDURATION, lock_duration_handler},
    {CMD_CFGOPENLEVEL, open_level_handler},
    {"/help", help_handler},
    {"/settings", settings_handler},
};

handler_response_t* gk_handler(char* buf, tg_update_t* update, QueueHandle_t open_queue, QueueHandle_t status_queue) {
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

    *resp_batch_buf = compose_response(buf, update->message, "Unknown command");

    return resp_batch_buf;
}
