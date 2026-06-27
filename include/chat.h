#ifndef CHAT_H
#define CHAT_H

int chat_log_init(void);
int chat_log_group(const char *sender, const char *message);
int chat_log_private(const char *sender, const char *recipient,
                     const char *message);

#endif
