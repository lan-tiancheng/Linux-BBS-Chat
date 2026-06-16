# Backend Protocol

This document describes the member A server interface used by the terminal
client and by the BBS/client integration work.

## Transport

- TCP, default port `8888`.
- Text commands and responses are UTF-8 lines terminated by `\n`.
- A text line is at most 1023 bytes, excluding the newline.
- Commands are uppercase and arguments are separated by ASCII spaces.
- The connection remains open after an `ERR` response unless noted otherwise.

File payloads are binary frames. After an `UPLOAD` command the sender must send
exactly the declared number of bytes. After a `FILE` response the server sends
exactly the declared number of bytes. No newline follows binary payload data.

## Connection Commands

```text
PING
HELP
ECHO <text>
QUIT
```

`PING` returns `OK PONG`. `QUIT` returns `OK bye` and closes the connection.
These commands do not require login.

## Accounts

```text
REGISTER <username> <password>
LOGIN <username> <password>
LOGOUT
```

Usernames are 1-31 characters and may contain letters, digits, `_`, and `-`.
Passwords are 1-63 non-whitespace characters and cannot contain `:`. The
current course-project storage is `data/users.db`; passwords are plain text and
must not be treated as production-grade authentication.

Successful responses include:

```text
OK registered
OK logged in <username>
OK logged out
```

Only one active connection may log in as a given username. Closing a socket
without `LOGOUT` automatically removes its online state.

## Online Users And Chat

These commands require login:

```text
WHO
GROUP <message>
PRIVATE <username> <message>
```

Responses and asynchronous events:

```text
ONLINE <count> <username> ...
MSG <sender> <message>
PMSG <sender> <message>
OK private message sent
ERR user is not online
```

`GROUP` sends `MSG` to every online user, including the sender. `PRIVATE`
sends `PMSG` only to the target. Successful chat messages are appended to
`logs/chat.log`.

## File Transfer

The interactive client accepts:

```text
UPLOAD <recipient> <local-path>
DOWNLOAD <filename>
```

For an upload, the client converts the local command to this wire frame:

```text
UPLOAD <recipient> <filename> <size>\n
<exactly size binary bytes>
```

Files are limited to 16 MiB. Filenames may contain letters, digits, `.`, `_`,
and `-`; directory components are never accepted by the server. A successful
upload returns the following response. An existing file for the same recipient
is not overwritten; the sender receives `ERR upload failed` instead.

```text
OK uploaded <filename> for <recipient>
```

If the recipient is online, that connection also receives:

```text
FILE_READY <sender> <filename> <size>
```

The recipient downloads with `DOWNLOAD <filename>`. The response is:

```text
FILE <filename> <size>\n
<exactly size binary bytes>
```

The bundled client saves downloads under `downloads/` and adds a numeric
prefix when a local filename already exists. Server-side files are scoped to
the recipient username, so another account cannot download them.

## Common Errors

```text
ERR login required
ERR unknown command; send HELP
ERR invalid username or password
ERR username already exists
ERR user already logged in
ERR recipient does not exist
ERR user is not online
ERR file not found
```

Clients must also be prepared to display other `ERR <message>` responses.
An invalid `UPLOAD` size closes the connection because the server cannot safely
determine where the following binary frame ends.
