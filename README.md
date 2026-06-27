# Linux Web-BBS 多用户聊天系统

这是一个基于 Linux Socket 的多客户端聊天与 BBS 项目。

## 当前状态

`dev` 分支已经包含后端聊天、登录注册、文件传输和基础测试脚本。
成员 C 主要负责工程结构、数据持久化约定、文档整理和最终交付材料。

## 目录

- `src/`：源代码
- `include/`：头文件
- `data/`：持久化数据
- `logs/`：运行日志
- `uploads/`：服务端接收文件
- `downloads/`：客户端下载文件
- `backup/`：备份预留
- `docs/`：协议、存储设计和报告
- `tests/`：集成测试

## 构建

```bash
make all
```

生成：

```text
bin/server
bin/client
```

## 运行

```bash
./bin/server
./bin/client
```

也可以指定端口：

```bash
./bin/server 9000
./bin/client 127.0.0.1 9000
```

## 测试

```bash
make test
```

测试会使用临时文件和临时目录，不会污染仓库里的默认数据。

## 协议

服务端命令和文件传输协议见：

[`docs/backend_protocol.md`](docs/backend_protocol.md)

## 存储结构

默认目录约定如下：

- `data/`：用户和持久化数据
- `logs/`：聊天日志
- `uploads/`：服务端上传文件
- `downloads/`：客户端下载文件
- `backup/`：备份和归档

对应的环境变量：

- `BBS_USERS_FILE`
- `BBS_CHAT_LOG`
- `BBS_UPLOAD_DIR`
- `BBS_DOWNLOAD_DIR`

详细说明见：

[`docs/storage_design.md`](docs/storage_design.md)

## 协作

建议统一在 `dev` 上开发，不要直接改 `main`。
功能完成后再合并到主线。

## 交付建议

- 提交前先跑一次 `make test`
- 确认 `data/`、`logs/`、`uploads/`、`downloads/`、`backup/` 的目录约定一致
- 报告和 README 保持同一套术语
