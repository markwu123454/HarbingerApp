#include "ioworker.h"
#include <cstring>
#include <climits>

IoWorker::IoWorker(SOCKET sock) : m_sock(sock) {}

void IoWorker::requestStop() { m_stop = true; }

void IoWorker::sendPing()             { uint8_t b = MSG_PING;              rawSend(&b, 1); }
void IoWorker::sendFire()             { uint8_t b = MSG_FIRE;              rawSend(&b, 1); }
void IoWorker::sendClearCalibration() { uint8_t b = MSG_CLEAR_CALIBRATION; rawSend(&b, 1); }

void IoWorker::sendAim(float h, float e)  { PktAim p{h, e};   msgSend(MSG_AIM,         &p, sizeof(p)); }
void IoWorker::sendArm(uint8_t flags)     { PktArm p{flags};  msgSend(MSG_ARM,         &p, sizeof(p)); }
void IoWorker::sendSetVoltage(float v)    { PktSetVoltage p{v}; msgSend(MSG_SET_VOLTAGE, &p, sizeof(p)); }

void IoWorker::run() {
    uint8_t buf[512];
    while (!m_stop) {
        int n = recv(m_sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n > 0) { for (int i = 0; i < n; ++i) feedByte(buf[i]); }
        else       { if (!m_stop) emit disconnected(); break; }
    }
}

void IoWorker::feedByte(uint8_t b) {
    switch (m_rxState) {
    case RxState::TYPE:
        m_rxType = b; m_rxBuf.clear();
        {
            size_t psiz = payloadSize(b);
            if (psiz == SIZE_MAX)    break;
            if (b == MSG_SHOT)       { m_rxExpected = 5;    m_rxState = RxState::PAYLOAD; }
            else if (b == MSG_LOG)   { m_rxExpected = 2;    m_rxState = RxState::PAYLOAD; }
            else if (psiz == 0)      { emit packetReceived(b, QByteArray()); }
            else                     { m_rxExpected = psiz; m_rxState = RxState::PAYLOAD; }
        }
        break;

    case RxState::PAYLOAD:
        m_rxBuf.append(static_cast<char>(b));
        if (static_cast<size_t>(m_rxBuf.size()) >= m_rxExpected) {
            if (m_rxType == MSG_SHOT) {
                PktShotHeader hdr;
                memcpy(&hdr, m_rxBuf.constData(), sizeof(hdr));
                if (hdr.stage_count == 0) {
                    emit packetReceived(MSG_SHOT, m_rxBuf);
                    m_rxState = RxState::TYPE;
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
                    emit packetReceived(MSG_LOG, m_rxBuf);
                    m_rxState = RxState::TYPE;
                }
            } else {
                emit packetReceived(m_rxType, m_rxBuf);
                m_rxState = RxState::TYPE;
            }
        }
        break;

    case RxState::SHOT_STAGES:
        m_rxBuf.append(static_cast<char>(b));
        if (static_cast<size_t>(m_rxBuf.size()) >= m_rxExpected) {
            emit packetReceived(MSG_SHOT, m_rxBuf);
            m_rxState = RxState::TYPE;
        }
        break;
    }
}

size_t IoWorker::payloadSize(uint8_t type) {
    switch (type) {
    case MSG_PONG:      return 0;
    case MSG_STATE:     return 5;
    case MSG_TELEMETRY: return 24;
    case MSG_SHOT:      return 5;
    case MSG_LOG:       return 2;  // header only; extended after slen is known
    default:            return SIZE_MAX;
    }
}

void IoWorker::rawSend(const uint8_t *data, size_t len) {
    if (m_sock != INVALID_SOCKET)
        send(m_sock, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
}

void IoWorker::msgSend(uint8_t type, const void *payload, size_t len) {
    rawSend(&type, 1);
    if (payload && len) rawSend(static_cast<const uint8_t*>(payload), len);
}
