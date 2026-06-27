#include "chat.h"
#include "storage.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static pthread_mutex_t chat_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static int write_log_entry(const char *type, const char *sender,
                           const char *recipient, const char *message)
{
    FILE *file;
    time_t now;
    struct tm local_time;
    char timestamp[32];
    int result = 0;

    now = time(NULL);
    if (localtime_r(&now, &local_time) == NULL ||
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                 &local_time) == 0) {
        return -1;
    }

    pthread_mutex_lock(&chat_log_mutex);
    file = fopen(storage_chat_log_file(), "a");
    if (file == NULL) {
        result = -1;
    } else {
        if (recipient == NULL) {
            result = fprintf(file, "[%s] [%s] %s: %s\n", timestamp, type,
                             sender, message) < 0
                         ? -1
                         : 0;
        } else {
            result = fprintf(file, "[%s] [%s] %s -> %s: %s\n", timestamp,
                             type, sender, recipient, message) < 0
                         ? -1
                         : 0;
        }
        if (fflush(file) != 0 || fclose(file) != 0) {
            result = -1;
        }
    }
    pthread_mutex_unlock(&chat_log_mutex);
    return result;
}

int chat_log_init(void)
{
    FILE *file;

    pthread_mutex_lock(&chat_log_mutex);
    file = fopen(storage_chat_log_file(), "a");
    if (file != NULL) {
        fclose(file);
    }
    pthread_mutex_unlock(&chat_log_mutex);
    return file == NULL ? -1 : 0;
}

int chat_log_group(const char *sender, const char *message)
{
    return write_log_entry("GROUP", sender, NULL, message);
}

int chat_log_private(const char *sender, const char *recipient,
                     const char *message)
{
    return write_log_entry("PRIVATE", sender, recipient, message);
}
