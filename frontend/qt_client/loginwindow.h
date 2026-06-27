#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class NetworkClient;

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(NetworkClient *client, QWidget *parent = nullptr);

signals:
    void loginAccepted(const QString &username);

private slots:
    void doConnect();
    void doLogin();
    void doRegister();
    void onConnected();
    void onError(const QString &message);
    void onLoginSucceeded(const QString &username);
    void onLoginFailed(const QString &message);
    void onRegisterSucceeded(const QString &message);
    void onRegisterFailed(const QString &message);

private:
    NetworkClient *m_client;
    QLineEdit *m_hostEdit;
    QLineEdit *m_portEdit;
    QLineEdit *m_userEdit;
    QLineEdit *m_passwordEdit;
    QLabel *m_statusLabel;
    QPushButton *m_connectButton;
    QPushButton *m_loginButton;
    QPushButton *m_registerButton;

    bool ensureConnected();
};

#endif
