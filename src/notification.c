#include "notification.h"

#include "storage.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t notification_mutex = PTHREAD_MUTEX_INITIALIZER;

static int encode_field(const char *input, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;

    if (input == NULL || output == NULL || capacity == 0) {
        return 0;
    }
    while (*input != '\0') {
        unsigned char ch = (unsigned char)*input++;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            if (used + 1 >= capacity) {
                return 0;
            }
            output[used++] = (char)ch;
        } else {
            if (used + 3 >= capacity) {
                return 0;
            }
            output[used++] = '%';
            output[used++] = hex[ch >> 4];
            output[used++] = hex[ch & 0x0F];
        }
    }
    if (used >= capacity) {
        return 0;
    }
    output[used] = '\0';
    return 1;
}

static int decode_field(char *text)
{
    char *read = text;
    char *write = text;

    if (text == NULL) {
        return -1;
    }
    while (*read != '\0') {
        if (*read == '%' && read[1] && read[2] &&
            isxdigit((unsigned char)read[1]) &&
            isxdigit((unsigned char)read[2])) {
            int high = isdigit((unsigned char)read[1])
                           ? read[1] - '0'
                           : toupper((unsigned char)read[1]) - 'A' + 10;
            int low = isdigit((unsigned char)read[2])
                          ? read[2] - '0'
                          : toupper((unsigned char)read[2]) - 'A' + 10;
            *write++ = (char)((high << 4) | low);
            read += 3;
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
    return 0;
}

static int split_fields(char *line, char **fields, size_t field_count)
{
    size_t i;

    fields[0] = line;
    for (i = 1; i < field_count; i++) {
        char *separator = strchr(fields[i - 1], '|');

        if (separator == NULL) {
            return -1;
        }
        *separator = '\0';
        fields[i] = separator + 1;
    }
    return 0;
}

static int parse_line(char *line, NotificationRecord *record)
{
    char *fields[7];
    char *newline;

    newline = strpbrk(line, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    if (split_fields(line, fields, 7) < 0 ||
        sscanf(fields[0], "%lu", &record->id) != 1 ||
        sscanf(fields[6], "%d", &record->read) != 1) {
        return -1;
    }
    snprintf(record->account, sizeof(record->account), "%s", fields[1]);
    snprintf(record->type, sizeof(record->type), "%s", fields[2]);
    snprintf(record->target, sizeof(record->target), "%s", fields[3]);
    snprintf(record->message, sizeof(record->message), "%s", fields[4]);
    snprintf(record->created_at, sizeof(record->created_at), "%s", fields[5]);
    if (decode_field(record->account) < 0 ||
        decode_field(record->type) < 0 ||
        decode_field(record->target) < 0 ||
        decode_field(record->message) < 0 ||
        decode_field(record->created_at) < 0) {
        return -1;
    }
    return 0;
}

static int serialize_record(const NotificationRecord *record, char *buffer,
                            size_t capacity)
{
    char account[128];
    char type[128];
    char target[256];
    char message[2048];
    char created_at[64];

    if (!encode_field(record->account, account, sizeof(account)) ||
        !encode_field(record->type, type, sizeof(type)) ||
        !encode_field(record->target, target, sizeof(target)) ||
        !encode_field(record->message, message, sizeof(message)) ||
        !encode_field(record->created_at, created_at, sizeof(created_at))) {
        return 0;
    }
    return snprintf(buffer, capacity, "%lu|%s|%s|%s|%s|%s|%d", record->id,
                    account, type, target, message, created_at,
                    record->read ? 1 : 0) >= 0;
}

static int append_line(const char *path, const char *line)
{
    FILE *file = fopen(path, "a");
    int result = -1;

    if (file == NULL) {
        return -1;
    }
    if (fputs(line, file) != EOF && fputc('\n', file) != EOF &&
        fflush(file) == 0) {
        result = 0;
    }
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}

static int read_last_id(unsigned long *value)
{
    FILE *file;
    char line[4096];
    unsigned long last = 0;

    file = fopen(storage_notifications_file(), "r");
    if (file == NULL) {
        *value = 0;
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long current = 0;

        if (sscanf(line, "%lu", &current) == 1 && current > last) {
            last = current;
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    *value = last;
    return 0;
}

static void format_timestamp(char *buffer, size_t capacity)
{
    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) == NULL ||
        strftime(buffer, capacity, "%Y-%m-%d %H:%M:%S", &local_time) == 0) {
        snprintf(buffer, capacity, "unknown");
    }
}

int notification_init(void)
{
    FILE *file = fopen(storage_notifications_file(), "a");

    if (file == NULL) {
        return -1;
    }
    fclose(file);
    return 0;
}

int notification_add(const char *account, const char *type,
                     const char *target, const char *message,
                     unsigned long *id)
{
    NotificationRecord record;
    char line[4096];
    int result;

    if (account == NULL || *account == '\0' || type == NULL ||
        *type == '\0') {
        return -1;
    }
    memset(&record, 0, sizeof(record));
    pthread_mutex_lock(&notification_mutex);
    if (read_last_id(&record.id) < 0) {
        pthread_mutex_unlock(&notification_mutex);
        return -1;
    }
    record.id++;
    snprintf(record.account, sizeof(record.account), "%s", account);
    snprintf(record.type, sizeof(record.type), "%s", type);
    snprintf(record.target, sizeof(record.target), "%s",
             target == NULL ? "" : target);
    snprintf(record.message, sizeof(record.message), "%s",
             message == NULL ? "" : message);
    format_timestamp(record.created_at, sizeof(record.created_at));
    record.read = 0;
    if (!serialize_record(&record, line, sizeof(line))) {
        pthread_mutex_unlock(&notification_mutex);
        return -1;
    }
    result = append_line(storage_notifications_file(), line);
    pthread_mutex_unlock(&notification_mutex);
    if (result == 0 && id != NULL) {
        *id = record.id;
    }
    return result;
}

int notification_visit_for(const char *account, NotificationVisitor visitor,
                           void *context)
{
    FILE *file;
    char line[4096];
    int result = 0;

    if (account == NULL || visitor == NULL) {
        return -1;
    }
    pthread_mutex_lock(&notification_mutex);
    file = fopen(storage_notifications_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&notification_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        NotificationRecord record;

        if (parse_line(line, &record) == 0 &&
            strcmp(record.account, account) == 0 &&
            visitor(&record, context) != 0) {
            result = -1;
            break;
        }
    }
    if (ferror(file)) {
        result = -1;
    }
    fclose(file);
    pthread_mutex_unlock(&notification_mutex);
    return result;
}

static int rewrite_marked(const char *account, unsigned long id, int mark_all)
{
    FILE *source;
    FILE *target;
    char temp_path[4096];
    char line[4096];
    int found = 0;
    int result = -1;

    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp",
                 storage_notifications_file()) >= (int)sizeof(temp_path)) {
        return -1;
    }
    source = fopen(storage_notifications_file(), "r");
    if (source == NULL) {
        return -1;
    }
    target = fopen(temp_path, "w");
    if (target == NULL) {
        fclose(source);
        return -1;
    }
    while (fgets(line, sizeof(line), source) != NULL) {
        char original[4096];
        NotificationRecord record;

        snprintf(original, sizeof(original), "%s", line);
        if (parse_line(line, &record) == 0 &&
            strcmp(record.account, account) == 0 &&
            (mark_all || record.id == id)) {
            char serialized[4096];

            record.read = 1;
            if (!serialize_record(&record, serialized, sizeof(serialized)) ||
                fputs(serialized, target) == EOF ||
                fputc('\n', target) == EOF) {
                goto done;
            }
            found = 1;
            continue;
        }
        if (fputs(original, target) == EOF) {
            goto done;
        }
    }
    if (ferror(source) || fflush(target) != 0) {
        goto done;
    }
    result = 0;

done:
    fclose(source);
    if (fclose(target) != 0) {
        result = -1;
    }
    if (result == 0 && (found || mark_all)) {
        if (rename(temp_path, storage_notifications_file()) != 0) {
            result = -1;
        }
    } else {
        unlink(temp_path);
    }
    return result == 0 && (found || mark_all) ? (found ? 1 : 0) : result;
}

int notification_mark_read(const char *account, unsigned long id)
{
    int result;

    if (account == NULL || id == 0) {
        return -1;
    }
    pthread_mutex_lock(&notification_mutex);
    result = rewrite_marked(account, id, 0);
    pthread_mutex_unlock(&notification_mutex);
    return result;
}

int notification_mark_all_read(const char *account)
{
    int result;

    if (account == NULL) {
        return -1;
    }
    pthread_mutex_lock(&notification_mutex);
    result = rewrite_marked(account, 0, 1);
    pthread_mutex_unlock(&notification_mutex);
    return result < 0 ? -1 : 0;
}
