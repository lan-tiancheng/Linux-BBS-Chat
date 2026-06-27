# Backend Protocol

TCP 默认端口是 `8888`。命令与响应是 UTF-8 文本行，以 `\n` 结尾。文件内容使用二进制 payload，在声明 size 后紧跟指定字节数。

## Accounts

```text
REGISTER <username> <password>
LOGIN <username> <password>
LOGOUT
```

成功示例：

```text
OK registered
OK logged in alice
OK logged out
```

同一个用户名不能同时在两个连接中登录。

## Online Users, Chat And History

```text
WHO
GROUP <message>
PRIVATE <username> <message>
HISTORY
```

事件/响应：

```text
ONLINE <count> <username> ...
MSG <sender> <message>
PMSG <sender> <message>
HISTORY_BEGIN
HMSG <timestamp>|<sender>|<message>
HPMSG <timestamp>|<sender>|<recipient>|<message>
HISTORY_END
OK private message sent
OK private message stored for offline user
ERR user does not exist
ERR cannot send private message to yourself
```

`PRIVATE` 现在会先检查目标用户是否已注册。目标用户注册但离线时，消息不会丢失，会写入 `logs/chat.log`，对方之后执行 `HISTORY` 可以看到。

## Chat File Transfer

```text
UPLOAD <recipient> <filename> <size>\n<binary bytes>
DOWNLOAD <filename>
```

成功下载响应：

```text
FILE <filename> <size>\n<binary bytes>
```

## BBS

```text
BBS_LIST
BBS_VIEW <post-id>
BBS_CREATE <title>|<content>
BBS_REPLY <post-id>|<content>
BBS_UPLOAD_POST <post-id> <filename> <size>\n<binary bytes>
BBS_UPLOAD_REPLY <reply-id> <filename> <size>\n<binary bytes>
BBS_DOWNLOAD_POST <post-id>
BBS_DOWNLOAD_REPLY <reply-id>
```

帖子列表响应：

```text
BBS_POSTS_BEGIN
BBS_POST <id>|<author>|<title>|<content>|<attachment>|<time>
BBS_POSTS_END
```

帖子详情响应：

```text
BBS_POST_BEGIN
BBS_POST <id>|<author>|<title>|<content>|<attachment>|<time>
BBS_REPLIES_BEGIN
BBS_REPLY <id>|<post-id>|<author>|<content>|<attachment>|<time>
BBS_REPLIES_END
BBS_POST_END
```

附件下载响应：

```text
BBS_FILE <filename> <size>\n<binary bytes>
```

权限规则：

- 只有帖子作者可以执行 `BBS_UPLOAD_POST` 给自己的帖子加附件。
- 只有回复作者可以执行 `BBS_UPLOAD_REPLY` 给自己的回复加附件。
- 其他用户可以下载附件，但不能替别人上传附件。
