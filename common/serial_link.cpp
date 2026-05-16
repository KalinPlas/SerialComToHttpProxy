#include "serial_link.h"
#include <QDataStream>
#include <cstring>

static constexpr quint32 MAGIC = 0xC0DECAFE;
static constexpr int HDR = 13;  // magic(4)+connId(4)+type(1)+len(4)
static constexpr int CRC = 2;

// ---------- CRC-16 (0xA001) с таблицей ----------
static const quint16 *crcTable() {
    static quint16 t[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) {
            quint16 c = i;
            for (int j = 0; j < 8; ++j)
                c = (c >> 1) ^ ((c & 1) ? 0xA001 : 0);
            t[i] = c;
        }
        init = true;
    }
    return t;
}
static quint16 crc16(const char *data, int len) {
    const quint16 *t = crcTable();
    quint16 c = 0xFFFF;
    const uchar *p = reinterpret_cast<const uchar*>(data);
    for (int i = 0; i < len; ++i)
        c = (c >> 8) ^ t[(c ^ p[i]) & 0xFF];
    return c;
}

SerialLink::SerialLink(QObject *p) : QObject(p) {
    connect(&m_port, &QSerialPort::readyRead,     this, &SerialLink::onReadyRead);
    connect(&m_port, &QSerialPort::bytesWritten,  this, &SerialLink::onBytesWritten);
}

bool SerialLink::open(const QString &portName, qint32 baud) {
    m_port.setPortName(portName);
    m_port.setBaudRate(baud);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::Parity::NoParity);
    m_port.setStopBits(QSerialPort::StopBits::OneStop);
    m_port.setFlowControl(QSerialPort::FlowControl::NoFlowControl);
    m_rxBuf.clear();
    m_rxPos = 0;
    return m_port.open(QIODevice::ReadWrite);
}

void SerialLink::close() { m_port.close(); m_rxBuf.clear(); m_rxPos = 0; }

qint64 SerialLink::pendingBytes() const { return m_port.bytesToWrite(); }
bool   SerialLink::isCongested() const { return m_port.bytesToWrite() > HIGH_WATER; }

void SerialLink::sendFrame(quint32 connId, quint8 type, const QByteArray &payload)
{
    int total = payload.size();
    int offset = 0;

    do {
        int chunk = qMin<int>(MAX_PAYLOAD, total - offset);
        QByteArray frame;
        frame.reserve(HDR + chunk + CRC);
        QDataStream ds(&frame, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << MAGIC << connId << type << (quint32)chunk;
        if (chunk > 0)
            ds.writeRawData(payload.constData() + offset, chunk);
        quint16 c = crc16(frame.constData(), frame.size());
        ds << c;
        m_port.write(frame);
        m_port.flush();
        offset += chunk;
    } while (offset < total);
}

void SerialLink::onReadyRead() {
    m_rxBuf.append(m_port.readAll());
    parseBuffer();
    compactRx();
}

void SerialLink::onBytesWritten(qint64) {
    if (m_wasCongested && m_port.bytesToWrite() <= LOW_WATER) {
        m_wasCongested = false;
        emit drained();
    } else if (!m_wasCongested && m_port.bytesToWrite() > HIGH_WATER) {
        m_wasCongested = true;
    }
}

void SerialLink::pendFlush()
{
    if(!m_flushPending)
        return;

    m_port.flush();
    m_flushPending = false;
}

static int findMagic(const char *data, int size, int from) {
    static const uchar first = (MAGIC >> 24) & 0xFF;
    while (from + 4 <= size) {
        const void *p = std::memchr(data + from, first, size - from - 3);
        if (!p) return -1;
        int i = int(static_cast<const char*>(p) - data);
        const uchar *b = reinterpret_cast<const uchar*>(data + i);
        quint32 m = (quint32(b[0]) << 24) | (quint32(b[1]) << 16)
                  | (quint32(b[2]) << 8)  |  b[3];
        if (m == MAGIC) return i;
        from = i + 1;
    }
    return -1;
}

void SerialLink::parseBuffer() {
    while (true) {
        const char *data = m_rxBuf.constData() + m_rxPos;
        int size = m_rxBuf.size() - m_rxPos;

        int idx = findMagic(data, size, 0);
        if (idx < 0) {
            if (size > 3) m_rxPos += size - 3;
            return;
        }
        if (idx > 0) m_rxPos += idx;

        data = m_rxBuf.constData() + m_rxPos;
        size = m_rxBuf.size() - m_rxPos;
        if (size < HDR) return;

        const uchar *b = reinterpret_cast<const uchar*>(data);
        quint32 connId = (quint32(b[4])<<24) | (quint32(b[5])<<16)
                       | (quint32(b[6])<<8)  |  b[7];
        quint8  type   = b[8];
        quint32 len    = (quint32(b[9])<<24) | (quint32(b[10])<<16)
                       | (quint32(b[11])<<8) |  b[12];

        if (len > MAX_PAYLOAD) { m_rxPos += 1; continue; }

        int total = HDR + int(len) + CRC;
        if (size < total) return;

        const uchar *crcp = b + HDR + len;
        quint16 wantCrc = (quint16(crcp[0]) << 8) | crcp[1];
        quint16 gotCrc  = crc16(data, HDR + len);
        if (wantCrc != gotCrc) { m_rxPos += 1; continue; }

        QByteArray payload(data + HDR, int(len));
        m_rxPos += total;
        emit frameReceived(connId, type, payload);
    }
}

void SerialLink::compactRx() {
    if (m_rxPos >= 4096 && m_rxPos * 2 >= m_rxBuf.size()) {
        m_rxBuf.remove(0, m_rxPos);
        m_rxPos = 0;
    }
}
