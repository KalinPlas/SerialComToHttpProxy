#ifndef SERVER_H
#define SERVER_H
#include <QObject>
#include <QTcpSocket>
#include <QHash>
#include "../common/serial_link.h"

class Server : public QObject {
    Q_OBJECT
public:
    explicit Server(SerialLink *link, QObject *parent = nullptr);

private slots:
    void onFrame(quint32 connId, quint8 type, QByteArray payload);
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError);

private:
    void closeConn(quint32 id);

    SerialLink                   *m_link;
    QHash<quint32, QTcpSocket*>   m_sockets;
    QHash<QTcpSocket*, quint32>   m_rev;
    void onSocketReadyRead_(QTcpSocket *s);
};
#endif
