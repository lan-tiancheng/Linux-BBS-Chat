# Linux BBS/Chat C Demo

这是一个用于 Linux 系统编程大作业的 C 语言终端 socket Demo。
本版本主要完成 BBS 功能：发帖、看帖、回帖、BBS 附件上传和下载。聊天功能、用户功能和工程部分做了简化处理，方便后续继续扩展。

## 编译

```bash
make all
```

如果之前运行过旧版本，建议先清理再编译：

```bash
make clean
make all
```

## 运行

终端 1 启动服务器：

```bash
./server
```

终端 2 启动客户端：

```bash
./client
```

终端 3 可以再启动一个客户端，用于模拟多终端用户：

```bash
./client
```

## 已实现功能

- C 语言 TCP socket 服务端/客户端
- 服务端 pthread 多客户端连接
- 注册、登录简化版
- BBS 发帖
- 查看帖子列表
- 查看帖子详情
- 回帖
- 回复在帖子详情中按当前帖子从 1 开始显示
- BBS 附件上传
- 限制只能给自己发布的帖子上传附件
- BBS 附件下载
- 群聊/私聊简化版：消息保存到 `data/messages.txt`
- Makefile：`all`、`clean`、`install`、`uninstall`

## 数据文件

- `data/users.txt`：用户数据
- `data/posts.txt`：帖子数据
- `data/replies.txt`：回复数据
- `data/messages.txt`：聊天备份
- `uploads/bbs/`：服务器保存的 BBS 附件
- `downloads/`：客户端下载的附件

## 测试建议

1. 运行 `./server`。
2. 打开第一个客户端，注册并登录用户 `alice`。
3. 进入 BBS，发布帖子。
4. 上传附件到 alice 自己发布的帖子。
5. 打开第二个客户端，注册并登录用户 `bob`。
6. bob 可以查看帖子和回复帖子，但不能给 alice 的帖子上传附件。
7. bob 可以下载 alice 帖子中的附件。
