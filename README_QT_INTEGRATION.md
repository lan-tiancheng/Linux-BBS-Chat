# Qt 前端 + 聊天/BBS 后端整合说明（新版）

本版本以 `Linux-BBS-Chat-dev` 的实时聊天后端为基础，把 `feature-bbs` 的论坛功能合并进同一个 `src/server.c`，并重做了 Qt 前端。

## 本次修改重点

1. **界面重做**
   - 使用浅色卡片布局，不再是原来单调的表格风格。
   - 主色改成更稳的青绿色，按钮、卡片、输入框和 Tab 重新设计。
   - BBS 列表改成帖子卡片：大字标题、小字正文摘要、作者、时间、附件标记。

2. **私聊逻辑重做**
   - 私聊统一显示为 `【私聊】我 -> bob` 或 `【私聊】alice -> 我`。
   - 选择自己时，前端会提示“不能私聊自己”。
   - 后端也会拒绝 `PRIVATE 自己 消息`。
   - 目标用户未注册时返回 `ERR user does not exist`，Qt 显示“用户不存在”。
   - 目标用户已注册但离线时，私聊会写入聊天日志；对方上线后点击/自动请求历史记录即可看到。

3. **历史消息**
   - 新增后端命令：`HISTORY`。
   - Qt 登录成功后会自动请求历史消息。
   - 群聊历史会显示为 `【历史群聊】`。
   - 和当前用户有关的私聊历史会显示为 `【历史私聊】`。

4. **BBS 附件逻辑**
   - 发帖时可以选择是否上传附件。
   - 回复时也可以选择是否上传附件。
   - 帖子附件只能由帖子作者上传。
   - 回复附件只能由该回复作者上传。
   - 点击帖子详情里的附件链接，会自动下载到 Qt 程序当前目录下的 `downloads/` 文件夹。

## 后端新增/调整协议

```text
HISTORY
BBS_LIST
BBS_VIEW <post-id>
BBS_CREATE <title>|<content>
BBS_REPLY <post-id>|<content>
BBS_UPLOAD_POST <post-id> <filename> <size>\n<binary bytes>
BBS_UPLOAD_REPLY <reply-id> <filename> <size>\n<binary bytes>
BBS_DOWNLOAD_POST <post-id>
BBS_DOWNLOAD_REPLY <reply-id>
```

兼容旧命令：

```text
BBS_UPLOAD <post-id> <filename> <size>
BBS_DOWNLOAD <post-id>
```

## 在 Ubuntu 虚拟机运行

### 1. 安装依赖

Ubuntu 18.04 / 20.04 可用：

```bash
sudo apt update
sudo apt install -y build-essential qtbase5-dev qtbase5-dev-tools qtchooser
```

如果你的源里有 `qt5-default`，也可以安装：

```bash
sudo apt install -y qt5-default
```

### 2. 编译并启动后端

```bash
cd Linux-BBS-Chat-Qt-integrated
make clean
make all
./bin/server
```

看到类似输出说明后端启动成功：

```text
server listening on 0.0.0.0:8888
```

### 3. 编译并启动 Qt 前端

重新打开一个终端：

```bash
cd Linux-BBS-Chat-Qt-integrated/frontend/qt_client
qmake qt_client.pro
make
./bbs_chat_qt
```

如果你的系统命令是 `qmake-qt5`：

```bash
qmake-qt5 qt_client.pro
make
./bbs_chat_qt
```

## 推荐测试流程

1. 开两个 Qt 客户端，分别注册/登录 `alice` 和 `bob`。
2. `alice` 群聊发送消息，两个窗口都能看到。
3. `alice` 私聊 `bob`，发送方显示 `【私聊】我 -> bob`，接收方显示 `【私聊】alice -> 我`。
4. `bob` 退出或下线后，`alice` 再私聊 `bob`，前端提示“私聊已保存，对方上线后可以看到”。
5. `bob` 重新登录后，历史区能看到 `alice` 发来的离线私聊。
6. `alice` 发帖并选择附件，帖子卡片中会显示附件标记。
7. `bob` 不能给 `alice` 的帖子上传帖子附件，但可以回复并给自己的回复上传附件。
8. 点击帖子详情中的附件链接，文件会保存到 `frontend/qt_client/downloads/`。

## 主要文件

```text
src/server.c                         # 整合聊天、BBS、历史消息、附件权限
frontend/qt_client/networkclient.h   # Qt TCP 协议封装声明
frontend/qt_client/networkclient.cpp # Qt TCP 协议封装实现
frontend/qt_client/loginwindow.*     # 单独登录/注册窗口
frontend/qt_client/mainwindow.*      # 聊天+BBS 主窗口
frontend/qt_client/style.qss         # 新版浅绿色卡片风格 QSS
frontend/qt_client/qt_client.pro     # qmake 工程文件
```

## 2026-06-27 v3 修改说明

本版继续修复 Qt 前端体验问题：

1. 聊天页的“登录成功”提示现在只是占位提示，第一条历史消息或实时消息到来时会先清空占位内容，不会再和第一条消息接在同一行。
2. BBS 页面改成左侧帖子广场 + 右侧工作区分页：
   - 默认显示“发布帖子”；
   - 只有点击左侧帖子卡片后，才启用并切换到“查看 / 回复”页面。
3. 帖子卡片改为更短的摘要卡，标题、正文和时间都会截断显示，避免必须横向/纵向拖拽才能看完。
4. 附件链接改为更稳定的自定义链接格式 `bbs-post:<id>` / `bbs-reply:<id>`，并在点击后给出下载状态提示。下载目录仍是 Qt 程序当前目录下的 `downloads/`。
