#include "mainwindow.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractItemView>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QSize>

MainWindow::MainWindow(NetworkClient *client, const QString &displayName, QWidget *parent)
    : QMainWindow(parent),
      m_client(client),
      m_displayName(displayName),
      m_conversationKind(NoConversation),
      m_currentGroupId(0),
      m_currentPostId(0),
      m_bbsWorkTabs(nullptr),
      m_bbsDetailTabIndex(-1)
{
    setWindowTitle("BBS Chat - " + displayName);
    resize(1180, 780);

    QTabWidget *tabs = new QTabWidget;
    tabs->setObjectName("MainTabs");
    tabs->addTab(buildChatPage(), "私信 / 群聊");
    tabs->addTab(buildBbsPage(), "论坛 BBS");
    setCentralWidget(tabs);

    m_statusLabel = new QLabel("已登录：" + displayName);
    statusBar()->addWidget(m_statusLabel, 1);

    connect(m_client, &NetworkClient::statusMessage, this, &MainWindow::showStatus);
    connect(m_client, &NetworkClient::errorText, this, &MainWindow::showStatus);
    connect(m_client, &NetworkClient::friendsReceived, this, &MainWindow::fillFriends);
    connect(m_client, &NetworkClient::requestsReceived, this, &MainWindow::fillRequests);
    connect(m_client, &NetworkClient::sentRequestsReceived, this, &MainWindow::fillSentRequests);
    connect(m_client, &NetworkClient::groupsReceived, this, &MainWindow::fillGroups);
    connect(m_client, &NetworkClient::notificationsReceived, this, &MainWindow::fillNotifications);
    connect(m_client, &NetworkClient::searchUserReceived, this, &MainWindow::showSearchUser);
    connect(m_client, &NetworkClient::privateMessageReceived, this, &MainWindow::appendPrivateMessage);
    connect(m_client, &NetworkClient::groupMessageReceived, this, &MainWindow::appendGroupMessage);
    connect(m_client, &NetworkClient::historyMessageReceived, this, &MainWindow::appendHistoryMessage);
    connect(m_client, &NetworkClient::bbsPostsReceived, this, &MainWindow::fillPosts);
    connect(m_client, &NetworkClient::bbsPostDetailReceived, this, &MainWindow::showPostDetail);
    connect(m_client, &NetworkClient::bbsOperationFinished, this, &MainWindow::onBbsOperation);
    connect(m_client, &NetworkClient::bbsDownloadFinished, this, &MainWindow::onBbsDownloadFinished);

    refreshSocial();
    refreshPosts();
}

QWidget *MainWindow::buildChatPage()
{
    QWidget *page = new QWidget;
    page->setObjectName("Page");

    m_userSearchEdit = new QLineEdit;
    m_userSearchEdit->setPlaceholderText("搜索 9 位账号或昵称");
    QPushButton *searchButton = new QPushButton("搜索");
    QPushButton *refreshButton = new QPushButton("刷新");
    refreshButton->setObjectName("GhostButton");
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::searchUser);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshSocial);

    QHBoxLayout *searchLayout = new QHBoxLayout;
    searchLayout->addWidget(m_userSearchEdit, 1);
    searchLayout->addWidget(searchButton);
    searchLayout->addWidget(refreshButton);

    m_conversationList = new QListWidget;
    m_conversationList->setObjectName("OnlineList");
    connect(m_conversationList, &QListWidget::itemClicked, this, &MainWindow::selectConversation);

    m_groupNameEdit = new QLineEdit;
    m_groupNameEdit->setPlaceholderText("群名");
    m_groupFriendList = new QListWidget;
    m_groupFriendList->setSelectionMode(QAbstractItemView::MultiSelection);
    QPushButton *createGroupButton = new QPushButton("创建群聊");
    createGroupButton->setObjectName("SecondaryButton");
    connect(createGroupButton, &QPushButton::clicked, this, &MainWindow::createGroup);

    QFrame *sideCard = new QFrame;
    sideCard->setObjectName("SideCard");
    QVBoxLayout *sideLayout = new QVBoxLayout(sideCard);
    QLabel *sideTitle = new QLabel("会话");
    sideTitle->setObjectName("PanelTitle");
    QLabel *sideHint = new QLabel("搜索用户可发起首条私信；对方回复后自动成为好友。群聊只能从好友中创建。");
    sideHint->setObjectName("HintText");
    sideHint->setWordWrap(true);
    sideLayout->addWidget(sideTitle);
    sideLayout->addWidget(sideHint);
    sideLayout->addLayout(searchLayout);
    sideLayout->addWidget(m_conversationList, 3);
    sideLayout->addWidget(new QLabel("从好友创建群聊"));
    sideLayout->addWidget(m_groupNameEdit);
    sideLayout->addWidget(m_groupFriendList, 1);
    sideLayout->addWidget(createGroupButton);

    m_conversationTitle = new QLabel("选择会话");
    m_conversationTitle->setObjectName("PanelTitle");
    m_chatView = new QTextBrowser;
    m_chatView->setObjectName("ChatView");
    m_chatView->setHtml("<style>"
                         ".empty{padding:28px;color:#718575;text-align:center;}"
                         ".bubble{max-width:72%;padding:10px 12px;border:1px solid #dbe8dc;"
                         "border-radius:12px;background:#fff;margin:8px 0;}"
                         ".me{margin-left:24%;background:#eaf7f1;}"
                         ".private{border-color:#d8eadf;}.group{border-color:#eadcbf;}"
                         ".history{opacity:.88}.time{color:#718575;font-size:12px;}"
                         "</style><div class='empty'>从左侧选择好友、请求或群聊开始。</div>");

    m_messageEdit = new QLineEdit;
    m_messageEdit->setPlaceholderText("输入消息，回车发送");
    QPushButton *sendButton = new QPushButton("发送");
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);

    QHBoxLayout *composeLayout = new QHBoxLayout;
    composeLayout->addWidget(m_messageEdit, 1);
    composeLayout->addWidget(sendButton);

    QFrame *chatCard = new QFrame;
    chatCard->setObjectName("ContentCard");
    QVBoxLayout *chatLayout = new QVBoxLayout(chatCard);
    QLabel *chatTip = new QLabel("每个会话都会单独拉取历史记录；发送后立即显示本地消息。");
    chatTip->setObjectName("HintText");
    chatLayout->addWidget(m_conversationTitle);
    chatLayout->addWidget(chatTip);
    chatLayout->addWidget(m_chatView, 1);
    chatLayout->addLayout(composeLayout);

    QSplitter *splitter = new QSplitter;
    splitter->addWidget(sideCard);
    splitter->addWidget(chatCard);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes(QList<int>() << 360 << 780);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addWidget(splitter);
    return page;
}

void MainWindow::refreshSocial()
{
    m_client->refreshSocial();
}

void MainWindow::searchUser()
{
    m_client->searchUser(m_userSearchEdit->text());
}

void MainWindow::showSearchUser(const SocialUser &user)
{
    addConversationItem("搜索结果", user.nickname, user.account, PrivateConversation, user.account);
    showStatus("已找到用户：" + user.nickname);
}

void MainWindow::fillFriends(const QVector<SocialUser> &friends)
{
    m_friends = friends;
    rebuildConversationList();
    rebuildGroupFriendList();
}

void MainWindow::fillRequests(const QVector<SocialUser> &requests)
{
    m_requests = requests;
    rebuildConversationList();
}

void MainWindow::fillSentRequests(const QVector<SocialUser> &requests)
{
    m_sentRequests = requests;
    rebuildConversationList();
}

void MainWindow::fillGroups(const QVector<GroupInfo> &groups)
{
    m_groups = groups;
    rebuildConversationList();
}

void MainWindow::fillNotifications(const QVector<NotificationInfo> &notifications)
{
    int unread = 0;
    for (const NotificationInfo &item : notifications) {
        if (!item.read) {
            ++unread;
        }
    }
    if (unread > 0) {
        showStatus(QString("有 %1 条未读通知").arg(unread));
    }
}

void MainWindow::rebuildConversationList()
{
    m_conversationList->clear();
    for (const SocialUser &request : m_requests) {
        addConversationItem("私信请求", request.nickname, request.message,
                            PrivateReplyConversation, request.account);
    }
    for (const SocialUser &request : m_sentRequests) {
        addConversationItem("等待回复", request.nickname, request.message,
                            PrivateConversation, request.account);
    }
    for (const SocialUser &friendUser : m_friends) {
        addConversationItem("好友", friendUser.nickname, friendUser.account,
                            PrivateConversation, friendUser.account);
    }
    for (const GroupInfo &group : m_groups) {
        addConversationItem("群聊", group.name, QString("#%1").arg(group.id),
                            GroupConversation, group.name, group.id);
    }
    if (m_conversationList->count() == 0) {
        QListWidgetItem *item = new QListWidgetItem("暂无会话。搜索用户发送首条私信。");
        item->setFlags(Qt::NoItemFlags);
        m_conversationList->addItem(item);
    }
}

void MainWindow::rebuildGroupFriendList()
{
    m_groupFriendList->clear();
    for (const SocialUser &friendUser : m_friends) {
        QListWidgetItem *item = new QListWidgetItem(friendUser.nickname + " (" + friendUser.account + ")");
        item->setData(Qt::UserRole, friendUser.account);
        m_groupFriendList->addItem(item);
    }
}

void MainWindow::addConversationItem(const QString &section, const QString &title, const QString &subtitle,
                                     ConversationKind kind, const QString &target, int groupId)
{
    QListWidgetItem *item = new QListWidgetItem(QString("[%1] %2\n%3").arg(section, title, subtitle));
    item->setData(Qt::UserRole, int(kind));
    item->setData(Qt::UserRole + 1, target);
    item->setData(Qt::UserRole + 2, groupId);
    item->setData(Qt::UserRole + 3, title);
    m_conversationList->addItem(item);
}

void MainWindow::selectConversation(QListWidgetItem *item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }
    m_conversationKind = ConversationKind(item->data(Qt::UserRole).toInt());
    m_currentTarget = item->data(Qt::UserRole + 1).toString();
    m_currentGroupId = item->data(Qt::UserRole + 2).toInt();
    m_currentTitle = item->data(Qt::UserRole + 3).toString();

    if (m_conversationKind == GroupConversation) {
        clearChatForSelection("群聊：" + m_currentTitle, QString("群 ID：%1").arg(m_currentGroupId));
        m_client->requestGroupHistory(m_currentGroupId);
    } else {
        const QString hint = m_conversationKind == PrivateReplyConversation
            ? "回复后双方会自动成为好友。"
            : "非好友可以先发送一条消息，对方回复后成为好友。";
        clearChatForSelection("私聊：" + m_currentTitle, hint);
        m_client->requestPrivateHistory(m_currentTarget);
    }
    m_messageEdit->setFocus();
}

void MainWindow::clearChatForSelection(const QString &title, const QString &hint)
{
    m_conversationTitle->setText(title);
    m_chatView->setHtml(QString("<style>"
                                ".empty{padding:28px;color:#718575;text-align:center;}"
                                ".bubble{max-width:72%;padding:10px 12px;border:1px solid #dbe8dc;"
                                "border-radius:12px;background:#fff;margin:8px 0;}"
                                ".me{margin-left:24%;background:#eaf7f1;}"
                                ".private{border-color:#d8eadf;}.group{border-color:#eadcbf;}"
                                ".history{opacity:.88}.time{color:#718575;font-size:12px;}"
                                "</style><div class='empty'>%1<br>%2</div>")
                        .arg(htmlEscape(title), htmlEscape(hint)));
}

void MainWindow::sendCurrentMessage()
{
    const QString text = m_messageEdit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (m_conversationKind == NoConversation) {
        QMessageBox::information(this, "发送消息", "请先从左侧选择一个私聊或群聊。");
        return;
    }
    m_messageEdit->clear();
    if (m_conversationKind == GroupConversation) {
        appendGroupMessage(m_currentGroupId, m_client->currentAccount(), text);
        m_client->sendGroupMessage(m_currentGroupId, text);
    } else if (m_conversationKind == PrivateReplyConversation) {
        appendPrivateMessage(m_currentTarget, text, false);
        m_client->sendPrivateReply(m_currentTarget, text);
    } else {
        appendPrivateMessage(m_currentTarget, text, false);
        m_client->sendPrivateStart(m_currentTarget, text);
    }
}

void MainWindow::createGroup()
{
    QStringList members;
    for (QListWidgetItem *item : m_groupFriendList->selectedItems()) {
        members << item->data(Qt::UserRole).toString();
    }
    m_client->createGroup(m_groupNameEdit->text(), members);
    m_groupNameEdit->clear();
}

void MainWindow::appendPrivateMessage(const QString &senderOrTarget, const QString &message, bool incoming)
{
    if (m_conversationKind != PrivateConversation &&
        m_conversationKind != PrivateReplyConversation) {
        showStatus("收到私信：" + peerName(senderOrTarget));
        return;
    }
    if (incoming && senderOrTarget != m_currentTarget) {
        showStatus("收到私信：" + peerName(senderOrTarget));
        return;
    }
    const QString who = incoming ? peerName(senderOrTarget) : "我";
    const QString cls = incoming ? "bubble private" : "bubble private me";
    appendChatHtml(QString("<div class='%1'><b>%2</b><span class='time'> %3</span><br>%4</div>")
                   .arg(cls, htmlEscape(who),
                        QDateTime::currentDateTime().toString("HH:mm:ss"),
                        htmlEscape(message)));
}

void MainWindow::appendGroupMessage(int groupId, const QString &sender, const QString &message)
{
    if (m_conversationKind != GroupConversation || groupId != m_currentGroupId) {
        showStatus(QString("群聊 #%1 有新消息").arg(groupId));
        return;
    }
    const QString who = sender == m_client->currentAccount() ? "我" : peerName(sender);
    const QString cls = sender == m_client->currentAccount() ? "bubble group me" : "bubble group";
    appendChatHtml(QString("<div class='%1'><b>%2</b><span class='time'> %3</span><br>%4</div>")
                   .arg(cls, htmlEscape(who),
                        QDateTime::currentDateTime().toString("HH:mm:ss"),
                        htmlEscape(message)));
}

void MainWindow::appendHistoryMessage(const QString &kind, const QString &sender, const QString &recipient,
                                      const QString &message, const QString &timestamp)
{
    if (kind == "GROUP") {
        if (m_conversationKind != GroupConversation || recipient.toInt() != m_currentGroupId) {
            return;
        }
        const QString who = sender == m_client->currentAccount() ? "我" : peerName(sender);
        appendChatHtml(QString("<div class='bubble history'><b>%1</b><span class='time'> %2</span><br>%3</div>")
                       .arg(htmlEscape(who), htmlEscape(timestamp), htmlEscape(message)));
    } else {
        const QString peer = sender == m_client->currentAccount() ? recipient : sender;
        if ((m_conversationKind != PrivateConversation && m_conversationKind != PrivateReplyConversation) ||
            peer != m_currentTarget) {
            return;
        }
        const QString who = sender == m_client->currentAccount() ? "我" : peerName(sender);
        appendChatHtml(QString("<div class='bubble history private'><b>%1</b><span class='time'> %2</span><br>%3</div>")
                       .arg(htmlEscape(who), htmlEscape(timestamp), htmlEscape(message)));
    }
}

void MainWindow::appendChatHtml(const QString &html)
{
    m_chatView->moveCursor(QTextCursor::End);
    m_chatView->insertHtml(html);
    m_chatView->insertHtml("<br><br>");
    m_chatView->moveCursor(QTextCursor::End);
}

QString MainWindow::peerName(const QString &account) const
{
    if (account == m_client->currentAccount()) {
        return "我";
    }
    for (const SocialUser &user : m_friends) {
        if (user.account == account || user.nickname == account) {
            return user.nickname;
        }
    }
    for (const SocialUser &user : m_requests) {
        if (user.account == account || user.nickname == account) {
            return user.nickname;
        }
    }
    for (const SocialUser &user : m_sentRequests) {
        if (user.account == account || user.nickname == account) {
            return user.nickname;
        }
    }
    return account;
}

void MainWindow::showStatus(const QString &message)
{
    m_statusLabel->setText(message);
    statusBar()->showMessage(message, 4500);
}

QWidget *MainWindow::buildBbsPage()
{
    QWidget *page = new QWidget;
    page->setObjectName("Page");

    m_postsList = new QListWidget;
    m_postsList->setObjectName("PostsList");
    m_postsList->setSpacing(8);
    m_postsList->setMinimumWidth(310);
    connect(m_postsList, &QListWidget::itemClicked, this, &MainWindow::viewSelectedPost);

    QLabel *listTitle = new QLabel("社区讨论");
    listTitle->setObjectName("PanelTitle");
    QPushButton *refreshButton = new QPushButton("刷新");
    refreshButton->setObjectName("GhostButton");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPosts);

    QHBoxLayout *listHeader = new QHBoxLayout;
    listHeader->addWidget(listTitle);
    listHeader->addStretch();
    listHeader->addWidget(refreshButton);

    QFrame *listCard = new QFrame;
    listCard->setObjectName("SideCard");
    QVBoxLayout *listLayout = new QVBoxLayout(listCard);
    listLayout->addLayout(listHeader);
    listLayout->addWidget(m_postsList, 1);

    m_titleEdit = new QLineEdit;
    m_titleEdit->setPlaceholderText("写一个清楚的标题");
    m_contentEdit = new QTextEdit;
    m_contentEdit->setPlaceholderText("补充正文内容");
    m_contentEdit->setMinimumHeight(230);
    m_postAttachmentEdit = new QLineEdit;
    m_postAttachmentEdit->setPlaceholderText("可选附件");
    m_postAttachmentEdit->setReadOnly(true);
    QPushButton *choosePostFile = new QPushButton("选择");
    choosePostFile->setObjectName("GhostButton");
    QPushButton *clearPostFile = new QPushButton("清空");
    clearPostFile->setObjectName("GhostButton");
    QPushButton *createButton = new QPushButton("发布帖子");
    connect(choosePostFile, &QPushButton::clicked, this, &MainWindow::choosePostAttachment);
    connect(clearPostFile, &QPushButton::clicked, this, &MainWindow::clearPostAttachment);
    connect(createButton, &QPushButton::clicked, this, &MainWindow::createPost);

    QHBoxLayout *postFileLayout = new QHBoxLayout;
    postFileLayout->addWidget(m_postAttachmentEdit, 1);
    postFileLayout->addWidget(choosePostFile);
    postFileLayout->addWidget(clearPostFile);

    QFrame *createCard = new QFrame;
    createCard->setObjectName("ContentCard");
    QVBoxLayout *createLayout = new QVBoxLayout(createCard);
    QLabel *createTitle = new QLabel("发布新帖");
    createTitle->setObjectName("PanelTitle");
    createLayout->addWidget(createTitle);
    createLayout->addWidget(m_titleEdit);
    createLayout->addWidget(m_contentEdit, 1);
    createLayout->addLayout(postFileLayout);
    createLayout->addWidget(createButton, 0, Qt::AlignRight);

    QWidget *createPage = new QWidget;
    QVBoxLayout *createPageLayout = new QVBoxLayout(createPage);
    createPageLayout->addWidget(createCard, 1);

    m_postDetail = new QTextBrowser;
    m_postDetail->setObjectName("PostDetail");
    m_postDetail->setOpenLinks(false);
    m_postDetail->setOpenExternalLinks(false);
    connect(m_postDetail, &QTextBrowser::anchorClicked, this, &MainWindow::openBbsLink);
    renderEmptyDetail();

    m_replyEdit = new QTextEdit;
    m_replyEdit->setPlaceholderText("写下回复内容");
    m_replyEdit->setMinimumHeight(100);
    m_replyAttachmentEdit = new QLineEdit;
    m_replyAttachmentEdit->setPlaceholderText("可选回复附件");
    m_replyAttachmentEdit->setReadOnly(true);
    QPushButton *chooseReplyFile = new QPushButton("选择");
    chooseReplyFile->setObjectName("GhostButton");
    QPushButton *clearReplyFile = new QPushButton("清空");
    clearReplyFile->setObjectName("GhostButton");
    QPushButton *replyButton = new QPushButton("回复");
    replyButton->setObjectName("SecondaryButton");
    connect(chooseReplyFile, &QPushButton::clicked, this, &MainWindow::chooseReplyAttachment);
    connect(clearReplyFile, &QPushButton::clicked, this, &MainWindow::clearReplyAttachment);
    connect(replyButton, &QPushButton::clicked, this, &MainWindow::replyToPost);

    QHBoxLayout *replyFileLayout = new QHBoxLayout;
    replyFileLayout->addWidget(m_replyAttachmentEdit, 1);
    replyFileLayout->addWidget(chooseReplyFile);
    replyFileLayout->addWidget(clearReplyFile);

    QFrame *detailCard = new QFrame;
    detailCard->setObjectName("ContentCard");
    QVBoxLayout *detailLayout = new QVBoxLayout(detailCard);
    QLabel *detailTitle = new QLabel("帖子详情");
    detailTitle->setObjectName("PanelTitle");
    detailLayout->addWidget(detailTitle);
    detailLayout->addWidget(m_postDetail, 1);
    detailLayout->addWidget(m_replyEdit);
    detailLayout->addLayout(replyFileLayout);
    detailLayout->addWidget(replyButton, 0, Qt::AlignRight);

    QWidget *detailPage = new QWidget;
    QVBoxLayout *detailPageLayout = new QVBoxLayout(detailPage);
    detailPageLayout->addWidget(detailCard, 1);

    m_bbsWorkTabs = new QTabWidget;
    m_bbsWorkTabs->setObjectName("BbsWorkTabs");
    m_bbsWorkTabs->addTab(createPage, "发布帖子");
    m_bbsDetailTabIndex = m_bbsWorkTabs->addTab(detailPage, "查看 / 回复");
    m_bbsWorkTabs->setTabEnabled(m_bbsDetailTabIndex, false);

    QSplitter *splitter = new QSplitter;
    splitter->addWidget(listCard);
    splitter->addWidget(m_bbsWorkTabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes(QList<int>() << 330 << 820);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addWidget(splitter);
    return page;
}

QWidget *MainWindow::createPostCardWidget(const BbsPost &post) const
{
    QFrame *card = new QFrame;
    card->setObjectName("PostCard");
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(4);

    QString titleText = post.title.simplified();
    if (titleText.size() > 26) {
        titleText = titleText.left(26) + "...";
    }
    QLabel *title = new QLabel(titleText);
    title->setObjectName("PostCardTitle");
    title->setTextFormat(Qt::PlainText);

    QString bodyText = post.content.simplified();
    if (bodyText.size() > 44) {
        bodyText = bodyText.left(44) + "...";
    }
    QLabel *body = new QLabel(bodyText);
    body->setObjectName("PostCardBody");
    body->setTextFormat(Qt::PlainText);

    QLabel *meta = new QLabel(QString("#%1 · %2 · %3%4")
                              .arg(post.id)
                              .arg(post.author)
                              .arg(post.time)
                              .arg(post.attachment == "none" || post.attachment.isEmpty() ? "" : " · 附件"));
    meta->setObjectName("PostCardMeta");
    meta->setTextFormat(Qt::PlainText);

    layout->addWidget(title);
    layout->addWidget(body);
    layout->addWidget(meta);
    return card;
}

void MainWindow::refreshPosts()
{
    m_client->listPosts();
}

void MainWindow::createPost()
{
    m_client->createPost(m_titleEdit->text(), m_contentEdit->toPlainText(), m_postAttachmentEdit->text());
}

void MainWindow::viewSelectedPost()
{
    const int id = selectedPostId();
    if (id <= 0) {
        return;
    }
    m_currentPostId = id;
    if (m_bbsWorkTabs && m_bbsDetailTabIndex >= 0) {
        m_bbsWorkTabs->setTabEnabled(m_bbsDetailTabIndex, true);
        m_bbsWorkTabs->setCurrentIndex(m_bbsDetailTabIndex);
    }
    m_client->viewPost(id);
}

void MainWindow::replyToPost()
{
    if (m_currentPostId <= 0) {
        QMessageBox::information(this, "回复", "请先选择一个帖子。");
        return;
    }
    m_client->replyPost(m_currentPostId, m_replyEdit->toPlainText(), m_replyAttachmentEdit->text());
}

void MainWindow::choosePostAttachment()
{
    const QString path = QFileDialog::getOpenFileName(this, "选择帖子附件");
    if (!path.isEmpty()) {
        m_postAttachmentEdit->setText(path);
    }
}

void MainWindow::chooseReplyAttachment()
{
    if (m_currentPostId <= 0) {
        QMessageBox::information(this, "回复附件", "请先选择一个帖子。");
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, "选择回复附件");
    if (!path.isEmpty()) {
        m_replyAttachmentEdit->setText(path);
    }
}

void MainWindow::clearPostAttachment()
{
    m_postAttachmentEdit->clear();
}

void MainWindow::clearReplyAttachment()
{
    m_replyAttachmentEdit->clear();
}

void MainWindow::openBbsLink(const QUrl &url)
{
    const int id = bbsUrlId(url);
    if (id <= 0) {
        QMessageBox::warning(this, "下载附件", "附件链接无效：" + url.toString());
        return;
    }
    const QString dir = downloadsDirectory();
    if (url.scheme() == "bbs-post") {
        m_client->downloadBbsPostAttachment(id, dir);
    } else if (url.scheme() == "bbs-reply") {
        m_client->downloadBbsReplyAttachment(id, dir);
    }
}

void MainWindow::fillPosts(const QVector<BbsPost> &posts)
{
    m_posts = posts;
    m_postsList->clear();
    for (const BbsPost &post : posts) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, post.id);
        item->setSizeHint(QSize(310, 90));
        m_postsList->addItem(item);
        m_postsList->setItemWidget(item, createPostCardWidget(post));
    }
    showStatus(QString("已加载 %1 个帖子").arg(posts.size()));
}

void MainWindow::showPostDetail(const BbsPost &post, const QVector<BbsReply> &replies)
{
    m_currentPostId = post.id;
    m_currentReplies = replies;

    QString html;
    html += "<style>"
            "body{font-family:'Microsoft YaHei'; color:#213527;}"
            ".post{background:#fff; border:1px solid #dbe8dc; border-radius:14px; padding:16px;}"
            ".title{font-size:22px; font-weight:800; color:#213527;}"
            ".meta{color:#718575; margin:8px 0 14px 0;}"
            ".body{font-size:15px; line-height:165%; margin-top:12px;}"
            ".fileline{margin-top:14px; padding:8px 10px; background:#eef8f1; border-radius:8px; color:#16745a;}"
            ".reply{background:#f7fbf7; border:1px solid #e4eee5; border-radius:12px; padding:12px; margin-top:12px;}"
            ".reply-meta{color:#718575; font-size:12px; margin-bottom:5px;}"
            ".reply-body{font-size:14px; line-height:155%;}"
            "a{color:#16745a; text-decoration:none; font-weight:700;}"
            "</style>";
    html += QString("<div class='post'><div class='title'>%1</div>")
            .arg(htmlEscape(post.title));
    html += QString("<div class='meta'>#%1 · 作者：%2 · 时间：%3</div>")
            .arg(post.id)
            .arg(htmlEscape(post.author))
            .arg(htmlEscape(post.time));
    html += QString("<div class='body'>%1</div>").arg(htmlEscape(post.content));
    if (!post.attachment.isEmpty() && post.attachment != "none") {
        html += QString("<div class='fileline'><a href=\"bbs-post:%1\">下载帖子附件：%2</a></div>")
                .arg(post.id)
                .arg(htmlEscape(post.attachment));
    }
    html += "</div>";

    html += QString("<h3>回复 · %1 条</h3>").arg(replies.size());
    for (const BbsReply &reply : replies) {
        html += "<div class='reply'>";
        html += QString("<div class='reply-meta'>#%1 · %2 · %3</div>")
                .arg(reply.id)
                .arg(htmlEscape(reply.author))
                .arg(htmlEscape(reply.time));
        html += QString("<div class='reply-body'>%1</div>").arg(htmlEscape(reply.content));
        if (!reply.attachment.isEmpty() && reply.attachment != "none") {
            html += QString("<div class='fileline'><a href=\"bbs-reply:%1\">下载回复附件：%2</a></div>")
                    .arg(reply.id)
                    .arg(htmlEscape(reply.attachment));
        }
        html += "</div>";
    }
    m_postDetail->setHtml(html);
}

void MainWindow::onBbsOperation(const QString &message, bool ok)
{
    showStatus((ok ? "成功：" : "失败：") + message);
    if (!ok) {
        return;
    }
    if (message.contains("正在上传")) {
        return;
    }
    const QString lower = message.toLower();
    if (lower.contains("uploaded") || message.contains("上传")) {
        refreshPosts();
        if (m_currentPostId > 0) {
            m_client->viewPost(m_currentPostId);
        }
        return;
    }
    if (lower.contains("post") || message.contains("帖子")) {
        m_titleEdit->clear();
        m_contentEdit->clear();
        m_postAttachmentEdit->clear();
        refreshPosts();
    }
    if (lower.contains("reply") || message.contains("回复")) {
        m_replyEdit->clear();
        m_replyAttachmentEdit->clear();
        if (m_currentPostId > 0) {
            m_client->viewPost(m_currentPostId);
        }
    }
}

void MainWindow::onBbsDownloadFinished(const QString &path)
{
    showStatus("附件已下载到：" + path);
    QMessageBox::information(this, "下载完成", "附件已保存到：\n" + path);
}

int MainWindow::selectedPostId() const
{
    QListWidgetItem *item = m_postsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

QString MainWindow::htmlEscape(const QString &text) const
{
    return text.toHtmlEscaped().replace("\n", "<br>");
}

QString MainWindow::downloadsDirectory() const
{
    QDir dir(QDir::currentPath());
    if (!dir.exists("downloads")) {
        dir.mkdir("downloads");
    }
    return dir.filePath("downloads");
}

void MainWindow::renderEmptyDetail()
{
    m_postDetail->setHtml("<div style='padding:28px; color:#718575; font-size:15px;'>请选择左侧帖子查看详情。</div>");
}

int MainWindow::bbsUrlId(const QUrl &url)
{
    bool ok = false;
    int id = url.host().toInt(&ok);
    if (ok && id > 0) {
        return id;
    }
    QString text = url.path();
    text.remove('/');
    id = text.toInt(&ok);
    if (ok && id > 0) {
        return id;
    }
    text = url.toString();
    const int colon = text.indexOf(':');
    if (colon >= 0) {
        text = text.mid(colon + 1);
        text.remove('/');
        id = text.toInt(&ok);
        if (ok && id > 0) {
            return id;
        }
    }
    return 0;
}
