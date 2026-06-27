#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <stdio.h>

#define MAX_FILENAME_LENGTH 127
#define MAX_TRANSFER_SIZE (16ULL * 1024ULL * 1024ULL)

int file_transfer_init(void);
int file_name_valid(const char *filename);
int file_parse_size(const char *text, unsigned long long *size);
int file_local_info(const char *path, char *filename, size_t filename_capacity,
                    unsigned long long *size);
int file_send_contents(int sockfd, FILE *file, unsigned long long size);
int file_receive_contents(int sockfd, FILE *file, unsigned long long size);
int file_discard_contents(int sockfd, unsigned long long size);

int file_store_upload(int sockfd, const char *recipient, const char *filename,
                      unsigned long long size);
int file_server_path(const char *recipient, const char *filename, char *path,
                     size_t capacity);
int file_open_download(const char *recipient, const char *filename, FILE **file,
                       unsigned long long *size);
int file_open_client_download(const char *filename, FILE **file,
                              char *saved_path, size_t saved_path_capacity);

#endif
