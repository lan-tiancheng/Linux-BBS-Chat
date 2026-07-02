#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define DEFAULT_PORT 8888
#define MAX_LINE_LENGTH 1024
#define MAX_CLIENTS 64

int send_all(int sockfd, const void *buffer, size_t len);
int recv_all(int sockfd, void *buffer, size_t len);
int send_line(int sockfd, const char *line);

/* Returns length, 0 on disconnect, -1 on error, or -2 for an overlong line. */
int recv_line(int sockfd, char *buffer, size_t capacity);

int parse_port(const char *text);

#endif
