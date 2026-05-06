#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <cmath>
#include <limits>
#include "../theme.h"

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
        QRectF r(4, 4, width() - 8, height() - 8);

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::widgetBg());
        p.drawRoundedRect(r, 4, 4);

        const float angles[] = {-30, -15, 0, 15, 30};
        for (float a : angles) {
            double y = r.center().y() - (a / 45.0) * (r.height() / 2.2);
            bool   zero = (a == 0);
            p.setPen(QPen(zero ? Theme::tickMajor() : Theme::tickMinor(), 1,
                         zero ? Qt::SolidLine : Qt::DotLine));
            p.drawLine(QPointF(r.left() + 28, y), QPointF(r.right() - 4, y));
            p.setPen(Theme::textMuted());
            p.setFont(QFont("Segoe UI", 7));
            p.drawText(QRectF(r.left() + 2, y - 7, 24, 14),
                       Qt::AlignRight | Qt::AlignVCenter, QString("%1").arg((int)a));
        }

        // Target elevation — dashed blue
        if (!std::isnan(m_target)) {
            double ty = r.center().y() - (m_target / 45.0) * (r.height() / 2.2);
            p.setPen(QPen(QColor(0x3b, 0x82, 0xf6, 180), 1.5, Qt::DashLine));
            p.drawLine(QPointF(r.left() + 28, ty), QPointF(r.right() - 4, ty));
        }

        // Current elevation fill + line
        double ey     = r.center().y() - (m_elevation / 45.0) * (r.height() / 2.2);
        double bottom = r.bottom() - 4;
        QPainterPath fill;
        fill.moveTo(r.left() + 28, bottom);
        fill.lineTo(r.right() - 4, bottom);
        fill.lineTo(r.right() - 4, ey);
        fill.lineTo(r.left() + 28, ey);
        fill.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3b, 0x82, 0xf6, 50));
        p.drawPath(fill);

        p.setPen(QPen(QColor(0x3b, 0x82, 0xf6), 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.left() + 28, ey), QPointF(r.right() - 4, ey));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::tickMinor(), 1));
        p.drawRoundedRect(r, 4, 4);
    }

private:
    float m_elevation = 0;
    float m_target    = std::numeric_limits<float>::quiet_NaN();
};
