#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QStatusBar>
#include <QThread>
#include <QScrollBar>
#include <QMessageBox>

// ── Windows / Bluetooth headers ────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")

#include <QString>
#include <QList>
#include <atomic>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Protocol constants (mirrors Harbinger proto.h)
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint8_t MSG_PING        = 0x01;
constexpr uint8_t MSG_AIM         = 0x02;
constexpr uint8_t MSG_ARM         = 0x03;
constexpr uint8_t MSG_SET_VOLTAGE = 0x04;
constexpr uint8_t MSG_FIRE        = 0x05;
constexpr uint8_t MSG_PONG        = 0x81;
constexpr uint8_t MSG_STATE       = 0x82;
constexpr uint8_t MSG_TELEMETRY   = 0x83;
constexpr uint8_t MSG_SHOT        = 0x84;

constexpr uint8_t ARM_SHIFT_MASTER = 0;
constexpr uint8_t ARM_SHIFT_TURRET = 2;
constexpr uint8_t ARM_SHIFT_GUN    = 4;
constexpr uint8_t ARM_FALSE        = 0x01;
constexpr uint8_t ARM_TRUE         = 0x02;
constexpr uint8_t STATE_MASTER_ARM = 0x01;
constexpr uint8_t STATE_TURRET_ARM = 0x02;
constexpr uint8_t STATE_GUN_ARM    = 0x04;

#pragma pack(push, 1)
struct PktAim        { float heading; float elevation; };
struct PktArm        { uint8_t flags; };
struct PktSetVoltage { float voltage; };
struct PktState      { uint8_t flags; float target_v; };
struct PktTelemetry  { float heading; float elevation;
                       float motorA_vel; float motorA_acc;
                       float motorB_vel; float motorB_acc; };
struct PktShotHeader { uint32_t total_shots; uint8_t stage_count; };
struct PktShotStage  { uint32_t t_us; float v_mps; float drain_v; };
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
struct BtDevice {
    QString  name;
    BTH_ADDR address;
    QString  addressStr;
    bool     authenticated;
    bool     remembered;
    bool     connected;
};

// ─────────────────────────────────────────────────────────────────────────────
class ScanWorker : public QObject {
    Q_OBJECT
public slots:
    void scan() {
        QList<BtDevice> found;
        BLUETOOTH_DEVICE_SEARCH_PARAMS params{};
        params.dwSize               = sizeof(params);
        params.fReturnAuthenticated = TRUE;
        params.fReturnRemembered    = TRUE;
        params.fReturnUnknown       = TRUE;
        params.fReturnConnected     = TRUE;
        params.fIssueInquiry        = TRUE;
        params.cTimeoutMultiplier   = 4;
        BLUETOOTH_DEVICE_INFO info{};
        info.dwSize = sizeof(info);
        HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&params, &info);
        if (hFind) {
            do {
                BtDevice dev;
                dev.name          = QString::fromWCharArray(info.szName);
                dev.address       = info.Address.ullLong;
                dev.authenticated = info.fAuthenticated;
                dev.remembered    = info.fRemembered;
                dev.connected     = info.fConnected;
                BTH_ADDR a = info.Address.ullLong;
                dev.addressStr = QString("%1:%2:%3:%4:%5:%6")
                    .arg((a >> 40) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >> 32) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >> 24) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >> 16) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >>  8) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >>  0) & 0xFF, 2, 16, QChar('0'))
                    .toUpper();
                found.append(dev);
            } while (BluetoothFindNextDevice(hFind, &info));
            BluetoothFindDeviceClose(hFind);
        }
        emit scanDone(found);
    }
signals:
    void scanDone(QList<BtDevice> devices);
};

// ─────────────────────────────────────────────────────────────────────────────
// IoWorker: owns the socket, runs binary RX parser on the IO thread.
// Send methods are called DIRECTLY from the main thread — send() is
// thread-safe alongside a concurrent blocking recv() in another thread.
// ─────────────────────────────────────────────────────────────────────────────
class IoWorker : public QObject {
    Q_OBJECT
public:
    explicit IoWorker(SOCKET sock) : m_sock(sock) {}

    void requestStop() { m_stop = true; }

    // Called directly from the main thread — send() is thread-safe.
    void sendPing() {
        uint8_t b = MSG_PING;
        rawSend(&b, 1);
    }
    void sendAim(float heading, float elevation) {
        PktAim pkt { heading, elevation };
        msgSend(MSG_AIM, &pkt, sizeof(pkt));
    }
    void sendArm(uint8_t flags) {
        PktArm pkt { flags };
        msgSend(MSG_ARM, &pkt, sizeof(pkt));
    }
    void sendSetVoltage(float voltage) {
        PktSetVoltage pkt { voltage };
        msgSend(MSG_SET_VOLTAGE, &pkt, sizeof(pkt));
    }
    void sendFire() {
        uint8_t b = MSG_FIRE;
        rawSend(&b, 1);
    }

public slots:
    // Runs on the IO thread; blocks in recv(). Because this never returns,
    // the thread's event loop never starts — so sends must NOT use
    // invokeMethod/QueuedConnection to this object.
    void run() {
        uint8_t buf[512];
        while (!m_stop) {
            int n = recv(m_sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (n > 0) {
                for (int i = 0; i < n; ++i)
                    feedByte(buf[i]);
            } else {
                if (!m_stop) emit disconnected();
                break;
            }
        }
    }

signals:
    void packetReceived(uint8_t type, QByteArray payload);
    void disconnected();

private:
    SOCKET            m_sock = INVALID_SOCKET;
    std::atomic<bool> m_stop{false};

    enum class RxState { TYPE, PAYLOAD, SHOT_STAGES };
    RxState    m_rxState    = RxState::TYPE;
    uint8_t    m_rxType     = 0;
    size_t     m_rxExpected = 0;
    QByteArray m_rxBuf;

    void feedByte(uint8_t b) {
        switch (m_rxState) {
        case RxState::TYPE:
            m_rxType = b;
            m_rxBuf.clear();
            {
                size_t psiz = payloadSize(b);
                if (psiz == SIZE_MAX) break;
                if (b == MSG_SHOT) {
                    m_rxExpected = 5;
                    m_rxState = RxState::PAYLOAD;
                } else if (psiz == 0) {
                    emit packetReceived(b, QByteArray());
                } else {
                    m_rxExpected = psiz;
                    m_rxState = RxState::PAYLOAD;
                }
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

    static size_t payloadSize(uint8_t type) {
        switch (type) {
        case MSG_PONG:      return 0;
        case MSG_STATE:     return 5;
        case MSG_TELEMETRY: return 24;
        case MSG_SHOT:      return 5;
        default:            return SIZE_MAX;
        }
    }

    void rawSend(const uint8_t* data, size_t len) {
        if (m_sock != INVALID_SOCKET)
            send(m_sock, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
    }
    void msgSend(uint8_t type, const void* payload, size_t len) {
        rawSend(&type, 1);
        if (payload && len > 0)
            rawSend(static_cast<const uint8_t*>(payload), len);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            QMessageBox::critical(nullptr, "Fatal", "WSAStartup failed.");
            std::exit(1);
        }
        setWindowTitle("Harbinger Control");
        setMinimumSize(960, 640);
        buildUi();
    }
    ~MainWindow() override { disconnectDevice(); WSACleanup(); }

private:
    QListWidget    *m_deviceList   = nullptr;
    QPushButton    *m_scanBtn      = nullptr;
    QPushButton    *m_connectBtn   = nullptr;
    QPushButton    *m_disconnBtn   = nullptr;
    QLabel         *m_statusLabel  = nullptr;

    QDoubleSpinBox *m_headingSpin   = nullptr;
    QDoubleSpinBox *m_elevationSpin = nullptr;
    QPushButton    *m_aimBtn        = nullptr;
    QCheckBox      *m_masterArmChk  = nullptr;
    QCheckBox      *m_turretArmChk  = nullptr;
    QCheckBox      *m_gunArmChk     = nullptr;
    QPushButton    *m_applyArmBtn   = nullptr;
    QDoubleSpinBox *m_voltageSpin   = nullptr;
    QPushButton    *m_setVoltageBtn = nullptr;
    QPushButton    *m_fireBtn       = nullptr;
    QPushButton    *m_pingBtn       = nullptr;

    QLabel    *m_telHeading   = nullptr;
    QLabel    *m_telElevation = nullptr;
    QLabel    *m_telAVel      = nullptr;
    QLabel    *m_telAAcc      = nullptr;
    QLabel    *m_telBVel      = nullptr;
    QLabel    *m_telBAcc      = nullptr;
    QLabel    *m_stateFlags   = nullptr;
    QLabel    *m_stateVoltage = nullptr;
    QLabel    *m_shotTotal    = nullptr;
    QTextEdit *m_eventLog     = nullptr;

    QList<BtDevice> m_devices;
    SOCKET          m_sock     = INVALID_SOCKET;
    QThread        *m_ioThread = nullptr;
    IoWorker       *m_ioWorker = nullptr;

    void buildUi() {
        auto *root  = new QWidget(this);
        setCentralWidget(root);
        auto *rootH = new QHBoxLayout(root);
        rootH->setContentsMargins(4, 4, 4, 4);
        rootH->setSpacing(6);

        // Left: device list
        auto *leftPanel = new QWidget;
        leftPanel->setFixedWidth(240);
        auto *leftV = new QVBoxLayout(leftPanel);
        leftV->setContentsMargins(0, 0, 0, 0);
        leftV->addWidget(new QLabel("Bluetooth Devices"));
        m_deviceList = new QListWidget;
        leftV->addWidget(m_deviceList, 1);
        m_scanBtn    = new QPushButton("Scan");
        m_connectBtn = new QPushButton("Connect");
        m_disconnBtn = new QPushButton("Disconnect");
        m_connectBtn->setEnabled(false);
        m_disconnBtn->setEnabled(false);
        leftV->addWidget(m_scanBtn);
        leftV->addWidget(m_connectBtn);
        leftV->addWidget(m_disconnBtn);
        rootH->addWidget(leftPanel);

        // Right: controls + data
        auto *rightPanel = new QWidget;
        auto *rightV     = new QVBoxLayout(rightPanel);
        rightV->setContentsMargins(0, 0, 0, 0);

        auto *ctrlRow = new QHBoxLayout;

        auto *aimGroup = new QGroupBox("Aim (MSG_AIM)");
        auto *aimGrid  = new QGridLayout(aimGroup);
        aimGrid->addWidget(new QLabel("Heading:"), 0, 0);
        m_headingSpin = new QDoubleSpinBox;
        m_headingSpin->setRange(-180.0, 180.0);
        m_headingSpin->setDecimals(1);
        m_headingSpin->setSuffix("\xc2\xb0");
        aimGrid->addWidget(m_headingSpin, 0, 1);
        aimGrid->addWidget(new QLabel("Elevation:"), 1, 0);
        m_elevationSpin = new QDoubleSpinBox;
        m_elevationSpin->setRange(-90.0, 90.0);
        m_elevationSpin->setDecimals(1);
        m_elevationSpin->setSuffix("\xc2\xb0");
        aimGrid->addWidget(m_elevationSpin, 1, 1);
        m_aimBtn = new QPushButton("Send Aim");
        m_aimBtn->setEnabled(false);
        aimGrid->addWidget(m_aimBtn, 2, 0, 1, 2);
        ctrlRow->addWidget(aimGroup);

        auto *armGroup = new QGroupBox("Arm (MSG_ARM)");
        auto *armV     = new QVBoxLayout(armGroup);
        m_masterArmChk = new QCheckBox("Master Arm");
        m_turretArmChk = new QCheckBox("Turret Arm");
        m_gunArmChk    = new QCheckBox("Gun Arm");
        m_applyArmBtn  = new QPushButton("Apply");
        m_applyArmBtn->setEnabled(false);
        armV->addWidget(m_masterArmChk);
        armV->addWidget(m_turretArmChk);
        armV->addWidget(m_gunArmChk);
        armV->addWidget(m_applyArmBtn);
        ctrlRow->addWidget(armGroup);

        auto *vfGroup = new QGroupBox("Voltage / Fire");
        auto *vfGrid  = new QGridLayout(vfGroup);
        vfGrid->addWidget(new QLabel("Voltage:"), 0, 0);
        m_voltageSpin = new QDoubleSpinBox;
        m_voltageSpin->setRange(0.0, 120.0);
        m_voltageSpin->setDecimals(1);
        m_voltageSpin->setSuffix(" V");
        vfGrid->addWidget(m_voltageSpin, 0, 1);
        m_setVoltageBtn = new QPushButton("Set Voltage (MSG_SET_VOLTAGE)");
        m_setVoltageBtn->setEnabled(false);
        vfGrid->addWidget(m_setVoltageBtn, 1, 0, 1, 2);
        m_fireBtn = new QPushButton("FIRE (MSG_FIRE)");
        m_fireBtn->setEnabled(false);
        vfGrid->addWidget(m_fireBtn, 2, 0, 1, 2);
        ctrlRow->addWidget(vfGroup);

        auto *miscGroup = new QGroupBox("Misc");
        auto *miscV     = new QVBoxLayout(miscGroup);
        m_pingBtn = new QPushButton("Ping (MSG_PING)");
        m_pingBtn->setEnabled(false);
        miscV->addWidget(m_pingBtn);
        miscV->addStretch();
        ctrlRow->addWidget(miscGroup);

        rightV->addLayout(ctrlRow);

        auto *infoRow  = new QHBoxLayout;

        auto *telGroup = new QGroupBox("Telemetry (MSG_TELEMETRY, 250ms)");
        auto *telGrid  = new QGridLayout(telGroup);
        auto addTelRow = [&](int row, const QString &lbl, QLabel *&out) {
            telGrid->addWidget(new QLabel(lbl), row, 0);
            out = new QLabel("--");
            telGrid->addWidget(out, row, 1);
        };
        addTelRow(0, "Heading:",     m_telHeading);
        addTelRow(1, "Elevation:",   m_telElevation);
        addTelRow(2, "Motor A vel:", m_telAVel);
        addTelRow(3, "Motor A acc:", m_telAAcc);
        addTelRow(4, "Motor B vel:", m_telBVel);
        addTelRow(5, "Motor B acc:", m_telBAcc);
        infoRow->addWidget(telGroup);

        auto *stateGroup = new QGroupBox("Device State (MSG_STATE)");
        auto *stateGrid  = new QGridLayout(stateGroup);
        stateGrid->addWidget(new QLabel("Armed:"),          0, 0);
        m_stateFlags = new QLabel("--");
        stateGrid->addWidget(m_stateFlags, 0, 1);
        stateGrid->addWidget(new QLabel("Target voltage:"), 1, 0);
        m_stateVoltage = new QLabel("--");
        stateGrid->addWidget(m_stateVoltage, 1, 1);
        stateGrid->addWidget(new QLabel("Total shots:"),    2, 0);
        m_shotTotal = new QLabel("--");
        stateGrid->addWidget(m_shotTotal, 2, 1);
        stateGrid->setRowStretch(3, 1);
        infoRow->addWidget(stateGroup);

        rightV->addLayout(infoRow);

        rightV->addWidget(new QLabel("Event Log:"));
        m_eventLog = new QTextEdit;
        m_eventLog->setReadOnly(true);
        rightV->addWidget(m_eventLog, 1);

        rootH->addWidget(rightPanel, 1);

        m_statusLabel = new QLabel("  ready");
        statusBar()->addWidget(m_statusLabel, 1);

        connect(m_scanBtn,       &QPushButton::clicked, this, &MainWindow::startScan);
        connect(m_connectBtn,    &QPushButton::clicked, this, &MainWindow::connectSelected);
        connect(m_disconnBtn,    &QPushButton::clicked, this, &MainWindow::disconnectDevice);
        connect(m_pingBtn,       &QPushButton::clicked, this, &MainWindow::onPing);
        connect(m_aimBtn,        &QPushButton::clicked, this, &MainWindow::onAim);
        connect(m_applyArmBtn,   &QPushButton::clicked, this, &MainWindow::onArm);
        connect(m_setVoltageBtn, &QPushButton::clicked, this, &MainWindow::onSetVoltage);
        connect(m_fireBtn,       &QPushButton::clicked, this, &MainWindow::onFire);
        connect(m_deviceList, &QListWidget::currentRowChanged, [this](int row) {
            m_connectBtn->setEnabled(row >= 0 && m_sock == INVALID_SOCKET);
        });
    }

    void startScan() {
        m_scanBtn->setEnabled(false);
        m_deviceList->clear();
        m_devices.clear();
        setStatus("scanning\xe2\x80\xa6 (\xe2\x89\x885 s)");
        auto *thread = new QThread(this);
        auto *worker = new ScanWorker;
        worker->moveToThread(thread);
        connect(thread, &QThread::started, worker, &ScanWorker::scan);
        connect(worker, &ScanWorker::scanDone, this, [this, thread, worker](QList<BtDevice> devs) {
            m_devices = devs;
            m_deviceList->clear();
            for (const auto &d : devs) {
                m_deviceList->addItem(new QListWidgetItem(
                    QString("%1\n%2%3")
                        .arg(d.name.isEmpty() ? "(unknown)" : d.name)
                        .arg(d.addressStr)
                        .arg(d.connected ? " [connected]" : "")));
            }
            setStatus(devs.isEmpty()
                ? "scan complete \xe2\x80\x94 no devices found"
                : QString("scan complete \xe2\x80\x94 %1 device(s)").arg(devs.size()));
            m_scanBtn->setEnabled(true);
            thread->quit(); worker->deleteLater(); thread->deleteLater();
        });
        thread->start();
    }

    void connectSelected() {
        int row = m_deviceList->currentRow();
        if (row < 0 || row >= m_devices.size()) return;
        const BtDevice &dev = m_devices[row];
        setStatus(QString("connecting to %1\xe2\x80\xa6").arg(dev.name));
        m_connectBtn->setEnabled(false);
        m_scanBtn->setEnabled(false);

        SOCKADDR_BTH addr{};
        addr.addressFamily  = AF_BTH;
        addr.btAddr         = dev.address;
        addr.serviceClassId = RFCOMM_PROTOCOL_UUID;
        addr.port           = BT_PORT_ANY;

        SOCKET sock = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (sock == INVALID_SOCKET) {
            setStatus("socket() failed: " + QString::number(WSAGetLastError()));
            m_connectBtn->setEnabled(true); m_scanBtn->setEnabled(true);
            return;
        }
        auto *thread = new QThread(this);
        connect(thread, &QThread::started, [=]() {
            int res = ::connect(sock,
                reinterpret_cast<SOCKADDR*>(const_cast<SOCKADDR_BTH*>(&addr)), sizeof(addr));
            QMetaObject::invokeMethod(this, [=]() {
                thread->quit(); thread->deleteLater();
                if (res == SOCKET_ERROR) {
                    setStatus("connect() failed: " + QString::number(WSAGetLastError()));
                    closesocket(sock);
                    m_connectBtn->setEnabled(true); m_scanBtn->setEnabled(true);
                } else {
                    onConnected(sock, dev.name);
                }
            }, Qt::QueuedConnection);
        });
        thread->start();
    }

    void onConnected(SOCKET sock, const QString &name) {
        m_sock = sock;
        setStatus("connected \xe2\x80\x94 " + name);
        setControlsEnabled(true);
        logEvent(QString("=== Connected to %1 ===").arg(name));

        m_ioThread = new QThread(this);
        m_ioWorker = new IoWorker(sock);
        m_ioWorker->moveToThread(m_ioThread);

        connect(m_ioThread, &QThread::started,         m_ioWorker, &IoWorker::run);
        connect(m_ioWorker, &IoWorker::packetReceived, this,       &MainWindow::handlePacket);
        connect(m_ioWorker, &IoWorker::disconnected,   this,       [this]() {
            logEvent("=== Remote disconnected ===");
            disconnectDevice();
        });
        m_ioThread->start();
    }

    void disconnectDevice() {
        if (m_ioWorker) m_ioWorker->requestStop();
        if (m_ioThread) {
            if (m_sock != INVALID_SOCKET) shutdown(m_sock, SD_BOTH);
            m_ioThread->quit();
            m_ioThread->wait(2000);
            delete m_ioWorker; m_ioWorker = nullptr;
            delete m_ioThread; m_ioThread = nullptr;
        }
        if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
        setControlsEnabled(false);
        m_connectBtn->setEnabled(m_deviceList->currentRow() >= 0);
        m_scanBtn->setEnabled(true);
        setStatus("disconnected \xe2\x80\x94 ready");
    }

    void setControlsEnabled(bool en) {
        m_disconnBtn->setEnabled(en);
        m_scanBtn->setEnabled(!en);
        m_pingBtn->setEnabled(en);
        m_aimBtn->setEnabled(en);
        m_applyArmBtn->setEnabled(en);
        m_setVoltageBtn->setEnabled(en);
        m_fireBtn->setEnabled(en);
        if (!en) m_connectBtn->setEnabled(m_deviceList->currentRow() >= 0);
    }

    // ── Send: called directly from main thread (send() is thread-safe) ─────────
    void onPing() {
        if (!m_ioWorker) return;
        logEvent("\xe2\x86\x92 PING");
        m_ioWorker->sendPing();
    }
    void onAim() {
        if (!m_ioWorker) return;
        float h = static_cast<float>(m_headingSpin->value());
        float e = static_cast<float>(m_elevationSpin->value());
        logEvent(QString("\xe2\x86\x92 AIM heading=%1 elevation=%2").arg(h,0,'f',1).arg(e,0,'f',1));
        m_ioWorker->sendAim(h, e);
    }
    void onArm() {
        if (!m_ioWorker) return;
        auto enc = [](bool v) -> uint8_t { return v ? ARM_TRUE : ARM_FALSE; };
        uint8_t flags = static_cast<uint8_t>(
            (enc(m_masterArmChk->isChecked()) << ARM_SHIFT_MASTER) |
            (enc(m_turretArmChk->isChecked()) << ARM_SHIFT_TURRET) |
            (enc(m_gunArmChk->isChecked())    << ARM_SHIFT_GUN));
        logEvent(QString("\xe2\x86\x92 ARM flags=0x%1 (master=%2 turret=%3 gun=%4)")
            .arg(flags, 2, 16, QChar('0'))
            .arg(m_masterArmChk->isChecked() ? 1 : 0)
            .arg(m_turretArmChk->isChecked() ? 1 : 0)
            .arg(m_gunArmChk->isChecked()    ? 1 : 0));
        m_ioWorker->sendArm(flags);
    }
    void onSetVoltage() {
        if (!m_ioWorker) return;
        float v = static_cast<float>(m_voltageSpin->value());
        logEvent(QString("\xe2\x86\x92 SET_VOLTAGE %1 V").arg(v, 0, 'f', 1));
        m_ioWorker->sendSetVoltage(v);
    }
    void onFire() {
        if (!m_ioWorker) return;
        logEvent("\xe2\x86\x92 FIRE");
        m_ioWorker->sendFire();
    }

    // ── Receive: called on main thread via queued signal ──────────────────────
    void handlePacket(uint8_t type, QByteArray payload) {
        switch (type) {
        case MSG_PONG:
            logEvent("\xe2\x86\x90 PONG");
            break;

        case MSG_STATE: {
            if (payload.size() < static_cast<int>(sizeof(PktState))) break;
            PktState pkt;
            memcpy(&pkt, payload.constData(), sizeof(pkt));
            QStringList armed;
            if (pkt.flags & STATE_MASTER_ARM) armed << "MASTER";
            if (pkt.flags & STATE_TURRET_ARM) armed << "TURRET";
            if (pkt.flags & STATE_GUN_ARM)    armed << "GUN";
            QString armedStr = armed.isEmpty() ? "none" : armed.join(" ");
            m_stateFlags->setText(armedStr);
            m_stateVoltage->setText(QString("%1 V").arg(pkt.target_v, 0, 'f', 1));
            m_masterArmChk->setChecked(pkt.flags & STATE_MASTER_ARM);
            m_turretArmChk->setChecked(pkt.flags & STATE_TURRET_ARM);
            m_gunArmChk->setChecked(pkt.flags    & STATE_GUN_ARM);
            m_voltageSpin->setValue(pkt.target_v);
            logEvent(QString("\xe2\x86\x90 STATE arm=[%1] target_v=%2 V").arg(armedStr).arg(pkt.target_v,0,'f',1));
            break;
        }

        case MSG_TELEMETRY: {
            if (payload.size() < static_cast<int>(sizeof(PktTelemetry))) break;
            PktTelemetry pkt;
            memcpy(&pkt, payload.constData(), sizeof(pkt));
            m_telHeading->setText(QString("%1\xc2\xb0").arg(pkt.heading,   0, 'f', 2));
            m_telElevation->setText(QString("%1\xc2\xb0").arg(pkt.elevation, 0, 'f', 2));
            m_telAVel->setText(QString("%1").arg(pkt.motorA_vel, 0, 'f', 3));
            m_telAAcc->setText(QString("%1").arg(pkt.motorA_acc, 0, 'f', 3));
            m_telBVel->setText(QString("%1").arg(pkt.motorB_vel, 0, 'f', 3));
            m_telBAcc->setText(QString("%1").arg(pkt.motorB_acc, 0, 'f', 3));
            // Log briefly so it's visible; suppress repeating identical lines
            QString telLine = QString("\xe2\x86\x90 TEL h=%1 e=%2 Avel=%3 Bvel=%4")
                .arg(pkt.heading,   0, 'f', 1).arg(pkt.elevation, 0, 'f', 1)
                .arg(pkt.motorA_vel, 0, 'f', 2).arg(pkt.motorB_vel, 0, 'f', 2);
            static QString lastTelLine;
            if (telLine != lastTelLine) { logEvent(telLine); lastTelLine = telLine; }
            break;
        }

        case MSG_SHOT: {
            if (payload.size() < static_cast<int>(sizeof(PktShotHeader))) break;
            PktShotHeader hdr;
            memcpy(&hdr, payload.constData(), sizeof(hdr));
            m_shotTotal->setText(QString::number(hdr.total_shots));
            QString msg = QString("\xe2\x86\x90 SHOT total=%1 stages=%2").arg(hdr.total_shots).arg(hdr.stage_count);
            int offset = static_cast<int>(sizeof(PktShotHeader));
            for (uint8_t i = 0; i < hdr.stage_count; ++i, offset += 12) {
                if (offset + 12 > payload.size()) break;
                PktShotStage stage;
                memcpy(&stage, payload.constData() + offset, sizeof(stage));
                msg += QString("\n  [%1] t=%2us v=%3m/s drain=%4V")
                    .arg(i).arg(stage.t_us).arg(stage.v_mps,0,'f',2).arg(stage.drain_v,0,'f',2);
            }
            logEvent(msg);
            break;
        }

        default:
            logEvent(QString("\xe2\x86\x90 unknown 0x%1 (%2 B)").arg(type,2,16,QChar('0')).arg(payload.size()));
            break;
        }
    }

    void logEvent(const QString &msg) {
        m_eventLog->append(msg);
        m_eventLog->verticalScrollBar()->setValue(m_eventLog->verticalScrollBar()->maximum());
    }
    void setStatus(const QString &msg) { m_statusLabel->setText("  " + msg); }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow win;
    win.show();
    return app.exec();
}

#include "main.moc"
