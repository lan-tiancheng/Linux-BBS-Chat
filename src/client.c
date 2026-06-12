// client.c - Linux BBS/Chat C socket demo client
// Main purpose: provide terminal interaction for BBS functions.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define IP "127.0.0.1"
#define PORT 8888
#define MAXLINE 2048

void clean(char *s) {
    for (int i = 0; s && s[i]; i++) {
        if (s[i] == '|' || s[i] == '\n' || s[i] == '\r') {
            s[i] = ' ';
        }
    }
}

void input_text(const char *prompt, char *buf, int n) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, n, stdin)) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\n")] = '\0';
    clean(buf);
}

int input_int(const char *prompt) {
    char buf[64];
    input_text(prompt, buf, sizeof(buf));
    return atoi(buf);
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

void print_result(char *resp) {
    if (strncmp(resp, "OK|", 3) == 0) {
        printf("[成功] %s\n", resp + 3);
    } else if (strncmp(resp, "ERR|", 4) == 0) {
        printf("[错误] %s\n", resp + 4);
    } else {
        printf("%s\n", resp);
    }
}

int request_line(int fd, const char *cmd, char *resp) {
    send_line(fd, cmd);
    return read_line(fd, resp, MAXLINE) > 0;
}

int connect_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, IP, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

void show_post(char *line) {
    char *saveptr = NULL;
    char *id = strtok_r(line, "|", &saveptr);
    char *author = strtok_r(NULL, "|", &saveptr);
    char *title = strtok_r(NULL, "|", &saveptr);
    char *content = strtok_r(NULL, "|", &saveptr);
    char *attachment = strtok_r(NULL, "|", &saveptr);
    char *timebuf = strtok_r(NULL, "\n", &saveptr);

    if (id && author && title && content && attachment && timebuf) {
        printf("--------------------------------\n");
        printf("ID: %s\n", id);
        printf("作者: %s\n", author);
        printf("标题: %s\n", title);
        printf("内容: %s\n", content);
        printf("附件: %s\n", attachment);
        printf("时间: %s\n", timebuf);
    }
}

void show_reply(char *line, int display_no) {
    char *saveptr = NULL;
    strtok_r(line, "|", &saveptr);                 // real reply id, hidden from user
    strtok_r(NULL, "|", &saveptr);                 // post id
    char *author = strtok_r(NULL, "|", &saveptr);
    char *content = strtok_r(NULL, "|", &saveptr);
    char *timebuf = strtok_r(NULL, "\n", &saveptr);

    if (author && content && timebuf) {
        printf("回复%d | %s | %s\n", display_no, author, timebuf);
        printf("  %s\n", content);
    }
}

int auth_menu(int fd, char *username) {
    while (1) {
        printf("\n===== 用户入口 =====\n");
        printf("1. 注册\n");
        printf("2. 登录\n");
        printf("0. 退出\n");

        int choice = input_int("请选择: ");
        if (choice == 0) return 0;

        char u[128], p[128], cmd[MAXLINE], resp[MAXLINE];
        input_text("用户名: ", u, sizeof(u));
        input_text("密码: ", p, sizeof(p));

        if (choice == 1) {
            snprintf(cmd, sizeof(cmd), "REGISTER|%s|%s\n", u, p);
        } else if (choice == 2) {
            snprintf(cmd, sizeof(cmd), "LOGIN|%s|%s\n", u, p);
        } else {
            printf("[错误] 无效选项。\n");
            continue;
        }

        if (request_line(fd, cmd, resp)) {
            print_result(resp);
            if (choice == 2 && strncmp(resp, "OK|", 3) == 0) {
                strcpy(username, u);
                return 1;
            }
        }
    }
}

void create_post(int fd, const char *username) {
    char title[256], content[1024], cmd[MAXLINE], resp[MAXLINE];
    input_text("标题: ", title, sizeof(title));
    input_text("内容: ", content, sizeof(content));

    snprintf(cmd, sizeof(cmd), "CREATE_POST|%s|%s|%s\n", username, title, content);
    if (request_line(fd, cmd, resp)) {
        print_result(resp);
    }
}

void list_posts(int fd) {
    char line[MAXLINE];
    send_line(fd, "LIST_POSTS\n");

    if (read_line(fd, line, sizeof(line)) <= 0) return;  // POSTS_BEGIN

    printf("\n===== 帖子列表 =====\n");
    int count = 0;
    while (read_line(fd, line, sizeof(line)) > 0 && strcmp(line, "POSTS_END") != 0) {
        char copy[MAXLINE];
        strcpy(copy, line);
        show_post(copy);
        count++;
    }

    if (count == 0) {
        printf("暂无帖子。\n");
    }
}

void view_post(int fd) {
    int post_id = input_int("帖子ID: ");
    char cmd[128], line[MAXLINE];
    snprintf(cmd, sizeof(cmd), "VIEW_POST|%d\n", post_id);
    send_line(fd, cmd);

    if (read_line(fd, line, sizeof(line)) <= 0) return;  // POST_BEGIN
    if (read_line(fd, line, sizeof(line)) <= 0) return;  // post line or NOT_FOUND

    if (strcmp(line, "NOT_FOUND") == 0) {
        printf("[错误] 帖子不存在。\n");
        while (read_line(fd, line, sizeof(line)) > 0 && strcmp(line, "POST_END") != 0) {
            ;
        }
        return;
    }

    printf("\n===== 帖子详情 =====\n");
    char copy[MAXLINE];
    strcpy(copy, line);
    show_post(copy);

    if (read_line(fd, line, sizeof(line)) <= 0) return;  // REPLIES_BEGIN

    printf("\n===== 回复 =====\n");
    int count = 0;
    while (read_line(fd, line, sizeof(line)) > 0 && strcmp(line, "REPLIES_END") != 0) {
        strcpy(copy, line);
        count++;
        show_reply(copy, count);
    }

    if (count == 0) {
        printf("暂无回复。\n");
    }

    while (read_line(fd, line, sizeof(line)) > 0 && strcmp(line, "POST_END") != 0) {
        ;
    }
}

void reply_post(int fd, const char *username) {
    int post_id = input_int("回复帖子ID: ");
    char content[1024], cmd[MAXLINE], resp[MAXLINE];
    input_text("回复内容: ", content, sizeof(content));

    snprintf(cmd, sizeof(cmd), "REPLY_POST|%s|%d|%s\n", username, post_id, content);
    if (request_line(fd, cmd, resp)) {
        print_result(resp);
    }
}

void upload_attachment(int fd, const char *username) {
    int post_id = input_int("绑定附件的帖子ID: ");
    char path[512];
    input_text("本地文件路径: ", path, sizeof(path));

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char header[MAXLINE];
    snprintf(header, sizeof(header), "UPLOAD_BBS|%s|%d|%s|%ld\n",
             username, post_id, basename2(path), size);
    send_line(fd, header);

    char buf[4096];
    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        send_all(fd, buf, n);
    }
    fclose(fp);

    char resp[MAXLINE];
    if (read_line(fd, resp, sizeof(resp)) > 0) {
        print_result(resp);
    }
}

void download_attachment(int fd) {
    mkdir("downloads", 0755);

    int post_id = input_int("下载附件的帖子ID: ");
    char cmd[128], line[MAXLINE];
    snprintf(cmd, sizeof(cmd), "DOWNLOAD_BBS|%d\n", post_id);
    send_line(fd, cmd);

    if (read_line(fd, line, sizeof(line)) <= 0) return;

    if (strncmp(line, "ERR|", 4) == 0) {
        print_result(line);
        return;
    }

    char *saveptr = NULL;
    char *tag = strtok_r(line, "|", &saveptr);
    char *filename = strtok_r(NULL, "|", &saveptr);
    char *sizestr = strtok_r(NULL, "|", &saveptr);

    if (!tag || !filename || !sizestr || strcmp(tag, "FILE") != 0) {
        printf("[错误] 服务器返回格式错误。\n");
        return;
    }

    long size = atol(sizestr);
    char outpath[600];
    snprintf(outpath, sizeof(outpath), "downloads/%s", filename);

    FILE *fp = fopen(outpath, "wb");
    if (!fp) {
        perror("fopen");
        return;
    }

    char buf[4096];
    while (size > 0) {
        size_t chunk = size > (long)sizeof(buf) ? sizeof(buf) : (size_t)size;
        if (recv_all(fd, buf, chunk) < 0) break;
        fwrite(buf, 1, chunk, fp);
        size -= (long)chunk;
    }
    fclose(fp);

    printf("[成功] 已下载到 %s\n", outpath);
}

void bbs_menu(int fd, const char *username) {
    while (1) {
        printf("\n===== BBS菜单 =====\n");
        printf("1. 发帖\n");
        printf("2. 看帖子列表\n");
        printf("3. 看帖子详情\n");
        printf("4. 回帖\n");
        printf("5. 上传帖子附件\n");
        printf("6. 下载帖子附件\n");
        printf("0. 返回\n");

        int choice = input_int("请选择: ");
        if (choice == 0) return;

        if (choice == 1) create_post(fd, username);
        else if (choice == 2) list_posts(fd);
        else if (choice == 3) view_post(fd);
        else if (choice == 4) reply_post(fd, username);
        else if (choice == 5) upload_attachment(fd, username);
        else if (choice == 6) download_attachment(fd);
        else printf("[错误] 无效选项。\n");
    }
}

void chat_simple(int fd, const char *username, int is_private) {
    char to[128] = "all";
    char msg[1024], cmd[MAXLINE], resp[MAXLINE];

    if (is_private) {
        input_text("接收用户: ", to, sizeof(to));
    }

    input_text(is_private ? "私聊消息: " : "群聊消息: ", msg, sizeof(msg));

    if (is_private) {
        snprintf(cmd, sizeof(cmd), "PRIVATE_CHAT|%s|%s|%s\n", username, to, msg);
    } else {
        snprintf(cmd, sizeof(cmd), "GROUP_CHAT|%s|%s\n", username, msg);
    }

    if (request_line(fd, cmd, resp)) {
        print_result(resp);
    }
}

int main() {
    int fd = connect_server();
    if (fd < 0) {
        printf("请先运行 ./server\n");
        return 1;
    }

    char username[128];
    if (!auth_menu(fd, username)) {
        close(fd);
        return 0;
    }

    while (1) {
        printf("\n===== Linux BBS/Chat Client =====\n");
        printf("当前用户: %s\n", username);
        printf("1. BBS功能\n");
        printf("2. 群聊简化版\n");
        printf("3. 私聊简化版\n");
        printf("0. 退出\n");

        int choice = input_int("请选择: ");
        if (choice == 0) {
            send_line(fd, "QUIT\n");
            break;
        }

        if (choice == 1) bbs_menu(fd, username);
        else if (choice == 2) chat_simple(fd, username, 0);
        else if (choice == 3) chat_simple(fd, username, 1);
        else printf("[错误] 无效选项。\n");
    }

    close(fd);
    return 0;
}
