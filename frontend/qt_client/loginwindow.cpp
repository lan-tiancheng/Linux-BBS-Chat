#include "loginwindow.h"
#include "networkclient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginWindow::LoginWindow(NetworkClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setWindowTitle("BBS Chat - 登录 / 注册");
    setMinimumSize(430, 360);
    setObjectName("LoginWindow");

    QLabel *title = new QLabel("Linux BBS Chat");
    title->setObjectName("LoginTitle");
    QLabel *subtitle = new QLabel("淡绿色 Qt 前端 · 登录后进入聊天与 BBS 页面");
    subtitle->setObjectName("LoginSubtitle");

    m_hostEdit = new QLineEdit("127.0.0.1");
    m_portEdit = new QLineEdit("8888");
    m_userEdit = new QLineEdit;
    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_hostEdit->setPlaceholderText("服务器 IP");
    m_portEdit->setPlaceholderText("端口");
    m_userEdit->setPlaceholderText("用户名：字母/数字/_/-");
    m_passwordEdit->setPlaceholderText("密码");

    QFormLayout *form = new QFormLayout;
    form->addRow("服务器", m_hostEdit);
    form->addRow("端口", m_portEdit);
    form->addRow("用户名", m_userEdit);
    form->addRow("密码", m_passwordEdit);

    m_connectButton = new QPushButton("连接服务器");
    m_loginButton = new QPushButton("登录");
    m_registerButton = new QPushButton("注册");
    m_loginButton->setDefault(true);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_connectButton);
    buttons->addWidget(m_registerButton);
    buttons->addWidget(m_loginButton);

    m_statusLabel = new QLabel("先启动后端 server，再连接登录。");
    m_statusLabel->setObjectName("StatusLabel");
    m_statusLabel->setWordWrap(true);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addWidget(subtitle, 0, Qt::AlignHCenter);
    layout->addSpacing(16);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_statusLabel);
    layout->addStretch();

    connect(m_connectButton, &QPushButton::clicked, this, &LoginWindow::doConnect);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::doLogin);
    connect(m_registerButton, &QPushButton::clicked, this, &LoginWindow::doRegister);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::doLogin);

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
    m_statusLabel->setText("正在登录...");
    m_client->login(m_userEdit->text(), m_passwordEdit->text());
}

void LoginWindow::doRegister()
{
    if (!ensureConnected()) {
        return;
    }
    m_statusLabel->setText("正在注册...");
    m_client->registerUser(m_userEdit->text(), m_passwordEdit->text());
}

void LoginWindow::onConnected()
{
    m_statusLabel->setText("服务器已连接，可以登录或注册。");
}

void LoginWindow::onError(const QString &message)
{
    m_statusLabel->setText("连接错误：" + message);
}

void LoginWindow::onLoginSucceeded(const QString &username)
{
    m_statusLabel->setText("登录成功：" + username);
    emit loginAccepted(username);
}

void LoginWindow::onLoginFailed(const QString &message)
{
    m_statusLabel->setText("登录失败：" + message);
}

void LoginWindow::onRegisterSucceeded(const QString &message)
{
    m_statusLabel->setText("注册成功：" + message + "。现在可以登录。");
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
    m_statusLabel->setText("已发起连接，请连接成功后再点一次登录/注册。");
    return false;
}
