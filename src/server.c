// server.c - Linux BBS/Chat C socket demo server
// Main purpose: provide a simple TCP server for BBS functions.
// The chat/user/storage parts are simplified so that the BBS module can be tested first.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PORT 8888
#define MAXLINE 2048

#define USERS   "data/users.txt"
#define POSTS   "data/posts.txt"
#define REPLIES "data/replies.txt"
#define MSGS    "data/messages.txt"

pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

void mkdirs() {
    mkdir("data", 0755);
    mkdir("logs", 0755);
    mkdir("uploads", 0755);
    mkdir("uploads/bbs", 0755);
}

void clean(char *s) {
    for (int i = 0; s && s[i]; i++) {
        if (s[i] == '|' || s[i] == '\n' || s[i] == '\r') {
            s[i] = ' ';
        }
    }
}

char *now_time(char *buf, int n) {
    time_t t = time(NULL);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", localtime(&t));
    return buf;
}

const char *basename2(const char *path) {
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *c = a > b ? a : b;
    return c ? c + 1 : path;
}

ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    const char *p = (const char *)buf;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return (ssize_t)sent;
}

ssize_t recv_all(int fd, void *buf, size_t len) {
    size_t recved = 0;
    char *p = (char *)buf;
    while (recved < len) {
        ssize_t n = recv(fd, p + recved, len - recved, 0);
        if (n <= 0) return -1;
        recved += n;
    }
    return (ssize_t)recved;
}

void drain_bytes(int fd, long size) {
    char tmp[4096];
    while (size > 0) {
        size_t chunk = size > (long)sizeof(tmp) ? sizeof(tmp) : (size_t)size;
        if (recv_all(fd, tmp, chunk) < 0) break;
        size -= (long)chunk;
    }
}

ssize_t read_line(int fd, char *buf, size_t max) {
    size_t i = 0;
    char c;
    while (i + 1 < max) {
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) return n;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

void send_line(int fd, const char *s) {
    send_all(fd, s, strlen(s));
}

int next_id(const char *file) {
    FILE *fp = fopen(file, "r");
    if (!fp) return 1;

    char line[MAXLINE];
    int max_id = 0;
    while (fgets(line, sizeof(line), fp)) {
        int id = atoi(line);
        if (id > max_id) max_id = id;
    }
    fclose(fp);
    return max_id + 1;
}

int post_exists(int post_id) {
    FILE *fp = fopen(POSTS, "r");
    if (!fp) return 0;

    char line[MAXLINE];
    int ok = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (atoi(line) == post_id) {
            ok = 1;
            break;
        }
    }
    fclose(fp);
    return ok;
}

int is_post_owner(int post_id, const char *username) {
    FILE *fp = fopen(POSTS, "r");
    if (!fp) return 0;

    char line[MAXLINE];
    int ok = 0;
    while (fgets(line, sizeof(line), fp)) {
        char copy[MAXLINE];
        snprintf(copy, sizeof(copy), "%s", line);

        char *saveptr = NULL;
        char *id = strtok_r(copy, "|", &saveptr);
        char *author = strtok_r(NULL, "|", &saveptr);

        if (id && author && atoi(id) == post_id && strcmp(author, username) == 0) {
            ok = 1;
            break;
        }
    }
    fclose(fp);
    return ok;
}

int find_attachment(int post_id, char *out, int n) {
    FILE *fp = fopen(POSTS, "r");
    if (!fp) return -1;

    char line[MAXLINE];
    int ret = -2;  // post not found
    while (fgets(line, sizeof(line), fp)) {
        char *saveptr = NULL;
        char *id = strtok_r(line, "|", &saveptr);
        strtok_r(NULL, "|", &saveptr);  // author
        strtok_r(NULL, "|", &saveptr);  // title
        strtok_r(NULL, "|", &saveptr);  // content
        char *att = strtok_r(NULL, "|", &saveptr);

        if (id && att && atoi(id) == post_id) {
            snprintf(out, n, "%s", att);
            ret = strcmp(att, "none") == 0 ? -3 : 0;  // -3 means no attachment
            break;
        }
    }
    fclose(fp);
    return ret;
}

int register_user(const char *username, const char *password) {
    pthread_mutex_lock(&file_lock);

    FILE *fp = fopen(USERS, "a+");
    if (!fp) {
        pthread_mutex_unlock(&file_lock);
        return -1;
    }

    rewind(fp);
    char line[MAXLINE];
    while (fgets(line, sizeof(line), fp)) {
        char *name = strtok(line, "|");
        if (name && strcmp(name, username) == 0) {
            fclose(fp);
            pthread_mutex_unlock(&file_lock);
            return -2;
        }
    }

    fprintf(fp, "%s|%s\n", username, password);
    fclose(fp);
    pthread_mutex_unlock(&file_lock);
    return 0;
}

int login_user(const char *username, const char *password) {
    pthread_mutex_lock(&file_lock);

    FILE *fp = fopen(USERS, "r");
    if (!fp) {
        pthread_mutex_unlock(&file_lock);
        return -1;
    }

    char line[MAXLINE];
    int ok = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *saveptr = NULL;
        char *name = strtok_r(line, "|", &saveptr);
        char *pass = strtok_r(NULL, "\n", &saveptr);
        if (name && pass && strcmp(name, username) == 0 && strcmp(pass, password) == 0) {
            ok = 1;
            break;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&file_lock);
    return ok ? 0 : -2;
}

void create_post(int fd, char *username, char *title, char *content) {
    clean(username);
    clean(title);
    clean(content);

    pthread_mutex_lock(&file_lock);
    int id = next_id(POSTS);
    FILE *fp = fopen(POSTS, "a");
    char timebuf[64];

    if (fp) {
        now_time(timebuf, sizeof(timebuf));
        fprintf(fp, "%d|%s|%s|%s|none|%s\n", id, username, title, content, timebuf);
        fclose(fp);

        char resp[128];
        snprintf(resp, sizeof(resp), "OK|post created|%d\n", id);
        send_line(fd, resp);
    } else {
        send_line(fd, "ERR|cannot write post\n");
    }
    pthread_mutex_unlock(&file_lock);
}

void list_posts(int fd) {
    pthread_mutex_lock(&file_lock);

    send_line(fd, "POSTS_BEGIN\n");
    FILE *fp = fopen(POSTS, "r");
    char line[MAXLINE];
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            send_line(fd, line);
        }
        fclose(fp);
    }
    send_line(fd, "POSTS_END\n");

    pthread_mutex_unlock(&file_lock);
}

void view_post(int fd, int post_id) {
    pthread_mutex_lock(&file_lock);

    send_line(fd, "POST_BEGIN\n");

    FILE *fp = fopen(POSTS, "r");
    char line[MAXLINE];
    int found = 0;
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (atoi(line) == post_id) {
                send_line(fd, line);
                found = 1;
                break;
            }
        }
        fclose(fp);
    }

    if (!found) {
        send_line(fd, "NOT_FOUND\n");
        send_line(fd, "POST_END\n");
        pthread_mutex_unlock(&file_lock);
        return;
    }

    send_line(fd, "REPLIES_BEGIN\n");
    fp = fopen(REPLIES, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char copy[MAXLINE];
            snprintf(copy, sizeof(copy), "%s", line);
            char *saveptr = NULL;
            strtok_r(copy, "|", &saveptr);               // reply id
            char *pidstr = strtok_r(NULL, "|", &saveptr); // post id
            if (pidstr && atoi(pidstr) == post_id) {
                send_line(fd, line);
            }
        }
        fclose(fp);
    }
    send_line(fd, "REPLIES_END\n");
    send_line(fd, "POST_END\n");

    pthread_mutex_unlock(&file_lock);
}

void reply_post(int fd, char *username, int post_id, char *content) {
    clean(username);
    clean(content);

    if (!post_exists(post_id)) {
        send_line(fd, "ERR|post not found\n");
        return;
    }

    pthread_mutex_lock(&file_lock);
    int id = next_id(REPLIES);
    FILE *fp = fopen(REPLIES, "a");
    char timebuf[64];

    if (fp) {
        now_time(timebuf, sizeof(timebuf));
        fprintf(fp, "%d|%d|%s|%s|%s\n", id, post_id, username, content, timebuf);
        fclose(fp);

        char resp[128];
        snprintf(resp, sizeof(resp), "OK|reply created|%d\n", id);
        send_line(fd, resp);
    } else {
        send_line(fd, "ERR|cannot write reply\n");
    }
    pthread_mutex_unlock(&file_lock);
}

void set_attachment(int post_id, const char *attachment) {
    FILE *in = fopen(POSTS, "r");
    FILE *out = fopen("data/posts.tmp", "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return;
    }

    char line[MAXLINE];
    while (fgets(line, sizeof(line), in)) {
        char copy[MAXLINE];
        snprintf(copy, sizeof(copy), "%s", line);

        char *saveptr = NULL;
        char *id = strtok_r(copy, "|", &saveptr);
        char *author = strtok_r(NULL, "|", &saveptr);
        char *title = strtok_r(NULL, "|", &saveptr);
        char *content = strtok_r(NULL, "|", &saveptr);
        strtok_r(NULL, "|", &saveptr);  // old attachment
        char *timebuf = strtok_r(NULL, "\n", &saveptr);

        if (id && author && title && content && timebuf && atoi(id) == post_id) {
            fprintf(out, "%s|%s|%s|%s|%s|%s\n", id, author, title, content, attachment, timebuf);
        } else {
            fputs(line, out);
        }
    }

    fclose(in);
    fclose(out);
    rename("data/posts.tmp", POSTS);
}

void upload_bbs(int fd, char *username, int post_id, char *filename, long size) {
    clean(username);

    if (!post_exists(post_id)) {
        drain_bytes(fd, size);
        send_line(fd, "ERR|post not found\n");
        return;
    }

    if (!is_post_owner(post_id, username)) {
        drain_bytes(fd, size);
        send_line(fd, "ERR|you can only upload attachments to your own post\n");
        return;
    }

    char saved[512];
    snprintf(saved, sizeof(saved), "post_%d_%s", post_id, basename2(filename));
    clean(saved);

    char path[1024];
    snprintf(path, sizeof(path), "uploads/bbs/%s", saved);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        drain_bytes(fd, size);
        send_line(fd, "ERR|cannot save file\n");
        return;
    }

    char buf[4096];
    long left = size;
    while (left > 0) {
        size_t chunk = left > (long)sizeof(buf) ? sizeof(buf) : (size_t)left;
        if (recv_all(fd, buf, chunk) < 0) {
            fclose(fp);
            send_line(fd, "ERR|upload broken\n");
            return;
        }
        fwrite(buf, 1, chunk, fp);
        left -= (long)chunk;
    }
    fclose(fp);

    pthread_mutex_lock(&file_lock);
    set_attachment(post_id, saved);
    pthread_mutex_unlock(&file_lock);

    char resp[MAXLINE];
    snprintf(resp, sizeof(resp), "OK|file uploaded|%s\n", saved);
    send_line(fd, resp);
}

void download_bbs(int fd, int post_id) {
    char attachment[512];
    int r = find_attachment(post_id, attachment, sizeof(attachment));

    if (r == -2) {
        send_line(fd, "ERR|post not found\n");
        return;
    }
    if (r == -3) {
        send_line(fd, "ERR|no attachment\n");
        return;
    }

    char path[1024];
    snprintf(path, sizeof(path), "uploads/bbs/%s", attachment);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        send_line(fd, "ERR|file missing\n");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char header[MAXLINE];
    snprintf(header, sizeof(header), "FILE|%s|%ld\n", attachment, size);
    send_line(fd, header);

    char buf[4096];
    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        send_all(fd, buf, n);
    }
    fclose(fp);
}

void save_msg(char *type, char *username, char *to, char *msg) {
    clean(msg);

    pthread_mutex_lock(&file_lock);
    FILE *fp = fopen(MSGS, "a");
    char timebuf[64];
    if (fp) {
        now_time(timebuf, sizeof(timebuf));
        fprintf(fp, "%s|%s|%s|%s|%s\n", timebuf, type, username, to, msg);
        fclose(fp);
    }
    pthread_mutex_unlock(&file_lock);
}

void handle_command(int fd, char *line) {
    char *saveptr = NULL;
    char *cmd = strtok_r(line, "|", &saveptr);
    if (!cmd) return;

    if (strcmp(cmd, "REGISTER") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *p = strtok_r(NULL, "|", &saveptr);
        int r = (u && p) ? register_user(u, p) : -1;
        send_line(fd, r == 0 ? "OK|register success\n" :
                      r == -2 ? "ERR|user exists\n" : "ERR|register failed\n");
    } else if (strcmp(cmd, "LOGIN") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *p = strtok_r(NULL, "|", &saveptr);
        int r = (u && p) ? login_user(u, p) : -1;
        send_line(fd, r == 0 ? "OK|login success\n" : "ERR|wrong user or password\n");
    } else if (strcmp(cmd, "CREATE_POST") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *t = strtok_r(NULL, "|", &saveptr);
        char *c = strtok_r(NULL, "", &saveptr);
        if (u && t && c) create_post(fd, u, t, c);
        else send_line(fd, "ERR|bad post command\n");
    } else if (strcmp(cmd, "LIST_POSTS") == 0) {
        list_posts(fd);
    } else if (strcmp(cmd, "VIEW_POST") == 0) {
        char *id = strtok_r(NULL, "|", &saveptr);
        view_post(fd, id ? atoi(id) : 0);
    } else if (strcmp(cmd, "REPLY_POST") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *id = strtok_r(NULL, "|", &saveptr);
        char *c = strtok_r(NULL, "", &saveptr);
        if (u && id && c) reply_post(fd, u, atoi(id), c);
        else send_line(fd, "ERR|bad reply command\n");
    } else if (strcmp(cmd, "UPLOAD_BBS") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *id = strtok_r(NULL, "|", &saveptr);
        char *fn = strtok_r(NULL, "|", &saveptr);
        char *sz = strtok_r(NULL, "|", &saveptr);
        if (u && id && fn && sz) upload_bbs(fd, u, atoi(id), fn, atol(sz));
        else send_line(fd, "ERR|bad upload command\n");
    } else if (strcmp(cmd, "DOWNLOAD_BBS") == 0) {
        char *id = strtok_r(NULL, "|", &saveptr);
        download_bbs(fd, id ? atoi(id) : 0);
    } else if (strcmp(cmd, "GROUP_CHAT") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *m = strtok_r(NULL, "", &saveptr);
        if (u && m) {
            save_msg("group", u, "all", m);
            send_line(fd, "OK|group message backed up; real-time broadcast is simplified\n");
        } else {
            send_line(fd, "ERR|bad group chat command\n");
        }
    } else if (strcmp(cmd, "PRIVATE_CHAT") == 0) {
        char *u = strtok_r(NULL, "|", &saveptr);
        char *to = strtok_r(NULL, "|", &saveptr);
        char *m = strtok_r(NULL, "", &saveptr);
        if (u && to && m) {
            save_msg("private", u, to, m);
            send_line(fd, "OK|private message backed up; forwarding is simplified\n");
        } else {
            send_line(fd, "ERR|bad private chat command\n");
        }
    } else {
        send_line(fd, "ERR|unknown command\n");
    }
}

void *client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    char line[MAXLINE];
    while (1) {
        ssize_t n = read_line(fd, line, sizeof(line));
        if (n <= 0) break;
        if (strcmp(line, "QUIT") == 0) {
            send_line(fd, "OK|bye\n");
            break;
        }
        handle_command(fd, line);
    }

    close(fd);
    return NULL;
}

int main() {
    mkdirs();

    FILE *fp = fopen(USERS, "a");
    if (fp) fclose(fp);
    fp = fopen(POSTS, "a");
    if (fp) fclose(fp);
    fp = fopen(REPLIES, "a");
    if (fp) fclose(fp);
    fp = fopen(MSGS, "a");
    if (fp) fclose(fp);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    printf("[server] started on port %d\n", PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        int *p = (int *)malloc(sizeof(int));
        if (!p) {
            close(client_fd);
            continue;
        }
        *p = client_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, p);
        pthread_detach(tid);
    }

    return 0;
}
