#pragma once

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QFont>
#include <algorithm>

class VideoViewLabel : public QLabel
{
public:
    explicit VideoViewLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
    }

    void setDarkMode(bool dark)
    {
        if (m_darkMode != dark) {
            m_darkMode = dark;
            update();
        }
    }

    bool isDarkMode() const { return m_darkMode; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (!pixmap(Qt::ReturnByValue).isNull()) {
            QLabel::paintEvent(event);
            return;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // Fill background matching theme
        p.fillRect(rect(), m_darkMode ? QColor("#0c0c0c") : QColor("#f8f9fa"));

        // Draw transparent watermark logo behind text
        QPixmap logo(m_darkMode ? ":/resources/icons/logo-white.png"
                                : ":/resources/icons/logo-black.png");
        if (!logo.isNull() && width() > 40 && height() > 40) {
            int targetW = std::clamp(static_cast<int>(width() * 0.42), 240, 500);
            int targetH = targetW * logo.height() / logo.width();
            if (targetH > height() * 0.55 && height() > 60) {
                targetH = std::max(40, static_cast<int>(height() * 0.55));
                targetW = targetH * logo.width() / logo.height();
            }

            const QRect logoRect((width() - targetW) / 2, (height() - targetH) / 2, targetW, targetH);
            p.save();
            p.setOpacity(m_darkMode ? 0.25 : 0.18);
            p.drawPixmap(logoRect, logo);
            p.restore();
        }

        // Draw "No Signal" text over the center
        p.save();
        QFont f = font();
        f.setPointSize(16);
        f.setBold(true);
        p.setFont(f);
        p.setPen(m_darkMode ? QColor("#9b9b9b") : QColor("#718096"));
        p.drawText(rect(), Qt::AlignCenter, "No Signal");
        p.restore();
    }

private:
    bool m_darkMode = true;
};
