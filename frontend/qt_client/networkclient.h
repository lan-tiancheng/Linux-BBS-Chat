#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QVector>

struct BbsPost {
    int id = 0;
    QString author;
    QString title;
    QString content;
    QString attachment;
    QString time;
};

struct BbsReply {
    int id = 0;
    int postId = 0;
    QString author;
    QString content;
    QString attachment;
    QString time;
};

class NetworkClient : public QObject
{
    Q_OBJECT
public:
    explicit NetworkClient(QObject *parent = nullptr);
    ~NetworkClient();

    bool isConnected() const;
    QString currentUser() const;

public slots:
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void login(const QString &username, const QString &password);
    void registerUser(const QString &username, const QString &password);
    void requestOnlineUsers();
    void sendGroupMessage(const QString &message);
    void sendPrivateMessage(const QString &target, const QString &message);

    void listPosts();
    void viewPost(int postId);
    void createPost(const QString &title, const QString &content, const QString &attachmentPath = QString());
    void replyPost(int postId, const QString &content, const QString &attachmentPath = QString());
    void uploadBbsPostAttachment(int postId, const QString &localPath);
    void uploadBbsReplyAttachment(int replyId, const QString &localPath);
    void downloadBbsPostAttachment(int postId, const QString &saveDirectory = QString());
    void downloadBbsReplyAttachment(int replyId, const QString &saveDirectory = QString());

signals:
    void connected();
    void disconnected();
    void errorText(const QString &message);
    void statusMessage(const QString &message);

    void loginSucceeded(const QString &username);
    void loginFailed(const QString &message);
    void registerSucceeded(const QString &message);
    void registerFailed(const QString &message);

    void onlineUsersReceived(const QStringList &users);
    void groupMessageReceived(const QString &sender, const QString &message);
    void privateMessageReceived(const QString &senderOrTarget, const QString &message, bool incoming);
    void historyMessageReceived(const QString &kind, const QString &sender, const QString &recipient,
                                const QString &message, const QString &timestamp);

    void bbsPostsReceived(const QVector<BbsPost> &posts);
    void bbsPostDetailReceived(const BbsPost &post, const QVector<BbsReply> &replies);
    void bbsOperationFinished(const QString &message, bool ok);
    void bbsDownloadFinished(const QString &path);

private slots:
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);

private:
    enum PendingKind {
        PendingNone,
        PendingLogin,
        PendingRegister,
        PendingWho,
        PendingPrivate,
        PendingBbsList,
        PendingBbsView,
        PendingBbsCreate,
        PendingBbsReply,
        PendingBbsUpload,
        PendingBbsDownload
    };

    QTcpSocket *m_socket;
    QByteArray m_buffer;
    PendingKind m_pending;
    QVector<QString> m_pendingLines;
    QString m_currentUser;
    QString m_loginUser;
    QString m_privateTarget;
    QString m_privateMessage;

    QString m_pendingCreateAttachment;
    QString m_pendingReplyAttachment;

    QFile *m_downloadFile;
    qint64 m_downloadBytesLeft;
    QString m_downloadPath;

    bool sendLine(const QString &line);
    bool startPending(PendingKind kind);
    void finishPending();
    void processBuffer();
    void handleLine(const QString &line);
    bool handleAsyncLine(const QString &line);

    void handleSingleLineResult(const QString &line, PendingKind kind);
    void finishBbsList();
    void finishBbsView();
    bool startBbsUpload(const QString &command, int id, const QString &localPath);
    void startDownloadPayload(const QString &filename, qint64 size, const QString &directory);

    static QString cleanLineText(QString text);
    static QString cleanBbsField(QString text);
    static BbsPost parsePostLine(const QString &line);
    static BbsReply parseReplyLine(const QString &line);
    static QString fileBaseName(const QString &path);
    static QString uniquePath(const QString &directory, const QString &filename);
    static int parseTrailingId(const QString &message);
};

#endif
