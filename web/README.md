# Web Frontend

`web/` contains a browser-based demonstration frontend for the Linux BBS/Chat project.

The browser cannot connect to the raw TCP socket server directly, so `web/server.py` provides a small HTTP gateway. Each browser session maps to one backend TCP connection and keeps the existing login state.

## Run With Docker

From the repository root:

```bash
docker build -t linux-bbs-chat-web .
docker run --rm -it -p 8080:8080 -p 8888:8888 -v "$PWD:/workspace" -w /workspace linux-bbs-chat-web sh web/run_web.sh
```

Open:

```text
http://127.0.0.1:8080
```

## Run Locally

```bash
make all
python3 web/server.py
```

Open:

```text
http://127.0.0.1:8080
```

## Features

- Register and login
- Online users and chat history
- Group chat and private chat
- BBS post, list, detail and reply
- Chat file upload/download
- BBS post/reply attachment upload/download
- Backup creation
- Console log for raw backend protocol responses

## Test

```bash
make test
```

The test suite includes `tests/test_web_gateway.py`, which verifies the HTTP gateway and BBS/file operations through the web API.
