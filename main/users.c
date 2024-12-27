#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
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

static user_t admins[10] = ADMINS_INITIALIZER;
static user_t users[100] = USERS_INITIALIZER;

static int_fast8_t add(int64_t id, user_t* users, size_t size);
static int_fast8_t drop(int64_t id, user_t* users, size_t size);
static char* list(char* buf, size_t buf_size, user_t* users, size_t size);
static size_t count(user_t* usr, size_t usr_size);

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

int_fast8_t user_add(int64_t id) {
    return add(id, users, sizeof(users) / sizeof(users[0]));
}

int_fast8_t user_drop(int64_t id) {
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

int_fast8_t admin_add(int64_t id) {
    return add(id, admins, sizeof(admins) / sizeof(admins[0]));
}

int_fast8_t admin_drop(int64_t id) {
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

static int_fast8_t add(int64_t id, user_t* usr, size_t usr_size) {
    if (id == 0) return USER_WRONG_ID;

    int_fast8_t empty = -1;
    for (int i = 0; i < usr_size; i++) {
        if (usr[i].id == id) {
            return USER_ALREADY_EXIST;
        }
        if (usr[i].id == 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        usr[empty].id = id;
        return USER_OK;
    }

    return USER_NO_FREE_SPACE;
}

static int_fast8_t drop(int64_t id, user_t* usr, size_t usr_size) {
    if (id == 0) return USER_WRONG_ID;

    for (size_t i = 0; i < usr_size; i++) {
        if (usr[i].id == id) {
            usr[i].id = 0;
            return USER_OK;
        }
    }

    return USER_NOT_FOUND;
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