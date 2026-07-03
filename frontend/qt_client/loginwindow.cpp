#include "loginwindow.h"
#include "networkclient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegExp>
#include <QTabWidget>
#include <QVBoxLayout>

LoginWindow::LoginWindow(NetworkClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setWindowTitle("BBS Chat - 登录 / 注册");
    setMinimumSize(500, 430);
    setObjectName("LoginWindow");

    QLabel *title = new QLabel("Linux BBS Chat");
    title->setObjectName("LoginTitle");
    QLabel *subtitle = new QLabel("Qt 客户端与 Web 前端使用同一套账号、好友、私聊、群聊和论坛协议");
    subtitle->setObjectName("LoginSubtitle");
    subtitle->setWordWrap(true);

    m_hostEdit = new QLineEdit("127.0.0.1");
    m_portEdit = new QLineEdit("8888");
    m_hostEdit->setPlaceholderText("服务器 IP");
    m_portEdit->setPlaceholderText("端口");
    m_connectButton = new QPushButton("连接服务器");

    QHBoxLayout *serverLayout = new QHBoxLayout;
    serverLayout->addWidget(m_hostEdit, 1);
    serverLayout->addWidget(m_portEdit);
    serverLayout->addWidget(m_connectButton);

    m_tabs = new QTabWidget;
    QWidget *loginPage = new QWidget;
    QFormLayout *loginForm = new QFormLayout(loginPage);
    m_loginEdit = new QLineEdit;
    m_loginPasswordEdit = new QLineEdit;
    m_loginPasswordEdit->setEchoMode(QLineEdit::Password);
    m_loginEdit->setPlaceholderText("9 位账号或昵称");
    m_loginPasswordEdit->setPlaceholderText("密码");
    m_loginButton = new QPushButton("登录");
    m_loginButton->setDefault(true);
    loginForm->addRow("账号或昵称", m_loginEdit);
    loginForm->addRow("密码", m_loginPasswordEdit);
    loginForm->addRow(QString(), m_loginButton);

    QWidget *registerPage = new QWidget;
    QFormLayout *registerForm = new QFormLayout(registerPage);
    m_registerAccountEdit = new QLineEdit;
    m_registerNicknameEdit = new QLineEdit;
    m_registerPasswordEdit = new QLineEdit;
    m_registerPasswordEdit->setEchoMode(QLineEdit::Password);
    m_registerAccountEdit->setPlaceholderText("必须是 9 位数字");
    m_registerNicknameEdit->setPlaceholderText("昵称不能重复");
    m_registerPasswordEdit->setPlaceholderText("至少 7 位，且包含数字和字母");
    m_registerButton = new QPushButton("注册账号");
    registerForm->addRow("账号", m_registerAccountEdit);
    registerForm->addRow("昵称", m_registerNicknameEdit);
    registerForm->addRow("密码", m_registerPasswordEdit);
    registerForm->addRow(QString(), m_registerButton);

    m_tabs->addTab(loginPage, "登录");
    m_tabs->addTab(registerPage, "注册");

    m_statusLabel = new QLabel("先启动后端 server，再连接登录。");
    m_statusLabel->setObjectName("StatusLabel");
    m_statusLabel->setWordWrap(true);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addWidget(subtitle, 0, Qt::AlignHCenter);
    layout->addSpacing(12);
    layout->addLayout(serverLayout);
    layout->addWidget(m_tabs);
    layout->addWidget(m_statusLabel);
    layout->addStretch();

    connect(m_connectButton, &QPushButton::clicked, this, &LoginWindow::doConnect);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::doLogin);
    connect(m_registerButton, &QPushButton::clicked, this, &LoginWindow::doRegister);
    connect(m_loginPasswordEdit, &QLineEdit::returnPressed, this, &LoginWindow::doLogin);
    connect(m_registerPasswordEdit, &QLineEdit::returnPressed, this, &LoginWindow::doRegister);

    connect(m_client, &NetworkClient::connected, this, &LoginWindow::onConnected);
    connect(m_client, &NetworkClient::errorText, this, &LoginWindow::onError);
    connect(m_client, &NetworkClient::loginSucceeded, this, &LoginWindow::onLoginSucceeded);
    connect(m_client, &NetworkClient::loginFailed, this, &LoginWindow::onLoginFailed);
    connect(m_client, &NetworkClient::registerSucceeded, this, &LoginWindow::onRegisterSucceeded);
    connect(m_client, &NetworkClient::registerFailed, this, &LoginWindow::onRegisterFailed);
}

void LoginWindow::doConnect()
{
    bool ok = false;
    const quint16 port = static_cast<quint16>(m_portEdit->text().toUShort(&ok));
    if (!ok) {
        m_statusLabel->setText("端口必须是数字。");
        return;
    }
    m_statusLabel->setText("正在连接服务器...");
    m_client->connectToServer(m_hostEdit->text().trimmed(), port);
}

void LoginWindow::doLogin()
{
    if (!ensureConnected()) {
        return;
    }
    const QString loginName = m_loginEdit->text().trimmed();
    const QString password = m_loginPasswordEdit->text();
    if (loginName.isEmpty() || password.isEmpty()) {
        m_statusLabel->setText("请填写账号或昵称和密码。");
        return;
    }
    if (loginName.contains(QRegExp("\\s|\\|"))) {
        m_statusLabel->setText("账号或昵称不能包含空格或竖线。");
        return;
    }
    m_statusLabel->setText("正在登录...");
    m_client->login(loginName, password);
}

void LoginWindow::doRegister()
{
    if (!ensureConnected()) {
        return;
    }
    const QString account = m_registerAccountEdit->text().trimmed();
    const QString nickname = m_registerNicknameEdit->text().trimmed();
    const QString password = m_registerPasswordEdit->text();
    if (!validateAccount(account)) {
        m_statusLabel->setText("账号必须是 9 位数字。");
        return;
    }
    if (nickname.isEmpty() || nickname.contains(QRegExp("\\s|\\|"))) {
        m_statusLabel->setText("昵称不能为空，且不能包含空格或竖线。");
        return;
    }
    if (!validatePassword(password)) {
        m_statusLabel->setText("密码至少 7 位，并且必须同时包含数字和字母。");
        return;
    }
    m_statusLabel->setText("正在注册...");
    m_client->registerUser(account, password, nickname);
}

void LoginWindow::onConnected()
{
    m_statusLabel->setText("服务器已连接，可以登录或注册。");
}

void LoginWindow::onError(const QString &message)
{
    m_statusLabel->setText("连接错误：" + message);
}

void LoginWindow::onLoginSucceeded(const QString &displayName)
{
    m_statusLabel->setText("登录成功：" + displayName);
    emit loginAccepted(displayName);
}

void LoginWindow::onLoginFailed(const QString &message)
{
    m_statusLabel->setText("登录失败：" + message);
}

void LoginWindow::onRegisterSucceeded(const QString &message)
{
    m_statusLabel->setText("注册成功：" + message + "。现在可以登录。");
    m_tabs->setCurrentIndex(0);
    m_loginEdit->setText(m_registerNicknameEdit->text().trimmed());
    m_loginPasswordEdit->clear();
}

void LoginWindow::onRegisterFailed(const QString &message)
{
    m_statusLabel->setText("注册失败：" + message);
}

bool LoginWindow::ensureConnected()
{
    if (m_client->isConnected()) {
        return true;
    }
    doConnect();
    m_statusLabel->setText("已发起连接，请连接成功后再点击一次。");
    return false;
}

bool LoginWindow::validateAccount(const QString &account) const
{
    return QRegExp("^\\d{9}$").exactMatch(account);
}

bool LoginWindow::validatePassword(const QString &password) const
{
    return password.size() >= 7 &&
           password.contains(QRegExp("[A-Za-z]")) &&
           password.contains(QRegExp("\\d"));
}
