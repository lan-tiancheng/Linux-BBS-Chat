#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <QFile>
#include <QObject>
#include <QTcpSocket>
#include <QStringList>
#include <QVector>

struct SocialUser {
    QString account;
    QString nickname;
    QString message;
};

struct GroupInfo {
    int id = 0;
    QString owner;
    QString name;
};

struct NotificationInfo {
    QString id;
    QString type;
    QString target;
    QString message;
    QString createdAt;
    bool read = false;
};

struct BbsPost {
    int id = 0;
    QString author;
    QString title;
    QString content;
    QString attachment;
    QString time;
    QString replies;
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
    QString currentAccount() const;
    QString currentNickname() const;
    QString displayName() const;

public slots:
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void login(const QString &loginName, const QString &password);
    void registerUser(const QString &account, const QString &password, const QString &nickname);
    void logout();

    void refreshSocial();
    void searchUser(const QString &loginName);
    void requestPrivateHistory(const QString &accountOrNickname);
    void requestGroupHistory(int groupId);
    void sendPrivateStart(const QString &target, const QString &message);
    void sendPrivateReply(const QString &target, const QString &message);
    void createGroup(const QString &name, const QStringList &members);
    void sendGroupMessage(int groupId, const QString &message);
    void markAllNotificationsRead();

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

    void loginSucceeded(const QString &displayName);
    void loginFailed(const QString &message);
    void registerSucceeded(const QString &message);
    void registerFailed(const QString &message);

    void searchUserReceived(const SocialUser &user);
    void friendsReceived(const QVector<SocialUser> &friends);
    void requestsReceived(const QVector<SocialUser> &requests);
    void sentRequestsReceived(const QVector<SocialUser> &requests);
    void groupsReceived(const QVector<GroupInfo> &groups);
    void notificationsReceived(const QVector<NotificationInfo> &notifications);

    void groupMessageReceived(int groupId, const QString &sender, const QString &message);
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
        PendingPrivate,
        PendingGroupCreate,
        PendingGroupSend,
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
    QString m_currentAccount;
    QString m_currentNickname;
    QString m_loginName;
    QString m_privateTarget;
    QString m_privateMessage;
    int m_groupTargetId;
    QString m_groupMessage;

    QString m_pendingCreateAttachment;
    QString m_pendingReplyAttachment;

    QFile *m_downloadFile;
    qint64 m_downloadBytesLeft;
    QString m_downloadPath;

    bool m_collectingFriends;
    bool m_collectingRequests;
    bool m_collectingSentRequests;
    bool m_collectingGroups;
    bool m_collectingNotifications;
    QVector<SocialUser> m_friends;
    QVector<SocialUser> m_requests;
    QVector<SocialUser> m_sentRequests;
    QVector<GroupInfo> m_groups;
    QVector<NotificationInfo> m_notifications;

    bool sendLine(const QString &line);
    bool startPending(PendingKind kind);
    void finishPending();
    void processBuffer();
    void handleLine(const QString &line);
    bool handleCollectionLine(const QString &line);
    bool handleAsyncLine(const QString &line);
    void handleSingleLineResult(const QString &line, PendingKind kind);
    void finishBbsList();
    void finishBbsView();
    bool startBbsUpload(const QString &command, int id, const QString &localPath);
    void startDownloadPayload(const QString &filename, qint64 size, const QString &directory);
    void resetCollections();

    static QString cleanLineText(QString text);
    static QString cleanBbsField(QString text);
    static BbsPost parsePostLine(const QString &line);
    static BbsReply parseReplyLine(const QString &line);
    static SocialUser parseSocialUser(const QString &line);
    static GroupInfo parseGroupLine(const QString &line);
    static NotificationInfo parseNotificationLine(const QString &line);
    static QString fileBaseName(const QString &path);
    static QString uniquePath(const QString &directory, const QString &filename);
    static int parseTrailingId(const QString &message);
};

#endif
