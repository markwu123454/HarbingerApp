#include "mainwindow.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QSlider>
#include <QTextEdit>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QGuiApplication>
#include <QStyleHints>

#include "protocol.h"
#include "scanworker.h"
#include "ioworker.h"
#include "theme.h"
#include "widgets/aimwidget.h"
#include "widgets/compasswidget.h"
#include "widgets/elevationwidget.h"
#include "widgets/bimotorwidget.h"
#include "widgets/holdfirebutton.h"

#include <cstring>

MainWindow::MainWindow() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        QMessageBox::critical(nullptr, "Fatal", "WSAStartup failed.");
        std::exit(1);
    }
    setWindowTitle("Harbinger");
    setMinimumSize(1000, 660);
    buildUi();
    applyTheme();

    // Re-apply theme whenever the OS switches light ↔ dark
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this]() { applyTheme(); });

    m_autoScanTimer = new QTimer(this);
    m_autoScanTimer->setSingleShot(true);
    connect(m_autoScanTimer, &QTimer::timeout, this, &MainWindow::doAutoScan);
    doAutoScan();
}

MainWindow::~MainWindow() {
    disconnectDevice();
    WSACleanup();
}

// ── UI construction ───────────────────────────────────────────────
void MainWindow::buildUi() {
    auto *root  = new QWidget(this);
    setCentralWidget(root);
    auto *rootV = new QVBoxLayout(root);
    rootV->setContentsMargins(0, 0, 0, 0);
    rootV->setSpacing(0);

    // ── Topbar ────────────────────────────────────────────────────
    m_topbar = new QWidget;
    m_topbar->setFixedHeight(44);
    auto *topH = new QHBoxLayout(m_topbar);
    topH->setContentsMargins(14, 0, 14, 0);
    topH->setSpacing(8);

    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(10, 10);
    topH->addWidget(m_statusDot);

    m_statusLabel = new QLabel("searching...");
    topH->addWidget(m_statusLabel);
    topH->addStretch();

    auto makeBadge = [&](const QString &text, const QString &bg, const QString &fg) {
        auto *lbl = new QLabel(text);
        lbl->setStyleSheet(QString("background:%1; color:%2; border-radius:3px;"
                                   " padding:2px 8px; font-size:11px; font-weight:600;").arg(bg, fg));
        lbl->setVisible(false);
        return lbl;
    };
    m_masterBadge = makeBadge("MASTER", "#92400e", "#fbbf24");
    m_turretBadge = makeBadge("TURRET", "#1e3a5f", "#60a5fa");
    m_gunBadge    = makeBadge("GUN",    "#7f1d1d", "#fca5a5");
    topH->addWidget(m_masterBadge);
    topH->addWidget(m_turretBadge);
    topH->addWidget(m_gunBadge);
    topH->addSpacing(16);

    m_titleLabel = new QLabel("HARBINGER");
    m_titleLabel->setStyleSheet("font-size:15px; font-weight:700; letter-spacing:3px;");
    topH->addWidget(m_titleLabel);
    rootV->addWidget(m_topbar);

    // ── Content row ───────────────────────────────────────────────
    auto *content  = new QWidget;
    auto *contentH = new QHBoxLayout(content);
    contentH->setContentsMargins(0, 0, 0, 0);
    contentH->setSpacing(0);

    // ── Left sidebar ──────────────────────────────────────────────
    m_sidebar = new QWidget;
    m_sidebar->setFixedWidth(220);
    auto *sideV = new QVBoxLayout(m_sidebar);
    sideV->setContentsMargins(10, 10, 10, 10);
    sideV->setSpacing(8);

    auto addSectionLabel = [&](const QString &text) {
        auto *lbl = new QLabel(text);
        lbl->setObjectName("sectionLabel");
        sideV->addWidget(lbl);
    };
    auto addSep = [&]() {
        auto *sep = new QWidget;
        sep->setFixedHeight(1);
        m_separators.append(sep);
        sideV->addWidget(sep);
    };

    // Device
    addSectionLabel("DEVICE");
    m_deviceList = new QListWidget;
    m_deviceList->setFixedHeight(90);
    sideV->addWidget(m_deviceList);

    auto *devBtnRow = new QHBoxLayout;
    devBtnRow->setSpacing(4);
    auto *scanBtn    = new QPushButton("Scan");
    auto *connectBtn = new QPushButton("Connect");
    auto *disconnBtn = new QPushButton("Disconnect");
    scanBtn->setStyleSheet("font-size:11px; padding:3px 6px;");
    connectBtn->setStyleSheet("font-size:11px; padding:3px 6px;");
    disconnBtn->setStyleSheet("font-size:11px; padding:3px 6px;");
    connectBtn->setEnabled(false);
    disconnBtn->setEnabled(false);
    devBtnRow->addWidget(scanBtn);
    devBtnRow->addWidget(connectBtn);
    devBtnRow->addWidget(disconnBtn);
    sideV->addLayout(devBtnRow);
    m_scanBtnRef    = scanBtn;
    m_connectBtnRef = connectBtn;
    m_disconnBtnRef = disconnBtn;

    addSep();

    // Interlock
    addSectionLabel("INTERLOCK");
    m_masterArmBtn = new QPushButton("Master Arm");
    m_masterArmBtn->setCheckable(true);
    m_masterArmBtn->setEnabled(false);
    m_masterArmBtn->setObjectName("masterArmBtn");
    sideV->addWidget(m_masterArmBtn);

    addSep();

    // Arm
    addSectionLabel("ARM");
    auto *armRow = new QHBoxLayout;
    armRow->setSpacing(6);
    m_turretArmBtn = new QPushButton("Turret");
    m_gunArmBtn    = new QPushButton("Gun");
    m_turretArmBtn->setCheckable(true); m_turretArmBtn->setEnabled(false);
    m_gunArmBtn->setCheckable(true);    m_gunArmBtn->setEnabled(false);
    m_turretArmBtn->setObjectName("turretArmBtn");
    m_gunArmBtn->setObjectName("gunArmBtn");
    armRow->addWidget(m_turretArmBtn);
    armRow->addWidget(m_gunArmBtn);
    sideV->addLayout(armRow);

    addSep();

    // Voltage
    addSectionLabel("CHARGE TARGET");
    auto *voltRow = new QHBoxLayout;
    voltRow->setSpacing(6);
    m_voltSlider = new QSlider(Qt::Horizontal);
    m_voltSlider->setRange(0, 120);
    m_voltSlider->setValue(0);
    m_voltSlider->setEnabled(false);
    m_voltLabel = new QLabel("0 V");
    m_voltLabel->setFixedWidth(34);
    m_voltLabel->setStyleSheet("font-size:12px; font-weight:600;");
    voltRow->addWidget(m_voltSlider, 1);
    voltRow->addWidget(m_voltLabel);
    sideV->addLayout(voltRow);

    addSep();

    // Fire control
    addSectionLabel("FIRE CONTROL");
    m_fireBtn = new HoldFireButton;
    m_fireBtn->setFixedSize(88, 88);
    auto *fireCenter = new QHBoxLayout;
    fireCenter->addStretch();
    fireCenter->addWidget(m_fireBtn);
    fireCenter->addStretch();
    sideV->addLayout(fireCenter);

    sideV->addStretch();

    // Event log
    addSectionLabel("EVENT LOG");
    m_eventLog = new QTextEdit;
    m_eventLog->setReadOnly(true);
    m_eventLog->setFixedHeight(110);
    sideV->addWidget(m_eventLog);

    contentH->addWidget(m_sidebar);

    // ── Right: aim pad + telemetry strip ──────────────────────────
    auto *rightWidget = new QWidget;
    auto *rightV      = new QVBoxLayout(rightWidget);
    rightV->setContentsMargins(0, 0, 0, 0);
    rightV->setSpacing(0);

    m_aimWidget = new AimWidget;
    rightV->addWidget(m_aimWidget, 1);

    // Telemetry strip
    m_strip = new QWidget;
    m_strip->setFixedHeight(155);
    auto *stripH = new QHBoxLayout(m_strip);
    stripH->setContentsMargins(10, 8, 10, 8);
    stripH->setSpacing(8);

    auto addTelCell = [&](QWidget *w, const QString &title) {
        auto *cell  = new QWidget;
        auto *cellV = new QVBoxLayout(cell);
        cellV->setContentsMargins(6, 4, 6, 4);
        cellV->setSpacing(2);
        auto *tlbl = new QLabel(title);
        tlbl->setObjectName("telLabel");
        tlbl->setAlignment(Qt::AlignHCenter);
        cellV->addWidget(tlbl);
        cellV->addWidget(w, 1);
        m_telCells.append(cell);
        stripH->addWidget(cell);
    };

    m_compass   = new CompassWidget;
    m_elevation = new ElevationWidget;
    m_motorA    = new BiMotorWidget;
    m_motorB    = new BiMotorWidget;
    addTelCell(m_compass,   "HEADING");
    addTelCell(m_elevation, "ELEVATION");
    addTelCell(m_motorA,    "MOTOR A");
    addTelCell(m_motorB,    "MOTOR B");

    rightV->addWidget(m_strip);
    contentH->addWidget(rightWidget, 1);
    rootV->addWidget(content, 1);

    // ── Signal connections ────────────────────────────────────────
    connect(scanBtn, &QPushButton::clicked, this, [this]() {
        if (!m_scanning) doAutoScan();
    });
    connect(connectBtn, &QPushButton::clicked, this, [this]() {
        int row = m_deviceList->currentRow();
        if (row >= 0 && row < m_devices.size()) connectTo(m_devices[row]);
    });
    connect(disconnBtn, &QPushButton::clicked, this, &MainWindow::disconnectDevice);
    connect(m_deviceList, &QListWidget::currentRowChanged, this, [this, connectBtn](int row) {
        connectBtn->setEnabled(row >= 0 && m_sock == INVALID_SOCKET);
    });
    connect(m_masterArmBtn, &QPushButton::toggled, this, &MainWindow::onMasterArmToggled);
    connect(m_turretArmBtn, &QPushButton::toggled, this, &MainWindow::onTurretArmToggled);
    connect(m_gunArmBtn,    &QPushButton::toggled, this, &MainWindow::onGunArmToggled);
    connect(m_voltSlider, &QSlider::valueChanged, this, [this](int v) {
        m_voltLabel->setText(QString("%1 V").arg(v));
    });
    connect(m_voltSlider, &QSlider::sliderReleased, this, &MainWindow::onVoltageReleased);
    connect(m_fireBtn,   &HoldFireButton::fired,    this, &MainWindow::onFire);
    connect(m_aimWidget, &AimWidget::targetChanged, this, [this](float h, float e) {
        m_compass->setTarget(h);
        m_elevation->setTarget(e);
        if (m_ioWorker) m_ioWorker->sendAim(h, e);
    });
}

// ── Theme application ─────────────────────────────────────────────
void MainWindow::applyTheme() {
    qApp->setStyleSheet(Theme::stylesheet());

    // Container backgrounds (these override the global stylesheet)
    m_topbar->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;")
            .arg(Theme::topbarBg().name(), Theme::border().name()));
    m_sidebar->setStyleSheet(
        QString("background:%1; border-right:1px solid %2;")
            .arg(Theme::windowBg().name(), Theme::border().name()));
    m_strip->setStyleSheet(
        QString("background:%1; border-top:1px solid %2;")
            .arg(Theme::stripBg().name(), Theme::border().name()));

    // Topbar text colors
    m_titleLabel->setStyleSheet(
        QString("color:%1; font-size:15px; font-weight:700; letter-spacing:3px;")
            .arg(Theme::topbarText().name()));
    m_statusLabel->setStyleSheet("font-size:12px;");

    // Sidebar widgets
    m_deviceList->setStyleSheet(
        QString("background:%1; border:1px solid %2; border-radius:4px; font-size:11px;")
            .arg(Theme::surfaceBg().name(), Theme::border().name()));
    m_eventLog->setStyleSheet(
        QString("background:%1; border:1px solid %2; border-radius:4px;"
                " font-size:10px; font-family:'Consolas','Courier New',monospace;")
            .arg(Theme::surfaceBg().name(), Theme::border().name()));

    // Separators
    for (auto *sep : m_separators)
        sep->setStyleSheet(QString("background:%1;").arg(Theme::border().name()));

    // Telemetry cells
    for (auto *cell : m_telCells)
        cell->setStyleSheet(
            QString("QWidget { background:%1; border:1px solid %2; border-radius:6px; }")
                .arg(Theme::surfaceBg().name(), Theme::borderLight().name()));

    // Status dot — preserve current state (gray = disconnected)
    if (m_sock == INVALID_SOCKET)
        m_statusDot->setStyleSheet("background:#6b7280; border-radius:5px;");

    // Force repaint of custom-painted widgets so they pick up new Theme colors
    if (m_compass)   m_compass->update();
    if (m_elevation) m_elevation->update();
    if (m_motorA)    m_motorA->update();
    if (m_motorB)    m_motorB->update();
    if (m_aimWidget) m_aimWidget->update();
    if (m_fireBtn)   m_fireBtn->update();
}

// ── Auto-scan / connect ───────────────────────────────────────────
void MainWindow::doAutoScan() {
    if (m_sock != INVALID_SOCKET || m_scanning) return;
    m_scanning = true;
    setStatus("scanning...", false);

    auto *thread = new QThread;
    auto *worker = new ScanWorker;
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &ScanWorker::scan);
    connect(worker, &ScanWorker::scanDone, this,
            [this, thread, worker](QList<BtDevice> devs) {
        m_scanning = false;
        thread->quit(); worker->deleteLater(); thread->deleteLater();

        m_devices = devs;
        m_deviceList->clear();
        for (const auto &d : devs)
            m_deviceList->addItem(d.name.isEmpty() ? "(unknown)" : d.name);

        if (m_sock != INVALID_SOCKET) return;

        for (const auto &d : devs) {
            if (d.name.contains("Harbinger", Qt::CaseInsensitive)) {
                setStatus(QString("found %1, connecting...").arg(d.name), false);
                connectTo(d);
                return;
            }
        }

        setStatus("device not found, retrying...", false);
        if (m_autoScan) m_autoScanTimer->start(2000);
    });
    thread->start();
}

void MainWindow::connectTo(const BtDevice &dev) {
    SOCKET sock = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (sock == INVALID_SOCKET) {
        setStatus("socket() failed", false);
        if (m_autoScan) m_autoScanTimer->start(2000);
        return;
    }
    SOCKADDR_BTH addr{};
    addr.addressFamily  = AF_BTH;
    addr.btAddr         = dev.address;
    addr.serviceClassId = RFCOMM_PROTOCOL_UUID;
    addr.port           = BT_PORT_ANY;

    QString devName = dev.name;
    auto *thread = new QThread;
    connect(thread, &QThread::started, [sock, addr, devName, thread, this]() mutable {
        int res = ::connect(sock, reinterpret_cast<SOCKADDR*>(&addr), sizeof(addr));
        QMetaObject::invokeMethod(this, [=]() {
            thread->quit(); thread->deleteLater();
            if (res == SOCKET_ERROR) {
                closesocket(sock);
                setStatus("connect failed, retrying...", false);
                if (m_autoScan) m_autoScanTimer->start(2000);
            } else {
                onConnected(sock, devName);
            }
        }, Qt::QueuedConnection);
    });
    thread->start();
}

void MainWindow::onConnected(SOCKET sock, const QString &name) {
    m_sock     = sock;
    m_autoScan = false;
    m_autoScanTimer->stop();

    m_statusDot->setStyleSheet("background:#22c55e; border-radius:5px;");
    m_statusLabel->setText(name);
    m_statusLabel->setStyleSheet("color:#bbf7d0; font-size:12px;");

    // Enable only disconnect while we wait for the firmware's initial state dump.
    // The rest of the controls (arm, fire, voltage) become live in handlePacket()
    // once MSG_STATE is received, so the app always sees current state before acting.
    m_disconnBtnRef->setEnabled(true);
    m_scanBtnRef->setEnabled(false);
    m_connectBtnRef->setEnabled(false);
    m_waitingForInitialState = true;

    logEvent(QString("=== Connected to %1 — waiting for initial state ===").arg(name));

    m_ioThread = new QThread(this);
    m_ioWorker = new IoWorker(sock);
    m_ioWorker->moveToThread(m_ioThread);
    connect(m_ioThread, &QThread::started,         m_ioWorker, &IoWorker::run);
    connect(m_ioWorker, &IoWorker::packetReceived, this,       &MainWindow::handlePacket);
    connect(m_ioWorker, &IoWorker::disconnected,   this, [this]() {
        logEvent("=== Remote disconnected ===");
        disconnectDevice();
    });
    m_ioThread->start();
}

void MainWindow::disconnectDevice() {
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
    m_waitingForInitialState = false;

    // Reset arm button checked states so the next connection starts fresh
    // (no stale state from the previous session shown before MSG_STATE arrives).
    for (auto *btn : {m_masterArmBtn, m_turretArmBtn, m_gunArmBtn})
        btn->blockSignals(true);
    m_masterArmBtn->setChecked(false);
    m_turretArmBtn->setChecked(false);
    m_gunArmBtn->setChecked(false);
    for (auto *btn : {m_masterArmBtn, m_turretArmBtn, m_gunArmBtn})
        btn->blockSignals(false);
    m_masterArmed = m_turretArmed = m_gunArmed = false;

    m_masterBadge->setVisible(false);
    m_turretBadge->setVisible(false);
    m_gunBadge->setVisible(false);
    m_aimWidget->setArmed(false);
    m_autoScan = true;
    doAutoScan();
}

void MainWindow::setStatus(const QString &msg, bool connected) {
    if (connected) {
        m_statusDot->setStyleSheet("background:#22c55e; border-radius:5px;");
        m_statusLabel->setStyleSheet("color:#bbf7d0; font-size:12px;");
    } else {
        m_statusDot->setStyleSheet("background:#6b7280; border-radius:5px;");
        m_statusLabel->setStyleSheet("color:#9ca3af; font-size:12px;");
    }
    m_statusLabel->setText(msg);
}

void MainWindow::setControlsEnabled(bool en) {
    m_masterArmBtn->setEnabled(en);
    m_turretArmBtn->setEnabled(en);
    m_gunArmBtn->setEnabled(en);
    m_voltSlider->setEnabled(en);
    m_fireBtn->setEnabled(en);
    m_disconnBtnRef->setEnabled(en);
    m_scanBtnRef->setEnabled(!en);
    m_connectBtnRef->setEnabled(!en && m_deviceList->currentRow() >= 0);
}

// ── Arm toggles ───────────────────────────────────────────────────
void MainWindow::onMasterArmToggled(bool checked) {
    if (!m_ioWorker) {
        m_masterArmBtn->blockSignals(true);
        m_masterArmBtn->setChecked(!checked);
        m_masterArmBtn->blockSignals(false);
        return;
    }
    uint8_t flags = static_cast<uint8_t>((checked ? ARM_TRUE : ARM_FALSE) << ARM_SHIFT_MASTER);
    logEvent(QString("\xe2\x86\x92 ARM master=%1").arg(checked ? "true" : "false"));
    m_ioWorker->sendArm(flags);
}

void MainWindow::onTurretArmToggled(bool checked) {
    if (!m_ioWorker) {
        m_turretArmBtn->blockSignals(true);
        m_turretArmBtn->setChecked(!checked);
        m_turretArmBtn->blockSignals(false);
        return;
    }
    uint8_t flags = static_cast<uint8_t>((checked ? ARM_TRUE : ARM_FALSE) << ARM_SHIFT_TURRET);
    logEvent(QString("\xe2\x86\x92 ARM turret=%1").arg(checked ? "true" : "false"));
    m_ioWorker->sendArm(flags);
}

void MainWindow::onGunArmToggled(bool checked) {
    if (!m_ioWorker) {
        m_gunArmBtn->blockSignals(true);
        m_gunArmBtn->setChecked(!checked);
        m_gunArmBtn->blockSignals(false);
        return;
    }
    uint8_t flags = static_cast<uint8_t>((checked ? ARM_TRUE : ARM_FALSE) << ARM_SHIFT_GUN);
    logEvent(QString("\xe2\x86\x92 ARM gun=%1").arg(checked ? "true" : "false"));
    m_ioWorker->sendArm(flags);
}

void MainWindow::onVoltageReleased() {
    if (!m_ioWorker) return;
    float v = static_cast<float>(m_voltSlider->value());
    logEvent(QString("\xe2\x86\x92 SET_VOLTAGE %1 V").arg(v, 0, 'f', 0));
    m_ioWorker->sendSetVoltage(v);
}

void MainWindow::onFire() {
    if (!m_ioWorker) return;
    logEvent("\xe2\x86\x92 FIRE");
    m_ioWorker->sendFire();
}

// ── Packet receive ────────────────────────────────────────────────
void MainWindow::handlePacket(uint8_t type, QByteArray payload) {
    switch (type) {
    case MSG_PONG:
        logEvent("\xe2\x86\x90 PONG");
        break;

    case MSG_STATE: {
        if (payload.size() < static_cast<int>(sizeof(PktState))) break;
        PktState pkt;
        memcpy(&pkt, payload.constData(), sizeof(pkt));
        m_masterArmed = pkt.flags & STATE_MASTER_ARM;
        m_turretArmed = pkt.flags & STATE_TURRET_ARM;
        m_gunArmed    = pkt.flags & STATE_GUN_ARM;

        for (auto *btn : {m_masterArmBtn, m_turretArmBtn, m_gunArmBtn})
            btn->blockSignals(true);
        m_masterArmBtn->setChecked(m_masterArmed);
        m_turretArmBtn->setChecked(m_turretArmed);
        m_gunArmBtn->setChecked(m_gunArmed);
        for (auto *btn : {m_masterArmBtn, m_turretArmBtn, m_gunArmBtn})
            btn->blockSignals(false);

        m_voltSlider->blockSignals(true);
        m_voltSlider->setValue(static_cast<int>(pkt.target_v));
        m_voltLabel->setText(QString("%1 V").arg(static_cast<int>(pkt.target_v)));
        m_voltSlider->blockSignals(false);

        m_masterBadge->setVisible(m_masterArmed);
        m_turretBadge->setVisible(m_turretArmed);
        m_gunBadge->setVisible(m_gunArmed);
        m_aimWidget->setArmed(m_turretArmed);

        QStringList armed;
        if (m_masterArmed) armed << "MASTER";
        if (m_turretArmed) armed << "TURRET";
        if (m_gunArmed)    armed << "GUN";
        logEvent(QString("\xe2\x86\x90 STATE arm=[%1] v=%2V")
            .arg(armed.isEmpty() ? "none" : armed.join(","))
            .arg(pkt.target_v, 0, 'f', 1));

        // Enable all controls now that we have the authoritative initial state.
        if (m_waitingForInitialState) {
            m_waitingForInitialState = false;
            setControlsEnabled(true);
            logEvent("=== Initial state received — controls enabled ===");
        }
        break;
    }

    case MSG_TELEMETRY: {
        if (payload.size() < static_cast<int>(sizeof(PktTelemetry))) break;
        PktTelemetry pkt;
        memcpy(&pkt, payload.constData(), sizeof(pkt));
        m_compass->setHeading(pkt.heading);
        m_elevation->setElevation(pkt.elevation);
        m_motorA->setValues(pkt.motorA_vel, pkt.motorA_acc);
        m_motorB->setValues(pkt.motorB_vel, pkt.motorB_acc);
        m_aimWidget->setActual(pkt.heading, pkt.elevation);
        QString line = QString("\xe2\x86\x90 TEL h=%1 e=%2 Avel=%3 Bvel=%4")
            .arg(pkt.heading,    0, 'f', 1).arg(pkt.elevation,  0, 'f', 1)
            .arg(pkt.motorA_vel, 0, 'f', 2).arg(pkt.motorB_vel, 0, 'f', 2);
        static QString lastTel;
        if (line != lastTel) { logEvent(line); lastTel = line; }
        break;
    }

    case MSG_SHOT: {
        if (payload.size() < static_cast<int>(sizeof(PktShotHeader))) break;
        PktShotHeader hdr;
        memcpy(&hdr, payload.constData(), sizeof(hdr));
        QString msg = QString("\xe2\x86\x90 SHOT #%1 (%2 stages)")
            .arg(hdr.total_shots).arg(hdr.stage_count);
        int off = sizeof(PktShotHeader);
        for (uint8_t i = 0; i < hdr.stage_count; ++i, off += 12) {
            if (off + 12 > payload.size()) break;
            PktShotStage s;
            memcpy(&s, payload.constData() + off, sizeof(s));
            msg += QString("\n  [%1] %2us %3m/s %4V")
                .arg(i).arg(s.t_us).arg(s.v_mps, 0, 'f', 2).arg(s.drain_v, 0, 'f', 2);
        }
        logEvent(msg);
        break;
    }

    case MSG_LOG: {
        if (payload.size() < 2) break;
        uint8_t level = static_cast<uint8_t>(payload.at(0));
        uint8_t slen  = static_cast<uint8_t>(payload.at(1));
        QString text  = QString::fromLatin1(payload.constData() + 2,
                                            qMin((int)slen, payload.size() - 2));
        const char* prefix = (level == LOG_ERROR) ? "[ERR]" :
                             (level == LOG_WARN)  ? "[WRN]" : "[LOG]";
        logEvent(QString("%1 %2").arg(prefix, text));
        break;
    }

    default:
        logEvent(QString("\xe2\x86\x90 unknown 0x%1").arg(type, 2, 16, QChar('0')));
    }
}

void MainWindow::logEvent(const QString &msg) {
    m_eventLog->append(msg);
    m_eventLog->verticalScrollBar()->setValue(
        m_eventLog->verticalScrollBar()->maximum());
}
