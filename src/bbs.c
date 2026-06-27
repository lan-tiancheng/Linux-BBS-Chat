#include "bbs.h"

#include "storage.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t bbs_mutex = PTHREAD_MUTEX_INITIALIZER;

static size_t encode_field(const char *input, char *output, size_t capacity)
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
    return used;
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

static int read_last_id(const char *path, unsigned long *value)
{
    FILE *file;
    char line[4096];
    unsigned long last = 0;

    if (value == NULL) {
        return -1;
    }
    file = fopen(path, "r");
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

static int count_matching_lines(const char *path, int (*predicate)(const char *),
                                unsigned long *count)
{
    FILE *file;
    char line[4096];
    unsigned long total = 0;

    if (count == NULL) {
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        *count = 0;
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (predicate == NULL || predicate(line)) {
            total++;
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    *count = total;
    return 0;
}

static int reply_matches_post(const char *line, unsigned long post_id)
{
    unsigned long line_id = 0;
    unsigned long line_post_id = 0;

    return sscanf(line, "%lu|%lu|", &line_id, &line_post_id) == 2 &&
           line_post_id == post_id;
}

static int split_fields(char *line, char **fields, size_t field_count)
{
    size_t i;

    if (line == NULL || fields == NULL || field_count == 0) {
        return -1;
    }
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

static int decode_field(char *text)
{
    char *read = text;
    char *write = text;

    if (text == NULL) {
        return -1;
    }
    while (*read != '\0') {
        if (*read == '%' && read[1] != '\0' && read[2] != '\0' &&
            isxdigit((unsigned char)read[1]) &&
            isxdigit((unsigned char)read[2])) {
            int high = isdigit((unsigned char)read[1]) ? read[1] - '0'
                                                       : toupper((unsigned char)read[1]) - 'A' + 10;
            int low = isdigit((unsigned char)read[2]) ? read[2] - '0'
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

static int parse_post_line(char *line, BbsPostRecord *record)
{
    char *fields[7];
    char *newline;

    newline = strpbrk(line, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    if (split_fields(line, fields, 7) < 0) {
        return -1;
    }
    if (sscanf(fields[0], "%lu", &record->id) != 1 ||
        sscanf(fields[6], "%d", &record->active) != 1) {
        return -1;
    }
    snprintf(record->author, sizeof(record->author), "%s", fields[1]);
    snprintf(record->title, sizeof(record->title), "%s", fields[2]);
    snprintf(record->content, sizeof(record->content), "%s", fields[3]);
    snprintf(record->created_at, sizeof(record->created_at), "%s", fields[4]);
    snprintf(record->updated_at, sizeof(record->updated_at), "%s", fields[5]);
    if (decode_field(record->author) < 0 || decode_field(record->title) < 0 ||
        decode_field(record->content) < 0 ||
        decode_field(record->created_at) < 0 ||
        decode_field(record->updated_at) < 0) {
        return -1;
    }
    return 0;
}

static int parse_reply_line(char *line, BbsReplyRecord *record)
{
    char *fields[6];
    char *newline;

    newline = strpbrk(line, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    if (split_fields(line, fields, 6) < 0) {
        return -1;
    }
    if (sscanf(fields[0], "%lu", &record->id) != 1 ||
        sscanf(fields[1], "%lu", &record->post_id) != 1 ||
        sscanf(fields[5], "%d", &record->active) != 1) {
        return -1;
    }
    snprintf(record->author, sizeof(record->author), "%s", fields[2]);
    snprintf(record->content, sizeof(record->content), "%s", fields[3]);
    snprintf(record->created_at, sizeof(record->created_at), "%s", fields[4]);
    if (decode_field(record->author) < 0 || decode_field(record->content) < 0 ||
        decode_field(record->created_at) < 0) {
        return -1;
    }
    return 0;
}

static int parse_file_line(char *line, BbsFileRecord *record)
{
    char *fields[9];
    char *newline;

    newline = strpbrk(line, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    if (split_fields(line, fields, 9) < 0) {
        return -1;
    }
    if (sscanf(fields[0], "%lu", &record->id) != 1 ||
        sscanf(fields[6], "%llu", &record->size) != 1 ||
        sscanf(fields[8], "%d", &record->active) != 1) {
        return -1;
    }
    snprintf(record->owner, sizeof(record->owner), "%s", fields[1]);
    snprintf(record->sender, sizeof(record->sender), "%s", fields[2]);
    snprintf(record->recipient, sizeof(record->recipient), "%s", fields[3]);
    snprintf(record->filename, sizeof(record->filename), "%s", fields[4]);
    snprintf(record->stored_name, sizeof(record->stored_name), "%s",
             fields[5]);
    snprintf(record->created_at, sizeof(record->created_at), "%s", fields[7]);
    if (decode_field(record->owner) < 0 || decode_field(record->sender) < 0 ||
        decode_field(record->recipient) < 0 ||
        decode_field(record->filename) < 0 ||
        decode_field(record->stored_name) < 0 ||
        decode_field(record->created_at) < 0) {
        return -1;
    }
    return 0;
}

static int visit_post_file(BbsPostVisitor visitor, void *context)
{
    FILE *file;
    char line[4096];

    if (visitor == NULL) {
        return -1;
    }
    file = fopen(storage_posts_file(), "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        BbsPostRecord current;

        if (parse_post_line(line, &current) == 0 &&
            visitor(&current, context) != 0) {
            fclose(file);
            return -1;
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

static int visit_reply_file(unsigned long post_id, BbsReplyVisitor visitor,
                            void *context)
{
    FILE *file;
    char line[4096];

    if (visitor == NULL) {
        return -1;
    }
    file = fopen(storage_replies_file(), "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        BbsReplyRecord current;

        if (parse_reply_line(line, &current) == 0 && current.post_id == post_id
            && visitor(&current, context) != 0) {
            fclose(file);
            return -1;
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

static int visit_file_index(BbsFileVisitor visitor, void *context)
{
    FILE *file;
    char line[4096];

    if (visitor == NULL) {
        return -1;
    }
    file = fopen(storage_file_index(), "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        BbsFileRecord current;

        if (parse_file_line(line, &current) == 0 &&
            visitor(&current, context) != 0) {
            fclose(file);
            return -1;
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

static int serialize_post(const BbsPostRecord *record, char *buffer,
                          size_t capacity)
{
    char author[256];
    char title[512];
    char content[8192];
    char created_at[64];
    char updated_at[64];

    if (encode_field(record->author, author, sizeof(author)) == 0 ||
        encode_field(record->title, title, sizeof(title)) == 0 ||
        encode_field(record->content, content, sizeof(content)) == 0 ||
        encode_field(record->created_at, created_at, sizeof(created_at)) == 0 ||
        encode_field(record->updated_at, updated_at, sizeof(updated_at)) == 0) {
        return 0;
    }
    return snprintf(buffer, capacity, "%lu|%s|%s|%s|%s|%s|%d", record->id,
                    author, title, content, created_at, updated_at,
                    record->active) >= 0;
}

static int serialize_reply(const BbsReplyRecord *record, char *buffer,
                           size_t capacity)
{
    char author[256];
    char content[8192];
    char created_at[64];

    if (encode_field(record->author, author, sizeof(author)) == 0 ||
        encode_field(record->content, content, sizeof(content)) == 0 ||
        encode_field(record->created_at, created_at, sizeof(created_at)) == 0) {
        return 0;
    }
    return snprintf(buffer, capacity, "%lu|%lu|%s|%s|%s|%d", record->id,
                    record->post_id, author, content, created_at,
                    record->active) >= 0;
}

static int serialize_file_record(const BbsFileRecord *record, char *buffer,
                                 size_t capacity)
{
    char owner[256];
    char sender[256];
    char recipient[256];
    char filename[512];
    char stored_name[512];
    char created_at[64];

    if (encode_field(record->owner, owner, sizeof(owner)) == 0 ||
        encode_field(record->sender, sender, sizeof(sender)) == 0 ||
        encode_field(record->recipient, recipient, sizeof(recipient)) == 0 ||
        encode_field(record->filename, filename, sizeof(filename)) == 0 ||
        encode_field(record->stored_name, stored_name,
                     sizeof(stored_name)) == 0 ||
        encode_field(record->created_at, created_at, sizeof(created_at)) == 0) {
        return 0;
    }
    return snprintf(buffer, capacity,
                    "%lu|%s|%s|%s|%s|%s|%llu|%s|%d", record->id, owner,
                    sender, recipient, filename, stored_name, record->size,
                    created_at, record->active) >= 0;
}

int bbs_init(void)
{
    return storage_init();
}

int bbs_append_post(const BbsPostRecord *record)
{
    char line[4096];

    if (record == NULL || !serialize_post(record, line, sizeof(line))) {
        return -1;
    }
    pthread_mutex_lock(&bbs_mutex);
    if (append_line(storage_posts_file(), line) < 0) {
        pthread_mutex_unlock(&bbs_mutex);
        return -1;
    }
    pthread_mutex_unlock(&bbs_mutex);
    return 0;
}

int bbs_append_reply(const BbsReplyRecord *record)
{
    char line[4096];

    if (record == NULL || !serialize_reply(record, line, sizeof(line))) {
        return -1;
    }
    pthread_mutex_lock(&bbs_mutex);
    if (append_line(storage_replies_file(), line) < 0) {
        pthread_mutex_unlock(&bbs_mutex);
        return -1;
    }
    pthread_mutex_unlock(&bbs_mutex);
    return 0;
}

int bbs_append_file_record(const BbsFileRecord *record)
{
    char line[4096];

    if (record == NULL || !serialize_file_record(record, line, sizeof(line))) {
        return -1;
    }
    pthread_mutex_lock(&bbs_mutex);
    if (append_line(storage_file_index(), line) < 0) {
        pthread_mutex_unlock(&bbs_mutex);
        return -1;
    }
    pthread_mutex_unlock(&bbs_mutex);
    return 0;
}

int bbs_next_post_id(unsigned long *id)
{
    int result;

    pthread_mutex_lock(&bbs_mutex);
    result = read_last_id(storage_posts_file(), id);
    if (result == 0) {
        *id = *id + 1;
    }
    pthread_mutex_unlock(&bbs_mutex);
    return result;
}

int bbs_next_reply_id(unsigned long *id)
{
    int result;

    pthread_mutex_lock(&bbs_mutex);
    result = read_last_id(storage_replies_file(), id);
    if (result == 0) {
        *id = *id + 1;
    }
    pthread_mutex_unlock(&bbs_mutex);
    return result;
}

int bbs_next_file_id(unsigned long *id)
{
    int result;

    pthread_mutex_lock(&bbs_mutex);
    result = read_last_id(storage_file_index(), id);
    if (result == 0) {
        *id = *id + 1;
    }
    pthread_mutex_unlock(&bbs_mutex);
    return result;
}

int bbs_backup(char *snapshot_path, size_t snapshot_path_capacity)
{
    return storage_backup_snapshot("bbs", snapshot_path, snapshot_path_capacity);
}

int bbs_backup_named(const char *label, char *snapshot_path,
                     size_t snapshot_path_capacity)
{
    return storage_backup_snapshot(label, snapshot_path,
                                   snapshot_path_capacity);
}

int bbs_post_count(unsigned long *count)
{
    return count_matching_lines(storage_posts_file(), NULL, count);
}

int bbs_reply_count(unsigned long post_id, unsigned long *count)
{
    FILE *file;
    char line[4096];
    unsigned long total = 0;

    if (count == NULL) {
        return -1;
    }
    file = fopen(storage_replies_file(), "r");
    if (file == NULL) {
        *count = 0;
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (reply_matches_post(line, post_id)) {
            total++;
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    *count = total;
    return 0;
}

int bbs_file_count(unsigned long *count)
{
    return count_matching_lines(storage_file_index(), NULL, count);
}

int bbs_read_post(unsigned long post_id, BbsPostRecord *record)
{
    FILE *file;
    char line[4096];

    if (record == NULL) {
        return -1;
    }
    file = fopen(storage_posts_file(), "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        BbsPostRecord current;

        if (parse_post_line(line, &current) == 0 && current.id == post_id) {
            *record = current;
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return -1;
}

int bbs_read_reply(unsigned long reply_id, BbsReplyRecord *record)
{
    FILE *file;
    char line[4096];

    if (record == NULL) {
        return -1;
    }
    file = fopen(storage_replies_file(), "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        BbsReplyRecord current;

        if (parse_reply_line(line, &current) == 0 && current.id == reply_id) {
            *record = current;
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return -1;
}

int bbs_read_file(unsigned long file_id, BbsFileRecord *record)
{
    FILE *file;
    char line[4096];

    if (record == NULL) {
        return -1;
    }
    file = fopen(storage_file_index(), "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        BbsFileRecord current;

        if (parse_file_line(line, &current) == 0 && current.id == file_id) {
            *record = current;
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return -1;
}

int bbs_visit_posts(BbsPostVisitor visitor, void *context)
{
    return visit_post_file(visitor, context);
}

int bbs_visit_replies(unsigned long post_id, BbsReplyVisitor visitor,
                      void *context)
{
    return visit_reply_file(post_id, visitor, context);
}

int bbs_visit_files(BbsFileVisitor visitor, void *context)
{
    return visit_file_index(visitor, context);
}
