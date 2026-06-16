#include "client.h"
#include "file_transfer.h"
#include "protocol.h"
#include "user.h"

#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int sockfd;
} ReceiverContext;

static int receive_download(int sockfd, const char *line)
{
    char filename[MAX_FILENAME_LENGTH + 1];
    char size_text[32];
    char extra;
    char saved_path[4096];
    unsigned long long size;
    FILE *file;

    if (sscanf(line, "FILE %127s %31s %c", filename, size_text, &extra) != 2 ||
        !file_name_valid(filename) || file_parse_size(size_text, &size) < 0) {
        return 0;
    }
    if (file_open_client_download(filename, &file, saved_path,
                                  sizeof(saved_path)) < 0) {
        fprintf(stderr, "\n[cannot create local download file]\n");
        file = tmpfile();
        if (file == NULL) {
            return -1;
        }
        if (file_receive_contents(sockfd, file, size) < 0) {
            fclose(file);
            return -1;
        }
        fclose(file);
        return 1;
    }
    if (file_receive_contents(sockfd, file, size) < 0) {
        fclose(file);
        unlink(saved_path);
        return -1;
    }
    fclose(file);
    printf("\n[file saved to %s]\n> ", saved_path);
    fflush(stdout);
    return 1;
}

static void *receiver_thread(void *argument)
{
    ReceiverContext *context = argument;
    char line[MAX_LINE_LENGTH + 64];

    for (;;) {
        int result = recv_line(context->sockfd, line, sizeof(line));

        if (result == 0) {
            printf("\n[connection closed by server]\n");
            break;
        }
        if (result == -2) {
            printf("\n[server response was too long]\n");
            continue;
        }
        if (result < 0) {
            perror("recv");
            break;
        }
        result = receive_download(context->sockfd, line);
        if (result < 0) {
            fprintf(stderr, "\n[file download failed]\n");
            break;
        }
        if (result > 0) {
            continue;
        }
        printf("\n%s\n> ", line);
        fflush(stdout);
    }
    return NULL;
}

static int connect_to_server(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    char port_text[16];
    int sockfd = -1;
    int status;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);

    status = getaddrinfo(host, port_text, &hints, &addresses);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    for (current = addresses; current != NULL; current = current->ai_next) {
        sockfd = socket(current->ai_family, current->ai_socktype,
                        current->ai_protocol);
        if (sockfd < 0) {
            continue;
        }
        if (connect(sockfd, current->ai_addr, current->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(addresses);
    return sockfd;
}

int run_client(const char *host, int port)
{
    int sockfd;
    pthread_t receiver;
    ReceiverContext context;
    char line[MAX_LINE_LENGTH];

    signal(SIGPIPE, SIG_IGN);
    sockfd = connect_to_server(host, port);
    if (sockfd < 0) {
        fprintf(stderr, "unable to connect to %s:%d\n", host, port);
        return 1;
    }

    context.sockfd = sockfd;
    if (pthread_create(&receiver, NULL, receiver_thread, &context) != 0) {
        perror("pthread_create");
        close(sockfd);
        return 1;
    }

    printf("connected to %s:%d\n", host, port);
    printf("type HELP to list commands\n> ");
    fflush(stdout);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);

        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        } else if (len == sizeof(line) - 1) {
            int byte;
            while ((byte = getchar()) != '\n' && byte != EOF) {
            }
            fprintf(stderr, "input line is too long\n> ");
            continue;
        }
        if (line[0] == '\0') {
            printf("> ");
            fflush(stdout);
            continue;
        }
        if (strncmp(line, "UPLOAD ", 7) == 0) {
            char target[32];
            char path[768];
            char filename[MAX_FILENAME_LENGTH + 1];
            char header[MAX_LINE_LENGTH];
            char extra;
            unsigned long long size;
            FILE *file;

            if (sscanf(line + 7, "%31s %767s %c", target, path, &extra) != 2 ||
                !user_valid_username(target) ||
                file_local_info(path, filename, sizeof(filename), &size) < 0) {
                fprintf(stderr,
                        "usage: UPLOAD <user> <path>; file names may use "
                        "letters, digits, dot, underscore, and hyphen\n> ");
                fflush(stderr);
                continue;
            }
            file = fopen(path, "rb");
            if (file == NULL) {
                perror("open upload file");
                printf("> ");
                fflush(stdout);
                continue;
            }
            snprintf(header, sizeof(header), "UPLOAD %s %s %llu", target,
                     filename, size);
            if (send_line(sockfd, header) < 0 ||
                file_send_contents(sockfd, file, size) < 0) {
                fclose(file);
                perror("upload");
                break;
            }
            fclose(file);
            printf("> ");
            fflush(stdout);
            continue;
        }
        if (send_line(sockfd, line) < 0) {
            perror("send");
            break;
        }
        if (strcmp(line, "QUIT") == 0) {
            break;
        }
        printf("> ");
        fflush(stdout);
    }

    shutdown(sockfd, SHUT_WR);
    pthread_join(receiver, NULL);
    close(sockfd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [host] [port]\n", argv[0]);
        return 2;
    }
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc == 3) {
        port = parse_port(argv[2]);
        if (port < 0) {
            fprintf(stderr, "invalid port: %s\n", argv[2]);
            return 2;
        }
    }
    return run_client(host, port);
}
