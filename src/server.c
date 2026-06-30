#include "chat.h"
#include "bbs.h"
#include "file_transfer.h"
#include "protocol.h"
#include "storage.h"
#include "server.h"
#include "social.h"
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
    char nickname[MAX_NICKNAME_LENGTH + 1];
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
            selected->nickname[0] = '\0';
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
    client->nickname[0] = '\0';
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

static int parse_registration(const char *argument, char *account,
                              char *password, char *nickname)
{
    char extra;

    if (argument == NULL) {
        return 0;
    }
    return sscanf(argument, "%9s %63s %31s %c", account, password, nickname,
                  &extra) == 3;
}

static int client_is_logged_in(ClientSlot *client)
{
    int logged_in;

    pthread_mutex_lock(&clients_mutex);
    logged_in = client->active && client->logged_in;
    pthread_mutex_unlock(&clients_mutex);
    return logged_in;
}

static int login_client(ClientSlot *client, const char *account,
                        const char *nickname)
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
            strcmp(clients[i].username, account) == 0) {
            success = 0;
        }
    }
    if (success) {
        client->logged_in = 1;
        snprintf(client->username, sizeof(client->username), "%s", account);
        snprintf(client->nickname, sizeof(client->nickname), "%s", nickname);
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
    client->nickname[0] = '\0';
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

static int send_to_account(const char *account, const char *line)
{
    int delivered = 0;
    size_t i;

    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].logged_in &&
            strcmp(clients[i].username, account) == 0) {
            delivered = slot_send_line(&clients[i], line) == 0 ? 1 : -1;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return delivered;
}

static int private_message(const ClientSlot *sender, const char *recipient,
                           const char *message)
{
    char response[MAX_LINE_LENGTH + 64];

    snprintf(response, sizeof(response), "PMSG %s %s", sender->username,
             message);
    int delivery = send_to_account(recipient, response);
    if (delivery >= 0 &&
        chat_log_private(sender->username, recipient, message) < 0) {
        return -2;
    }
    return delivery;
}

static void copy_client_nickname(ClientSlot *client, char *nickname,
                                 size_t capacity)
{
    pthread_mutex_lock(&clients_mutex);
    snprintf(nickname, capacity, "%s", client->nickname);
    pthread_mutex_unlock(&clients_mutex);
}

static void copy_protocol_field(char *destination, size_t capacity,
                                const char *source)
{
    size_t i;

    if (capacity == 0) {
        return;
    }
    for (i = 0; i + 1 < capacity && source != NULL && source[i] != '\0'; i++) {
        char ch = source[i];
        destination[i] = (ch == '|' || ch == '\r' || ch == '\n') ? ' ' : ch;
    }
    destination[i] = '\0';
}

typedef struct {
    const char *owner;
    BbsFileRecord record;
    int found;
} BbsAttachmentLookup;

static int remember_attachment_for_owner(const BbsFileRecord *record,
                                         void *context)
{
    BbsAttachmentLookup *lookup = context;

    if (record->active && strcmp(record->owner, lookup->owner) == 0) {
        lookup->record = *record;
        lookup->found = 1;
    }
    return 0;
}

static int find_bbs_attachment(const char *owner, BbsFileRecord *record)
{
    BbsAttachmentLookup lookup;

    memset(&lookup, 0, sizeof(lookup));
    lookup.owner = owner;
    if (bbs_visit_files(remember_attachment_for_owner, &lookup) < 0 ||
        !lookup.found) {
        return -1;
    }
    if (record != NULL) {
        *record = lookup.record;
    }
    return 0;
}

static void post_attachment_owner(unsigned long post_id, char *owner,
                                  size_t capacity)
{
    snprintf(owner, capacity, "post:%lu", post_id);
}

static void reply_attachment_owner(unsigned long reply_id, char *owner,
                                   size_t capacity)
{
    snprintf(owner, capacity, "reply:%lu", reply_id);
}

static void format_attachment_name(const char *owner, char *name,
                                   size_t capacity)
{
    BbsFileRecord record;

    if (find_bbs_attachment(owner, &record) == 0) {
        copy_protocol_field(name, capacity, record.filename);
    } else {
        snprintf(name, capacity, "none");
    }
}

typedef struct {
    ClientSlot *client;
    unsigned long count;
} BbsListContext;

typedef struct {
    ClientSlot *client;
    unsigned long count;
} SocialListContext;

static int send_friend_item(const char *account, void *context)
{
    SocialListContext *list = context;
    UserRecord record;
    char line[MAX_LINE_LENGTH];

    if (user_find_by_account(account, &record) <= 0) {
        return 0;
    }
    snprintf(line, sizeof(line), "FRIEND %s|%s", record.account,
             record.nickname);
    list->count++;
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static int send_request_item(const char *from, const char *to,
                             const char *message, void *context)
{
    SocialListContext *list = context;
    UserRecord record;
    char line[MAX_LINE_LENGTH * 2];

    (void)to;
    if (user_find_by_account(from, &record) <= 0) {
        return 0;
    }
    snprintf(line, sizeof(line), "REQUEST %s|%s|%s", record.account,
             record.nickname, message);
    list->count++;
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static int send_sent_request_item(const char *from, const char *to,
                                  const char *message, void *context)
{
    SocialListContext *list = context;
    UserRecord record;
    char line[MAX_LINE_LENGTH * 2];

    (void)from;
    if (user_find_by_account(to, &record) <= 0) {
        return 0;
    }
    snprintf(line, sizeof(line), "SENT_REQUEST %s|%s|%s", record.account,
             record.nickname, message);
    list->count++;
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static int send_group_item(unsigned long id, const char *owner,
                           const char *name, void *context)
{
    SocialListContext *list = context;
    char line[MAX_LINE_LENGTH];

    snprintf(line, sizeof(line), "GROUP_ITEM %lu|%s|%s", id, owner, name);
    list->count++;
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

typedef struct {
    const char *line;
} GroupDeliveryContext;

static int deliver_group_member(const char *account, void *context)
{
    GroupDeliveryContext *delivery = context;

    (void)send_to_account(account, delivery->line);
    return 0;
}

static void copy_account(char *destination, size_t capacity,
                         const char *source)
{
    if (snprintf(destination, capacity, "%s", source) >= (int)capacity) {
        destination[0] = '\0';
    }
}

static int send_bbs_post_item(const BbsPostRecord *post, void *context)
{
    BbsListContext *list = context;
    char owner[32];
    char title[BBS_TITLE_LENGTH + 1];
    char content[BBS_CONTENT_LENGTH + 1];
    char attachment[128];
    char line[MAX_LINE_LENGTH * 3];

    if (!post->active) {
        return 0;
    }
    post_attachment_owner(post->id, owner, sizeof(owner));
    format_attachment_name(owner, attachment, sizeof(attachment));
    copy_protocol_field(title, sizeof(title), post->title);
    copy_protocol_field(content, sizeof(content), post->content);
    snprintf(line, sizeof(line), "BBS_POST %lu|%s|%s|%s|%s|%s", post->id,
             post->author, title, content, attachment, post->created_at);
    list->count++;
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static int send_bbs_reply_item(const BbsReplyRecord *reply, void *context)
{
    BbsListContext *list = context;
    char owner[32];
    char content[BBS_CONTENT_LENGTH + 1];
    char attachment[128];
    char line[MAX_LINE_LENGTH * 3];

    if (!reply->active) {
        return 0;
    }
    reply_attachment_owner(reply->id, owner, sizeof(owner));
    format_attachment_name(owner, attachment, sizeof(attachment));
    copy_protocol_field(content, sizeof(content), reply->content);
    snprintf(line, sizeof(line), "BBS_REPLY %lu|%lu|%s|%s|%s|%s",
             reply->id, reply->post_id, reply->author, content, attachment,
             reply->created_at);
    list->count++;
    return slot_send_line(list->client, line) < 0 ? -1 : 0;
}

static int append_bbs_attachment_record(const char *owner, const char *sender,
                                        const char *filename,
                                        const char *stored_path,
                                        unsigned long long size)
{
    BbsFileRecord record;
    char timestamp[20];

    memset(&record, 0, sizeof(record));
    format_timestamp(timestamp, sizeof(timestamp));
    if (bbs_next_file_id(&record.id) < 0) {
        return -1;
    }
    snprintf(record.owner, sizeof(record.owner), "%s", owner);
    snprintf(record.sender, sizeof(record.sender), "%s", sender);
    snprintf(record.recipient, sizeof(record.recipient), "%s", owner);
    snprintf(record.filename, sizeof(record.filename), "%s", filename);
    snprintf(record.stored_name, sizeof(record.stored_name), "%s",
             stored_path);
    record.size = size;
    snprintf(record.created_at, sizeof(record.created_at), "%s", timestamp);
    record.active = 1;
    return bbs_append_file_record(&record);
}

static int store_bbs_attachment(ClientSlot *client, const char *owner,
                                const char *filename,
                                unsigned long long size,
                                char *stored_path,
                                size_t stored_path_capacity)
{
    char safe_owner[32];
    FILE *file;
    int result;
    size_t i;

    for (i = 0; i + 1 < sizeof(safe_owner) && owner[i] != '\0'; i++) {
        safe_owner[i] = owner[i] == ':' ? '_' : owner[i];
    }
    safe_owner[i] = '\0';
    if (snprintf(stored_path, stored_path_capacity, "%s/%s__%s",
                 storage_upload_dir(), safe_owner, filename) >=
        (int)stored_path_capacity) {
        (void)file_discard_contents(client->sockfd, size);
        return -1;
    }
    file = fopen(stored_path, "wbx");
    if (file == NULL) {
        (void)file_discard_contents(client->sockfd, size);
        return -1;
    }
    result = file_receive_contents(client->sockfd, file, size);
    if (fclose(file) != 0) {
        result = -1;
    }
    if (result < 0) {
        unlink(stored_path);
    }
    return result;
}

static int send_bbs_attachment(ClientSlot *client, const char *owner)
{
    BbsFileRecord record;
    char header[MAX_LINE_LENGTH];
    FILE *file;
    int result;

    if (find_bbs_attachment(owner, &record) < 0) {
        return slot_send_line(client, "ERR attachment not found") < 0;
    }
    file = fopen(record.stored_name, "rb");
    if (file == NULL) {
        return slot_send_line(client, "ERR attachment not found") < 0;
    }
    snprintf(header, sizeof(header), "BBS_FILE %s %llu", record.filename,
             record.size);
    pthread_mutex_lock(&client->send_mutex);
    result = send_line(client->sockfd, header);
    if (result == 0) {
        result = file_send_contents(client->sockfd, file, record.size);
    }
    pthread_mutex_unlock(&client->send_mutex);
    fclose(file);
    return result < 0;
}

static int send_history(ClientSlot *client)
{
    FILE *file;
    char line[MAX_LINE_LENGTH * 2];

    if (slot_send_line(client, "HISTORY_BEGIN") < 0) {
        return 1;
    }
    file = fopen(storage_chat_log_file(), "r");
    if (file == NULL) {
        return slot_send_line(client, "HISTORY_END") < 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char timestamp[32];
        char type[16];
        char sender[MAX_USERNAME_LENGTH + 1];
        char recipient[MAX_USERNAME_LENGTH + 1];
        char message[MAX_LINE_LENGTH];
        char response[MAX_LINE_LENGTH * 2];

        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "[%31[^]]] [%15[^]]] %31s -> %31[^:]: %1087[^\n]",
                   timestamp, type, sender, recipient, message) == 5 &&
            strcmp(type, "PRIVATE") == 0) {
            snprintf(response, sizeof(response), "HPMSG %s|%s|%s|%s",
                     timestamp, sender, recipient, message);
            if (slot_send_line(client, response) < 0) {
                fclose(file);
                return 1;
            }
            continue;
        }
        if (sscanf(line, "[%31[^]]] [%15[^]]] %31s -> %31[^:]: %1087[^\n]",
                   timestamp, type, sender, recipient, message) == 5 &&
            strcmp(type, "GROUP") == 0) {
            snprintf(response, sizeof(response), "HMSG %s|%s|%s|%s",
                     timestamp, recipient, sender, message);
            if (slot_send_line(client, response) < 0) {
                fclose(file);
                return 1;
            }
            continue;
        }
        if (sscanf(line, "[%31[^]]] [%15[^]]] %31[^:]: %1087[^\n]",
                   timestamp, type, sender, message) == 4 &&
            strcmp(type, "GROUP") == 0) {
            snprintf(response, sizeof(response), "HMSG %s|group:0|%s|%s",
                     timestamp, sender, message);
            if (slot_send_line(client, response) < 0) {
                fclose(file);
                return 1;
            }
        }
    }
    fclose(file);
    return slot_send_line(client, "HISTORY_END") < 0;
}

static int send_private_history(ClientSlot *client, const char *peer)
{
    FILE *file;
    char line[MAX_LINE_LENGTH * 2];

    if (slot_send_line(client, "PRIVATE_HISTORY_BEGIN") < 0) {
        return 1;
    }
    file = fopen(storage_chat_log_file(), "r");
    if (file == NULL) {
        return slot_send_line(client, "PRIVATE_HISTORY_END") < 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char timestamp[32];
        char type[16];
        char sender[MAX_USERNAME_LENGTH + 1];
        char recipient[MAX_USERNAME_LENGTH + 1];
        char message[MAX_LINE_LENGTH];
        char response[MAX_LINE_LENGTH * 2];

        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "[%31[^]]] [%15[^]]] %31s -> %31[^:]: %1087[^\n]",
                   timestamp, type, sender, recipient, message) == 5 &&
            strcmp(type, "PRIVATE") == 0 &&
            ((strcmp(sender, client->username) == 0 &&
              strcmp(recipient, peer) == 0) ||
             (strcmp(sender, peer) == 0 &&
              strcmp(recipient, client->username) == 0))) {
            snprintf(response, sizeof(response), "HPMSG %s|%s|%s|%s",
                     timestamp, sender, recipient, message);
            if (slot_send_line(client, response) < 0) {
                fclose(file);
                return 1;
            }
        }
    }
    fclose(file);
    return slot_send_line(client, "PRIVATE_HISTORY_END") < 0;
}

static int send_group_history(ClientSlot *client, unsigned long group_id)
{
    FILE *file;
    char line[MAX_LINE_LENGTH * 2];

    if (slot_send_line(client, "GROUP_HISTORY_BEGIN") < 0) {
        return 1;
    }
    file = fopen(storage_chat_log_file(), "r");
    if (file == NULL) {
        return slot_send_line(client, "GROUP_HISTORY_END") < 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char timestamp[32];
        char type[16];
        char sender[MAX_USERNAME_LENGTH + 1];
        char group_text[32];
        char message[MAX_LINE_LENGTH];
        char response[MAX_LINE_LENGTH * 2];
        unsigned long logged_group_id = 0;

        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "[%31[^]]] [%15[^]]] %31s -> %31[^:]: %1087[^\n]",
                   timestamp, type, sender, group_text, message) == 5 &&
            strcmp(type, "GROUP") == 0 &&
            sscanf(group_text, "group-%lu", &logged_group_id) == 1 &&
            logged_group_id == group_id) {
            snprintf(response, sizeof(response), "HGMSG %s|%lu|%s|%s",
                     timestamp, group_id, sender, message);
            if (slot_send_line(client, response) < 0) {
                fclose(file);
                return 1;
            }
        }
    }
    fclose(file);
    return slot_send_line(client, "GROUP_HISTORY_END") < 0;
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
                           " %s|%s", clients[i].username,
                           clients[i].nickname);
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
                   "COMMANDS REGISTER <account9> <pass> <nickname> "
                   "LOGIN <account-or-nickname> <pass> LOGOUT WHO HISTORY "
                   "HISTORY_PRIVATE HISTORY_GROUP "
                   "SEARCH_USER FRIENDS REQUESTS SENT_REQUESTS "
                   "PRIVATE_START PRIVATE_REPLY "
                   "GROUP_CREATE GROUPS GROUP_SEND "
                   "UPLOAD <user> <local-path> DOWNLOAD <filename> "
                   "POST <title> <content> REPLY <post_id> <content> "
                   "LISTPOST VIEWPOST <post_id> "
                   "BBS_LIST BBS_VIEW <post_id> BBS_CREATE <title>|<content> "
                   "BBS_REPLY <post_id>|<content> BACKUP [label] "
                   "PING ECHO <text> HELP QUIT") < 0;
    }
    if (strcmp(line, "WHO") == 0) {
        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        send_client_list(client);
        return 0;
    }
    if (strcmp(line, "HISTORY") == 0) {
        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        return send_history(client);
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
        char account[USER_ACCOUNT_LENGTH + 1];
        char password[MAX_PASSWORD_LENGTH + 1];
        char nickname[MAX_NICKNAME_LENGTH + 1];
        UserResult result;

        if (!parse_registration(argument, account, password, nickname)) {
            return slot_send_line(client,
                                  "ERR usage: REGISTER <account9> <pass> <nickname>") < 0;
        }
        if (client_is_logged_in(client)) {
            return slot_send_line(client,
                                  "ERR logout before registering") < 0;
        }
        result = user_register(account, password, nickname);
        if (result != USER_OK) {
            snprintf(response, sizeof(response), "ERR %s",
                     user_result_message(result));
            return slot_send_line(client, response) < 0;
        }
        return slot_send_line(client, "OK registered") < 0;
    }
    if (strcmp(line, "LOGIN") == 0) {
        char login[MAX_NICKNAME_LENGTH + 1];
        char password[MAX_PASSWORD_LENGTH + 1];
        UserRecord record;
        UserResult result;

        if (!parse_credentials(argument, login, password)) {
            return slot_send_line(client,
                                  "ERR usage: LOGIN <account-or-nickname> <pass>") < 0;
        }
        if (client_is_logged_in(client)) {
            return slot_send_line(client, "ERR already logged in") < 0;
        }
        result = user_authenticate(login, password, &record);
        if (result != USER_OK) {
            snprintf(response, sizeof(response), "ERR %s",
                     user_result_message(result));
            return slot_send_line(client, response) < 0;
        }
        if (!login_client(client, record.account, record.nickname)) {
            return slot_send_line(client,
                                  "ERR user already logged in") < 0;
        }
        snprintf(response, sizeof(response), "OK logged in %s|%s",
                 record.account, record.nickname);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "SEARCH_USER") == 0) {
        UserRecord record;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client,
                                  "ERR usage: SEARCH_USER <account-or-nickname>") < 0;
        }
        if (user_find(argument, &record) <= 0) {
            return slot_send_line(client, "ERR user not found") < 0;
        }
        snprintf(response, sizeof(response), "USER %s|%s", record.account,
                 record.nickname);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "HISTORY_PRIVATE") == 0) {
        UserRecord peer;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0' ||
            user_find(argument, &peer) <= 0) {
            return slot_send_line(
                       client,
                       "ERR usage: HISTORY_PRIVATE <account-or-nickname>") < 0;
        }
        return send_private_history(client, peer.account);
    }
    if (strcmp(line, "HISTORY_GROUP") == 0) {
        unsigned long group_id;
        char *end = NULL;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client,
                                  "ERR usage: HISTORY_GROUP <group_id>") < 0;
        }
        group_id = strtoul(argument, &end, 10);
        if (end == NULL || *end != '\0' || group_id == 0 ||
            !social_is_group_member(group_id, client->username)) {
            return slot_send_line(client, "ERR group not found") < 0;
        }
        return send_group_history(client, group_id);
    }
    if (strcmp(line, "FRIENDS") == 0) {
        SocialListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        context.client = client;
        context.count = 0;
        if (slot_send_line(client, "FRIENDS_BEGIN") < 0 ||
            social_visit_friends(client->username, send_friend_item,
                                 &context) < 0 ||
            slot_send_line(client, "FRIENDS_END") < 0) {
            return 1;
        }
        return 0;
    }
    if (strcmp(line, "REQUESTS") == 0) {
        SocialListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        context.client = client;
        context.count = 0;
        if (slot_send_line(client, "REQUESTS_BEGIN") < 0 ||
            social_visit_requests_for(client->username, send_request_item,
                                      &context) < 0 ||
            slot_send_line(client, "REQUESTS_END") < 0) {
            return 1;
        }
        return 0;
    }
    if (strcmp(line, "SENT_REQUESTS") == 0) {
        SocialListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        context.client = client;
        context.count = 0;
        if (slot_send_line(client, "SENT_REQUESTS_BEGIN") < 0 ||
            social_visit_sent_requests_for(client->username,
                                           send_sent_request_item,
                                           &context) < 0 ||
            slot_send_line(client, "SENT_REQUESTS_END") < 0) {
            return 1;
        }
        return 0;
    }
    if (strcmp(line, "PRIVATE_START") == 0 ||
        strcmp(line, "PRIVATE_REPLY") == 0 ||
        strcmp(line, "PRIVATE") == 0) {
        char *message;
        UserRecord target;
        int delivery;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (message = strchr(argument, ' ')) == NULL) {
            return slot_send_line(
                       client,
                       "ERR usage: PRIVATE_START <account-or-nickname> <text>") < 0;
        }
        *message++ = '\0';
        while (*message == ' ') {
            message++;
        }
        if (*message == '\0' || user_find(argument, &target) <= 0) {
            return slot_send_line(client, "ERR user not found") < 0;
        }
        if (strcmp(client->username, target.account) == 0) {
            return slot_send_line(
                       client,
                       "ERR cannot send private message to yourself") < 0;
        }
        if (strcmp(line, "PRIVATE_REPLY") == 0) {
            if (social_has_private_request(target.account, client->username)) {
                (void)social_add_friend_pair(client->username, target.account);
            } else if (!social_are_friends(client->username, target.account)) {
                return slot_send_line(
                           client,
                           "ERR no incoming request from this user") < 0;
            }
        } else if (!social_are_friends(client->username, target.account)) {
            if (social_add_private_request(client->username, target.account,
                                           message) < 0) {
                return slot_send_line(client, "ERR request save failed") < 0;
            }
        }
        delivery = private_message(client, target.account, message);
        if (delivery < 0) {
            return slot_send_line(client,
                                  "ERR private delivery failed") < 0;
        }
        if (social_are_friends(client->username, target.account)) {
            return slot_send_line(client, "OK private message sent") < 0;
        }
        return slot_send_line(client, "OK private request sent") < 0;
    }
    if (strcmp(line, "GROUPS") == 0) {
        SocialListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        context.client = client;
        context.count = 0;
        if (slot_send_line(client, "GROUPS_BEGIN") < 0 ||
            social_visit_groups_for(client->username, send_group_item,
                                    &context) < 0 ||
            slot_send_line(client, "GROUPS_END") < 0) {
            return 1;
        }
        return 0;
    }
    if (strcmp(line, "GROUP_CREATE") == 0) {
        char *members_text;
        char *token;
        char member_storage[32][USER_ACCOUNT_LENGTH + 1];
        const char *member_accounts[32];
        char group_name[64];
        size_t count = 0;
        unsigned long group_id = 0;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (members_text = strchr(argument, ' ')) == NULL) {
            return slot_send_line(
                       client,
                       "ERR usage: GROUP_CREATE <name> <friend1,friend2>") < 0;
        }
        *members_text++ = '\0';
        copy_protocol_field(group_name, sizeof(group_name), argument);
        token = strtok(members_text, ",");
        while (token != NULL && count < 32) {
            UserRecord member;

            if (user_find(token, &member) > 0 &&
                social_are_friends(client->username, member.account)) {
                copy_account(member_storage[count], sizeof(member_storage[count]),
                             member.account);
                member_accounts[count] = member_storage[count];
                count++;
            }
            token = strtok(NULL, ",");
        }
        if (count == 0) {
            return slot_send_line(client,
                                  "ERR group needs at least one friend") < 0;
        }
        if (social_create_group(client->username, group_name, member_accounts,
                                count, &group_id) < 0) {
            return slot_send_line(client, "ERR group create failed") < 0;
        }
        snprintf(response, sizeof(response), "OK group %lu created", group_id);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "GROUP_SEND") == 0) {
        unsigned long group_id;
        char *message;
        char *end = NULL;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (message = strchr(argument, ' ')) == NULL) {
            return slot_send_line(client,
                                  "ERR usage: GROUP_SEND <group_id> <text>") < 0;
        }
        *message++ = '\0';
        group_id = strtoul(argument, &end, 10);
        if (end == NULL || *end != '\0' || group_id == 0 ||
            !social_is_group_member(group_id, client->username)) {
            return slot_send_line(client, "ERR group not found") < 0;
        }
        /* Delivery is handled below with a simple member visitor context. */
        snprintf(response, sizeof(response), "GMSG %lu %s %s", group_id,
                 client->username, message);
        GroupDeliveryContext delivery = {response};
        if (social_visit_group_members(group_id, deliver_group_member,
                                       &delivery) < 0 ||
            chat_log_group(group_id, client->username, message) < 0) {
            return slot_send_line(client, "ERR group send failed") < 0;
        }
        return 0;
    }
    if (strcmp(line, "GROUP") == 0 || strcmp(line, "BROADCAST") == 0) {
        return slot_send_line(
                   client,
                   "ERR global group removed; use GROUP_CREATE and GROUP_SEND") < 0;
    }
    if (strcmp(line, "UPLOAD") == 0) {
        char recipient[MAX_NICKNAME_LENGTH + 1];
        char filename[MAX_FILENAME_LENGTH + 1];
        char size_text[32];
        char sender[MAX_USERNAME_LENGTH + 1];
        char extra;
        unsigned long long size;
        UserRecord recipient_record;

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
        if (!file_name_valid(filename)) {
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
        if (user_find(recipient, &recipient_record) <= 0) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client, "ERR recipient does not exist") < 0;
        }
        if (file_store_upload(client->sockfd, recipient_record.account,
                              filename, size) < 0) {
            return slot_send_line(client, "ERR upload failed") < 0;
        }
        copy_client_username(client, sender, sizeof(sender));
        if (append_uploaded_file_record(sender, recipient_record.account,
                                        filename, size) < 0) {
            char path[4096];

            if (file_server_path(recipient_record.account, filename, path,
                                 sizeof(path)) == 0) {
                unlink(path);
            }
            return slot_send_line(client, "ERR upload record failed") < 0;
        }
        notify_file_ready(sender, recipient_record.account, filename, size);
        snprintf(response, sizeof(response), "OK uploaded %s for %s", filename,
                 recipient_record.nickname);
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
        copy_client_nickname(client, record.author, sizeof(record.author));
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
        copy_client_nickname(client, record.author, sizeof(record.author));
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
    if (strcmp(line, "BBS_LIST") == 0) {
        BbsListContext context;

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (slot_send_line(client, "BBS_POSTS_BEGIN") < 0) {
            return 1;
        }
        context.client = client;
        context.count = 0;
        if (bbs_visit_posts(send_bbs_post_item, &context) < 0) {
            return slot_send_line(client, "ERR list failed") < 0;
        }
        return slot_send_line(client, "BBS_POSTS_END") < 0;
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
    if (strcmp(line, "BBS_VIEW") == 0) {
        unsigned long post_id;
        char *end = NULL;
        BbsPostRecord post;
        BbsListContext context;
        char owner[32];
        char title[BBS_TITLE_LENGTH + 1];
        char content[BBS_CONTENT_LENGTH + 1];
        char attachment[128];
        char bbs_line[MAX_LINE_LENGTH * 3];

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client, "ERR usage: BBS_VIEW <post_id>") < 0;
        }
        errno = 0;
        post_id = strtoul(argument, &end, 10);
        if (errno != 0 || end == NULL || *end != '\0' || post_id == 0) {
            return slot_send_line(client, "ERR invalid post id") < 0;
        }
        if (bbs_read_post(post_id, &post) < 0) {
            return slot_send_line(client, "BBS_NOT_FOUND") < 0;
        }
        post_attachment_owner(post.id, owner, sizeof(owner));
        format_attachment_name(owner, attachment, sizeof(attachment));
        copy_protocol_field(title, sizeof(title), post.title);
        copy_protocol_field(content, sizeof(content), post.content);
        snprintf(bbs_line, sizeof(bbs_line), "BBS_POST %lu|%s|%s|%s|%s|%s",
                 post.id, post.author, title, content, attachment,
                 post.created_at);
        if (slot_send_line(client, "BBS_POST_BEGIN") < 0 ||
            slot_send_line(client, bbs_line) < 0 ||
            slot_send_line(client, "BBS_REPLIES_BEGIN") < 0) {
            return 1;
        }
        context.client = client;
        context.count = 0;
        if (bbs_visit_replies(post_id, send_bbs_reply_item, &context) < 0) {
            return slot_send_line(client, "ERR reply list failed") < 0;
        }
        return slot_send_line(client, "BBS_REPLIES_END") < 0 ||
               slot_send_line(client, "BBS_POST_END") < 0;
    }
    if (strcmp(line, "BBS_CREATE") == 0) {
        char *separator;
        BbsPostRecord record;
        char timestamp[20];

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (separator = strchr(argument, '|')) == NULL) {
            return slot_send_line(
                       client, "ERR usage: BBS_CREATE <title>|<content>") < 0;
        }
        *separator++ = '\0';
        if (*argument == '\0' || *separator == '\0') {
            return slot_send_line(
                       client, "ERR usage: BBS_CREATE <title>|<content>") < 0;
        }
        memset(&record, 0, sizeof(record));
        if (bbs_next_post_id(&record.id) < 0) {
            return slot_send_line(client, "ERR post id failed") < 0;
        }
        copy_client_nickname(client, record.author, sizeof(record.author));
        copy_protocol_field(record.title, sizeof(record.title), argument);
        copy_protocol_field(record.content, sizeof(record.content), separator);
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
    if (strcmp(line, "BBS_REPLY") == 0) {
        char *separator;
        unsigned long post_id;
        char *end = NULL;
        BbsReplyRecord record;
        char timestamp[20];

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || (separator = strchr(argument, '|')) == NULL) {
            return slot_send_line(
                       client, "ERR usage: BBS_REPLY <post_id>|<content>") < 0;
        }
        *separator++ = '\0';
        errno = 0;
        post_id = strtoul(argument, &end, 10);
        if (errno != 0 || end == NULL || *end != '\0' || post_id == 0) {
            return slot_send_line(client, "ERR invalid post id") < 0;
        }
        if (*separator == '\0') {
            return slot_send_line(
                       client, "ERR usage: BBS_REPLY <post_id>|<content>") < 0;
        }
        if (bbs_read_post(post_id, &(BbsPostRecord){0}) < 0) {
            return slot_send_line(client, "ERR post not found") < 0;
        }
        memset(&record, 0, sizeof(record));
        if (bbs_next_reply_id(&record.id) < 0) {
            return slot_send_line(client, "ERR reply id failed") < 0;
        }
        record.post_id = post_id;
        copy_client_nickname(client, record.author, sizeof(record.author));
        copy_protocol_field(record.content, sizeof(record.content), separator);
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(record.created_at, sizeof(record.created_at), "%s", timestamp);
        record.active = 1;
        if (bbs_append_reply(&record) < 0) {
            return slot_send_line(client, "ERR reply save failed") < 0;
        }
        snprintf(response, sizeof(response), "OK reply %lu created", record.id);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "BBS_UPLOAD_POST") == 0 ||
        strcmp(line, "BBS_UPLOAD_REPLY") == 0) {
        char id_text[32];
        char filename[MAX_FILENAME_LENGTH + 1];
        char size_text[32];
        char extra;
        char *end = NULL;
        unsigned long object_id;
        unsigned long long size;
        char username[MAX_NICKNAME_LENGTH + 1];
        char owner[32];
        char stored_path[256];

        if (argument == NULL ||
            sscanf(argument, "%31s %127s %31s %c", id_text, filename,
                   size_text, &extra) != 3) {
            return slot_send_line(
                       client,
                       strcmp(line, "BBS_UPLOAD_POST") == 0
                           ? "ERR usage: BBS_UPLOAD_POST <post_id> <filename> <size>"
                           : "ERR usage: BBS_UPLOAD_REPLY <reply_id> <filename> <size>") < 0;
        }
        if (file_parse_size(size_text, &size) < 0) {
            (void)slot_send_line(client, "ERR invalid file size");
            return 1;
        }
        if (!file_name_valid(filename)) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client, "ERR invalid filename") < 0;
        }
        if (!client_is_logged_in(client)) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client, "ERR login required") < 0;
        }
        errno = 0;
        object_id = strtoul(id_text, &end, 10);
        if (errno != 0 || end == NULL || *end != '\0' || object_id == 0) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client, "ERR invalid id") < 0;
        }
        copy_client_nickname(client, username, sizeof(username));
        if (strcmp(line, "BBS_UPLOAD_POST") == 0) {
            BbsPostRecord post;

            if (bbs_read_post(object_id, &post) < 0) {
                if (file_discard_contents(client->sockfd, size) < 0) {
                    return 1;
                }
                return slot_send_line(client, "ERR post not found") < 0;
            }
            if (strcmp(post.author, username) != 0) {
                if (file_discard_contents(client->sockfd, size) < 0) {
                    return 1;
                }
                return slot_send_line(
                           client,
                           "ERR only post author can upload attachment") < 0;
            }
            post_attachment_owner(object_id, owner, sizeof(owner));
        } else {
            BbsReplyRecord reply;

            if (bbs_read_reply(object_id, &reply) < 0) {
                if (file_discard_contents(client->sockfd, size) < 0) {
                    return 1;
                }
                return slot_send_line(client, "ERR reply not found") < 0;
            }
            if (strcmp(reply.author, username) != 0) {
                if (file_discard_contents(client->sockfd, size) < 0) {
                    return 1;
                }
                return slot_send_line(
                           client,
                           "ERR only reply author can upload attachment") < 0;
            }
            reply_attachment_owner(object_id, owner, sizeof(owner));
        }
        if (find_bbs_attachment(owner, NULL) == 0) {
            if (file_discard_contents(client->sockfd, size) < 0) {
                return 1;
            }
            return slot_send_line(client, "ERR attachment already exists") < 0;
        }
        if (store_bbs_attachment(client, owner, filename, size, stored_path,
                                 sizeof(stored_path)) < 0) {
            return slot_send_line(client, "ERR upload failed") < 0;
        }
        if (append_bbs_attachment_record(owner, username, filename, stored_path,
                                         size) < 0) {
            unlink(stored_path);
            return slot_send_line(client, "ERR upload record failed") < 0;
        }
        snprintf(response, sizeof(response), "OK uploaded %s", filename);
        return slot_send_line(client, response) < 0;
    }
    if (strcmp(line, "BBS_DOWNLOAD_POST") == 0 ||
        strcmp(line, "BBS_DOWNLOAD_REPLY") == 0) {
        unsigned long object_id;
        char *end = NULL;
        char owner[32];

        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        if (argument == NULL || *argument == '\0') {
            return slot_send_line(client, "ERR usage: BBS_DOWNLOAD <id>") < 0;
        }
        errno = 0;
        object_id = strtoul(argument, &end, 10);
        if (errno != 0 || end == NULL || *end != '\0' || object_id == 0) {
            return slot_send_line(client, "ERR invalid id") < 0;
        }
        if (strcmp(line, "BBS_DOWNLOAD_POST") == 0) {
            post_attachment_owner(object_id, owner, sizeof(owner));
        } else {
            reply_attachment_owner(object_id, owner, sizeof(owner));
        }
        return send_bbs_attachment(client, owner);
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
    if (social_init() < 0) {
        perror("initialize social storage");
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
