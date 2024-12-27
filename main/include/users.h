#ifndef _USERS_H_
#define _USERS_H_

#include <stdint.h>

#define USER_OK 0
#define USER_ALREADY_EXIST (-1)
#define USER_NO_FREE_SPACE (-2)
#define USER_NOT_FOUND (-3)
#define USER_WRONG_ID (-4)

typedef struct {
    int64_t id; // 52-bit value
    char username[32];
    char first_name[32];
    char last_name[32];
} user_t;

bool is_admin(int64_t id);
int_fast8_t user_add(int64_t id);
int_fast8_t user_drop(int64_t id);
char* users_list(char* buf, size_t buf_size);
size_t user_count();
int_fast8_t admin_add(int64_t id);
int_fast8_t admin_drop(int64_t id);
char* admins_list(char* buf, size_t buf_size);
size_t admin_count();

#endif // _USERS_H_
