#pragma once
#include <QObject>
#include <QByteArray>
#include <winsock2.h>
#include <atomic>
#include <cstring>
#include <limits>
#include "protocol.h"

// Owns the Bluetooth socket.  run() blocks on recv() on the IO thread.
// Send methods are called from the main thread — send() is thread-safe
// alongside a concurrent blocking recv() in another thread.
class IoWorker : public QObject {
    Q_OBJECT
public:
    explicit IoWorker(SOCKET sock) : m_sock(sock) {}
    void requestStop() { m_stop = true; }

    void sendPing()               { uint8_t b = MSG_PING; rawSend(&b, 1); }
    void sendAim(float h, float e){ PktAim p{h, e}; msgSend(MSG_AIM, &p, sizeof(p)); }
    void sendArm(uint8_t f)       { PktArm p{f};    msgSend(MSG_ARM, &p, sizeof(p)); }
    void sendSetVoltage(float v)  { PktSetVoltage p{v}; msgSend(MSG_SET_VOLTAGE, &p, sizeof(p)); }
    void sendFire()               { uint8_t b = MSG_FIRE; rawSend(&b, 1); }

public slots:
    void run() {
        uint8_t buf[512];
        while (!m_stop) {
            int n = recv(m_sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (n > 0) { for (int i = 0; i < n; ++i) feedByte(buf[i]); }
            else       { if (!m_stop) emit disconnected(); break; }
        }
    }

signals:
    void packetReceived(uint8_t type, QByteArray payload);
    void disconnected();

private:
    SOCKET            m_sock;
    std::atomic<bool> m_stop{false};

    enum class RxState { TYPE, PAYLOAD, SHOT_STAGES };
    RxState    m_rxState    = RxState::TYPE;
    uint8_t    m_rxType     = 0;
    size_t     m_rxExpected = 0;
    QByteArray m_rxBuf;

    void feedByte(uint8_t b) {
        switch (m_rxState) {
        case RxState::TYPE:
            m_rxType = b; m_rxBuf.clear();
            {
                size_t psiz = payloadSize(b);
                if (psiz == SIZE_MAX) break;
                if (b == MSG_SHOT)  { m_rxExpected = 5; m_rxState = RxState::PAYLOAD; }
                else if (b == MSG_LOG) { m_rxExpected = 2; m_rxState = RxState::PAYLOAD; }
                else if (psiz == 0) { emit packetReceived(b, QByteArray()); }
                else                { m_rxExpected = psiz; m_rxState = RxState::PAYLOAD; }
            }
            break;
        case RxState::PAYLOAD:
            m_rxBuf.append(static_cast<char>(b));
            if (static_cast<size_t>(m_rxBuf.size()) >= m_rxExpected) {
                if (m_rxType == MSG_SHOT) {
                    PktShotHeader hdr;
                    memcpy(&hdr, m_rxBuf.constData(), sizeof(hdr));
                    if (hdr.stage_count == 0) {
                        emit packetReceived(MSG_SHOT, m_rxBuf); m_rxState = RxState::TYPE;
                    } else {
                        m_rxExpected = 5 + static_cast<size_t>(hdr.stage_count) * 12;
                        m_rxState = RxState::SHOT_STAGES;
                    }
                } else if (m_rxType == MSG_LOG && static_cast<size_t>(m_rxBuf.size()) == 2) {
                    // Header received: byte 0=level, byte 1=slen.
                    // If slen>0 stay in PAYLOAD and read slen more bytes.
                    uint8_t slen = static_cast<uint8_t>(m_rxBuf.at(1));
                    if (slen > 0) {
                        m_rxExpected = 2 + slen;
                    } else {
                        emit packetReceived(MSG_LOG, m_rxBuf); m_rxState = RxState::TYPE;
                    }
                } else {
                    emit packetReceived(m_rxType, m_rxBuf); m_rxState = RxState::TYPE;
                }
            }
            break;
        case RxState::SHOT_STAGES:
            m_rxBuf.append(static_cast<char>(b));
            if (static_cast<size_t>(m_rxBuf.size()) >= m_rxExpected) {
                emit packetReceived(MSG_SHOT, m_rxBuf); m_rxState = RxState::TYPE;
            }
            break;
        }
    }

    static size_t payloadSize(uint8_t t) {
        switch (t) {
        case MSG_PONG:      return 0;
        case MSG_STATE:     return 5;
        case MSG_TELEMETRY: return 24;
        case MSG_SHOT:      return 5;
        case MSG_LOG:       return 2;  // header only; extended after slen is known
        default:            return SIZE_MAX;
        }
    }

    void rawSend(const uint8_t *d, size_t n) {
        if (m_sock != INVALID_SOCKET)
            send(m_sock, reinterpret_cast<const char*>(d), static_cast<int>(n), 0);
    }
    void msgSend(uint8_t t, const void *p, size_t n) {
        rawSend(&t, 1);
        if (p && n) rawSend(static_cast<const uint8_t*>(p), n);
    }
};
