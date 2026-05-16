#include "proxy_server.h"
#include <QHostAddress>
#include <QUrl>
#include <QDebug>

ProxyServer::ProxyServer(SerialLink *link, QObject *parent)
    : QObject(parent), m_link(link)
{
    connect(&m_server, &QTcpServer::newConnection,
            this, &ProxyServer::onNewConnection);
    connect(m_link, &SerialLink::frameReceived,
            this, &ProxyServer::onFrame);
    // Возобновляем чтение клиентов после «пробивки» COM-очереди
    connect(m_link, &SerialLink::drained, this, [this]{
        for (auto it = m_conns.begin(); it != m_conns.end(); ++it) {
            if (it.value().sock && it.value().state == StRelay)
                QMetaObject::invokeMethod(this, [this, s=it.value().sock]{
                    if (s) onClientReadyRead_(s);
                }, Qt::QueuedConnection);
        }
    });
}

ProxyServer::~ProxyServer()
{
    for(auto con : m_conns)
    {
        closeConn(m_rev.value(con.sock, 0), true);
    }
}

bool ProxyServer::listen(QString addr, quint16 port) {
    return m_server.listen(QHostAddress(addr), port);
}

void ProxyServer::onNewConnection() {
    while (auto *s = m_server.nextPendingConnection()) {
        quint32 id = m_nextId++;
        Conn c; c.sock = s;
        m_conns.insert(id, c);
        m_rev.insert(s, id);
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        s->setReadBufferSize(1 * 1024 * 1024);
        connect(s, &QTcpSocket::readyRead,    this, &ProxyServer::onClientReadyRead);
        connect(s, &QTcpSocket::disconnected, this, &ProxyServer::onClientDisconnected);
    }
}

void ProxyServer::onClientReadyRead() {
    auto *s = qobject_cast<QTcpSocket*>(sender());
    if (s) onClientReadyRead_(s);
}

void ProxyServer::onClientReadyRead_(QTcpSocket *s) {
    quint32 id = m_rev.value(s, 0);
    if (!id) return;
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    Conn &c = it.value();

    if (m_link->isCongested())
        return;

    constexpr int CHUNK = 64 * 1024;
    while (s->bytesAvailable() > 0 && !m_link->isCongested())
    {
        QByteArray data = s->read(CHUNK);
        if (data.isEmpty()) break;

        switch (c.state) {
        case StWaitHeaders:
            c.headerBuf.append(data);
            handleHeaders(id, c);
            if (!m_conns.contains(id)) return;
            break;
        case StWaitConnect:
            if (c.pending.size() + data.size() > 1 * 1024 * 1024) {
                closeConn(id, true);
                return;
            }
            c.pending.append(data);
            break;
        case StRelay:
            m_link->sendFrame(id, FR_DATA, data);
            break;
        }
    }
}

void ProxyServer::handleHeaders(quint32 id, Conn &c) {
    int hdrEnd = c.headerBuf.indexOf("\r\n\r\n");
    if (hdrEnd < 0) {
        if (c.headerBuf.size() > 128 * 1024)
            closeConn(id, true);
        return;
    }
    QByteArray head = c.headerBuf.left(hdrEnd);
    QByteArray rest = c.headerBuf.mid(hdrEnd + 4);
    c.headerBuf.clear();           // освобождаем

    QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) { closeConn(id, true); return; }

    QByteArray reqLine = lines.first().trimmed();
    QList<QByteArray> parts = reqLine.split(' ');
    if (parts.size() < 3) { closeConn(id, true); return; }

    QByteArray method = parts[0];
    QByteArray target = parts[1];
    QString host; quint16 port = 80;

    if (method == "CONNECT") {
        c.isConnect = true;
        int colon = target.lastIndexOf(':');
        if (colon <= 0) { closeConn(id, true); return; }
        host = QString::fromLatin1(target.left(colon));
        port = target.mid(colon + 1).toUShort();
        // ВАЖНО: сохраняем байты, пришедшие после заголовков (начало TLS)
        c.pending = rest;
    } else {
        c.isConnect = false;
        QUrl u = QUrl::fromEncoded(target);
        if (u.isValid() && !u.host().isEmpty()) {
            host = u.host();
            port = u.port(80);
            QByteArray newTarget = u.path(QUrl::FullyEncoded).toUtf8();
            if (newTarget.isEmpty()) newTarget = "/";
            if (u.hasQuery())
                newTarget += "?" + u.query(QUrl::FullyEncoded).toUtf8();
            lines[0] = method + " " + newTarget + " " + parts[2];
        } else {
            for (int i = 1; i < lines.size(); ++i) {
                QByteArray l = lines[i].trimmed();
                if (l.toLower().startsWith("host:")) {
                    QByteArray h = l.mid(5).trimmed();
                    int cc = h.lastIndexOf(':');
                    // IPv6 в скобках [::1]:80
                    if (cc > 0 && !h.contains('[')) {
                        host = QString::fromLatin1(h.left(cc));
                        port = h.mid(cc + 1).toUShort();
                    } else {
                        host = QString::fromLatin1(h);
                        port = 80;
                    }
                    break;
                }
            }
        }

        QByteArray rebuilt;
        rebuilt.reserve(head.size() + 4 + rest.size());
        for (int i = 0; i < lines.size(); ++i) {
            QByteArray l = lines[i];
            if (l.endsWith('\r')) l.chop(1);
            // Удаляем hop-by-hop заголовки
            QByteArray low = l.toLower();
            if (low.startsWith("proxy-connection:")) continue;
            if (low.startsWith("proxy-authorization:")) continue;
            rebuilt += l;
            rebuilt += "\r\n";
        }
        rebuilt += "\r\n";
        rebuilt += rest;
        c.pending = rebuilt;
    }

    if (host.isEmpty()) { closeConn(id, true); return; }

    qInfo() << "[cli]" << id << method << host << ":" << port;
    c.state = StWaitConnect;
    QByteArray hp = host.toUtf8() + ":" + QByteArray::number(port);
    m_link->sendFrame(id, FR_CONNECT, hp);
}

void ProxyServer::onClientDisconnected() {
    auto *s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    quint32 id = m_rev.value(s, 0);
    if (!id) { s->deleteLater(); return; }
    closeConn(id, true);
}

void ProxyServer::onFrame(quint32 id, quint8 type, QByteArray payload) {
    auto it = m_conns.find(id);
    if (it == m_conns.end()) return;
    Conn &c = it.value();

    switch (type) {
    case FR_CONNECTED:
        if (c.isConnect && c.sock)
        {
            c.sock->write("HTTP/1.1 200 Connection Established\r\n\r\n");
            c.sock->flush();
        }

        c.state = StRelay;
        if (!c.pending.isEmpty()) {
            // Отдельно: CONNECT-pending едет уже ПОСЛЕ 200 OK по relay-каналу
            m_link->sendFrame(id, FR_DATA, c.pending);
            c.pending.clear();
        }
        break;

    case FR_DATA:
        if (c.sock)
        {
            c.sock->write(payload);
            c.sock->flush();
        }
        break;

    case FR_CONN_FAIL:
        if (c.sock && !c.isConnect) {
            c.sock->write("HTTP/1.1 502 Bad Gateway\r\n"
                          "Content-Length: 0\r\nConnection: close\r\n\r\n");
            c.sock->flush();
        }
        closeConn(id, false); // удалённый сам почистит
        break;

    case FR_CLOSE:
        closeConn(id, false);
        break;
    }
}

void ProxyServer::closeConn(quint32 id, bool notifyRemote) {
    auto it = m_conns.find(id);
    if (it == m_conns.end()) {
        if (notifyRemote) m_link->sendFrame(id, FR_CLOSE);
        return;
    }

    Conn c = it.value();
    m_conns.erase(it);

    if (notifyRemote) m_link->sendFrame(id, FR_CLOSE);

    if (c.sock) {
        m_rev.remove(c.sock);
        c.sock->disconnect(this);
        if (c.sock->state() != QAbstractSocket::UnconnectedState)
            c.sock->disconnectFromHost();
        c.sock->deleteLater();
    }
}
