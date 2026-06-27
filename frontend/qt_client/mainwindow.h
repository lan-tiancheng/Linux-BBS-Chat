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
class QFrame;
class QUrl;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(NetworkClient *client, const QString &username, QWidget *parent = nullptr);

private slots:
    void sendGroup();
    void sendPrivate();
    void refreshOnlineUsers();
    void setPrivateTargetFromList();
    void appendGroupMessage(const QString &sender, const QString &message);
    void appendPrivateMessage(const QString &senderOrTarget, const QString &message, bool incoming);
    void appendHistoryMessage(const QString &kind, const QString &sender, const QString &recipient,
                              const QString &message, const QString &timestamp);
    void updateOnlineUsers(const QStringList &users);
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
    NetworkClient *m_client;
    QString m_username;
    int m_currentPostId;
    bool m_chatPlaceholderVisible;
    QVector<BbsPost> m_posts;
    QVector<BbsReply> m_currentReplies;

    QTextBrowser *m_chatView;
    QListWidget *m_onlineList;
    QLineEdit *m_messageEdit;
    QLineEdit *m_privateTargetEdit;
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
    int selectedPostId() const;
    BbsPost selectedPost() const;
    QString htmlEscape(const QString &text) const;
    QString downloadsDirectory() const;
    void appendChatHtml(const QString &html);
    void renderEmptyDetail();
    static int bbsUrlId(const QUrl &url);
};

#endif
