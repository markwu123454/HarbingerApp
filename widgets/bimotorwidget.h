#pragma once
#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QFont>
#include "theme.h"

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
        QRectF r(4, 4, width() - 8, height() - 8);

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::widgetBg());
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
            p.setBrush(Theme::trackBg());
            p.drawRoundedRect(QRectF(x, barY, barW, barH), 3, 3);
            p.setBrush(col);
            if (h >= 0)
                p.drawRoundedRect(QRectF(x, centerY - h, barW, qMax(h, 1.0)), 2, 2);
            else
                p.drawRoundedRect(QRectF(x, centerY, barW, qMax(-h, 1.0)), 2, 2);
            p.setPen(QPen(Theme::tickMinor(), 1));
            p.drawLine(QPointF(x, centerY), QPointF(x + barW, centerY));
        };

        drawBar(x1, m_vel, QColor(0x3b, 0x82, 0xf6, 200));
        drawBar(x2, m_acc, QColor(0xf5, 0x9e, 0x0b, 200));

        p.setFont(QFont("Segoe UI", 7));
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(x1, r.bottom() - 22, barW, 10), Qt::AlignHCenter, "vel");
        p.drawText(QRectF(x2, r.bottom() - 22, barW, 10), Qt::AlignHCenter, "acc");

        p.setPen(Theme::textPrimary());
        p.setFont(QFont("Segoe UI", 7));
        p.drawText(QRectF(x1, r.bottom() - 12, barW, 10), Qt::AlignHCenter,
                   QString::number(m_vel, 'f', 1));
        p.drawText(QRectF(x2, r.bottom() - 12, barW, 10), Qt::AlignHCenter,
                   QString::number(m_acc, 'f', 1));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::tickMinor(), 1));
        p.drawRoundedRect(r, 4, 4);
    }

private:
    float m_vel = 0, m_acc = 0;
};
