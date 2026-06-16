#include "protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>

int send_all(int sockfd, const void *buffer, size_t len)
{
    const char *cursor = buffer;
    size_t sent = 0;

    while (sent < len) {
        ssize_t result = send(sockfd, cursor + sent, len - sent, 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            return -1;
        }
        sent += (size_t)result;
    }
    return 0;
}

int recv_all(int sockfd, void *buffer, size_t len)
{
    char *cursor = buffer;
    size_t received = 0;

    while (received < len) {
        ssize_t result = recv(sockfd, cursor + received, len - received, 0);
        if (result == 0) {
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        received += (size_t)result;
    }
    return 0;
}

int send_line(int sockfd, const char *line)
{
    const char newline = '\n';
    size_t len = 0;

    while (line[len] != '\0') {
        len++;
    }
    if (send_all(sockfd, line, len) < 0) {
        return -1;
    }
    return send_all(sockfd, &newline, 1);
}

int recv_line(int sockfd, char *buffer, size_t capacity)
{
    size_t used = 0;
    int overflow = 0;

    if (capacity == 0) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        char byte;
        ssize_t result = recv(sockfd, &byte, 1, 0);

        if (result == 0) {
            if (used == 0 && !overflow) {
                return 0;
            }
            break;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (byte == '\n') {
            break;
        }
        if (byte == '\r') {
            continue;
        }
        if (used + 1 < capacity) {
            buffer[used++] = byte;
        } else {
            overflow = 1;
        }
    }

    buffer[used] = '\0';
    return overflow ? -2 : (int)used;
}

int parse_port(const char *text)
{
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0') {
        return -1;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1 || value > 65535) {
        return -1;
    }
    return (int)value;
}
