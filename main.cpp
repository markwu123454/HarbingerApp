#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextEdit>
#include <QSlider>
#include <QFrame>
#include <QThread>
#include <QScrollBar>
#include <QMessageBox>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPolygonF>

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
#include <cmath>
#include <limits>

static constexpr double PI = 3.14159265358979323846;

// ── Protocol constants ────────────────────────────────────────
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

// ── BtDevice ──────────────────────────────────────────────────
struct BtDevice {
    QString  name;
    BTH_ADDR address;
    QString  addressStr;
    bool     connected;
};

// ── ScanWorker ────────────────────────────────────────────────
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
                dev.name      = QString::fromWCharArray(info.szName);
                dev.address   = info.Address.ullLong;
                dev.connected = info.fConnected;
                BTH_ADDR a    = info.Address.ullLong;
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

// ── IoWorker ──────────────────────────────────────────────────
// Owns the socket. run() blocks on recv() on the IO thread.
// Send methods are called directly from the main thread — send() is
// thread-safe alongside a concurrent blocking recv() in another thread.
class IoWorker : public QObject {
    Q_OBJECT
public:
    explicit IoWorker(SOCKET sock) : m_sock(sock) {}
    void requestStop() { m_stop = true; }

    void sendPing()                     { uint8_t b = MSG_PING; rawSend(&b, 1); }
    void sendAim(float h, float e)      { PktAim p{h,e}; msgSend(MSG_AIM,&p,sizeof(p)); }
    void sendArm(uint8_t f)             { PktArm p{f}; msgSend(MSG_ARM,&p,sizeof(p)); }
    void sendSetVoltage(float v)        { PktSetVoltage p{v}; msgSend(MSG_SET_VOLTAGE,&p,sizeof(p)); }
    void sendFire()                     { uint8_t b = MSG_FIRE; rawSend(&b, 1); }

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
                if (b == MSG_SHOT)   { m_rxExpected = 5; m_rxState = RxState::PAYLOAD; }
                else if (psiz == 0)  { emit packetReceived(b, QByteArray()); }
                else                 { m_rxExpected = psiz; m_rxState = RxState::PAYLOAD; }
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

// ── CompassWidget ─────────────────────────────────────────────
class CompassWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompassWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(100, 100);
    }
    void setHeading(float h) { m_heading = h; update(); }
    void setTarget(float h)  { m_target  = h; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int s = qMin(width(), height()) - 6;
        QRectF r((width()-s)/2.0, (height()-s)/2.0, s, s);
        QPointF c = r.center();
        double radius = s / 2.0;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf8, 0xf9, 0xfa));
        p.drawEllipse(r);

        // Tick marks
        for (int deg = 0; deg < 360; deg += 10) {
            double rad   = (deg - 90) * PI / 180.0;
            double inner = (deg % 30 == 0) ? radius * 0.72 : radius * 0.84;
            p.setPen(QPen(QColor(deg % 30 == 0 ? 0xadb5bd : 0xd0d4da), deg % 30 == 0 ? 1.5 : 1));
            p.drawLine(
                QPointF(c.x() + inner       * cos(rad), c.y() + inner       * sin(rad)),
                QPointF(c.x() + radius*0.94 * cos(rad), c.y() + radius*0.94 * sin(rad)));
        }

        // Cardinal labels
        const struct { const char *lbl; int ang; } cards[] = {{"N",0},{"E",90},{"S",180},{"W",270}};
        p.setFont(QFont("Segoe UI", 7, QFont::Bold));
        for (auto &card : cards) {
            double rad = (card.ang - 90) * PI / 180.0;
            double d   = radius * 0.56;
            p.setPen(QColor(0x6b, 0x72, 0x80));
            QRectF tr(c.x() + d*cos(rad) - 7, c.y() + d*sin(rad) - 7, 14, 14);
            p.drawText(tr, Qt::AlignCenter, card.lbl);
        }

        // Target heading (dashed blue)
        if (!std::isnan(m_target)) {
            double rad = (m_target - 90) * PI / 180.0;
            p.setPen(QPen(QColor(0x3b, 0x82, 0xf6, 180), 1.5, Qt::DashLine));
            p.drawLine(c, QPointF(c.x() + radius*0.68*cos(rad), c.y() + radius*0.68*sin(rad)));
        }

        // Heading needle (red)
        double rad = (m_heading - 90) * PI / 180.0;
        p.setPen(QPen(QColor(0xef, 0x44, 0x44), 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(
            QPointF(c.x() - radius*0.14*cos(rad), c.y() - radius*0.14*sin(rad)),
            QPointF(c.x() + radius*0.72*cos(rad), c.y() + radius*0.72*sin(rad)));

        // Center dot
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1a, 0x23, 0x32));
        p.drawEllipse(c, 3.0, 3.0);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xd0, 0xd4, 0xda), 1));
        p.drawEllipse(r);
    }

private:
    float m_heading = 0;
    float m_target  = std::numeric_limits<float>::quiet_NaN();
};

// ── ElevationWidget ───────────────────────────────────────────
class ElevationWidget : public QWidget {
    Q_OBJECT
public:
    explicit ElevationWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(90, 100);
    }
    void setElevation(float e) { m_elevation = e; update(); }
    void setTarget(float e)    { m_target    = e; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r(4, 4, width()-8, height()-8);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf8, 0xf9, 0xfa));
        p.drawRoundedRect(r, 4, 4);

        const float angles[] = { -30, -15, 0, 15, 30 };
        for (float a : angles) {
            double y = r.center().y() - (a / 45.0) * (r.height() / 2.2);
            p.setPen(QPen(QColor(a == 0 ? 0xadb5bd : 0xd0d4da), 1,
                         a == 0 ? Qt::SolidLine : Qt::DotLine));
            p.drawLine(QPointF(r.left()+28, y), QPointF(r.right()-4, y));
            p.setPen(QColor(0x9c, 0xa3, 0xaf));
            p.setFont(QFont("Segoe UI", 7));
            p.drawText(QRectF(r.left()+2, y-7, 24, 14), Qt::AlignRight | Qt::AlignVCenter,
                       QString("%1").arg((int)a));
        }

        // Target elevation (dashed blue)
        if (!std::isnan(m_target)) {
            double ty = r.center().y() - (m_target / 45.0) * (r.height() / 2.2);
            p.setPen(QPen(QColor(0x3b, 0x82, 0xf6, 180), 1.5, Qt::DashLine));
            p.drawLine(QPointF(r.left()+28, ty), QPointF(r.right()-4, ty));
        }

        // Current elevation fill + line
        double ey = r.center().y() - (m_elevation / 45.0) * (r.height() / 2.2);
        double bottom = r.bottom() - 4;
        QPainterPath fill;
        fill.moveTo(r.left()+28, bottom);
        fill.lineTo(r.right()-4, bottom);
        fill.lineTo(r.right()-4, ey);
        fill.lineTo(r.left()+28, ey);
        fill.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3b, 0x82, 0xf6, 50));
        p.drawPath(fill);

        p.setPen(QPen(QColor(0x3b, 0x82, 0xf6), 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.left()+28, ey), QPointF(r.right()-4, ey));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xd0, 0xd4, 0xda), 1));
        p.drawRoundedRect(r, 4, 4);
    }

private:
    float m_elevation = 0;
    float m_target    = std::numeric_limits<float>::quiet_NaN();
};

// ── BiMotorWidget ─────────────────────────────────────────────
class BiMotorWidget : public QWidget {
    Q_OBJECT
public:
    explicit BiMotorWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(80, 100);
    }
    void setValues(float vel, float acc) { m_vel = vel; m_acc = acc; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r(4, 4, width()-8, height()-8);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf8, 0xf9, 0xfa));
        p.drawRoundedRect(r, 4, 4);

        double barH    = r.height() - 36;
        double barY    = r.top() + 6;
        double centerY = barY + barH / 2.0;
        double barW    = (r.width() - 20) / 2.0;
        double x1      = r.left() + 8;
        double x2      = x1 + barW + 4;

        auto drawBar = [&](double x, float val, QColor col) {
            double frac = qBound(-1.0, static_cast<double>(val) / 10.0, 1.0);
            double h    = frac * barH / 2.0;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xe5, 0xe7, 0xeb));
            p.drawRoundedRect(QRectF(x, barY, barW, barH), 3, 3);
            p.setBrush(col);
            if (h >= 0)
                p.drawRoundedRect(QRectF(x, centerY-h, barW, qMax(h, 1.0)), 2, 2);
            else
                p.drawRoundedRect(QRectF(x, centerY, barW, qMax(-h, 1.0)), 2, 2);
            p.setPen(QPen(QColor(0xd0, 0xd4, 0xda), 1));
            p.drawLine(QPointF(x, centerY), QPointF(x+barW, centerY));
        };

        drawBar(x1, m_vel, QColor(0x3b, 0x82, 0xf6, 200));
        drawBar(x2, m_acc, QColor(0xf5, 0x9e, 0x0b, 200));

        // Sub-labels
        p.setFont(QFont("Segoe UI", 7));
        p.setPen(QColor(0x9c, 0xa3, 0xaf));
        p.drawText(QRectF(x1, r.bottom()-22, barW, 10), Qt::AlignHCenter, "vel");
        p.drawText(QRectF(x2, r.bottom()-22, barW, 10), Qt::AlignHCenter, "acc");

        // Values
        p.setPen(QColor(0x1a, 0x23, 0x32));
        p.setFont(QFont("Segoe UI", 7));
        p.drawText(QRectF(x1, r.bottom()-12, barW, 10), Qt::AlignHCenter,
                   QString::number(m_vel, 'f', 1));
        p.drawText(QRectF(x2, r.bottom()-12, barW, 10), Qt::AlignHCenter,
                   QString::number(m_acc, 'f', 1));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xd0, 0xd4, 0xda), 1));
        p.drawRoundedRect(r, 4, 4);
    }

private:
    float m_vel = 0, m_acc = 0;
};

// ── AimWidget ─────────────────────────────────────────────────
// Dark 2D pad: drag to set heading/elevation target.
// Blue diamond = actual position (from telemetry).
// White crosshair = target position.
class AimWidget : public QWidget {
    Q_OBJECT
public:
    explicit AimWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(200, 200);
        setCursor(Qt::CrossCursor);
    }
    void setActual(float h, float e) { m_actH = h; m_actE = e; update(); }
    void setTarget(float h, float e) { m_tgtH = h; m_tgtE = e; update(); }
    void setArmed(bool turret)       { m_turretArmed = turret; update(); }

signals:
    void targetChanged(float heading, float elevation);

protected:
    void mousePressEvent(QMouseEvent *ev) override {
        if (ev->button() != Qt::LeftButton) return;
        m_dragging    = true;
        m_dragStart   = ev->pos();
        m_dragStartH  = m_tgtH;
        m_dragStartE  = m_tgtE;
    }
    void mouseMoveEvent(QMouseEvent *ev) override {
        if (!m_dragging) return;
        QPoint delta = ev->pos() - m_dragStart;
        float h = m_dragStartH + delta.x() * 0.5f;
        float e = m_dragStartE - delta.y() * 0.5f;
        h = static_cast<float>(fmod(h + 540.0, 360.0)) - 180.0f;
        e = qBound(-90.0f, e, 90.0f);
        m_tgtH = h; m_tgtE = e;
        update();
        emit targetChanged(m_tgtH, m_tgtE);
    }
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) m_dragging = false;
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.fillRect(rect(), QColor(0x1a, 0x23, 0x32));

        // Subtle grid
        p.setPen(QPen(QColor(0xff, 0xff, 0xff, 18), 1));
        for (int x = 0; x < width(); x += 48)  p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 48) p.drawLine(0, y, width(), y);
        // Center cross
        p.setPen(QPen(QColor(0xff, 0xff, 0xff, 35), 1, Qt::DashLine));
        p.drawLine(width()/2, 0, width()/2, height());
        p.drawLine(0, height()/2, width(), height()/2);

        QPointF tgtPt = worldToScreen(m_tgtH, m_tgtE);
        QPointF actPt = worldToScreen(m_actH, m_actE);

        // Actual position — blue diamond
        p.setPen(QPen(QColor(0x60, 0xa5, 0xfa), 1.5));
        p.setBrush(QColor(0x60, 0xa5, 0xfa, 60));
        QPolygonF diamond;
        double ds = 7;
        diamond << QPointF(actPt.x(),    actPt.y()-ds)
                << QPointF(actPt.x()+ds, actPt.y())
                << QPointF(actPt.x(),    actPt.y()+ds)
                << QPointF(actPt.x()-ds, actPt.y());
        p.drawPolygon(diamond);

        // Target — white crosshair
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        double cs = 13;
        p.drawLine(QPointF(tgtPt.x()-cs, tgtPt.y()), QPointF(tgtPt.x()-4,  tgtPt.y()));
        p.drawLine(QPointF(tgtPt.x()+4,  tgtPt.y()), QPointF(tgtPt.x()+cs, tgtPt.y()));
        p.drawLine(QPointF(tgtPt.x(), tgtPt.y()-cs), QPointF(tgtPt.x(), tgtPt.y()-4));
        p.drawLine(QPointF(tgtPt.x(), tgtPt.y()+4),  QPointF(tgtPt.x(), tgtPt.y()+cs));
        p.drawEllipse(tgtPt, 4.0, 4.0);

        // Coordinate overlay
        p.setPen(QColor(0xff, 0xff, 0xff, 110));
        p.setFont(QFont("Segoe UI", 9));
        p.drawText(QRectF(10, 8, 220, 18), Qt::AlignLeft,
            QString("TGT  %1\xc2\xb0  %2\xc2\xb0")
                .arg(m_tgtH,0,'f',1).arg(m_tgtE,0,'f',1));
        p.drawText(QRectF(10, 26, 220, 18), Qt::AlignLeft,
            QString("ACT  %1\xc2\xb0  %2\xc2\xb0")
                .arg(m_actH,0,'f',1).arg(m_actE,0,'f',1));

        // Drag hint
        if (!m_dragging) {
            p.setPen(QColor(0xff, 0xff, 0xff, 40));
            p.setFont(QFont("Segoe UI", 9));
            p.drawText(rect().adjusted(0,0,0,-8), Qt::AlignHCenter | Qt::AlignBottom,
                       "drag to aim");
        }

        // Disarmed overlay
        if (!m_turretArmed) {
            p.fillRect(rect(), QColor(0, 0, 0, 100));
            p.setPen(QColor(0xef, 0x44, 0x44));
            p.setFont(QFont("Segoe UI", 16, QFont::Bold));
            p.drawText(rect(), Qt::AlignCenter, "TURRET DISARMED");
        }
    }

private:
    float  m_tgtH = 0, m_tgtE = 0;
    float  m_actH = 0, m_actE = 0;
    bool   m_turretArmed = false;
    bool   m_dragging    = false;
    QPoint m_dragStart;
    float  m_dragStartH = 0, m_dragStartE = 0;

    QPointF worldToScreen(float h, float e) const {
        double nx = (h + 180.0) / 360.0;
        double ny = 1.0 - (e + 90.0) / 180.0;
        return { nx * width(), ny * height() };
    }
};

// ── HoldFireButton ────────────────────────────────────────────
// Hold 600 ms to fire; shows a progress arc while held.
class HoldFireButton : public QWidget {
    Q_OBJECT
public:
    explicit HoldFireButton(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(80, 80);
        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            m_elapsed += 16;
            if (m_elapsed >= m_holdMs) {
                m_timer->stop();
                m_elapsed  = 0;
                m_pressing = false;
                update();
                emit fired();
            } else {
                update();
            }
        });
    }

signals:
    void fired();

protected:
    void mousePressEvent(QMouseEvent *ev) override {
        if (!isEnabled() || ev->button() != Qt::LeftButton) return;
        m_pressing = true;
        m_elapsed  = 0;
        m_timer->start();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() != Qt::LeftButton) return;
        m_pressing = false;
        m_elapsed  = 0;
        m_timer->stop();
        update();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int     s = qMin(width(), height()) - 8;
        QRectF  r((width()-s)/2.0, (height()-s)/2.0, s, s);
        QPointF c = r.center();
        (void)c;

        bool en = isEnabled();
        QColor bgCol  = en ? QColor(0x45, 0x0a, 0x0a) : QColor(0xe5, 0xe7, 0xeb);
        QColor arcCol = QColor(0xef, 0x44, 0x44);
        QColor txtCol = en ? Qt::white : QColor(0x9c, 0xa3, 0xaf);
        QColor rimCol = en ? QColor(0xef, 0x44, 0x44, 160) : QColor(0xd1, 0xd5, 0xdb);

        p.setPen(Qt::NoPen);
        p.setBrush(bgCol);
        p.drawEllipse(r);

        // Progress arc
        if (m_pressing && m_elapsed > 0) {
            double frac = static_cast<double>(m_elapsed) / m_holdMs;
            p.setPen(QPen(arcCol, 5, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(r.adjusted(3,3,-3,-3), 90*16,
                      static_cast<int>(-frac * 360 * 16));
        }

        p.setPen(txtCol);
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(r, Qt::AlignCenter, m_pressing ? "HOLD" : "FIRE");

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(rimCol, 2));
        p.drawEllipse(r);
    }

private:
    QTimer *m_timer   = nullptr;
    bool    m_pressing = false;
    int     m_elapsed  = 0;
    int     m_holdMs   = 600;
};

// ── MainWindow ────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            QMessageBox::critical(nullptr, "Fatal", "WSAStartup failed.");
            std::exit(1);
        }
        setWindowTitle("Harbinger");
        setMinimumSize(1000, 660);
        buildUi();

        m_autoScanTimer = new QTimer(this);
        m_autoScanTimer->setSingleShot(true);
        connect(m_autoScanTimer, &QTimer::timeout, this, &MainWindow::doAutoScan);
        doAutoScan();
    }

    ~MainWindow() override { disconnectDevice(); WSACleanup(); }

private:
    // Topbar
    QLabel         *m_statusDot    = nullptr;
    QLabel         *m_statusLabel  = nullptr;
    QLabel         *m_masterBadge  = nullptr;
    QLabel         *m_turretBadge  = nullptr;
    QLabel         *m_gunBadge     = nullptr;

    // Sidebar controls
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

    // Main area
    AimWidget      *m_aimWidget  = nullptr;
    CompassWidget  *m_compass    = nullptr;
    ElevationWidget*m_elevation  = nullptr;
    BiMotorWidget  *m_motorA     = nullptr;
    BiMotorWidget  *m_motorB     = nullptr;

    // State
    bool   m_masterArmed = false;
    bool   m_turretArmed = false;
    bool   m_gunArmed    = false;

    // Networking
    QList<BtDevice> m_devices;
    SOCKET          m_sock     = INVALID_SOCKET;
    QThread        *m_ioThread = nullptr;
    IoWorker       *m_ioWorker = nullptr;
    bool            m_scanning = false;

    // Auto-scan
    QTimer *m_autoScanTimer = nullptr;
    bool    m_autoScan      = true;

    void buildUi() {
        auto *root  = new QWidget(this);
        setCentralWidget(root);
        auto *rootV = new QVBoxLayout(root);
        rootV->setContentsMargins(0, 0, 0, 0);
        rootV->setSpacing(0);

        // ── Topbar ────────────────────────────────────────────
        auto *topbar = new QWidget;
        topbar->setFixedHeight(44);
        topbar->setStyleSheet("background:#1a2332;");
        auto *topH = new QHBoxLayout(topbar);
        topH->setContentsMargins(14, 0, 14, 0);
        topH->setSpacing(8);

        m_statusDot = new QLabel;
        m_statusDot->setFixedSize(10, 10);
        m_statusDot->setStyleSheet("background:#6b7280; border-radius:5px;");
        topH->addWidget(m_statusDot);

        m_statusLabel = new QLabel("searching...");
        m_statusLabel->setStyleSheet("color:#9ca3af; font-size:12px;");
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

        auto *titleLbl = new QLabel("HARBINGER");
        titleLbl->setStyleSheet("color:white; font-size:15px; font-weight:700; letter-spacing:3px;");
        topH->addWidget(titleLbl);
        rootV->addWidget(topbar);

        // ── Content row ───────────────────────────────────────
        auto *content  = new QWidget;
        auto *contentH = new QHBoxLayout(content);
        contentH->setContentsMargins(0, 0, 0, 0);
        contentH->setSpacing(0);

        // Left sidebar
        auto *sidebar = new QWidget;
        sidebar->setFixedWidth(220);
        sidebar->setStyleSheet("background:#f3f4f6; border-right:1px solid #d1d5db;");
        auto *sideV = new QVBoxLayout(sidebar);
        sideV->setContentsMargins(10, 10, 10, 10);
        sideV->setSpacing(8);

        auto addSectionLabel = [&](const QString &text) {
            auto *lbl = new QLabel(text);
            lbl->setStyleSheet("color:#6b7280; font-size:10px; font-weight:700; letter-spacing:1px;");
            sideV->addWidget(lbl);
        };
        auto addSep = [&]() {
            auto *sep = new QWidget;
            sep->setFixedHeight(1);
            sep->setStyleSheet("background:#d1d5db;");
            sideV->addWidget(sep);
        };

        // Device section
        addSectionLabel("DEVICE");
        m_deviceList = new QListWidget;
        m_deviceList->setFixedHeight(90);
        m_deviceList->setStyleSheet(
            "background:white; border:1px solid #d1d5db; border-radius:4px;"
            " font-size:11px;");
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
        m_eventLog->setStyleSheet(
            "background:white; border:1px solid #d1d5db; border-radius:4px;"
            " font-size:10px; font-family:'Consolas','Courier New',monospace;");
        sideV->addWidget(m_eventLog);

        contentH->addWidget(sidebar);

        // Right: aim pad + telemetry strip
        auto *rightWidget = new QWidget;
        auto *rightV      = new QVBoxLayout(rightWidget);
        rightV->setContentsMargins(0, 0, 0, 0);
        rightV->setSpacing(0);

        m_aimWidget = new AimWidget;
        rightV->addWidget(m_aimWidget, 1);

        // Telemetry strip
        auto *strip = new QWidget;
        strip->setFixedHeight(155);
        strip->setStyleSheet("background:#f9fafb; border-top:1px solid #d1d5db;");
        auto *stripH = new QHBoxLayout(strip);
        stripH->setContentsMargins(10, 8, 10, 8);
        stripH->setSpacing(8);

        auto addTelCell = [&](QWidget *w, const QString &title) {
            auto *cell  = new QWidget;
            auto *cellV = new QVBoxLayout(cell);
            cellV->setContentsMargins(6, 4, 6, 4);
            cellV->setSpacing(2);
            auto *tlbl = new QLabel(title);
            tlbl->setStyleSheet(
                "color:#9ca3af; font-size:9px; font-weight:700; letter-spacing:1px;"
                " background:transparent; border:none;");
            tlbl->setAlignment(Qt::AlignHCenter);
            cellV->addWidget(tlbl);
            cellV->addWidget(w, 1);
            cell->setStyleSheet(
                "QWidget { background:white; border:1px solid #e5e7eb; border-radius:6px; }");
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

        rightV->addWidget(strip);
        contentH->addWidget(rightWidget, 1);
        rootV->addWidget(content, 1);

        // ── Global stylesheet ─────────────────────────────────
        qApp->setStyleSheet(R"(
QWidget {
    font-family: 'Segoe UI', sans-serif;
    font-size: 13px;
    color: #1a2332;
}
QPushButton {
    background: #f3f4f6;
    border: 1px solid #d1d5db;
    border-radius: 4px;
    padding: 5px 12px;
}
QPushButton:hover   { background: #e5e7eb; }
QPushButton:pressed { background: #d1d5db; }
QPushButton:disabled { color: #9ca3af; border-color: #e5e7eb; background: #f9fafb; }
QPushButton#masterArmBtn:checked { background: #f59e0b; color: white; border-color: #d97706; }
QPushButton#turretArmBtn:checked { background: #3b82f6; color: white; border-color: #2563eb; }
QPushButton#gunArmBtn:checked    { background: #ef4444; color: white; border-color: #dc2626; }
QSlider::groove:horizontal {
    height: 4px; background: #d1d5db; border-radius: 2px;
}
QSlider::sub-page:horizontal { background: #3b82f6; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; height: 14px;
    background: #3b82f6;
    border-radius: 7px;
    margin: -5px 0;
}
QSlider::handle:horizontal:disabled { background: #d1d5db; }
QListWidget { background: white; }
QListWidget::item:selected { background: #dbeafe; color: #1d4ed8; }
QScrollBar:vertical {
    width: 6px; background: transparent;
}
QScrollBar::handle:vertical {
    background: #d1d5db; border-radius: 3px; min-height: 20px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");

        // ── Signal connections ────────────────────────────────
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
        connect(m_fireBtn,  &HoldFireButton::fired,    this, &MainWindow::onFire);
        connect(m_aimWidget, &AimWidget::targetChanged, this, [this](float h, float e) {
            m_compass->setTarget(h);
            m_elevation->setTarget(e);
            if (m_ioWorker) m_ioWorker->sendAim(h, e);
        });
    }

    // ── Auto-scan / connect ───────────────────────────────────
    void doAutoScan() {
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

    void connectTo(const BtDevice &dev) {
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

    void onConnected(SOCKET sock, const QString &name) {
        m_sock     = sock;
        m_autoScan = false;
        m_autoScanTimer->stop();

        m_statusDot->setStyleSheet("background:#22c55e; border-radius:5px;");
        m_statusLabel->setText(name);
        m_statusLabel->setStyleSheet("color:#bbf7d0; font-size:12px;");

        setControlsEnabled(true);
        logEvent(QString("=== Connected to %1 ===").arg(name));

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
        m_masterBadge->setVisible(false);
        m_turretBadge->setVisible(false);
        m_gunBadge->setVisible(false);
        m_aimWidget->setArmed(false);
        m_autoScan = true;
        doAutoScan();
    }

    void setStatus(const QString &msg, bool connected) {
        if (connected) {
            m_statusDot->setStyleSheet("background:#22c55e; border-radius:5px;");
            m_statusLabel->setStyleSheet("color:#bbf7d0; font-size:12px;");
        } else {
            m_statusDot->setStyleSheet("background:#6b7280; border-radius:5px;");
            m_statusLabel->setStyleSheet("color:#9ca3af; font-size:12px;");
        }
        m_statusLabel->setText(msg);
    }

    void setControlsEnabled(bool en) {
        m_masterArmBtn->setEnabled(en);
        m_turretArmBtn->setEnabled(en);
        m_gunArmBtn->setEnabled(en);
        m_voltSlider->setEnabled(en);
        m_fireBtn->setEnabled(en);
        m_disconnBtnRef->setEnabled(en);
        m_scanBtnRef->setEnabled(!en);
        m_connectBtnRef->setEnabled(!en && m_deviceList->currentRow() >= 0);
    }

    // ── Arm toggles ───────────────────────────────────────────
    void onMasterArmToggled(bool checked) {
        if (!m_ioWorker) {
            m_masterArmBtn->blockSignals(true);
            m_masterArmBtn->setChecked(!checked);
            m_masterArmBtn->blockSignals(false);
            return;
        }
        uint8_t flags = static_cast<uint8_t>(
            (checked ? ARM_TRUE : ARM_FALSE) << ARM_SHIFT_MASTER);
        logEvent(QString("\xe2\x86\x92 ARM master=%1").arg(checked ? "true" : "false"));
        m_ioWorker->sendArm(flags);
    }
    void onTurretArmToggled(bool checked) {
        if (!m_ioWorker) {
            m_turretArmBtn->blockSignals(true);
            m_turretArmBtn->setChecked(!checked);
            m_turretArmBtn->blockSignals(false);
            return;
        }
        uint8_t flags = static_cast<uint8_t>(
            (checked ? ARM_TRUE : ARM_FALSE) << ARM_SHIFT_TURRET);
        logEvent(QString("\xe2\x86\x92 ARM turret=%1").arg(checked ? "true" : "false"));
        m_ioWorker->sendArm(flags);
    }
    void onGunArmToggled(bool checked) {
        if (!m_ioWorker) {
            m_gunArmBtn->blockSignals(true);
            m_gunArmBtn->setChecked(!checked);
            m_gunArmBtn->blockSignals(false);
            return;
        }
        uint8_t flags = static_cast<uint8_t>(
            (checked ? ARM_TRUE : ARM_FALSE) << ARM_SHIFT_GUN);
        logEvent(QString("\xe2\x86\x92 ARM gun=%1").arg(checked ? "true" : "false"));
        m_ioWorker->sendArm(flags);
    }
    void onVoltageReleased() {
        if (!m_ioWorker) return;
        float v = static_cast<float>(m_voltSlider->value());
        logEvent(QString("\xe2\x86\x92 SET_VOLTAGE %1 V").arg(v, 0, 'f', 0));
        m_ioWorker->sendSetVoltage(v);
    }
    void onFire() {
        if (!m_ioWorker) return;
        logEvent("\xe2\x86\x92 FIRE");
        m_ioWorker->sendFire();
    }

    // ── Packet receive ────────────────────────────────────────
    void handlePacket(uint8_t type, QByteArray payload) {
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
                .arg(pkt.heading,    0,'f',1).arg(pkt.elevation,  0,'f',1)
                .arg(pkt.motorA_vel, 0,'f',2).arg(pkt.motorB_vel, 0,'f',2);
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
                    .arg(i).arg(s.t_us).arg(s.v_mps,0,'f',2).arg(s.drain_v,0,'f',2);
            }
            logEvent(msg);
            break;
        }
        default:
            logEvent(QString("\xe2\x86\x90 unknown 0x%1").arg(type,2,16,QChar('0')));
        }
    }

    void logEvent(const QString &msg) {
        m_eventLog->append(msg);
        m_eventLog->verticalScrollBar()->setValue(
            m_eventLog->verticalScrollBar()->maximum());
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow win;
    win.show();
    return app.exec();
}

#include "main.moc"
