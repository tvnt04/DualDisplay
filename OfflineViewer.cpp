#include "OfflineViewer.h"
#include "CameraWorker.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>

#include <QTime>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QStyle>
#include <QPixmap>
#include <QImage>
#include <QMainWindow>
#include <QStatusBar>
#include <QFrame>
#include <QTabBar>
#include <QRegularExpression>
#include <QPainter>
#include <QSignalBlocker>

namespace {
double estimateRecordedFps(const QString& captureDir, bool isMerged, int camIndex)
{
    if (isMerged) {
        QFile metadataCsv(QDir(captureDir).filePath("metadata.csv"));
        if (metadataCsv.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&metadataCsv);
            qint64 firstTimestamp = 0;
            qint64 lastTimestamp = 0;
            int frameCount = 0;
            while (!in.atEnd()) {
                const QStringList fields = in.readLine().split(',');
                if (fields.size() < 5 || fields[0] == "frame_index") continue;
                bool ok0 = false;
                bool ok1 = false;
                const qint64 ts0 = fields[2].toLongLong(&ok0);
                const qint64 ts1 = fields[4].toLongLong(&ok1);
                if (!ok0 || !ok1) continue;
                const qint64 pairTimestamp = ts0 + (ts1 - ts0) / 2;
                if (frameCount == 0) firstTimestamp = pairTimestamp;
                lastTimestamp = pairTimestamp;
                ++frameCount;
            }
            metadataCsv.close();
            const qint64 spanMs = lastTimestamp - firstTimestamp;
            if (frameCount > 1 && spanMs > 0)
                return static_cast<double>(frameCount - 1) * 1000.0 / spanMs;
        }
    }

    QFile sessionLog(QDir(captureDir).filePath("session.log"));
    if (!sessionLog.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0;

    const QString cameraTag = QString("[Cam%1]").arg(camIndex);
    double allFpsTotal = 0.0;
    int allFpsCount = 0;
    double savedFpsTotal = 0.0;
    int savedFpsCount = 0;
    bool sawSaveStart = false;
    QTextStream in(&sessionLog);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if ((isMerged && (line.contains("[MergeSave] Started") || line.contains("Saving → Started"))) ||
            (!isMerged && (line.contains(cameraTag + " Save started") || line.contains(cameraTag + " Saving → Started")))) {
            sawSaveStart = true;
            continue;
        }

        if (line.contains("Grabber instant FPS: ") && (isMerged || line.contains(cameraTag))) {
            bool ok = false;
            const double fps = line.section("Grabber instant FPS: ", -1).trimmed().toDouble(&ok);
            if (ok && fps > 0.0) {
                allFpsTotal += fps;
                ++allFpsCount;
                if (sawSaveStart) {
                    savedFpsTotal += fps;
                    ++savedFpsCount;
                }
            }
        }
    }
    sessionLog.close();

    const int sampleCount = savedFpsCount > 0 ? savedFpsCount : allFpsCount;
    const double sampleTotal = savedFpsCount > 0 ? savedFpsTotal : allFpsTotal;
    return sampleCount > 0 ? sampleTotal / sampleCount : 0.0;
}

class OfflineThemeSwitch final : public QAbstractButton
{
public:
    explicit OfflineThemeSwitch(QWidget* parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(40, 24);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_StyledBackground, false);
        setStyleSheet("QAbstractButton { background: transparent; border: none; outline: none; }");
    }

    QSize sizeHint() const override { return QSize(40, 24); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF trackRect = rect().adjusted(1.5, 2.5, -1.5, -2.5);
        const bool dark = isChecked();
        const QColor trackColor = dark ? QColor("#27ae60") : QColor("#d5d8dc");
        const QColor borderColor = dark ? QColor("#229954") : QColor("#aeb6bf");

        painter.setPen(QPen(borderColor, 1.2));
        painter.setBrush(trackColor);
        painter.drawRoundedRect(trackRect, trackRect.height() / 2.0, trackRect.height() / 2.0);

        const qreal knobDiameter = trackRect.height() - 4.0;
        const qreal knobX = dark ? trackRect.right() - knobDiameter - 2.0 : trackRect.left() + 2.0;
        const QRectF knobRect(knobX, trackRect.top() + 2.0, knobDiameter, knobDiameter);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(knobRect);
    }
};

bool resolveCameraPair(const QString& selectedPath,
                       QString& leftPath,
                       QString& rightPath,
                       QString& statusText)
{
    const QFileInfo selectedInfo(selectedPath);
    const QString folderName = selectedInfo.fileName();
    const QRegularExpression cameraFolderRe("^Cam(\\d+)_(.+)$");
    const QRegularExpressionMatch match = cameraFolderRe.match(folderName);
    if (!match.hasMatch()) return false;

    const int selectedNumber = match.captured(1).toInt();
    const QString suffix = match.captured(2);
    const QDir parentDir(selectedInfo.absolutePath());

    QList<int> candidates;
    if (selectedNumber == 0) {
        candidates << 1;
    } else {
        candidates << (selectedNumber + 1) << (selectedNumber - 1);
    }

    for (int pairNumber : candidates) {
        if (pairNumber < 0) continue;
        const QString pairFolder = QString("Cam%1_%2").arg(pairNumber).arg(suffix);
        const QString pairPath = parentDir.absoluteFilePath(pairFolder);
        if (!QDir(pairPath).exists()) continue;

        if (pairNumber > selectedNumber) {
            leftPath = selectedPath;
            rightPath = pairPath;
        } else {
            leftPath = pairPath;
            rightPath = selectedPath;
        }

        statusText = QString("Auto-detected camera pair: %1 + %2.")
            .arg(QFileInfo(leftPath).fileName())
            .arg(QFileInfo(rightPath).fileName());
        return true;
    }

    return false;
}
}

OfflineViewer::OfflineViewer(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* offlineTabLayout = new QVBoxLayout(this);
    offlineTabLayout->setContentsMargins(0, 0, 0, 0);

    QWidget* offlineTopToolbar = new QWidget(this);
    offlineTopToolbar->setObjectName("offlineToolbar");
    QHBoxLayout* offlineTbLayout = new QHBoxLayout(offlineTopToolbar);
    offlineTbLayout->setContentsMargins(8, 4, 8, 4);
    offlineTbLayout->addWidget(new QLabel("Offline Layouts:", offlineTopToolbar));
    
    m_offlineLayoutCombo = new QComboBox(offlineTopToolbar);
    m_offlineLayoutCombo->setObjectName("layoutCombo");
    m_offlineLayoutCombo->addItem("Side by Side", 0);
    m_offlineLayoutCombo->addItem("Top/Bottom", 1);
    m_offlineLayoutCombo->addItem("Dual Control", 2);
    m_offlineLayoutCombo->addItem("Detached", 3);
    offlineTbLayout->addWidget(m_offlineLayoutCombo);

    offlineTbLayout->addSpacing(15);
    m_offlineAnalysisBtn = new QPushButton("📊 Histogram & Analysis", offlineTopToolbar);
    m_offlineAnalysisBtn->setCheckable(true);
    offlineTbLayout->addWidget(m_offlineAnalysisBtn);

    QLabel* lightThemeLabel = new QLabel("Light", offlineTopToolbar);
    lightThemeLabel->setObjectName("modeSideLabel");
    offlineTbLayout->addWidget(lightThemeLabel);

    m_offlineThemeBtn = new OfflineThemeSwitch(offlineTopToolbar);
    m_offlineThemeBtn->setObjectName("offlineDarkModeSwitch");
    m_offlineThemeBtn->setToolTip("Theme switch: left is Light, right is Dark");
    offlineTbLayout->addWidget(m_offlineThemeBtn);
    connect(m_offlineThemeBtn, &QAbstractButton::clicked, this, [this]() {
        emit themeToggleRequested(m_offlineThemeBtn->isChecked());
    });

    QLabel* darkThemeLabel = new QLabel("Dark", offlineTopToolbar);
    darkThemeLabel->setObjectName("modeSideLabel");
    offlineTbLayout->addWidget(darkThemeLabel);

    offlineTbLayout->addStretch(1);
    offlineTabLayout->addWidget(offlineTopToolbar);

    // ── Create image view container first ────────────────────────────────────
    m_offlineViewContainer = new QWidget(this);
    QVBoxLayout* containerLayout = new QVBoxLayout(m_offlineViewContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    m_offlineViewSplitter = new QSplitter(Qt::Horizontal, m_offlineViewContainer);
    m_offlineViewSplitter->setChildrenCollapsible(false);

    m_offlineViewTabs = new QTabWidget(m_offlineViewContainer);
    m_offlineViewTabs->setObjectName("offlineViewTabs");
    m_offlineViewTabs->setDocumentMode(true);
    m_offlineViewTabs->tabBar()->setExpanding(true);
    m_offlineViewTabs->hide();

    containerLayout->addWidget(m_offlineViewSplitter);
    containerLayout->addWidget(m_offlineViewTabs);

    // ── Vertical splitter: image views (top) ↔ analysis graphs (bottom) ────
    m_viewGraphSplitter = new QSplitter(Qt::Vertical, this);
    m_viewGraphSplitter->setObjectName("viewGraphSplitter");
    m_viewGraphSplitter->setChildrenCollapsible(false);
    m_viewGraphSplitter->setHandleWidth(8);

    m_viewGraphSplitter->addWidget(m_offlineViewContainer);

    m_analysisPanel = new AnalysisPanelWidget(m_viewGraphSplitter);
    m_analysisPanel->setMinimumHeight(160);
    m_analysisPanel->hide();
    m_viewGraphSplitter->addWidget(m_analysisPanel);

    // Default sizes: give image ~65%, graph ~35% of total height
    // (graph is hidden initially so these only matter once it's shown)
    m_viewGraphSplitter->setSizes({600, 280});

    offlineTabLayout->addWidget(m_viewGraphSplitter, 1);

    connect(m_offlineAnalysisBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (m_analysisPanel) {
            m_analysisPanel->setVisible(checked);
            if (checked) {
                // If graph panel was fully collapsed, restore a sensible split
                QList<int> sz = m_viewGraphSplitter->sizes();
                int total = sz[0] + sz[1];
                if (sz[1] < m_analysisPanel->minimumHeight() + 10 && total > 0) {
                    int graphH = qMax(m_analysisPanel->minimumHeight(), total / 3);
                    m_viewGraphSplitter->setSizes({total - graphH, graphH});
                }
                // Re-render last frame into the panel
                for (int i = 0; i < 2; ++i) {
                    if (m_lastFrameWidth[i] > 0 && m_lastFrameHeight[i] > 0 && !m_lastFramePixels[i].isEmpty()) {
                        renderOfflineFrame(i, m_lastFramePixels[i], m_lastFrameWidth[i], m_lastFrameHeight[i]);
                        break;
                    }
                }
            }
        }
    });

    connect(m_analysisPanel, &AnalysisPanelWidget::closeRequested, this, [this]() {
        if (m_offlineAnalysisBtn) m_offlineAnalysisBtn->setChecked(false);
        if (m_analysisPanel) m_analysisPanel->hide();
    });

    for (int i = 0; i < 2; ++i) {

        m_offlineViewWidgets[i] = new QWidget(m_offlineViewSplitter);
        QVBoxLayout* vl = new QVBoxLayout(m_offlineViewWidgets[i]);
        vl->setContentsMargins(0,0,0,0);
        vl->setSpacing(0);
        
        QFrame* bar = new QFrame(m_offlineViewWidgets[i]);
        bar->setObjectName("viewBar");
        bar->setFixedHeight(24);
        QHBoxLayout* bl = new QHBoxLayout(bar);
        bl->setContentsMargins(8,0,8,0);
        
        m_offlineViewTitleLabels[i] = new QLabel(QString("CAM %1").arg(i), bar);
        m_offlineViewTitleLabels[i]->setObjectName("viewBarTitle");
        bl->addWidget(m_offlineViewTitleLabels[i]);
        bl->addStretch();
        
        m_offlineViewSensorTempLabels[i] = new QLabel("S: — °C", bar);
        m_offlineViewSensorTempLabels[i]->setObjectName("sensorTempLabel");
        m_offlineViewSensorTempLabels[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bl->addWidget(m_offlineViewSensorTempLabels[i]);

        bl->addSpacing(8);

        m_offlineViewGrabberTempLabels[i] = new QLabel("G: — °C", bar);
        m_offlineViewGrabberTempLabels[i]->setObjectName("grabberTempLabel");
        m_offlineViewGrabberTempLabels[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bl->addWidget(m_offlineViewGrabberTempLabels[i]);

        bl->addSpacing(12);
        
        m_offlineViewFpsLabels[i] = new QLabel("— fps", bar);
        m_offlineViewFpsLabels[i]->setObjectName("fpsLabel");
        m_offlineViewFpsLabels[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bl->addWidget(m_offlineViewFpsLabels[i]);
        vl->addWidget(bar);

        m_offlineViewScrolls[i] = new QScrollArea(m_offlineViewWidgets[i]);
        m_offlineViewLabels[i]  = new QLabel("Ready for Playback", m_offlineViewScrolls[i]);
        m_offlineViewLabels[i]->setAlignment(Qt::AlignCenter);
        m_offlineViewLabels[i]->installEventFilter(this);
        m_offlineViewLabels[i]->setMouseTracking(true);
        m_offlineViewScrolls[i]->setWidget(m_offlineViewLabels[i]);
        m_offlineViewScrolls[i]->setWidgetResizable(true);
        vl->addWidget(m_offlineViewScrolls[i], 1);

        m_offlineViewStatusLabels[i] = new QLabel("Playback stopped", m_offlineViewWidgets[i]);
        m_offlineViewStatusLabels[i]->setFixedHeight(20);
        m_offlineViewStatusLabels[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        vl->addWidget(m_offlineViewStatusLabels[i]);
        
        QWidget* sliderBar = new QWidget(m_offlineViewWidgets[i]);
        sliderBar->setFixedHeight(28);
        QHBoxLayout* sbl = new QHBoxLayout(sliderBar);
        sbl->setContentsMargins(4, 4, 4, 4);
        m_offlineTimeLabels[i] = new QLabel("00:00 / 00:00", sliderBar);
        m_offlineSliders[i] = new QSlider(Qt::Horizontal, sliderBar);
        m_offlineSliders[i]->setRange(0, 0);
        m_offlineSliders[i]->setEnabled(false);
        sbl->addWidget(m_offlineSliders[i]);
        sbl->addWidget(m_offlineTimeLabels[i]);
        vl->addWidget(sliderBar);
        
        connect(m_offlineSliders[i], &QSlider::valueChanged, this, [this, i](int val) {
            if (m_playing[i]) onOfflinePauseClicked(i);
            m_playbackIndex[i] = val;
            m_lastOfflineLogSecond = -1;
            onPlaybackTimer(i);
        });
        
        m_offlineViewSplitter->addWidget(m_offlineViewWidgets[i]);
    }
    m_offlineHost = new QWidget(this);
    m_offlineHost->setObjectName("offlineHost");
    m_offlineHostLayout = new QHBoxLayout(m_offlineHost);
    m_offlineHostLayout->setContentsMargins(8, 8, 8, 8);
    m_offlineHostLayout->setSpacing(8);
    
    QLabel* offlineLabel = new QLabel("Offline Playback:", m_offlineHost);
    m_offlinePathEdit = new QLineEdit(m_offlineHost);
    m_offlinePathEdit->setPlaceholderText("Select capture folder...");
    m_offlineBrowseBtn = new QPushButton("Browse", m_offlineHost);
    m_offlineBrowseBtn->setObjectName("smallBtn");
    
    m_offlinePlayAllBtn = new QPushButton("▶ All", m_offlineHost);
    m_offlinePauseAllBtn = new QPushButton("⏸ All", m_offlineHost);
    m_offlineStopAllBtn = new QPushButton("⏹ All", m_offlineHost);
    m_offlinePlayAllBtn->setObjectName("playBtn");
    m_offlinePauseAllBtn->setObjectName("smallBtn");
    m_offlineStopAllBtn->setObjectName("smallBtn");
    
    // Create individual camera control widgets
    for (int i = 0; i < 2; ++i) {
        m_offlineCamControlWidgets[i] = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(m_offlineCamControlWidgets[i]);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(6);
        
        m_offlinePlayBtn[i] = new QPushButton(QString("▶ C%1").arg(i), m_offlineCamControlWidgets[i]);
        m_offlinePauseBtn[i] = new QPushButton(QString("⏸ C%1").arg(i), m_offlineCamControlWidgets[i]);
        m_offlineStopBtn[i] = new QPushButton(QString("⏹ C%1").arg(i), m_offlineCamControlWidgets[i]);
        m_offlinePlayBtn[i]->setObjectName("smallBtn");
        m_offlinePauseBtn[i]->setObjectName("smallBtn");
        m_offlineStopBtn[i]->setObjectName("smallBtn");
        m_offlinePauseBtn[i]->hide();
        m_offlineStopBtn[i]->hide();
        
        l->addWidget(m_offlinePlayBtn[i]);
    }

    // Flat layout container
    m_offlineFlatControlsWidget = new QWidget(m_offlineHost);
    QHBoxLayout* flatLayout = new QHBoxLayout(m_offlineFlatControlsWidget);
    flatLayout->setContentsMargins(0, 0, 0, 0);
    flatLayout->setSpacing(8);

    QFrame* div2 = new QFrame(m_offlineFlatControlsWidget); div2->setFrameShape(QFrame::VLine);
    flatLayout->addWidget(div2);
    flatLayout->addWidget(m_offlineCamControlWidgets[0]);

    QFrame* div3 = new QFrame(m_offlineFlatControlsWidget); div3->setFrameShape(QFrame::VLine);
    flatLayout->addWidget(div3);
    flatLayout->addWidget(m_offlineCamControlWidgets[1]);

    // Active control container for Dual Control mode
    m_offlineActiveControlContainer = new QWidget(m_offlineHost);
    QHBoxLayout* activeLayout = new QHBoxLayout(m_offlineActiveControlContainer);
    activeLayout->setContentsMargins(0, 0, 0, 0);
    activeLayout->setSpacing(8);
    QFrame* activeDiv = new QFrame(m_offlineActiveControlContainer); activeDiv->setFrameShape(QFrame::VLine);
    activeLayout->addWidget(activeDiv);
    m_offlineActiveControlContainer->hide();

    m_offlineHostLayout->addWidget(offlineLabel);
    m_offlineHostLayout->addWidget(m_offlinePathEdit, 1);
    m_offlineHostLayout->addWidget(m_offlineBrowseBtn);
    
    m_offlineDiv1 = new QFrame(m_offlineHost); m_offlineDiv1->setFrameShape(QFrame::VLine);
    m_offlineHostLayout->addWidget(m_offlineDiv1);
    m_offlineHostLayout->addWidget(m_offlinePlayAllBtn);
    m_offlinePauseAllBtn->hide();
    m_offlineHostLayout->addWidget(m_offlineStopAllBtn);

    m_offlineHostLayout->addWidget(m_offlineFlatControlsWidget);
    m_offlineHostLayout->addWidget(m_offlineActiveControlContainer);

    connect(m_offlineBrowseBtn, &QPushButton::clicked, this, &OfflineViewer::onOfflineBrowse);
    connect(m_offlinePlayAllBtn, &QPushButton::clicked, this, &OfflineViewer::onOfflinePlayAllClicked);
    connect(m_offlinePauseAllBtn, &QPushButton::clicked, this, &OfflineViewer::onOfflinePauseAllClicked);
    connect(m_offlineStopAllBtn, &QPushButton::clicked, this, &OfflineViewer::onOfflineStopAllClicked);
    
    connect(m_offlineLayoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int preset = m_offlineLayoutCombo->itemData(index).toInt();
        switch(preset) {
            case 0: applyOfflinePresetSideBySide(); break;
            case 1: applyOfflinePresetTopBottom(); break;
            case 2: applyOfflinePresetDualControl(); break;
            case 3: applyOfflinePresetDetached(); break;
            default: applyOfflinePresetSideBySide(); break;
        }
    });

    for (int i = 0; i < 2; ++i) {
        connect(m_offlinePlayBtn[i], &QPushButton::clicked, this, [this, i]{ onOfflinePlayClicked(i); });
        connect(m_offlinePauseBtn[i], &QPushButton::clicked, this, [this, i]{ onOfflinePauseClicked(i); });
        connect(m_offlineStopBtn[i], &QPushButton::clicked, this, [this, i]{ onOfflineStopClicked(i); });
    }

    // Sync active controls when changing view tabs
    connect(m_offlineViewTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0 && m_offlineActiveControlContainer->isVisible()) {
            m_offlineCamControlWidgets[0]->setVisible(index == 0);
            m_offlineCamControlWidgets[1]->setVisible(index == 1);
        }
    });

    offlineTabLayout->addWidget(m_offlineHost);

    m_offlineLogTerminal = new QPlainTextEdit(this);
    m_offlineLogTerminal->setObjectName("logTerminal");
    m_offlineLogTerminal->setReadOnly(true);
    m_offlineLogTerminal->setMaximumBlockCount(2000);
    m_offlineLogTerminal->setPlaceholderText("Offline Runtime logs...");
    m_offlineLogTerminal->setMinimumHeight(72);
    m_offlineLogTerminal->setMaximumHeight(96);
    offlineTabLayout->addWidget(m_offlineLogTerminal);

    QWidget* mw = parentWidget();
    while (mw) {
        if (auto* qmw = qobject_cast<QMainWindow*>(mw)) {
            connect(qmw, SIGNAL(displaySettingsChanged()), this, SLOT(onDisplaySettingsChanged()));
            break;
        }
        mw = mw->parentWidget();
    }
    applyOfflinePresetSideBySide();
}

OfflineViewer::~OfflineViewer()
{
    for (int i = 0; i < 2; ++i) {
        if (m_playbackTimers[i]) {
            m_playbackTimers[i]->stop();
            delete m_playbackTimers[i];
        }
        if (m_playbackFile[i]) {
            if (m_playbackFile[i]->isOpen()) {
                m_playbackFile[i]->close();
            }
            delete m_playbackFile[i];
        }
    }
    if (m_offlineDetachedView) {
        m_offlineDetachedView->close();
        delete m_offlineDetachedView;
    }
}

void OfflineViewer::showStatusMsg(const QString& msg, int timeout)
{
    QWidget* p = this->parentWidget();
    while (p) {
        if (auto* mw = qobject_cast<QMainWindow*>(p)) {
            if (mw->statusBar()) {
                mw->statusBar()->showMessage(msg, timeout);
            }
            break;
        }
        p = p->parentWidget();
    }
}

void OfflineViewer::restoreViewsAndControlsFlat()
{
    if (m_offlineViewTabs && m_offlineViewTabs->count() > 0) {
        m_offlineViewTabs->removeTab(0);
        m_offlineViewTabs->removeTab(0);
    }
    if (m_offlineViewTabs) m_offlineViewTabs->hide();

    if (m_offlineActiveControlContainer && m_offlineActiveControlContainer->layout()) {
        auto* activeLayout = qobject_cast<QHBoxLayout*>(m_offlineActiveControlContainer->layout());
        if (activeLayout) {
            activeLayout->removeWidget(m_offlineCamControlWidgets[0]);
            activeLayout->removeWidget(m_offlineCamControlWidgets[1]);
        }
    }
    if (m_offlineActiveControlContainer) m_offlineActiveControlContainer->hide();

    // Show play-all/global controls
    if (m_offlineDiv1) m_offlineDiv1->show();
    if (m_offlinePlayAllBtn) m_offlinePlayAllBtn->show();
    if (m_offlinePauseAllBtn) m_offlinePauseAllBtn->hide();
    if (m_offlineStopAllBtn) m_offlineStopAllBtn->hide();

    // Re-insert widgets into flat container
    if (m_offlineFlatControlsWidget && m_offlineFlatControlsWidget->layout()) {
        auto* flatLayout = qobject_cast<QHBoxLayout*>(m_offlineFlatControlsWidget->layout());
        if (flatLayout) {
            flatLayout->insertWidget(1, m_offlineCamControlWidgets[0]);
            flatLayout->insertWidget(3, m_offlineCamControlWidgets[1]);
        }
        m_offlineCamControlWidgets[0]->show();
        m_offlineCamControlWidgets[1]->show();
        m_offlineFlatControlsWidget->show();
    }

    if (m_offlineViewSplitter) m_offlineViewSplitter->show();
}

void OfflineViewer::applyOfflinePresetSideBySide()
{
    if (m_offlineDetachedView && m_offlineDetachedView->isVisible()) m_offlineDetachedView->close();
    restoreViewsAndControlsFlat();
    m_offlineViewSplitter->setOrientation(Qt::Horizontal);
    m_offlineViewSplitter->addWidget(m_offlineViewWidgets[0]);
    m_offlineViewSplitter->addWidget(m_offlineViewWidgets[1]);
    m_offlineViewSplitter->setSizes({800, 800});
}

void OfflineViewer::applyOfflinePresetTopBottom()
{
    if (m_offlineDetachedView && m_offlineDetachedView->isVisible()) m_offlineDetachedView->close();
    restoreViewsAndControlsFlat();
    m_offlineViewSplitter->setOrientation(Qt::Vertical);
    m_offlineViewSplitter->addWidget(m_offlineViewWidgets[0]);
    m_offlineViewSplitter->addWidget(m_offlineViewWidgets[1]);
    m_offlineViewSplitter->setSizes({500, 500});
}

void OfflineViewer::applyOfflinePresetDetached()
{
    restoreViewsAndControlsFlat();
    m_offlineViewSplitter->setOrientation(Qt::Horizontal);
    m_offlineViewSplitter->addWidget(m_offlineViewWidgets[0]);
    
    if (!m_offlineDetachedView) {
        m_offlineDetachedView = new QDialog(this);
        m_offlineDetachedView->setWindowTitle("Offline Cam 1 - Detached");
        m_offlineDetachedView->resize(800, 600);
        QVBoxLayout* l = new QVBoxLayout(m_offlineDetachedView);
        l->setContentsMargins(0,0,0,0);
        connect(m_offlineDetachedView, &QDialog::finished, this, [this](int){
            if (m_offlineLayoutCombo) m_offlineLayoutCombo->setCurrentIndex(0);
            else applyOfflinePresetSideBySide();
        });
    }
    m_offlineDetachedView->layout()->addWidget(m_offlineViewWidgets[1]);
    m_offlineDetachedView->show();
}

void OfflineViewer::applyOfflinePresetDualControl()
{
    if (m_offlineDetachedView && m_offlineDetachedView->isVisible()) m_offlineDetachedView->close();
    
    // Hide flat view elements and global controls
    m_offlineViewSplitter->hide();
    if (m_offlineDiv1) m_offlineDiv1->hide();
    if (m_offlinePlayAllBtn) m_offlinePlayAllBtn->hide();
    if (m_offlinePauseAllBtn) m_offlinePauseAllBtn->hide();
    if (m_offlineStopAllBtn) m_offlineStopAllBtn->hide();
    m_offlineFlatControlsWidget->hide();

    // Setup view tabs
    m_offlineViewTabs->addTab(m_offlineViewWidgets[0], "CAM 0");
    m_offlineViewTabs->addTab(m_offlineViewWidgets[1], "CAM 1");

    // Add controls to active container
    if (m_offlineActiveControlContainer && m_offlineActiveControlContainer->layout()) {
        auto* activeLayout = qobject_cast<QHBoxLayout*>(m_offlineActiveControlContainer->layout());
        if (activeLayout) {
            activeLayout->addWidget(m_offlineCamControlWidgets[0]);
            activeLayout->addWidget(m_offlineCamControlWidgets[1]);
        }
    }

    m_offlineViewTabs->setCurrentIndex(0);
    m_offlineCamControlWidgets[0]->show();
    m_offlineCamControlWidgets[1]->hide();

    m_offlineViewTabs->show();
    m_offlineActiveControlContainer->show();
}

void OfflineViewer::applyMergedSingleViewLayout(bool merged)
{
    m_isMergedPlayback = merged;
    if (merged) {
        if (m_offlineDetachedView && m_offlineDetachedView->isVisible()) {
            m_offlineDetachedView->close();
        }
        restoreViewsAndControlsFlat();
        m_offlineViewSplitter->setOrientation(Qt::Horizontal);
        if (m_offlineViewWidgets[0]) m_offlineViewWidgets[0]->show();
        if (m_offlineViewWidgets[1]) m_offlineViewWidgets[1]->hide();
        if (m_offlineViewTitleLabels[0]) m_offlineViewTitleLabels[0]->setText("MERGED PANORAMIC VIEW (CAM 0 + CAM 1)");
        if (m_offlineCamControlWidgets[1]) m_offlineCamControlWidgets[1]->hide();
        if (m_offlinePlayAllBtn) m_offlinePlayAllBtn->hide();
        if (m_offlinePauseAllBtn) m_offlinePauseAllBtn->hide();
        if (m_offlineStopAllBtn) m_offlineStopAllBtn->hide();
        if (m_offlineDiv1) m_offlineDiv1->hide();
        if (m_offlineLayoutCombo) m_offlineLayoutCombo->setEnabled(false);

        if (m_offlinePlayBtn[0]) m_offlinePlayBtn[0]->setText(m_playing[0] && m_playbackTimers[0] && m_playbackTimers[0]->isActive() ? "⏸ Pause" : "▶ Play");
        if (m_offlinePauseBtn[0]) m_offlinePauseBtn[0]->hide();
        if (m_offlineStopBtn[0]) m_offlineStopBtn[0]->hide();
    } else {
        restoreViewsAndControlsFlat();
        if (m_offlineViewWidgets[0]) m_offlineViewWidgets[0]->show();
        if (m_offlineViewWidgets[1]) m_offlineViewWidgets[1]->show();
        if (m_offlineViewTitleLabels[0]) m_offlineViewTitleLabels[0]->setText("CAM 0");
        if (m_offlineViewTitleLabels[1]) m_offlineViewTitleLabels[1]->setText("CAM 1");
        if (m_offlineCamControlWidgets[0]) m_offlineCamControlWidgets[0]->show();
        if (m_offlineCamControlWidgets[1]) m_offlineCamControlWidgets[1]->show();
        if (m_offlinePlayAllBtn) m_offlinePlayAllBtn->show();
        if (m_offlinePauseAllBtn) m_offlinePauseAllBtn->hide();
        if (m_offlineStopAllBtn) m_offlineStopAllBtn->hide();
        if (m_offlineDiv1) m_offlineDiv1->show();
        if (m_offlineLayoutCombo) m_offlineLayoutCombo->setEnabled(true);

        if (m_offlinePlayBtn[0]) m_offlinePlayBtn[0]->setText(m_playing[0] && m_playbackTimers[0] && m_playbackTimers[0]->isActive() ? "⏸ C0" : "▶ C0");
        if (m_offlinePlayBtn[1]) m_offlinePlayBtn[1]->setText(m_playing[1] && m_playbackTimers[1] && m_playbackTimers[1]->isActive() ? "⏸ C1" : "▶ C1");
        if (m_offlinePauseBtn[0]) m_offlinePauseBtn[0]->hide();
        if (m_offlinePauseBtn[1]) m_offlinePauseBtn[1]->hide();
        if (m_offlineStopBtn[0]) m_offlineStopBtn[0]->hide();
        if (m_offlineStopBtn[1]) m_offlineStopBtn[1]->hide();
    }
}

void OfflineViewer::onOfflineBrowse()
{
    QString p = QFileDialog::getExistingDirectory(this, "Select Capture Folder");
    if (p.isEmpty()) return;

    if (m_offlinePathEdit) m_offlinePathEdit->setText(p);

    m_playbackMetadata[0] = PlaybackMetadata();
    m_playbackMetadata[1] = PlaybackMetadata();
    m_isMergedPlayback = false;
    m_isMergedDualCam = false;

    QString dirPath = p;
    QString folderName = QDir(dirPath).dirName();
    bool isMergedFolder = folderName.startsWith("Merged_") || QDir(dirPath).exists("Merged_Capture.raw");
    QFile jsonFile(QDir(dirPath).filePath("metadata.json"));
    if (jsonFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
        if (doc.isObject() && doc.object().value("isMerged").toBool(false)) {
            isMergedFolder = true;
        }
        jsonFile.close();
    }

    if (isMergedFolder) {
        m_playbackMetadata[0] = PlaybackMetadata();
        m_playbackMetadata[1] = PlaybackMetadata();
        m_playbackMetadata[0].rawPath = p;
        m_playbackMetadata[1].rawPath = "";
        // Peek at metadata to decide single vs dual camera merged
        QFile peekJson(QDir(p).filePath("metadata.json"));
        int mergedWidth = 0;
        if (peekJson.open(QIODevice::ReadOnly)) {
            QJsonDocument d = QJsonDocument::fromJson(peekJson.readAll());
            if (d.isObject()) mergedWidth = d.object().value("Width").toVariant().toInt();
            peekJson.close();
        }
        // Dual-camera merged: width equals 2× single-cam width
        m_isMergedDualCam = (mergedWidth > 0 && mergedWidth >= 9344 * 2);
        if (m_isMergedDualCam) {
            m_singleCamWidth = mergedWidth / 2;
            if (m_analysisPanel) m_analysisPanel->setDualCameraMode(true);
            showStatusMsg("Loaded Dual-Camera Merged Session (Panoramic view, 4-graph analysis).", 5000);
        } else {
            if (m_analysisPanel) m_analysisPanel->setDualCameraMode(false);
            showStatusMsg("Loaded Merged Capture Session (Single View Mode).", 5000);
        }
        // Always show single panoramic view for merged sessions
        applyMergedSingleViewLayout(true);

    } else if (folderName.startsWith("Cam")) {
        m_isMergedDualCam = false;
        applyMergedSingleViewLayout(false);
        QString leftPath;
        QString rightPath;
        QString statusText;
        if (resolveCameraPair(p, leftPath, rightPath, statusText)) {
            m_playbackMetadata[0].rawPath = leftPath;
            m_playbackMetadata[1].rawPath = rightPath;
            if (m_analysisPanel) m_analysisPanel->setDualCameraMode(true);
            showStatusMsg(statusText, 5000);
        } else {
            m_playbackMetadata[0].rawPath = p;
            m_playbackMetadata[1].rawPath.clear();
            if (m_analysisPanel) m_analysisPanel->setDualCameraMode(false);
            showStatusMsg(QString("Loaded single camera folder: %1").arg(folderName), 5000);
        }
    } else {
        m_isMergedDualCam = false;
        if (m_analysisPanel) m_analysisPanel->setDualCameraMode(false);
        applyMergedSingleViewLayout(false);
        m_playbackMetadata[0].rawPath = p;
        m_playbackMetadata[1].rawPath = p;
    }

    if (m_offlineLogTerminal) {
        m_offlineLogTerminal->clear();
        m_lastOfflineLogSecond = -1;
        for (int idx = 0; idx < 2; ++idx) {
            m_offlineLogs[idx].clear();
            m_offlineLogPrintIndex[idx] = 0;
            m_offlineLogStartTime[idx] = 0;
            m_offlineLogSaveStartSec[idx] = 0;
            QString logPath = m_playbackMetadata[idx].rawPath;
            QFileInfo info(logPath);
            QString finalLogPath = info.isDir() ? logPath + "/session.log" : info.absolutePath() + "/session.log";
            
            int saveStartSec = -1;
            QFile logFile(finalLogPath);
            if (logFile.exists() && logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&logFile);
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    if (line.startsWith("[")) {
                        QString timeStr = line.mid(1, 8);
                        QTime t = QTime::fromString(timeStr, "HH:mm:ss");
                        if (t.isValid()) {
                            int sec = t.msecsSinceStartOfDay() / 1000;
                            if (m_offlineLogs[idx].isEmpty()) m_offlineLogStartTime[idx] = sec;
                            m_offlineLogs[idx].append({sec, line});
                            
                            if (line.contains(QString("[Cam%1] Save started").arg(idx)) ||
                                line.contains(QString("[Cam%1] Saving →").arg(idx)) ||
                                line.contains("[MergeSave] Started")) {
                                saveStartSec = sec;
                            }
                        }
                    }
                }
                logFile.close();
            }
            m_offlineLogSaveStartSec[idx] = (saveStartSec != -1) ? saveStartSec : m_offlineLogStartTime[idx];
        }

        auto latestTelemetryValue = [](const QList<QPair<int, QString>>& entries,
                                       const QString& cameraTag, const QString& marker) {
            QString value = "—";
            for (const auto& entry : entries) {
                if (!entry.second.contains(cameraTag) || !entry.second.contains(marker)) continue;
                value = entry.second.section(marker, -1).trimmed();
            }
            return value;
        };
        if (isMergedFolder) {
            const QString sensor0 = latestTelemetryValue(m_offlineLogs[0], "[Cam0]", "Sensor Temp:");
            const QString sensor1 = latestTelemetryValue(m_offlineLogs[0], "[Cam1]", "Sensor Temp:");
            const QString grabber0 = latestTelemetryValue(m_offlineLogs[0], "[Cam0]", "Grabber Temp:");
            const QString grabber1 = latestTelemetryValue(m_offlineLogs[0], "[Cam1]", "Grabber Temp:");
            const QString fps0 = latestTelemetryValue(m_offlineLogs[0], "[Cam0]", "Grabber instant FPS:");
            const QString fps1 = latestTelemetryValue(m_offlineLogs[0], "[Cam1]", "Grabber instant FPS:");
            if (m_offlineViewSensorTempLabels[0])
                m_offlineViewSensorTempLabels[0]->setText(QString("C0: S: %1 °C | C1: S: %2 °C").arg(sensor0, sensor1));
            if (m_offlineViewGrabberTempLabels[0])
                m_offlineViewGrabberTempLabels[0]->setText(QString("C0: G: %1 °C | C1: G: %2 °C").arg(grabber0, grabber1));
            if (m_offlineViewFpsLabels[0] && fps0 != "—" && fps1 != "—")
                m_offlineViewFpsLabels[0]->setText(QString("C0: %1 fps | C1: %2 fps").arg(fps0, fps1));
        } else {
            for (int idx = 0; idx < 2; ++idx) {
                const QString cameraTag = QString("[Cam%1]").arg(idx);
                const QString sensor = latestTelemetryValue(m_offlineLogs[idx], cameraTag, "Sensor Temp:");
                const QString grabber = latestTelemetryValue(m_offlineLogs[idx], cameraTag, "Grabber Temp:");
                const QString fps = latestTelemetryValue(m_offlineLogs[idx], cameraTag, "Grabber instant FPS:");
                if (m_offlineViewSensorTempLabels[idx])
                    m_offlineViewSensorTempLabels[idx]->setText("S: " + sensor + (sensor == "—" ? "" : " °C"));
                if (m_offlineViewGrabberTempLabels[idx])
                    m_offlineViewGrabberTempLabels[idx]->setText("G: " + grabber + (grabber == "—" ? "" : " °C"));
                if (m_offlineViewFpsLabels[idx] && fps != "—")
                    m_offlineViewFpsLabels[idx]->setText(fps + " fps");
            }
        }
        m_offlineLogTerminal->setPlainText("Logs parsed and ready for playback...");
    }
}

void OfflineViewer::onOfflinePlayClicked(int idx)
{
    if (idx < 0 || idx > 1) return;

    if (m_playing[idx]) {
        if (m_playbackTimers[idx] && m_playbackTimers[idx]->isActive()) {
            onOfflinePauseClicked(idx);
            return;
        } else if (m_playbackTimers[idx] && !m_playbackTimers[idx]->isActive()) {
            m_playbackTimers[idx]->start();
            if (m_offlineViewStatusLabels[idx]) {
                QString width = QString::number(m_playbackMetadata[idx].width);
                QString height = QString::number(m_playbackMetadata[idx].height);
                QString fps = QString::number(m_playbackMetadata[idx].fps);
                m_offlineViewStatusLabels[idx]->setText(QString("[ PLAYING ]  W=%1 H=%2 FPS=%3").arg(width).arg(height).arg(fps));
            }
            if (m_offlinePlayBtn[idx]) {
                m_offlinePlayBtn[idx]->setText((m_isMergedPlayback || m_playbackMetadata[idx].isMerged) ? "⏸ Pause" : QString("⏸ C%1").arg(idx));
            }
            if (m_offlinePlayAllBtn && (idx == 0)) {
                m_offlinePlayAllBtn->setText("⏸ All");
            }
            showStatusMsg(QString("[Cam%1] Playback resumed.").arg(idx), 3000);
            return;
        }
    }
    
    if (m_playbackMetadata[idx].rawPath.isEmpty()) {
        QMessageBox::warning(this, "Warning", QString("No playback folder is loaded for Cam%1. Select a matching camera pair or play the loaded camera only.").arg(idx));
        return;
    }

    onPlaybackClicked(idx);
}

void OfflineViewer::onOfflinePauseClicked(int idx)
{
    if (idx >= 0 && idx <= 1 && m_playing[idx] && m_playbackTimers[idx] && m_playbackTimers[idx]->isActive()) {
        m_playbackTimers[idx]->stop();
        if (m_offlineViewStatusLabels[idx]) {
            QString width = QString::number(m_playbackMetadata[idx].width);
            QString height = QString::number(m_playbackMetadata[idx].height);
            QString fps = QString::number(m_playbackMetadata[idx].fps);
            m_offlineViewStatusLabels[idx]->setText(QString("[ PAUSED ]  W=%1 H=%2 FPS=%3").arg(width).arg(height).arg(fps));
        }
        if (m_offlinePlayBtn[idx]) {
            m_offlinePlayBtn[idx]->setText((m_isMergedPlayback || m_playbackMetadata[idx].isMerged) ? "▶ Play" : QString("▶ C%1").arg(idx));
        }
        if (m_offlinePlayAllBtn && (idx == 0)) {
            m_offlinePlayAllBtn->setText("▶ All");
        }
        showStatusMsg(QString("[Cam%1] Playback paused.").arg(idx), 3000);
    }
}

void OfflineViewer::onOfflineStopClicked(int idx)
{
    if (idx >= 0 && idx <= 1 && m_playing[idx]) {
        stopPlayback(idx, false);
    }
}

void OfflineViewer::onOfflinePlayAllClicked()
{
    bool anyPlaying = (m_playing[0] && m_playbackTimers[0] && m_playbackTimers[0]->isActive()) ||
                      (m_playing[1] && m_playbackTimers[1] && m_playbackTimers[1]->isActive());
    if (anyPlaying) {
        onOfflinePauseAllClicked();
        if (m_offlinePlayAllBtn) m_offlinePlayAllBtn->setText("▶ All");
    } else {
        if (m_isMergedPlayback || m_playbackMetadata[0].isMerged) {
            onOfflinePlayClicked(0);
        } else {
            if (!m_playbackMetadata[0].rawPath.isEmpty()) onOfflinePlayClicked(0);
            if (!m_playbackMetadata[1].rawPath.isEmpty()) onOfflinePlayClicked(1);
        }
        if (m_offlinePlayAllBtn) m_offlinePlayAllBtn->setText("⏸ All");
    }
}

void OfflineViewer::onOfflinePauseAllClicked()
{
    if (m_isMergedPlayback || m_playbackMetadata[0].isMerged) {
        onOfflinePauseClicked(0);
    } else {
        if (!m_playbackMetadata[0].rawPath.isEmpty()) onOfflinePauseClicked(0);
        if (!m_playbackMetadata[1].rawPath.isEmpty()) onOfflinePauseClicked(1);
    }
}

void OfflineViewer::onOfflineStopAllClicked()
{
    if (m_isMergedPlayback || m_playbackMetadata[0].isMerged) {
        onOfflineStopClicked(0);
    } else {
        if (!m_playbackMetadata[0].rawPath.isEmpty()) onOfflineStopClicked(0);
        if (!m_playbackMetadata[1].rawPath.isEmpty()) onOfflineStopClicked(1);
    }
}

void OfflineViewer::stopPlayback(int idx, bool completed)
{
    if (idx < 0 || idx > 1) return;
    m_playing[idx] = false;
    if (m_playbackTimers[idx])
        m_playbackTimers[idx]->stop();
    if (m_playbackFile[idx])
    {
        if (m_playbackFile[idx]->isOpen())
            m_playbackFile[idx]->close();
        delete m_playbackFile[idx];
        m_playbackFile[idx] = nullptr;
    }
    m_playbackFiles[idx].clear();
    m_playbackIndex[idx] = 0;
    m_playbackFrameSize[idx] = 0;
    m_playbackRawSequence[idx] = false;
    if (m_offlineViewStatusLabels[idx]) {
        m_offlineViewStatusLabels[idx]->setText("Playback stopped");
    }
    if (m_offlinePlayBtn[idx]) {
        m_offlinePlayBtn[idx]->setText((m_isMergedPlayback || m_playbackMetadata[idx].isMerged) ? "▶ Play" : QString("▶ C%1").arg(idx));
    }
    if (m_offlinePlayAllBtn && (idx == 0)) {
        m_offlinePlayAllBtn->setText("▶ All");
    }
    if (completed)
        showStatusMsg(QString("[Cam%1] Playback complete.").arg(idx), 5000);
    else
        showStatusMsg(QString("[Cam%1] Playback stopped.").arg(idx), 3000);
}

bool OfflineViewer::loadPlaybackMetadata(const QString& path, int idx)
{
    if (idx < 0 || idx > 1) return false;
    m_playbackMetadata[idx] = PlaybackMetadata();
    m_playbackMetadata[idx].rawPath = path;
    QFileInfo info(path);
    QString jsonPath;
    if (info.isDir())
        jsonPath = QDir(info.absoluteFilePath()).filePath("metadata.json");
    else
        jsonPath = QDir(info.absolutePath()).filePath("metadata.json");

    QFile metadataFile(jsonPath);
    if (!metadataFile.exists() || !metadataFile.open(QIODevice::ReadOnly)) {
        // Fallback for directories without metadata.json (e.g. legacy Merged or Single raw captures)
        if (info.isDir()) {
            QDir dir(path);
            bool isMerged = dir.dirName().startsWith("Merged_") || dir.exists("Merged_Capture.raw");
            if (isMerged || dir.exists("Capture.raw")) {
                m_playbackMetadata[idx].width = isMerged ? (9344 * 2) : 9344;
                m_playbackMetadata[idx].height = 3840;
                m_playbackMetadata[idx].fps = 10.0;
                m_playbackMetadata[idx].saveFormat = ".raw";
                m_playbackMetadata[idx].saveMode = "Single File";
                m_playbackMetadata[idx].isMerged = isMerged;
                m_playbackMetadata[idx].pixelEncoding = isMerged ? "Gray8" : "CameraRaw";
                return true;
            }
        }
        return false;
    }

    const QByteArray raw = metadataFile.readAll();
    metadataFile.close();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject obj = doc.object();
    QString width = obj.value("Width").toVariant().toString();
    if (width.isEmpty()) width = obj.value("width").toVariant().toString();

    QString height = obj.value("BandHeight").toVariant().toString();
    if (height.isEmpty()) height = obj.value("height").toVariant().toString();

    QString fps = obj.value("FPS").toVariant().toString();
    if (fps.isEmpty()) fps = obj.value("fps").toVariant().toString();

    bool isMerged = obj.value("isMerged").toBool(false) || (info.isDir() && (QDir(path).dirName().startsWith("Merged_") || QDir(path).exists("Merged_Capture.raw")));
    m_playbackMetadata[idx].isMerged = isMerged;
    m_playbackMetadata[idx].pixelEncoding = obj.value("PixelEncoding").toString();
    if (m_playbackMetadata[idx].pixelEncoding.isEmpty() && isMerged)
        m_playbackMetadata[idx].pixelEncoding = "Gray8";

    QString saveFormat = obj.value("saveFormat").toString();
    if (saveFormat.isEmpty()) {
        if (info.isDir()) {
            QDir dir(path);
            if (dir.exists("Capture.raw") || dir.exists("Merged_Capture.raw")) saveFormat = ".raw";
            else {
                QStringList bmpFiles = dir.entryList({"*.bmp"}, QDir::Files);
                if (!bmpFiles.isEmpty()) saveFormat = ".bmp";
                else saveFormat = ".raw";
            }
        } else {
            saveFormat = "." + info.suffix().toLower();
        }
    }

    QString saveMode = obj.value("saveMode").toString();
    if (saveMode.isEmpty()) {
        if (info.isDir()) {
            QDir dir(path);
            if (dir.exists("Capture.raw") || dir.exists("Merged_Capture.raw")) saveMode = "Single File";
            else saveMode = "Image Sequence";
        } else {
            saveMode = "Single File";
        }
    }

    if (!width.isEmpty() && !height.isEmpty())
    {
        m_playbackMetadata[idx].width = width.toInt();
        m_playbackMetadata[idx].height = height.toInt();
        double playbackFps = fps.isEmpty() ? 10.0 : fps.toDouble();
        if (!obj.value("FPSMeasured").toBool(false)) {
            const QString captureDir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
            const double recordedFps = estimateRecordedFps(captureDir, isMerged, idx);
            if (recordedFps > 0.0) playbackFps = recordedFps;
        }
        m_playbackMetadata[idx].fps = playbackFps;
        m_playbackMetadata[idx].saveFormat = saveFormat;
        m_playbackMetadata[idx].saveMode = saveMode;
        return true;
    }
    return false;
}

void OfflineViewer::onPlaybackClicked(int idx)
{
    if (idx < 0 || idx > 1) return;
    if (m_playing[idx])
    {
        stopPlayback(idx, false);
        return;
    }

    const QString path = m_playbackMetadata[idx].rawPath.trimmed();
    if (path.isEmpty())
    {
        QMessageBox::warning(this, "Warning", "Select a saved raw/bmp file or directory first.");
        return;
    }

    QFileInfo info(path);
    if (!info.exists())
    {
        QMessageBox::warning(this, "Warning", QString("Playback source does not exist: %1").arg(path));
        return;
    }

    if (!loadPlaybackMetadata(path, idx)) {
        QMessageBox::warning(this, "Warning", QString("Unable to read playback metadata from %1").arg(path));
        return;
    }

    if (m_playbackMetadata[idx].isMerged && !m_isMergedDualCam) {
        // Legacy single-cam merged (non-dual): ensure single view
        applyMergedSingleViewLayout(true);
    } else if (m_playbackMetadata[idx].isMerged && m_isMergedDualCam) {
        // Dual-cam merged: keep single panoramic view (already set during browse)
        applyMergedSingleViewLayout(true);
    }

    const double fps = std::max(0.001, m_playbackMetadata[idx].fps);
    if (fps <= 0.0)
    {
        QMessageBox::warning(this, "Warning", "Invalid FPS value for playback.");
        return;
    }

    if (!m_playbackTimers[idx])
    {
        m_playbackTimers[idx] = new QTimer(this);
        connect(m_playbackTimers[idx], &QTimer::timeout, this, [this, idx](){ onPlaybackTimer(idx); });
    }
    m_playbackTimers[idx]->setInterval(int(1000.0 / fps));

    int totalFrames = 1;

    if (info.isDir())
    {
        QDir dir(path);
        QString rawFileName;
        if (dir.exists("Capture.raw")) rawFileName = "Capture.raw";
        else if (dir.exists("Merged_Capture.raw")) rawFileName = "Merged_Capture.raw";


        if (!rawFileName.isEmpty()) {
            const int width = m_playbackMetadata[idx].width;
            const int height = m_playbackMetadata[idx].height;
            if (width <= 0 || height <= 0) {
                QMessageBox::warning(this, "Warning", "Width and height must be set for raw playback.");
                return;
            }
            m_playbackFile[idx] = new QFile(dir.absoluteFilePath(rawFileName), this);
            if (!m_playbackFile[idx]->open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, "Warning", QString("Unable to open %1 in %2").arg(rawFileName).arg(path));
                delete m_playbackFile[idx];
                m_playbackFile[idx] = nullptr;
                return;
            }
            const qint64 packed12bitBytes = qint64(width) * qint64(height) * 3 / 2;
            const qint64 unpacked16bitBytes = qint64(width) * qint64(height) * 2;
            const qint64 unpacked8bitBytes = qint64(width) * qint64(height);
            const qint64 fileSize = m_playbackFile[idx]->size();

            const bool isGray8 = m_playbackMetadata[idx].pixelEncoding.compare("Gray8", Qt::CaseInsensitive) == 0;
            if (isGray8) {
                m_playbackFrameSize[idx] = unpacked8bitBytes;
                m_is10BitPacked[idx] = false;
            } else if (packed12bitBytes > 0 && (fileSize % packed12bitBytes == 0)) {
                m_playbackFrameSize[idx] = packed12bitBytes;
                m_is10BitPacked[idx] = true;
            } else if (unpacked16bitBytes > 0 && (fileSize % unpacked16bitBytes == 0)) {
                m_playbackFrameSize[idx] = unpacked16bitBytes;
                m_is10BitPacked[idx] = true;
            } else {
                m_playbackFrameSize[idx] = unpacked8bitBytes;
                m_is10BitPacked[idx] = false;
            }

            m_playbackRawSequence[idx] = true;
            totalFrames = (m_playbackFrameSize[idx] > 0) ? (fileSize / m_playbackFrameSize[idx]) : 0;
        } else {
            QStringList rawFiles = dir.entryList({"*.raw"}, QDir::Files, QDir::Name);
            QStringList bmpFiles = dir.entryList({"*.bmp"}, QDir::Files, QDir::Name);
            QStringList files;
            for (const QString& file : rawFiles) files << dir.absoluteFilePath(file);
            for (const QString& file : bmpFiles) files << dir.absoluteFilePath(file);
            if (files.isEmpty()) {
                QMessageBox::warning(this, "Warning", QString("No .raw or .bmp files found in %1").arg(path));
                return;
            }
            m_playbackFiles[idx] = files;
            totalFrames = files.size();
        }
    }
    else if (info.isFile())
    {
        const QString extension = info.suffix().toLower();
        if (extension == "raw")
        {
            const int width = m_playbackMetadata[idx].width;
            const int height = m_playbackMetadata[idx].height;
            if (width <= 0 || height <= 0)
            {
                QMessageBox::warning(this, "Warning", "Width and height must be set for raw playback.");
                return;
            }
            m_playbackFile[idx] = new QFile(path, this);
            if (!m_playbackFile[idx]->open(QIODevice::ReadOnly))
            {
                QMessageBox::warning(this, "Warning", QString("Unable to open %1").arg(path));
                delete m_playbackFile[idx];
                m_playbackFile[idx] = nullptr;
                return;
            }
            const qint64 packed12bitBytes = qint64(width) * qint64(height) * 3 / 2;
            const qint64 unpacked16bitBytes = qint64(width) * qint64(height) * 2;
            const qint64 unpacked8bitBytes = qint64(width) * qint64(height);
            const qint64 fileSize = m_playbackFile[idx]->size();

            const bool isGray8 = m_playbackMetadata[idx].pixelEncoding.compare("Gray8", Qt::CaseInsensitive) == 0;
            if (isGray8) {
                m_playbackFrameSize[idx] = unpacked8bitBytes;
                m_is10BitPacked[idx] = false;
            } else if (packed12bitBytes > 0 && (fileSize % packed12bitBytes == 0)) {
                m_playbackFrameSize[idx] = packed12bitBytes;
                m_is10BitPacked[idx] = true;
            } else if (unpacked16bitBytes > 0 && (fileSize % unpacked16bitBytes == 0)) {
                m_playbackFrameSize[idx] = unpacked16bitBytes;
                m_is10BitPacked[idx] = true;
            } else {
                m_playbackFrameSize[idx] = unpacked8bitBytes;
                m_is10BitPacked[idx] = false;
            }

            m_playbackRawSequence[idx] = true;
            totalFrames = (m_playbackFrameSize[idx] > 0) ? (fileSize / m_playbackFrameSize[idx]) : 0;
        }
        else if (extension == "bmp")
        {
            m_playbackFiles[idx].append(path);
            totalFrames = 1;
        }
        else
        {
            QMessageBox::warning(this, "Warning", "Unsupported playback file format.");
            return;
        }
    }
    else
    {
        QMessageBox::warning(this, "Warning", "Playback source is invalid.");
        return;
    }

    if (m_offlineSliders[idx]) {
        m_offlineSliders[idx]->setRange(0, std::max(0, totalFrames - 1));
        m_offlineSliders[idx]->setEnabled(totalFrames > 1);
        m_offlineSliders[idx]->setValue(0);
    }

    m_playbackIndex[idx] = 0;
    m_playing[idx] = true;
    m_offlineLogPrintIndex[idx] = 0;
    
    if (m_offlineViewStatusLabels[idx]) {
        QString w = QString::number(m_playbackMetadata[idx].width);
        QString h = QString::number(m_playbackMetadata[idx].height);
        QString f = QString::number(m_playbackMetadata[idx].fps);
        m_offlineViewStatusLabels[idx]->setText(QString("[ PLAYING ]  W=%1 H=%2 FPS=%3").arg(w).arg(h).arg(f));
    }
    if (m_offlinePlayBtn[idx]) {
        m_offlinePlayBtn[idx]->setText((m_isMergedPlayback || m_playbackMetadata[idx].isMerged) ? "⏸ Pause" : QString("⏸ C%1").arg(idx));
    }
    m_playbackTimers[idx]->start();
    showStatusMsg(QString("[Cam%1] Playback started.").arg(idx), 3000);
    onPlaybackTimer(idx);
}

void OfflineViewer::onPlaybackTimer(int idx)
{
    if (idx < 0 || idx > 1 || !m_playing[idx]) return;
    QByteArray raw;
    int width = 0;
    int height = 0;

    if (!m_playbackFiles[idx].isEmpty())
    {
        if (m_playbackIndex[idx] >= m_playbackFiles[idx].size())
        {
            stopPlayback(idx, true);
            return;
        }

        const QString path = m_playbackFiles[idx].at(m_playbackIndex[idx]++);
        const QString extension = QFileInfo(path).suffix().toLower();
        
        if (extension == "raw")
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                stopPlayback(idx, false);
                QMessageBox::warning(this, "Warning", QString("Unable to open %1").arg(path));
                return;
            }
            width = m_playbackMetadata[idx].width;
            height = m_playbackMetadata[idx].height;
            const qint64 packed12bitBytes = qint64(width) * qint64(height) * 3 / 2;
            const qint64 unpacked16bitBytes = qint64(width) * qint64(height) * 2;
            const qint64 unpacked8bitBytes = qint64(width) * qint64(height);
            qint64 bytesToRead = (m_playbackFrameSize[idx] > 0) ? m_playbackFrameSize[idx] :
                                 (file.size() == packed12bitBytes ? packed12bitBytes :
                                 (file.size() == unpacked16bitBytes ? unpacked16bitBytes : unpacked8bitBytes));
            if (bytesToRead <= 0)
            {
                file.close(); stopPlayback(idx, false);
                QMessageBox::warning(this, "Warning", "Invalid frame dimensions for raw playback.");
                return;
            }
            raw = file.read(bytesToRead);
            file.close();
            if (raw.size() != bytesToRead)
            {
                stopPlayback(idx, true);
                return;
            }
        }
        else
        {
            QImage img(path);
            if (img.isNull())
            {
                stopPlayback(idx, false);
                QMessageBox::warning(this, "Warning", QString("Unable to load image %1").arg(path));
                return;
            }
            if (img.format() != QImage::Format_Grayscale8)
                img = img.convertToFormat(QImage::Format_Grayscale8);
            width = img.width();
            height = img.height();
            raw = QByteArray(reinterpret_cast<const char*>(img.constBits()), img.sizeInBytes());
        }
    }
    else if (m_playbackFile[idx])
    {
        if (!m_playbackFile[idx]->isOpen())
        {
            stopPlayback(idx, false);
            return;
        }

        qint64 targetPos = qint64(m_playbackIndex[idx]) * m_playbackFrameSize[idx];
        if (m_playbackFile[idx]->pos() != targetPos) {
            m_playbackFile[idx]->seek(targetPos);
        }

        raw = m_playbackFile[idx]->read(m_playbackFrameSize[idx]);
        if (raw.size() != m_playbackFrameSize[idx])
        {
            stopPlayback(idx, true);
            return;
        }
        width = m_playbackMetadata[idx].width;
        height = m_playbackMetadata[idx].height;
        m_playbackIndex[idx]++;
    }
    else
    {
        stopPlayback(idx, true);
        return;
    }

    const qint64 packed12bitBytes = qint64(width) * qint64(height) * 3 / 2;
    const qint64 unpacked16bitBytes = qint64(width) * qint64(height) * 2;

    if (m_is10BitPacked[idx] || (width > 0 && height > 0 && (raw.size() == packed12bitBytes || raw.size() == unpacked16bitBytes)))
    {
        std::vector<uint8_t> unpacked = CameraWorker::unpackRawTo8bit(
            reinterpret_cast<const uint8_t*>(raw.constData()),
            width, height, raw.size()
        );
        raw = QByteArray(reinterpret_cast<const char*>(unpacked.data()), unpacked.size());
    }

    if (m_offlineSliders[idx]) {
        QSignalBlocker blocker(m_offlineSliders[idx]);
        m_offlineSliders[idx]->setValue(m_playbackIndex[idx] - 1);
    }
    if (m_offlineTimeLabels[idx]) {
        int currentSecs = (m_playbackIndex[idx] - 1) / std::max(0.001, m_playbackMetadata[idx].fps);
        int totalSecs = m_offlineSliders[idx] ? m_offlineSliders[idx]->maximum() : 0;
        double fps = std::max(0.001, m_playbackMetadata[idx].fps);
        int totalDurationSecs = totalSecs / fps;
        QString cur = QString("%1:%2").arg(currentSecs / 60, 2, 10, QChar('0')).arg(currentSecs % 60, 2, 10, QChar('0'));
        QString tot = QString("%1:%2").arg(totalDurationSecs / 60, 2, 10, QChar('0')).arg(totalDurationSecs % 60, 2, 10, QChar('0'));
        m_offlineTimeLabels[idx]->setText(cur + " / " + tot);
    }

    // Parse historical telemetry
    {
        double playFps = std::max(0.001, m_playbackMetadata[idx].fps);
        int T_current = m_offlineLogSaveStartSec[idx] + (m_playbackIndex[idx] / playFps);

        if (m_isMergedPlayback || m_playbackMetadata[idx].isMerged)
        {
            QString histFps0 = "—", histFps1 = "—";
            QString histSensorTemp0 = "—", histSensorTemp1 = "—";
            QString histGrabberTemp0 = "—", histGrabberTemp1 = "—";

            for (const auto& pair : m_offlineLogs[0]) {
                if (pair.first <= T_current) {
                    QString line = pair.second;
                    if (line.contains("[Cam0] Grabber instant FPS: ")) {
                        histFps0 = line.split("Grabber instant FPS: ").last().trimmed();
                    } else if (line.contains("Grabber instant FPS: ") && histFps0 == "—") {
                        histFps0 = line.split("Grabber instant FPS: ").last().trimmed();
                    }
                    if (line.contains("[Cam0] Sensor Temp: ")) {
                        histSensorTemp0 = line.split("Sensor Temp: ").last().trimmed();
                    } else if (line.contains("Sensor Temp: ") && histSensorTemp0 == "—") {
                        histSensorTemp0 = line.split("Sensor Temp: ").last().trimmed();
                    }
                    if (line.contains("[Cam0] Grabber Temp: ")) {
                        histGrabberTemp0 = line.split("Grabber Temp: ").last().trimmed();
                    } else if (line.contains("Grabber Temp: ") && histGrabberTemp0 == "—") {
                        histGrabberTemp0 = line.split("Grabber Temp: ").last().trimmed();
                    }

                    if (line.contains("[Cam1] Grabber instant FPS: ")) {
                        histFps1 = line.split("Grabber instant FPS: ").last().trimmed();
                    }
                    if (line.contains("[Cam1] Sensor Temp: ")) {
                        histSensorTemp1 = line.split("Sensor Temp: ").last().trimmed();
                    }
                    if (line.contains("[Cam1] Grabber Temp: ")) {
                        histGrabberTemp1 = line.split("Grabber Temp: ").last().trimmed();
                    }
                }
            }

            if (m_offlineViewFpsLabels[0]) {
                if (histFps1 != "—")
                    m_offlineViewFpsLabels[0]->setText(QString("C0: %1 fps | C1: %2 fps").arg(histFps0).arg(histFps1));
                else
                    m_offlineViewFpsLabels[0]->setText(histFps0 + " fps");
            }
            if (m_offlineViewSensorTempLabels[0]) {
                if (histSensorTemp1 != "—")
                    m_offlineViewSensorTempLabels[0]->setText(QString("C0: S: %1 °C | C1: S: %2 °C").arg(histSensorTemp0).arg(histSensorTemp1));
                else
                    m_offlineViewSensorTempLabels[0]->setText("S: " + histSensorTemp0 + (histSensorTemp0 == "—" ? "" : " °C"));
            }
            if (m_offlineViewGrabberTempLabels[0]) {
                if (histGrabberTemp1 != "—")
                    m_offlineViewGrabberTempLabels[0]->setText(QString("C0: G: %1 °C | C1: G: %2 °C").arg(histGrabberTemp0).arg(histGrabberTemp1));
                else
                    m_offlineViewGrabberTempLabels[0]->setText("G: " + histGrabberTemp0 + (histGrabberTemp0 == "—" ? "" : " °C"));
            }
        }
        else
        {
            QString histFps = "—";
            QString histSensorTemp = "—";
            QString histGrabberTemp = "—";

            for (const auto& pair : m_offlineLogs[idx]) {
                if (pair.first <= T_current) {
                    QString line = pair.second;
                    if (line.contains("Grabber instant FPS: ")) {
                        histFps = line.split("Grabber instant FPS: ").last().trimmed();
                    }
                    if (line.contains("Sensor Temp: ")) {
                        histSensorTemp = line.split("Sensor Temp: ").last().trimmed();
                    }
                    if (line.contains("Grabber Temp: ")) {
                        histGrabberTemp = line.split("Grabber Temp: ").last().trimmed();
                    }
                }
            }

            if (m_offlineViewFpsLabels[idx]) {
                m_offlineViewFpsLabels[idx]->setText(histFps + " fps");
            }
            if (m_offlineViewSensorTempLabels[idx]) {
                m_offlineViewSensorTempLabels[idx]->setText("S: " + histSensorTemp + (histSensorTemp == "—" ? "" : " °C"));
            }
            if (m_offlineViewGrabberTempLabels[idx]) {
                m_offlineViewGrabberTempLabels[idx]->setText("G: " + histGrabberTemp + (histGrabberTemp == "—" ? "" : " °C"));
            }
        }
    }

    m_lastFramePixels[idx] = raw;
    m_lastFrameWidth[idx]  = width;   // full panoramic width
    m_lastFrameHeight[idx] = height;


    renderOfflineFrame(idx, raw, width, height);
    updateOfflineLogTerminal();
}

void OfflineViewer::renderOfflineFrame(int idx, const QByteArray& pixels, int width, int height)
{
    if (idx < 0 || idx > 1 || !m_offlineViewLabels[idx]) return;
    const qint64 expectedBytes = qint64(width) * qint64(height);
    if (width <= 0 || height <= 0 || pixels.size() < expectedBytes) {
        if (m_offlineViewLabels[idx]) {
            m_offlineViewLabels[idx]->setText("No valid frame loaded");
        }
        return;
    }

    // Always build the full-width image for display (panoramic merged view unchanged)
    QImage img(reinterpret_cast<const uchar*>(pixels.constData()),
               width, height, width, QImage::Format_Grayscale8);
    if (img.isNull()) {
        m_offlineViewLabels[idx]->setText("No valid frame loaded");
        return;
    }
    QImage displayImg = img.convertToFormat(QImage::Format_RGB32);

    // Overlay Crosshair lines or ROI rectangle on the video image
    if (m_analysisPanel && m_analysisPanel->isVisible()) {
        QPainter p(&displayImg);
        p.setRenderHint(QPainter::Antialiasing, true);

        auto mode = m_analysisPanel->getMode();
        bool isDark = m_analysisPanel ? m_analysisPanel->isDarkMode() : true;
        QColor cyanOverlay   = isDark ? QColor(0, 230, 230) : QColor("#06B6D4");
        QColor yellowOverlay = isDark ? QColor(255, 190, 40) : QColor("#F59E0B");

        if (mode == AnalysisGraphCanvas::ModeLineProfile) {
            int overlayCam = idx;
            if (m_isMergedDualCam && width >= m_singleCamWidth * 2) {
                overlayCam = qBound(0, m_analysisPanel->getActiveCameraIndex(), 1);
            }
            QPoint crosshair = m_analysisPanel->getCrosshairForCamera(overlayCam);
            if (m_isMergedDualCam && width >= m_singleCamWidth * 2 && overlayCam == 1) {
                crosshair.rx() += m_singleCamWidth;
            }
            int cx = crosshair.x();
            int cy = crosshair.y();

            // Draw Horizontal Row Y Line (Cyan)
            if (cy >= 0 && cy < height) {
                p.setPen(QPen(cyanOverlay, qMax(2, height / 1000), Qt::SolidLine));
                p.drawLine(0, cy, width, cy);
            }

            // Draw Vertical Col X Line (Yellow)
            if (cx >= 0 && cx < width) {
                p.setPen(QPen(yellowOverlay, qMax(2, width / 4000), Qt::SolidLine));
                p.drawLine(cx, 0, cx, height);
            }

            // Draw intersection marker circle
            if (cx >= 0 && cx < width && cy >= 0 && cy < height) {
                int r = qMax(6, width / 1000);
                p.setPen(QPen(QColor(255, 255, 255), 2));
                p.setBrush(QBrush(cyanOverlay));
                p.drawEllipse(QPoint(cx, cy), r, r);
            }

        } else if (mode == AnalysisGraphCanvas::ModeROIHistogram) {
            QRect roi = m_analysisPanel->getROI();
            if (!roi.isEmpty() && roi.isValid()) {
                p.setPen(QPen(cyanOverlay, qMax(2, width / 4000), Qt::DashLine));
                p.setBrush(QBrush(isDark ? QColor(0, 180, 240, 50) : QColor(6, 182, 212, 40)));
                p.drawRect(roi);

                // Corner markers
                p.setPen(QPen(cyanOverlay, qMax(3, width / 3000)));
                int rL = roi.left(), rR = roi.right(), rT = roi.top(), rB = roi.bottom();
                int cLen = qMin(30, qMin(roi.width(), roi.height()) / 3);
                // Top-Left corner
                p.drawLine(rL, rT, rL + cLen, rT); p.drawLine(rL, rT, rL, rT + cLen);
                // Top-Right corner
                p.drawLine(rR, rT, rR - cLen, rT); p.drawLine(rR, rT, rR, rT + cLen);
                // Bottom-Left corner
                p.drawLine(rL, rB, rL + cLen, rB); p.drawLine(rL, rB, rL, rB - cLen);
                // Bottom-Right corner
                p.drawLine(rR, rB, rR - cLen, rB); p.drawLine(rR, rB, rR, rB - cLen);
            }
        }
    }

    QPixmap px;

    bool fitToScreen = true;
    QWidget* mw = parentWidget();
    while (mw) {
        if (auto* qmw = qobject_cast<QMainWindow*>(mw)) {
            fitToScreen = qmw->property("fitToScreen").toBool();
            break;
        }
        mw = mw->parentWidget();
    }

    if (fitToScreen && m_offlineViewScrolls[idx]) {
        QSize targetSize = m_offlineViewScrolls[idx]->viewport()->size();
        if (targetSize.isValid() && !targetSize.isEmpty()) {
            QImage scaledImg = displayImg.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            px = QPixmap::fromImage(scaledImg);
        } else {
            px = QPixmap::fromImage(displayImg);
        }
    } else {
        px = QPixmap::fromImage(displayImg);
    }

    m_offlineViewLabels[idx]->setPixmap(px);
    m_offlineViewLabels[idx]->adjustSize();

    if (m_analysisPanel && m_analysisPanel->isVisible()) {
        if (m_isMergedDualCam && width >= m_singleCamWidth * 2) {
            // Internally split the merged frame: left half → CAM0 canvas, right half → CAM1 canvas
            // This gives 4 graphs in the analysis panel without splitting the displayed view
            const int halfW = qMin(m_singleCamWidth, width / 2);
            QImage cam0Half = img.copy(0,        0, halfW, height);
            QImage cam1Half = img.copy(halfW,     0, halfW, height);
            m_analysisPanel->processImageFrame(cam0Half);
            m_analysisPanel->processSecondCameraFrame(cam1Half);
        } else {
            if (m_analysisPanel->isDualCameraMode() && idx == 1) {
                m_analysisPanel->processSecondCameraFrame(img);
            } else {
                m_analysisPanel->processImageFrame(img);
            }
        }
    }
}


bool OfflineViewer::eventFilter(QObject* watched, QEvent* event)
{
    int activeIdx = -1;
    if (watched == m_offlineViewLabels[0]) activeIdx = 0;
    else if (watched == m_offlineViewLabels[1]) activeIdx = 1;

    if (activeIdx != -1 && m_analysisPanel && m_analysisPanel->isVisible()) {
        QLabel* lbl = m_offlineViewLabels[activeIdx];
        if (lbl && lbl->pixmap() && !lbl->pixmap()->isNull()) {
            QMouseEvent* mouseEv = dynamic_cast<QMouseEvent*>(event);
            if (mouseEv) {
                QPoint mousePos = mouseEv->pos();
                int origW = m_lastFrameWidth[activeIdx];
                int origH = m_lastFrameHeight[activeIdx];

                if (origW > 0 && origH > 0) {
                    QSize lblSize = lbl->size();
                    QSize pxSize = lbl->pixmap()->size();

                    int offsetX = (lblSize.width() - pxSize.width()) / 2;
                    int offsetY = (lblSize.height() - pxSize.height()) / 2;

                    int clampedMouseX = qBound(offsetX, mousePos.x(), offsetX + pxSize.width() - 1);
                    int clampedMouseY = qBound(offsetY, mousePos.y(), offsetY + pxSize.height() - 1);

                    int imgX = static_cast<int>((static_cast<double>(clampedMouseX - offsetX) / pxSize.width()) * origW);
                    int imgY = static_cast<int>((static_cast<double>(clampedMouseY - offsetY) / pxSize.height()) * origH);

                    imgX = qBound(0, imgX, origW - 1);
                    imgY = qBound(0, imgY, origH - 1);

                    auto mode = m_analysisPanel->getMode();
                    const bool mergedDualDisplay = m_isMergedDualCam && origW >= m_singleCamWidth * 2 && activeIdx == 0;

                    if (event->type() == QEvent::MouseButtonPress && mouseEv->button() == Qt::LeftButton) {
                        m_isDraggingROI = true;
                        m_dragStartPos = QPoint(imgX, imgY);
                        m_activeDragIdx = activeIdx;

                        if (mode == AnalysisGraphCanvas::ModeLineProfile) {
                            int cameraIdx = activeIdx;
                            QPoint localPt(imgX, imgY);
                            if (mergedDualDisplay) {
                                cameraIdx = (imgX >= m_singleCamWidth) ? 1 : 0;
                                localPt.setX(imgX - (cameraIdx == 1 ? m_singleCamWidth : 0));
                            }
                            m_analysisPanel->setCrosshair(localPt, cameraIdx);
                            renderOfflineFrame(activeIdx, m_lastFramePixels[activeIdx], origW, origH);
                        } else if (mode == AnalysisGraphCanvas::ModeROIHistogram) {
                            m_analysisPanel->setROI(QRect(imgX, imgY, 1, 1));
                            renderOfflineFrame(activeIdx, m_lastFramePixels[activeIdx], origW, origH);
                        }
                        return true;
                    }
                    else if (event->type() == QEvent::MouseMove) {
                        if (mode == AnalysisGraphCanvas::ModeLineProfile && (mouseEv->buttons() & Qt::LeftButton)) {
                            int cameraIdx = activeIdx;
                            QPoint localPt(imgX, imgY);
                            if (mergedDualDisplay) {
                                cameraIdx = (imgX >= m_singleCamWidth) ? 1 : 0;
                                localPt.setX(imgX - (cameraIdx == 1 ? m_singleCamWidth : 0));
                            }
                            m_analysisPanel->setCrosshair(localPt, cameraIdx);
                            renderOfflineFrame(activeIdx, m_lastFramePixels[activeIdx], origW, origH);
                            return true;
                        } 
                        else if (mode == AnalysisGraphCanvas::ModeROIHistogram && m_isDraggingROI) {
                            QRect roi = QRect(m_dragStartPos, QPoint(imgX, imgY)).normalized();
                            m_analysisPanel->setROI(roi);
                            renderOfflineFrame(activeIdx, m_lastFramePixels[activeIdx], origW, origH);
                            return true;
                        }
                    } 
                    else if (event->type() == QEvent::MouseButtonRelease && mouseEv->button() == Qt::LeftButton) {
                        m_isDraggingROI = false;
                        return true;
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void OfflineViewer::setDarkMode(bool dark)
{
    if (m_offlineThemeBtn) {
        const QSignalBlocker blocker(m_offlineThemeBtn);
        m_offlineThemeBtn->setChecked(dark);
    }

    // Clear any local stylesheet so the global qApp stylesheet from MainWindow
    // propagates through without being overridden here.
    setStyleSheet(QString());

    if (m_analysisPanel) {
        m_analysisPanel->setDarkMode(dark);
    }

    // Re-render cached frames so pixel brightness maps correctly for the theme.
    for (int i = 0; i < 2; ++i) {
        if (m_lastFrameWidth[i] > 0 && m_lastFrameHeight[i] > 0 && !m_lastFramePixels[i].isEmpty()) {
            renderOfflineFrame(i, m_lastFramePixels[i], m_lastFrameWidth[i], m_lastFrameHeight[i]);
        }
    }
}


void OfflineViewer::onDisplaySettingsChanged()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_lastFramePixels[i].isEmpty()) {
            renderOfflineFrame(i, m_lastFramePixels[i], m_lastFrameWidth[i], m_lastFrameHeight[i]);
        }
    }
}

void OfflineViewer::updateOfflineLogTerminal()
{
    if (!m_offlineLogTerminal) return;

    int T0 = -1;
    if (!m_offlineLogs[0].isEmpty()) {
        double fps = std::max(0.001, m_playbackMetadata[0].fps);
        T0 = m_offlineLogSaveStartSec[0] + (m_playbackIndex[0] / fps);
    }
    int T1 = -1;
    if (!m_offlineLogs[1].isEmpty()) {
        double fps = std::max(0.001, m_playbackMetadata[1].fps);
        T1 = m_offlineLogSaveStartSec[1] + (m_playbackIndex[1] / fps);
    }

    int maxT = std::max(T0, T1);
    if (maxT == -1) return;

    if (maxT == m_lastOfflineLogSecond) return;
    m_lastOfflineLogSecond = maxT;

    m_offlineLogTerminal->clear();

    QMap<int, QStringList> uniqueLogs;
    for (int idx = 0; idx < 2; ++idx) {
        for (const auto& pair : m_offlineLogs[idx]) {
            int sec = pair.first;
            if (sec <= maxT) {
                QString line = pair.second;
                if (line.contains("Sensor Temp:") || line.contains("Grabber Temp:") || line.contains("Grabber instant FPS:")) {
                    continue;
                }
                if (!uniqueLogs[sec].contains(line)) {
                    uniqueLogs[sec].append(line);
                }
            }
        }
    }

    for (auto it = uniqueLogs.begin(); it != uniqueLogs.end(); ++it) {
        for (const QString& line : it.value()) {
            m_offlineLogTerminal->appendPlainText(line);
        }
    }
}
