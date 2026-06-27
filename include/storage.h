#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

#ifndef STORAGE_PATH_CAPACITY
#define STORAGE_PATH_CAPACITY 4096
#endif

const char *storage_data_dir(void);
const char *storage_logs_dir(void);
const char *storage_upload_dir(void);
const char *storage_download_dir(void);
const char *storage_backup_dir(void);

const char *storage_users_file(void);
const char *storage_posts_file(void);
const char *storage_replies_file(void);
const char *storage_file_index(void);
const char *storage_chat_log_file(void);

int storage_ensure_directory(const char *path);
int storage_init(void);
int storage_copy_file(const char *source_path, const char *destination_path);
int storage_backup_snapshot(const char *label, char *snapshot_path,
                            size_t snapshot_path_capacity);

#endif
