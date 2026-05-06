#pragma once
#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QTimer>
#include <QMouseEvent>
#include "theme.h"

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
        int    s = qMin(width(), height()) - 8;
        QRectF r((width() - s) / 2.0, (height() - s) / 2.0, s, s);

        bool en = isEnabled();
        bool dark = Theme::isDark();
        QColor bgCol  = en ? QColor(0x45, 0x0a, 0x0a)
                           : (dark ? QColor(0x2a, 0x2e, 0x38) : QColor(0xe5, 0xe7, 0xeb));
        QColor arcCol = QColor(0xef, 0x44, 0x44);
        QColor txtCol = en ? Qt::white
                           : (dark ? QColor(0x4b, 0x55, 0x63) : QColor(0x9c, 0xa3, 0xaf));
        QColor rimCol = en ? QColor(0xef, 0x44, 0x44, 160)
                           : (dark ? QColor(0x3a, 0x40, 0x4c) : QColor(0xd1, 0xd5, 0xdb));

        p.setPen(Qt::NoPen);
        p.setBrush(bgCol);
        p.drawEllipse(r);

        if (m_pressing && m_elapsed > 0) {
            double frac = static_cast<double>(m_elapsed) / m_holdMs;
            p.setPen(QPen(arcCol, 5, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(r.adjusted(3, 3, -3, -3), 90 * 16,
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
    QTimer *m_timer    = nullptr;
    bool    m_pressing = false;
    int     m_elapsed  = 0;
    int     m_holdMs   = 600;
};
