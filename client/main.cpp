#include <QCoreApplication>
#include <QCommandLineParser>
#include <QMainWindow>
#include <QToolBar>
#include <QLineEdit>
#include <QAction>
#include <QNetworkProxy>
#include <QDebug>

#include "../common/serial_link.h"
#include "proxy_server.h"

int main(int argc, char *argv[]) {

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("comproxy-client");

    QStringList args = app.arguments();

    QString portName = args[1];
    int baud = args[2].toInt();
    QString addr = args[3];
    int port = args[4].toInt();

    SerialLink link;
    QObject::connect(&link, &SerialLink::linkError,
                     [](const QString &m){ qWarning() << "[link]" << m; });
    if (!link.open(portName, baud)) {
        qCritical() << "Can't open" << portName;
        return 1;
    }

    ProxyServer proxy(&link);
    quint16 lport = port;
    if (!proxy.listen(addr, lport)) {
        qCritical() << "Cannot listen" << addr << ":" << lport;
        return 1;
    }
    qInfo() << "Proxy" << addr << ":" << lport
            << "COM:" << portName;

    return app.exec();
}
