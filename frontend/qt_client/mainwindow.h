#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "networkclient.h"

#include <QMainWindow>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextBrowser;
class QTextEdit;
class QTabWidget;
class QUrl;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(NetworkClient *client, const QString &displayName, QWidget *parent = nullptr);

private slots:
    void refreshSocial();
    void searchUser();
    void selectConversation(QListWidgetItem *item);
    void sendCurrentMessage();
    void createGroup();
    void fillFriends(const QVector<SocialUser> &friends);
    void fillRequests(const QVector<SocialUser> &requests);
    void fillSentRequests(const QVector<SocialUser> &requests);
    void fillGroups(const QVector<GroupInfo> &groups);
    void fillNotifications(const QVector<NotificationInfo> &notifications);
    void showSearchUser(const SocialUser &user);
    void appendPrivateMessage(const QString &senderOrTarget, const QString &message, bool incoming);
    void appendGroupMessage(int groupId, const QString &sender, const QString &message);
    void appendHistoryMessage(const QString &kind, const QString &sender, const QString &recipient,
                              const QString &message, const QString &timestamp);
    void showStatus(const QString &message);

    void refreshPosts();
    void createPost();
    void viewSelectedPost();
    void replyToPost();
    void choosePostAttachment();
    void chooseReplyAttachment();
    void clearPostAttachment();
    void clearReplyAttachment();
    void openBbsLink(const QUrl &url);
    void fillPosts(const QVector<BbsPost> &posts);
    void showPostDetail(const BbsPost &post, const QVector<BbsReply> &replies);
    void onBbsOperation(const QString &message, bool ok);
    void onBbsDownloadFinished(const QString &path);

private:
    enum ConversationKind {
        NoConversation,
        PrivateConversation,
        PrivateReplyConversation,
        GroupConversation
    };

    NetworkClient *m_client;
    QString m_displayName;
    ConversationKind m_conversationKind;
    QString m_currentTarget;
    QString m_currentTitle;
    int m_currentGroupId;
    int m_currentPostId;
    QVector<SocialUser> m_friends;
    QVector<SocialUser> m_requests;
    QVector<SocialUser> m_sentRequests;
    QVector<GroupInfo> m_groups;
    QVector<BbsPost> m_posts;
    QVector<BbsReply> m_currentReplies;

    QLabel *m_conversationTitle;
    QTextBrowser *m_chatView;
    QListWidget *m_conversationList;
    QListWidget *m_groupFriendList;
    QLineEdit *m_userSearchEdit;
    QLineEdit *m_groupNameEdit;
    QLineEdit *m_messageEdit;
    QLabel *m_statusLabel;

    QListWidget *m_postsList;
    QTextBrowser *m_postDetail;
    QLineEdit *m_titleEdit;
    QTextEdit *m_contentEdit;
    QTextEdit *m_replyEdit;
    QLineEdit *m_postAttachmentEdit;
    QLineEdit *m_replyAttachmentEdit;
    QTabWidget *m_bbsWorkTabs;
    int m_bbsDetailTabIndex;

    QWidget *buildChatPage();
    QWidget *buildBbsPage();
    QWidget *createPostCardWidget(const BbsPost &post) const;
    void addConversationItem(const QString &section, const QString &title, const QString &subtitle,
                             ConversationKind kind, const QString &target, int groupId = 0);
    void rebuildConversationList();
    void rebuildGroupFriendList();
    void clearChatForSelection(const QString &title, const QString &hint);
    void appendChatHtml(const QString &html);
    QString peerName(const QString &account) const;
    int selectedPostId() const;
    QString htmlEscape(const QString &text) const;
    QString downloadsDirectory() const;
    void renderEmptyDetail();
    static int bbsUrlId(const QUrl &url);
};

#endif
