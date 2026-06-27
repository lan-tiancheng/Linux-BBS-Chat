# Backend Protocol

The server listens on TCP port `8888` by default. Commands and normal responses are UTF-8 text lines ending with `\n`. File payloads are sent as raw bytes immediately after a command or response line that declares a byte size.

## Accounts

```text
REGISTER <username> <password>
LOGIN <username> <password>
LOGOUT
WHO
QUIT
```

Examples:

```text
OK registered
OK logged in alice
OK logged out
ONLINE 2 alice bob
```

The same username cannot be logged in from two connections at the same time.

## Chat

```text
GROUP <message>
PRIVATE <username> <message>
HISTORY
```

Realtime events:

```text
MSG <sender> <message>
PMSG <sender> <message>
```

Private messages are delivered immediately when the target user is online. If the target user is registered but offline, the message is saved to `logs/chat.log` and the sender receives:

```text
OK private message stored for offline user
```

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

## Storage

Default paths:

```text
data/users.db
data/posts.db
data/replies.db
data/files.db
logs/chat.log
uploads/chat/
downloads/
backup/
```

The storage paths can be redirected with environment variables documented in `docs/storage_design.md`.
