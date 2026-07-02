#ifndef USER_H
#define USER_H

#include <stddef.h>

#define MAX_USERNAME_LENGTH 31
#define MAX_PASSWORD_LENGTH 63
#define USER_ACCOUNT_LENGTH 9
#define MAX_NICKNAME_LENGTH 31

typedef enum {
    USER_OK = 0,
    USER_INVALID_INPUT,
    USER_ALREADY_EXISTS,
    USER_NOT_FOUND,
    USER_BAD_PASSWORD,
    USER_STORAGE_ERROR
} UserResult;

typedef struct {
    char account[USER_ACCOUNT_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
    char nickname[MAX_NICKNAME_LENGTH + 1];
} UserRecord;

int user_store_init(void);
UserResult user_register(const char *account, const char *password,
                         const char *nickname);
UserResult user_authenticate(const char *login, const char *password,
                             UserRecord *record);
int user_exists(const char *account);
int user_find(const char *login, UserRecord *record);
int user_find_by_account(const char *account, UserRecord *record);
const char *user_result_message(UserResult result);

int user_valid_account(const char *account);
int user_valid_password(const char *password);
int user_valid_nickname(const char *nickname);

#endif
