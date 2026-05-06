#pragma once
#include <QMainWindow>
#include <QList>
#include <winsock2.h>
#include <windows.h>
#include "btdevice.h"

class QLabel;
class QPushButton;
class QListWidget;
class QSlider;
class QTextEdit;
class QTimer;
class QThread;
class AimWidget;
class CompassWidget;
class ElevationWidget;
class BiMotorWidget;
class HoldFireButton;
class IoWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

private:
    // ── Topbar ────────────────────────────────────────────────────
    QWidget *m_topbar      = nullptr;
    QLabel  *m_statusDot   = nullptr;
    QLabel  *m_statusLabel = nullptr;
    QLabel  *m_masterBadge = nullptr;
    QLabel  *m_turretBadge = nullptr;
    QLabel  *m_gunBadge    = nullptr;

    // ── Sidebar ───────────────────────────────────────────────────
    QWidget        *m_sidebar      = nullptr;
    QListWidget    *m_deviceList   = nullptr;
    QPushButton    *m_scanBtnRef   = nullptr;
    QPushButton    *m_connectBtnRef= nullptr;
    QPushButton    *m_disconnBtnRef= nullptr;
    QPushButton    *m_masterArmBtn = nullptr;
    QPushButton    *m_turretArmBtn = nullptr;
    QPushButton    *m_gunArmBtn    = nullptr;
    QSlider        *m_voltSlider   = nullptr;
    QLabel         *m_voltLabel    = nullptr;
    HoldFireButton *m_fireBtn      = nullptr;
    QTextEdit      *m_eventLog     = nullptr;
    QList<QWidget*> m_separators;

    // ── Main area ─────────────────────────────────────────────────
    AimWidget       *m_aimWidget  = nullptr;
    QWidget         *m_strip      = nullptr;
    CompassWidget   *m_compass    = nullptr;
    ElevationWidget *m_elevation  = nullptr;
    BiMotorWidget   *m_motorA     = nullptr;
    BiMotorWidget   *m_motorB     = nullptr;
    QList<QWidget*>  m_telCells;

    // ── State ─────────────────────────────────────────────────────
    bool m_masterArmed = false;
    bool m_turretArmed = false;
    bool m_gunArmed    = false;

    // ── Networking ────────────────────────────────────────────────
    QList<BtDevice> m_devices;
    SOCKET          m_sock     = INVALID_SOCKET;
    QThread        *m_ioThread = nullptr;
    IoWorker       *m_ioWorker = nullptr;
    bool            m_scanning = false;

    // ── Auto-scan ─────────────────────────────────────────────────
    QTimer *m_autoScanTimer = nullptr;
    bool    m_autoScan      = true;

    void buildUi();
    void applyTheme();

    void doAutoScan();
    void connectTo(const BtDevice &dev);
    void onConnected(SOCKET sock, const QString &name);
    void disconnectDevice();

    void setStatus(const QString &msg, bool connected);
    void setControlsEnabled(bool en);
    void logEvent(const QString &msg);

    void onMasterArmToggled(bool checked);
    void onTurretArmToggled(bool checked);
    void onGunArmToggled(bool checked);
    void onVoltageReleased();
    void onFire();

    void handlePacket(uint8_t type, QByteArray payload);
};
