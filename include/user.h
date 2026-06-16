#ifndef USER_H
#define USER_H

#include <stddef.h>

#define MAX_USERNAME_LENGTH 31
#define MAX_PASSWORD_LENGTH 63

typedef enum {
    USER_OK = 0,
    USER_INVALID_INPUT,
    USER_ALREADY_EXISTS,
    USER_NOT_FOUND,
    USER_BAD_PASSWORD,
    USER_STORAGE_ERROR
} UserResult;

int user_store_init(void);
UserResult user_register(const char *username, const char *password);
UserResult user_authenticate(const char *username, const char *password);
int user_exists(const char *username);
const char *user_result_message(UserResult result);

/* Usernames use letters, digits, underscore, and hyphen. */
int user_valid_username(const char *username);
int user_valid_password(const char *password);

#endif
