#ifndef SOCIAL_H
#define SOCIAL_H

#include <stddef.h>

typedef int (*SocialFriendVisitor)(const char *account, void *context);
typedef int (*SocialRequestVisitor)(const char *from, const char *to,
                                    const char *message, void *context);
typedef int (*SocialGroupVisitor)(unsigned long id, const char *owner,
                                  const char *name, void *context);
typedef int (*SocialMemberVisitor)(const char *account, void *context);

int social_init(void);
int social_are_friends(const char *left, const char *right);
int social_add_friend_pair(const char *left, const char *right);
int social_add_private_request(const char *from, const char *to,
                               const char *message);
int social_has_private_request(const char *from, const char *to);
int social_visit_friends(const char *account, SocialFriendVisitor visitor,
                         void *context);
int social_visit_requests_for(const char *account, SocialRequestVisitor visitor,
                              void *context);
int social_visit_sent_requests_for(const char *account,
                                   SocialRequestVisitor visitor,
                                   void *context);
int social_create_group(const char *owner, const char *name,
                        const char **members, size_t member_count,
                        unsigned long *group_id);
int social_is_group_member(unsigned long group_id, const char *account);
int social_visit_groups_for(const char *account, SocialGroupVisitor visitor,
                            void *context);
int social_visit_group_members(unsigned long group_id,
                               SocialMemberVisitor visitor, void *context);

#endif
