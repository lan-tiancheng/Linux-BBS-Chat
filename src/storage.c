#include "storage.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_DATA_DIR "data"
#define DEFAULT_LOGS_DIR "logs"
#define DEFAULT_UPLOAD_DIR "uploads/chat"
#define DEFAULT_DOWNLOAD_DIR "downloads"
#define DEFAULT_BACKUP_DIR "backup"

static pthread_once_t storage_once = PTHREAD_ONCE_INIT;

static char data_dir_path[PATH_MAX];
static char logs_dir_path[PATH_MAX];
static char upload_dir_path[PATH_MAX];
static char download_dir_path[PATH_MAX];
static char backup_dir_path[PATH_MAX];
static char users_file_path[PATH_MAX];
static char posts_file_path[PATH_MAX];
static char replies_file_path[PATH_MAX];
static char file_index_path[PATH_MAX];
static char chat_log_path[PATH_MAX];

static const char *env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && *value != '\0' ? value : fallback;
}

static int path_join(char *output, size_t capacity, const char *left,
                     const char *right)
{
    int written;

    if (left == NULL || *left == '\0') {
        written = snprintf(output, capacity, "%s", right);
    } else if (right == NULL || *right == '\0') {
        written = snprintf(output, capacity, "%s", left);
    } else {
        written = snprintf(output, capacity, "%s/%s", left, right);
    }
    return written >= 0 && (size_t)written < capacity ? 0 : -1;
}

static int ensure_directory_chain(const char *path)
{
    char buffer[PATH_MAX];
    size_t length;
    size_t i;
    struct stat status;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (snprintf(buffer, sizeof(buffer), "%s", path) >= (int)sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (stat(buffer, &status) == 0) {
        return S_ISDIR(status.st_mode) ? 0 : -1;
    }

    length = strlen(buffer);
    while (length > 1 && buffer[length - 1] == '/') {
        buffer[--length] = '\0';
    }

    if (mkdir(buffer, 0755) == 0) {
        return 0;
    }
    if (errno != ENOENT && errno != EEXIST) {
        return -1;
    }

    for (i = 1; buffer[i] != '\0'; i++) {
        if (buffer[i] != '/') {
            continue;
        }
        buffer[i] = '\0';
        if (buffer[0] != '\0' && mkdir(buffer, 0755) < 0 && errno != EEXIST) {
            struct stat status;

            if (stat(buffer, &status) != 0 || !S_ISDIR(status.st_mode)) {
                buffer[i] = '/';
                return -1;
            }
        }
        buffer[i] = '/';
    }

    if (mkdir(buffer, 0755) < 0 && errno != EEXIST) {
        struct stat status;

        if (stat(buffer, &status) != 0 || !S_ISDIR(status.st_mode)) {
            return -1;
        }
    }
    return 0;
}

static int ensure_parent_directory(const char *path)
{
    char buffer[PATH_MAX];
    char *separator;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (snprintf(buffer, sizeof(buffer), "%s", path) >= (int)sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    separator = strrchr(buffer, '/');
    if (separator == NULL) {
        return 0;
    }
    if (separator == buffer) {
        separator[1] = '\0';
    } else {
        *separator = '\0';
    }
    return ensure_directory_chain(buffer);
}

static void set_path(char *output, size_t capacity, const char *value)
{
    if (snprintf(output, capacity, "%s", value) >= (int)capacity) {
        output[0] = '\0';
    }
}

static void initialize_paths(void)
{
    const char *data_dir = env_or_default("BBS_DATA_DIR", DEFAULT_DATA_DIR);
    const char *logs_dir = env_or_default("BBS_LOG_DIR", DEFAULT_LOGS_DIR);
    const char *upload_dir = env_or_default("BBS_UPLOAD_DIR",
                                            DEFAULT_UPLOAD_DIR);
    const char *download_dir = env_or_default("BBS_DOWNLOAD_DIR",
                                              DEFAULT_DOWNLOAD_DIR);
    const char *backup_dir = env_or_default("BBS_BACKUP_DIR",
                                            DEFAULT_BACKUP_DIR);
    const char *users_file = getenv("BBS_USERS_FILE");
    const char *posts_file = getenv("BBS_POSTS_FILE");
    const char *replies_file = getenv("BBS_REPLIES_FILE");
    const char *file_index = getenv("BBS_FILE_INDEX");
    const char *chat_log = getenv("BBS_CHAT_LOG");

    set_path(data_dir_path, sizeof(data_dir_path), data_dir);
    set_path(logs_dir_path, sizeof(logs_dir_path), logs_dir);
    set_path(upload_dir_path, sizeof(upload_dir_path), upload_dir);
    set_path(download_dir_path, sizeof(download_dir_path), download_dir);
    set_path(backup_dir_path, sizeof(backup_dir_path), backup_dir);

    if (users_file != NULL && *users_file != '\0') {
        set_path(users_file_path, sizeof(users_file_path), users_file);
    } else {
        (void)path_join(users_file_path, sizeof(users_file_path),
                        data_dir_path, "users.db");
    }
    if (posts_file != NULL && *posts_file != '\0') {
        set_path(posts_file_path, sizeof(posts_file_path), posts_file);
    } else {
        (void)path_join(posts_file_path, sizeof(posts_file_path),
                        data_dir_path, "posts.db");
    }
    if (replies_file != NULL && *replies_file != '\0') {
        set_path(replies_file_path, sizeof(replies_file_path), replies_file);
    } else {
        (void)path_join(replies_file_path, sizeof(replies_file_path),
                        data_dir_path, "replies.db");
    }
    if (file_index != NULL && *file_index != '\0') {
        set_path(file_index_path, sizeof(file_index_path), file_index);
    } else {
        (void)path_join(file_index_path, sizeof(file_index_path),
                        data_dir_path, "files.db");
    }
    if (chat_log != NULL && *chat_log != '\0') {
        set_path(chat_log_path, sizeof(chat_log_path), chat_log);
    } else {
        (void)path_join(chat_log_path, sizeof(chat_log_path), logs_dir_path,
                        "chat.log");
    }
}

static void open_and_close(const char *path)
{
    FILE *file = fopen(path, "a");

    if (file != NULL) {
        fclose(file);
    }
}

const char *storage_data_dir(void)
{
    pthread_once(&storage_once, initialize_paths);
    return data_dir_path;
}

const char *storage_logs_dir(void)
{
    pthread_once(&storage_once, initialize_paths);
    return logs_dir_path;
}

const char *storage_upload_dir(void)
{
    pthread_once(&storage_once, initialize_paths);
    return upload_dir_path;
}

const char *storage_download_dir(void)
{
    pthread_once(&storage_once, initialize_paths);
    return download_dir_path;
}

const char *storage_backup_dir(void)
{
    pthread_once(&storage_once, initialize_paths);
    return backup_dir_path;
}

const char *storage_users_file(void)
{
    pthread_once(&storage_once, initialize_paths);
    return users_file_path;
}

const char *storage_posts_file(void)
{
    pthread_once(&storage_once, initialize_paths);
    return posts_file_path;
}

const char *storage_replies_file(void)
{
    pthread_once(&storage_once, initialize_paths);
    return replies_file_path;
}

const char *storage_file_index(void)
{
    pthread_once(&storage_once, initialize_paths);
    return file_index_path;
}

const char *storage_chat_log_file(void)
{
    pthread_once(&storage_once, initialize_paths);
    return chat_log_path;
}

int storage_ensure_directory(const char *path)
{
    return ensure_directory_chain(path);
}

int storage_init(void)
{
    pthread_once(&storage_once, initialize_paths);
    if (ensure_directory_chain(storage_data_dir()) < 0 ||
        ensure_directory_chain(storage_logs_dir()) < 0 ||
        ensure_directory_chain(storage_upload_dir()) < 0 ||
        ensure_directory_chain(storage_download_dir()) < 0 ||
        ensure_directory_chain(storage_backup_dir()) < 0 ||
        ensure_parent_directory(storage_users_file()) < 0 ||
        ensure_parent_directory(storage_posts_file()) < 0 ||
        ensure_parent_directory(storage_replies_file()) < 0 ||
        ensure_parent_directory(storage_file_index()) < 0 ||
        ensure_parent_directory(storage_chat_log_file()) < 0) {
        return -1;
    }

    open_and_close(storage_users_file());
    open_and_close(storage_posts_file());
    open_and_close(storage_replies_file());
    open_and_close(storage_file_index());
    open_and_close(storage_chat_log_file());
    return 0;
}

int storage_copy_file(const char *source_path, const char *destination_path)
{
    FILE *source;
    FILE *destination;
    unsigned char buffer[8192];
    size_t count;
    int result = -1;

    if (ensure_parent_directory(destination_path) < 0) {
        return -1;
    }

    source = fopen(source_path, "rb");
    if (source == NULL) {
        return -1;
    }
    destination = fopen(destination_path, "wb");
    if (destination == NULL) {
        fclose(source);
        return -1;
    }

    while ((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, count, destination) != count) {
            goto done;
        }
    }
    if (ferror(source)) {
        goto done;
    }
    if (fflush(destination) != 0) {
        goto done;
    }
    result = 0;

done:
    fclose(source);
    if (fclose(destination) != 0) {
        result = -1;
    }
    return result;
}

static int copy_tree(const char *source_path, const char *destination_path);

static int copy_directory_contents(const char *source_path,
                                   const char *destination_path)
{
    DIR *directory;
    struct dirent *entry;
    int result = 0;

    if (ensure_directory_chain(destination_path) < 0) {
        return -1;
    }

    directory = opendir(source_path);
    if (directory == NULL) {
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        char source_child[PATH_MAX];
        char destination_child[PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (path_join(source_child, sizeof(source_child), source_path,
                      entry->d_name) < 0 ||
            path_join(destination_child, sizeof(destination_child),
                      destination_path, entry->d_name) < 0) {
            result = -1;
            break;
        }
        if (copy_tree(source_child, destination_child) < 0) {
            result = -1;
            break;
        }
    }
    closedir(directory);
    return result;
}

static int copy_tree(const char *source_path, const char *destination_path)
{
    struct stat status;

    if (stat(source_path, &status) < 0) {
        return -1;
    }
    if (S_ISDIR(status.st_mode)) {
        return copy_directory_contents(source_path, destination_path);
    }
    return storage_copy_file(source_path, destination_path);
}

static void sanitize_label(const char *input, char *output, size_t capacity)
{
    size_t index = 0;

    if (input == NULL || *input == '\0') {
        time_t now = time(NULL);
        struct tm local_time;

        if (localtime_r(&now, &local_time) != NULL) {
            strftime(output, capacity, "%Y%m%d-%H%M%S", &local_time);
        } else {
            snprintf(output, capacity, "snapshot");
        }
        return;
    }

    while (input[index] != '\0' && index + 1 < capacity) {
        unsigned char ch = (unsigned char)input[index];
        output[index] = (isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
                            ? (char)ch
                            : '_';
        index++;
    }
    output[index] = '\0';
    if (index == 0) {
        snprintf(output, capacity, "snapshot");
    }
}

int storage_backup_snapshot(const char *label, char *snapshot_path,
                            size_t snapshot_path_capacity)
{
    char sanitized[128];
    char snapshot_root[PATH_MAX];
    char data_target[PATH_MAX];
    char logs_target[PATH_MAX];
    char uploads_target[PATH_MAX];

    pthread_once(&storage_once, initialize_paths);
    sanitize_label(label, sanitized, sizeof(sanitized));
    if (path_join(snapshot_root, sizeof(snapshot_root), storage_backup_dir(),
                  sanitized) < 0 ||
        path_join(data_target, sizeof(data_target), snapshot_root, "data") < 0 ||
        path_join(logs_target, sizeof(logs_target), snapshot_root, "logs") < 0 ||
        path_join(uploads_target, sizeof(uploads_target), snapshot_root,
                  "uploads") < 0) {
        return -1;
    }

    if (ensure_directory_chain(snapshot_root) < 0 ||
        ensure_directory_chain(data_target) < 0 ||
        ensure_directory_chain(logs_target) < 0 ||
        ensure_directory_chain(uploads_target) < 0) {
        return -1;
    }

    if (access(storage_users_file(), R_OK) == 0) {
        char destination[PATH_MAX];

        if (path_join(destination, sizeof(destination), data_target,
                      "users.db") < 0 ||
            storage_copy_file(storage_users_file(), destination) < 0) {
            return -1;
        }
    }
    if (access(storage_posts_file(), R_OK) == 0) {
        char destination[PATH_MAX];

        if (path_join(destination, sizeof(destination), data_target,
                      "posts.db") < 0 ||
            storage_copy_file(storage_posts_file(), destination) < 0) {
            return -1;
        }
    }
    if (access(storage_replies_file(), R_OK) == 0) {
        char destination[PATH_MAX];

        if (path_join(destination, sizeof(destination), data_target,
                      "replies.db") < 0 ||
            storage_copy_file(storage_replies_file(), destination) < 0) {
            return -1;
        }
    }
    if (access(storage_file_index(), R_OK) == 0) {
        char destination[PATH_MAX];

        if (path_join(destination, sizeof(destination), data_target,
                      "files.db") < 0 ||
            storage_copy_file(storage_file_index(), destination) < 0) {
            return -1;
        }
    }
    if (access(storage_chat_log_file(), R_OK) == 0) {
        char destination[PATH_MAX];

        if (path_join(destination, sizeof(destination), logs_target,
                      "chat.log") < 0 ||
            storage_copy_file(storage_chat_log_file(), destination) < 0) {
            return -1;
        }
    }
    if (copy_tree(storage_upload_dir(), uploads_target) < 0) {
        return -1;
    }
    if (snapshot_path != NULL && snapshot_path_capacity > 0) {
        if (snprintf(snapshot_path, snapshot_path_capacity, "%s",
                     snapshot_root) >= (int)snapshot_path_capacity) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    return 0;
}
