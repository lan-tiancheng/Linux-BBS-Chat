#include "file_transfer.h"

#include "protocol.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_UPLOAD_DIR "uploads/chat"
#define DEFAULT_DOWNLOAD_DIR "downloads"
#define FILE_CHUNK_SIZE 8192

static const char *upload_directory(void)
{
    const char *configured = getenv("BBS_UPLOAD_DIR");
    return configured != NULL && *configured != '\0' ? configured
                                                       : DEFAULT_UPLOAD_DIR;
}

static const char *download_directory(void)
{
    const char *configured = getenv("BBS_DOWNLOAD_DIR");
    return configured != NULL && *configured != '\0' ? configured
                                                       : DEFAULT_DOWNLOAD_DIR;
}

static int ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int build_server_path(char *path, size_t capacity, const char *recipient,
                             const char *filename)
{
    int written = snprintf(path, capacity, "%s/%s__%s", upload_directory(),
                           recipient, filename);
    return written >= 0 && (size_t)written < capacity ? 0 : -1;
}

int file_transfer_init(void)
{
    if (strcmp(upload_directory(), DEFAULT_UPLOAD_DIR) == 0 &&
        ensure_directory("uploads") < 0) {
        return -1;
    }
    return ensure_directory(upload_directory());
}

int file_name_valid(const char *filename)
{
    size_t length = 0;

    if (filename == NULL || *filename == '\0' || strcmp(filename, ".") == 0 ||
        strcmp(filename, "..") == 0) {
        return 0;
    }
    while (filename[length] != '\0') {
        unsigned char character = (unsigned char)filename[length];
        if (!isalnum(character) && character != '.' && character != '_' &&
            character != '-') {
            return 0;
        }
        length++;
        if (length > MAX_FILENAME_LENGTH) {
            return 0;
        }
    }
    return 1;
}

int file_parse_size(const char *text, unsigned long long *size)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || *text == '\0' || size == NULL) {
        return -1;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > MAX_TRANSFER_SIZE) {
        return -1;
    }
    *size = value;
    return 0;
}

int file_local_info(const char *path, char *filename, size_t filename_capacity,
                    unsigned long long *size)
{
    const char *base;
    struct stat status;

    if (path == NULL || filename == NULL || size == NULL ||
        stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || (uintmax_t)status.st_size > MAX_TRANSFER_SIZE) {
        return -1;
    }
    base = strrchr(path, '/');
    base = base == NULL ? path : base + 1;
    if (!file_name_valid(base) || strlen(base) + 1 > filename_capacity) {
        return -1;
    }
    snprintf(filename, filename_capacity, "%s", base);
    *size = (unsigned long long)status.st_size;
    return 0;
}

int file_send_contents(int sockfd, FILE *file, unsigned long long size)
{
    unsigned char buffer[FILE_CHUNK_SIZE];
    unsigned long long remaining = size;

    while (remaining > 0) {
        size_t wanted = remaining < sizeof(buffer) ? (size_t)remaining
                                                   : sizeof(buffer);
        size_t count = fread(buffer, 1, wanted, file);
        if (count != wanted || send_all(sockfd, buffer, count) < 0) {
            return -1;
        }
        remaining -= count;
    }
    return 0;
}

int file_receive_contents(int sockfd, FILE *file, unsigned long long size)
{
    unsigned char buffer[FILE_CHUNK_SIZE];
    unsigned long long remaining = size;
    int write_failed = 0;

    while (remaining > 0) {
        size_t wanted = remaining < sizeof(buffer) ? (size_t)remaining
                                                   : sizeof(buffer);
        if (recv_all(sockfd, buffer, wanted) < 0) {
            return -1;
        }
        if (!write_failed && fwrite(buffer, 1, wanted, file) != wanted) {
            write_failed = 1;
        }
        remaining -= wanted;
    }
    return !write_failed && fflush(file) == 0 ? 0 : -1;
}

int file_discard_contents(int sockfd, unsigned long long size)
{
    unsigned char buffer[FILE_CHUNK_SIZE];
    unsigned long long remaining = size;

    while (remaining > 0) {
        size_t wanted = remaining < sizeof(buffer) ? (size_t)remaining
                                                   : sizeof(buffer);
        if (recv_all(sockfd, buffer, wanted) < 0) {
            return -1;
        }
        remaining -= wanted;
    }
    return 0;
}

int file_store_upload(int sockfd, const char *recipient, const char *filename,
                      unsigned long long size)
{
    char path[PATH_MAX];
    FILE *file;
    int result;

    if (build_server_path(path, sizeof(path), recipient, filename) < 0) {
        return -1;
    }
    file = fopen(path, "wbx");
    if (file == NULL) {
        (void)file_discard_contents(sockfd, size);
        return -1;
    }
    result = file_receive_contents(sockfd, file, size);
    if (fclose(file) != 0) {
        result = -1;
    }
    if (result < 0) {
        unlink(path);
    }
    return result;
}

int file_open_download(const char *recipient, const char *filename, FILE **file,
                       unsigned long long *size)
{
    char path[PATH_MAX];
    struct stat status;

    if (build_server_path(path, sizeof(path), recipient, filename) < 0 ||
        stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || (uintmax_t)status.st_size > MAX_TRANSFER_SIZE) {
        return -1;
    }
    *file = fopen(path, "rb");
    if (*file == NULL) {
        return -1;
    }
    *size = (unsigned long long)status.st_size;
    return 0;
}

int file_open_client_download(const char *filename, FILE **file,
                              char *saved_path, size_t saved_path_capacity)
{
    unsigned int suffix = 0;

    if (ensure_directory(download_directory()) < 0) {
        return -1;
    }
    for (;;) {
        int written;
        if (suffix == 0) {
            written = snprintf(saved_path, saved_path_capacity, "%s/%s",
                               download_directory(), filename);
        } else {
            written = snprintf(saved_path, saved_path_capacity, "%s/%u_%s",
                               download_directory(), suffix, filename);
        }
        if (written < 0 || (size_t)written >= saved_path_capacity) {
            return -1;
        }
        *file = fopen(saved_path, "wx");
        if (*file != NULL) {
            return 0;
        }
        if (errno != EEXIST || suffix == UINT_MAX) {
            return -1;
        }
        suffix++;
    }
}
