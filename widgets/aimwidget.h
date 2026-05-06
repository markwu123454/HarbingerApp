#pragma once
#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFont>
#include <QApplication>
#include <QCursor>
#include <cmath>
#include "../theme.h"

// 2D drag pad: click to capture mouse (3D-game style), ESC to release.
// Blue diamond = actual position (from telemetry).
// Themed crosshair = target position.
class AimWidget : public QWidget {
    Q_OBJECT
public:
    explicit AimWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(200, 200);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::CrossCursor);
    }
    void setActual(float h, float e) { m_actH = h; m_actE = e; update(); }
    void setTarget(float h, float e) { m_tgtH = h; m_tgtE = e; update(); }
    void setArmed(bool turret)       { m_turretArmed = turret; update(); }

signals:
    void targetChanged(float heading, float elevation);

protected:
    void mousePressEvent(QMouseEvent *ev) override {
        if (ev->button() != Qt::LeftButton || m_captured) return;
        m_captured = true;
        m_captureCenter = mapToGlobal(rect().center());
        QCursor::setPos(m_captureCenter);
        QApplication::setOverrideCursor(Qt::BlankCursor);
        grabMouse();
        setFocus();
        update();
    }

    void mouseMoveEvent(QMouseEvent *ev) override {
        if (!m_captured) return;
        QPoint globalPos = ev->globalPos();
        if (globalPos == m_captureCenter) return; // skip the warp-back synthetic event
        QPoint delta = globalPos - m_captureCenter;
        float h = m_tgtH + delta.x() * 0.5f;
        float e = m_tgtE - delta.y() * 0.5f;
        h = static_cast<float>(fmod(h + 540.0, 360.0)) - 180.0f;
        e = qBound(-90.0f, e, 90.0f);
        m_tgtH = h; m_tgtE = e;
        update();
        emit targetChanged(m_tgtH, m_tgtE);
        QCursor::setPos(m_captureCenter);
    }

    void keyPressEvent(QKeyEvent *ev) override {
        if (ev->key() == Qt::Key_Escape && m_captured)
            releaseCapture();
    }

    void focusOutEvent(QFocusEvent *) override {
        if (m_captured) releaseCapture();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.fillRect(rect(), Theme::aimBg());

        // Subtle grid
        p.setPen(QPen(Theme::gridLine(), 1));
        for (int x = 0; x < width();  x += 48) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 48) p.drawLine(0, y, width(), y);

        // Center cross
        p.setPen(QPen(Theme::centerLine(), 1, Qt::DashLine));
        p.drawLine(width() / 2, 0, width() / 2, height());
        p.drawLine(0, height() / 2, width(), height() / 2);

        QPointF tgtPt = worldToScreen(m_tgtH, m_tgtE);
        QPointF actPt = worldToScreen(m_actH, m_actE);

        // Actual position — blue diamond
        p.setPen(QPen(QColor(0x60, 0xa5, 0xfa), 1.5));
        p.setBrush(QColor(0x60, 0xa5, 0xfa, 60));
        double ds = 7;
        QPolygonF diamond;
        diamond << QPointF(actPt.x(),      actPt.y() - ds)
                << QPointF(actPt.x() + ds, actPt.y())
                << QPointF(actPt.x(),      actPt.y() + ds)
                << QPointF(actPt.x() - ds, actPt.y());
        p.drawPolygon(diamond);

        // Target — crosshair
        QColor ch = Theme::aimCrosshair();
        p.setPen(QPen(ch, 1.5));
        p.setBrush(Qt::NoBrush);
        double cs = 13;
        p.drawLine(QPointF(tgtPt.x() - cs, tgtPt.y()), QPointF(tgtPt.x() - 4,  tgtPt.y()));
        p.drawLine(QPointF(tgtPt.x() + 4,  tgtPt.y()), QPointF(tgtPt.x() + cs, tgtPt.y()));
        p.drawLine(QPointF(tgtPt.x(), tgtPt.y() - cs), QPointF(tgtPt.x(), tgtPt.y() - 4));
        p.drawLine(QPointF(tgtPt.x(), tgtPt.y() + 4),  QPointF(tgtPt.x(), tgtPt.y() + cs));
        p.drawEllipse(tgtPt, 4.0, 4.0);

        // Coordinate overlay
        p.setPen(Theme::aimOverlayText());
        p.setFont(QFont("Segoe UI", 9));
        p.drawText(QRectF(10, 8, 220, 18), Qt::AlignLeft,
            QString("TGT  %1\xc2\xb0  %2\xc2\xb0")
                .arg(m_tgtH, 0, 'f', 1).arg(m_tgtE, 0, 'f', 1));
        p.drawText(QRectF(10, 26, 220, 18), Qt::AlignLeft,
            QString("ACT  %1\xc2\xb0  %2\xc2\xb0")
                .arg(m_actH, 0, 'f', 1).arg(m_actE, 0, 'f', 1));

        // Hint
        p.setPen(Theme::aimHintText());
        p.setFont(QFont("Segoe UI", 9));
        p.drawText(rect().adjusted(0, 0, 0, -8),
                   Qt::AlignHCenter | Qt::AlignBottom,
                   m_captured ? "ESC to release" : "click to aim");

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
    bool   m_captured    = false;
    QPoint m_captureCenter;

    void releaseCapture() {
        m_captured = false;
        releaseMouse();
        QApplication::restoreOverrideCursor();
        update();
    }

    QPointF worldToScreen(float h, float e) const {
        double nx = (h + 180.0) / 360.0;
        double ny = 1.0 - (e + 90.0) / 180.0;
        return {nx * width(), ny * height()};
    }
};
