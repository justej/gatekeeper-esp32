#ifndef _TG_H_
#define _TG_H_

#include <stdint.h>
#include <stdbool.h>

#define JSMN_HEADER
#include "jsmn.h"

typedef struct {
    jsmntok_t* id;
    jsmntok_t* first_name;
    jsmntok_t* last_name;
    jsmntok_t* username;
    jsmntok_t* is_bot;
} tg_user_t;

typedef struct {
    jsmntok_t* id;
    jsmntok_t* type;
    jsmntok_t* first_name;
    jsmntok_t* last_name;
    jsmntok_t* username;
} tg_chat_t;

struct tg_message {
    jsmntok_t* id;
    tg_user_t* from;
    tg_chat_t* chat;
    struct tg_message* reply_to_message;
    jsmntok_t* text;
};

typedef struct tg_message tg_message_t;

typedef struct {
    jsmntok_t* id;
    tg_message_t* message;
} tg_update_t;

void tg_log_token(char* buf, char* key, jsmntok_t* token);
void tg_start(char* token, void update_handler(char*, tg_update_t*));

#endif // _TG_H_