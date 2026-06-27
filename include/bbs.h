#ifndef BBS_H
#define BBS_H

#include <stddef.h>

#define BBS_TITLE_LENGTH 128
#define BBS_CONTENT_LENGTH 2048
#define BBS_USERNAME_LENGTH 31

typedef struct {
    unsigned long id;
    char author[BBS_USERNAME_LENGTH + 1];
    char title[BBS_TITLE_LENGTH + 1];
    char content[BBS_CONTENT_LENGTH + 1];
    char created_at[20];
    char updated_at[20];
    int active;
} BbsPostRecord;

typedef struct {
    unsigned long id;
    unsigned long post_id;
    char author[BBS_USERNAME_LENGTH + 1];
    char content[BBS_CONTENT_LENGTH + 1];
    char created_at[20];
    int active;
} BbsReplyRecord;

typedef struct {
    unsigned long id;
    char owner[BBS_USERNAME_LENGTH + 1];
    char sender[BBS_USERNAME_LENGTH + 1];
    char recipient[BBS_USERNAME_LENGTH + 1];
    char filename[128];
    char stored_name[256];
    unsigned long long size;
    char created_at[20];
    int active;
} BbsFileRecord;

typedef int (*BbsPostVisitor)(const BbsPostRecord *record, void *context);
typedef int (*BbsReplyVisitor)(const BbsReplyRecord *record, void *context);
typedef int (*BbsFileVisitor)(const BbsFileRecord *record, void *context);

int bbs_init(void);
int bbs_append_post(const BbsPostRecord *record);
int bbs_append_reply(const BbsReplyRecord *record);
int bbs_append_file_record(const BbsFileRecord *record);
int bbs_next_post_id(unsigned long *id);
int bbs_next_reply_id(unsigned long *id);
int bbs_next_file_id(unsigned long *id);
int bbs_post_count(unsigned long *count);
int bbs_reply_count(unsigned long post_id, unsigned long *count);
int bbs_file_count(unsigned long *count);
int bbs_read_post(unsigned long post_id, BbsPostRecord *record);
int bbs_read_reply(unsigned long reply_id, BbsReplyRecord *record);
int bbs_read_file(unsigned long file_id, BbsFileRecord *record);
int bbs_visit_posts(BbsPostVisitor visitor, void *context);
int bbs_visit_replies(unsigned long post_id, BbsReplyVisitor visitor,
                      void *context);
int bbs_visit_files(BbsFileVisitor visitor, void *context);
int bbs_backup(char *snapshot_path, size_t snapshot_path_capacity);
int bbs_backup_named(const char *label, char *snapshot_path,
                     size_t snapshot_path_capacity);

#endif
