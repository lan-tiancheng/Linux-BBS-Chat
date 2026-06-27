#include "chat.h"
#include "bbs.h"
#include "file_transfer.h"
#include "protocol.h"
#include "storage.h"
#include "server.h"
#include "user.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int active;
    int sockfd;
    unsigned long id;
    char peer[INET_ADDRSTRLEN];
    unsigned short port;
    int logged_in;
    char username[MAX_USERNAME_LENGTH + 1];
    pthread_mutex_t send_mutex;
} ClientSlot;

static ClientSlot clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long next_client_id = 1;

static int slot_send_line(ClientSlot *client, const char *line)
{
    int result;

    pthread_mutex_lock(&client->send_mutex);
    result = send_line(client->sockfd, line);
    pthread_mutex_unlock(&client->send_mutex);
    return result;
}

static ClientSlot *register_client(int sockfd, const struct sockaddr_in *address)
{
    ClientSlot *selected = NULL;
    size_t i;

    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            selected = &clients[i];
            selected->active = 1;
            selected->sockfd = sockfd;
            selected->id = next_client_id++;
            selected->port = ntohs(address->sin_port);
            selected->logged_in = 0;
            selected->username[0] = '\0';
            if (inet_ntop(AF_INET, &address->sin_addr, selected->peer,
                          sizeof(selected->peer)) == NULL) {
                snprintf(selected->peer, sizeof(selected->peer), "unknown");
            }
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return selected;
}

static void unregister_client(ClientSlot *client)
{
    pthread_mutex_lock(&clients_mutex);
    client->active = 0;
    client->sockfd = -1;
    client->logged_in = 0;
    client->username[0] = '\0';
    pthread_mutex_unlock(&clients_mutex);
}

static int parse_credentials(const char *argument, char *username,
                             char *password)
{
    char extra;

    if (argument == NULL) {
        return 0;
    }
    return sscanf(argument, "%31s %63s %c", username, password, &extra) == 2;
}

static int client_is_logged_in(ClientSlot *client)
{
    int logged_in;

    pthread_mutex_lock(&clients_mutex);
    logged_in = client->active && client->logged_in;
    pthread_mutex_unlock(&clients_mutex);
    return logged_in;
}

static int login_client(ClientSlot *client, const char *username)
{
    size_t i;
    int success = 1;

    pthread_mutex_lock(&clients_mutex);
    if (client->logged_in) {
        success = 0;
    }
    for (i = 0; success && i < MAX_CLIENTS; i++) {
        if (&clients[i] != client && clients[i].active &&
            clients[i].logged_in &&
            strcmp(clients[i].username, username) == 0) {
            success = 0;
        }
    }
    if (success) {
        client->logged_in = 1;
        snprintf(client->username, sizeof(client->username), "%s", username);
    }
    pthread_mutex_unlock(&clients_mutex);
    return success;
}

static int logout_client(ClientSlot *client)
{
    int was_logged_in;

    pthread_mutex_lock(&clients_mutex);
    was_logged_in = client->logged_in;
    client->logged_in = 0;
    client->username[0] = '\0';
    pthread_mutex_unlock(&clients_mutex);
    return was_logged_in;
}

static void copy_client_username(ClientSlot *client, char *username,
                                 size_t capacity)
{
    pthread_mutex_lock(&clients_mutex);
    snprintf(username, capacity, "%s", client->username);
    pthread_mutex_unlock(&clients_mutex);
}

static void format_timestamp(char *buffer, size_t capacity)
{
    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) == NULL ||
        strftime(buffer, capacity, "%Y-%m-%d %H:%M:%S", &local_time) == 0) {
        snprintf(buffer, capacity, "unknown");
    }
}

typedef struct {
    ClientSlot *client;
    unsigned long count;
} PostListContext;

typedef struct {
    ClientSlot *client;
    unsigned long count;
} ReplyListContext;

static int send_post_list_item(const BbsPostRecord *post, void *context)
{
    PostListContext *list = context;
    char line[MAX_LINE_LENGTH + 128];

    list->count++;
    snprintf(line, sizeof(line), "%lu | %s | %s", post->id, post->author,
             post->title);
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static int send_reply_list_item(const BbsReplyRecord *reply, void *context)
{
    ReplyListContext *list = context;
    char line[MAX_LINE_LENGTH * 3];

    list->count++;
    snprintf(line, sizeof(line), "%lu | %s | %s", reply->id, reply->author,
             reply->content);
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static void notify_file_ready(const char *sender, const char *recipient,
                              const char *filename,
                              unsigned long long size)
{
    char response[MAX_LINE_LENGTH];
    size_t i;

    snprintf(response, sizeof(response), "FILE_READY %s %s %llu", sender,
             filename, size);
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].logged_in &&
            strcmp(clients[i].username, recipient) == 0) {
            (void)slot_send_line(&clients[i], response);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static int send_download(ClientSlot *client, const char *filename)
{
    char username[MAX_USERNAME_LENGTH + 1];
    char header[MAX_LINE_LENGTH];
    unsigned long long size;
    FILE *file;
    int result = -1;

    copy_client_username(client, username, sizeof(username));
    if (file_open_download(username, filename, &file, &size) < 0) {
        return slot_send_line(client, "ERR file not found") < 0 ? -1 : 0;
    }

    snprintf(header, sizeof(header), "FILE %s %llu", filename, size);
    pthread_mutex_lock(&client->send_mutex);
    if (send_line(client->sockfd, header) == 0 &&
        file_send_contents(client->sockfd, file, size) == 0) {
        result = 0;
    }
    pthread_mutex_unlock(&client->send_mutex);
    fclose(file);
    return result;
}

static int broadcast_message(const ClientSlot *sender, const char *message)
{
    char response[MAX_LINE_LENGTH + 64];
    size_t i;

    snprintf(response, sizeof(response), "MSG %s %s", sender->username,
             message);
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].logged_in) {
            (void)slot_send_line(&clients[i], response);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return chat_log_group(sender->username, message);
}

static int private_message(const ClientSlot *sender, const char *recipient,
                           const char *message)
{
    char response[MAX_LINE_LENGTH + 64];
    int delivery = 0;
    size_t i;

    snprintf(response, sizeof(response), "PMSG %s %s", sender->username,
             message);
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].logged_in &&
            strcmp(clients[i].username, recipient) == 0) {
            delivery = slot_send_line(&clients[i], response) == 0 ? 1 : -1;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (delivery == 1 &&
        chat_log_private(sender->username, recipient, message) < 0) {
        return -2;
    }
    return delivery;
}

static int append_uploaded_file_record(const char *sender,
                                       const char *recipient,
                                       const char *filename,
                                       unsigned long long size)
{
    BbsFileRecord record;
    char timestamp[20];

    memset(&record, 0, sizeof(record));
    format_timestamp(timestamp, sizeof(timestamp));
    if (bbs_next_file_id(&record.id) < 0) {
        return -1;
    }
    snprintf(record.owner, sizeof(record.owner), "%s", recipient);
    snprintf(record.sender, sizeof(record.sender), "%s", sender);
    snprintf(record.recipient, sizeof(record.recipient), "%s", recipient);
    snprintf(record.filename, sizeof(record.filename), "%s", filename);
    if (file_server_path(recipient, filename, record.stored_name,
                         sizeof(record.stored_name)) < 0) {
        return -1;
    }
    record.size = size;
    snprintf(record.created_at, sizeof(record.created_at), "%s", timestamp);
    record.active = 1;
    return bbs_append_file_record(&record);
}

static void send_client_list(ClientSlot *requester)
{
    char response[MAX_LINE_LENGTH];
    size_t used;
    size_t count = 0;
    size_t i;

    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].logged_in) {
            count++;
        }
    }
    used = (size_t)snprintf(response, sizeof(response), "ONLINE %zu", count);
    for (i = 0; i < MAX_CLIENTS && used < sizeof(response); i++) {
        int written;

        if (!clients[i].active || !clients[i].logged_in) {
            continue;
        }
        written = snprintf(response + used, sizeof(response) - used,
                           " %s", clients[i].username);
        if (written < 0 || (size_t)written >= sizeof(response) - used) {
            break;
        }
        used += (size_t)written;
    }
    pthread_mutex_unlock(&clients_mutex);
    (void)slot_send_line(requester, response);
}

/* Return nonzero when this client connection should close. */
static int handle_command(ClientSlot *client, char *line)
{
    char response[MAX_LINE_LENGTH + 64];
    char *argument;

    if (strcmp(line, "PING") == 0) {
        return slot_send_line(client, "OK PONG") < 0;
    }
    if (strcmp(line, "HELP") == 0) {
        return slot_send_line(
                   client,
                   "COMMANDS REGISTER <user> <pass> LOGIN <user> <pass> "
                   "LOGOUT WHO GROUP <text> PRIVATE <user> <text> "
                   "UPLOAD <user> <local-path> DOWNLOAD <filename> "
                   "POST <title> <content> REPLY <post_id> <content> "
                   "LISTPOST VIEWPOST <post_id> BACKUP [label] "
                   "PING ECHO <text> HELP QUIT") < 0;
    }
    if (strcmp(line, "WHO") == 0) {
        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        send_client_list(client);
        return 0;
    }
    if (strcmp(line, "LOGOUT") == 0) {
        if (!logout_client(client)) {
            return slot_send_line(client, "ERR not logged in") < 0;
        }
        return slot_send_line(client, "OK logged out") < 0;
    }
    if (strcmp(line, "QUIT") == 0) {
        (void)slot_send_line(client, "OK bye");
        return 1;
    }

    argument = strchr(line, ' ');
    if (argument != NULL) {
        *argument++ = '\0';
        while (*argument == ' ') {
            argument++;
        }
    }

    if (strcmp(line, "ECHO") == 0) {
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client, "ERR ECHO requires text") < 0;
        }
        snprintf(response, sizeof(response), "ECHO %s", argument);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "REGISTER") == 0) {
        char username[MAX_USERNAME_LENGTH + 1];
        char password[MAX_PASSWORD_LENGTH + 1];
        UserResult result;

        if (!parse_credentials(argument, username, password)) {
            return slot_send_line(client,
                                  "ERR usage: REGISTER <user> <pass>") < 0;
        }
        if (client_is_logged_in(client)) {
            return slot_send_line(client,
                                  "ERR logout before registering") < 0;
        }
        result = user_register(username, password);
        if (result != USER_OK) {
            snprintf(response, sizeof(response), "ERR %s",
                     user_result_message(result));
            return slot_send_line(client, response) < 0;
        }
        return slot_send_line(client, "OK registered") < 0;
    }
    if (strcmp(line, "LOGIN") == 0) {
        char username[MAX_USERNAME_LENGTH + 1];
        char password[MAX_PASSWORD_LENGTH + 1];
        UserResult result;

        if (!parse_credentials(argument, username, password)) {
            return slot_send_line(client,
                                  "ERR usage: LOGIN <user> <pass>") < 0;
        }
        if (client_is_logged_in(client)) {
            return slot_send_line(client, "ERR already logged in") < 0;
        }
        result = user_authenticate(username, password);
        if (result != USER_OK) {
            snprintf(response, sizeof(response), "ERR %s",
                     user_result_message(result));
            return slot_send_line(client, response) < 0;
        }
        if (!login_client(client, username)) {
            return slot_send_line(client,
                                  "ERR user already logged in") < 0;
        }
        snprintf(response, sizeof(response), "OK logged in %s", username);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "GROUP") == 0 || strcmp(line, "BROADCAST") == 0) {
        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client, "ERR GROUP requires text") < 0;
        }
        if (broadcast_message(client, argument) < 0) {
            return slot_send_line(client,
                                  "ERR message delivered but log failed") < 0;
        }
        return 0;
    }
    if (strcmp(line, "PRIVATE") == 0) {
        char *message;
        int delivery;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (message = strchr(argument, ' ')) == NULL) {
            return slot_send_line(
                       client, "ERR usage: PRIVATE <user> <text>") < 0;
        }
        *message++ = '\0';
        while (*message == ' ') {
            message++;
        }
        if (!user_valid_username(argument) || *message == '\0') {
            return slot_send_line(
                       client, "ERR usage: PRIVATE <user> <text>") < 0;
        }
        delivery = private_message(client, argument, message);
        if (delivery == 0) {
            return slot_send_line(client, "ERR user is not online") < 0;
        }
        if (delivery == -1) {
            return slot_send_line(client, "ERR private delivery failed") < 0;
        }
        if (delivery == -2) {
            return slot_send_line(
                       client, "ERR message delivered but log failed") < 0;
        }
        return slot_send_line(client, "OK private message sent") < 0;
    }
    if (strcmp(line, "UPLOAD") == 0) {
        char recipient[MAX_USERNAME_LENGTH + 1];
        char filename[MAX_FILENAME_LENGTH + 1];
        char size_text[32];
        char sender[MAX_USERNAME_LENGTH + 1];
        char extra;
        unsigned long long size;
        int recipient_exists;

        if (argument == NULL ||
            sscanf(argument, "%31s %127s %31s %c", recipient, filename,
                   size_text, &extra) != 3) {
            return slot_send_line(
                       client,
                       "ERR usage: UPLOAD <user> <filename> <size>") < 0;
        }
        if (file_parse_size(size_text, &size) < 0) {
            (void)slot_send_line(client, "ERR invalid file size");
            return 1;
        }
        if (!user_valid_username(recipient) || !file_name_valid(filename)) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client,
                                  "ERR invalid recipient or filename") < 0;
        }
        if (!client_is_logged_in(client)) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client, "ERR login required") < 0;
        }
        recipient_exists = user_exists(recipient);
        if (recipient_exists <= 0) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(
                       client, recipient_exists == 0
                                   ? "ERR recipient does not exist"
                                   : "ERR user storage error") < 0;
        }
        if (file_store_upload(client->sockfd, recipient, filename, size) < 0) {
            return slot_send_line(client, "ERR upload failed") < 0;
        }
        copy_client_username(client, sender, sizeof(sender));
        if (append_uploaded_file_record(sender, recipient, filename, size) <
            0) {
            char path[4096];

            if (file_server_path(recipient, filename, path, sizeof(path)) ==
                0) {
                unlink(path);
            }
            return slot_send_line(client, "ERR upload record failed") < 0;
        }
        notify_file_ready(sender, recipient, filename, size);
        snprintf(response, sizeof(response), "OK uploaded %s for %s", filename,
                 recipient);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "DOWNLOAD") == 0) {
        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || strchr(argument, ' ') != NULL ||
            !file_name_valid(argument)) {
            return slot_send_line(client,
                                  "ERR usage: DOWNLOAD <filename>") < 0;
        }
        return send_download(client, argument) < 0;
    }
    if (strcmp(line, "POST") == 0) {
        char *title;
        char *content;
        BbsPostRecord record;
        char timestamp[20];

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client,
                                  "ERR usage: POST <title> <content>") < 0;
        }
        title = argument;
        if ((content = strchr(title, ' ')) == NULL) {
            return slot_send_line(client,
                                  "ERR usage: POST <title> <content>") < 0;
        }
        *content++ = '\0';
        while (*content == ' ') {
            content++;
        }
        if (*title == '\0' || *content == '\0') {
            return slot_send_line(client,
                                  "ERR usage: POST <title> <content>") < 0;
        }
        memset(&record, 0, sizeof(record));
        if (bbs_next_post_id(&record.id) < 0) {
            return slot_send_line(client, "ERR post id failed") < 0;
        }
        copy_client_username(client, record.author, sizeof(record.author));
        snprintf(record.title, sizeof(record.title), "%s", title);
        snprintf(record.content, sizeof(record.content), "%s", content);
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(record.created_at, sizeof(record.created_at), "%s", timestamp);
        snprintf(record.updated_at, sizeof(record.updated_at), "%s", timestamp);
        record.active = 1;
        if (bbs_append_post(&record) < 0) {
            return slot_send_line(client, "ERR post save failed") < 0;
        }
        snprintf(response, sizeof(response), "OK post %lu created", record.id);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "REPLY") == 0) {
        char *message;
        unsigned long post_id;
        char *end = NULL;
        BbsReplyRecord record;
        char timestamp[20];

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (message = strchr(argument, ' ')) == NULL) {
            return slot_send_line(client,
                                  "ERR usage: REPLY <post_id> <content>") < 0;
        }
        *message++ = '\0';
        while (*message == ' ') {
            message++;
        }
        errno = 0;
        post_id = strtoul(argument, &end, 10);
        if (errno != 0 || end == NULL || *end != '\0' || post_id == 0) {
            return slot_send_line(client, "ERR invalid post id") < 0;
        }
        if (bbs_read_post(post_id, &(BbsPostRecord){0}) < 0) {
            return slot_send_line(client, "ERR post not found") < 0;
        }
        memset(&record, 0, sizeof(record));
        if (bbs_next_reply_id(&record.id) < 0) {
            return slot_send_line(client, "ERR reply id failed") < 0;
        }
        record.post_id = post_id;
        copy_client_username(client, record.author, sizeof(record.author));
        snprintf(record.content, sizeof(record.content), "%s", message);
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(record.created_at, sizeof(record.created_at), "%s", timestamp);
        record.active = 1;
        if (bbs_append_reply(&record) < 0) {
            return slot_send_line(client, "ERR reply save failed") < 0;
        }
        snprintf(response, sizeof(response), "OK reply %lu created", record.id);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "LISTPOST") == 0) {
        PostListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (slot_send_line(client, "OK posts") < 0) {
            return 1;
        }
        context.client = client;
        context.count = 0;
        if (bbs_visit_posts(send_post_list_item, &context) < 0) {
            return slot_send_line(client, "ERR list failed") < 0;
        }
        snprintf(response, sizeof(response), "OK total %lu", context.count);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "VIEWPOST") == 0) {
        unsigned long post_id;
        char *end = NULL;
        BbsPostRecord post;
        ReplyListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client, "ERR usage: VIEWPOST <post_id>") < 0;
        }
        errno = 0;
        post_id = strtoul(argument, &end, 10);
        if (errno != 0 || end == NULL || *end != '\0' || post_id == 0) {
            return slot_send_line(client, "ERR invalid post id") < 0;
        }
        if (bbs_read_post(post_id, &post) < 0) {
            return slot_send_line(client, "ERR post not found") < 0;
        }
        snprintf(response, sizeof(response), "POST %lu %s %s", post.id,
                 post.author, post.title);
        if (slot_send_line(client, response) < 0 ||
            slot_send_line(client, post.content) < 0) {
            return 1;
        }
        context.client = client;
        context.count = 0;
        if (bbs_visit_replies(post_id, send_reply_list_item, &context) < 0) {
            return slot_send_line(client, "ERR reply list failed") < 0;
        }
        snprintf(response, sizeof(response), "OK replies %lu", context.count);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "BACKUP") == 0) {
        char snapshot_path[PATH_MAX];
        char backup_response[PATH_MAX + 32];
        const char *label = argument != NULL && *argument != '\0'
                                ? argument
                                : "manual";

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (bbs_backup_named(label, snapshot_path, sizeof(snapshot_path)) < 0) {
            return slot_send_line(client, "ERR backup failed") < 0;
        }
        snprintf(backup_response, sizeof(backup_response),
                 "OK backup created %s",
                 snapshot_path);
        return slot_send_line(client, backup_response) < 0;
    }

    return slot_send_line(client, "ERR unknown command; send HELP") < 0;
}

static void *client_thread(void *argument)
{
    ClientSlot *client = argument;
    char line[MAX_LINE_LENGTH];
    int sockfd = client->sockfd;

    printf("client-%lu connected from %s:%hu\n", client->id, client->peer,
           client->port);
    (void)slot_send_line(client, "OK connected; send HELP for commands");

    for (;;) {
        int result = recv_line(client->sockfd, line, sizeof(line));

        if (result == 0) {
            break;
        }
        if (result == -2) {
            if (slot_send_line(client, "ERR line too long") < 0) {
                break;
            }
            continue;
        }
        if (result < 0 || handle_command(client, line)) {
            break;
        }
    }

    printf("client-%lu disconnected\n", client->id);
    unregister_client(client);
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    return NULL;
}

int run_server(int port)
{
    int listen_fd;
    int reuse = 1;
    struct sockaddr_in address;
    size_t i;

    signal(SIGPIPE, SIG_IGN);
    if (bbs_init() < 0) {
        perror("initialize storage");
        return 1;
    }
    if (user_store_init() < 0) {
        perror("initialize user store");
        return 1;
    }
    if (chat_log_init() < 0) {
        perror("initialize chat log");
        return 1;
    }
    if (file_transfer_init() < 0) {
        perror("initialize upload directory");
        return 1;
    }
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sockfd = -1;
        pthread_mutex_init(&clients[i].send_mutex, NULL);
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("server listening on 0.0.0.0:%d\n", port);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        int client_fd = accept(listen_fd, (struct sockaddr *)&peer,
                               &peer_length);
        ClientSlot *client;
        pthread_t thread;

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        client = register_client(client_fd, &peer);
        if (client == NULL) {
            (void)send_line(client_fd, "ERR server full");
            close(client_fd);
            continue;
        }
        if (pthread_create(&thread, NULL, client_thread, client) != 0) {
            perror("pthread_create");
            unregister_client(client);
            close(client_fd);
            continue;
        }
        pthread_detach(thread);
    }
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 2;
    }
    if (argc == 2) {
        port = parse_port(argv[1]);
        if (port < 0) {
            fprintf(stderr, "invalid port: %s\n", argv[1]);
            return 2;
        }
    }
    return run_server(port);
}
