#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class NetworkClient;

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(NetworkClient *client, QWidget *parent = nullptr);

signals:
    void loginAccepted(const QString &displayName);

private slots:
    void doConnect();
    void doLogin();
    void doRegister();
    void onConnected();
    void onError(const QString &message);
    void onLoginSucceeded(const QString &displayName);
    void onLoginFailed(const QString &message);
    void onRegisterSucceeded(const QString &message);
    void onRegisterFailed(const QString &message);

private:
    NetworkClient *m_client;
    QTabWidget *m_tabs;
    QLineEdit *m_hostEdit;
    QLineEdit *m_portEdit;
    QLineEdit *m_loginEdit;
    QLineEdit *m_loginPasswordEdit;
    QLineEdit *m_registerAccountEdit;
    QLineEdit *m_registerNicknameEdit;
    QLineEdit *m_registerPasswordEdit;
    QLabel *m_statusLabel;
    QPushButton *m_connectButton;
    QPushButton *m_loginButton;
    QPushButton *m_registerButton;

    bool ensureConnected();
    bool validateAccount(const QString &account) const;
    bool validatePassword(const QString &password) const;
};

#endif
