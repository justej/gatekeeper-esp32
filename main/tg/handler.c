#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "handler.h"
#include "gate_control.h"
#include "users.h"

#define GK_OPEN_QUEUE_TIMEOUT pdMS_TO_TICKS(10000)

static const char TAG[] = "handler";

static char resp_buf[512];

typedef char* (*message_handler_t)(const char* const, tg_message_t*, QueueHandle_t, QueueHandle_t);

typedef struct {
    const char* const command;
    message_handler_t handler;
} command_handler_t;

static char* start_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "You're not authorized. Contact house committee";
    }

    int32_t min = ((pdTICKS_TO_MS(cfg_get_gate_lock_duration()) / 1000) + 30) / 60;
    sprintf(resp_buf, "Welcome to Gate Keeper!\nHere you can:\n- open upper and lower gates\n- open and lock opened the lower gate for %li minutes. Don't forget to unlock it when you're done", min);

    return resp_buf;
}

static char* open_upper_gate_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "Unauthorized";
    }

    gate_delay_t gate_delay = {
        .delay = cfg_get_gate_open_duration(),
        .gate = UPPER_GATE,
    };
    xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);
    return "Upper gate has been opened";
}

static char* open_lower_gate_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "Unauthorized";
    }

    gate_delay_t gate_delay = {
        .delay = cfg_get_gate_open_duration(),
        .gate = LOWER_GATE,
    };
    xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);
    return "Lower gate has been opened";
}

static char* open_and_lock_lower_gate_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "Unauthorized";
    }

    gate_delay_t gate_delay = {
        .delay = cfg_get_gate_lock_duration(),
        .gate = LOWER_GATE,
    };
    xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);
    int32_t min = ((pdTICKS_TO_MS(cfg_get_gate_lock_duration()) / 1000) + 30) / 60;
    sprintf(resp_buf, "Lower gate has been opened and locked for %li minutes. Don't forget to unlock it when you're done", min);

    return resp_buf;
}

static char* status_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "Unauthorized";
    }

    int32_t lower_gate_time_left;
    xQueuePeek(status_queue, &lower_gate_time_left, GK_OPEN_QUEUE_TIMEOUT);
    if (lower_gate_time_left < 0) {
        int32_t seconds_left = pdTICKS_TO_MS(-lower_gate_time_left) / 1000;
        int32_t min = seconds_left / 60;
        int32_t sec = seconds_left % 60;
        sprintf(resp_buf, "Lower gate status: %li m %li s left till closing\n", min, sec);
        return resp_buf;
    } else {
        return "Lower gate is closed";
    }
}

static char* lock_lower_opened_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "Unauthorized";
    }

    gate_delay_t gate_delay = {
        .delay = cfg_get_gate_lock_duration(),
        .gate = LOWER_GATE,
    };
    xQueueSend(open_queue, &gate_delay, GK_OPEN_QUEUE_TIMEOUT);
    int32_t min = ((pdTICKS_TO_MS(gate_delay.delay) / 1000) + 30) / 60;
    sprintf(resp_buf, "Lower gate has been locked for %li minutes\n", min);
    return resp_buf;
}

static char* unlock_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t user = 0;
    sscanf(&buf[token->start], "%lli", &user);

    if (!is_authorized(user)) {
        return "Unauthorized";
    }

    gate_delay_t unlock_gate = {
        .delay = -1,
        .gate = LOWER_GATE,
    };
    xQueueSend(open_queue, &unlock_gate, GK_OPEN_QUEUE_TIMEOUT);
    return "Gate has been unlocked";
}

static char* add_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to add user";
    }

    token = message->text;
    int64_t id = 0;
    sscanf(&buf[token->start], "/adduser %lli", &id);

    switch (user_add(id)) {
    case ESP_OK:
        return "Added user";
    case ESP_ERR_USR_ALREADY_EXISTS:
        return "User exists";
    case ESP_ERR_USR_NO_SPACE:
        return "Failed to add user: too many users";
    default:
        return "Unknown error";
    }
}

static char* drop_user_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to drop user";
    }

    token = message->text;
    int64_t id = 0;
    sscanf(&buf[token->start], "/dropuser %lli", &id);

    switch (user_drop(id)) {
    case ESP_OK:
        sprintf(resp_buf, "Dropped user %lli", id);
        return resp_buf;
    case ESP_ERR_NOT_FOUND:
        return "User not found";
    default:
        return "Unknown error";
    }
}

static char* list_users_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to list users";
    }

    return users_list(resp_buf, sizeof(resp_buf));
}

static char* add_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to add admin";
    }

    token = message->text;
    int64_t id = 0;
    sscanf(&buf[token->start], "/addadmin %lli", &id);

    switch (admin_add(id)) {
    case ESP_OK:
        return "Added admin";
    case ESP_ERR_USR_ALREADY_EXISTS:
        return "Admin exists";
    case ESP_ERR_USR_NO_SPACE:
        return "Failed to add admin: too many admins";
    case ESP_ERR_USR_WRONG_ID:
        return "Wrong ID";
    default:
        return "Unknown error";
    }
}

static char* drop_admin_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to drop admin";
    }

    token = message->text;
    int64_t id = 0;
    sscanf(&buf[token->start], "/dropadmin %lli", &id);

    if (admin_count() < 2) {
        return "At least one admin should remain";
    }

    switch (admin_drop(id)) {
    case ESP_OK:
        sprintf(resp_buf, "Dropped admin %lli", id);
        return resp_buf;
    case ESP_ERR_NOT_FOUND:
        return "Admin not found";
    default:
        return "Unknown error";
    }
}

static char* list_admins_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to list admins";
    }

    return admins_list(resp_buf, sizeof(resp_buf));
}

static char* gate_poll_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to set duration";
    }

    token = message->text;
    uint32_t period = 0;
    sscanf(&buf[token->start], "/cfggatepoll %lu", &period);

    if (period == 0) {
        sprintf(resp_buf, "Gate poll: %li ms", pdTICKS_TO_MS(cfg_get_gate_poll()));
    } else {
        if (cfg_set_gate_poll(pdMS_TO_TICKS(period)) == ESP_OK) {
            sprintf(resp_buf, "Gate poll period set %lu ms", period);
        } else {
            return "Failed to set duration";
        }
    }

    return resp_buf;
}

static char* open_pulse_duration_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to set duration";
    }

    token = message->text;
    uint32_t duration = 0;
    sscanf(&buf[token->start], "/cfgopenpulseduration %lu", &duration);

    if (duration == 0) {
        sprintf(resp_buf, "Gate open pulse duration: %li ms", pdTICKS_TO_MS(cfg_get_gate_open_pulse_duration()));
    } else {
        if (cfg_set_gate_open_pulse_duration(pdMS_TO_TICKS(duration)) == ESP_OK) {
            sprintf(resp_buf, "Gate open pulse duration set %lu ms", duration);
        } else {
            return "Failed to set duration";
        }
    }

    return resp_buf;
}

static char* open_duration_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to set duration";
    }

    token = message->text;
    uint32_t duration = 0;
    sscanf(&buf[token->start], "/cfgopenduration %lu", &duration);

    if (duration == 0) {
        sprintf(resp_buf, "Gate open duration: %li ms", pdTICKS_TO_MS(cfg_get_gate_open_duration()));
    } else {
        if (cfg_set_gate_open_duration(pdMS_TO_TICKS(duration)) == ESP_OK) {
            sprintf(resp_buf, "Gate open duration set %lu ms", duration);
        } else {
            return "Failed to set duration";
        }
    }

    return resp_buf;
}

static char* lock_duration_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to set duration";
    }

    token = message->text;
    uint32_t duration = 0;
    sscanf(&buf[token->start], "/cfglockduration %lu", &duration);

    if (duration == 0) {
        sprintf(resp_buf, "Gate lock duration: %lu ms", pdTICKS_TO_MS(cfg_get_gate_lock_duration()));
    } else {
        if (cfg_set_gate_lock_duration(pdMS_TO_TICKS(duration)) == ESP_OK) {
            sprintf(resp_buf, "Gate lock opened duration set %lu ms", duration);
        } else {
            return "Failed to set duration";
        }
    }

    return resp_buf;
}

static char* open_level_handler(const char* const buf, tg_message_t* message, QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmntok_t* token = message->from->id;
    int64_t admin_id = 0;
    sscanf(&buf[token->start], "%lli", &admin_id);

    if (!is_admin(admin_id)) {
        return "Unauthorized to set duration";
    }

    token = message->text;
    uint32_t level = 0;
    sscanf(&buf[token->start], "/cfgopenlevel %lu", &level);

    if (token->end - token->start <= sizeof("/cfgopenlevel")) {
        sprintf(resp_buf, "Gate open level: %s", cfg_get_open_gate_level() ? "high" : "low");
    } else {
        if (cfg_set_open_gate_level(level) == ESP_OK) {
            sprintf(resp_buf, "Open level set %s", level ? "high" : "low");
        } else {
            return "Failed to set level";
        }
    }

    return resp_buf;
}

command_handler_t command_handlers[] = {
    {"/start", start_handler},
    {"Open upper gate", open_upper_gate_handler},
    {"Open lower gate", open_lower_gate_handler},
    {"Open and lock lower gate", open_and_lock_lower_gate_handler},
    {"Lower gate status", status_handler},
    {"Unlock lower gate", unlock_handler},

    {"/open", open_upper_gate_handler},
    {"/status", status_handler},
    {"/lockopened", lock_lower_opened_handler},
    {"/unlock", unlock_handler},
    {"/adduser", add_user_handler},
    {"/dropuser", drop_user_handler},
    {"/users", list_users_handler},
    {"/addadmin", add_admin_handler},
    {"/dropadmin", drop_admin_handler},
    {"/admins", list_admins_handler},
    {"/cfggatepoll", gate_poll_handler},
    {"/cfgopenpulseduration", open_pulse_duration_handler},
    {"/cfgopenduration", open_duration_handler},
    {"/cfglockduration", lock_duration_handler},
    {"/cfgopenlevel", open_level_handler},
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
