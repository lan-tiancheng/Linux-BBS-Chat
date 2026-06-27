# Linux BBS/Chat 多客户端系统

这是一个基于 Linux Socket 的多客户端聊天与 BBS 系统，整合了命令行客户端、Qt 前端、用户管理、聊天、帖子、回复、文件上传下载、持久化存储、备份和自动化测试。

## 功能

- 用户注册、登录、登出、在线用户列表
- 群聊、私聊、离线私聊保存、聊天历史
- 聊天文件上传和下载
- BBS 发帖、帖子列表、帖子详情、回帖
- 帖子附件、回复附件上传和下载
- `data/`、`logs/`、`uploads/`、`downloads/`、`backup/` 统一目录结构
- `users.db`、`posts.db`、`replies.db`、`files.db`、`chat.log` 持久化保存
- Docker 后端运行环境
- 自动化集成测试

## 目录

```text
src/                    C 后端和命令行客户端源码
include/                头文件
frontend/qt_client/     Qt 图形客户端源码
data/                   运行时数据文件，默认不提交
logs/                   运行日志，默认不提交
uploads/                服务端上传文件，默认不提交
downloads/              客户端下载文件，默认不提交
backup/                 备份快照，默认不提交
docs/                   协议、存储设计和项目报告
tests/                  自动化测试
```

## Docker 运行后端

构建镜像：

```bash
docker build -t linux-bbs-chat .
```

编译并运行测试：

```bash
docker run --rm -v "$PWD:/workspace" -w /workspace linux-bbs-chat bash -lc "make clean && make all && make test"
```

启动服务端：

```bash
docker run --rm -it -p 8888:8888 -v "$PWD:/workspace" -w /workspace linux-bbs-chat bash -lc "make all && ./bin/server 8888"
```

Windows PowerShell 使用：

```powershell
docker run --rm -it -p 8888:8888 -v "${PWD}:/workspace" -w /workspace linux-bbs-chat bash -lc "make all && ./bin/server 8888"
```

## 命令行客户端

服务端启动后，另开一个终端：

```bash
./bin/client 127.0.0.1 8888
```

常用命令：

```text
REGISTER alice 123456
LOGIN alice 123456
GROUP hello
PRIVATE bob hi
POST title content
LISTPOST
VIEWPOST 1
BACKUP final
QUIT
```

## Qt 前端

Qt 前端源码位于 `frontend/qt_client/`。在安装 Qt5 开发环境的 Linux/Ubuntu 中编译：

```bash
cd frontend/qt_client
qmake qt_client.pro
make
./bbs_chat_qt
```

如果系统使用 `qmake-qt5`：

```bash
qmake-qt5 qt_client.pro
make
./bbs_chat_qt
```

Qt 前端连接默认地址 `127.0.0.1:8888`。详细说明见 `README_QT_INTEGRATION.md`。

## 测试

```bash
make test
```

测试覆盖：

- 多客户端登录、聊天、文件传输
- 服务端重启和断开连接
- 数据表、目录和备份结构
- Qt 前端依赖的 BBS 协议、帖子/回复附件上传下载

## 协议和存储文档

- 后端协议：`docs/backend_protocol.md`
- 存储设计：`docs/storage_design.md`
- 项目报告：`docs/project_report.md`

## 提交前检查

```bash
make clean
make all
make test
```

不要提交运行时生成的 `data/*.db`、`logs/*.log`、`uploads/*`、`downloads/*`、`backup/*`、Qt `.o` 文件和 Qt 可执行文件。
