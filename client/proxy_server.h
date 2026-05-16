#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include "../common/serial_link.h"

class ProxyServer : public QObject {
    Q_OBJECT
public:
    explicit ProxyServer(SerialLink *link, QObject *parent = nullptr);
    ~ProxyServer();
    bool listen(QString addr, quint16 port);
    quint16 serverPort() const { return m_server.serverPort(); }

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onFrame(quint32 connId, quint8 type, QByteArray payload);

private:
    enum State { StWaitHeaders, StWaitConnect, StRelay };

    struct Conn {
        QTcpSocket *sock = nullptr;
        State       state = StWaitHeaders;
        bool        isConnect = false;
        QByteArray  pending;   // что отослать на сервер после FR_CONNECTED
        QByteArray  headerBuf; // накопитель HTTP-заголовков
    };

    void handleHeaders(quint32 id, Conn &c);
    void closeConn(quint32 id, bool notifyRemote);

    SerialLink               *m_link;
    QTcpServer                m_server;
    QHash<quint32, Conn>      m_conns;
    QHash<QTcpSocket*, quint32> m_rev;
    quint32                   m_nextId = 1;
    void onClientReadyRead_(QTcpSocket *s);
};
#endif
