#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include "../common/serial_link.h"
#include "server.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("comproxy-server");

    QStringList args = app.arguments();
    QString portName = args[1];
    int baud = args[2].toInt();

    SerialLink link;
    QObject::connect(&link, &SerialLink::linkError,
                     [](const QString &m){ qWarning() << "[link]" << m; });

    if (!link.open(portName, baud)) {
        qCritical() << "Can't open" << portName;
        return 1;
    }
    qInfo() << "Server start:" << portName
            << "@"  << baud;

    Server server(&link);
    return app.exec();
}
