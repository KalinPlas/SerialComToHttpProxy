#ifndef SERIAL_LINK_H
#define SERIAL_LINK_H

#include <QObject>
#include <QSerialPort>
#include <QByteArray>

enum FrameType : quint8 {
    FR_CONNECT    = 1, // клиент -> сервер : "host:port"
    FR_CONNECTED  = 2, // сервер -> клиент : соединение открыто
    FR_CONN_FAIL  = 3, // сервер -> клиент : ошибка открытия
    FR_DATA       = 4, // оба направления : полезные данные
    FR_CLOSE      = 5, // оба направления : закрыть соединение
};

class SerialLink : public QObject {
    Q_OBJECT
public:
    static constexpr quint32 MAX_PAYLOAD = 64 * 1024;
    explicit SerialLink(QObject *parent = nullptr);
    bool open(const QString &portName, qint32 baud = 115200);
    void close();
    bool isOpen() const { return m_port.isOpen(); }

    void sendFrame(quint32 connId, quint8 type,
                   const QByteArray &payload = {});

    qint64 pendingBytes() const;

    bool isCongested() const;

signals:
    void frameReceived(quint32 connId, quint8 type, QByteArray payload);
    void linkError(QString msg);
    void drained();

private slots:
    void onReadyRead();
    void pendFlush();
    void onBytesWritten(qint64);

private:
    void parseBuffer();
    void compactRx();

    static constexpr qint64 HIGH_WATER = 256 * 1024;
    static constexpr qint64 LOW_WATER  =  64 * 1024;

    bool        m_flushPending = false;
    bool        m_wasCongested = false;
    int         m_rxPos = 0;
    QSerialPort m_port;
    QByteArray  m_rxBuf;
};
#endif
