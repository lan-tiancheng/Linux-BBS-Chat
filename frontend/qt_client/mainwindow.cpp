#include "mainwindow.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QSize>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(NetworkClient *client, const QString &username, QWidget *parent)
    : QMainWindow(parent),
      m_client(client),
      m_username(username),
      m_currentPostId(0),
      m_chatPlaceholderVisible(false),
      m_bbsWorkTabs(nullptr),
      m_bbsDetailTabIndex(-1)
{
    setWindowTitle("BBS Chat - " + username);
    resize(1120, 760);

    QTabWidget *tabs = new QTabWidget;
    tabs->setObjectName("MainTabs");
    tabs->addTab(buildChatPage(), "聊天 Chat");
    tabs->addTab(buildBbsPage(), "论坛 BBS");
    setCentralWidget(tabs);

    m_statusLabel = new QLabel("已登录：" + username);
    statusBar()->addWidget(m_statusLabel, 1);

    connect(m_client, &NetworkClient::statusMessage, this, &MainWindow::showStatus);
    connect(m_client, &NetworkClient::errorText, this, &MainWindow::showStatus);
    connect(m_client, &NetworkClient::onlineUsersReceived, this, &MainWindow::updateOnlineUsers);
    connect(m_client, &NetworkClient::groupMessageReceived, this, &MainWindow::appendGroupMessage);
    connect(m_client, &NetworkClient::privateMessageReceived, this, &MainWindow::appendPrivateMessage);
    connect(m_client, &NetworkClient::historyMessageReceived, this, &MainWindow::appendHistoryMessage);
    connect(m_client, &NetworkClient::bbsPostsReceived, this, &MainWindow::fillPosts);
    connect(m_client, &NetworkClient::bbsPostDetailReceived, this, &MainWindow::showPostDetail);
    connect(m_client, &NetworkClient::bbsOperationFinished, this, &MainWindow::onBbsOperation);
    connect(m_client, &NetworkClient::bbsDownloadFinished, this, &MainWindow::onBbsDownloadFinished);

    refreshOnlineUsers();
    QTimer::singleShot(200, this, &MainWindow::refreshPosts);
}

QWidget *MainWindow::buildChatPage()
{
    QWidget *page = new QWidget;
    page->setObjectName("Page");

    m_chatView = new QTextBrowser;
    m_chatView->setObjectName("ChatView");
    m_chatView->setOpenExternalLinks(false);
    m_chatView->setHtml("<div class='empty'>登录成功。这里会显示历史群聊、历史私聊和实时消息。</div><br>");
    m_chatPlaceholderVisible = true;

    m_onlineList = new QListWidget;
    m_onlineList->setObjectName("OnlineList");
    connect(m_onlineList, &QListWidget::itemDoubleClicked, this, &MainWindow::setPrivateTargetFromList);

    QLabel *onlineTitle = new QLabel("在线用户");
    onlineTitle->setObjectName("PanelTitle");
    QPushButton *refreshButton = new QPushButton("刷新");
    refreshButton->setObjectName("GhostButton");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshOnlineUsers);

    QHBoxLayout *onlineHeader = new QHBoxLayout;
    onlineHeader->addWidget(onlineTitle);
    onlineHeader->addStretch();
    onlineHeader->addWidget(refreshButton);

    QFrame *sideCard = new QFrame;
    sideCard->setObjectName("SideCard");
    QVBoxLayout *sideLayout = new QVBoxLayout(sideCard);
    sideLayout->addLayout(onlineHeader);
    sideLayout->addWidget(m_onlineList, 1);

    m_messageEdit = new QLineEdit;
    m_messageEdit->setPlaceholderText("输入群聊消息，回车发送");
    QPushButton *sendGroupButton = new QPushButton("发送群聊");
    connect(sendGroupButton, &QPushButton::clicked, this, &MainWindow::sendGroup);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::sendGroup);

    QHBoxLayout *groupLayout = new QHBoxLayout;
    groupLayout->addWidget(m_messageEdit, 1);
    groupLayout->addWidget(sendGroupButton);

    m_privateTargetEdit = new QLineEdit;
    m_privateTargetEdit->setPlaceholderText("私聊对象用户名");
    QPushButton *sendPrivateButton = new QPushButton("发送私聊");
    sendPrivateButton->setObjectName("SecondaryButton");
    connect(sendPrivateButton, &QPushButton::clicked, this, &MainWindow::sendPrivate);

    QHBoxLayout *privateLayout = new QHBoxLayout;
    privateLayout->addWidget(m_privateTargetEdit, 0);
    privateLayout->addWidget(sendPrivateButton, 0);

    QLabel *chatTitle = new QLabel("消息流");
    chatTitle->setObjectName("PanelTitle");
    QLabel *chatTip = new QLabel("私聊会标记为【私聊】；给离线注册用户发送后，消息会保存到历史记录。");
    chatTip->setObjectName("HintText");

    QFrame *chatCard = new QFrame;
    chatCard->setObjectName("ContentCard");
    QVBoxLayout *chatLayout = new QVBoxLayout(chatCard);
    chatLayout->addWidget(chatTitle);
    chatLayout->addWidget(chatTip);
    chatLayout->addWidget(m_chatView, 1);
    chatLayout->addLayout(groupLayout);
    chatLayout->addLayout(privateLayout);

    QSplitter *splitter = new QSplitter;
    splitter->addWidget(chatCard);
    splitter->addWidget(sideCard);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addWidget(splitter);
    return page;
}

QWidget *MainWindow::buildBbsPage()
{
    QWidget *page = new QWidget;
    page->setObjectName("Page");

    m_postsList = new QListWidget;
    m_postsList->setObjectName("PostsList");
    m_postsList->setSpacing(8);
    m_postsList->setMinimumWidth(300);
    connect(m_postsList, &QListWidget::itemClicked, this, &MainWindow::viewSelectedPost);

    QLabel *listTitle = new QLabel("帖子广场");
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
    m_titleEdit->setPlaceholderText("帖子标题");
    m_contentEdit = new QTextEdit;
    m_contentEdit->setPlaceholderText("帖子正文");
    m_contentEdit->setMinimumHeight(260);
    m_postAttachmentEdit = new QLineEdit;
    m_postAttachmentEdit->setPlaceholderText("可选附件路径");
    m_postAttachmentEdit->setReadOnly(true);
    QPushButton *choosePostFile = new QPushButton("选择附件");
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
    QLabel *createTitle = new QLabel("发布新帖子");
    createTitle->setObjectName("PanelTitle");
    QLabel *createTip = new QLabel("可以选择附件一起上传；附件只属于你发布的帖子。");
    createTip->setObjectName("HintText");
    createLayout->addWidget(createTitle);
    createLayout->addWidget(createTip);
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
    m_postDetail->setMinimumHeight(430);
    connect(m_postDetail, &QTextBrowser::anchorClicked, this, &MainWindow::openBbsLink);
    renderEmptyDetail();

    m_replyEdit = new QTextEdit;
    m_replyEdit->setPlaceholderText("选择帖子后，在这里写回复");
    m_replyEdit->setMinimumHeight(110);
    m_replyAttachmentEdit = new QLineEdit;
    m_replyAttachmentEdit->setPlaceholderText("可选回复附件路径");
    m_replyAttachmentEdit->setReadOnly(true);
    QPushButton *chooseReplyFile = new QPushButton("选择附件");
    chooseReplyFile->setObjectName("GhostButton");
    QPushButton *clearReplyFile = new QPushButton("清空");
    clearReplyFile->setObjectName("GhostButton");
    QPushButton *replyButton = new QPushButton("回复帖子");
    replyButton->setObjectName("SecondaryButton");
    connect(chooseReplyFile, &QPushButton::clicked, this, &MainWindow::chooseReplyAttachment);
    connect(clearReplyFile, &QPushButton::clicked, this, &MainWindow::clearReplyAttachment);
    connect(replyButton, &QPushButton::clicked, this, &MainWindow::replyToPost);

    QHBoxLayout *replyFileLayout = new QHBoxLayout;
    replyFileLayout->addWidget(m_replyAttachmentEdit, 1);
    replyFileLayout->addWidget(chooseReplyFile);
    replyFileLayout->addWidget(clearReplyFile);

    QFrame *replyCard = new QFrame;
    replyCard->setObjectName("ComposeCard");
    QVBoxLayout *replyLayout = new QVBoxLayout(replyCard);
    QLabel *replyTitle = new QLabel("回复当前帖子");
    replyTitle->setObjectName("PanelTitle");
    QLabel *replyTip = new QLabel("回复附件只属于你自己的回复，别人不能给你的回复补附件。");
    replyTip->setObjectName("HintText");
    replyLayout->addWidget(replyTitle);
    replyLayout->addWidget(replyTip);
    replyLayout->addWidget(m_replyEdit);
    replyLayout->addLayout(replyFileLayout);
    replyLayout->addWidget(replyButton, 0, Qt::AlignRight);

    QFrame *detailCard = new QFrame;
    detailCard->setObjectName("ContentCard");
    QVBoxLayout *detailLayout = new QVBoxLayout(detailCard);
    QLabel *detailTitle = new QLabel("帖子详情 / 回复");
    detailTitle->setObjectName("PanelTitle");
    detailLayout->addWidget(detailTitle);
    detailLayout->addWidget(m_postDetail, 1);
    detailLayout->addWidget(replyCard);

    QWidget *detailPage = new QWidget;
    QVBoxLayout *detailPageLayout = new QVBoxLayout(detailPage);
    detailPageLayout->addWidget(detailCard, 1);

    m_bbsWorkTabs = new QTabWidget;
    m_bbsWorkTabs->setObjectName("BbsWorkTabs");
    m_bbsWorkTabs->addTab(createPage, "发布帖子");
    m_bbsDetailTabIndex = m_bbsWorkTabs->addTab(detailPage, "查看 / 回复");
    m_bbsWorkTabs->setTabEnabled(m_bbsDetailTabIndex, false);
    m_bbsWorkTabs->setCurrentIndex(0);

    QSplitter *splitter = new QSplitter;
    splitter->addWidget(listCard);
    splitter->addWidget(m_bbsWorkTabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes(QList<int>() << 330 << 790);

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
    if (titleText.size() > 22) {
        titleText = titleText.left(22) + "...";
    }
    QLabel *title = new QLabel(titleText);
    title->setObjectName("PostCardTitle");
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(false);

    QString bodyText = post.content.simplified();
    if (bodyText.size() > 38) {
        bodyText = bodyText.left(38) + "...";
    }
    QLabel *body = new QLabel(bodyText);
    body->setObjectName("PostCardBody");
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(false);

    QString timeText = post.time;
    if (timeText.size() > 16) {
        timeText = timeText.left(16);
    }
    QLabel *meta = new QLabel(QString("#%1 · %2 · %3%4")
                              .arg(post.id)
                              .arg(post.author)
                              .arg(timeText)
                              .arg(post.attachment == "none" || post.attachment.isEmpty() ? "" : " · 📎"));
    meta->setObjectName("PostCardMeta");
    meta->setTextFormat(Qt::PlainText);
    meta->setWordWrap(false);

    layout->addWidget(title);
    layout->addWidget(body);
    layout->addWidget(meta);
    return card;
}

void MainWindow::sendGroup()
{
    const QString text = m_messageEdit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_client->sendGroupMessage(text);
    m_messageEdit->clear();
}

void MainWindow::sendPrivate()
{
    const QString target = m_privateTargetEdit->text().trimmed();
    const QString text = m_messageEdit->text().trimmed();
    if (target.isEmpty()) {
        QMessageBox::information(this, "私聊", "请先填写私聊对象，或在右侧在线用户中双击选择。也可以手动输入已注册但离线的用户名。");
        return;
    }
    if (target == m_username) {
        QMessageBox::warning(this, "私聊", "不能私聊自己。请换一个用户。");
        return;
    }
    if (text.isEmpty()) {
        QMessageBox::information(this, "私聊", "请输入要发送的消息内容。");
        return;
    }
    m_client->sendPrivateMessage(target, text);
    m_messageEdit->clear();
}

void MainWindow::refreshOnlineUsers()
{
    m_client->requestOnlineUsers();
}

void MainWindow::setPrivateTargetFromList()
{
    QListWidgetItem *item = m_onlineList->currentItem();
    if (!item) {
        return;
    }
    const QString dataName = item->data(Qt::UserRole).toString();
    const QString name = dataName.isEmpty() ? item->text().trimmed() : dataName;
    if (name == m_username) {
        QMessageBox::warning(this, "私聊", "不能私聊自己。列表里自己的名字只是用来显示在线状态。");
        return;
    }
    m_privateTargetEdit->setText(name);
    showStatus("私聊对象已设为：" + name);
}

void MainWindow::appendGroupMessage(const QString &sender, const QString &message)
{
    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");
    appendChatHtml(QString("<div class='bubble group'><span class='tag'>【群聊】</span>"
                           "<b>%1</b><span class='time'> %2</span><br>%3</div>")
                   .arg(htmlEscape(sender), now, htmlEscape(message)));
}

void MainWindow::appendPrivateMessage(const QString &senderOrTarget, const QString &message, bool incoming)
{
    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString title = incoming
        ? QString("%1 -> 我").arg(htmlEscape(senderOrTarget))
        : QString("我 -> %1").arg(htmlEscape(senderOrTarget));
    appendChatHtml(QString("<div class='bubble private'><span class='tag'>【私聊】</span>"
                           "<b>%1</b><span class='time'> %2</span><br>%3</div>")
                   .arg(title, now, htmlEscape(message)));
}

void MainWindow::appendHistoryMessage(const QString &kind, const QString &sender, const QString &recipient,
                                      const QString &message, const QString &timestamp)
{
    if (kind == "GROUP") {
        appendChatHtml(QString("<div class='bubble history'><span class='tag'>【历史群聊】</span>"
                               "<b>%1</b><span class='time'> %2</span><br>%3</div>")
                       .arg(htmlEscape(sender), htmlEscape(timestamp), htmlEscape(message)));
    } else {
        const QString title = recipient == m_username
            ? QString("%1 -> 我").arg(htmlEscape(sender))
            : QString("我 -> %1").arg(htmlEscape(recipient));
        appendChatHtml(QString("<div class='bubble history private'><span class='tag'>【历史私聊】</span>"
                               "<b>%1</b><span class='time'> %2</span><br>%3</div>")
                       .arg(title, htmlEscape(timestamp), htmlEscape(message)));
    }
}

void MainWindow::appendChatHtml(const QString &html)
{
    if (m_chatPlaceholderVisible) {
        m_chatView->clear();
        m_chatPlaceholderVisible = false;
    }
    m_chatView->moveCursor(QTextCursor::End);
    m_chatView->insertHtml(html);
    m_chatView->insertHtml("<br><br>");
    m_chatView->moveCursor(QTextCursor::End);
}

void MainWindow::updateOnlineUsers(const QStringList &users)
{
    m_onlineList->clear();
    for (const QString &user : users) {
        QListWidgetItem *item = new QListWidgetItem(user);
        if (user == m_username) {
            item->setText(user + "  我");
            item->setData(Qt::UserRole, user);
        }
        m_onlineList->addItem(item);
    }
}

void MainWindow::showStatus(const QString &message)
{
    m_statusLabel->setText(message);
    statusBar()->showMessage(message, 4500);
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
        QMessageBox::information(this, "回复", "请先在左侧选择一个帖子。");
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
        QMessageBox::information(this, "回复附件", "请先选择一个帖子，再给回复选择附件。");
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
        QMessageBox::warning(this, "下载附件", "附件链接无效，无法下载。\n链接：" + url.toString());
        return;
    }

    const QString dir = downloadsDirectory();
    if (url.scheme() == "bbs-post") {
        showStatus(QString("正在下载帖子附件 #%1 ...").arg(id));
        m_client->downloadBbsPostAttachment(id, dir);
    } else if (url.scheme() == "bbs-reply") {
        showStatus(QString("正在下载回复附件 #%1 ...").arg(id));
        m_client->downloadBbsReplyAttachment(id, dir);
    } else {
        QMessageBox::warning(this, "下载附件", "不支持的附件链接：" + url.toString());
    }
}

void MainWindow::fillPosts(const QVector<BbsPost> &posts)
{
    m_posts = posts;
    m_postsList->clear();
    for (const BbsPost &post : posts) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, post.id);
        item->setSizeHint(QSize(300, 86));
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
            ".post{background:#ffffff; border:1px solid #dbe8dc; border-radius:18px; padding:18px;}"
            ".title{font-size:24px; font-weight:800; color:#213527;}"
            ".meta{color:#7a8b7d; margin:8px 0 14px 0;}"
            ".body{font-size:15px; line-height:160%; margin:14px 0 0 0;}"
            ".fileline{margin:16px 0 0 0; padding:8px 10px; background:#eef8f1; border:1px solid #d7eadb; border-radius:10px; color:#16745a;}"
            ".reply{background:#f7fbf7; border:1px solid #e4eee5; border-radius:14px; padding:12px; margin-top:12px;}"
            ".reply-meta{color:#718575; font-size:12px; margin-bottom:5px;}"
            ".reply-body{font-size:14px; line-height:155%; margin:6px 0 0 0;}"
            "a{color:#16745a; text-decoration:none; font-weight:700;}"
            "h3{margin:18px 0 8px 0;}"
            "</style>";
    html += QString("<div class='post'><div class='title'>%1</div>")
            .arg(htmlEscape(post.title));
    html += QString("<div class='meta'>#%1 · 作者：%2 · 时间：%3</div>")
            .arg(post.id)
            .arg(htmlEscape(post.author))
            .arg(htmlEscape(post.time));
    html += QString("<p class='body'>%1</p>").arg(htmlEscape(post.content));
    if (!post.attachment.isEmpty() && post.attachment != "none") {
        html += QString("<p class='fileline'>📎 <a href=\"bbs-post:%1\">下载帖子附件：%2</a></p>")
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
        html += QString("<p class='reply-body'>%1</p>").arg(htmlEscape(reply.content));
        if (!reply.attachment.isEmpty() && reply.attachment != "none") {
            html += QString("<p class='fileline'>📎 <a href=\"bbs-reply:%1\">下载回复附件：%2</a></p>")
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

    const QString lower = message.toLower();

    // 创建帖子/回复之后，如果还有附件要上传，NetworkClient 会继续发起上传请求。
    // 这里先不刷新，等附件上传完成后再刷新，避免界面显示旧状态。
    if (message.contains("正在上传")) {
        return;
    }

    const bool postCreated = lower.contains("bbs post created") || lower.contains("帖子已发布");
    const bool replyCreated = lower.contains("bbs reply created") || lower.contains("回复已发布");
    const bool replyFileUploaded = lower.contains("bbs reply file uploaded");
    const bool postFileUploaded = lower.contains("bbs file uploaded") && !replyFileUploaded;

    if (postCreated || postFileUploaded) {
        m_titleEdit->clear();
        m_contentEdit->clear();
        m_postAttachmentEdit->clear();
        refreshPosts();
        return;
    }

    if (replyCreated || replyFileUploaded) {
        m_replyEdit->clear();
        m_replyAttachmentEdit->clear();
        if (m_currentPostId > 0) {
            m_client->viewPost(m_currentPostId);
        }
        return;
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
    if (!item) {
        return 0;
    }
    return item->data(Qt::UserRole).toInt();
}

BbsPost MainWindow::selectedPost() const
{
    const int id = selectedPostId();
    for (const BbsPost &post : m_posts) {
        if (post.id == id) {
            return post;
        }
    }
    return BbsPost();
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
    m_postDetail->setHtml("<div style='padding:28px; color:#718575; font-size:15px;'>请先点击左侧帖子卡片。<br><br>打开详情后，可以在下方回复，也可以点击附件链接下载到当前 Qt 程序目录下的 downloads 文件夹。</div>");
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
