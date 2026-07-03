#include "networkclient.h"

#include <QDir>
#include <QFileInfo>
#include <QRegExp>
#include <QTimer>

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_pending(PendingNone),
      m_groupTargetId(0),
      m_downloadFile(nullptr),
      m_downloadBytesLeft(0),
      m_collectingFriends(false),
      m_collectingRequests(false),
      m_collectingSentRequests(false),
      m_collectingGroups(false),
      m_collectingNotifications(false)
{
    connect(m_socket, &QTcpSocket::connected, this, &NetworkClient::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetworkClient::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetworkClient::onReadyRead);
    connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onSocketError(QAbstractSocket::SocketError)));
}

NetworkClient::~NetworkClient()
{
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
    }
}

bool NetworkClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString NetworkClient::currentAccount() const
{
    return m_currentAccount;
}

QString NetworkClient::currentNickname() const
{
    return m_currentNickname;
}

QString NetworkClient::displayName() const
{
    if (m_currentNickname.isEmpty()) {
        return m_currentAccount;
    }
    return QString("%1 (%2)").arg(m_currentNickname, m_currentAccount);
}

void NetworkClient::connectToServer(const QString &host, quint16 port)
{
    if (isConnected()) {
        emit connected();
        return;
    }
    m_socket->connectToHost(host, port);
}

void NetworkClient::disconnectFromServer()
{
    if (isConnected()) {
        logout();
        m_socket->disconnectFromHost();
    }
}

void NetworkClient::login(const QString &loginName, const QString &password)
{
    if (!isConnected()) {
        emit loginFailed("尚未连接服务器");
        return;
    }
    if (!startPending(PendingLogin)) {
        return;
    }
    m_loginName = loginName.trimmed();
    sendLine(QString("LOGIN %1 %2").arg(cleanLineText(loginName), cleanLineText(password)));
}

void NetworkClient::registerUser(const QString &account, const QString &password, const QString &nickname)
{
    if (!isConnected()) {
        emit registerFailed("尚未连接服务器");
        return;
    }
    if (!startPending(PendingRegister)) {
        return;
    }
    sendLine(QString("REGISTER %1 %2 %3")
             .arg(cleanLineText(account), cleanLineText(password), cleanLineText(nickname)));
}

void NetworkClient::logout()
{
    if (isConnected()) {
        sendLine("LOGOUT");
    }
    m_currentAccount.clear();
    m_currentNickname.clear();
}

void NetworkClient::refreshSocial()
{
    sendLine("FRIENDS");
    sendLine("REQUESTS");
    sendLine("SENT_REQUESTS");
    sendLine("GROUPS");
    sendLine("NOTIFICATIONS");
}

void NetworkClient::searchUser(const QString &loginName)
{
    const QString value = cleanLineText(loginName).trimmed();
    if (value.isEmpty()) {
        emit statusMessage("请先输入账号或昵称");
        return;
    }
    sendLine(QString("SEARCH_USER %1").arg(value));
}

void NetworkClient::requestPrivateHistory(const QString &accountOrNickname)
{
    const QString value = cleanLineText(accountOrNickname).trimmed();
    if (!value.isEmpty()) {
        sendLine(QString("HISTORY_PRIVATE %1").arg(value));
    }
}

void NetworkClient::requestGroupHistory(int groupId)
{
    if (groupId > 0) {
        sendLine(QString("HISTORY_GROUP %1").arg(groupId));
    }
}

void NetworkClient::sendPrivateStart(const QString &target, const QString &message)
{
    const QString user = cleanLineText(target).trimmed();
    const QString text = cleanLineText(message).trimmed();
    if (user.isEmpty() || text.isEmpty()) {
        emit statusMessage("私聊需要目标用户和消息内容");
        return;
    }
    if (user == m_currentAccount || user == m_currentNickname) {
        emit statusMessage("不能给自己发私聊");
        return;
    }
    if (!startPending(PendingPrivate)) {
        return;
    }
    m_privateTarget = user;
    m_privateMessage = text;
    sendLine(QString("PRIVATE_START %1 %2").arg(user, text));
}

void NetworkClient::sendPrivateReply(const QString &target, const QString &message)
{
    const QString user = cleanLineText(target).trimmed();
    const QString text = cleanLineText(message).trimmed();
    if (user.isEmpty() || text.isEmpty()) {
        emit statusMessage("回复私信需要目标用户和消息内容");
        return;
    }
    if (!startPending(PendingPrivate)) {
        return;
    }
    m_privateTarget = user;
    m_privateMessage = text;
    sendLine(QString("PRIVATE_REPLY %1 %2").arg(user, text));
}

void NetworkClient::createGroup(const QString &name, const QStringList &members)
{
    const QString groupName = cleanLineText(name).trimmed();
    QStringList cleanMembers;
    for (const QString &member : members) {
        const QString value = cleanLineText(member).trimmed();
        if (!value.isEmpty()) {
            cleanMembers << value;
        }
    }
    if (groupName.isEmpty() || cleanMembers.isEmpty()) {
        emit statusMessage("创建群聊需要群名和至少一位好友");
        return;
    }
    if (!startPending(PendingGroupCreate)) {
        return;
    }
    sendLine(QString("GROUP_CREATE %1 %2").arg(groupName, cleanMembers.join(",")));
}

void NetworkClient::sendGroupMessage(int groupId, const QString &message)
{
    const QString text = cleanLineText(message).trimmed();
    if (groupId <= 0 || text.isEmpty()) {
        emit statusMessage("请先选择群聊并输入消息");
        return;
    }
    if (!startPending(PendingGroupSend)) {
        return;
    }
    m_groupTargetId = groupId;
    m_groupMessage = text;
    sendLine(QString("GROUP_SEND %1 %2").arg(groupId).arg(text));
}

void NetworkClient::markAllNotificationsRead()
{
    sendLine("MARK_READ_ALL");
}

void NetworkClient::listPosts()
{
    if (!startPending(PendingBbsList)) {
        return;
    }
    sendLine("BBS_LIST");
}

void NetworkClient::viewPost(int postId)
{
    if (postId <= 0) {
        emit bbsOperationFinished("请先选择一个帖子", false);
        return;
    }
    if (!startPending(PendingBbsView)) {
        return;
    }
    sendLine(QString("BBS_VIEW %1").arg(postId));
}

void NetworkClient::createPost(const QString &title, const QString &content, const QString &attachmentPath)
{
    const QString cleanTitle = cleanBbsField(title).trimmed();
    const QString cleanContent = cleanBbsField(content).trimmed();
    if (cleanTitle.isEmpty() || cleanContent.isEmpty()) {
        emit bbsOperationFinished("标题和内容不能为空", false);
        return;
    }
    if (!startPending(PendingBbsCreate)) {
        return;
    }
    m_pendingCreateAttachment = attachmentPath.trimmed();
    sendLine(QString("BBS_CREATE %1|%2").arg(cleanTitle, cleanContent));
}

void NetworkClient::replyPost(int postId, const QString &content, const QString &attachmentPath)
{
    const QString cleanContent = cleanBbsField(content).trimmed();
    if (postId <= 0 || cleanContent.isEmpty()) {
        emit bbsOperationFinished("请选择帖子并填写回复内容", false);
        return;
    }
    if (!startPending(PendingBbsReply)) {
        return;
    }
    m_pendingReplyAttachment = attachmentPath.trimmed();
    sendLine(QString("BBS_REPLY %1|%2").arg(postId).arg(cleanContent));
}

void NetworkClient::uploadBbsPostAttachment(int postId, const QString &localPath)
{
    startBbsUpload("BBS_UPLOAD_POST", postId, localPath);
}

void NetworkClient::uploadBbsReplyAttachment(int replyId, const QString &localPath)
{
    startBbsUpload("BBS_UPLOAD_REPLY", replyId, localPath);
}

void NetworkClient::downloadBbsPostAttachment(int postId, const QString &saveDirectory)
{
    if (postId <= 0) {
        emit bbsOperationFinished("请先选择一个帖子", false);
        return;
    }
    if (!startPending(PendingBbsDownload)) {
        return;
    }
    m_downloadPath = saveDirectory;
    sendLine(QString("BBS_DOWNLOAD_POST %1").arg(postId));
}

void NetworkClient::downloadBbsReplyAttachment(int replyId, const QString &saveDirectory)
{
    if (replyId <= 0) {
        emit bbsOperationFinished("回复编号无效", false);
        return;
    }
    if (!startPending(PendingBbsDownload)) {
        return;
    }
    m_downloadPath = saveDirectory;
    sendLine(QString("BBS_DOWNLOAD_REPLY %1").arg(replyId));
}

void NetworkClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void NetworkClient::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    emit errorText(m_socket->errorString());
}

bool NetworkClient::sendLine(const QString &line)
{
    if (!isConnected()) {
        emit statusMessage("未连接服务器");
        return false;
    }
    QByteArray data = line.toUtf8();
    data.append('\n');
    return m_socket->write(data) == data.size();
}

bool NetworkClient::startPending(PendingKind kind)
{
    if (m_pending != PendingNone) {
        emit statusMessage("上一项请求还没有完成，请稍后再试");
        return false;
    }
    m_pending = kind;
    m_pendingLines.clear();
    return true;
}

void NetworkClient::finishPending()
{
    m_pending = PendingNone;
    m_pendingLines.clear();
}

void NetworkClient::processBuffer()
{
    while (true) {
        if (m_downloadBytesLeft > 0 && m_downloadFile) {
            if (m_buffer.isEmpty()) {
                return;
            }
            const qint64 take = qMin<qint64>(m_downloadBytesLeft, m_buffer.size());
            m_downloadFile->write(m_buffer.constData(), take);
            m_buffer.remove(0, int(take));
            m_downloadBytesLeft -= take;
            if (m_downloadBytesLeft == 0) {
                const QString saved = m_downloadFile->fileName();
                m_downloadFile->close();
                delete m_downloadFile;
                m_downloadFile = nullptr;
                emit bbsDownloadFinished(saved);
            }
            continue;
        }

        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            return;
        }
        QByteArray raw = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (raw.endsWith('\r')) {
            raw.chop(1);
        }
        handleLine(QString::fromUtf8(raw));
    }
}

void NetworkClient::handleLine(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }
    if (handleCollectionLine(line) || handleAsyncLine(line)) {
        return;
    }

    switch (m_pending) {
    case PendingLogin:
    case PendingRegister:
    case PendingPrivate:
    case PendingGroupCreate:
    case PendingGroupSend:
    case PendingBbsCreate:
    case PendingBbsReply:
    case PendingBbsUpload:
        handleSingleLineResult(line, m_pending);
        return;
    case PendingBbsList:
        m_pendingLines << line;
        if (line == "BBS_POSTS_END" || line.startsWith("ERR ")) {
            finishBbsList();
        }
        return;
    case PendingBbsView:
        m_pendingLines << line;
        if (line == "BBS_POST_END" || line.startsWith("ERR ")) {
            finishBbsView();
        }
        return;
    case PendingBbsDownload:
        if (line.startsWith("BBS_FILE ")) {
            const QStringList parts = line.split(' ', QString::SkipEmptyParts);
            if (parts.size() == 3) {
                bool ok = false;
                const qint64 size = parts.at(2).toLongLong(&ok);
                if (ok && size >= 0) {
                    startDownloadPayload(parts.at(1), size, m_downloadPath);
                } else {
                    emit bbsOperationFinished("服务器返回的附件大小不正确", false);
                }
            } else {
                emit bbsOperationFinished("服务器返回的附件格式不正确", false);
            }
        } else if (line.startsWith("ERR ")) {
            emit bbsOperationFinished(line.mid(4), false);
        } else {
            emit bbsOperationFinished(line, false);
        }
        finishPending();
        return;
    case PendingNone:
        if (line.startsWith("OK ")) {
            emit statusMessage(line.mid(3));
        } else if (line.startsWith("ERR ")) {
            emit statusMessage(line.mid(4));
        } else {
            emit statusMessage(line);
        }
        return;
    }
}

bool NetworkClient::handleCollectionLine(const QString &line)
{
    if (line == "FRIENDS_BEGIN") {
        m_collectingFriends = true;
        m_friends.clear();
        return true;
    }
    if (line == "FRIENDS_END") {
        m_collectingFriends = false;
        emit friendsReceived(m_friends);
        return true;
    }
    if (m_collectingFriends && line.startsWith("FRIEND ")) {
        m_friends << parseSocialUser(line.mid(7));
        return true;
    }

    if (line == "REQUESTS_BEGIN") {
        m_collectingRequests = true;
        m_requests.clear();
        return true;
    }
    if (line == "REQUESTS_END") {
        m_collectingRequests = false;
        emit requestsReceived(m_requests);
        return true;
    }
    if (m_collectingRequests && line.startsWith("REQUEST ")) {
        m_requests << parseSocialUser(line.mid(8));
        return true;
    }

    if (line == "SENT_REQUESTS_BEGIN") {
        m_collectingSentRequests = true;
        m_sentRequests.clear();
        return true;
    }
    if (line == "SENT_REQUESTS_END") {
        m_collectingSentRequests = false;
        emit sentRequestsReceived(m_sentRequests);
        return true;
    }
    if (m_collectingSentRequests && line.startsWith("SENT_REQUEST ")) {
        m_sentRequests << parseSocialUser(line.mid(13));
        return true;
    }

    if (line == "GROUPS_BEGIN") {
        m_collectingGroups = true;
        m_groups.clear();
        return true;
    }
    if (line == "GROUPS_END") {
        m_collectingGroups = false;
        emit groupsReceived(m_groups);
        return true;
    }
    if (m_collectingGroups && line.startsWith("GROUP_ITEM ")) {
        m_groups << parseGroupLine(line.mid(11));
        return true;
    }

    if (line == "NOTIFICATIONS_BEGIN") {
        m_collectingNotifications = true;
        m_notifications.clear();
        return true;
    }
    if (line == "NOTIFICATIONS_END") {
        m_collectingNotifications = false;
        emit notificationsReceived(m_notifications);
        return true;
    }
    if (m_collectingNotifications && line.startsWith("NOTIFICATION ")) {
        m_notifications << parseNotificationLine(line.mid(13));
        return true;
    }

    return false;
}

bool NetworkClient::handleAsyncLine(const QString &line)
{
    if (line.startsWith("USER ")) {
        emit searchUserReceived(parseSocialUser(line.mid(5)));
        return true;
    }
    if (line.startsWith("PMSG ")) {
        const QString body = line.mid(5);
        const int space = body.indexOf(' ');
        if (space > 0) {
            emit privateMessageReceived(body.left(space), body.mid(space + 1), true);
            refreshSocial();
        }
        return true;
    }
    if (line.startsWith("GMSG ")) {
        const QString body = line.mid(5);
        const int first = body.indexOf(' ');
        const int second = first < 0 ? -1 : body.indexOf(' ', first + 1);
        if (first > 0 && second > first) {
            const QString sender = body.mid(first + 1, second - first - 1);
            if (sender != m_currentAccount) {
                emit groupMessageReceived(body.left(first).toInt(), sender, body.mid(second + 1));
            }
        }
        return true;
    }
    if (line.startsWith("HPMSG ")) {
        const QString data = line.mid(6);
        const QStringList parts = data.split('|');
        if (parts.size() >= 4) {
            emit historyMessageReceived("PRIVATE", parts.at(1), parts.at(2),
                                        parts.mid(3).join("|"), parts.at(0));
        }
        return true;
    }
    if (line.startsWith("HGMSG ")) {
        const QString data = line.mid(6);
        const QStringList parts = data.split('|');
        if (parts.size() >= 4) {
            emit historyMessageReceived("GROUP", parts.at(2), parts.at(1),
                                        parts.mid(3).join("|"), parts.at(0));
        }
        return true;
    }
    if (line == "PRIVATE_HISTORY_BEGIN" || line == "PRIVATE_HISTORY_END" ||
        line == "GROUP_HISTORY_BEGIN" || line == "GROUP_HISTORY_END") {
        return true;
    }
    if (line.startsWith("EVENT GROUP_INVITED ")) {
        emit statusMessage("收到群聊邀请：" + line.mid(20));
        refreshSocial();
        return true;
    }
    if (line.startsWith("EVENT BBS_POST_CREATED ") ||
        line.startsWith("EVENT BBS_REPLY_CREATED ")) {
        emit statusMessage("论坛有新动态：" + line.mid(6));
        listPosts();
        refreshSocial();
        return true;
    }
    if (line.startsWith("FILE_READY ")) {
        emit statusMessage("收到聊天文件提醒：" + line.mid(11));
        refreshSocial();
        return true;
    }
    return false;
}

void NetworkClient::handleSingleLineResult(const QString &line, PendingKind kind)
{
    const bool ok = line.startsWith("OK");
    const QString message = ok ? line.mid(line.startsWith("OK ") ? 3 : 2).trimmed()
                               : line.mid(line.startsWith("ERR ") ? 4 : 0).trimmed();

    if (kind == PendingLogin) {
        finishPending();
        if (ok && line.startsWith("OK logged in ")) {
            const QString identity = line.mid(13).trimmed();
            const QStringList parts = identity.split('|');
            m_currentAccount = parts.value(0);
            m_currentNickname = parts.value(1, m_currentAccount);
            emit loginSucceeded(displayName());
            QTimer::singleShot(0, this, [this]() {
                refreshSocial();
                listPosts();
            });
        } else if (ok) {
            m_currentAccount = m_loginName;
            m_currentNickname = m_loginName;
            emit loginSucceeded(displayName());
            QTimer::singleShot(0, this, [this]() { refreshSocial(); });
        } else {
            emit loginFailed(message);
        }
        return;
    }
    if (kind == PendingRegister) {
        finishPending();
        if (ok) {
            emit registerSucceeded(message.isEmpty() ? "注册成功" : message);
        } else {
            emit registerFailed(message);
        }
        return;
    }
    if (kind == PendingPrivate) {
        finishPending();
        if (ok) {
            emit statusMessage(message.isEmpty() ? "私聊已发送" : message);
            refreshSocial();
        } else {
            emit statusMessage("私聊失败：" + message);
        }
        return;
    }
    if (kind == PendingGroupCreate) {
        finishPending();
        emit statusMessage(ok ? "群聊创建成功" : "群聊创建失败：" + message);
        refreshSocial();
        return;
    }
    if (kind == PendingGroupSend) {
        finishPending();
        if (ok) {
            emit statusMessage("群消息已发送");
        } else {
            emit statusMessage("群消息发送失败：" + message);
        }
        return;
    }

    if (kind == PendingBbsCreate && ok && !m_pendingCreateAttachment.isEmpty()) {
        const int id = parseTrailingId(message);
        const QString path = m_pendingCreateAttachment;
        m_pendingCreateAttachment.clear();
        finishPending();
        emit bbsOperationFinished("帖子已发布，正在上传附件...", true);
        if (id > 0) {
            QTimer::singleShot(0, this, [this, id, path]() { uploadBbsPostAttachment(id, path); });
        } else {
            emit bbsOperationFinished("帖子已发布，但没有拿到帖子编号，附件未上传", false);
        }
        return;
    }
    if (kind == PendingBbsReply && ok && !m_pendingReplyAttachment.isEmpty()) {
        const int id = parseTrailingId(message);
        const QString path = m_pendingReplyAttachment;
        m_pendingReplyAttachment.clear();
        finishPending();
        emit bbsOperationFinished("回复已发布，正在上传附件...", true);
        if (id > 0) {
            QTimer::singleShot(0, this, [this, id, path]() { uploadBbsReplyAttachment(id, path); });
        } else {
            emit bbsOperationFinished("回复已发布，但没有拿到回复编号，附件未上传", false);
        }
        return;
    }

    if (kind == PendingBbsCreate) {
        m_pendingCreateAttachment.clear();
    }
    if (kind == PendingBbsReply) {
        m_pendingReplyAttachment.clear();
    }
    finishPending();
    emit bbsOperationFinished(message, ok);
}

void NetworkClient::finishBbsList()
{
    QVector<BbsPost> posts;
    for (const QString &line : qAsConst(m_pendingLines)) {
        if (line.startsWith("ERR ")) {
            const QString error = line.mid(4);
            finishPending();
            emit bbsOperationFinished(error, false);
            return;
        }
        if (line.startsWith("BBS_POST ")) {
            posts << parsePostLine(line.mid(9));
        }
    }
    finishPending();
    emit bbsPostsReceived(posts);
}

void NetworkClient::finishBbsView()
{
    BbsPost post;
    QVector<BbsReply> replies;
    bool found = false;

    for (const QString &line : qAsConst(m_pendingLines)) {
        if (line.startsWith("ERR ")) {
            const QString error = line.mid(4);
            finishPending();
            emit bbsOperationFinished(error, false);
            return;
        }
        if (line == "BBS_NOT_FOUND") {
            finishPending();
            emit bbsOperationFinished("帖子不存在", false);
            return;
        }
        if (line.startsWith("BBS_POST ")) {
            post = parsePostLine(line.mid(9));
            found = true;
        } else if (line.startsWith("BBS_REPLY ")) {
            replies << parseReplyLine(line.mid(10));
        }
    }

    finishPending();
    if (found) {
        emit bbsPostDetailReceived(post, replies);
    } else {
        emit bbsOperationFinished("帖子不存在", false);
    }
}

bool NetworkClient::startBbsUpload(const QString &command, int id, const QString &localPath)
{
    QFile file(localPath);
    const QString filename = fileBaseName(localPath);

    if (id <= 0 || filename.isEmpty()) {
        emit bbsOperationFinished("请选择有效对象和本地文件", false);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        emit bbsOperationFinished("无法打开本地文件", false);
        return false;
    }
    if (file.size() > 16LL * 1024LL * 1024LL) {
        emit bbsOperationFinished("文件不能超过 16 MiB", false);
        return false;
    }
    if (!startPending(PendingBbsUpload)) {
        return false;
    }
    sendLine(QString("%1 %2 %3 %4").arg(command).arg(id).arg(filename).arg(file.size()));
    while (!file.atEnd()) {
        m_socket->write(file.read(8192));
    }
    m_socket->flush();
    return true;
}

void NetworkClient::startDownloadPayload(const QString &filename, qint64 size, const QString &directory)
{
    QDir dir(directory.isEmpty() ? QDir::currentPath() + "/downloads" : directory);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    const QString path = uniquePath(dir.absolutePath(), filename);
    m_downloadFile = new QFile(path, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit bbsOperationFinished("无法创建下载文件", false);
        return;
    }
    m_downloadBytesLeft = size;
    if (size == 0) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit bbsDownloadFinished(path);
    }
}

void NetworkClient::resetCollections()
{
    m_collectingFriends = false;
    m_collectingRequests = false;
    m_collectingSentRequests = false;
    m_collectingGroups = false;
    m_collectingNotifications = false;
}

QString NetworkClient::cleanLineText(QString text)
{
    text.replace('\n', ' ');
    text.replace('\r', ' ');
    text.replace('|', ' ');
    return text.trimmed();
}

QString NetworkClient::cleanBbsField(QString text)
{
    text.replace('\n', ' ');
    text.replace('\r', ' ');
    text.replace('|', ' ');
    return text.trimmed();
}

BbsPost NetworkClient::parsePostLine(const QString &line)
{
    const QStringList parts = line.split('|');
    BbsPost post;
    if (parts.size() >= 6) {
        post.id = parts.at(0).toInt();
        post.author = parts.at(1);
        post.title = parts.at(2);
        post.content = parts.at(3);
        post.attachment = parts.at(4);
        post.time = parts.at(5);
        post.replies = parts.value(6);
    }
    return post;
}

BbsReply NetworkClient::parseReplyLine(const QString &line)
{
    const QStringList parts = line.split('|');
    BbsReply reply;
    if (parts.size() >= 6) {
        reply.id = parts.at(0).toInt();
        reply.postId = parts.at(1).toInt();
        reply.author = parts.at(2);
        reply.content = parts.at(3);
        reply.attachment = parts.at(4);
        reply.time = parts.at(5);
    }
    return reply;
}

SocialUser NetworkClient::parseSocialUser(const QString &line)
{
    const QStringList parts = line.split('|');
    SocialUser user;
    user.account = parts.value(0);
    user.nickname = parts.value(1, user.account);
    user.message = parts.mid(2).join("|");
    return user;
}

GroupInfo NetworkClient::parseGroupLine(const QString &line)
{
    const QStringList parts = line.split('|');
    GroupInfo group;
    group.id = parts.value(0).toInt();
    group.owner = parts.value(1);
    group.name = parts.value(2);
    return group;
}

NotificationInfo NetworkClient::parseNotificationLine(const QString &line)
{
    const QStringList parts = line.split('|');
    NotificationInfo item;
    item.id = parts.value(0);
    item.type = parts.value(1);
    item.target = parts.value(2);
    item.message = parts.value(3);
    item.createdAt = parts.value(4);
    item.read = parts.value(5) == "1";
    return item;
}

QString NetworkClient::fileBaseName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    name.replace(QRegExp("[^A-Za-z0-9._-]"), "_");
    return name;
}

QString NetworkClient::uniquePath(const QString &directory, const QString &filename)
{
    QDir dir(directory);
    QString path = dir.filePath(filename);
    if (!QFileInfo::exists(path)) {
        return path;
    }
    const QFileInfo info(filename);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();
    for (int i = 1; i < 10000; ++i) {
        path = dir.filePath(QString("%1_%2%3").arg(base).arg(i).arg(suffix));
        if (!QFileInfo::exists(path)) {
            return path;
        }
    }
    return dir.filePath("download_" + filename);
}

int NetworkClient::parseTrailingId(const QString &message)
{
    const QStringList parts = message.split(' ', QString::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const int id = part.toInt(&ok);
        if (ok && id > 0) {
            return id;
        }
    }
    return 0;
}
