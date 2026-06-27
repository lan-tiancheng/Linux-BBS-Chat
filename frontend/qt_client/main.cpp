#include "loginwindow.h"
#include "mainwindow.h"
#include "networkclient.h"

#include <QApplication>
#include <QFile>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QFile styleFile(":/style.qss");
    if (styleFile.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    NetworkClient client;
    LoginWindow login(&client);
    MainWindow *mainWindow = nullptr;

    QObject::connect(&login, &LoginWindow::loginAccepted,
                     [&](const QString &username) {
        mainWindow = new MainWindow(&client, username);
        mainWindow->show();
        login.hide();
    });

    QObject::connect(&app, &QApplication::aboutToQuit,
                     [&]() { client.disconnectFromServer(); });

    login.show();
    return app.exec();
}
