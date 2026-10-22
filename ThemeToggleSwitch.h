#pragma once

#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPointer>
#include <cmath>

class ThemeToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal offset READ offset WRITE setOffset)

public:
    explicit ThemeToggleSwitch(QWidget* parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(42, 20);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_StyledBackground, false);
        setStyleSheet("QAbstractButton { background: transparent; border: none; outline: none; }");
    }

    qreal offset() const { return m_offset; }
    void setOffset(qreal o) { m_offset = o; update(); }

    void setDarkMode(bool dark, bool animate = true)
    {
        m_darkMode = dark;
        const qreal target = dark ? 0.0 : 1.0;
        setToolTip(dark ? "Dark Theme (Click for Light)" : "Light Theme (Click for Dark)");
        if (!animate || std::abs(m_offset - target) < 0.001) {
            m_offset = target;
            update();
            return;
        }
        if (m_anim) {
            m_anim->stop();
            delete m_anim;
            m_anim = nullptr;
        }
        m_anim = new QPropertyAnimation(this, "offset", this);
        m_anim->setDuration(160);
        m_anim->setStartValue(m_offset);
        m_anim->setEndValue(target);
        m_anim->setEasingCurve(QEasingCurve::InOutQuad);
        m_anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    bool isDarkMode() const { return m_darkMode; }

    QSize sizeHint() const override { return QSize(42, 20); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF trackRect = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        const QColor darkTrack("#141721");
        const QColor lightTrack("#e2e8f0");
        const QColor darkBorder("#2e3648");
        const QColor lightBorder("#cbd5e1");

        const int r = darkTrack.red()   + static_cast<int>((lightTrack.red()   - darkTrack.red())   * m_offset);
        const int g = darkTrack.green() + static_cast<int>((lightTrack.green() - darkTrack.green()) * m_offset);
        const int b = darkTrack.blue()  + static_cast<int>((lightTrack.blue()  - darkTrack.blue())  * m_offset);

        const int br = darkBorder.red()   + static_cast<int>((lightBorder.red()   - darkBorder.red())   * m_offset);
        const int bg = darkBorder.green() + static_cast<int>((lightBorder.green() - darkBorder.green()) * m_offset);
        const int bb = darkBorder.blue()  + static_cast<int>((lightBorder.blue()  - darkBorder.blue())  * m_offset);

        p.setPen(QPen(QColor(br, bg, bb), 1.2));
        p.setBrush(QColor(r, g, b));
        p.drawRoundedRect(trackRect, trackRect.height() / 2.0, trackRect.height() / 2.0);

        const qreal knobDiameter = trackRect.height() - 4.0;
        const qreal travel = trackRect.width() - knobDiameter - 4.0;
        const qreal knobX = trackRect.left() + 2.0 + (m_offset * travel);
        const qreal knobY = trackRect.top() + 2.0;
        const QRectF knobRect(knobX, knobY, knobDiameter, knobDiameter);

        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(knobRect);

        const qreal cx = knobRect.center().x();
        const qreal cy = knobRect.center().y();

        if (m_offset < 0.5) {
            // Moon symbol inside circle when pointing to left (Dark mode)
            const qreal radius = knobDiameter * 0.32;
            QPainterPath fullCircle;
            fullCircle.addEllipse(QPointF(cx, cy), radius, radius);
            QPainterPath cutout;
            cutout.addEllipse(QPointF(cx + radius * 0.55, cy - radius * 0.35), radius * 0.85, radius * 0.85);
            QPainterPath crescent = fullCircle.subtracted(cutout);

            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#c9a15d"));
            p.drawPath(crescent);
        } else {
            // Sun symbol inside circle when pointing to right (Light mode)
            const qreal rCore = knobDiameter * 0.16;
            const qreal rInner = knobDiameter * 0.25;
            const qreal rOuter = knobDiameter * 0.38;
            const QColor sunColor("#d97706");

            p.setPen(Qt::NoPen);
            p.setBrush(sunColor);
            p.drawEllipse(QPointF(cx, cy), rCore, rCore);

            p.setPen(QPen(sunColor, 1.3, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 8; ++i) {
                const qreal angle = i * (3.14159265358979323846 / 4.0);
                const qreal cosA = std::cos(angle);
                const qreal sinA = std::sin(angle);
                p.drawLine(QPointF(cx + rInner * cosA, cy + rInner * sinA),
                           QPointF(cx + rOuter * cosA, cy + rOuter * sinA));
            }
        }
    }

private:
    qreal m_offset = 0.0;
    bool m_darkMode = true;
    QPointer<QPropertyAnimation> m_anim;
};

