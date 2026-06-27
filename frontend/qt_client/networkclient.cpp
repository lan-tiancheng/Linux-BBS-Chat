#include "networkclient.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegExp>
#include <QTimer>

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_pending(PendingNone),
      m_downloadFile(nullptr),
      m_downloadBytesLeft(0)
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

QString NetworkClient::currentUser() const
{
    return m_currentUser;
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
        sendLine("QUIT");
        m_socket->disconnectFromHost();
    }
}

void NetworkClient::login(const QString &username, const QString &password)
{
    if (!isConnected()) {
        emit loginFailed("尚未连接服务器");
        return;
    }
    if (!startPending(PendingLogin)) {
        return;
    }
    m_loginUser = username.trimmed();
    sendLine(QString("LOGIN %1 %2").arg(cleanLineText(username), cleanLineText(password)));
}

void NetworkClient::registerUser(const QString &username, const QString &password)
{
    if (!isConnected()) {
        emit registerFailed("尚未连接服务器");
        return;
    }
    if (!startPending(PendingRegister)) {
        return;
    }
    sendLine(QString("REGISTER %1 %2").arg(cleanLineText(username), cleanLineText(password)));
}

void NetworkClient::requestOnlineUsers()
{
    if (!startPending(PendingWho)) {
        return;
    }
    sendLine("WHO");
}

void NetworkClient::sendGroupMessage(const QString &message)
{
    const QString text = cleanLineText(message).trimmed();
    if (text.isEmpty()) {
        return;
    }
    sendLine("GROUP " + text);
}

void NetworkClient::sendPrivateMessage(const QString &target, const QString &message)
{
    const QString user = cleanLineText(target).trimmed();
    const QString text = cleanLineText(message).trimmed();
    if (user.isEmpty() || text.isEmpty()) {
        emit statusMessage("私聊需要填写目标用户和消息内容");
        return;
    }
    if (!m_currentUser.isEmpty() && user == m_currentUser) {
        emit statusMessage("不能私聊自己");
        return;
    }
    if (!startPending(PendingPrivate)) {
        return;
    }
    m_privateTarget = user;
    m_privateMessage = text;
    sendLine(QString("PRIVATE %1 %2").arg(user, text));
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
        emit statusMessage("上一个请求还没有完成，请稍后再试");
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
    if (handleAsyncLine(line)) {
        return;
    }

    switch (m_pending) {
    case PendingLogin:
    case PendingRegister:
    case PendingBbsCreate:
    case PendingBbsReply:
    case PendingBbsUpload:
        handleSingleLineResult(line, m_pending);
        return;
    case PendingWho:
        if (line.startsWith("ONLINE ")) {
            const QStringList parts = line.split(' ', QString::SkipEmptyParts);
            QStringList users;
            for (int i = 2; i < parts.size(); ++i) {
                users << parts.at(i);
            }
            emit onlineUsersReceived(users);
        } else if (line.startsWith("ERR ")) {
            emit statusMessage(line.mid(4));
        }
        finishPending();
        return;
    case PendingPrivate:
        if (line.startsWith("OK")) {
            emit privateMessageReceived(m_privateTarget, m_privateMessage, false);
            if (line.contains("stored", Qt::CaseInsensitive) || line.contains("offline", Qt::CaseInsensitive)) {
                emit statusMessage("私聊已保存，对方上线后可以看到");
            } else {
                emit statusMessage("私聊已发送");
            }
        } else if (line.startsWith("ERR ")) {
            const QString error = line.mid(4);
            if (error.contains("does not exist", Qt::CaseInsensitive) || error.contains("not registered", Qt::CaseInsensitive)) {
                emit statusMessage("私聊失败：用户不存在");
            } else if (error.contains("yourself", Qt::CaseInsensitive)) {
                emit statusMessage("私聊失败：不能私聊自己");
            } else {
                emit statusMessage("私聊失败：" + error);
            }
        } else {
            emit statusMessage(line);
        }
        finishPending();
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

bool NetworkClient::handleAsyncLine(const QString &line)
{
    if (line.startsWith("MSG ")) {
        const QString body = line.mid(4);
        const int space = body.indexOf(' ');
        if (space > 0) {
            emit groupMessageReceived(body.left(space), body.mid(space + 1));
        }
        return true;
    }
    if (line.startsWith("PMSG ")) {
        const QString body = line.mid(5);
        const int space = body.indexOf(' ');
        if (space > 0) {
            emit privateMessageReceived(body.left(space), body.mid(space + 1), true);
        }
        return true;
    }
    if (line.startsWith("HMSG ")) {
        const QString data = line.mid(5);
        const int p1 = data.indexOf('|');
        const int p2 = p1 < 0 ? -1 : data.indexOf('|', p1 + 1);
        if (p1 > 0 && p2 > p1) {
            emit historyMessageReceived("GROUP", data.mid(p1 + 1, p2 - p1 - 1), QString(), data.mid(p2 + 1), data.left(p1));
        }
        return true;
    }
    if (line.startsWith("HPMSG ")) {
        const QString data = line.mid(6);
        const int p1 = data.indexOf('|');
        const int p2 = p1 < 0 ? -1 : data.indexOf('|', p1 + 1);
        const int p3 = p2 < 0 ? -1 : data.indexOf('|', p2 + 1);
        if (p1 > 0 && p2 > p1 && p3 > p2) {
            emit historyMessageReceived("PRIVATE", data.mid(p1 + 1, p2 - p1 - 1), data.mid(p2 + 1, p3 - p2 - 1), data.mid(p3 + 1), data.left(p1));
        }
        return true;
    }
    if (line == "HISTORY_BEGIN" || line == "HISTORY_END") {
        return true;
    }
    if (line.startsWith("FILE_READY ")) {
        emit statusMessage("收到聊天文件提醒：" + line.mid(11));
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
        if (ok) {
            m_currentUser = m_loginUser;
            emit loginSucceeded(m_currentUser);
            QTimer::singleShot(0, this, [this]() { sendLine("HISTORY"); });
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

    if (kind == PendingBbsCreate && ok && !m_pendingCreateAttachment.isEmpty()) {
        const int id = parseTrailingId(message);
        const QString path = m_pendingCreateAttachment;
        m_pendingCreateAttachment.clear();
        finishPending();
        emit bbsOperationFinished(QString("帖子已发布，正在上传附件..."), true);
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
        emit bbsOperationFinished(QString("回复已发布，正在上传附件..."), true);
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

QString NetworkClient::cleanLineText(QString text)
{
    text.replace('\n', ' ');
    text.replace('\r', ' ');
    return text.trimmed();
}

QString NetworkClient::cleanBbsField(QString text)
{
    text = cleanLineText(text);
    text.replace('|', ' ');
    return text;
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
        post.time = parts.mid(5).join("|");
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
        reply.time = parts.mid(5).join("|");
    } else if (parts.size() >= 5) {
        reply.id = parts.at(0).toInt();
        reply.postId = parts.at(1).toInt();
        reply.author = parts.at(2);
        reply.content = parts.at(3);
        reply.attachment = "none";
        reply.time = parts.mid(4).join("|");
    }
    return reply;
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
    if (parts.isEmpty()) {
        return 0;
    }
    bool ok = false;
    const int id = parts.last().toInt(&ok);
    return ok ? id : 0;
}
