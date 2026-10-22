#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  DetachedView.h
//  A borderless fullscreen window that shows Cam1's live feed on
//  a second monitor. Controlled entirely from MainWindow.
//  Close it → feed goes back to the main window panel.
// ═══════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QLabel>
#include "VideoViewLabel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QKeyEvent>
#include <QPixmap>
#include <QScreen>
#include <QApplication>

class DetachedView : public QWidget
{
    Q_OBJECT
public:
    explicit DetachedView(QWidget* parent = nullptr) : QWidget(parent)
    {
        setWindowTitle("DualDisplay — Camera 1 (Monitor 2)");
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint
                       | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint);
        setStyleSheet("background-color: #060810;");

        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // ── Top bar ──────────────────────────────────────────────────
        QFrame* bar = new QFrame();
        bar->setFixedHeight(32);
        bar->setStyleSheet(
            "background: #1a1f2e; border-bottom: 1px solid #2a3048;");
        QHBoxLayout* bl = new QHBoxLayout(bar);
        bl->setContentsMargins(10, 0, 10, 0);

        m_titleLabel = new QLabel("◈  CAM 1  —  Monitor 2");
        m_titleLabel->setStyleSheet(
            "color:#5eaef5; font-family:'Courier New',monospace;"
            "font-size:12px; font-weight:bold; letter-spacing:2px;");

        m_fpsLabel = new QLabel("— fps");
        m_fpsLabel->setStyleSheet("color:#3eb87a; font-family:'Courier New',monospace; font-size:11px;");

        m_statusLabel = new QLabel("Frame: 0  |  Brightest: —");
        m_statusLabel->setStyleSheet("color:#3a5a8a; font-family:'Courier New',monospace; font-size:11px;");

        QPushButton* reattachBtn = new QPushButton("↩  Return to Main");
        reattachBtn->setStyleSheet(
            "background:#2a1f08; border:1px solid #f5c25e; color:#f5c25e;"
            "border-radius:3px; padding:3px 10px;"
            "font-family:'Courier New',monospace; font-size:11px;");
        connect(reattachBtn, &QPushButton::clicked, this, &DetachedView::requestReattach);

        bl->addWidget(m_titleLabel);
        bl->addStretch();
        bl->addWidget(m_fpsLabel);
        bl->addSpacing(20);
        bl->addWidget(m_statusLabel);
        bl->addSpacing(20);
        bl->addWidget(reattachBtn);
        root->addWidget(bar);

        // ── Image label ──────────────────────────────────────────────
        m_imageLabel = new VideoViewLabel();
        m_imageLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_imageLabel, 1);
    }

    // ── Move to second monitor and match requested window size ───────
    void moveToSecondMonitor(const QSize& preferredSize)
    {
        QList<QScreen*> screens = QApplication::screens();
        QScreen* target = (screens.size() > 1) ? screens[1] : screens[0];
        QRect geo = target->geometry();
        const QSize bounded = preferredSize.boundedTo(geo.size());
        const int x = geo.x() + (geo.width() - bounded.width()) / 2;
        const int y = geo.y() + (geo.height() - bounded.height()) / 2;
        setGeometry(QRect(QPoint(x, y), bounded));
        if (screens.size() < 2)
            setWindowTitle("DualDisplay — Camera 1 (No second monitor detected — showing on primary)");
    }

    QSize imageLabelSize() const { return m_imageLabel ? m_imageLabel->size() : QSize(1920, 1080); }

    void updateFrame(const QPixmap& px, int frameId, int brightest, double fps)
    {
        m_imageLabel->setPixmap(px);
        m_statusLabel->setText(QString("Frame: %1  |  Brightest: %2").arg(frameId).arg(brightest));
        m_fpsLabel->setText(fps > 0 ? QString("%1 fps").arg(fps, 0, 'f', 1) : "— fps");
    }

    void clearFrame()
    {
        m_imageLabel->clear();
        m_imageLabel->setText("No Signal");
        m_fpsLabel->setText("— fps");
        m_statusLabel->setText("Frame: 0  |  Brightest: —");
    }

signals:
    void requestReattach();   // user clicked "Return to Main"
    void closed();

protected:
    void closeEvent(QCloseEvent* e) override
    {
        emit closed();
        QWidget::closeEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) close();
        else QWidget::keyPressEvent(e);
    }

private:
    QLabel* m_imageLabel;
    QLabel* m_titleLabel;
    QLabel* m_fpsLabel;
    QLabel* m_statusLabel;
};
