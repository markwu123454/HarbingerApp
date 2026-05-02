#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QScrollBar>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>

// ── Windows / Bluetooth headers ──────────────────────────────────────────────
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
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// Data
// ─────────────────────────────────────────────────────────────────────────────
struct BtDevice {
    QString name;
    BTH_ADDR address;      // 6-byte packed address
    QString addressStr;
    bool    authenticated;
    bool    remembered;
    bool    connected;
};

// ─────────────────────────────────────────────────────────────────────────────
// Worker: scans for Bluetooth devices on a background thread
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
        params.fIssueInquiry        = FALSE;   // active radio inquiry
        params.cTimeoutMultiplier   = 0;      // 4 × 1.28 s ≈ 5 s inquiry

        BLUETOOTH_DEVICE_INFO info{};
        info.dwSize = sizeof(info);

        HBLUETOOTH_DEVICE_FIND hFind =
            BluetoothFindFirstDevice(&params, &info);

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
// Worker: owns the SOCKET and pumps reads on a background thread
// ─────────────────────────────────────────────────────────────────────────────
class IoWorker : public QObject {
    Q_OBJECT
public:
    explicit IoWorker(SOCKET sock) : m_sock(sock) {}

    void requestStop() { m_stop = true; }

public slots:
    void run() {
        char buf[4096];
        while (!m_stop) {
            int n = recv(m_sock, buf, sizeof(buf), 0);
            if (n > 0) {
                QByteArray data(buf, n);
                emit received(data);
            } else {
                // 0 = remote closed, SOCKET_ERROR = error
                if (!m_stop) emit disconnected();
                break;
            }
        }
    }

    void sendData(QByteArray data) {
        if (m_sock != INVALID_SOCKET) {
            send(m_sock, data.constData(), data.size(), 0);
        }
    }

signals:
    void received(QByteArray data);
    void disconnected();

private:
    SOCKET          m_sock = INVALID_SOCKET;
    std::atomic<bool> m_stop{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Main Window
// ─────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        // Winsock init
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            QMessageBox::critical(nullptr, "Fatal", "WSAStartup failed.");
            std::exit(1);
        }

        setWindowTitle("Harbinger — Bluetooth Terminal");
        setMinimumSize(820, 580);
        resize(1000, 680);

        applyTheme();
        buildUi();
    }

    ~MainWindow() override {
        disconnectDevice();
        WSACleanup();
    }

private:
    // ── UI pointers ────────────────────────────────────────────────────────
    QListWidget *m_deviceList  = nullptr;
    QPushButton *m_scanBtn     = nullptr;
    QPushButton *m_connectBtn  = nullptr;
    QPushButton *m_disconnBtn  = nullptr;
    QTextEdit   *m_rxPane      = nullptr;
    QLineEdit   *m_txLine      = nullptr;
    QPushButton *m_sendBtn     = nullptr;
    QLabel      *m_statusLabel = nullptr;

    // ── State ──────────────────────────────────────────────────────────────
    QList<BtDevice> m_devices;
    SOCKET          m_sock      = INVALID_SOCKET;
    QThread        *m_ioThread  = nullptr;
    IoWorker       *m_ioWorker  = nullptr;

    // ── Theme ──────────────────────────────────────────────────────────────
    void applyTheme() {
        // Industrial-terminal aesthetic: near-black bg, amber accent
        setStyleSheet(R"(
            QMainWindow, QWidget {
                background-color: #0d0f0e;
                color: #c8c0a8;
                font-family: "Consolas", "Courier New", monospace;
                font-size: 13px;
            }
            QSplitter::handle { background: #1e221f; width: 2px; height: 2px; }

            /* Left panel */
            #leftPanel {
                background: #0d0f0e;
                border-right: 1px solid #1e2a22;
            }
            #panelTitle {
                color: #e8b84b;
                font-size: 11px;
                font-weight: bold;
                letter-spacing: 3px;
                padding: 10px 12px 4px 12px;
                border-bottom: 1px solid #1e2a22;
            }
            QListWidget {
                background: #0d0f0e;
                border: none;
                outline: none;
                padding: 4px;
            }
            QListWidget::item {
                padding: 8px 10px;
                border-bottom: 1px solid #141714;
                color: #9ba89d;
                border-radius: 2px;
            }
            QListWidget::item:selected {
                background: #1a2e1e;
                color: #e8b84b;
                border-left: 2px solid #e8b84b;
            }
            QListWidget::item:hover:!selected {
                background: #141a15;
                color: #c8c0a8;
            }

            /* Buttons */
            QPushButton {
                background: #141a15;
                color: #9ba89d;
                border: 1px solid #2a3a2c;
                border-radius: 2px;
                padding: 6px 14px;
                font-family: "Consolas", monospace;
                font-size: 12px;
                letter-spacing: 1px;
            }
            QPushButton:hover  { background: #1e2a1f; color: #c8c0a8; border-color: #3a5040; }
            QPushButton:pressed { background: #0d0f0e; }
            QPushButton:disabled { color: #3a4040; border-color: #1a1e1a; }

            #primaryBtn {
                background: #1a3320;
                color: #e8b84b;
                border: 1px solid #2a5030;
                font-weight: bold;
            }
            #primaryBtn:hover  { background: #1e3d25; border-color: #e8b84b; }
            #primaryBtn:disabled { background: #0d0f0e; color: #2a3a2a; border-color: #1a1e1a; }

            #dangerBtn {
                background: #2a1010;
                color: #c05050;
                border: 1px solid #3a1818;
            }
            #dangerBtn:hover { background: #331515; color: #e06060; border-color: #c05050; }
            #dangerBtn:disabled { background: #0d0f0e; color: #2a1a1a; border-color: #1a1010; }

            /* RX pane */
            QTextEdit {
                background: #080a09;
                color: #7ec87e;
                border: 1px solid #1e2a1e;
                border-radius: 2px;
                padding: 8px;
                font-family: "Consolas", "Courier New", monospace;
                font-size: 12px;
                selection-background-color: #1a3320;
            }
            QScrollBar:vertical {
                background: #0d0f0e; width: 8px;
                border: none;
            }
            QScrollBar::handle:vertical {
                background: #2a3a2c; border-radius: 4px; min-height: 20px;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

            /* TX line */
            QLineEdit {
                background: #080a09;
                color: #c8c0a8;
                border: 1px solid #1e2a1e;
                border-radius: 2px;
                padding: 6px 10px;
                font-family: "Consolas", monospace;
                font-size: 12px;
                selection-background-color: #1a3320;
            }
            QLineEdit:focus { border-color: #e8b84b; }

            /* Status bar */
            QStatusBar {
                background: #080a09;
                color: #4a5a4c;
                border-top: 1px solid #1e2a1e;
                font-size: 11px;
            }
        )");
    }

    void buildUi() {
        auto *root = new QWidget(this);
        setCentralWidget(root);
        auto *rootH = new QHBoxLayout(root);
        rootH->setContentsMargins(0, 0, 0, 0);
        rootH->setSpacing(0);

        // ── Left panel ─────────────────────────────────────────────────────
        auto *leftPanel = new QWidget;
        leftPanel->setObjectName("leftPanel");
        leftPanel->setFixedWidth(260);
        auto *leftV = new QVBoxLayout(leftPanel);
        leftV->setContentsMargins(0, 0, 0, 0);
        leftV->setSpacing(0);

        auto *panelTitle = new QLabel("DEVICES");
        panelTitle->setObjectName("panelTitle");
        leftV->addWidget(panelTitle);

        m_deviceList = new QListWidget;
        leftV->addWidget(m_deviceList, 1);

        // Scan / Connect / Disconnect buttons
        auto *btnArea = new QWidget;
        auto *btnV    = new QVBoxLayout(btnArea);
        btnV->setContentsMargins(10, 8, 10, 10);
        btnV->setSpacing(6);

        m_scanBtn    = new QPushButton("[ SCAN ]");
        m_connectBtn = new QPushButton("[ CONNECT ]");
        m_disconnBtn = new QPushButton("[ DISCONNECT ]");

        m_scanBtn->setObjectName("primaryBtn");
        m_connectBtn->setObjectName("primaryBtn");
        m_disconnBtn->setObjectName("dangerBtn");

        m_connectBtn->setEnabled(false);
        m_disconnBtn->setEnabled(false);

        btnV->addWidget(m_scanBtn);
        btnV->addWidget(m_connectBtn);
        btnV->addWidget(m_disconnBtn);
        leftV->addWidget(btnArea);

        rootH->addWidget(leftPanel);

        // ── Right panel: IO ────────────────────────────────────────────────
        auto *rightPanel = new QWidget;
        auto *rightV     = new QVBoxLayout(rightPanel);
        rightV->setContentsMargins(12, 10, 12, 10);
        rightV->setSpacing(8);

        auto *rxTitle = new QLabel("RX — RECEIVE");
        rxTitle->setObjectName("panelTitle");
        rxTitle->setStyleSheet("color:#7ec87e; font-size:11px; font-weight:bold; letter-spacing:3px; padding:0;");
        rightV->addWidget(rxTitle);

        m_rxPane = new QTextEdit;
        m_rxPane->setReadOnly(true);
        m_rxPane->setPlaceholderText("// awaiting connection…");
        rightV->addWidget(m_rxPane, 1);

        // TX row
        auto *txRow = new QHBoxLayout;
        auto *txLabel = new QLabel("TX ›");
        txLabel->setStyleSheet("color:#e8b84b; font-weight:bold; padding-right:4px;");
        m_txLine  = new QLineEdit;
        m_txLine->setPlaceholderText("type to send… (Enter or Send)");
        m_txLine->setEnabled(false);
        m_sendBtn = new QPushButton("SEND");
        m_sendBtn->setObjectName("primaryBtn");
        m_sendBtn->setFixedWidth(70);
        m_sendBtn->setEnabled(false);

        txRow->addWidget(txLabel);
        txRow->addWidget(m_txLine, 1);
        txRow->addWidget(m_sendBtn);
        rightV->addLayout(txRow);

        rootH->addWidget(rightPanel, 1);

        // ── Status bar ─────────────────────────────────────────────────────
        m_statusLabel = new QLabel("  ready — no device connected");
        statusBar()->addWidget(m_statusLabel, 1);
        statusBar()->setSizeGripEnabled(false);

        // ── Signals ────────────────────────────────────────────────────────
        connect(m_scanBtn,    &QPushButton::clicked, this, &MainWindow::startScan);
        connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::connectSelected);
        connect(m_disconnBtn, &QPushButton::clicked, this, &MainWindow::disconnectDevice);
        connect(m_sendBtn,    &QPushButton::clicked, this, &MainWindow::sendTx);
        connect(m_txLine, &QLineEdit::returnPressed,  this, &MainWindow::sendTx);

        connect(m_deviceList, &QListWidget::currentRowChanged, [this](int row) {
            m_connectBtn->setEnabled(row >= 0 && m_sock == INVALID_SOCKET);
        });
    }

    // ── Scan ───────────────────────────────────────────────────────────────
    void startScan() {
        m_scanBtn->setEnabled(false);
        m_deviceList->clear();
        m_devices.clear();
        setStatus("scanning… (≈5 s inquiry)");

        auto *thread = new QThread(this);
        auto *worker = new ScanWorker;
        worker->moveToThread(thread);

        connect(thread, &QThread::started,  worker, &ScanWorker::scan);
        connect(worker, &ScanWorker::scanDone, this, [this, thread, worker](QList<BtDevice> devs) {
            m_devices = devs;
            m_deviceList->clear();
            for (const auto &d : devs) {
                QString flags;
                if (d.connected)     flags += " ●";
                if (d.authenticated) flags += " 🔑";
                QString label = QString("  %1\n  %2%3")
                    .arg(d.name.isEmpty() ? "(unknown)" : d.name)
                    .arg(d.addressStr)
                    .arg(flags);
                auto *item = new QListWidgetItem(label);
                m_deviceList->addItem(item);
            }
            setStatus(devs.isEmpty()
                ? "scan complete — no devices found"
                : QString("scan complete — %1 device(s)").arg(devs.size()));
            m_scanBtn->setEnabled(true);
            thread->quit();
            worker->deleteLater();
            thread->deleteLater();
        });

        thread->start();
    }

    // ── Connect ────────────────────────────────────────────────────────────
    void connectSelected() {
        int row = m_deviceList->currentRow();
        if (row < 0 || row >= m_devices.size()) return;

        const BtDevice &dev = m_devices[row];
        setStatus(QString("connecting to %1…").arg(dev.name));
        m_connectBtn->setEnabled(false);
        m_scanBtn->setEnabled(false);

        // Build RFCOMM socket address (channel 1 — SPP)
        SOCKADDR_BTH addr{};
        addr.addressFamily = AF_BTH;
        addr.btAddr        = dev.address;
        addr.serviceClassId = RFCOMM_PROTOCOL_UUID;
        addr.port           = BT_PORT_ANY;   // let stack pick channel via SDP

        SOCKET sock = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (sock == INVALID_SOCKET) {
            setStatus("socket() failed: " + QString::number(WSAGetLastError()));
            m_connectBtn->setEnabled(true);
            m_scanBtn->setEnabled(true);
            return;
        }

        // connect() is blocking — run on a thread
        auto *thread = new QThread(this);
        connect(thread, &QThread::started, [=]() {
            int res = ::connect(sock,
                reinterpret_cast<SOCKADDR*>(const_cast<SOCKADDR_BTH*>(&addr)),
                sizeof(addr));
            // emit result back on GUI thread
            QMetaObject::invokeMethod(this, [=]() {
                thread->quit();
                thread->deleteLater();
                if (res == SOCKET_ERROR) {
                    setStatus("connect() failed: " + QString::number(WSAGetLastError()));
                    closesocket(sock);
                    m_connectBtn->setEnabled(true);
                    m_scanBtn->setEnabled(true);
                } else {
                    onConnected(sock, dev.name);
                }
            }, Qt::QueuedConnection);
        });
        thread->start();
    }

    void onConnected(SOCKET sock, const QString &name) {
        m_sock = sock;
        setStatus("connected — " + name);

        m_disconnBtn->setEnabled(true);
        m_connectBtn->setEnabled(false);
        m_scanBtn->setEnabled(false);
        m_txLine->setEnabled(true);
        m_sendBtn->setEnabled(true);
        m_txLine->setFocus();

        appendRx(QString("── connected to %1 ──\n").arg(name), "#e8b84b");

        // Start IO worker
        m_ioThread = new QThread(this);
        m_ioWorker = new IoWorker(sock);
        m_ioWorker->moveToThread(m_ioThread);

        connect(m_ioThread, &QThread::started, m_ioWorker, &IoWorker::run);
        connect(m_ioWorker, &IoWorker::received, this, [this](QByteArray data) {
            appendRx(QString::fromLocal8Bit(data), "#7ec87e");
        });
        connect(m_ioWorker, &IoWorker::disconnected, this, [this]() {
            appendRx("── remote disconnected ──\n", "#c05050");
            disconnectDevice();
        });

        m_ioThread->start();
    }

    // ── Disconnect ─────────────────────────────────────────────────────────
    void disconnectDevice() {
        if (m_ioWorker) {
            m_ioWorker->requestStop();
        }
        if (m_ioThread) {
            // shutdown the socket so recv() unblocks
            if (m_sock != INVALID_SOCKET) shutdown(m_sock, SD_BOTH);
            m_ioThread->quit();
            m_ioThread->wait(2000);
            delete m_ioWorker;  m_ioWorker = nullptr;
            delete m_ioThread;  m_ioThread = nullptr;
        }
        if (m_sock != INVALID_SOCKET) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }

        m_disconnBtn->setEnabled(false);
        m_connectBtn->setEnabled(m_deviceList->currentRow() >= 0);
        m_scanBtn->setEnabled(true);
        m_txLine->setEnabled(false);
        m_sendBtn->setEnabled(false);
        setStatus("disconnected — ready");
    }

    // ── TX ─────────────────────────────────────────────────────────────────
    void sendTx() {
        QString text = m_txLine->text();
        if (text.isEmpty() || !m_ioWorker) return;

        QByteArray data = text.toLocal8Bit();
        data.append('\n');                  // most SPP devices expect newline

        // Echo locally
        appendRx("› " + text + "\n", "#e8b84b");
        m_txLine->clear();

        // Route to IO worker (thread-safe via signal)
        QMetaObject::invokeMethod(m_ioWorker, [this, data]() {
            m_ioWorker->sendData(data);
        }, Qt::QueuedConnection);
    }

    // ── Helpers ────────────────────────────────────────────────────────────
    void appendRx(const QString &text, const QString &color) {
        // Move cursor to end, insert colored html
        QTextCursor cursor = m_rxPane->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_rxPane->setTextCursor(cursor);

        QString html = QString("<span style=\"color:%1; white-space:pre;\">%2</span>")
            .arg(color, text.toHtmlEscaped());
        m_rxPane->insertHtml(html);

        // Auto-scroll
        m_rxPane->verticalScrollBar()->setValue(
            m_rxPane->verticalScrollBar()->maximum());
    }

    void setStatus(const QString &msg) {
        m_statusLabel->setText("  " + msg);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow win;
    win.show();
    return app.exec();
}

#include "main.moc"