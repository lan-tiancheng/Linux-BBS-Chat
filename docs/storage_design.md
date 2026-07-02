# 存储与目录设计

本文档由成员 C 负责整理，用于统一项目的数据保存方式和目录职责。

## 目标

- 保证用户、消息、文件记录能稳定落盘
- 让老师和队友看清楚运行时会生成哪些文件
- 让测试和后期打包有明确的目录边界

## 默认目录

```text
Linux-BBS-Chat/
├── data/

#### notifications.db

格式：

    notification_id|account|type|target|message|created_at|read

通知字段采用和 BBS/群聊存储一致的文本落盘方式；字符串字段会进行简单转义，便于保存包含空格的消息。
├── logs/
├── uploads/
├── downloads/
└── backup/
```

## 当前约定

### `data/`

保存核心持久化数据。当前实现默认使用：

- `data/users.db`
- `data/posts.db`
- `data/replies.db`
- `data/files.db`
- data/notifications.db

用户数据采用文本格式，每行一条记录，便于调试和备份。

#### `users.db`

格式：

```text
username:password
```

#### `posts.db`

格式：

```text
post_id|author|title|content|created_at|updated_at|active
```

#### `replies.db`

格式：

```text
reply_id|post_id|author|content|created_at|active
```

#### `files.db`

格式：

```text
file_id|owner|sender|recipient|filename|stored_name|size|created_at|active
```


#### notifications.db

格式：

    notification_id|account|type|target|message|created_at|read

通知字段采用和 BBS/群聊存储一致的文本落盘方式；字符串字段会进行简单转义，便于保存包含空格的消息。
### `logs/`

保存运行日志和聊天日志。当前实现默认使用：


#### notifications.db

格式：

    notification_id|account|type|target|message|created_at|read

通知字段采用和 BBS/群聊存储一致的文本落盘方式；字符串字段会进行简单转义，便于保存包含空格的消息。
- `logs/chat.log`

格式示例：

```text
[2026-06-25 12:00:00] [GROUP] alice: hello
[2026-06-25 12:01:00] [PRIVATE] alice -> bob: hi
```

群聊和私聊成功后会追加日志，便于验收和排错。

### `uploads/`

保存服务端收到的文件。当前实现默认使用：

- `uploads/chat/`

文件按接收对象进行隔离，避免不同用户直接混放。

文件命名建议：

```text
recipient__filename
```

### `downloads/`

保存客户端本地下载结果。客户端会优先把文件写入该目录，
如遇重名会自动增加前缀。

### `backup/`

预留给后续备份、归档、打包导出使用。当前阶段先保留目录，
后续如需要可在这里放数据库快照或提交前备份。

快照结构建议：

```text
backup/<label>/
├── data/

#### notifications.db

格式：

    notification_id|account|type|target|message|created_at|read

通知字段采用和 BBS/群聊存储一致的文本落盘方式；字符串字段会进行简单转义，便于保存包含空格的消息。
├── logs/
└── uploads/
```

## 环境变量

实现里支持通过环境变量覆盖默认路径：

- `BBS_USERS_FILE`
- `BBS_CHAT_LOG`
- `BBS_UPLOAD_DIR`
- `BBS_DOWNLOAD_DIR`
- BBS_NOTIFICATIONS_FILE

这样测试时可以把数据放到临时目录，避免污染仓库本体。

## 备份建议

- 交作业前导出一次完整目录

#### notifications.db

格式：

    notification_id|account|type|target|message|created_at|read

通知字段采用和 BBS/群聊存储一致的文本落盘方式；字符串字段会进行简单转义，便于保存包含空格的消息。
- `data/`、`logs/`、`uploads/` 按日期打包
- 报告里说明哪些数据属于运行时生成，哪些属于提交材料

## 后续扩展

如果后面补 BBS 模块，可以继续把帖子、回复、附件索引分拆到独立文件，
避免全部塞进一个大文件里。
