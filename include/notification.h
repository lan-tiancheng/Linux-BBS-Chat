#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stddef.h>

#define NOTIFICATION_TYPE_LENGTH 48
#define NOTIFICATION_TARGET_LENGTH 128
#define NOTIFICATION_MESSAGE_LENGTH 512

typedef struct {
    unsigned long id;
    char account[32];
    char type[NOTIFICATION_TYPE_LENGTH + 1];
    char target[NOTIFICATION_TARGET_LENGTH + 1];
    char message[NOTIFICATION_MESSAGE_LENGTH + 1];
    char created_at[20];
    int read;
} NotificationRecord;

typedef int (*NotificationVisitor)(const NotificationRecord *record,
                                   void *context);

int notification_init(void);
int notification_add(const char *account, const char *type,
                     const char *target, const char *message,
                     unsigned long *id);
int notification_visit_for(const char *account, NotificationVisitor visitor,
                           void *context);
int notification_mark_read(const char *account, unsigned long id);
int notification_mark_all_read(const char *account);

#endif
