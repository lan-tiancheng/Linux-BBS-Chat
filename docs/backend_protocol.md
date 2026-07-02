# Backend Protocol

The server listens on TCP port `8888` by default. Commands and normal responses are UTF-8 text lines ending with `\n`. File payloads are sent as raw bytes immediately after a command or response line that declares a byte size.

## Accounts

```text
REGISTER <account9> <password> <nickname>
LOGIN <account-or-nickname> <password>
LOGOUT
WHO
QUIT
```

Examples:

```text
OK registered
OK logged in 100000001|alpha
OK logged out
ONLINE 2 100000001|alpha 100000002|beta
```

Accounts must be exactly 9 digits. Passwords must be longer than 6 characters and contain both letters and digits. Nicknames and accounts are unique. The same account cannot be logged in from two connections at the same time.

## Chat

```text
SEARCH_USER <account-or-nickname>
FRIENDS
REQUESTS
PRIVATE_START <account-or-nickname> <message>
PRIVATE_REPLY <account-or-nickname> <message>
GROUP_CREATE <group_name> <friend1,friend2,...>
GROUPS
GROUP_SEND <group_id> <message>
HISTORY
```

Realtime events:

```text
GMSG <group_id> <sender> <message>
PMSG <sender> <message>
EVENT GROUP_INVITED <group_id>|<owner>|<name>
EVENT BBS_POST_CREATED <post_id>|<author>|<title>
EVENT BBS_REPLY_CREATED <post_id>|<reply_id>|<author>
```

Private chat starts as a request. A non-friend may send one initial message with `PRIVATE_START`. When the receiver replies with `PRIVATE_REPLY`, both users become friends. Groups are created from existing friends only; the old global group chat is removed.

History response:

```text
HISTORY_BEGIN
HMSG <timestamp>|<sender>|<message>
HPMSG <timestamp>|<sender>|<recipient>|<message>
HISTORY_END
```

## Chat File Transfer

Client to server:

```text
UPLOAD <recipient> <filename> <size>\n<binary bytes>
```

Server notification:

```text
FILE_READY <sender> <filename> <size>
```

Download:

```text
DOWNLOAD <filename>
```

Successful download response:

```text
FILE <filename> <size>\n<binary bytes>
```

## BBS Commands Used By The CLI

```text
POST <title> <content>
REPLY <post_id> <content>
LISTPOST
VIEWPOST <post_id>
BACKUP [label]
```

## BBS Commands Used By The Qt Client

```text
BBS_LIST
BBS_VIEW <post_id>
BBS_CREATE <title>|<content>
BBS_REPLY <post_id>|<content>
BBS_UPLOAD_POST <post_id> <filename> <size>\n<binary bytes>
BBS_UPLOAD_REPLY <reply_id> <filename> <size>\n<binary bytes>
BBS_DOWNLOAD_POST <post_id>
BBS_DOWNLOAD_REPLY <reply_id>
```

Post list response:

```text
BBS_POSTS_BEGIN
BBS_POST <id>|<author>|<title>|<content>|<attachment>|<time>
BBS_POSTS_END
```

Post detail response:

```text
BBS_POST_BEGIN
BBS_POST <id>|<author>|<title>|<content>|<attachment>|<time>
BBS_REPLIES_BEGIN
BBS_REPLY <id>|<post_id>|<author>|<content>|<attachment>|<time>
BBS_REPLIES_END
BBS_POST_END
```

BBS attachment download response:

```text
BBS_FILE <filename> <size>\n<binary bytes>
```

Attachment permission rules:

- Only the post author may upload a post attachment.
- Only the reply author may upload a reply attachment.
- Other users may download attachments.


## Notifications

Commands:

    NOTIFICATIONS
    MARK_READ <notification_id>
    MARK_READ_ALL

NOTIFICATIONS returns persisted notifications for the logged-in account. Each row is pipe-separated:

    NOTIFICATIONS_BEGIN
    NOTIFICATION <id>|<type>|<target>|<message>|<created_at>|<read_flag>
    NOTIFICATIONS_END

read_flag is 0 for unread and 1 for read. Realtime EVENT delivery is unchanged; matching notification rows are still written so offline users can retrieve missed group invites, BBS replies, file-ready events, and private messages later.
## Storage

Default paths:

```text
data/users.db
data/posts.db
data/replies.db
data/files.db
data/notifications.db
logs/chat.log
uploads/chat/
downloads/
backup/
```

The storage paths can be redirected with environment variables documented in `docs/storage_design.md`.
