#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "jsmn.h"
#include "tg.h"

// #define JSMN_DEBUG

#define TOK_LEN 512
#define MESSAGE_BUF_SIZE 10

static const char TAG[] = "tg";

jsmntok_t tokens[TOK_LEN];

#ifdef JSMN_DEBUG
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
#else
#define __JSMNLOG(buf, tokens, i_tok)
#endif

void tg_log_token(char* buf, char* key, jsmntok_t* token) {
    if (token != NULL) {
        buf[token->end] = '\0';
        printf("%s: %s\n", key, &buf[token->start]);
    }
}

void tg_log(char* buf, tg_update_t* update) {
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

static bool parse_user(tg_user_t* user, char* buf, jsmntok_t* tokens, int* i_tok, int parsed_len) {
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

static bool parse_chat(tg_chat_t* chat, char* buf, jsmntok_t* tokens, int* i_tok, int parsed_len) {
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

static bool parse_message(tg_message_t* message, char* buf, jsmntok_t* tokens, int* i_tok, int parsed_len) {
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
            if (parse_user(message->from, buf, tokens, i_tok, parsed_len)) {
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "chat")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (parse_chat(message->chat, buf, tokens, i_tok, parsed_len)) {
                continue;
            } else {
                return false;
            }
        }

        if (jsmn_strcmp(buf, &tokens[*i_tok], "text")) {
            (*i_tok)++;
            __JSMNLOG(buf, tokens, *i_tok);
            if (jsmn_is_str(&tokens[*i_tok])) {
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

static bool parse_update(tg_update_t* update, char* buf, jsmntok_t* tokens, int* i_tok, int parsed_len) {
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
            if (parse_message(update->message, buf, tokens, i_tok, parsed_len)) {
                continue;
            } else {
                return false;
            }
        }

        skip_tokens(tokens, parsed_len, i_tok);
    }

    return true;
}

static void handle_updates(char* buf, int buf_len, void update_handler(char*, tg_update_t*)) {
    ESP_LOGI(TAG, "JSON: %*s", buf_len, buf);

    jsmn_parser parser;

    jsmn_init(&parser);
    int parsed_len = jsmn_parse(&parser, buf, buf_len, tokens, TOK_LEN);

    int i_tok = 0;
    // for (i_tok = 0; i_tok < parsed_len; i_tok++) {
    //     __JSMNLOG(buf, tokens, i_tok);
    // }

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

                ESP_LOGI(TAG, "update %i / %i", i, size);
                if (parse_update(&update, buf, tokens, &i_tok, parsed_len)) {
                    update_handler(buf, &update);
                }
            }
            continue;
        }

        skip_tokens(tokens, parsed_len, &i_tok);
    }
}

void tg_parse(char* buf, int buf_len, void update_handler(char*, tg_update_t*)) {
    if (buf_len < 4) {
        return;
    }

    for (int start_pos = 0; start_pos < buf_len; start_pos++) {
        if (strncmp(buf + start_pos, "\r\n\r\n", 4) == 0) {
            start_pos += 4;
            handle_updates(buf + start_pos, buf_len - start_pos, update_handler);
            return;
        }
    }
}
