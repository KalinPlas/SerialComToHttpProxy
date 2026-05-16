#include "server.h"
#include <QDebug>

Server::Server(SerialLink *link, QObject *parent)
    : QObject(parent), m_link(link)
{
    connect(link, &SerialLink::frameReceived, this, &Server::onFrame);
    connect(link, &SerialLink::drained, this, [this]{
        for (auto *s : qAsConst(m_sockets))
            if (s && s->bytesAvailable() > 0)
                QMetaObject::invokeMethod(s, [this, s]{ onSocketReadyRead_(s); }, Qt::QueuedConnection);
    });
}

void Server::onFrame(quint32 id, quint8 type, QByteArray payload) {
    switch (type) {
    case FR_CONNECT: {
        QString hp  = QString::fromUtf8(payload);
        int colon   = hp.lastIndexOf(':');
        if (colon <= 0) {
            m_link->sendFrame(id, FR_CONN_FAIL, "bad target");
            return;
        }
        QString host = hp.left(colon);
        quint16 port = hp.mid(colon + 1).toUShort();
        qInfo() << "[srv] conn" << id << "->" << host << ":" << port;

        if (auto *old = m_sockets.take(id)) {
            m_rev.remove(old);
            old->disconnect(this);
            old->abort();
            old->deleteLater();
        }

        auto *s = new QTcpSocket(this);
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        s->setReadBufferSize(1 * 1024 * 1024);
        m_sockets[id] = s;
        m_rev[s]      = id;
        connect(s, &QTcpSocket::connected,    this, &Server::onSocketConnected);
        connect(s, &QTcpSocket::readyRead,    this, &Server::onSocketReadyRead);
        connect(s, &QTcpSocket::disconnected, this, &Server::onSocketDisconnected);
        connect(s, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onSocketError(QAbstractSocket::SocketError)));
        s->connectToHost(host, port);
        break;
    }
    case FR_DATA:
        if (auto *s = m_sockets.value(id))
        {
            s->write(payload);
            s->flush();
        }
        break;
    case FR_CLOSE:
        closeConn(id);
        break;
    }
}

void Server::onSocketConnected() {
    auto *s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    quint32 id = m_rev.value(s, 0);
    if (id) m_link->sendFrame(id, FR_CONNECTED);
}

void Server::onSocketReadyRead() {
    auto *s = qobject_cast<QTcpSocket*>(sender());
    if (s) onSocketReadyRead_(s);
}

void Server::onSocketReadyRead_(QTcpSocket *s) {
    quint32 id = m_rev.value(s, 0);
    if (!id) return;

    constexpr int CHUNK = 64 * 1024;
    while (s->bytesAvailable() > 0) {
        if (m_link->isCongested())
        {
            return;
        }
        QByteArray data = s->read(CHUNK);
        if (data.isEmpty()) break;
        m_link->sendFrame(id, FR_DATA, data);
    }
}

void Server::onSocketDisconnected() {
    auto *s = qobject_cast<QTcpSocket*>(sender());
    if (!s || !m_rev.contains(s)) return;
    quint32 id = m_rev.take(s);
    m_sockets.remove(id);
    while (s->bytesAvailable() > 0) {
        QByteArray data = s->read(64 * 1024);
        m_link->sendFrame(id, FR_DATA, data);
    }
    m_link->sendFrame(id, FR_CLOSE);
    s->deleteLater();
}

void Server::onSocketError(QAbstractSocket::SocketError) {
    auto *s = qobject_cast<QTcpSocket*>(sender());
    if (!s || !m_rev.contains(s)) return;
    if (s->state() != QAbstractSocket::ConnectedState) {
        quint32 id = m_rev.take(s);
        m_sockets.remove(id);
        m_link->sendFrame(id, FR_CONN_FAIL, s->errorString().toUtf8());
        s->disconnect(this);
        s->deleteLater();
    }
}

void Server::closeConn(quint32 id) {
    auto *s = m_sockets.take(id);
    if (!s) return;
    m_rev.remove(s);
    s->disconnect(this);
    if (s->state() != QAbstractSocket::UnconnectedState)
        s->disconnectFromHost();
    s->deleteLater();
}
