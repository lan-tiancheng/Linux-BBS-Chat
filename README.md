# Linux Web-BBS 多用户聊天室系统

本项目是在 Linux 平台下开发的基于 Socket 的多客户多终端 BBS/即时通讯系统。

## 功能目标

- 用户注册、登录
- BBS 发帖、回帖
- 文件上传与下载
- 群聊与私聊
- 通讯内容备份
- Makefile 支持 all、clean、install、uninstall

## 编译运行

```bash
make all
./bin/server
./bin/client
```

也可以指定端口和服务端地址：

```bash
./bin/server 9000
./bin/client 127.0.0.1 9000
```

## 第一阶段通信协议

当前协议使用以换行符结尾的 UTF-8 文本命令，每条命令最长 1023 字节：

```text
PING
ECHO <text>
REGISTER <username> <password>
LOGIN <username> <password>
LOGOUT
GROUP <text>
PRIVATE <username> <text>
UPLOAD <username> <local-path>
DOWNLOAD <filename>
WHO
HELP
QUIT
```

服务端主要响应包括 `OK`、`ERR`、`ECHO`、`MSG`、`PMSG`、`ONLINE` 和
`COMMANDS`。账号默认保存在 `data/users.db`。用户名只能包含字母、数字、
下划线和连字符；同一账号不能同时登录两次。`WHO`、`GROUP` 和 `PRIVATE`
需要先登录。`GROUP` 会发送给所有在线用户，包括发送者；`PRIVATE` 只发送给指定
在线用户。成功发送的群聊和私聊会追加到 `logs/chat.log`。

客户端的 `UPLOAD` 命令接受本地文件路径，单个文件最大 16 MiB。服务端将
文件保存到 `uploads/chat/`，并向在线接收者发送：

```text
FILE_READY <sender> <filename> <size>
```

接收者执行 `DOWNLOAD <filename>` 后，客户端会把文件保存到 `downloads/`。
服务端按接收者隔离文件，因此其他用户不能下载该文件。文件名只允许字母、
数字、点、下划线和连字符。

自动编译并运行多客户端集成测试：

```bash
make test
```

测试会验证三客户端群聊、私聊、在线列表、账号持久化、异常断线后的重新
登录，以及二进制文件上传和下载。完整的后端协议见
[`docs/backend_protocol.md`](docs/backend_protocol.md)。
