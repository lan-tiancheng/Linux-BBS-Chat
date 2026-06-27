#include "user.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_USERS_FILE "data/users.db"

static pthread_mutex_t users_file_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *users_file_path(void)
{
    const char *configured = getenv("BBS_USERS_FILE");
    return configured != NULL && *configured != '\0' ? configured
                                                       : DEFAULT_USERS_FILE;
}

static int parse_user_record(char *line, char **username, char **password)
{
    char *separator = strchr(line, ':');
    char *newline;

    if (separator == NULL) {
        return 0;
    }
    *separator = '\0';
    *username = line;
    *password = separator + 1;
    newline = strpbrk(*password, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    return **username != '\0';
}

int user_valid_username(const char *username)
{
    size_t length = 0;

    if (username == NULL || *username == '\0') {
        return 0;
    }
    while (username[length] != '\0') {
        unsigned char character = (unsigned char)username[length];
        if (!isalnum(character) && character != '_' && character != '-') {
            return 0;
        }
        length++;
        if (length > MAX_USERNAME_LENGTH) {
            return 0;
        }
    }
    return 1;
}

int user_valid_password(const char *password)
{
    size_t length = 0;

    if (password == NULL || *password == '\0') {
        return 0;
    }
    while (password[length] != '\0') {
        unsigned char character = (unsigned char)password[length];
        if (isspace(character) || character == ':') {
            return 0;
        }
        length++;
        if (length > MAX_PASSWORD_LENGTH) {
            return 0;
        }
    }
    return 1;
}

int user_store_init(void)
{
    FILE *file;

    pthread_mutex_lock(&users_file_mutex);
    file = fopen(users_file_path(), "a");
    if (file != NULL) {
        fclose(file);
    }
    pthread_mutex_unlock(&users_file_mutex);
    return file == NULL ? -1 : 0;
}

UserResult user_register(const char *username, const char *password)
{
    FILE *file;
    char line[MAX_USERNAME_LENGTH + MAX_PASSWORD_LENGTH + 4];
    UserResult result = USER_OK;

    if (!user_valid_username(username) || !user_valid_password(password)) {
        return USER_INVALID_INPUT;
    }

    pthread_mutex_lock(&users_file_mutex);
    file = fopen(users_file_path(), "a+");
    if (file == NULL) {
        result = USER_STORAGE_ERROR;
        goto done;
    }

    rewind(file);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *stored_username;
        char *stored_password;
        if (parse_user_record(line, &stored_username, &stored_password) &&
            strcmp(stored_username, username) == 0) {
            result = USER_ALREADY_EXISTS;
            goto close_file;
        }
    }
    if (ferror(file)) {
        result = USER_STORAGE_ERROR;
        goto close_file;
    }
    if (fprintf(file, "%s:%s\n", username, password) < 0 || fflush(file) != 0) {
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

UserResult user_authenticate(const char *username, const char *password)
{
    FILE *file;
    char line[MAX_USERNAME_LENGTH + MAX_PASSWORD_LENGTH + 4];
    UserResult result = USER_NOT_FOUND;

    if (!user_valid_username(username) || !user_valid_password(password)) {
        return USER_INVALID_INPUT;
    }

    pthread_mutex_lock(&users_file_mutex);
    file = fopen(users_file_path(), "r");
    if (file == NULL) {
        result = errno == ENOENT ? USER_NOT_FOUND : USER_STORAGE_ERROR;
        goto done;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *stored_username;
        char *stored_password;
        if (!parse_user_record(line, &stored_username, &stored_password) ||
            strcmp(stored_username, username) != 0) {
            continue;
        }
        result = strcmp(stored_password, password) == 0 ? USER_OK
                                                        : USER_BAD_PASSWORD;
        break;
    }
    if (ferror(file)) {
        result = USER_STORAGE_ERROR;
    }
    fclose(file);
done:
    pthread_mutex_unlock(&users_file_mutex);
    return result;
}

int user_exists(const char *username)
{
    FILE *file;
    char line[MAX_USERNAME_LENGTH + MAX_PASSWORD_LENGTH + 4];
    int result = 0;

    if (!user_valid_username(username)) {
        return 0;
    }
    pthread_mutex_lock(&users_file_mutex);
    file = fopen(users_file_path(), "r");
    if (file == NULL) {
        result = errno == ENOENT ? 0 : -1;
        goto done;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *stored_username;
        char *stored_password;
        if (parse_user_record(line, &stored_username, &stored_password) &&
            strcmp(stored_username, username) == 0) {
            result = 1;
            break;
        }
    }
    if (ferror(file)) {
        result = -1;
    }
    fclose(file);
done:
    pthread_mutex_unlock(&users_file_mutex);
    return result;
}

const char *user_result_message(UserResult result)
{
    switch (result) {
    case USER_OK:
        return "success";
    case USER_INVALID_INPUT:
        return "invalid username or password";
    case USER_ALREADY_EXISTS:
        return "username already exists";
    case USER_NOT_FOUND:
    case USER_BAD_PASSWORD:
        return "invalid username or password";
    case USER_STORAGE_ERROR:
        return "user storage error";
    }
    return "unknown user error";
}
