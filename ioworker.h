#pragma once
#include <QObject>
#include <QByteArray>
#include <winsock2.h>
#include <atomic>
#include "protocol.h"

// Owns the Bluetooth socket. run() blocks on recv() on the IO thread.
// Send methods are called from the main thread and are thread-safe alongside
// a concurrent blocking recv().
class IoWorker : public QObject {
    Q_OBJECT
public:
    explicit IoWorker(SOCKET sock);
    void requestStop();

    void sendPing();
    void sendAim(float h, float e);
    void sendArm(uint8_t flags);
    void sendSetVoltage(float v);
    void sendFire();
    void sendClearCalibration();

public slots:
    void run();

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

    void feedByte(uint8_t b);
    static size_t payloadSize(uint8_t type);
    void rawSend(const uint8_t *data, size_t len);
    void msgSend(uint8_t type, const void *payload, size_t len);
};
