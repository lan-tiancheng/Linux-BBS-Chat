#include "social.h"
#include "storage.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t social_mutex = PTHREAD_MUTEX_INITIALIZER;

static int append_line(const char *path, const char *line)
{
    FILE *file = fopen(path, "a");
    int result = -1;

    if (file == NULL) {
        return -1;
    }
    if (fputs(line, file) != EOF && fputc('\n', file) != EOF &&
        fflush(file) == 0) {
        result = 0;
    }
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}

static int pair_matches(const char *a, const char *b, const char *left,
                        const char *right)
{
    return (strcmp(a, left) == 0 && strcmp(b, right) == 0) ||
           (strcmp(a, right) == 0 && strcmp(b, left) == 0);
}

static int social_are_friends_unlocked(const char *left, const char *right)
{
    FILE *file;
    char line[256];
    int result = 0;

    file = fopen(storage_friends_file(), "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *a = strtok(line, "|");
        char *b = strtok(NULL, "|\r\n");

        if (a != NULL && b != NULL && pair_matches(a, b, left, right)) {
            result = 1;
            break;
        }
    }
    fclose(file);
    return result;
}

int social_init(void)
{
    FILE *file;
    const char *paths[] = {
        storage_friends_file(),
        storage_private_requests_file(),
        storage_groups_file(),
        storage_group_members_file(),
    };
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        file = fopen(paths[i], "a");
        if (file == NULL) {
            return -1;
        }
        fclose(file);
    }
    return 0;
}

int social_are_friends(const char *left, const char *right)
{
    int result;

    pthread_mutex_lock(&social_mutex);
    result = social_are_friends_unlocked(left, right);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_add_friend_pair(const char *left, const char *right)
{
    char line[128];
    int result;

    if (strcmp(left, right) == 0) {
        return -1;
    }
    if (social_are_friends(left, right)) {
        return 0;
    }
    snprintf(line, sizeof(line), "%s|%s", left, right);
    pthread_mutex_lock(&social_mutex);
    result = append_line(storage_friends_file(), line);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_add_private_request(const char *from, const char *to,
                               const char *message)
{
    char line[1400];
    int result;

    if (social_has_private_request(from, to)) {
        return 0;
    }
    snprintf(line, sizeof(line), "%s|%s|%s", from, to, message);
    pthread_mutex_lock(&social_mutex);
    result = append_line(storage_private_requests_file(), line);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_has_private_request(const char *from, const char *to)
{
    FILE *file;
    char line[1400];
    int result = 0;

    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_private_requests_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *stored_from = strtok(line, "|");
        char *stored_to = strtok(NULL, "|");

        if (stored_from != NULL && stored_to != NULL &&
            strcmp(stored_from, from) == 0 && strcmp(stored_to, to) == 0) {
            result = 1;
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_visit_friends(const char *account, SocialFriendVisitor visitor,
                         void *context)
{
    FILE *file;
    char line[256];
    int result = 0;

    if (visitor == NULL) {
        return -1;
    }
    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_friends_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *a = strtok(line, "|");
        char *b = strtok(NULL, "|\r\n");

        if (a == NULL || b == NULL) {
            continue;
        }
        if (strcmp(a, account) == 0 && visitor(b, context) != 0) {
            result = -1;
            break;
        }
        if (strcmp(b, account) == 0 && visitor(a, context) != 0) {
            result = -1;
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_visit_requests_for(const char *account, SocialRequestVisitor visitor,
                              void *context)
{
    FILE *file;
    char line[1400];
    int result = 0;

    if (visitor == NULL) {
        return -1;
    }
    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_private_requests_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *from = strtok(line, "|");
        char *to = strtok(NULL, "|");
        char *message = strtok(NULL, "\r\n");

        if (from != NULL && to != NULL && message != NULL &&
            strcmp(to, account) == 0 &&
            !social_are_friends_unlocked(from, to) &&
            visitor(from, to, message, context) != 0) {
            result = -1;
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_visit_sent_requests_for(const char *account,
                                   SocialRequestVisitor visitor,
                                   void *context)
{
    FILE *file;
    char line[1400];
    int result = 0;

    if (visitor == NULL) {
        return -1;
    }
    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_private_requests_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *from = strtok(line, "|");
        char *to = strtok(NULL, "|");
        char *message = strtok(NULL, "\r\n");

        if (from != NULL && to != NULL && message != NULL &&
            strcmp(from, account) == 0 &&
            !social_are_friends_unlocked(from, to) &&
            visitor(from, to, message, context) != 0) {
            result = -1;
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

static unsigned long next_group_id_unlocked(void)
{
    FILE *file = fopen(storage_groups_file(), "r");
    char line[512];
    unsigned long max_id = 0;

    if (file == NULL) {
        return 1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long id = 0;
        if (sscanf(line, "%lu|", &id) == 1 && id > max_id) {
            max_id = id;
        }
    }
    fclose(file);
    return max_id + 1;
}

int social_create_group(const char *owner, const char *name,
                        const char **members, size_t member_count,
                        unsigned long *group_id)
{
    char line[512];
    unsigned long id;
    size_t i;
    int result = 0;

    pthread_mutex_lock(&social_mutex);
    id = next_group_id_unlocked();
    snprintf(line, sizeof(line), "%lu|%s|%s", id, owner, name);
    if (append_line(storage_groups_file(), line) < 0) {
        result = -1;
        goto done;
    }
    snprintf(line, sizeof(line), "%lu|%s", id, owner);
    if (append_line(storage_group_members_file(), line) < 0) {
        result = -1;
        goto done;
    }
    for (i = 0; i < member_count; i++) {
        if (members[i] == NULL || strcmp(members[i], owner) == 0) {
            continue;
        }
        snprintf(line, sizeof(line), "%lu|%s", id, members[i]);
        if (append_line(storage_group_members_file(), line) < 0) {
            result = -1;
            goto done;
        }
    }
    if (group_id != NULL) {
        *group_id = id;
    }
done:
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_is_group_member(unsigned long group_id, const char *account)
{
    FILE *file;
    char line[256];
    int result = 0;

    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_group_members_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long id = 0;
        char member[64];

        if (sscanf(line, "%lu|%63[^\r\n]", &id, member) == 2 &&
            id == group_id && strcmp(member, account) == 0) {
            result = 1;
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_visit_groups_for(const char *account, SocialGroupVisitor visitor,
                            void *context)
{
    FILE *file;
    char line[512];
    unsigned long ids[256];
    size_t id_count = 0;
    size_t i;
    int result = 0;

    if (visitor == NULL) {
        return -1;
    }
    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_group_members_file(), "r");
    if (file != NULL) {
        while (fgets(line, sizeof(line), file) != NULL &&
               id_count < sizeof(ids) / sizeof(ids[0])) {
            unsigned long id = 0;
            char member[64];

            if (sscanf(line, "%lu|%63[^\r\n]", &id, member) == 2 &&
                strcmp(member, account) == 0) {
                ids[id_count++] = id;
            }
        }
        fclose(file);
    }
    file = fopen(storage_groups_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long id = 0;
        char *id_text = strtok(line, "|");
        char *owner = strtok(NULL, "|");
        char *name = strtok(NULL, "\r\n");

        if (id_text == NULL || owner == NULL || name == NULL) {
            continue;
        }
        id = strtoul(id_text, NULL, 10);
        for (i = 0; i < id_count; i++) {
            if (ids[i] == id && visitor(id, owner, name, context) != 0) {
                result = -1;
                break;
            }
        }
        if (result < 0) {
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}

int social_visit_group_members(unsigned long group_id,
                               SocialMemberVisitor visitor, void *context)
{
    FILE *file;
    char line[256];
    int result = 0;

    if (visitor == NULL) {
        return -1;
    }
    pthread_mutex_lock(&social_mutex);
    file = fopen(storage_group_members_file(), "r");
    if (file == NULL) {
        pthread_mutex_unlock(&social_mutex);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long id = 0;
        char member[64];

        if (sscanf(line, "%lu|%63[^\r\n]", &id, member) == 2 &&
            id == group_id && visitor(member, context) != 0) {
            result = -1;
            break;
        }
    }
    fclose(file);
    pthread_mutex_unlock(&social_mutex);
    return result;
}
