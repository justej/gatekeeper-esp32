#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "jsmn.h"
#include "tg.h"

// #define TG_DEBUG

#define AS_STRING(x) #x

#define TG_UPDATES_COUNT 5
#define TOK_LEN 512

#define HOST_NAME "api.telegram.org"
#define WEB_SERVER_URL "https://" HOST_NAME
#define WEB_PORT "443"

#define GET_MESSAGES_FORMAT_STRING "GET /bot%s/getUpdates?offset=%li&limit=5 HTTP/1.1\r\n" \
    "Host: " HOST_NAME "\r\n" \
    "User-Agent: esp-idf/1.0 esp32\r\n" \
    "Connection: close\r\n\r\n"

#define SEND_MESSAGE_BODY_FORMAT_STRING "{\"reply_markup\":{\"keyboard\":[[{\"text\":\"open upper gate\"},{\"text\":\"open lower gate\"}],[{\"text\":\"unlock lower gate\"},{\"text\":\"open and lock lower gate\"}],[{\"text\":\"lower gate status\"}]]},\"chat_id\":%s,\"text\":\"%s\"}\r\n"

#define SEND_MESSAGE_FORMAT_STRING "POST /bot%s/sendMessage HTTP/1.1\r\n" \
    "Host: " HOST_NAME "\r\n" \
    "User-Agent: esp-idf/1.0 esp32\r\n" \
    "Connection: close\r\n" \
    "Content-Type: application/json\r\n" \
    "Content-length: %i\r\n\r\n" \
    SEND_MESSAGE_BODY_FORMAT_STRING

typedef struct {
    char bot_token[46];
    int64_t update_id;
    bool initialized;
    esp_tls_cfg_t tls_cfg;
} tg_config_t;


static int https_send_request(char*, int, esp_tls_cfg_t, const char*, const char*);

static jsmntok_t tokens[TOK_LEN];
static char req_buf[4096];
static char resp_buf[4096];
static char request[1024]; // make sure the request fits this size because currently it's almost overflown

static const char TAG[] = "tg";

tg_config_t tg_config = {
    .bot_token = "",
    .update_id = -1,
    .tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    },
    .initialized = false,
};

#ifdef TG_DEBUG
static char* token_type(jsmntype_t t) {
    switch (t) {
    case JSMN_UNDEFINED: return "UNDEFINED";
    case JSMN_OBJECT: return "OBJECT";
    case JSMN_ARRAY: return "ARRAY";
    case JSMN_STRING: return "STRING";
    case JSMN_PRIMITIVE: return "PRIMITIVE";
    }
    return "_UNKNOWN_";
}

#define __JSMNLOG(buf, tokens, i_tok) do { \
            buf[tokens[i_tok].end] = 0; \
            ESP_LOGI(TAG, "type: %s, start: %i, end: %i, size: %i", token_type(tokens[i_tok].type), tokens[i_tok].start, tokens[i_tok].end, tokens[i_tok].size); \
            ESP_LOGI(TAG, "i_tok: %i, len: %i,  value: %s", i_tok, tokens[i_tok].end - tokens[i_tok].start, &buf[tokens[i_tok].start]); \
        } while (0)

static void tg_log(char* buf, tg_update_t* update) {
    jsmntok_t* token;
    if (update == NULL) return;

    token = update->id;
    tg_log_token(buf, "update_id", token);

    if (update->message != NULL) {
        token = update->message->id;
        tg_log_token(buf, "message_id", token);

        tg_user_t* user = update->message->from;
        if (user != NULL) {
            tg_log_token(buf, "from id", user->id);
            tg_log_token(buf, "from is_bot", user->is_bot);
            tg_log_token(buf, "from first_name", user->first_name);
            tg_log_token(buf, "from last_name", user->last_name);
            tg_log_token(buf, "from username", user->username);
        }

        tg_chat_t* chat = update->message->chat;
        if (chat != NULL) {
            tg_log_token(buf, "chat id", chat->id);
            tg_log_token(buf, "chat is_bot", chat->type);
            tg_log_token(buf, "chat first_name", chat->first_name);
            tg_log_token(buf, "chat last_name", chat->last_name);
            tg_log_token(buf, "chat username", chat->username);
        }

        tg_log_token(buf, "text", update->message->text);
    }
}
#else
#define __JSMNLOG(buf, tokens, i_tok)
#endif

void tg_log_token(char* buf, char* key, jsmntok_t* token) {
    if (token == NULL) return;

    printf("%s: %s\n", key, &buf[token->start]);
}

static bool jsmn_strcmp(char* buf, jsmntok_t* token, char* str) {
    return token->type == JSMN_STRING && !strncmp(buf + token->start, str, token->end - token->start);
}

static bool jsmn_boolcmp(char* buf, jsmntok_t* token, bool b) {
    return token->type == JSMN_PRIMITIVE && ((buf[token->start] == 't' && b) || (buf[token->start] == 'f' && !b));
}

static bool jsmn_is_str(jsmntok_t* token) {
    return token->type == JSMN_STRING;
}

static bool jsmn_is_bool(char* buf, jsmntok_t* token) {
    return token->type == JSMN_PRIMITIVE && (buf[token->start] == 't' || buf[token->start] == 'f');
}

static bool jsmn_is_int(char* buf, jsmntok_t* token) {
    char c = buf[token->start];
    return token->type == JSMN_PRIMITIVE && c != 't' && c != 'f' && c != 'n';
}

static void skip_tokens(jsmntok_t* tokens, int parsed_len, int* i_tok) {
    if (*i_tok >= parsed_len) {
        return;
    }

    int size = tokens[*i_tok].size;
    switch (tokens[*i_tok].type) {
    case JSMN_STRING:
        (*i_tok)++;
        if (size) {
            skip_tokens(tokens, parsed_len, i_tok);
        }
        break;

    case JSMN_OBJECT:
        // fall through
    case JSMN_ARRAY:
        (*i_tok)++;
        for (int i = 0; i < size; i++) {
            skip_tokens(tokens, parsed_len, i_tok);
        }
        break;

    case JSMN_UNDEFINED:
        // fall through
    case JSMN_PRIMITIVE:
        (*i_tok)++;
        break;
    }
}

static bool parse_user(tg_user_t* user, char* buf, jsmntok_t* tokens, int parsed_len, int* i_tok) {
    int size = tokens[*i_tok].size;

    __JSMNLOG(buf, tokens, *i_tok);
    if (tokens[*i_tok].type != JSMN_OBJECT) {
        return false;
    }
    (*i_tok)++;

    for (int i = 0; i < size && *i_tok < parsed_len; i++) {
        __JSMNLOG(buf, tokens, *i_tok);
        if (jsmn_strcmp(buf, &tokens[*i_tok], "id")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_int(buf, &tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                user->id = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "first_name")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                user->first_name = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "last_name")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                user->last_name = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "username")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                user->username = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "is_bot")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_bool(buf, &tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                user->is_bot = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        skip_tokens(tokens, parsed_len, i_tok);
    }

    return true;
}

static bool parse_chat(tg_chat_t* chat, char* buf, jsmntok_t* tokens, int parsed_len, int* i_tok) {
    int size = tokens[*i_tok].size;

    __JSMNLOG(buf, tokens, *i_tok);
    if (tokens[*i_tok].type != JSMN_OBJECT) {
        return false;
    }
    (*i_tok)++;

    for (int i = 0; i < size && *i_tok < parsed_len; i++) {
        __JSMNLOG(buf, tokens, *i_tok);
        if (jsmn_strcmp(buf, &tokens[*i_tok], "id")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_int(buf, &tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                chat->id = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "type")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                chat->type = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "first_name")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                chat->first_name = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "last_name")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                chat->last_name = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "username")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                chat->username = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        skip_tokens(tokens, parsed_len, i_tok);
    }

    return true;
}

static bool parse_message(tg_message_t* message, char* buf, jsmntok_t* tokens, int parsed_len, int* i_tok) {
    int size = tokens[*i_tok].size;

    __JSMNLOG(buf, tokens, *i_tok);
    if (tokens[*i_tok].type != JSMN_OBJECT) {
        return false;
    }
    (*i_tok)++;

    for (int i = 0; i < size && *i_tok < parsed_len; i++) {
        __JSMNLOG(buf, tokens, *i_tok);
        if (jsmn_strcmp(buf, &tokens[*i_tok], "message_id")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_int(buf, &tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                message->id = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "from")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (parse_user(message->from, buf, tokens, parsed_len, i_tok)) {
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "chat")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (parse_chat(message->chat, buf, tokens, parsed_len, i_tok)) {
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "text")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                message->text = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        skip_tokens(tokens, parsed_len, i_tok);
    }

    return true;
}

static bool parse_update(tg_update_t* update, char* buf, jsmntok_t* tokens, int parsed_len, int* i_tok) {
    int size = tokens[*i_tok].size;

    __JSMNLOG(buf, tokens, *i_tok);
    if (tokens[*i_tok].type != JSMN_OBJECT) {
        return false;
    }
    (*i_tok)++;

    for (int i = 0; i < size && *i_tok < parsed_len; i++) {
        __JSMNLOG(buf, tokens, *i_tok);
        if (jsmn_strcmp(buf, &tokens[*i_tok], "update_id")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_int(buf, &tokens[*i_tok])) {
                buf[tokens[*i_tok].end] = '\0';
                update->id = &tokens[*i_tok];
                (*i_tok)++;
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "message")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (parse_message(update->message, buf, tokens, parsed_len, i_tok)) {
                continue;
            } else {
                return false;
            }
        }

        skip_tokens(tokens, parsed_len, i_tok);
    }

    return true;
}

static void handle_updates(char* buf, int buf_size, char* update_handler(char*, tg_update_t*, QueueHandle_t, QueueHandle_t), QueueHandle_t open_queue, QueueHandle_t status_queue) {
    jsmn_parser parser;

    jsmn_init(&parser);
    int parsed_len = jsmn_parse(&parser, buf, buf_size, tokens, TOK_LEN);

    if (parsed_len < 0) {
        ESP_LOGE(TAG, "JSON error: %i", parsed_len);
        return;
    }

    int i_tok = 0;
#ifdef TG_DEBUG
    for (i_tok = 0; i_tok < parsed_len; i_tok++) {
        __JSMNLOG(buf, tokens, i_tok);
    }
    ESP_LOGI(TAG, "=====");
#endif

    // first object
    if (tokens[i_tok].type != JSMN_OBJECT) {
        return;
    }
    i_tok++;

    for (int i = 0; i < 2 && i_tok < parsed_len; i++) {
        if (jsmn_strcmp(buf, &tokens[i_tok], "ok")) {
            i_tok++;
            __JSMNLOG(buf, tokens, i_tok);
            if (!jsmn_boolcmp(buf, &tokens[i_tok], true)) {
                ESP_LOGE(TAG, "ok != true");
                return;
            }
            i_tok++;
            continue;
        }

        if (jsmn_strcmp(buf, &tokens[i_tok], "result")) {
            i_tok++;
            __JSMNLOG(buf, tokens, i_tok);
            if (tokens[i_tok].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "result is not array");
                return;
            }
            int size = tokens[i_tok].size;
            i_tok++;
            for (int i = 0; i < size && i_tok < parsed_len; i++) {
                tg_chat_t chat_buf = {
                    .id = NULL,
                    .type = NULL,
                    .first_name = NULL,
                    .last_name = NULL,
                    .username = NULL,
                };
                tg_user_t user_buf = {
                    .id = NULL,
                    .is_bot = NULL,
                    .first_name = NULL,
                    .last_name = NULL,
                    .username = NULL,
                };
                tg_message_t message_buf = {
                    .id = NULL,
                    .from = &user_buf,
                    .chat = &chat_buf,
                    .reply_to_message = NULL,
                    .text = NULL,
                };
                tg_update_t update = {
                    .id = NULL,
                    .message = &message_buf
                };

                ESP_LOGI(TAG, "update %i / %i", i + 1, size);
                if (parse_update(&update, buf, tokens, parsed_len, &i_tok)) {
                    tg_config.update_id = atol(&buf[update.id->start]);

                    char* resp = update_handler(buf, &update, open_queue, status_queue);
                    if (resp == NULL) continue;

                    int ret = tg_send_message(&buf[update.message->chat->id->start], resp);
                    if (ret <= 0) {
                        ESP_LOGE(TAG, "Failed sending message with code %i", ret);
                    }
                }
            }
            continue;
        }

        skip_tokens(tokens, parsed_len, &i_tok);
    }
}

static void tg_parse(char* buf, int buf_len, char* update_handler(char*, tg_update_t*, QueueHandle_t, QueueHandle_t), QueueHandle_t open_queue, QueueHandle_t status_queue) {
    if (buf_len < 4) {
        return;
    }

    for (int start_pos = 0; start_pos < buf_len; start_pos++) {
        // Looking for response body
        if (strncmp(buf + start_pos, "\r\n\r\n", 4) == 0) {
            start_pos += 4;
            handle_updates(buf + start_pos, buf_len - start_pos, update_handler, open_queue, status_queue);
            return;
        }
    }
}

static int https_send_request(char* buf, int buf_size, esp_tls_cfg_t cfg, const char* web_server_url, const char* request) {
    esp_tls_t* tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        return ESP_FAIL;
    }

    int ret;
    if (esp_tls_conn_http_new_sync(web_server_url, &cfg, tls) == 1) {
        ESP_LOGI(TAG, "Connection established...");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        int esp_tls_code = 0, esp_tls_flags = 0;
        esp_tls_error_handle_t tls_e = NULL;
        esp_tls_get_error_handle(tls, &tls_e);
        /* Try to get TLS stack level error and certificate failure flags, if any */
        ret = esp_tls_get_and_clear_last_error(tls_e, &esp_tls_code, &esp_tls_flags);
        if (ret == ESP_OK) {
            ESP_LOGE(TAG, "TLS error = -0x%x, TLS flags = -0x%x", esp_tls_code, esp_tls_flags);
        }
        goto cleanup;
    }

    size_t written_bytes = 0;
    size_t request_len = strlen(request);
    do {
        ret = esp_tls_conn_write(tls,
            request + written_bytes,
            request_len - written_bytes);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            goto cleanup;
        }
    } while (written_bytes < request_len);

    ESP_LOGI(TAG, "Reading HTTP response...");
    do {
        memset(buf, 0x00, buf_size);
        ret = esp_tls_conn_read(tls, (char*)buf, buf_size - 1);

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
            continue;
        } else if (ret < 0) {
            ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
            break;
        } else if (ret == 0) {
            ESP_LOGI(TAG, "connection closed");
            break;
        }

        int len = ret;
        ESP_LOGD(TAG, "%d bytes read", len);
#ifdef TG_DEBUG
        /* Print response directly to stdout as it is read */
        for (int i = 0; i < len; i++) {
            putchar(buf[i]);
        }
        puts("\n\n\n"); // JSON output doesn't have a newline at end
#endif
        break; // comment out this line in case of stream processing of response
    } while (1);

cleanup:
    esp_tls_conn_destroy(tls);
    return ret;
}

int tg_send_message(char* chat_id, char* text) {
    if (!tg_config.initialized) return ESP_FAIL;

    sprintf(request, SEND_MESSAGE_FORMAT_STRING, tg_config.bot_token, sizeof(SEND_MESSAGE_BODY_FORMAT_STRING) - sizeof("%s%s") + strlen(chat_id) + strlen(text), chat_id, text);
    return https_send_request(resp_buf, sizeof(resp_buf), tg_config.tls_cfg, WEB_SERVER_URL, request);
}

int tg_get_messages(char* bot_token, int32_t update_id) {
    if (!tg_config.initialized) return ESP_FAIL;

    sprintf(request, GET_MESSAGES_FORMAT_STRING, bot_token, update_id + 1);
    return https_send_request(req_buf, sizeof(req_buf), tg_config.tls_cfg, WEB_SERVER_URL, request);
}

esp_err_t tg_init(char* bot_token) {
    if (tg_config.initialized) {
        return ESP_FAIL;
    }

    strcpy(tg_config.bot_token, bot_token);
    tg_config.initialized = true;

    return ESP_OK;
}

void tg_deinit() {
    tg_config.initialized = false;
}

void tg_start(char* update_handler(char*, tg_update_t*, QueueHandle_t, QueueHandle_t), QueueHandle_t open_queue, QueueHandle_t status_queue) {
    if (!tg_config.initialized) {
        return;
    }

    while (42) {
        ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());

        int ret = tg_get_messages(tg_config.bot_token, tg_config.update_id);
        if (ret > 0) {
            int buf_size = ret;
            tg_parse(req_buf, buf_size, update_handler, open_queue, status_queue);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}