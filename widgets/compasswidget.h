#pragma once
#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <cmath>
#include <limits>
#include "../protocol.h"
#include "../theme.h"

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
        QRectF r((width() - s) / 2.0, (height() - s) / 2.0, s, s);
        QPointF c = r.center();
        double radius = s / 2.0;

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::widgetBg());
        p.drawEllipse(r);

        // Tick marks
        for (int deg = 0; deg < 360; deg += 10) {
            double rad   = (deg - 90) * PI / 180.0;
            double inner = (deg % 30 == 0) ? radius * 0.72 : radius * 0.84;
            bool   major = (deg % 30 == 0);
            p.setPen(QPen(major ? Theme::tickMajor() : Theme::tickMinor(), major ? 1.5 : 1.0));
            p.drawLine(
                QPointF(c.x() + inner        * cos(rad), c.y() + inner        * sin(rad)),
                QPointF(c.x() + radius * 0.94 * cos(rad), c.y() + radius * 0.94 * sin(rad)));
        }

        // Cardinal labels
        const struct { const char *lbl; int ang; } cards[] = {
            {"N",0}, {"E",90}, {"S",180}, {"W",270}
        };
        p.setFont(QFont("Segoe UI", 7, QFont::Bold));
        for (auto &card : cards) {
            double rad = (card.ang - 90) * PI / 180.0;
            double d   = radius * 0.56;
            p.setPen(Theme::textSecondary());
            QRectF tr(c.x() + d * cos(rad) - 7, c.y() + d * sin(rad) - 7, 14, 14);
            p.drawText(tr, Qt::AlignCenter, card.lbl);
        }

        // Target heading — dashed blue
        if (!std::isnan(m_target)) {
            double rad = (m_target - 90) * PI / 180.0;
            p.setPen(QPen(QColor(0x3b, 0x82, 0xf6, 180), 1.5, Qt::DashLine));
            p.drawLine(c, QPointF(c.x() + radius * 0.68 * cos(rad),
                                  c.y() + radius * 0.68 * sin(rad)));
        }

        // Heading needle — red
        double rad = (m_heading - 90) * PI / 180.0;
        p.setPen(QPen(QColor(0xef, 0x44, 0x44), 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(
            QPointF(c.x() - radius * 0.14 * cos(rad), c.y() - radius * 0.14 * sin(rad)),
            QPointF(c.x() + radius * 0.72 * cos(rad), c.y() + radius * 0.72 * sin(rad)));

        // Center dot
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::centerDot());
        p.drawEllipse(c, 3.0, 3.0);

        // Border ring
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::tickMinor(), 1));
        p.drawEllipse(r);
    }

private:
    float m_heading = 0;
    float m_target  = std::numeric_limits<float>::quiet_NaN();
};
