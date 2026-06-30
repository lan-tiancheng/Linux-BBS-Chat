#include "user.h"
#include "storage.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t users_file_mutex = PTHREAD_MUTEX_INITIALIZER;

static int parse_user_record(char *line, UserRecord *record)
{
    char *account;
    char *password;
    char *nickname;
    char *newline;

    if (line == NULL || record == NULL) {
        return 0;
    }
    newline = strpbrk(line, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }

    account = strtok(line, "|");
    password = strtok(NULL, "|");
    nickname = strtok(NULL, "|");
    if (account == NULL || password == NULL || nickname == NULL) {
        return 0;
    }
    snprintf(record->account, sizeof(record->account), "%s", account);
    snprintf(record->password, sizeof(record->password), "%s", password);
    snprintf(record->nickname, sizeof(record->nickname), "%s", nickname);
    return record->account[0] != '\0' && record->nickname[0] != '\0';
}

int user_valid_account(const char *account)
{
    size_t i;

    if (account == NULL || strlen(account) != USER_ACCOUNT_LENGTH) {
        return 0;
    }
    for (i = 0; account[i] != '\0'; i++) {
        if (!isdigit((unsigned char)account[i])) {
            return 0;
        }
    }
    return 1;
}

int user_valid_nickname(const char *nickname)
{
    size_t length = 0;

    if (nickname == NULL || *nickname == '\0') {
        return 0;
    }
    while (nickname[length] != '\0') {
        unsigned char ch = (unsigned char)nickname[length];
        if (ch == '|' || ch == '\r' || ch == '\n' || isspace(ch)) {
            return 0;
        }
        length++;
        if (length > MAX_NICKNAME_LENGTH) {
            return 0;
        }
    }
    return 1;
}

int user_valid_password(const char *password)
{
    size_t length = 0;
    int has_alpha = 0;
    int has_digit = 0;

    if (password == NULL) {
        return 0;
    }
    while (password[length] != '\0') {
        unsigned char ch = (unsigned char)password[length];
        if (isspace(ch) || ch == '|') {
            return 0;
        }
        if (isalpha(ch)) {
            has_alpha = 1;
        }
        if (isdigit(ch)) {
            has_digit = 1;
        }
        length++;
        if (length > MAX_PASSWORD_LENGTH) {
            return 0;
        }
    }
    return length > 6 && has_alpha && has_digit;
}

int user_store_init(void)
{
    FILE *file;

    pthread_mutex_lock(&users_file_mutex);
    file = fopen(storage_users_file(), "a");
    if (file != NULL) {
        fclose(file);
    }
    pthread_mutex_unlock(&users_file_mutex);
    return file == NULL ? -1 : 0;
}

UserResult user_register(const char *account, const char *password,
                         const char *nickname)
{
    FILE *file;
    char line[256];
    UserResult result = USER_OK;

    if (!user_valid_account(account) || !user_valid_password(password) ||
        !user_valid_nickname(nickname)) {
        return USER_INVALID_INPUT;
    }

    pthread_mutex_lock(&users_file_mutex);
    file = fopen(storage_users_file(), "a+");
    if (file == NULL) {
        result = USER_STORAGE_ERROR;
        goto done;
    }

    rewind(file);
    while (fgets(line, sizeof(line), file) != NULL) {
        UserRecord current;

        if (!parse_user_record(line, &current)) {
            continue;
        }
        if (strcmp(current.account, account) == 0 ||
            strcmp(current.nickname, nickname) == 0) {
            result = USER_ALREADY_EXISTS;
            goto close_file;
        }
    }
    if (ferror(file)) {
        result = USER_STORAGE_ERROR;
        goto close_file;
    }
    if (fprintf(file, "%s|%s|%s\n", account, password, nickname) < 0 ||
        fflush(file) != 0) {
        result = USER_STORAGE_ERROR;
    }

close_file:
    if (fclose(file) != 0 && result == USER_OK) {
        result = USER_STORAGE_ERROR;
    }
done:
    pthread_mutex_unlock(&users_file_mutex);
    return result;
}

static int find_unlocked(const char *login, UserRecord *record)
{
    FILE *file;
    char line[256];
    int result = 0;

    if (login == NULL || *login == '\0' || record == NULL) {
        return 0;
    }
    file = fopen(storage_users_file(), "r");
    if (file == NULL) {
        return errno == ENOENT ? 0 : -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        UserRecord current;

        if (!parse_user_record(line, &current)) {
            continue;
        }
        if (strcmp(current.account, login) == 0 ||
            strcmp(current.nickname, login) == 0) {
            *record = current;
            result = 1;
            break;
        }
    }
    if (ferror(file)) {
        result = -1;
    }
    fclose(file);
    return result;
}

int user_find(const char *login, UserRecord *record)
{
    int result;

    pthread_mutex_lock(&users_file_mutex);
    result = find_unlocked(login, record);
    pthread_mutex_unlock(&users_file_mutex);
    return result;
}

int user_find_by_account(const char *account, UserRecord *record)
{
    if (!user_valid_account(account)) {
        return 0;
    }
    return user_find(account, record);
}

UserResult user_authenticate(const char *login, const char *password,
                             UserRecord *record)
{
    UserRecord current;
    int found;

    if ((login == NULL || *login == '\0') || !user_valid_password(password)) {
        return USER_INVALID_INPUT;
    }

    pthread_mutex_lock(&users_file_mutex);
    found = find_unlocked(login, &current);
    pthread_mutex_unlock(&users_file_mutex);
    if (found < 0) {
        return USER_STORAGE_ERROR;
    }
    if (found == 0) {
        return USER_NOT_FOUND;
    }
    if (strcmp(current.password, password) != 0) {
        return USER_BAD_PASSWORD;
    }
    if (record != NULL) {
        *record = current;
    }
    return USER_OK;
}

int user_exists(const char *account)
{
    UserRecord record;

    return user_find_by_account(account, &record);
}

const char *user_result_message(UserResult result)
{
    switch (result) {
    case USER_OK:
        return "success";
    case USER_INVALID_INPUT:
        return "invalid account, password, or nickname";
    case USER_ALREADY_EXISTS:
        return "account or nickname already exists";
    case USER_NOT_FOUND:
    case USER_BAD_PASSWORD:
        return "invalid login or password";
    case USER_STORAGE_ERROR:
        return "user storage error";
    }
    return "unknown user error";
}
