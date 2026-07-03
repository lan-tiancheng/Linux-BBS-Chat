# Qt 前端与 Web 前端一致性说明

本版本的 Qt 客户端已经改为使用 Web 前端同一套后端协议。账号、私聊请求、好友、群聊、通知、BBS 帖子和附件逻辑保持一致。

## 一致的账号规则

- 注册需要填写：9 位数字账号、密码、昵称。
- 账号不能重复，昵称不能重复。
- 密码至少 7 位，并且同时包含字母和数字。
- 登录时可以使用账号或昵称。

示例测试账号：

```text
100000001 / Alpha123 / alpha
100000002 / Beta1234 / beta
100000003 / Carol123 / carol
```

## 一致的聊天机制

Qt 客户端不再使用旧的全局 `GROUP` 群聊和旧 `PRIVATE` 私聊命令，改为：

```text
SEARCH_USER <account-or-nickname>
FRIENDS
REQUESTS
SENT_REQUESTS
PRIVATE_START <account-or-nickname> <message>
PRIVATE_REPLY <account-or-nickname> <message>
GROUP_CREATE <group_name> <friend1,friend2,...>
GROUPS
GROUP_SEND <group_id> <message>
HISTORY_PRIVATE <account-or-nickname>
HISTORY_GROUP <group_id>
NOTIFICATIONS
```

行为与 Web 一致：

- 搜索用户后可以发送第一条私信。
- 对方回复后，双方自动成为好友。
- 好友会显示在会话列表中。
- 只有好友可以被拉入群聊。
- 群聊通过 `GROUP_CREATE` 创建，每个群有独立 ID。
- 打开私聊或群聊时会拉取该会话的历史记录。

## BBS 与附件

Qt 继续使用新版 BBS 协议：

```text
BBS_LIST
BBS_VIEW <post_id>
BBS_CREATE <title>|<content>
BBS_REPLY <post_id>|<content>
BBS_UPLOAD_POST <post_id> <filename> <size>
BBS_UPLOAD_REPLY <reply_id> <filename> <size>
BBS_DOWNLOAD_POST <post_id>
BBS_DOWNLOAD_REPLY <reply_id>
```

帖子附件和回复附件会保存到 Qt 当前运行目录下的 `downloads/`。

## 运行方式

先启动后端：

```bash
make clean
make all
./bin/server 8888
```

再启动 Qt：

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

Ubuntu 依赖：

```bash
sudo apt update
sudo apt install -y build-essential qtbase5-dev qtbase5-dev-tools qt5-qmake
```

## 推荐测试流程

1. 打开两个 Qt 客户端，分别登录 `alpha / Alpha123` 和 `beta / Beta1234`。
2. alpha 搜索 beta，发送第一条私信。
3. beta 在“私信请求”中打开 alpha 的会话并回复。
4. 双方刷新后应互为好友。
5. alpha 选择 beta 为好友，创建一个群聊。
6. beta 刷新后可以看到该群聊。
7. 双方在群聊里发送消息，消息进入对应群 ID 的历史记录。
8. 在 BBS 中发帖、回复、上传/下载附件，确认 Qt 与 Web 的数据一致。

## 主要文件

```text
frontend/qt_client/loginwindow.*      # 登录/注册窗口，账号规则与 Web 一致
frontend/qt_client/networkclient.*    # Qt TCP 协议封装
frontend/qt_client/mainwindow.*       # 私聊/群聊/BBS 主窗口
frontend/qt_client/style.qss          # Qt 样式
src/server.c                          # 后端协议实现
docs/backend_protocol.md              # 后端协议说明
```
