#include "chat.h"
#include "file_transfer.h"
#include "protocol.h"
#include "server.h"
#include "user.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
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


#define BBS_POSTS_FILE "data/posts.txt"
#define BBS_REPLIES_FILE "data/replies.txt"
#define BBS_UPLOAD_DIR "uploads/bbs"

static pthread_mutex_t bbs_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *server_chat_log_path(void)
{
    const char *configured = getenv("BBS_CHAT_LOG");
    return configured != NULL && *configured != '\0' ? configured : "logs/chat.log";
}


static int ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void bbs_clean_field(char *text)
{
    size_t i;

    if (text == NULL) {
        return;
    }
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == '|' || text[i] == '\n' || text[i] == '\r') {
            text[i] = ' ';
        }
    }
}

static void bbs_time_now(char *buffer, size_t capacity)
{
    time_t now = time(NULL);
    struct tm value;

    if (localtime_r(&now, &value) == NULL) {
        snprintf(buffer, capacity, "unknown-time");
        return;
    }
    strftime(buffer, capacity, "%Y-%m-%d %H:%M:%S", &value);
}

static int bbs_init(void)
{
    FILE *file;

    if (ensure_directory("data") < 0 || ensure_directory("uploads") < 0 ||
        ensure_directory(BBS_UPLOAD_DIR) < 0) {
        return -1;
    }
    file = fopen(BBS_POSTS_FILE, "a");
    if (file == NULL) {
        return -1;
    }
    fclose(file);
    file = fopen(BBS_REPLIES_FILE, "a");
    if (file == NULL) {
        return -1;
    }
    fclose(file);
    return 0;
}

static int bbs_next_id_locked(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[MAX_LINE_LENGTH + 256];
    int max_id = 0;

    if (file == NULL) {
        return 1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        int id = atoi(line);
        if (id > max_id) {
            max_id = id;
        }
    }
    fclose(file);
    return max_id + 1;
}

static int bbs_post_exists_locked(int post_id)
{
    FILE *file = fopen(BBS_POSTS_FILE, "r");
    char line[MAX_LINE_LENGTH + 256];
    int found = 0;

    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (atoi(line) == post_id) {
            found = 1;
            break;
        }
    }
    fclose(file);
    return found;
}

static int bbs_post_owner_locked(int post_id, const char *username)
{
    FILE *file = fopen(BBS_POSTS_FILE, "r");
    char line[MAX_LINE_LENGTH + 256];
    int found = 0;

    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char copy[MAX_LINE_LENGTH + 256];
        char *saveptr = NULL;
        char *id;
        char *author;

        snprintf(copy, sizeof(copy), "%s", line);
        id = strtok_r(copy, "|", &saveptr);
        author = strtok_r(NULL, "|", &saveptr);
        if (id != NULL && author != NULL && atoi(id) == post_id &&
            strcmp(author, username) == 0) {
            found = 1;
            break;
        }
    }
    fclose(file);
    return found;
}

static int bbs_find_post_attachment_locked(int post_id, char *attachment,
                                           size_t capacity)
{
    FILE *file = fopen(BBS_POSTS_FILE, "r");
    char line[MAX_LINE_LENGTH + 256];
    int result = -2; /* post not found */

    if (file == NULL) {
        return -2;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *saveptr = NULL;
        char *id = strtok_r(line, "|", &saveptr);
        char *author = strtok_r(NULL, "|", &saveptr);
        char *title = strtok_r(NULL, "|", &saveptr);
        char *content = strtok_r(NULL, "|", &saveptr);
        char *field = strtok_r(NULL, "|", &saveptr);

        (void)author;
        (void)title;
        (void)content;
        if (id != NULL && field != NULL && atoi(id) == post_id) {
            snprintf(attachment, capacity, "%s", field);
            result = strcmp(field, "none") == 0 ? -3 : 0;
            break;
        }
    }
    fclose(file);
    return result;
}

static int bbs_set_attachment_locked(int post_id, const char *attachment)
{
    FILE *input = fopen(BBS_POSTS_FILE, "r");
    FILE *output = fopen("data/posts.tmp", "w");
    char line[MAX_LINE_LENGTH + 256];
    int changed = 0;

    if (input == NULL || output == NULL) {
        if (input != NULL) {
            fclose(input);
        }
        if (output != NULL) {
            fclose(output);
        }
        return -1;
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        char copy[MAX_LINE_LENGTH + 256];
        char *saveptr = NULL;
        char *id;
        char *author;
        char *title;
        char *content;
        char *old_attachment;
        char *time_text;

        snprintf(copy, sizeof(copy), "%s", line);
        id = strtok_r(copy, "|", &saveptr);
        author = strtok_r(NULL, "|", &saveptr);
        title = strtok_r(NULL, "|", &saveptr);
        content = strtok_r(NULL, "|", &saveptr);
        old_attachment = strtok_r(NULL, "|", &saveptr);
        time_text = strtok_r(NULL, "\n", &saveptr);
        (void)old_attachment;

        if (id != NULL && author != NULL && title != NULL && content != NULL &&
            time_text != NULL && atoi(id) == post_id) {
            fprintf(output, "%s|%s|%s|%s|%s|%s\n", id, author, title,
                    content, attachment, time_text);
            changed = 1;
        } else {
            fputs(line, output);
        }
    }

    fclose(input);
    if (fclose(output) != 0) {
        return -1;
    }
    if (rename("data/posts.tmp", BBS_POSTS_FILE) != 0) {
        return -1;
    }
    return changed ? 0 : -1;
}


static int bbs_reply_owner_locked(int reply_id, const char *username)
{
    FILE *file = fopen(BBS_REPLIES_FILE, "r");
    char line[MAX_LINE_LENGTH + 256];
    int found = 0;

    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char copy[MAX_LINE_LENGTH + 256];
        char *saveptr = NULL;
        char *id;
        char *post_id;
        char *author;

        snprintf(copy, sizeof(copy), "%s", line);
        id = strtok_r(copy, "|", &saveptr);
        post_id = strtok_r(NULL, "|", &saveptr);
        author = strtok_r(NULL, "|", &saveptr);
        (void)post_id;
        if (id != NULL && author != NULL && atoi(id) == reply_id &&
            strcmp(author, username) == 0) {
            found = 1;
            break;
        }
    }
    fclose(file);
    return found;
}

static int bbs_find_reply_attachment_locked(int reply_id, char *attachment,
                                            size_t capacity)
{
    FILE *file = fopen(BBS_REPLIES_FILE, "r");
    char line[MAX_LINE_LENGTH + 256];
    int result = -2; /* reply not found */

    if (file == NULL) {
        return -2;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *saveptr = NULL;
        char *id = strtok_r(line, "|", &saveptr);
        char *post_id = strtok_r(NULL, "|", &saveptr);
        char *author = strtok_r(NULL, "|", &saveptr);
        char *content = strtok_r(NULL, "|", &saveptr);
        char *field = strtok_r(NULL, "|\n", &saveptr);
        char *time_text = strtok_r(NULL, "\n", &saveptr);

        (void)post_id;
        (void)author;
        (void)content;
        if (id != NULL && atoi(id) == reply_id) {
            if (time_text == NULL) {
                snprintf(attachment, capacity, "none");
                result = -3;
            } else {
                snprintf(attachment, capacity, "%s", field == NULL ? "none" : field);
                result = (field == NULL || strcmp(field, "none") == 0) ? -3 : 0;
            }
            break;
        }
    }
    fclose(file);
    return result;
}

static int bbs_set_reply_attachment_locked(int reply_id, const char *attachment)
{
    FILE *input = fopen(BBS_REPLIES_FILE, "r");
    FILE *output = fopen("data/replies.tmp", "w");
    char line[MAX_LINE_LENGTH + 256];
    int changed = 0;

    if (input == NULL || output == NULL) {
        if (input != NULL) {
            fclose(input);
        }
        if (output != NULL) {
            fclose(output);
        }
        return -1;
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        char copy[MAX_LINE_LENGTH + 256];
        char *saveptr = NULL;
        char *id;
        char *post_id;
        char *author;
        char *content;
        char *old_attachment;
        char *time_text;

        snprintf(copy, sizeof(copy), "%s", line);
        id = strtok_r(copy, "|", &saveptr);
        post_id = strtok_r(NULL, "|", &saveptr);
        author = strtok_r(NULL, "|", &saveptr);
        content = strtok_r(NULL, "|", &saveptr);
        old_attachment = strtok_r(NULL, "|\n", &saveptr);
        time_text = strtok_r(NULL, "\n", &saveptr);

        if (id != NULL && post_id != NULL && author != NULL && content != NULL &&
            old_attachment != NULL && atoi(id) == reply_id) {
            if (time_text == NULL) {
                time_text = old_attachment; /* compatibility with old reply rows */
            }
            fprintf(output, "%s|%s|%s|%s|%s|%s\n", id, post_id, author,
                    content, attachment, time_text);
            changed = 1;
        } else {
            fputs(line, output);
        }
    }

    fclose(input);
    if (fclose(output) != 0) {
        return -1;
    }
    if (rename("data/replies.tmp", BBS_REPLIES_FILE) != 0) {
        return -1;
    }
    return changed ? 0 : -1;
}

static int bbs_list_posts(ClientSlot *client)
{
    FILE *file;
    char line[MAX_LINE_LENGTH + 256];
    int result = 0;

    pthread_mutex_lock(&bbs_mutex);
    file = fopen(BBS_POSTS_FILE, "r");
    pthread_mutex_lock(&client->send_mutex);
    if (send_line(client->sockfd, "BBS_POSTS_BEGIN") < 0) {
        result = -1;
    }
    if (file != NULL && result == 0) {
        while (fgets(line, sizeof(line), file) != NULL) {
            size_t len = strlen(line);
            char response[MAX_LINE_LENGTH + 320];
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
            snprintf(response, sizeof(response), "BBS_POST %s", line);
            if (send_line(client->sockfd, response) < 0) {
                result = -1;
                break;
            }
        }
    }
    if (result == 0 && send_line(client->sockfd, "BBS_POSTS_END") < 0) {
        result = -1;
    }
    pthread_mutex_unlock(&client->send_mutex);
    if (file != NULL) {
        fclose(file);
    }
    pthread_mutex_unlock(&bbs_mutex);
    return result;
}

static int bbs_view_post(ClientSlot *client, int post_id)
{
    FILE *file;
    char line[MAX_LINE_LENGTH + 256];
    int found = 0;
    int result = 0;

    pthread_mutex_lock(&bbs_mutex);
    pthread_mutex_lock(&client->send_mutex);
    if (send_line(client->sockfd, "BBS_POST_BEGIN") < 0) {
        result = -1;
    }

    file = fopen(BBS_POSTS_FILE, "r");
    if (file != NULL && result == 0) {
        while (fgets(line, sizeof(line), file) != NULL) {
            if (atoi(line) == post_id) {
                char response[MAX_LINE_LENGTH + 320];
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') {
                    line[len - 1] = '\0';
                }
                snprintf(response, sizeof(response), "BBS_POST %s", line);
                if (send_line(client->sockfd, response) < 0) {
                    result = -1;
                }
                found = 1;
                break;
            }
        }
        fclose(file);
    }

    if (result == 0 && !found) {
        if (send_line(client->sockfd, "BBS_NOT_FOUND") < 0 ||
            send_line(client->sockfd, "BBS_POST_END") < 0) {
            result = -1;
        }
        pthread_mutex_unlock(&client->send_mutex);
        pthread_mutex_unlock(&bbs_mutex);
        return result;
    }

    if (result == 0 && send_line(client->sockfd, "BBS_REPLIES_BEGIN") < 0) {
        result = -1;
    }
    file = fopen(BBS_REPLIES_FILE, "r");
    if (file != NULL && result == 0) {
        while (fgets(line, sizeof(line), file) != NULL) {
            char copy[MAX_LINE_LENGTH + 256];
            char response[MAX_LINE_LENGTH + 320];
            char *saveptr = NULL;
            char *reply_id;
            char *reply_post_id;
            size_t len;

            snprintf(copy, sizeof(copy), "%s", line);
            reply_id = strtok_r(copy, "|", &saveptr);
            reply_post_id = strtok_r(NULL, "|", &saveptr);
            (void)reply_id;
            if (reply_post_id == NULL || atoi(reply_post_id) != post_id) {
                continue;
            }
            len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
            snprintf(response, sizeof(response), "BBS_REPLY %s", line);
            if (send_line(client->sockfd, response) < 0) {
                result = -1;
                break;
            }
        }
        fclose(file);
    }
    if (result == 0 && send_line(client->sockfd, "BBS_REPLIES_END") < 0) {
        result = -1;
    }
    if (result == 0 && send_line(client->sockfd, "BBS_POST_END") < 0) {
        result = -1;
    }
    pthread_mutex_unlock(&client->send_mutex);
    pthread_mutex_unlock(&bbs_mutex);
    return result;
}

static int bbs_create_post(ClientSlot *client, const char *title,
                           const char *content)
{
    char username[MAX_USERNAME_LENGTH + 1];
    char clean_title[MAX_LINE_LENGTH];
    char clean_content[MAX_LINE_LENGTH];
    char time_text[64];
    char response[MAX_LINE_LENGTH + 64];
    int id;
    FILE *file;

    copy_client_username(client, username, sizeof(username));
    snprintf(clean_title, sizeof(clean_title), "%s", title);
    snprintf(clean_content, sizeof(clean_content), "%s", content);
    bbs_clean_field(clean_title);
    bbs_clean_field(clean_content);

    pthread_mutex_lock(&bbs_mutex);
    id = bbs_next_id_locked(BBS_POSTS_FILE);
    file = fopen(BBS_POSTS_FILE, "a");
    if (file == NULL) {
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR cannot write BBS post") < 0 ? -1 : 0;
    }
    bbs_time_now(time_text, sizeof(time_text));
    fprintf(file, "%d|%s|%s|%s|none|%s\n", id, username, clean_title,
            clean_content, time_text);
    fclose(file);
    pthread_mutex_unlock(&bbs_mutex);

    snprintf(response, sizeof(response), "OK BBS post created %d", id);
    return slot_send_line(client, response) < 0 ? -1 : 0;
}

static int bbs_reply_post(ClientSlot *client, int post_id, const char *content)
{
    char username[MAX_USERNAME_LENGTH + 1];
    char clean_content[MAX_LINE_LENGTH];
    char time_text[64];
    char response[MAX_LINE_LENGTH + 64];
    int id;
    FILE *file;

    copy_client_username(client, username, sizeof(username));
    snprintf(clean_content, sizeof(clean_content), "%s", content);
    bbs_clean_field(clean_content);

    pthread_mutex_lock(&bbs_mutex);
    if (!bbs_post_exists_locked(post_id)) {
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR BBS post not found") < 0 ? -1 : 0;
    }
    id = bbs_next_id_locked(BBS_REPLIES_FILE);
    file = fopen(BBS_REPLIES_FILE, "a");
    if (file == NULL) {
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR cannot write BBS reply") < 0 ? -1 : 0;
    }
    bbs_time_now(time_text, sizeof(time_text));
    fprintf(file, "%d|%d|%s|%s|none|%s\n", id, post_id, username, clean_content,
            time_text);
    fclose(file);
    pthread_mutex_unlock(&bbs_mutex);

    snprintf(response, sizeof(response), "OK BBS reply created %d", id);
    return slot_send_line(client, response) < 0 ? -1 : 0;
}

static int bbs_upload_post_attachment(ClientSlot *client, const char *argument)
{
    char username[MAX_USERNAME_LENGTH + 1];
    char filename[MAX_FILENAME_LENGTH + 1];
    char size_text[32];
    char saved_name[MAX_FILENAME_LENGTH + 64];
    char path[512];
    char response[MAX_LINE_LENGTH];
    char extra;
    int post_id;
    unsigned long long size;
    FILE *file;
    int result;

    if (argument == NULL ||
        sscanf(argument, "%d %127s %31s %c", &post_id, filename, size_text,
               &extra) != 3 || file_parse_size(size_text, &size) < 0 ||
        !file_name_valid(filename)) {
        return slot_send_line(
                   client,
                   "ERR usage: BBS_UPLOAD <post-id> <filename> <size>") < 0;
    }
    copy_client_username(client, username, sizeof(username));

    pthread_mutex_lock(&bbs_mutex);
    if (!bbs_post_exists_locked(post_id)) {
        pthread_mutex_unlock(&bbs_mutex);
        if (file_discard_contents(client->sockfd, size) < 0) {
            return 1;
        }
        return slot_send_line(client, "ERR BBS post not found") < 0;
    }
    if (!bbs_post_owner_locked(post_id, username)) {
        pthread_mutex_unlock(&bbs_mutex);
        if (file_discard_contents(client->sockfd, size) < 0) {
            return 1;
        }
        return slot_send_line(
                   client,
                   "ERR you can only upload attachments to your own BBS post") < 0;
    }
    snprintf(saved_name, sizeof(saved_name), "post_%d_%s", post_id, filename);
    snprintf(path, sizeof(path), "%s/%s", BBS_UPLOAD_DIR, saved_name);
    file = fopen(path, "wb");
    if (file == NULL) {
        pthread_mutex_unlock(&bbs_mutex);
        if (file_discard_contents(client->sockfd, size) < 0) {
            return 1;
        }
        return slot_send_line(client, "ERR cannot save BBS attachment") < 0;
    }
    result = file_receive_contents(client->sockfd, file, size);
    if (fclose(file) != 0) {
        result = -1;
    }
    if (result < 0) {
        unlink(path);
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR BBS upload failed") < 0;
    }
    if (bbs_set_attachment_locked(post_id, saved_name) < 0) {
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR cannot update BBS post attachment") < 0;
    }
    pthread_mutex_unlock(&bbs_mutex);

    snprintf(response, sizeof(response), "OK BBS file uploaded %s", saved_name);
    return slot_send_line(client, response) < 0;
}

static int bbs_download_post_attachment(ClientSlot *client, int post_id)
{
    char attachment[MAX_FILENAME_LENGTH + 64];
    char path[512];
    char header[MAX_LINE_LENGTH];
    unsigned long long size;
    FILE *file;
    int find_result;
    int result = -1;

    pthread_mutex_lock(&bbs_mutex);
    find_result = bbs_find_post_attachment_locked(post_id, attachment,
                                                  sizeof(attachment));
    pthread_mutex_unlock(&bbs_mutex);
    if (find_result == -2) {
        return slot_send_line(client, "ERR BBS post not found") < 0;
    }
    if (find_result == -3) {
        return slot_send_line(client, "ERR this BBS post has no attachment") < 0;
    }
    if (!file_name_valid(attachment)) {
        return slot_send_line(client, "ERR invalid BBS attachment name") < 0;
    }
    snprintf(path, sizeof(path), "%s/%s", BBS_UPLOAD_DIR, attachment);
    file = fopen(path, "rb");
    if (file == NULL) {
        return slot_send_line(client, "ERR BBS attachment missing") < 0;
    }
    if (fseeko(file, 0, SEEK_END) != 0) {
        fclose(file);
        return slot_send_line(client, "ERR cannot read BBS attachment") < 0;
    }
    size = (unsigned long long)ftello(file);
    rewind(file);

    snprintf(header, sizeof(header), "BBS_FILE %s %llu", attachment, size);
    pthread_mutex_lock(&client->send_mutex);
    if (send_line(client->sockfd, header) == 0 &&
        file_send_contents(client->sockfd, file, size) == 0) {
        result = 0;
    }
    pthread_mutex_unlock(&client->send_mutex);
    fclose(file);
    return result;
}


static int bbs_upload_reply_attachment(ClientSlot *client, const char *argument)
{
    char username[MAX_USERNAME_LENGTH + 1];
    char filename[MAX_FILENAME_LENGTH + 1];
    char size_text[32];
    char saved_name[MAX_FILENAME_LENGTH + 64];
    char path[512];
    char response[MAX_LINE_LENGTH];
    char extra;
    int reply_id;
    unsigned long long size;
    FILE *file;
    int result;

    if (argument == NULL ||
        sscanf(argument, "%d %127s %31s %c", &reply_id, filename, size_text,
               &extra) != 3 || file_parse_size(size_text, &size) < 0 ||
        !file_name_valid(filename)) {
        return slot_send_line(
                   client,
                   "ERR usage: BBS_UPLOAD_REPLY <reply-id> <filename> <size>") < 0;
    }
    copy_client_username(client, username, sizeof(username));

    pthread_mutex_lock(&bbs_mutex);
    if (!bbs_reply_owner_locked(reply_id, username)) {
        pthread_mutex_unlock(&bbs_mutex);
        if (file_discard_contents(client->sockfd, size) < 0) {
            return 1;
        }
        return slot_send_line(
                   client,
                   "ERR you can only upload attachments to your own BBS reply") < 0;
    }
    snprintf(saved_name, sizeof(saved_name), "reply_%d_%s", reply_id, filename);
    snprintf(path, sizeof(path), "%s/%s", BBS_UPLOAD_DIR, saved_name);
    file = fopen(path, "wb");
    if (file == NULL) {
        pthread_mutex_unlock(&bbs_mutex);
        if (file_discard_contents(client->sockfd, size) < 0) {
            return 1;
        }
        return slot_send_line(client, "ERR cannot save BBS reply attachment") < 0;
    }
    result = file_receive_contents(client->sockfd, file, size);
    if (fclose(file) != 0) {
        result = -1;
    }
    if (result < 0) {
        unlink(path);
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR BBS reply upload failed") < 0;
    }
    if (bbs_set_reply_attachment_locked(reply_id, saved_name) < 0) {
        pthread_mutex_unlock(&bbs_mutex);
        return slot_send_line(client, "ERR cannot update BBS reply attachment") < 0;
    }
    pthread_mutex_unlock(&bbs_mutex);

    snprintf(response, sizeof(response), "OK BBS reply file uploaded %s", saved_name);
    return slot_send_line(client, response) < 0;
}

static int bbs_download_reply_attachment(ClientSlot *client, int reply_id)
{
    char attachment[MAX_FILENAME_LENGTH + 64];
    char path[512];
    char header[MAX_LINE_LENGTH];
    unsigned long long size;
    FILE *file;
    int find_result;
    int result = -1;

    pthread_mutex_lock(&bbs_mutex);
    find_result = bbs_find_reply_attachment_locked(reply_id, attachment,
                                                   sizeof(attachment));
    pthread_mutex_unlock(&bbs_mutex);
    if (find_result == -2) {
        return slot_send_line(client, "ERR BBS reply not found") < 0;
    }
    if (find_result == -3) {
        return slot_send_line(client, "ERR this BBS reply has no attachment") < 0;
    }
    if (!file_name_valid(attachment)) {
        return slot_send_line(client, "ERR invalid BBS reply attachment name") < 0;
    }
    snprintf(path, sizeof(path), "%s/%s", BBS_UPLOAD_DIR, attachment);
    file = fopen(path, "rb");
    if (file == NULL) {
        return slot_send_line(client, "ERR BBS reply attachment missing") < 0;
    }
    if (fseeko(file, 0, SEEK_END) != 0) {
        fclose(file);
        return slot_send_line(client, "ERR cannot read BBS reply attachment") < 0;
    }
    size = (unsigned long long)ftello(file);
    rewind(file);

    snprintf(header, sizeof(header), "BBS_FILE %s %llu", attachment, size);
    pthread_mutex_lock(&client->send_mutex);
    if (send_line(client->sockfd, header) == 0 &&
        file_send_contents(client->sockfd, file, size) == 0) {
        result = 0;
    }
    pthread_mutex_unlock(&client->send_mutex);
    fclose(file);
    return result;
}

static int handle_bbs_command(ClientSlot *client, const char *command,
                              char *argument)
{
    if (!client_is_logged_in(client)) {
        return slot_send_line(client, "ERR login required") < 0;
    }

    if (strcmp(command, "BBS_LIST") == 0) {
        return bbs_list_posts(client) < 0;
    }
    if (strcmp(command, "BBS_VIEW") == 0) {
        char extra;
        int post_id;
        if (argument == NULL || sscanf(argument, "%d %c", &post_id, &extra) != 1) {
            return slot_send_line(client, "ERR usage: BBS_VIEW <post-id>") < 0;
        }
        return bbs_view_post(client, post_id) < 0;
    }
    if (strcmp(command, "BBS_CREATE") == 0) {
        char *separator;
        if (argument == NULL || (separator = strchr(argument, '|')) == NULL) {
            return slot_send_line(
                       client,
                       "ERR usage: BBS_CREATE <title>|<content>") < 0;
        }
        *separator++ = '\0';
        if (*argument == '\0' || *separator == '\0') {
            return slot_send_line(
                       client,
                       "ERR usage: BBS_CREATE <title>|<content>") < 0;
        }
        return bbs_create_post(client, argument, separator) < 0;
    }
    if (strcmp(command, "BBS_REPLY") == 0) {
        char *separator;
        int post_id;
        if (argument == NULL || (separator = strchr(argument, '|')) == NULL) {
            return slot_send_line(
                       client,
                       "ERR usage: BBS_REPLY <post-id>|<content>") < 0;
        }
        *separator++ = '\0';
        post_id = atoi(argument);
        if (post_id <= 0 || *separator == '\0') {
            return slot_send_line(
                       client,
                       "ERR usage: BBS_REPLY <post-id>|<content>") < 0;
        }
        return bbs_reply_post(client, post_id, separator) < 0;
    }
    if (strcmp(command, "BBS_UPLOAD") == 0 ||
        strcmp(command, "BBS_UPLOAD_POST") == 0) {
        return bbs_upload_post_attachment(client, argument);
    }
    if (strcmp(command, "BBS_UPLOAD_REPLY") == 0) {
        return bbs_upload_reply_attachment(client, argument);
    }
    if (strcmp(command, "BBS_DOWNLOAD") == 0 ||
        strcmp(command, "BBS_DOWNLOAD_POST") == 0) {
        char extra;
        int post_id;
        if (argument == NULL || sscanf(argument, "%d %c", &post_id, &extra) != 1) {
            return slot_send_line(client,
                                  "ERR usage: BBS_DOWNLOAD_POST <post-id>") < 0;
        }
        return bbs_download_post_attachment(client, post_id) < 0;
    }
    if (strcmp(command, "BBS_DOWNLOAD_REPLY") == 0) {
        char extra;
        int reply_id;
        if (argument == NULL || sscanf(argument, "%d %c", &reply_id, &extra) != 1) {
            return slot_send_line(client,
                                  "ERR usage: BBS_DOWNLOAD_REPLY <reply-id>") < 0;
        }
        return bbs_download_reply_attachment(client, reply_id) < 0;
    }
    return slot_send_line(client, "ERR unknown BBS command") < 0;
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


static void history_clean_field(char *text)
{
    size_t i;

    if (text == NULL) {
        return;
    }
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == '|' || text[i] == '\n' || text[i] == '\r') {
            text[i] = ' ';
        }
    }
}

static void send_chat_history(ClientSlot *client, const char *username)
{
    FILE *file = fopen(server_chat_log_path(), "r");
    char line[MAX_LINE_LENGTH + 256];

    (void)slot_send_line(client, "HISTORY_BEGIN");
    if (file == NULL) {
        (void)slot_send_line(client, "HISTORY_END");
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char timestamp[32];
        char *type_start;

        if (line[0] != '[' || strlen(line) < 24) {
            continue;
        }
        snprintf(timestamp, sizeof(timestamp), "%.19s", line + 1);
        type_start = strstr(line, "] [");
        if (type_start == NULL) {
            continue;
        }

        if (strstr(type_start, "[GROUP]") != NULL) {
            char *payload = strstr(line, "[GROUP] ");
            char *colon;
            char sender[MAX_USERNAME_LENGTH + 1];
            char message[MAX_LINE_LENGTH];
            char response[MAX_LINE_LENGTH + 96];
            size_t sender_len;

            if (payload == NULL) {
                continue;
            }
            payload += strlen("[GROUP] ");
            colon = strstr(payload, ": ");
            if (colon == NULL) {
                continue;
            }
            sender_len = (size_t)(colon - payload);
            if (sender_len == 0 || sender_len > MAX_USERNAME_LENGTH) {
                continue;
            }
            snprintf(sender, sizeof(sender), "%.*s", (int)sender_len, payload);
            snprintf(message, sizeof(message), "%s", colon + 2);
            history_clean_field(sender);
            history_clean_field(message);
            snprintf(response, sizeof(response), "HMSG %s|%s|%s", timestamp,
                     sender, message);
            (void)slot_send_line(client, response);
        } else if (strstr(type_start, "[PRIVATE]") != NULL) {
            char *payload = strstr(line, "[PRIVATE] ");
            char *arrow;
            char *colon;
            char sender[MAX_USERNAME_LENGTH + 1];
            char recipient[MAX_USERNAME_LENGTH + 1];
            char message[MAX_LINE_LENGTH];
            char response[MAX_LINE_LENGTH + 128];
            size_t sender_len;
            size_t recipient_len;

            if (payload == NULL) {
                continue;
            }
            payload += strlen("[PRIVATE] ");
            arrow = strstr(payload, " -> ");
            if (arrow == NULL) {
                continue;
            }
            colon = strstr(arrow + 4, ": ");
            if (colon == NULL) {
                continue;
            }
            sender_len = (size_t)(arrow - payload);
            recipient_len = (size_t)(colon - (arrow + 4));
            if (sender_len == 0 || recipient_len == 0 ||
                sender_len > MAX_USERNAME_LENGTH ||
                recipient_len > MAX_USERNAME_LENGTH) {
                continue;
            }
            snprintf(sender, sizeof(sender), "%.*s", (int)sender_len, payload);
            snprintf(recipient, sizeof(recipient), "%.*s", (int)recipient_len,
                     arrow + 4);
            if (strcmp(sender, username) != 0 && strcmp(recipient, username) != 0) {
                continue;
            }
            snprintf(message, sizeof(message), "%s", colon + 2);
            history_clean_field(sender);
            history_clean_field(recipient);
            history_clean_field(message);
            snprintf(response, sizeof(response), "HPMSG %s|%s|%s|%s", timestamp,
                     sender, recipient, message);
            (void)slot_send_line(client, response);
        }
    }
    fclose(file);
    (void)slot_send_line(client, "HISTORY_END");
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

    if (chat_log_private(sender->username, recipient, message) < 0) {
        return -2;
    }

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

    return delivery == 0 ? 2 : delivery; /* 2 means stored for an offline user. */
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
                   "LOGOUT WHO HISTORY GROUP <text> PRIVATE <user> <text> "
                   "UPLOAD <user> <local-path> DOWNLOAD <filename> "
                   "BBS_LIST BBS_VIEW <id> BBS_CREATE <title>|<content> "
                   "BBS_REPLY <id>|<text> BBS_UPLOAD_POST <id> <filename> <size> "
                   "BBS_UPLOAD_REPLY <id> <filename> <size> "
                   "BBS_DOWNLOAD_POST <id> BBS_DOWNLOAD_REPLY <id> "
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
        char username[MAX_USERNAME_LENGTH + 1];
        if (!client_is_logged_in(client)) {
            return slot_send_line(client, "ERR login required") < 0;
        }
        copy_client_username(client, username, sizeof(username));
        send_chat_history(client, username);
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
        if (strcmp(client->username, argument) == 0) {
            return slot_send_line(client,
                                  "ERR cannot send private message to yourself") < 0;
        }
        {
            int exists = user_exists(argument);
            if (exists <= 0) {
                return slot_send_line(
                           client, exists == 0 ? "ERR user does not exist"
                                               : "ERR user storage error") < 0;
            }
        }
        delivery = private_message(client, argument, message);
        if (delivery == 2) {
            return slot_send_line(client,
                                  "OK private message stored for offline user") < 0;
        }
        if (delivery == -1) {
            return slot_send_line(client, "ERR private delivery failed") < 0;
        }
        if (delivery == -2) {
            return slot_send_line(
                       client, "ERR message saved but log failed") < 0;
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
    if (strncmp(line, "BBS_", 4) == 0) {
        return handle_bbs_command(client, line, argument);
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
    if (ensure_directory("data") < 0 || ensure_directory("logs") < 0 ||
        ensure_directory("uploads") < 0) {
        perror("initialize data directories");
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
    if (bbs_init() < 0) {
        perror("initialize BBS storage");
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
