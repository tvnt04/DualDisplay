#include "MainWindow.h"
#include "NativeFile.h"

#ifdef HAS_OFFLINE_VIEWER
#include "OfflineViewer.h"
#endif

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QScreen>
#include <QImage>
#include <QPixmap>
#include <QMouseEvent>
#include <QTabBar>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <algorithm>
#include <set>

namespace {
class DisplayModeSwitch final : public QAbstractButton
{
public:
    explicit DisplayModeSwitch(QWidget* parent = nullptr)
    : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(40, 24);
        setFocusPolicy(Qt::NoFocus);

        setAttribute(Qt::WA_StyledBackground, false);

        setStyleSheet(
            "QAbstractButton {"
            "background: transparent;"
            "border: none;"
            "outline: none;"
            "}"
        );
    }

    QSize sizeHint() const override { return QSize(40, 24); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF trackRect = rect().adjusted(1.5, 2.5, -1.5, -2.5);
        const bool stretchMode = isChecked();
        const QColor trackColor = stretchMode ? QColor("#27ae60") : QColor("#d5d8dc");
        const QColor borderColor = stretchMode ? QColor("#229954") : QColor("#aeb6bf");

        p.setPen(QPen(borderColor, 1.2));
        p.setBrush(trackColor);
        p.drawRoundedRect(trackRect, trackRect.height() / 2.0, trackRect.height() / 2.0);

        const qreal knobDiameter = trackRect.height() - 4.0;
        const qreal knobX = stretchMode
            ? trackRect.right() - knobDiameter - 2.0
            : trackRect.left() + 2.0;
        const QRectF knobRect(knobX, trackRect.top() + 2.0, knobDiameter, knobDiameter);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(knobRect);
    }
};
}

// ═══════════════════════════════════════════════════════════════════════
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    qRegisterMetaType<FramePacketPtr>("FramePacketPtr");
    qRegisterMetaType<CameraParams>("CameraParams");

    setProperty("fitToScreen", m_fitToScreen);
    setProperty("pixelStretch", m_pixelStretch);

    setWindowTitle("DualDisplay — Dual Camera Live View");
    resize(1920, 1080);
    buildUI();

    m_cameraManager = new CameraManager(this);
    m_captureController = new CaptureController(this);
    m_playbackController = new PlaybackController(this);
    m_calibrationEngine = new CalibrationEngine(this);

    // CameraManager wiring
    connect(m_cameraManager, &CameraManager::frameReady,       this, &MainWindow::onFrameReady,       Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::logMessage,       this, &MainWindow::onLogMessage,       Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::streamStarted,    this, &MainWindow::onStreamStarted,    Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::streamStopped,    this, &MainWindow::onStreamStopped,    Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraOpened,     this, &MainWindow::onCameraOpened,     Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::paramsQueried,    this, &MainWindow::onParamsQueried,    Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::cameraClosed,     this, &MainWindow::onCameraClosed,     Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::errorOccurred,    this, &MainWindow::onCameraError,      Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::temperatureReady, this, &MainWindow::onTemperatureReady, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::fpsClamped,       this, [this](int camIdx, double requestedFps, double maxFps) {
        const QString msg = QString("CAM %1: %2 FPS exceeds camera max for current height (%3 FPS). Clamped to max %3 FPS.")
            .arg(camIdx).arg(requestedFps, 0, 'f', 1).arg(maxFps, 0, 'f', 2);
        if (statusBar()) statusBar()->showMessage(msg, 7000);
        appendLogLine(msg);
        m_cameraUiStates[camIdx].fps = QString::number(maxFps, 'f', 2);
        applyCameraUiStateToWidgets(camIdx);
    }, Qt::QueuedConnection);
    connect(m_cameraManager, &CameraManager::hardwareDetected, this, [this](int count, const QStringList& names) {
        Q_UNUSED(count);
        for (int slot = 0; slot < 2; ++slot) {
            auto populateSourceCombo = [slot, &names](QComboBox* cb) {
                if (!cb) return;
                const int currentIdx = cb->currentIndex();
                cb->blockSignals(true);
                cb->clear();
                for (int i = 0; i < names.size(); ++i) {
                    cb->addItem(names[i]);
                }
                if (names.isEmpty()) {
                    cb->addItem("No cameras found");
                }
                int targetIndex = (currentIdx >= 0 && currentIdx < cb->count()) ? currentIdx : slot;
                if (targetIndex >= cb->count()) targetIndex = 0;
                cb->setCurrentIndex(targetIndex);
                cb->blockSignals(false);
            };
            populateSourceCombo(m_sidebarSourceCombos[slot]);
            populateSourceCombo(m_topSourceCombos[slot]);
            applyCameraSourceSelection(slot);
        }
    });
    connect(m_cameraManager, &CameraManager::statusMessage, this, [this](const QString& msg, int timeoutMs) {
        if (statusBar()) statusBar()->showMessage(msg, timeoutMs);
    });

    m_cameraManager->initialize();

    // CaptureController wiring
    connect(m_captureController, &CaptureController::saveStateChanged, this, [this](int idx, bool saving) {
        m_saving[idx] = saving;
        m_cameraManager->setSaving(idx, saving);
        syncSidebarButtons(idx);
    });
    connect(m_captureController, &CaptureController::mergeSaveStateChanged, this, [this](bool saving) {
        if (m_saveModeToggleBtn) {
            m_saveModeToggleBtn->setChecked(saving);
        }
        for (int i = 0; i < 2; ++i) {
            m_cameraManager->setSaving(i, saving);
            syncSidebarButtons(i);
        }
    });
    connect(m_captureController, &CaptureController::countdownUpdated, this, [this](int idx, const QString& countdownText) {
        if (m_sidebarSaveBtns[idx]) m_sidebarSaveBtns[idx]->setText(countdownText);
        if (m_topSaveBtns[idx]) m_topSaveBtns[idx]->setText(countdownText);
    });
    connect(m_captureController, &CaptureController::statusMessage, this, [this](const QString& msg, int timeoutMs) {
        if (statusBar()) statusBar()->showMessage(msg, timeoutMs);
    });
    connect(m_captureController, &CaptureController::logMessage, this, &MainWindow::appendLogLine);
    connect(m_captureController, &CaptureController::mergeStatsUpdated, this, &MainWindow::onMergeSaverStats);

    // PlaybackController wiring
    connect(m_playbackController, &PlaybackController::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_playbackController, &PlaybackController::playbackStateChanged, this, [this](int idx, bool playing) {
        m_playing[idx] = playing;
        syncSidebarButtons(idx);
        if (m_sidebarStateLabels[idx]) {
            m_sidebarStateLabels[idx]->setText(playing ? "Playing" : (m_streaming[idx] ? "Streaming" : (m_connected[idx] ? "Connected" : "Disconnected")));
        }
        if (m_topStateLabels[idx]) {
            m_topStateLabels[idx]->setText(playing ? "Playing" : (m_streaming[idx] ? "Streaming" : (m_connected[idx] ? "Connected" : "Disconnected")));
        }
    });
    connect(m_playbackController, &PlaybackController::statusMessage, this, [this](const QString& msg, int timeoutMs) {
        if (statusBar()) statusBar()->showMessage(msg, timeoutMs);
    });
    connect(m_playbackController, &PlaybackController::errorMessage, this, [this](const QString& title, const QString& msg) {
        QMessageBox::critical(this, title, msg);
    });

    // CalibrationEngine wiring
    connect(m_calibrationEngine, &CalibrationEngine::lineDelayCalculated, this, [this](int diffLines) {
        setLineDelayValue(diffLines);
        if (m_startCalibrationBtn) {
            m_startCalibrationBtn->setText(" Start Calibration ");
        }
        if (statusBar()) {
            statusBar()->showMessage(QString("Calibration complete! Line delay: %1 lines").arg(diffLines), 5000);
        }
    });
    connect(m_calibrationEngine, &CalibrationEngine::statusMessageChanged, this, [this](const QString& msg) {
        if (m_startCalibrationBtn) {
            m_startCalibrationBtn->setText(msg);
        }
        if (statusBar()) statusBar()->showMessage(msg);
    });

    loadSettings();
    if (m_darkModeBtn) {
        m_darkModeBtn->setChecked(m_darkMode);
    }
    applyStyles();

    m_lastAppliedRegionOffset[0] = getActiveRegionOffset(0);
    m_lastAppliedRegionOffset[1] = getActiveRegionOffset(1);

    // Auto-detect cameras on startup after main window is fully rendered on screen
    QTimer::singleShot(100, this, &MainWindow::detectCameras);
}

MainWindow::~MainWindow()
{
    if (m_playbackController) {
        m_playbackController->disconnect(this);
        delete m_playbackController;
        m_playbackController = nullptr;
    }
    if (m_captureController) {
        m_captureController->disconnect(this);
        delete m_captureController;
        m_captureController = nullptr;
    }
    if (m_cameraManager) {
        m_cameraManager->disconnect(this);
        m_cameraManager->shutdown();
        delete m_cameraManager;
        m_cameraManager = nullptr;
    }
    if (m_calibrationEngine) {
        m_calibrationEngine->disconnect(this);
        delete m_calibrationEngine;
        m_calibrationEngine = nullptr;
    }
}

// ── Build UI ─────────────────────────────────────────────────────────
void MainWindow::buildUI()
{
    QWidget* center = new QWidget();
    center->setObjectName("centerPlaceholder");
    QVBoxLayout* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    m_rootSplitter = new QSplitter(Qt::Vertical, center);
    m_rootSplitter->setChildrenCollapsible(false);

    m_mainSplitter = new QSplitter(Qt::Horizontal, m_rootSplitter);
    m_mainSplitter->setChildrenCollapsible(false);
    m_logTerminal = new QPlainTextEdit(m_rootSplitter);
    m_logTerminal->setObjectName("logTerminal");
    m_logTerminal->setReadOnly(true);
    m_logTerminal->setMaximumBlockCount(2000);
    m_logTerminal->setPlaceholderText("Runtime logs...");
    m_logTerminal->setMinimumHeight(72);
    m_logTerminal->setMaximumHeight(96);

    m_rootSplitter->addWidget(m_mainSplitter);
    m_rootSplitter->addWidget(m_logTerminal);
    m_rootSplitter->setStretchFactor(0, 1);
    m_rootSplitter->setStretchFactor(1, 0);
    m_rootSplitter->setSizes({984, 96});

    m_mainTabs = new QTabWidget(center);
    m_mainTabs->tabBar()->setElideMode(Qt::ElideNone);
    m_mainTabs->tabBar()->setExpanding(false);
    QWidget* liveTab = new QWidget();
    QVBoxLayout* liveTabLayout = new QVBoxLayout(liveTab);
    liveTabLayout->setContentsMargins(0, 0, 0, 0);
    liveTabLayout->setSpacing(0);
    liveTabLayout->addWidget(m_rootSplitter);

    m_mainTabs->addTab(liveTab, "Live Capture");

#ifdef HAS_OFFLINE_VIEWER
    m_offlineViewer = new OfflineViewer(this);
    m_mainTabs->addTab(m_offlineViewer, "Offline Analyzer");
    connect(m_offlineViewer, &OfflineViewer::themeToggleRequested, this, &MainWindow::setDarkMode);
#endif

    centerLayout->addWidget(m_mainTabs);
    setCentralWidget(center);

    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString& msg) {
        if (!msg.trimmed().isEmpty())
            appendLogLine(msg);
    });

    m_sidebarScroll = new QScrollArea(center);
    m_sidebarScroll->setObjectName("sidebarScroll");
    m_sidebarScroll->setWidgetResizable(true);
    m_sidebarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebarScroll->setFrameShape(QFrame::NoFrame);
    m_sidebarScroll->setMinimumWidth(260);
    m_sidebarContent = new QWidget();
    m_sidebarContent->setObjectName("sidebarContent");
    m_sidebarLayout = new QVBoxLayout(m_sidebarContent);
    m_sidebarLayout->setContentsMargins(8, 8, 8, 8);
    m_sidebarLayout->setSpacing(8);
    m_sidebarScroll->setWidget(m_sidebarContent);
    m_mainSplitter->addWidget(m_sidebarScroll);

    m_operatorPanel = new QWidget(m_sidebarContent);
    m_operatorPanel->setObjectName("operatorPanel");
    QVBoxLayout* operatorLayout = new QVBoxLayout(m_operatorPanel);
    operatorLayout->setContentsMargins(0, 0, 0, 0);
    operatorLayout->setSpacing(10);
    m_topOperatorPanel = new QWidget(m_topControlsHost);
    m_topOperatorPanel->setObjectName("operatorPanel");
    QHBoxLayout* topOperatorLayout = new QHBoxLayout(m_topOperatorPanel);
    topOperatorLayout->setContentsMargins(0, 0, 0, 0);
    topOperatorLayout->setSpacing(10);

    for (int i = 0; i < 2; ++i)
    {
        QFrame* card = new QFrame(m_operatorPanel);
        card->setObjectName("operatorCard");
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 8, 8, 8);
        cardLayout->setSpacing(6);
        QHBoxLayout* headerRow = new QHBoxLayout();
        headerRow->setContentsMargins(0, 0, 0, 0);
        headerRow->setSpacing(4);

        QLabel* title = new QLabel(QString("CAM %1").arg(i), card);
        title->setObjectName("operatorTitle");
        headerRow->addWidget(title);
        headerRow->addStretch(1);

        m_sidebarCopySettingsBtns[i] = new QPushButton(QString("Apply CAM %1 Settings").arg(1 - i), card);
        m_sidebarCopySettingsBtns[i]->setObjectName("presetBtn");
        m_sidebarCopySettingsBtns[i]->setToolTip(QString("Apply all settings from CAM %1 to CAM %2").arg(1 - i).arg(i));
        headerRow->addWidget(m_sidebarCopySettingsBtns[i]);

        cardLayout->addLayout(headerRow);

        QHBoxLayout* sourceRow = new QHBoxLayout();
        sourceRow->setContentsMargins(0, 0, 0, 0);
        sourceRow->setSpacing(3);
        QLabel* sourceLabel = new QLabel("Source", card);
        sourceLabel->setObjectName("compactLabel");
        m_sidebarSourceCombos[i] = new QComboBox(card);
        m_sidebarSourceCombos[i]->setObjectName("compactCombo");
        m_sidebarSourceCombos[i]->addItem("Detect first", -1);
        m_sidebarSourceCombos[i]->setEnabled(false);
        sourceRow->addWidget(sourceLabel);
        sourceRow->addWidget(m_sidebarSourceCombos[i], 1);
        cardLayout->addLayout(sourceRow);

        QWidget* sidebarSaveRow = new QWidget(card);
        QHBoxLayout* sidebarSaveLayout = new QHBoxLayout(sidebarSaveRow);
        sidebarSaveLayout->setContentsMargins(0, 0, 0, 0);
        sidebarSaveLayout->setSpacing(3);
        QLabel* sidebarSaveLabel = new QLabel("Save", sidebarSaveRow);
        sidebarSaveLabel->setObjectName("compactLabel");
        m_sidebarSavePathEdits[i] = new QLineEdit(sidebarSaveRow);
        m_sidebarSavePathEdits[i]->setObjectName("compactEdit");
        m_sidebarSaveFormatCombos[i] = new QComboBox(sidebarSaveRow);
        m_sidebarSaveFormatCombos[i]->addItems({".raw", ".bmp"});
        sidebarSaveLayout->addWidget(sidebarSaveLabel);
        sidebarSaveLayout->addWidget(m_sidebarSavePathEdits[i], 1);
        sidebarSaveLayout->addWidget(m_sidebarSaveFormatCombos[i]);
        cardLayout->addWidget(sidebarSaveRow);

        QGridLayout* compactForm = new QGridLayout();
        compactForm->setHorizontalSpacing(4);
        compactForm->setVerticalSpacing(4);
        auto makeCompactField = [&](const QString& labelText, QLineEdit** target) {
            QWidget* wrap = new QWidget(card);
            QHBoxLayout* wrapLayout = new QHBoxLayout(wrap);
            wrapLayout->setContentsMargins(0, 0, 0, 0);
            wrapLayout->setSpacing(3);
            QLabel* label = new QLabel(labelText, wrap);
            label->setObjectName("compactLabel");
            *target = new QLineEdit(wrap);
            (*target)->setObjectName("compactEdit");
            wrapLayout->addWidget(label);
            wrapLayout->addWidget(*target, 1);
            return wrap;
        };
        auto makeCompactCombo = [&](const QString& labelText, QComboBox** target, const QStringList& items) {
            QWidget* wrap = new QWidget(card);
            QHBoxLayout* wrapLayout = new QHBoxLayout(wrap);
            wrapLayout->setContentsMargins(0, 0, 0, 0);
            wrapLayout->setSpacing(3);
            QLabel* label = new QLabel(labelText, wrap);
            label->setObjectName("compactLabel");
            *target = new QComboBox(wrap);
            (*target)->setObjectName("compactCombo");
            (*target)->addItems(items);
            wrapLayout->addWidget(label);
            wrapLayout->addWidget(*target, 1);
            return wrap;
        };
        compactForm->addWidget(makeCompactField("W", &m_sidebarWidthEdits[i]), 0, 0);
        compactForm->addWidget(makeCompactField("H", &m_sidebarHeightEdits[i]), 0, 1);
        compactForm->addWidget(makeCompactField("FPS", &m_sidebarFpsEdits[i]), 1, 0);
        compactForm->addWidget(makeCompactCombo("Test", &m_sidebarTestPatternCombos[i], {"Off", "Gray Diagonal Ramp"}), 1, 1);
        compactForm->addWidget(makeCompactField("Exp", &m_sidebarExposureEdits[i]), 2, 0);
        compactForm->addWidget(makeCompactField("Gain", &m_sidebarGainEdits[i]), 2, 1);
        cardLayout->addLayout(compactForm);

        m_sidebarWidthEdits[i]->setValidator(new QIntValidator(1024, 9344, this));
        m_sidebarHeightEdits[i]->setValidator(new QIntValidator(4, 7001, this));
        m_sidebarFpsEdits[i]->setValidator(new QDoubleValidator(0.1, 100.0, 2, this));
        m_sidebarExposureEdits[i]->setValidator(new QDoubleValidator(1.0, 100000.0, 1, this));
        m_sidebarGainEdits[i]->setValidator(new QDoubleValidator(1.0, 16.0, 2, this));

        m_sidebarWidthEdits[i]->setToolTip("Width (min 8192, max 9344)");
        m_sidebarHeightEdits[i]->setToolTip("Height / Region Height per active region (Total acquired size displayed at bottom of image)");
        m_sidebarFpsEdits[i]->setToolTip("Frame Rate (Hz)");
        m_sidebarExposureEdits[i]->setToolTip("Exposure Time (µs)");
        m_sidebarGainEdits[i]->setToolTip("Sensor Gain (1.0 to 16.0)");

        QWidget* sidebarTdiRow = new QWidget(card);
        QHBoxLayout* sidebarTdiLayout = new QHBoxLayout(sidebarTdiRow);
        sidebarTdiLayout->setContentsMargins(0, 0, 0, 0);
        sidebarTdiLayout->setSpacing(3);
        QLabel* sidebarTdiLabel = new QLabel("TDI", sidebarTdiRow);
        sidebarTdiLabel->setObjectName("compactLabel");
        m_sidebarTdiModeCombos[i] = new QComboBox(sidebarTdiRow);
        m_sidebarTdiModeCombos[i]->addItems({"Off", "Record", "TDI"});
        m_sidebarReverseXChecks[i] = new QCheckBox("RevX", sidebarTdiRow);
        m_sidebarReverseYChecks[i] = new QCheckBox("RevY", sidebarTdiRow);
        sidebarTdiLayout->addWidget(sidebarTdiLabel);
        sidebarTdiLayout->addWidget(m_sidebarTdiModeCombos[i], 1);
        sidebarTdiLayout->addWidget(m_sidebarReverseXChecks[i]);
        sidebarTdiLayout->addWidget(m_sidebarReverseYChecks[i]);
        cardLayout->addWidget(sidebarTdiRow);

        QWidget* sidebarTdiCfgRow = new QWidget(card);
        QHBoxLayout* sidebarTdiCfg = new QHBoxLayout(sidebarTdiCfgRow);
        sidebarTdiCfg->setContentsMargins(0, 0, 0, 0);
        sidebarTdiCfg->setSpacing(3);

        QWidget* stgGroup = new QWidget(sidebarTdiCfgRow);
        QHBoxLayout* stgLayout = new QHBoxLayout(stgGroup);
        stgLayout->setContentsMargins(0, 0, 0, 0);
        stgLayout->setSpacing(2);
        QLabel* stgLbl = new QLabel("Stg", stgGroup);
        stgLbl->setObjectName("compactLabel");
        m_sidebarTdiStagesEdits[i] = new QLineEdit(stgGroup);
        m_sidebarTdiStagesEdits[i]->setObjectName("compactEdit");
        m_sidebarTdiStagesEdits[i]->setValidator(new QIntValidator(1, 32, this));
        m_sidebarTdiStagesEdits[i]->setToolTip("TDI Stages (1 to 32)");
        stgLayout->addWidget(stgLbl);
        stgLayout->addWidget(m_sidebarTdiStagesEdits[i], 1);
        m_sidebarTdiStagesGroup[i] = stgGroup;
        sidebarTdiCfg->addWidget(stgGroup, 1);

        QWidget* yShiftGroup = new QWidget(sidebarTdiCfgRow);
        QHBoxLayout* yShiftLayout = new QHBoxLayout(yShiftGroup);
        yShiftLayout->setContentsMargins(0, 0, 0, 0);
        yShiftLayout->setSpacing(2);
        QLabel* yShiftLbl = new QLabel("Y-Shift", yShiftGroup);
        yShiftLbl->setObjectName("compactLabel");
        m_sidebarTdiYShiftEdits[i] = new QLineEdit(yShiftGroup);
        m_sidebarTdiYShiftEdits[i]->setObjectName("compactEdit");
        m_sidebarTdiYShiftEdits[i]->setValidator(new QIntValidator(0, 4096, this));
        m_sidebarTdiYShiftEdits[i]->setToolTip("TDI Y-Shift (lines)");
        yShiftLayout->addWidget(yShiftLbl);
        yShiftLayout->addWidget(m_sidebarTdiYShiftEdits[i], 1);
        m_sidebarTdiYShiftGroup[i] = yShiftGroup;
        sidebarTdiCfg->addWidget(yShiftGroup, 1);

        QWidget* frameCountGroup = new QWidget(sidebarTdiCfgRow);
        QHBoxLayout* frameCountLayout = new QHBoxLayout(frameCountGroup);
        frameCountLayout->setContentsMargins(0, 0, 0, 0);
        frameCountLayout->setSpacing(3);
        QLabel* frameCountLbl = new QLabel("Frames", frameCountGroup);
        frameCountLbl->setObjectName("compactLabel");
        m_sidebarTdiFrameCountEdits[i] = new QLineEdit(frameCountGroup);
        m_sidebarTdiFrameCountEdits[i]->setObjectName("compactEdit");
        m_sidebarTdiFrameCountEdits[i]->setValidator(new QIntValidator(1, 65535, this));
        m_sidebarTdiFrameCountEdits[i]->setToolTip("TDI Frame Count (Record mode)");
        frameCountLayout->addWidget(frameCountLbl);
        frameCountLayout->addWidget(m_sidebarTdiFrameCountEdits[i], 1);
        m_sidebarTdiFrameCountGroup[i] = frameCountGroup;
        sidebarTdiCfg->addWidget(frameCountGroup, 1);

        m_sidebarTdiCfgRow[i] = sidebarTdiCfgRow;
        cardLayout->addWidget(sidebarTdiCfgRow);

        QHBoxLayout* actionRow = new QHBoxLayout();
        actionRow->setSpacing(4);
        m_sidebarConnectBtns[i] = new QPushButton("Connect", card);
        m_sidebarStartBtns[i] = new QPushButton("Start", card);
        m_sidebarSaveBtns[i] = new QPushButton("Start Save", card);
        m_sidebarConnectBtns[i]->setObjectName("connectBtn");
        m_sidebarStartBtns[i]->setObjectName("startBtn");
        m_sidebarSaveBtns[i]->setObjectName("saveBtn");
        m_sidebarConnectBtns[i]->setFixedWidth(82);
        m_sidebarStartBtns[i]->setFixedWidth(68);
        m_sidebarSaveBtns[i]->setFixedWidth(98);
        actionRow->addWidget(m_sidebarConnectBtns[i]);
        actionRow->addWidget(m_sidebarStartBtns[i]);
        actionRow->addWidget(m_sidebarSaveBtns[i]);
        cardLayout->addLayout(actionRow);

        QWidget* saveModeRow = new QWidget(card);
        QHBoxLayout* saveModeLayout = new QHBoxLayout(saveModeRow);
        saveModeLayout->setContentsMargins(0, 0, 0, 0);
        saveModeLayout->setSpacing(3);
        QLabel* saveModeLabel = new QLabel("Mode", saveModeRow);
        saveModeLabel->setObjectName("compactLabel");
        m_sidebarSaveModeCombos[i] = new QComboBox(saveModeRow);
        m_sidebarSaveModeCombos[i]->addItems({"Sequence", "Single File"});
        m_sidebarSaveModeCombos[i]->setCurrentText("Sequence");
        saveModeLayout->addWidget(saveModeLabel);
        saveModeLayout->addWidget(m_sidebarSaveModeCombos[i], 1);
        cardLayout->addWidget(saveModeRow);

        QWidget* durationRow = new QWidget(card);
        QHBoxLayout* durationLayout = new QHBoxLayout(durationRow);
        durationLayout->setContentsMargins(0, 0, 0, 0);
        durationLayout->setSpacing(4);
        m_sidebarStopAfterDurationChecks[i] = new QCheckBox("Stop after", durationRow);
        m_sidebarSaveDurationEdits[i] = new QLineEdit("5.0", durationRow);
        m_sidebarSaveDurationEdits[i]->setPlaceholderText("Seconds");
        m_sidebarSaveDurationEdits[i]->setEnabled(false);
        connect(m_sidebarStopAfterDurationChecks[i], &QCheckBox::toggled,
                m_sidebarSaveDurationEdits[i], &QLineEdit::setEnabled);
        durationLayout->addWidget(m_sidebarStopAfterDurationChecks[i]);
        durationLayout->addWidget(m_sidebarSaveDurationEdits[i], 1);
        cardLayout->addWidget(durationRow);

        QHBoxLayout* regionBinningRow = new QHBoxLayout();
        regionBinningRow->setContentsMargins(0, 0, 0, 0);
        regionBinningRow->setSpacing(4);

        m_sidebarSettingsBtns[i] = new QPushButton("Region Modes", card);
        m_sidebarSettingsBtns[i]->setObjectName("presetBtn");
        regionBinningRow->addWidget(m_sidebarSettingsBtns[i], 1);

        m_sidebarBinningBtns[i] = new QPushButton("Binning Modes", card);
        m_sidebarBinningBtns[i]->setObjectName("presetBtn");
        regionBinningRow->addWidget(m_sidebarBinningBtns[i], 1);

        cardLayout->addLayout(regionBinningRow);

        operatorLayout->addWidget(card);

        QFrame* topCard = new QFrame(m_topOperatorPanel);
        topCard->setObjectName("operatorCard");
        QVBoxLayout* topCardLayout = new QVBoxLayout(topCard);
        topCardLayout->setContentsMargins(8, 8, 8, 8);
        topCardLayout->setSpacing(6);

        QHBoxLayout* topHeaderRow = new QHBoxLayout();
        topHeaderRow->setContentsMargins(0, 0, 0, 0);
        topHeaderRow->setSpacing(4);

        QLabel* topTitle = new QLabel(QString("CAM %1").arg(i), topCard);
        topTitle->setObjectName("operatorTitle");
        topHeaderRow->addWidget(topTitle);
        topHeaderRow->addStretch(1);

        m_topCopySettingsBtns[i] = new QPushButton(QString("Apply CAM %1 Settings").arg(1 - i), topCard);
        m_topCopySettingsBtns[i]->setObjectName("presetBtn");
        m_topCopySettingsBtns[i]->setToolTip(QString("Apply all settings from CAM %1 to CAM %2").arg(1 - i).arg(i));
        topHeaderRow->addWidget(m_topCopySettingsBtns[i]);

        topCardLayout->addLayout(topHeaderRow);

        m_topStateLabels[i] = new QLabel("Disconnected", topCard);
        m_topStateLabels[i]->setObjectName("operatorState");
        topCardLayout->addWidget(m_topStateLabels[i]);

        QHBoxLayout* topSourceRow = new QHBoxLayout();
        topSourceRow->setContentsMargins(0, 0, 0, 0);
        topSourceRow->setSpacing(3);
        QLabel* topSourceLabel = new QLabel("Source", topCard);
        topSourceLabel->setObjectName("compactLabel");
        m_topSourceCombos[i] = new QComboBox(topCard);
        m_topSourceCombos[i]->setObjectName("compactCombo");
        m_topSourceCombos[i]->addItem("Detect first", -1);
        m_topSourceCombos[i]->setEnabled(false);
        topSourceRow->addWidget(topSourceLabel);
        topSourceRow->addWidget(m_topSourceCombos[i], 1);
        topCardLayout->addLayout(topSourceRow);

        QWidget* topSaveRow = new QWidget(topCard);
        QHBoxLayout* topSaveLayout = new QHBoxLayout(topSaveRow);
        topSaveLayout->setContentsMargins(0, 0, 0, 0);
        topSaveLayout->setSpacing(3);
        QLabel* topSaveLabel = new QLabel("Save", topSaveRow);
        topSaveLabel->setObjectName("compactLabel");
        m_topSavePathEdits[i] = new QLineEdit(topSaveRow);
        m_topSavePathEdits[i]->setObjectName("compactEdit");
        m_topSaveFormatCombos[i] = new QComboBox(topSaveRow);
        m_topSaveFormatCombos[i]->addItems({".raw", ".bmp"});
        topSaveLayout->addWidget(topSaveLabel);
        topSaveLayout->addWidget(m_topSavePathEdits[i], 1);
        topSaveLayout->addWidget(m_topSaveFormatCombos[i]);
        topCardLayout->addWidget(topSaveRow);

        QGridLayout* topCompactForm = new QGridLayout();
        topCompactForm->setHorizontalSpacing(4);
        topCompactForm->setVerticalSpacing(4);
        auto makeTopCompactField = [&](const QString& labelText, QLineEdit** target) {
            QWidget* wrap = new QWidget(topCard);
            QHBoxLayout* wrapLayout = new QHBoxLayout(wrap);
            wrapLayout->setContentsMargins(0, 0, 0, 0);
            wrapLayout->setSpacing(3);
            QLabel* label = new QLabel(labelText, wrap);
            label->setObjectName("compactLabel");
            *target = new QLineEdit(wrap);
            (*target)->setObjectName("compactEdit");
            wrapLayout->addWidget(label);
            wrapLayout->addWidget(*target, 1);
            return wrap;
        };
        auto makeTopCompactCombo = [&](const QString& labelText, QComboBox** target, const QStringList& items) {
            QWidget* wrap = new QWidget(topCard);
            QHBoxLayout* wrapLayout = new QHBoxLayout(wrap);
            wrapLayout->setContentsMargins(0, 0, 0, 0);
            wrapLayout->setSpacing(3);
            QLabel* label = new QLabel(labelText, wrap);
            label->setObjectName("compactLabel");
            *target = new QComboBox(wrap);
            (*target)->setObjectName("compactCombo");
            (*target)->addItems(items);
            wrapLayout->addWidget(label);
            wrapLayout->addWidget(*target, 1);
            return wrap;
        };
        topCompactForm->setColumnStretch(0, 1);
        topCompactForm->setColumnStretch(1, 1);
        topCompactForm->setColumnStretch(2, 1);
        topCompactForm->addWidget(makeTopCompactField("W", &m_topWidthEdits[i]), 0, 0);
        topCompactForm->addWidget(makeTopCompactField("H", &m_topHeightEdits[i]), 0, 1);
        topCompactForm->addWidget(makeTopCompactField("FPS", &m_topFpsEdits[i]), 0, 2);
        topCompactForm->addWidget(makeTopCompactField("Exp", &m_topExposureEdits[i]), 1, 0);
        topCompactForm->addWidget(makeTopCompactField("Gain", &m_topGainEdits[i]), 1, 1);
        topCompactForm->addWidget(makeTopCompactCombo("Test", &m_topTestPatternCombos[i], {"Off", "Gray Diagonal Ramp"}), 1, 2);
        topCardLayout->addLayout(topCompactForm);

        m_topWidthEdits[i]->setValidator(new QIntValidator(1024, 9344, this));
        m_topHeightEdits[i]->setValidator(new QIntValidator(4, 7001, this));
        m_topFpsEdits[i]->setValidator(new QDoubleValidator(0.1, 100.0, 2, this));
        m_topExposureEdits[i]->setValidator(new QDoubleValidator(1.0, 100000.0, 1, this));
        m_topGainEdits[i]->setValidator(new QDoubleValidator(1.0, 16.0, 2, this));

        m_topWidthEdits[i]->setToolTip("Width (min 8192, max 9344)");
        m_topHeightEdits[i]->setToolTip("Height / Region Height per active region (Total acquired size displayed at bottom of image)");
        m_topFpsEdits[i]->setToolTip("Frame Rate (Hz)");
        m_topExposureEdits[i]->setToolTip("Exposure Time (µs)");
        m_topGainEdits[i]->setToolTip("Sensor Gain (1.0 to 16.0)");

        QWidget* topTdiRow = new QWidget(topCard);
        QHBoxLayout* topTdiLayout = new QHBoxLayout(topTdiRow);
        topTdiLayout->setContentsMargins(0, 0, 0, 0);
        topTdiLayout->setSpacing(3);
        QLabel* topTdiLabel = new QLabel("TDI", topTdiRow);
        topTdiLabel->setObjectName("compactLabel");
        m_topTdiModeCombos[i] = new QComboBox(topTdiRow);
        m_topTdiModeCombos[i]->addItems({"Off", "Record", "TDI"});
        m_topReverseXChecks[i] = new QCheckBox("RevX", topTdiRow);
        m_topReverseYChecks[i] = new QCheckBox("RevY", topTdiRow);
        topTdiLayout->addWidget(topTdiLabel);
        topTdiLayout->addWidget(m_topTdiModeCombos[i], 1);
        topTdiLayout->addWidget(m_topReverseXChecks[i]);
        topTdiLayout->addWidget(m_topReverseYChecks[i]);
        topCardLayout->addWidget(topTdiRow);

        QWidget* topTdiCfgRow = new QWidget(topCard);
        QHBoxLayout* topTdiCfg = new QHBoxLayout(topTdiCfgRow);
        topTdiCfg->setContentsMargins(0, 0, 0, 0);
        topTdiCfg->setSpacing(3);

        QWidget* topStgGroup = new QWidget(topTdiCfgRow);
        QHBoxLayout* topStgLayout = new QHBoxLayout(topStgGroup);
        topStgLayout->setContentsMargins(0, 0, 0, 0);
        topStgLayout->setSpacing(2);
        QLabel* topStgLbl = new QLabel("Stg", topStgGroup);
        topStgLbl->setObjectName("compactLabel");
        m_topTdiStagesEdits[i] = new QLineEdit(topStgGroup);
        m_topTdiStagesEdits[i]->setObjectName("compactEdit");
        m_topTdiStagesEdits[i]->setValidator(new QIntValidator(1, 32, this));
        m_topTdiStagesEdits[i]->setToolTip("TDI Stages (1 to 32)");
        topStgLayout->addWidget(topStgLbl);
        topStgLayout->addWidget(m_topTdiStagesEdits[i], 1);
        m_topTdiStagesGroup[i] = topStgGroup;
        topTdiCfg->addWidget(topStgGroup, 1);

        QWidget* topYShiftGroup = new QWidget(topTdiCfgRow);
        QHBoxLayout* topYShiftLayout = new QHBoxLayout(topYShiftGroup);
        topYShiftLayout->setContentsMargins(0, 0, 0, 0);
        topYShiftLayout->setSpacing(2);
        QLabel* topYShiftLbl = new QLabel("Y-Shift", topYShiftGroup);
        topYShiftLbl->setObjectName("compactLabel");
        m_topTdiYShiftEdits[i] = new QLineEdit(topYShiftGroup);
        m_topTdiYShiftEdits[i]->setObjectName("compactEdit");
        m_topTdiYShiftEdits[i]->setValidator(new QIntValidator(0, 4096, this));
        m_topTdiYShiftEdits[i]->setToolTip("TDI Y-Shift (lines)");
        topYShiftLayout->addWidget(topYShiftLbl);
        topYShiftLayout->addWidget(m_topTdiYShiftEdits[i], 1);
        m_topTdiYShiftGroup[i] = topYShiftGroup;
        topTdiCfg->addWidget(topYShiftGroup, 1);

        QWidget* topFrameCountGroup = new QWidget(topTdiCfgRow);
        QHBoxLayout* topFrameCountLayout = new QHBoxLayout(topFrameCountGroup);
        topFrameCountLayout->setContentsMargins(0, 0, 0, 0);
        topFrameCountLayout->setSpacing(3);
        QLabel* topFrameCountLbl = new QLabel("Frames", topFrameCountGroup);
        topFrameCountLbl->setObjectName("compactLabel");
        m_topTdiFrameCountEdits[i] = new QLineEdit(topFrameCountGroup);
        m_topTdiFrameCountEdits[i]->setObjectName("compactEdit");
        m_topTdiFrameCountEdits[i]->setValidator(new QIntValidator(1, 65535, this));
        m_topTdiFrameCountEdits[i]->setToolTip("TDI Frame Count (Record mode)");
        topFrameCountLayout->addWidget(topFrameCountLbl);
        topFrameCountLayout->addWidget(m_topTdiFrameCountEdits[i], 1);
        m_topTdiFrameCountGroup[i] = topFrameCountGroup;
        topTdiCfg->addWidget(topFrameCountGroup, 1);

        m_topTdiCfgRow[i] = topTdiCfgRow;
        topCardLayout->addWidget(topTdiCfgRow);

        QHBoxLayout* topActionRow = new QHBoxLayout();
        topActionRow->setSpacing(4);
        m_topConnectBtns[i] = new QPushButton("Connect", topCard);
        m_topStartBtns[i] = new QPushButton("Start", topCard);
        m_topSaveBtns[i] = new QPushButton("Save", topCard);
        m_topConnectBtns[i]->setObjectName("connectBtn");
        m_topStartBtns[i]->setObjectName("startBtn");
        m_topSaveBtns[i]->setObjectName("saveBtn");
        topActionRow->addWidget(m_topConnectBtns[i], 1);
        topActionRow->addWidget(m_topStartBtns[i], 1);
        topActionRow->addWidget(m_topSaveBtns[i], 1);
        topCardLayout->addLayout(topActionRow);

        QWidget* topSaveControlRow = new QWidget(topCard);
        QHBoxLayout* topSaveControlLayout = new QHBoxLayout(topSaveControlRow);
        topSaveControlLayout->setContentsMargins(0, 0, 0, 0);
        topSaveControlLayout->setSpacing(4);
        QLabel* topSaveModeLabel = new QLabel("Mode", topSaveControlRow);
        topSaveModeLabel->setObjectName("compactLabel");
        m_topSaveModeCombos[i] = new QComboBox(topSaveControlRow);
        m_topSaveModeCombos[i]->addItems({"Sequence", "Single File"});
        m_topSaveModeCombos[i]->setCurrentText("Sequence");
        topSaveControlLayout->addWidget(topSaveModeLabel);
        topSaveControlLayout->addWidget(m_topSaveModeCombos[i], 1);
        m_topStopAfterDurationChecks[i] = new QCheckBox("Stop after", topSaveControlRow);
        m_topSaveDurationEdits[i] = new QLineEdit("5.0", topSaveControlRow);
        m_topSaveDurationEdits[i]->setPlaceholderText("Seconds");
        m_topSaveDurationEdits[i]->setEnabled(false);
        connect(m_topStopAfterDurationChecks[i], &QCheckBox::toggled,
                m_topSaveDurationEdits[i], &QLineEdit::setEnabled);
        topSaveControlLayout->addWidget(m_topStopAfterDurationChecks[i]);
        topSaveControlLayout->addWidget(m_topSaveDurationEdits[i], 1);
        topCardLayout->addWidget(topSaveControlRow);

        QHBoxLayout* topRegionBinningRow = new QHBoxLayout();
        topRegionBinningRow->setContentsMargins(0, 0, 0, 0);
        topRegionBinningRow->setSpacing(4);

        m_topSettingsBtns[i] = new QPushButton("Region Modes", topCard);
        m_topSettingsBtns[i]->setObjectName("presetBtn");
        topRegionBinningRow->addWidget(m_topSettingsBtns[i], 1);

        m_topBinningBtns[i] = new QPushButton("Binning Modes", topCard);
        m_topBinningBtns[i]->setObjectName("presetBtn");
        topRegionBinningRow->addWidget(m_topBinningBtns[i], 1);

        topCardLayout->addLayout(topRegionBinningRow);

        topOperatorLayout->addWidget(topCard, 1);
    }
    operatorLayout->addStretch(1);

    m_panelParking = new QWidget(this);
    m_panelParking->hide();

    m_viewHost = new QWidget(center);
    m_viewHost->setObjectName("viewHost");
    m_viewHostLayout = new QVBoxLayout(m_viewHost);
    m_viewHostLayout->setContentsMargins(0, 0, 0, 0);
    m_viewHostLayout->setSpacing(0);
    m_controlTabs = new QTabWidget(m_sidebarContent);
    m_controlTabs->setDocumentMode(true);
    m_viewTabs = new QTabWidget(m_viewHost);
    m_viewTabs->setDocumentMode(true);
    m_viewTabs->tabBar()->setExpanding(true);
    m_controlsTopSplitter = new QSplitter(Qt::Vertical, m_viewHost);
    m_controlsTopSplitter->setChildrenCollapsible(false);
    m_topControlsHost = new QWidget(m_viewHost);
    m_topControlsHost->setObjectName("topControlsHost");
    m_topControlsLayout = new QHBoxLayout(m_topControlsHost);
    m_topControlsLayout->setContentsMargins(0, 0, 0, 8);
    m_topControlsLayout->setSpacing(8);
    // --- OFFLINE TAB UI IS NOW INSIDE OfflineViewer ---
    
    m_viewSplitter = new QSplitter(Qt::Horizontal, m_viewHost);
    m_viewSplitter->setChildrenCollapsible(false);
    m_viewHostLayout->addWidget(m_viewSplitter);
    m_mainSplitter->addWidget(m_viewHost);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({420, 1500});

    // ── Camera panels (controls only — scroll area hidden) ────────────
    for (int i = 0; i < 2; ++i)
    {
        m_panels[i] = new CameraPanel(i, this);
        connect(m_panels[i], &CameraPanel::connectClicked, this, &MainWindow::onConnectClicked);
        connect(m_panels[i], &CameraPanel::startClicked,   this, &MainWindow::onStartClicked);
        connect(m_panels[i], &CameraPanel::saveClicked,    this, &MainWindow::onSaveClicked);
        connect(m_panels[i], &CameraPanel::browseSave,     this, &MainWindow::onBrowseSave);
        connect(m_panels[i], &CameraPanel::browseRaw,      this, &MainWindow::onBrowseRaw);
        connect(m_panels[i], &CameraPanel::playbackClicked,this, &MainWindow::onPlaybackClicked);
        m_panels[i]->scrollArea->setVisible(false);   // view lives in separate dock
        if (m_panels[i]->scrollArea && m_panels[i]->scrollArea->viewport())
            m_panels[i]->scrollArea->viewport()->installEventFilter(this);
        m_panels[i]->setMinimumWidth(0);
        m_panels[i]->setMaximumWidth(QWIDGETSIZE_MAX);
        m_panels[i]->setParent(m_panelParking);
        connect(m_sidebarConnectBtns[i], &QPushButton::clicked, this, [this, i]{ onConnectClicked(i); });
        connect(m_sidebarStartBtns[i],   &QPushButton::clicked, this, [this, i]{ onStartClicked(i); });
        connect(m_sidebarSaveBtns[i],    &QPushButton::clicked, this, [this, i]{ onSaveClicked(i); });
        connect(m_sidebarSettingsBtns[i], &QPushButton::clicked, this, [this, i]{ openRegionSettingsDialog(i); });
        connect(m_sidebarBinningBtns[i],  &QPushButton::clicked, this, [this, i]{ openBinningSettingsDialog(i); });
        connect(m_topConnectBtns[i], &QPushButton::clicked, this, [this, i]{ onConnectClicked(i); });
        connect(m_topStartBtns[i],   &QPushButton::clicked, this, [this, i]{ onStartClicked(i); });
        connect(m_topSaveBtns[i],    &QPushButton::clicked, this, [this, i]{ onSaveClicked(i); });
        connect(m_topSettingsBtns[i], &QPushButton::clicked, this, [this, i]{ openRegionSettingsDialog(i); });
        connect(m_topBinningBtns[i],  &QPushButton::clicked, this, [this, i]{ openBinningSettingsDialog(i); });
        connect(m_sidebarCopySettingsBtns[i], &QPushButton::clicked, this, [this, i]{ copyCameraSettings(1 - i, i); });
        connect(m_topCopySettingsBtns[i],     &QPushButton::clicked, this, [this, i]{ copyCameraSettings(1 - i, i); });
        connect(m_panels[i], &CameraPanel::copySettingsClicked, this, [this, i]{ copyCameraSettings(1 - i, i); });
        connect(m_sidebarSourceCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i](int index) {
                    if (m_topSourceCombos[i] && m_topSourceCombos[i]->currentIndex() != index)
                        m_topSourceCombos[i]->setCurrentIndex(index);
                    applyCameraSourceSelection(i);
                });
        connect(m_topSourceCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i](int index) {
                    if (m_sidebarSourceCombos[i] && m_sidebarSourceCombos[i]->currentIndex() != index)
                        m_sidebarSourceCombos[i]->setCurrentIndex(index);
                    applyCameraSourceSelection(i);
                });

        auto bindCompactEdit = [](QLineEdit* source, QLineEdit* target) {
            QObject::connect(source, &QLineEdit::textEdited, target, &QLineEdit::setText);
        };
        bindCompactEdit(m_sidebarTdiStagesEdits[i], m_panels[i]->tdiStagesEdit);
        bindCompactEdit(m_sidebarTdiYShiftEdits[i], m_panels[i]->tdiYShiftEdit);
        bindCompactEdit(m_sidebarTdiFrameCountEdits[i], m_panels[i]->tdiFrameCountEdit);
        bindCompactEdit(m_topTdiStagesEdits[i],     m_panels[i]->tdiStagesEdit);
        bindCompactEdit(m_topTdiYShiftEdits[i],     m_panels[i]->tdiYShiftEdit);
        bindCompactEdit(m_topTdiFrameCountEdits[i], m_panels[i]->tdiFrameCountEdit);
        connect(m_sidebarTdiModeCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                m_panels[i]->tdiModeCombo, &QComboBox::setCurrentIndex);
        connect(m_topTdiModeCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                m_panels[i]->tdiModeCombo, &QComboBox::setCurrentIndex);
        connect(m_panels[i]->tdiModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                m_sidebarTdiModeCombos[i], &QComboBox::setCurrentIndex);
        connect(m_panels[i]->tdiModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                m_topTdiModeCombos[i], &QComboBox::setCurrentIndex);
        connect(m_panels[i]->tdiModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i](int){ updateTdiUiVisibility(i); });
        connect(m_sidebarReverseXChecks[i], &QCheckBox::toggled, m_panels[i]->reverseXCheck, &QCheckBox::setChecked);
        connect(m_topReverseXChecks[i], &QCheckBox::toggled, m_panels[i]->reverseXCheck, &QCheckBox::setChecked);
        connect(m_panels[i]->reverseXCheck, &QCheckBox::toggled, m_sidebarReverseXChecks[i], &QCheckBox::setChecked);
        connect(m_panels[i]->reverseXCheck, &QCheckBox::toggled, m_topReverseXChecks[i], &QCheckBox::setChecked);
        connect(m_sidebarReverseYChecks[i], &QCheckBox::toggled, m_panels[i]->reverseYCheck, &QCheckBox::setChecked);
        connect(m_topReverseYChecks[i], &QCheckBox::toggled, m_panels[i]->reverseYCheck, &QCheckBox::setChecked);
        connect(m_panels[i]->reverseYCheck, &QCheckBox::toggled, m_sidebarReverseYChecks[i], &QCheckBox::setChecked);
        connect(m_panels[i]->reverseYCheck, &QCheckBox::toggled, m_topReverseYChecks[i], &QCheckBox::setChecked);
        // Single source-of-truth for the core compact controls.
        updateCameraUiState(i,
            m_panels[i]->widthEdit->text(),
            m_panels[i]->heightEdit->text(),
            m_panels[i]->fpsEdit->text(),
            m_panels[i]->exposureEdit->text(),
            m_panels[i]->gainEdit->text(),
            m_panels[i]->testPatternCombo->currentText(),
            m_panels[i]->savePathEdit->text(),
            m_panels[i]->saveFormatCombo->currentText(),
            m_panels[i]->saveModeCombo->currentText());
        updateCameraAdvancedState(i,
            m_panels[i]->tdiModeCombo->currentText(),
            m_panels[i]->tdiStagesEdit->text(),
            m_panels[i]->tdiYShiftEdit->text(),
            m_panels[i]->tdiFrameCountEdit->text(),
            m_panels[i]->reverseXCheck->isChecked(),
            m_panels[i]->reverseYCheck->isChecked(),
            m_panels[i]->stopAfterDurationCheck->isChecked(),
            m_panels[i]->saveDurationEdit->text());

        auto syncCameraStateFromWidgets = [this, i]() {
            updateCameraUiState(i,
                m_panels[i]->widthEdit->text(),
                m_panels[i]->heightEdit->text(),
                m_panels[i]->fpsEdit->text(),
                m_panels[i]->exposureEdit->text(),
                m_panels[i]->gainEdit->text(),
                m_panels[i]->testPatternCombo->currentText(),
                m_panels[i]->savePathEdit->text(),
                m_panels[i]->saveFormatCombo->currentText(),
                m_panels[i]->saveModeCombo->currentText());
        };

        connect(m_panels[i]->widthEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, value, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });
        connect(m_sidebarWidthEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, value, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });
        connect(m_topWidthEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, value, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });

        connect(m_panels[i]->heightEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, value, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });
        connect(m_sidebarHeightEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, value, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });
        connect(m_topHeightEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, value, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });

        connect(m_panels[i]->fpsEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, value, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        });
        auto syncFpsText = [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, value, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        };
        auto applyLiveFps = [this, i]() {
            bool ok = false;
            const double fps = m_cameraUiStates[i].fps.toDouble(&ok);
            if (ok && fps > 0.0 && m_connected[i] && m_cameraManager) {
                m_cameraManager->setLiveFps(i, fps);
            }
        };
        connect(m_panels[i]->fpsEdit, &QLineEdit::textEdited, this, syncFpsText);
        connect(m_sidebarFpsEdits[i], &QLineEdit::textEdited, this, syncFpsText);
        connect(m_topFpsEdits[i], &QLineEdit::textEdited, this, syncFpsText);
        connect(m_panels[i]->fpsEdit, &QLineEdit::editingFinished, this, applyLiveFps);
        connect(m_sidebarFpsEdits[i], &QLineEdit::editingFinished, this, applyLiveFps);
        connect(m_topFpsEdits[i], &QLineEdit::editingFinished, this, applyLiveFps);

        auto syncExposureText = [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, value,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, m_cameraUiStates[i].saveMode);
        };
        auto applyLiveExposure = [this, i]() {
            bool ok = false;
            const double exposureUs = m_cameraUiStates[i].exposure.toDouble(&ok);
            if (ok && exposureUs > 0.0 && m_connected[i] && m_cameraManager) {
                m_cameraManager->setLiveExposure(i, exposureUs);
            }
        };
        connect(m_panels[i]->exposureEdit, &QLineEdit::textEdited, this, syncExposureText);
        connect(m_sidebarExposureEdits[i], &QLineEdit::textEdited, this, syncExposureText);
        connect(m_topExposureEdits[i], &QLineEdit::textEdited, this, syncExposureText);
        connect(m_panels[i]->exposureEdit, &QLineEdit::editingFinished, this, applyLiveExposure);
        connect(m_sidebarExposureEdits[i], &QLineEdit::editingFinished, this, applyLiveExposure);
        connect(m_topExposureEdits[i], &QLineEdit::editingFinished, this, applyLiveExposure);

        auto syncGainText = [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                value, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath, m_cameraUiStates[i].saveFormat,
                m_cameraUiStates[i].saveMode);
        };
        auto applyLiveGain = [this, i]() {
            bool ok = false;
            const double gain = m_cameraUiStates[i].gain.toDouble(&ok);
            if (ok && m_connected[i] && m_cameraManager) {
                m_cameraManager->setLiveGain(i, gain);
            }
        };
        connect(m_panels[i]->gainEdit, &QLineEdit::textEdited, this, syncGainText);
        connect(m_sidebarGainEdits[i], &QLineEdit::textEdited, this, syncGainText);
        connect(m_topGainEdits[i], &QLineEdit::textEdited, this, syncGainText);
        connect(m_panels[i]->gainEdit, &QLineEdit::editingFinished, this, applyLiveGain);
        connect(m_sidebarGainEdits[i], &QLineEdit::editingFinished, this, applyLiveGain);
        connect(m_topGainEdits[i], &QLineEdit::editingFinished, this, applyLiveGain);

        auto applyLiveTestPattern = [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, value, m_cameraUiStates[i].savePath, m_cameraUiStates[i].saveFormat,
                m_cameraUiStates[i].saveMode);
            const int pat = (value == "Gray Diagonal Ramp") ? 1 : 0;
            if (m_connected[i] && m_cameraManager) {
                m_cameraManager->setLiveTestPattern(i, pat);
            }
        };
        connect(m_panels[i]->testPatternCombo, &QComboBox::currentTextChanged, this, applyLiveTestPattern);
        connect(m_sidebarTestPatternCombos[i], &QComboBox::currentTextChanged, this, applyLiveTestPattern);
        connect(m_topTestPatternCombos[i], &QComboBox::currentTextChanged, this, applyLiveTestPattern);

        auto onGeometryUpdated = [this, i]() {
            updateWorkerParams(i);
        };
        connect(m_sidebarWidthEdits[i], &QLineEdit::returnPressed, this, onGeometryUpdated);
        connect(m_sidebarHeightEdits[i], &QLineEdit::returnPressed, this, onGeometryUpdated);
        connect(m_topWidthEdits[i], &QLineEdit::returnPressed, this, onGeometryUpdated);
        connect(m_topHeightEdits[i], &QLineEdit::returnPressed, this, onGeometryUpdated);

        connect(m_panels[i]->rawPathEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath, m_cameraUiStates[i].saveFormat,
                m_cameraUiStates[i].saveMode, value);
        });
        connect(m_panels[i]->savePathEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, value, m_cameraUiStates[i].saveFormat,
                m_cameraUiStates[i].saveMode);
        });
        connect(m_sidebarSavePathEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, value, m_cameraUiStates[i].saveFormat,
                m_cameraUiStates[i].saveMode);
        });
        connect(m_topSavePathEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, value, m_cameraUiStates[i].saveFormat,
                m_cameraUiStates[i].saveMode);
        });

        auto applySaveFormatChange = [this, i](const QString& value) {
            if (m_saveModeToggleBtn && m_saveModeToggleBtn->isChecked()) {
                for (int other = 0; other < 2; ++other) {
                    updateCameraUiState(other, m_cameraUiStates[other].width, m_cameraUiStates[other].height, m_cameraUiStates[other].fps, m_cameraUiStates[other].exposure,
                        m_cameraUiStates[other].gain, m_cameraUiStates[other].testPattern, m_cameraUiStates[other].savePath, value,
                        m_cameraUiStates[other].saveMode);
                }
            } else {
                updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                    m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath, value,
                    m_cameraUiStates[i].saveMode);
            }
        };

        connect(m_panels[i]->saveFormatCombo, &QComboBox::currentTextChanged, this, applySaveFormatChange);
        connect(m_sidebarSaveFormatCombos[i], &QComboBox::currentTextChanged, this, applySaveFormatChange);
        connect(m_topSaveFormatCombos[i], &QComboBox::currentTextChanged, this, applySaveFormatChange);

        connect(m_panels[i]->saveModeCombo, &QComboBox::currentTextChanged, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, value);
        });
        connect(m_sidebarSaveModeCombos[i], &QComboBox::currentTextChanged, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, value);
        });
        connect(m_topSaveModeCombos[i], &QComboBox::currentTextChanged, this, [this, i](const QString& value) {
            updateCameraUiState(i, m_cameraUiStates[i].width, m_cameraUiStates[i].height, m_cameraUiStates[i].fps, m_cameraUiStates[i].exposure,
                m_cameraUiStates[i].gain, m_cameraUiStates[i].testPattern, m_cameraUiStates[i].savePath,
                m_cameraUiStates[i].saveFormat, value);
        });

        connect(m_panels[i]->tdiModeCombo, &QComboBox::currentTextChanged, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, value, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarTdiModeCombos[i], &QComboBox::currentTextChanged, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, value, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topTdiModeCombos[i], &QComboBox::currentTextChanged, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, value, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->tdiStagesEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, value, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarTdiStagesEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, value, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topTdiStagesEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, value, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->tdiYShiftEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, value,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarTdiYShiftEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, value,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topTdiYShiftEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, value,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->tdiFrameCountEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages,
                m_cameraUiStates[i].tdiYShift, value, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarTdiFrameCountEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages,
                m_cameraUiStates[i].tdiYShift, value, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topTdiFrameCountEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages,
                m_cameraUiStates[i].tdiYShift, value, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY,
                m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->reverseXCheck, &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, value, m_cameraUiStates[i].reverseY, m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarReverseXChecks[i], &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, value, m_cameraUiStates[i].reverseY, m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topReverseXChecks[i], &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, value, m_cameraUiStates[i].reverseY, m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->reverseYCheck, &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, value, m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarReverseYChecks[i], &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, value, m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topReverseYChecks[i], &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, value, m_cameraUiStates[i].stopAfterEnabled, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->stopAfterDurationCheck, &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY, value, m_cameraUiStates[i].saveDuration);
        });
        connect(m_sidebarStopAfterDurationChecks[i], &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY, value, m_cameraUiStates[i].saveDuration);
        });
        connect(m_topStopAfterDurationChecks[i], &QCheckBox::toggled, this, [this, i](bool value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY, value, m_cameraUiStates[i].saveDuration);
        });

        connect(m_panels[i]->saveDurationEdit, &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY, m_cameraUiStates[i].stopAfterEnabled, value);
        });
        connect(m_sidebarSaveDurationEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY, m_cameraUiStates[i].stopAfterEnabled, value);
        });
        connect(m_topSaveDurationEdits[i], &QLineEdit::textEdited, this, [this, i](const QString& value) {
            updateCameraAdvancedState(i, m_cameraUiStates[i].tdiMode, m_cameraUiStates[i].tdiStages, m_cameraUiStates[i].tdiYShift,
                m_cameraUiStates[i].tdiFrameCount, m_cameraUiStates[i].reverseX, m_cameraUiStates[i].reverseY, m_cameraUiStates[i].stopAfterEnabled, value);
        });
        syncCameraStateFromWidgets();

        bindCompactEdit(m_sidebarSaveDurationEdits[i], m_panels[i]->saveDurationEdit);
        bindCompactEdit(m_topSaveDurationEdits[i], m_panels[i]->saveDurationEdit);
        bindCompactEdit(m_panels[i]->saveDurationEdit, m_sidebarSaveDurationEdits[i]);
        bindCompactEdit(m_panels[i]->saveDurationEdit, m_topSaveDurationEdits[i]);

        connect(m_sidebarStopAfterDurationChecks[i], &QCheckBox::toggled,
                m_panels[i]->stopAfterDurationCheck, &QCheckBox::setChecked);
        connect(m_topStopAfterDurationChecks[i], &QCheckBox::toggled,
                m_panels[i]->stopAfterDurationCheck, &QCheckBox::setChecked);
        connect(m_panels[i]->stopAfterDurationCheck, &QCheckBox::toggled,
                m_sidebarStopAfterDurationChecks[i], &QCheckBox::setChecked);
        connect(m_panels[i]->stopAfterDurationCheck, &QCheckBox::toggled,
                m_topStopAfterDurationChecks[i], &QCheckBox::setChecked);

        m_sidebarSavePathEdits[i]->setText(m_cameraUiStates[i].savePath);
        m_sidebarWidthEdits[i]->setText(m_cameraUiStates[i].width);
        m_sidebarHeightEdits[i]->setText(m_cameraUiStates[i].height);
        m_sidebarFpsEdits[i]->setText(m_cameraUiStates[i].fps);
        m_sidebarTestPatternCombos[i]->setCurrentText(m_cameraUiStates[i].testPattern);
        m_sidebarExposureEdits[i]->setText(m_cameraUiStates[i].exposure);
        m_sidebarGainEdits[i]->setText(m_cameraUiStates[i].gain);
        m_sidebarTdiStagesEdits[i]->setText(m_panels[i]->tdiStagesEdit->text());
        m_sidebarTdiYShiftEdits[i]->setText(m_panels[i]->tdiYShiftEdit->text());
        m_sidebarTdiFrameCountEdits[i]->setText(m_panels[i]->tdiFrameCountEdit->text());
        m_sidebarTdiModeCombos[i]->setCurrentIndex(m_panels[i]->tdiModeCombo->currentIndex());
        m_sidebarReverseXChecks[i]->setChecked(m_panels[i]->reverseXCheck->isChecked());
        m_sidebarReverseYChecks[i]->setChecked(m_panels[i]->reverseYCheck->isChecked());
        m_sidebarSaveFormatCombos[i]->setCurrentText(m_cameraUiStates[i].saveFormat);
        if (m_sidebarSaveModeCombos[i]) m_sidebarSaveModeCombos[i]->setCurrentText(m_cameraUiStates[i].saveMode);
        if (m_sidebarSaveDurationEdits[i]) m_sidebarSaveDurationEdits[i]->setText(m_panels[i]->saveDurationEdit->text());
        if (m_topSaveModeCombos[i]) m_topSaveModeCombos[i]->setCurrentText(m_cameraUiStates[i].saveMode);
        if (m_topSaveDurationEdits[i]) m_topSaveDurationEdits[i]->setText(m_panels[i]->saveDurationEdit->text());
        m_topSavePathEdits[i]->setText(m_cameraUiStates[i].savePath);
        m_topWidthEdits[i]->setText(m_cameraUiStates[i].width);
        m_topHeightEdits[i]->setText(m_cameraUiStates[i].height);
        m_topFpsEdits[i]->setText(m_cameraUiStates[i].fps);
        m_topTestPatternCombos[i]->setCurrentText(m_cameraUiStates[i].testPattern);
        m_topExposureEdits[i]->setText(m_cameraUiStates[i].exposure);
        m_topGainEdits[i]->setText(m_cameraUiStates[i].gain);
        m_topTdiStagesEdits[i]->setText(m_panels[i]->tdiStagesEdit->text());
        m_topTdiYShiftEdits[i]->setText(m_panels[i]->tdiYShiftEdit->text());
        m_topTdiFrameCountEdits[i]->setText(m_panels[i]->tdiFrameCountEdit->text());
        m_topTdiModeCombos[i]->setCurrentIndex(m_panels[i]->tdiModeCombo->currentIndex());
        m_topReverseXChecks[i]->setChecked(m_panels[i]->reverseXCheck->isChecked());
        m_topReverseYChecks[i]->setChecked(m_panels[i]->reverseYCheck->isChecked());
        m_topSaveFormatCombos[i]->setCurrentText(m_cameraUiStates[i].saveFormat);
        updateTdiUiVisibility(i);

        m_topControlScrolls[i] = new QScrollArea(m_topControlsHost);
        m_topControlScrolls[i]->setWidgetResizable(true);
        m_topControlScrolls[i]->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_topControlScrolls[i]->setFrameShape(QFrame::NoFrame);
        syncSidebarButtons(i);
    }

    // ── View panels ───────────────────────────────────────────────────
    for (int i = 0; i < 2; ++i)
    {
        QWidget* vw = new QWidget();
        vw->setObjectName("viewWidget");
        QVBoxLayout* vl = new QVBoxLayout(vw);
        vl->setContentsMargins(0,0,0,0);
        vl->setSpacing(0);

        // Thin bar inside the view dock
        QFrame* bar = new QFrame();
        bar->setObjectName("viewBar");
        bar->setFixedHeight(24);
        QHBoxLayout* bl = new QHBoxLayout(bar);
        bl->setContentsMargins(8,0,8,0);
        QLabel* vtitle = new QLabel(QString("CAM %1").arg(i));
        vtitle->setObjectName("viewBarTitle");
        bl->addWidget(vtitle);
        bl->addStretch();
        m_viewSensorTempLabels[i] = new QLabel("S: — °C");
        m_viewSensorTempLabels[i]->setObjectName("sensorTempLabel");
        m_viewSensorTempLabels[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bl->addWidget(m_viewSensorTempLabels[i]);
        bl->addSpacing(8);
        m_viewGrabberTempLabels[i] = new QLabel("G: — °C");
        m_viewGrabberTempLabels[i]->setObjectName("grabberTempLabel");
        m_viewGrabberTempLabels[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bl->addWidget(m_viewGrabberTempLabels[i]);
        bl->addSpacing(12);
        m_viewFpsLabels[i] = new QLabel("— fps");
        m_viewFpsLabels[i]->setObjectName("fpsLabel");
        m_viewFpsLabels[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bl->addWidget(m_viewFpsLabels[i]);
        vl->addWidget(bar);

        m_viewScrolls[i] = new QScrollArea();
        m_viewScrolls[i]->setObjectName("scrollArea");
        m_viewLabels[i]  = new QLabel("No Signal");
        m_viewLabels[i]->setObjectName("imageLabel");
        m_viewLabels[i]->setAlignment(Qt::AlignCenter);
        m_viewScrolls[i]->setWidget(m_viewLabels[i]);
        m_viewScrolls[i]->setWidgetResizable(true);
        if (m_viewScrolls[i]->viewport())
            m_viewScrolls[i]->viewport()->installEventFilter(this);
        vl->addWidget(m_viewScrolls[i], 1);
        
        QPushButton* markBtn = new QPushButton(i == 0 ? " 🎯 MARK CAM1 LINE " : " 🎯 MARK CAM2 LINE ");
        markBtn->setStyleSheet(i == 0 ? "background-color: #d35400; color: white; font-size: 20px; font-weight: bold; padding: 10px; margin: 10px;" : "background-color: #2980b9; color: white; font-size: 20px; font-weight: bold; padding: 10px; margin: 10px;");
        markBtn->setVisible(false);
        vl->addWidget(markBtn);
        if (i == 0) m_markCam1Btn = markBtn;
        else m_markCam2Btn = markBtn;

        m_viewStatusLabels[i] = new QLabel("Disconnected  |  Frame: 0  |  Min: —  |  Max: —");
        m_viewStatusLabels[i]->setObjectName("statusLabel");
        vl->addWidget(m_viewStatusLabels[i]);
        m_viewWidgets[i] = vw;
    }

    // ── Toolbar ───────────────────────────────────────────────────────
    QToolBar* tb = new QToolBar("Main Toolbar", liveTab);
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->setFloatable(false);
    liveTabLayout->insertWidget(0, tb);

    const int topBtnH = 20;
    auto tw = [&](QWidget* w){ tb->addWidget(w); };
    auto makeCapsule = [](const QString& objectName = "capsuleGroup") {
        QFrame* cap = new QFrame();
        cap->setObjectName(objectName);
        QHBoxLayout* lay = new QHBoxLayout(cap);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(3);
        return cap;
    };

    QPushButton* appName = new QPushButton("  ◈  DUAL DISPLAY  ");
    appName->setObjectName("appNameBtn");
    appName->setFixedHeight(topBtnH);
    appName->setToolTip("Click to reset all values to default");
    connect(appName, &QPushButton::clicked, this, &MainWindow::resetAllValuesToDefault);
    tw(appName);

    // Layout Capsule
    QFrame* layoutCap = makeCapsule();
    QHBoxLayout* layoutCapLay = qobject_cast<QHBoxLayout*>(layoutCap->layout());
    QLabel* layoutLabel = new QLabel("Layout:");
    layoutLabel->setObjectName("capsuleLabel");
    layoutCapLay->addWidget(layoutLabel);

    m_layoutCombo = new QComboBox();
    m_layoutCombo->setObjectName("layoutCombo");
    m_layoutCombo->addItem("⬛⬛ Side by Side", static_cast<int>(PresetLayout::SideBySide));
    m_layoutCombo->addItem("⬛/⬛ Top/Bottom", static_cast<int>(PresetLayout::TopBottom));
    m_layoutCombo->addItem("⬜⬛ Dual Control", static_cast<int>(PresetLayout::DualControl));
    m_layoutCombo->addItem("⬒⬒ Controls Top", static_cast<int>(PresetLayout::ControlsTop));
    m_layoutCombo->setFixedWidth(120);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int preset = m_layoutCombo->itemData(index).toInt();
        switch(preset) {
            case static_cast<int>(PresetLayout::SideBySide): applyPresetSideBySide(); break;
            case static_cast<int>(PresetLayout::TopBottom): applyPresetTopBottom(); break;
            case static_cast<int>(PresetLayout::DualControl): applyPresetDualControl(); break;
            case static_cast<int>(PresetLayout::ControlsTop): applyPresetControlsTop(); break;
            default: applyPresetSideBySide(); break;
        }
    });
    layoutCapLay->addWidget(m_layoutCombo);

    QPushButton* resetLayoutBtn = new QPushButton("↺ Reset");
    resetLayoutBtn->setObjectName("resetBtn");
    resetLayoutBtn->setToolTip("Reset all docks to default");
    resetLayoutBtn->setFixedWidth(56);
    resetLayoutBtn->setFixedHeight(topBtnH);
    connect(resetLayoutBtn, &QPushButton::clicked, this, &MainWindow::applyPresetReset);
    layoutCapLay->addWidget(resetLayoutBtn);
    tw(layoutCap);

    // Display Capsule
    QFrame* displayCap = makeCapsule();
    QHBoxLayout* displayCapLay = qobject_cast<QHBoxLayout*>(displayCap->layout());
    QLabel* displayModeLabel = new QLabel("Display:");
    displayModeLabel->setObjectName("capsuleLabel");
    displayCapLay->addWidget(displayModeLabel);

    QLabel* fitLabel = new QLabel("Fit");
    fitLabel->setObjectName("modeSideLabel");
    displayCapLay->addWidget(fitLabel);

    m_displayModeBtn = new DisplayModeSwitch(this);
    m_displayModeBtn->setObjectName("displayModeSwitch");
    m_displayModeBtn->setToolTip("Display mode switch: left is Fit, right is Stretch");
    m_displayModeBtn->setChecked(false);
    connect(m_displayModeBtn, &QPushButton::clicked, this, &MainWindow::onDisplayModeToggled);
    displayCapLay->addWidget(m_displayModeBtn);

    QLabel* stretchLabel = new QLabel("Stretch");
    stretchLabel->setObjectName("modeSideLabel");
    displayCapLay->addWidget(stretchLabel);

    m_pixelStretchCheck = new QCheckBox("Pixel Stretch", this);
    m_pixelStretchCheck->setObjectName("pixelStretchCheck");
    m_pixelStretchCheck->setChecked(false);
    connect(m_pixelStretchCheck, &QCheckBox::toggled, this, &MainWindow::onPixelStretchToggled);
    displayCapLay->addWidget(m_pixelStretchCheck);
    tw(displayCap);

    // Theme Capsule
    QFrame* themeCap = makeCapsule();
    QHBoxLayout* themeCapLay = qobject_cast<QHBoxLayout*>(themeCap->layout());
    QLabel* themeModeLabel = new QLabel("Theme:");
    themeModeLabel->setObjectName("capsuleLabel");
    themeCapLay->addWidget(themeModeLabel);

    QLabel* lightLabel = new QLabel("Light");
    lightLabel->setObjectName("modeSideLabel");
    themeCapLay->addWidget(lightLabel);

    m_darkModeBtn = new DisplayModeSwitch(this);
    m_darkModeBtn->setObjectName("darkModeSwitch");
    m_darkModeBtn->setToolTip("Theme switch: left is Light, right is Dark");
    m_darkModeBtn->setChecked(false);
    connect(m_darkModeBtn, &QPushButton::clicked, this, &MainWindow::onDarkModeToggled);
    themeCapLay->addWidget(m_darkModeBtn);

    QLabel* darkLabel = new QLabel("Dark");
    darkLabel->setObjectName("modeSideLabel");
    themeCapLay->addWidget(darkLabel);
    tw(themeCap);

    // Sync Capsule
    QFrame* syncCap = makeCapsule();
    QHBoxLayout* syncCapLay = qobject_cast<QHBoxLayout*>(syncCap->layout());
    QLabel* dl = new QLabel("⏱ Sync:");
    dl->setObjectName("capsuleLabel");
    syncCapLay->addWidget(dl);

    m_calibModeCombo = new QComboBox();
    m_calibModeCombo->addItems({"Direct (Lines)", "Direct (Time)", "Auto-Detect", "Manual Mark"});
    m_calibModeCombo->setFixedWidth(102);
    syncCapLay->addWidget(m_calibModeCombo);

    m_lineDelaySpin = new QComboBox();
    m_lineDelaySpin->setEditable(true);
    m_lineDelaySpin->setValidator(new QIntValidator(-999999, 999999, m_lineDelaySpin));
    m_lineDelaySpin->setFixedWidth(70);
    setLineDelayValue(0);

    m_timeDelaySpin = new QDoubleSpinBox();
    m_timeDelaySpin->setRange(-3600.0, 3600.0);
    m_timeDelaySpin->setSingleStep(0.1);
    m_timeDelaySpin->setDecimals(3);
    m_timeDelaySpin->setSuffix(" s");
    m_timeDelaySpin->setFixedWidth(75);

    m_startCalibrationBtn = new QPushButton("Start");
    m_startCalibrationBtn->setObjectName("presetBtn");
    m_startCalibrationBtn->setFixedWidth(50);
    m_startCalibrationBtn->setFixedHeight(topBtnH);

    m_syncStackedWidget = new QStackedWidget();
    m_syncStackedWidget->addWidget(m_lineDelaySpin);       // index 0
    m_syncStackedWidget->addWidget(m_timeDelaySpin);       // index 1
    m_syncStackedWidget->addWidget(m_startCalibrationBtn); // index 2
    syncCapLay->addWidget(m_syncStackedWidget);

    connect(m_calibModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (idx == 0) { // Direct Lines
            m_syncStackedWidget->setCurrentIndex(0);
            if (m_calibrationEngine && m_calibrationEngine->isCalibrating()) onStartCalibrationClicked();
        } else if (idx == 1) { // Direct Time
            m_syncStackedWidget->setCurrentIndex(1);
            if (m_calibrationEngine && m_calibrationEngine->isCalibrating()) onStartCalibrationClicked();
        } else { // Auto or Manual
            m_syncStackedWidget->setCurrentIndex(2);
        }
    });

    connect(m_startCalibrationBtn, &QPushButton::clicked, this, &MainWindow::onStartCalibrationClicked);
    connect(m_markCam1Btn, &QPushButton::clicked, this, &MainWindow::onMarkCam1Clicked);
    connect(m_markCam2Btn, &QPushButton::clicked, this, &MainWindow::onMarkCam2Clicked);
    tw(syncCap);

    // Camera Stream Capsule (Detect, Connect All [GREEN], Start All)
    QFrame* streamCap = makeCapsule();
    QHBoxLayout* streamCapLay = qobject_cast<QHBoxLayout*>(streamCap->layout());

    m_detectBtn = new QPushButton(" ⟳ Detect ");
    m_connectAllBtn = new QPushButton(" ⚡ Connect All ");
    m_startAllBtn   = new QPushButton(" ▶ Start All ");

    m_detectBtn->setObjectName("detectBtn");
    m_connectAllBtn->setObjectName("connectAllBtn");
    m_startAllBtn->setObjectName("startAllBtn");

    m_detectBtn->setFixedWidth(74);
    m_connectAllBtn->setFixedWidth(104);
    m_startAllBtn->setFixedWidth(84);

    m_detectBtn->setFixedHeight(topBtnH);
    m_connectAllBtn->setFixedHeight(topBtnH);
    m_startAllBtn->setFixedHeight(topBtnH);

    connect(m_detectBtn, &QPushButton::clicked, this, [this]{ detectCameras(); });
    connect(m_connectAllBtn, &QPushButton::clicked, this, &MainWindow::onConnectAllClicked);
    connect(m_startAllBtn,   &QPushButton::clicked, this, &MainWindow::onStartAllClicked);

    streamCapLay->addWidget(m_detectBtn);
    streamCapLay->addWidget(m_connectAllBtn);
    streamCapLay->addWidget(m_startAllBtn);
    tw(streamCap);

    // Save Capsule (Mode toggle, Save All)
    QFrame* saveCap = makeCapsule();
    QHBoxLayout* saveCapLay = qobject_cast<QHBoxLayout*>(saveCap->layout());

    m_saveModeToggleBtn = new QPushButton(" 📂 Mode: Separate ");
    m_saveModeToggleBtn->setObjectName("presetBtn");
    m_saveModeToggleBtn->setCheckable(true);
    m_saveModeToggleBtn->setChecked(false);
    m_saveModeToggleBtn->setToolTip("Toggle between Separate Files mode and Merged Single File mode");
    m_saveModeToggleBtn->setFixedWidth(118);
    m_saveModeToggleBtn->setFixedHeight(topBtnH);
    connect(m_saveModeToggleBtn, &QPushButton::toggled, this, [this](bool checked){
        m_saveModeToggleBtn->setText(checked ? " 🗜 Mode: Merged " : " 📂 Mode: Separate ");
        m_saveModeToggleBtn->setProperty("merged", checked);
        m_saveModeToggleBtn->style()->unpolish(m_saveModeToggleBtn);
        m_saveModeToggleBtn->style()->polish(m_saveModeToggleBtn);
        syncSidebarButtons(0);
    });

    m_saveAllBtn = new QPushButton(" ⬇ Save All ");
    m_saveAllBtn->setObjectName("saveAllBtn");
    m_saveAllBtn->setFixedWidth(88);
    m_saveAllBtn->setFixedHeight(topBtnH);
    connect(m_saveAllBtn, &QPushButton::clicked, this, &MainWindow::onSaveAllClicked);

    saveCapLay->addWidget(m_saveModeToggleBtn);
    saveCapLay->addWidget(m_saveAllBtn);
    tw(saveCap);

    // View Capsule (Hide Controls, Float Monitor 2)
    QFrame* viewCap = makeCapsule();
    QHBoxLayout* viewCapLay = qobject_cast<QHBoxLayout*>(viewCap->layout());

    m_toggleControlsBtn = new QPushButton(" Hide Controls ");
    m_toggleControlsBtn->setObjectName("presetBtn");
    m_toggleControlsBtn->setFixedWidth(94);
    m_toggleControlsBtn->setFixedHeight(topBtnH);
    connect(m_toggleControlsBtn, &QPushButton::clicked, this, &MainWindow::onToggleControlsClicked);

    m_floatCam1Btn = new QPushButton(" ⧉ Cam1 → Monitor 2 ");
    m_floatCam1Btn->setObjectName("floatBtn");
    m_floatCam1Btn->setFixedWidth(124);
    m_floatCam1Btn->setFixedHeight(topBtnH);
    m_floatCam1Btn->setToolTip("Float Camera 1 view dock to second monitor");
    m_floatCam1Btn->setFocusPolicy(Qt::NoFocus);
    m_floatCam1Btn->installEventFilter(this);

    viewCapLay->addWidget(m_toggleControlsBtn);
    viewCapLay->addWidget(m_floatCam1Btn);
    tw(viewCap);

    // Spacer
    QWidget* sp = new QWidget(); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tw(sp);
}

void MainWindow::appendLogLine(const QString& line)
{
    if (!m_logTerminal) return;
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString formattedLine = QString("[%1] %2").arg(ts, line);

    const bool isTelemetry = line.contains("Sensor Temp:") || line.contains("Grabber Temp:") || line.contains("Grabber instant FPS:");
    if (!isTelemetry)
        m_logTerminal->appendPlainText(formattedLine);

    if (m_captureController) {
        m_captureController->appendTelemetry(formattedLine);
    }
}



// ── Preset layouts ────────────────────────────────────────────────────
void MainWindow::resetControlLayout()
{
    for (int i = m_controlTabs ? m_controlTabs->count() - 1 : -1; i >= 0; --i)
        m_controlTabs->removeTab(i);
    if (m_controlTabs) m_controlTabs->hide();
    while (m_topControlsLayout && m_topControlsLayout->count() > 0)
    {
        QLayoutItem* item = m_topControlsLayout->takeAt(0);
        delete item;
    }
    if (m_topControlsHost) m_topControlsHost->hide();
    if (m_topOperatorPanel) m_topOperatorPanel->hide();
    while (m_sidebarLayout && m_sidebarLayout->count() > 0)
    {
        QLayoutItem* item = m_sidebarLayout->takeAt(0);
        delete item;
    }
    if (m_operatorPanel)
    {
        m_operatorPanel->setParent(m_sidebarContent);
        m_operatorPanel->show();
    }
    for (auto* panel : m_panels)
    {
        if (!panel) continue;
        panel->setParent(m_panelParking);
        panel->hide();
    }
}

void MainWindow::resetViewLayout()
{
    for (int i = m_viewTabs ? m_viewTabs->count() - 1 : -1; i >= 0; --i)
        m_viewTabs->removeTab(i);
    if (m_viewTabs) m_viewTabs->hide();
    if (m_viewSplitter) m_viewSplitter->hide();
    if (m_controlsTopSplitter) m_controlsTopSplitter->hide();
    while (m_viewHostLayout && m_viewHostLayout->count() > 0)
    {
        QLayoutItem* item = m_viewHostLayout->takeAt(0);
        delete item;
    }
    while (m_controlsTopSplitter && m_controlsTopSplitter->count() > 0)
    {
        QWidget* child = m_controlsTopSplitter->widget(0);
        child->setParent(m_viewHost);
    }
    while (m_viewSplitter && m_viewSplitter->count() > 0)
    {
        QWidget* child = m_viewSplitter->widget(0);
        child->setParent(nullptr);
    }
    for (auto* vw : m_viewWidgets)
    {
        if (!vw) continue;
        vw->setParent(m_viewHost);
        vw->show();
    }
    if (m_detachedView && m_detachedView->isVisible())
    {
        m_detachedView->close();
        m_panels[1]->isDetached = false;
    }
}

void MainWindow::applyPresetSideBySide()
{
    m_currentPreset = PresetLayout::SideBySide;
    resetControlLayout();
    resetViewLayout();
    m_sidebarScroll->setMinimumWidth(260);
    for (int i = 0; i < 2; ++i)
    {
        if (m_panels[i]->sensorTempLabel)  m_panels[i]->sensorTempLabel->setVisible(true);
        if (m_panels[i]->grabberTempLabel) m_panels[i]->grabberTempLabel->setVisible(true);
    }
    m_sidebarScroll->show();
    m_sidebarLayout->addWidget(m_operatorPanel);
    m_operatorPanel->show();
    m_sidebarLayout->addStretch(1);
    m_viewSplitter->setOrientation(Qt::Horizontal);
    m_viewSplitter->addWidget(m_viewWidgets[0]);
    m_viewSplitter->addWidget(m_viewWidgets[1]);
    m_viewHostLayout->addWidget(m_viewSplitter);
    m_viewSplitter->show();
    applyControlVisibility();
    m_viewSplitter->setSizes({810, 810});
    QTimer::singleShot(0, this, &MainWindow::refreshAllDisplays);
}

void MainWindow::applyPresetTopBottom()
{
    m_currentPreset = PresetLayout::TopBottom;
    resetControlLayout();
    resetViewLayout();
    m_sidebarScroll->setMinimumWidth(260);
    for (int i = 0; i < 2; ++i)
    {
        if (m_panels[i]->sensorTempLabel)  m_panels[i]->sensorTempLabel->setVisible(true);
        if (m_panels[i]->grabberTempLabel) m_panels[i]->grabberTempLabel->setVisible(true);
    }
    m_sidebarScroll->show();
    m_sidebarLayout->addWidget(m_operatorPanel);
    m_operatorPanel->show();
    m_sidebarLayout->addStretch(1);
    m_viewSplitter->setOrientation(Qt::Vertical);
    m_viewSplitter->addWidget(m_viewWidgets[0]);
    m_viewSplitter->addWidget(m_viewWidgets[1]);
    m_viewHostLayout->addWidget(m_viewSplitter);
    m_viewSplitter->show();
    applyControlVisibility();
    m_viewSplitter->setSizes({520, 520});
    QTimer::singleShot(0, this, &MainWindow::refreshAllDisplays);
}

void MainWindow::applyPresetDualControl()
{
    m_currentPreset = PresetLayout::DualControl;
    resetControlLayout();
    resetViewLayout();
    m_sidebarScroll->setMinimumWidth(440);
    m_sidebarScroll->show();
    for (int i = 0; i < 2; ++i)
    {
        m_panels[i]->setParent(m_sidebarContent);
        // Keep dual-control tabs compact and visually stable.
        if (m_panels[i]->sensorTempLabel)  m_panels[i]->sensorTempLabel->setVisible(false);
        if (m_panels[i]->grabberTempLabel) m_panels[i]->grabberTempLabel->setVisible(false);
        m_panels[i]->show();
        m_controlTabs->addTab(m_panels[i], QString("CAM %1").arg(i));
    }
    m_sidebarLayout->addWidget(m_controlTabs);
    m_controlTabs->show();

    m_viewTabs->addTab(m_viewWidgets[0], "CAM 0");
    m_viewTabs->addTab(m_viewWidgets[1], "CAM 1");
    m_viewHostLayout->addWidget(m_viewTabs);
    m_viewTabs->show();

    QObject::disconnect(m_controlTabs, nullptr, this, nullptr);
    QObject::disconnect(m_viewTabs, nullptr, this, nullptr);
    connect(m_controlTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0 && m_viewTabs->currentIndex() != index)
            m_viewTabs->setCurrentIndex(index);
    });
    connect(m_viewTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0 && m_controlTabs->currentIndex() != index)
            m_controlTabs->setCurrentIndex(index);
    });
    m_controlTabs->setCurrentIndex(0);
    m_viewTabs->setCurrentIndex(0);

    applyControlVisibility();
    QTimer::singleShot(0, this, &MainWindow::refreshAllDisplays);
}

void MainWindow::applyPresetControlsTop()
{
    m_currentPreset = PresetLayout::ControlsTop;
    resetControlLayout();
    resetViewLayout();
    for (int i = 0; i < 2; ++i)
    {
        if (m_panels[i]->sensorTempLabel)  m_panels[i]->sensorTempLabel->setVisible(true);
        if (m_panels[i]->grabberTempLabel) m_panels[i]->grabberTempLabel->setVisible(true);
    }
    m_sidebarScroll->hide();
    m_topControlsLayout->addWidget(m_topOperatorPanel, 1);
    m_topOperatorPanel->show();
    m_topControlsHost->show();

    m_viewSplitter->setOrientation(Qt::Horizontal);
    m_viewSplitter->addWidget(m_viewWidgets[0]);
    m_viewSplitter->addWidget(m_viewWidgets[1]);
    m_controlsTopSplitter->addWidget(m_topControlsHost);
    m_controlsTopSplitter->addWidget(m_viewSplitter);
    m_viewHostLayout->addWidget(m_controlsTopSplitter);
    m_controlsTopSplitter->show();
    m_viewSplitter->show();

    applyControlVisibility();
    m_viewSplitter->setSizes({960, 960});
    QTimer::singleShot(0, this, &MainWindow::refreshAllDisplays);
}

void MainWindow::applyPresetReset() { applyPresetSideBySide(); }

// ── Float Cam1 to monitor 2 ───────────────────────────────────────────
void MainWindow::onFloatCam1ToMonitor2()
{
    if (!m_detachedView)
    {
        m_detachedView = new DetachedView();
        connect(m_detachedView, &DetachedView::requestReattach, this, [this]{
            m_panels[1]->isDetached = false;
            if (m_viewWidgets[1]) m_viewWidgets[1]->show();
            m_detachedView->hide();
            refreshDisplay(1);
        });
        connect(m_detachedView, &DetachedView::closed, this, [this]{
            m_panels[1]->isDetached = false;
            if (m_viewWidgets[1]) m_viewWidgets[1]->show();
            refreshDisplay(1);
        });
    }

    m_panels[1]->isDetached = true;
    if (m_viewWidgets[1]) m_viewWidgets[1]->hide();
    m_detachedView->moveToSecondMonitor(size());
    m_detachedView->show();
    if (!m_panels[1]->lastPixmap.isNull())
        m_detachedView->updateFrame(m_panels[1]->lastPixmap,
                                    m_panels[1]->lastFrameId,
                                    m_panels[1]->lastBrightestPixel,
                                    m_panels[1]->lastFps);
    statusBar()->showMessage("Camera 1 moved to detached monitor view.", 5000);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_floatCam1Btn)
    {
        switch (event->type())
        {
        case QEvent::MouseButtonRelease:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_floatCam1Btn->rect().contains(mouseEvent->pos()))
            {
                onFloatCam1ToMonitor2();
                return true;
            }
            break;
        }
        case QEvent::KeyPress:
        case QEvent::Shortcut:
        case QEvent::ShortcutOverride:
            return true;
        default:
            break;
        }
    }

    for (int i = 0; i < 2; ++i)
    {
        QScrollArea* sa = m_viewScrolls[i];
        QScrollArea* pa = m_panels[i] ? m_panels[i]->scrollArea : nullptr;
        QWidget* v1 = sa ? sa->viewport() : nullptr;
        QWidget* v2 = pa ? pa->viewport() : nullptr;

        if ((v1 && watched == v1) || (v2 && watched == v2))
        {
            if (event->type() == QEvent::Wheel)
            {
                QWheelEvent* we = static_cast<QWheelEvent*>(event);
                int delta = we->angleDelta().y();
                if (delta != 0)
                {
                    QScrollArea* activeScroll = (watched == v1) ? sa : pa;

                    double factor = 1.15;
                    if (delta < 0) factor = 1.0 / 1.15;

                    double oldZoom = m_zoomFactors[i];
                    double newZoom = std::clamp(oldZoom * factor, 0.1, 10.0);

                    if (newZoom != oldZoom)
                    {
                        m_zoomFactors[i] = newZoom;

                        QPoint pos = we->position().toPoint();
                        int hVal = activeScroll ? activeScroll->horizontalScrollBar()->value() : 0;
                        int vVal = activeScroll ? activeScroll->verticalScrollBar()->value() : 0;

                        refreshDisplay(i);

                        if (activeScroll) {
                            double zoomRatio = newZoom / oldZoom;
                            int newH = static_cast<int>((hVal + pos.x()) * zoomRatio - pos.x());
                            int newV = static_cast<int>((vVal + pos.y()) * zoomRatio - pos.y());

                            activeScroll->horizontalScrollBar()->setValue(newH);
                            activeScroll->verticalScrollBar()->setValue(newV);
                        }
                    }
                    return true;
                }
            }
            else if (event->type() == QEvent::MouseButtonDblClick)
            {
                m_zoomFactors[i] = 1.0;
                refreshDisplay(i);
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

// ── Display refresh ───────────────────────────────────────────────────
void MainWindow::refreshDisplay(int idx)
{
    if (!m_lastFrames[idx]) return;
    const FramePacket& pkt = *m_lastFrames[idx];
    const auto pixels = CameraWorker::displayPixelsWithoutMetadata(pkt);
    const int displayHeight = (pkt.rawData.empty() || pkt.height <= 1) ? pkt.height : (pkt.height - 1);
    if (pkt.width <= 0 || displayHeight <= 0) return;
    const size_t expectedBytes = static_cast<size_t>(pkt.width) * static_cast<size_t>(displayHeight);
    if (pixels.size() < expectedBytes) return;

    const uint8_t* src = pixels.data();
    std::vector<uint8_t> buf;
    if (m_pixelStretch)
    {
        auto mn = *std::min_element(pixels.begin(), pixels.end());
        auto mx = *std::max_element(pixels.begin(), pixels.end());
        if (mn != mx)
        {
            buf.resize(pixels.size());
            float s = 255.0f / (mx - mn);
            for (size_t i = 0; i < pixels.size(); ++i)
                buf[i] = (uint8_t)((pixels[i] - mn) * s);
            src = buf.data();
        }
    }

    QImage img(src, pkt.width, displayHeight, pkt.width, QImage::Format_Grayscale8);
    QPixmap px;
    double zoom = m_zoomFactors[idx];
    if (m_fitToScreen && zoom == 1.0) {
        QSize targetSize;
        if (m_viewScrolls[idx] && m_viewScrolls[idx]->viewport()) {
            targetSize = m_viewScrolls[idx]->viewport()->size();
        }

        if (targetSize.isValid() && !targetSize.isEmpty()) {
            QImage scaledImg = img.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
            px = QPixmap::fromImage(scaledImg);
        } else {
            px = QPixmap::fromImage(img);
        }
    } else {
        QSize baseSize;
        if (m_fitToScreen && m_viewScrolls[idx] && m_viewScrolls[idx]->viewport() && !m_viewScrolls[idx]->viewport()->size().isEmpty()) {
            baseSize = img.scaled(m_viewScrolls[idx]->viewport()->size(), Qt::KeepAspectRatio, Qt::FastTransformation).size();
        } else {
            baseSize = QSize(pkt.width, displayHeight);
        }
        QSize targetSize = baseSize * zoom;
        if (targetSize.width() > 0 && targetSize.height() > 0) {
            QImage scaledImg = img.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
            px = QPixmap::fromImage(scaledImg);
        } else {
            px = QPixmap::fromImage(img);
        }
    }
    
    if (m_calibrationEngine && m_calibrationEngine->isCalibrating()) {
        QPainter p(&px);
        p.setPen(QPen(Qt::red, 3));
        p.drawLine(0, px.height() / 2, px.width(), px.height() / 2);
    }
    
    if (m_viewLabels[idx]) {
        m_viewLabels[idx]->setPixmap(px);
        m_viewLabels[idx]->adjustSize();
    }
    if (m_panels[idx]) {
        m_panels[idx]->lastPixmap = px;
        m_panels[idx]->updateFrameInfo(pkt, pixels);
        if (m_panels[idx]->imageLabel) {
            m_panels[idx]->imageLabel->setPixmap(px);
            m_panels[idx]->imageLabel->adjustSize();
        }
    }
    if (idx == 1 && m_panels[1]->isDetached && m_detachedView)
        m_detachedView->updateFrame(m_panels[1]->lastPixmap,
                                    m_panels[1]->lastFrameId,
                                    m_panels[1]->lastBrightestPixel,
                                    m_panels[1]->lastFps);
}

void MainWindow::refreshAllDisplays()
{
    for (int i = 0; i < 2; ++i) refreshDisplay(i);
    emit displaySettingsChanged();
}

void MainWindow::syncSidebarButtons(int idx)
{
    if (idx < 0 || idx > 1) return;
    applyCameraUiStateToWidgets(idx);

    const bool isConn = m_connected[idx];
    const bool isStream = m_streaming[idx];
    const bool isMergedMode = (m_saveModeToggleBtn && m_saveModeToggleBtn->isChecked());
    const bool isSaving = isMergedMode ? isMergeSaving() : m_saving[idx];
    if (m_panels[idx]) {
        m_panels[idx]->setSaving(isSaving);
        m_panels[idx]->saveBtn->setEnabled(isStream);
    }
    const bool allowGeometryChange = !isStream;
    const bool allowSourceChange = !isConn;

    auto setWidgetEnabled = [](QWidget* w, bool en) {
        if (w && w->isEnabled() != en) {
            w->setEnabled(en);
        }
    };

    // Parameters that cannot be modified during streaming are greyed out (like Vision Studio)
    setWidgetEnabled(m_sidebarWidthEdits[idx], allowGeometryChange);
    setWidgetEnabled(m_sidebarHeightEdits[idx], allowGeometryChange);
    setWidgetEnabled(m_topWidthEdits[idx], allowGeometryChange);
    setWidgetEnabled(m_topHeightEdits[idx], allowGeometryChange);
    if (m_panels[idx]) {
        setWidgetEnabled(m_panels[idx]->widthEdit, allowGeometryChange);
        setWidgetEnabled(m_panels[idx]->heightEdit, allowGeometryChange);
        setWidgetEnabled(m_panels[idx]->tdiModeCombo, allowGeometryChange);
        setWidgetEnabled(m_panels[idx]->tdiStagesEdit, allowGeometryChange);
        setWidgetEnabled(m_panels[idx]->tdiYShiftEdit,
            allowGeometryChange && m_cameraUiStates[idx].tdiMode == "TDI");
        setWidgetEnabled(m_panels[idx]->saveFormatCombo, !isSaving);
        setWidgetEnabled(m_panels[idx]->saveModeCombo, !isSaving);
        setWidgetEnabled(m_panels[idx]->connectBtn, !isStream);
        setWidgetEnabled(m_panels[idx]->startBtn, isConn);
        setWidgetEnabled(m_panels[idx]->savePathEdit, !isSaving);
        setWidgetEnabled(m_panels[idx]->browseRawBtn, !isSaving);
        setWidgetEnabled(m_panels[idx]->stopAfterDurationCheck, !isSaving);
        setWidgetEnabled(m_panels[idx]->saveDurationEdit, !isSaving && m_panels[idx]->stopAfterDurationCheck && m_panels[idx]->stopAfterDurationCheck->isChecked());
    }

    setWidgetEnabled(m_sidebarSettingsBtns[idx], allowGeometryChange);
    setWidgetEnabled(m_sidebarBinningBtns[idx], allowGeometryChange);
    setWidgetEnabled(m_topSettingsBtns[idx], allowGeometryChange);
    setWidgetEnabled(m_topBinningBtns[idx], allowGeometryChange);

    const int otherIdx = 1 - idx;
    const bool otherSaving = isMergedMode ? isMergeSaving() : m_saving[otherIdx];
    setWidgetEnabled(m_sidebarCopySettingsBtns[idx], !otherSaving);
    setWidgetEnabled(m_topCopySettingsBtns[idx], !otherSaving);
    if (m_panels[idx]) {
        setWidgetEnabled(m_panels[idx]->copySettingsBtn, !otherSaving);
    }

    setWidgetEnabled(m_sidebarTdiModeCombos[idx], allowGeometryChange);
    setWidgetEnabled(m_sidebarTdiStagesEdits[idx],
        allowGeometryChange && m_cameraUiStates[idx].tdiMode == "TDI");
    setWidgetEnabled(m_sidebarTdiYShiftEdits[idx],
        allowGeometryChange && m_cameraUiStates[idx].tdiMode == "TDI");
    setWidgetEnabled(m_sidebarTdiFrameCountEdits[idx],
        allowGeometryChange && m_cameraUiStates[idx].tdiMode == "Record");

    setWidgetEnabled(m_topTdiModeCombos[idx], allowGeometryChange);
    setWidgetEnabled(m_topTdiStagesEdits[idx],
        allowGeometryChange && m_cameraUiStates[idx].tdiMode == "TDI");
    setWidgetEnabled(m_topTdiYShiftEdits[idx],
        allowGeometryChange && m_cameraUiStates[idx].tdiMode == "TDI");
    setWidgetEnabled(m_topTdiFrameCountEdits[idx],
        allowGeometryChange && m_cameraUiStates[idx].tdiMode == "Record");

    updateTdiUiVisibility(idx);

    setWidgetEnabled(m_sidebarSourceCombos[idx], allowSourceChange);
    setWidgetEnabled(m_topSourceCombos[idx], allowSourceChange);

    setWidgetEnabled(m_sidebarSaveBtns[idx], isStream);
    setWidgetEnabled(m_topSaveBtns[idx], isStream);
    setWidgetEnabled(m_sidebarSaveFormatCombos[idx], !isSaving);
    setWidgetEnabled(m_sidebarSaveModeCombos[idx], !isSaving);
    setWidgetEnabled(m_topSaveFormatCombos[idx], !isSaving);
    setWidgetEnabled(m_topSaveModeCombos[idx], !isSaving);

    setWidgetEnabled(m_sidebarSavePathEdits[idx], !isSaving);
    setWidgetEnabled(m_topSavePathEdits[idx], !isSaving);
    setWidgetEnabled(m_sidebarSaveDurationEdits[idx], !isSaving);
    setWidgetEnabled(m_topSaveDurationEdits[idx], !isSaving);

    if (m_sidebarConnectBtns[idx]) m_sidebarConnectBtns[idx]->setText(m_panels[idx]->connectBtn->text());
    if (m_sidebarStartBtns[idx])   m_sidebarStartBtns[idx]->setText(m_panels[idx]->startBtn->text());
    if (m_sidebarSaveBtns[idx])    m_sidebarSaveBtns[idx]->setText(m_panels[idx]->saveBtn->text());

    if (m_sidebarConnectBtns[idx]) {
        m_sidebarConnectBtns[idx]->setEnabled(!isStream);
        m_sidebarConnectBtns[idx]->setProperty("connected", m_panels[idx]->connectBtn->property("connected"));
        m_sidebarConnectBtns[idx]->style()->unpolish(m_sidebarConnectBtns[idx]);
        m_sidebarConnectBtns[idx]->style()->polish(m_sidebarConnectBtns[idx]);
    }
    if (m_sidebarStartBtns[idx]) {
        m_sidebarStartBtns[idx]->setEnabled(isConn);
        m_sidebarStartBtns[idx]->setProperty("streaming", m_panels[idx]->startBtn->property("streaming"));
        m_sidebarStartBtns[idx]->style()->unpolish(m_sidebarStartBtns[idx]);
        m_sidebarStartBtns[idx]->style()->polish(m_sidebarStartBtns[idx]);
    }
    if (m_sidebarSaveBtns[idx]) {
        m_sidebarSaveBtns[idx]->setProperty("saving", m_panels[idx]->saveBtn->property("saving"));
        m_sidebarSaveBtns[idx]->style()->unpolish(m_sidebarSaveBtns[idx]);
        m_sidebarSaveBtns[idx]->style()->polish(m_sidebarSaveBtns[idx]);
    }

    if (m_sidebarStateLabels[idx]) {
        const QString state = m_connected[idx]
            ? (m_streaming[idx] ? "Streaming" : "Connected")
            : "Disconnected";
        m_sidebarStateLabels[idx]->setText(QString("%1 | %2").arg(state, m_panels[idx]->fpsLabel->text()));
    }
    if (m_sidebarWidthEdits[idx])    m_sidebarWidthEdits[idx]->setText(m_cameraUiStates[idx].width);
    if (m_sidebarHeightEdits[idx])   m_sidebarHeightEdits[idx]->setText(m_cameraUiStates[idx].height);
    if (m_sidebarFpsEdits[idx])      m_sidebarFpsEdits[idx]->setText(m_cameraUiStates[idx].fps);
    if (m_sidebarTestPatternCombos[idx]) m_sidebarTestPatternCombos[idx]->setCurrentText(m_cameraUiStates[idx].testPattern);
    if (m_sidebarRawEdits[idx])      m_sidebarRawEdits[idx]->setText(m_cameraUiStates[idx].savePath);
    if (m_sidebarSavePathEdits[idx]) m_sidebarSavePathEdits[idx]->setText(m_cameraUiStates[idx].savePath);
    if (m_sidebarExposureEdits[idx]) m_sidebarExposureEdits[idx]->setText(m_cameraUiStates[idx].exposure);
    if (m_sidebarGainEdits[idx])     m_sidebarGainEdits[idx]->setText(m_cameraUiStates[idx].gain);
    if (m_sidebarSaveFormatCombos[idx]) m_sidebarSaveFormatCombos[idx]->setCurrentText(m_cameraUiStates[idx].saveFormat);
    if (m_sidebarSaveModeCombos[idx])   m_sidebarSaveModeCombos[idx]->setCurrentText(m_cameraUiStates[idx].saveMode);
    if (m_sidebarSaveDurationEdits[idx]) m_sidebarSaveDurationEdits[idx]->setText(m_cameraUiStates[idx].saveDuration);

    if (m_topConnectBtns[idx]) m_topConnectBtns[idx]->setText(m_panels[idx]->connectBtn->text());
    if (m_topStartBtns[idx])   m_topStartBtns[idx]->setText(m_panels[idx]->startBtn->text());
    if (m_topSaveBtns[idx])    m_topSaveBtns[idx]->setText(m_panels[idx]->saveBtn->text());
    if (m_topConnectBtns[idx]) {
        m_topConnectBtns[idx]->setEnabled(!isStream);
        m_topConnectBtns[idx]->setProperty("connected", m_panels[idx]->connectBtn->property("connected"));
        m_topConnectBtns[idx]->style()->unpolish(m_topConnectBtns[idx]);
        m_topConnectBtns[idx]->style()->polish(m_topConnectBtns[idx]);
    }
    if (m_topStartBtns[idx]) {
        m_topStartBtns[idx]->setEnabled(isConn);
        m_topStartBtns[idx]->setProperty("streaming", m_panels[idx]->startBtn->property("streaming"));
        m_topStartBtns[idx]->style()->unpolish(m_topStartBtns[idx]);
        m_topStartBtns[idx]->style()->polish(m_topStartBtns[idx]);
    }
    if (m_topSaveBtns[idx]) {
        m_topSaveBtns[idx]->setProperty("saving", m_panels[idx]->saveBtn->property("saving"));
        m_topSaveBtns[idx]->style()->unpolish(m_topSaveBtns[idx]);
        m_topSaveBtns[idx]->style()->polish(m_topSaveBtns[idx]);
    }
    if (m_topStateLabels[idx]) {
        const QString state = m_connected[idx]
            ? (m_streaming[idx] ? "Streaming" : "Connected")
            : "Disconnected";
        m_topStateLabels[idx]->setText(QString("%1 | %2").arg(state, m_panels[idx]->fpsLabel->text()));
    }
    if (m_topWidthEdits[idx])    m_topWidthEdits[idx]->setText(m_cameraUiStates[idx].width);
    if (m_topHeightEdits[idx])   m_topHeightEdits[idx]->setText(m_cameraUiStates[idx].height);
    if (m_topFpsEdits[idx])      m_topFpsEdits[idx]->setText(m_cameraUiStates[idx].fps);
    if (m_topTestPatternCombos[idx]) m_topTestPatternCombos[idx]->setCurrentText(m_cameraUiStates[idx].testPattern);
    if (m_topRawEdits[idx])      m_topRawEdits[idx]->setText(m_cameraUiStates[idx].savePath);
    if (m_topSavePathEdits[idx]) m_topSavePathEdits[idx]->setText(m_cameraUiStates[idx].savePath);
    if (m_topExposureEdits[idx]) m_topExposureEdits[idx]->setText(m_cameraUiStates[idx].exposure);
    if (m_topGainEdits[idx])     m_topGainEdits[idx]->setText(m_cameraUiStates[idx].gain);
    if (m_topSaveFormatCombos[idx]) m_topSaveFormatCombos[idx]->setCurrentText(m_cameraUiStates[idx].saveFormat);
    if (m_topSaveModeCombos[idx])   m_topSaveModeCombos[idx]->setCurrentText(m_cameraUiStates[idx].saveMode);
    if (m_topSaveDurationEdits[idx]) m_topSaveDurationEdits[idx]->setText(m_cameraUiStates[idx].saveDuration);

    bool allConnected = m_connected[0] && m_connected[1];
    bool anyStreaming = m_streaming[0] || m_streaming[1];
    if (m_connectAllBtn) {
        m_connectAllBtn->setEnabled(!anyStreaming);
        m_connectAllBtn->setText(allConnected ? " ✖ Disconnect All " : " ⚡ Connect All ");
        m_connectAllBtn->setProperty("connected", allConnected);
        m_connectAllBtn->style()->unpolish(m_connectAllBtn);
        m_connectAllBtn->style()->polish(m_connectAllBtn);
    }
    if (m_detectBtn) {
        m_detectBtn->setEnabled(!m_connected[0] && !m_connected[1]);
    }
    if (m_saveModeToggleBtn) {
        const bool anySaving = m_saving[0] || m_saving[1] || isMergeSaving();
        m_saveModeToggleBtn->setEnabled(!anySaving);
    }
    if (m_startAllBtn) {
        m_startAllBtn->setText(anyStreaming ? " ⏹ Stop All " : " ▶ Start All ");
        m_startAllBtn->setProperty("streaming", anyStreaming);
        m_startAllBtn->style()->unpolish(m_startAllBtn);
        m_startAllBtn->style()->polish(m_startAllBtn);
    }
    bool anySaving = isMergedMode ? isMergeSaving() : (m_saving[0] || m_saving[1]);

    if (m_saveAllBtn) {
        m_saveAllBtn->setText(anySaving ? " ⏹ Stop All Saves " : " ⬇ Save All ");
        m_saveAllBtn->setProperty("saving", anySaving);
        m_saveAllBtn->style()->unpolish(m_saveAllBtn);
        m_saveAllBtn->style()->polish(m_saveAllBtn);
    }
}

void MainWindow::openRegionSettingsDialog(int idx)
{
    if (idx < 0 || idx > 1 || !m_panels[idx] || m_streaming[idx]) return;

    static const int defOffsets[7] = {0, 900, 2000, 3000, 4000, 5000, 6000};

    QDialog dlg(this);
    dlg.setWindowTitle(QString("CAM %1 Region Modes").arg(idx));
    dlg.resize(420, 520);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    QLabel* note = new QLabel("Configure region modes (On/Off) and vertical offsets for this camera.", &dlg);
    note->setWordWrap(true);
    layout->addWidget(note);

    QGroupBox* regionBox = new QGroupBox("Region Modes & Offsets", &dlg);
    QFormLayout* regionLayout = new QFormLayout(regionBox);
    regionLayout->setSpacing(6);

    QComboBox* modeCombos[7] = {};
    QLineEdit* offsetEdits[7] = {};
    for (int i = 0; i < 7; ++i)
    {
        modeCombos[i] = new QComboBox(regionBox);
        modeCombos[i]->addItems({"On", "Off"});
        modeCombos[i]->setCurrentText(m_panels[idx]->regionModeCombos[i]->currentText());

        QString currentText = m_panels[idx]->regionOffsetEdits[i]->text().trimmed();
        if (currentText.isEmpty() || currentText == "-1" || currentText.toInt() < 0) {
            currentText = QString::number(defOffsets[i]);
            m_panels[idx]->regionOffsetEdits[i]->setText(currentText);
        }
        offsetEdits[i] = new QLineEdit(currentText, regionBox);

        regionLayout->addRow(QString("R%1 Mode:").arg(i), modeCombos[i]);
        regionLayout->addRow(QString("R%1 Offset:").arg(i), offsetEdits[i]);
    }
    layout->addWidget(regionBox, 1);

    QHBoxLayout* footer = new QHBoxLayout();
    footer->addStretch(1);
    QPushButton* cancelBtn = new QPushButton("Cancel", &dlg);
    QPushButton* closeBtn = new QPushButton("Done", &dlg);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    footer->addWidget(cancelBtn);
    footer->addWidget(closeBtn);
    layout->addLayout(footer);

    if (dlg.exec() == QDialog::Accepted)
    {
        for (int i = 0; i < 7; ++i)
        {
            m_panels[idx]->regionModeCombos[i]->setCurrentText(modeCombos[i]->currentText());
            QString offVal = offsetEdits[i]->text().trimmed();
            if (offVal.isEmpty() || offVal == "-1" || offVal.toInt() < 0) {
                offVal = QString::number(defOffsets[i]);
            }
            m_panels[idx]->regionOffsetEdits[i]->setText(offVal);
        }
        m_panels[idx]->syncBinningControlsWithRegionModes();
        syncSidebarButtons(idx);
        updateWorkerParams(idx);
        saveSettings();
    }
}

void MainWindow::openBinningSettingsDialog(int idx)
{
    if (idx < 0 || idx > 1 || !m_panels[idx] || m_streaming[idx]) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("CAM %1 Binning Modes").arg(idx));
    dlg.resize(420, 480);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    QLabel* note = new QLabel("Configure binning modes (Off, 2x2, 4x4) for active regions only.", &dlg);
    note->setWordWrap(true);
    layout->addWidget(note);

    QGroupBox* binningBox = new QGroupBox("Region Binning Modes", &dlg);
    QFormLayout* binningLayout = new QFormLayout(binningBox);
    binningLayout->setSpacing(6);

    QComboBox* binningCombos[7] = {};
    int activeRegionCount = 0;

    for (int i = 0; i < 7; ++i)
    {
        const bool isRegionOn = (m_panels[idx]->regionModeCombos[i]->currentText() == "On");
        if (isRegionOn)
        {
            activeRegionCount++;
            binningCombos[i] = new QComboBox(binningBox);
            binningCombos[i]->addItems({"Off", "2x2", "4x4"});
            binningCombos[i]->setCurrentIndex(m_panels[idx]->regionBinningCombos[i]->currentIndex());
            binningLayout->addRow(QString("R%1 Binning:").arg(i), binningCombos[i]);
        }
        else
        {
            QLabel* disabledLabel = new QLabel("Disabled (Region Off)", binningBox);
            disabledLabel->setStyleSheet("color: gray;");
            binningLayout->addRow(QString("R%1 Binning:").arg(i), disabledLabel);
        }
    }

    if (activeRegionCount == 0)
    {
        QLabel* emptyLabel = new QLabel("No regions are currently active. Enable regions in Region Modes first.", &dlg);
        emptyLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
        emptyLabel->setWordWrap(true);
        layout->addWidget(emptyLabel);
    }

    layout->addWidget(binningBox, 1);

    QHBoxLayout* footer = new QHBoxLayout();
    footer->addStretch(1);
    QPushButton* cancelBtn = new QPushButton("Cancel", &dlg);
    QPushButton* closeBtn = new QPushButton("Done", &dlg);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    footer->addWidget(cancelBtn);
    footer->addWidget(closeBtn);
    layout->addLayout(footer);

    if (dlg.exec() == QDialog::Accepted)
    {
        for (int i = 0; i < 7; ++i)
        {
            if (binningCombos[i])
            {
                m_panels[idx]->regionBinningCombos[i]->setCurrentIndex(binningCombos[i]->currentIndex());
            }
        }
        syncSidebarButtons(idx);
        updateWorkerParams(idx);
        saveSettings();
    }
}

void MainWindow::copyCameraSettings(int sourceCamIdx, int targetCamIdx)
{
    if (sourceCamIdx < 0 || sourceCamIdx > 1 || targetCamIdx < 0 || targetCamIdx > 1 || sourceCamIdx == targetCamIdx)
        return;

    const bool isMergedMode = (m_saveModeToggleBtn && m_saveModeToggleBtn->isChecked());
    const bool isSavingTarget = isMergedMode ? isMergeSaving() : m_saving[targetCamIdx];

    if (isSavingTarget)
    {
        QMessageBox::warning(this, "Camera Settings",
            QString("CAM %1 is currently recording/saving. Please stop saving before applying settings.").arg(targetCamIdx));
        return;
    }

    const bool targetStreaming = m_streaming[targetCamIdx];

    if (targetStreaming)
    {
        // Copy live parameters: FPS, Exposure, Gain, Test Pattern
        m_cameraUiStates[targetCamIdx].fps         = m_cameraUiStates[sourceCamIdx].fps;
        m_cameraUiStates[targetCamIdx].exposure    = m_cameraUiStates[sourceCamIdx].exposure;
        m_cameraUiStates[targetCamIdx].gain        = m_cameraUiStates[sourceCamIdx].gain;
        m_cameraUiStates[targetCamIdx].testPattern = m_cameraUiStates[sourceCamIdx].testPattern;

        applyCameraUiStateToWidgets(targetCamIdx);

        if (m_connected[targetCamIdx] && m_cameraManager)
        {
            bool ok = false;
            double fps = m_cameraUiStates[targetCamIdx].fps.toDouble(&ok);
            if (ok && fps > 0.0)
                m_cameraManager->setLiveFps(targetCamIdx, fps);

            double expUs = m_cameraUiStates[targetCamIdx].exposure.toDouble(&ok);
            if (ok && expUs > 0.0)
                m_cameraManager->setLiveExposure(targetCamIdx, expUs);

            double gain = m_cameraUiStates[targetCamIdx].gain.toDouble(&ok);
            if (ok)
                m_cameraManager->setLiveGain(targetCamIdx, gain);

            int pat = (m_cameraUiStates[targetCamIdx].testPattern == "Gray Diagonal Ramp") ? 1 : 0;
            m_cameraManager->setLiveTestPattern(targetCamIdx, pat);
        }

        saveSettings();
        statusBar()->showMessage(QString("CAM %1 is streaming: applied live params (FPS, Exp, Gain, Test). Stop stream to apply geometry & regions.").arg(targetCamIdx), 5000);
        appendLogLine(QString("[System] CAM %1 is streaming: applied live parameters from CAM %2. Stop stream to apply geometry & regions.").arg(targetCamIdx).arg(sourceCamIdx));
        return;
    }

    // Target camera is NOT streaming: copy all configuration parameters
    const CameraUiState& srcState = m_cameraUiStates[sourceCamIdx];
    CameraUiState& dstState = m_cameraUiStates[targetCamIdx];

    dstState.width            = srcState.width;
    dstState.height           = srcState.height;
    dstState.fps              = srcState.fps;
    dstState.exposure         = srcState.exposure;
    dstState.gain             = srcState.gain;
    dstState.testPattern      = srcState.testPattern;
    dstState.tdiMode          = srcState.tdiMode;
    dstState.tdiStages        = srcState.tdiStages;
    dstState.tdiYShift        = srcState.tdiYShift;
    dstState.tdiFrameCount    = srcState.tdiFrameCount;
    dstState.reverseX         = srcState.reverseX;
    dstState.reverseY         = srcState.reverseY;
    dstState.savePath         = srcState.savePath;
    dstState.saveFormat       = srcState.saveFormat;
    dstState.saveMode         = srcState.saveMode;
    dstState.stopAfterEnabled = srcState.stopAfterEnabled;
    dstState.saveDuration     = srcState.saveDuration;
    dstState.appliedWidth     = srcState.width;
    dstState.appliedHeight    = srcState.height;

    // Copy region modes, offsets, and binning modes
    if (m_panels[sourceCamIdx] && m_panels[targetCamIdx])
    {
        for (int r = 0; r < 7; ++r)
        {
            if (m_panels[sourceCamIdx]->regionModeCombos[r] && m_panels[targetCamIdx]->regionModeCombos[r])
            {
                m_panels[targetCamIdx]->regionModeCombos[r]->setCurrentText(
                    m_panels[sourceCamIdx]->regionModeCombos[r]->currentText());
            }
            if (m_panels[sourceCamIdx]->regionOffsetEdits[r] && m_panels[targetCamIdx]->regionOffsetEdits[r])
            {
                m_panels[targetCamIdx]->regionOffsetEdits[r]->setText(
                    m_panels[sourceCamIdx]->regionOffsetEdits[r]->text());
            }
            if (m_panels[sourceCamIdx]->regionBinningCombos[r] && m_panels[targetCamIdx]->regionBinningCombos[r])
            {
                m_panels[targetCamIdx]->regionBinningCombos[r]->setCurrentIndex(
                    m_panels[sourceCamIdx]->regionBinningCombos[r]->currentIndex());
            }
        }
        m_panels[targetCamIdx]->syncBinningControlsWithRegionModes();
    }

    applyCameraUiStateToWidgets(targetCamIdx);
    syncSidebarButtons(targetCamIdx);
    updateWorkerParams(targetCamIdx);
    saveSettings();

    statusBar()->showMessage(QString("Applied all settings from CAM %1 to CAM %2.").arg(sourceCamIdx).arg(targetCamIdx), 4000);
    appendLogLine(QString("[System] Applied all settings (geometry, FPS, Exp, Gain, TDI, Test, Regions, Save) from CAM %1 to CAM %2.").arg(sourceCamIdx).arg(targetCamIdx));
}

void MainWindow::applyCameraSourceSelection(int slot)
{
    if (slot < 0 || slot > 1 || !m_cameraManager) return;
    int detectedIndex = -1;
    if (m_sidebarSourceCombos[slot])
        detectedIndex = m_sidebarSourceCombos[slot]->currentIndex();
    else if (m_topSourceCombos[slot])
        detectedIndex = m_topSourceCombos[slot]->currentIndex();

    m_cameraManager->assignCameraSource(slot, detectedIndex);
    if (m_panels[slot] && m_panels[slot]->connectBtn) {
        m_panels[slot]->connectBtn->setEnabled(detectedIndex >= 0);
    }
    syncSidebarButtons(slot);
}

void MainWindow::applyCameraUiStateToWidgets(int idx)
{
    if (idx < 0 || idx > 1 || !m_panels[idx]) return;
    const CameraUiState& state = m_cameraUiStates[idx];

    auto setEditText = [](QLineEdit* edit, const QString& value) {
        if (!edit || edit->text() == value) return;
        QSignalBlocker blocker(edit);
        edit->setText(value);
    };
    auto setComboText = [](QComboBox* combo, const QString& value) {
        if (!combo || combo->currentText() == value) return;
        QSignalBlocker blocker(combo);
        combo->setCurrentText(value);
    };

    setEditText(m_panels[idx]->widthEdit, state.width);
    setEditText(m_panels[idx]->heightEdit, state.height);
    setEditText(m_panels[idx]->fpsEdit, state.fps);
    setEditText(m_panels[idx]->exposureEdit, state.exposure);
    setEditText(m_panels[idx]->gainEdit, state.gain);
    setComboText(m_panels[idx]->testPatternCombo, state.testPattern);
    setEditText(m_panels[idx]->rawPathEdit,  state.rawPath);
    setEditText(m_panels[idx]->savePathEdit, state.savePath);
    setComboText(m_panels[idx]->saveFormatCombo, state.saveFormat);
    setComboText(m_panels[idx]->saveModeCombo, state.saveMode);
    setComboText(m_panels[idx]->tdiModeCombo, state.tdiMode);
    setEditText(m_panels[idx]->tdiStagesEdit, state.tdiStages);
    setEditText(m_panels[idx]->tdiYShiftEdit, state.tdiYShift);
    setEditText(m_panels[idx]->tdiFrameCountEdit, state.tdiFrameCount);
    if (m_panels[idx]->reverseXCheck && m_panels[idx]->reverseXCheck->isChecked() != state.reverseX) {
        QSignalBlocker blocker(m_panels[idx]->reverseXCheck);
        m_panels[idx]->reverseXCheck->setChecked(state.reverseX);
    }
    if (m_panels[idx]->reverseYCheck && m_panels[idx]->reverseYCheck->isChecked() != state.reverseY) {
        QSignalBlocker blocker(m_panels[idx]->reverseYCheck);
        m_panels[idx]->reverseYCheck->setChecked(state.reverseY);
    }
    if (m_panels[idx]->stopAfterDurationCheck && m_panels[idx]->stopAfterDurationCheck->isChecked() != state.stopAfterEnabled) {
        QSignalBlocker blocker(m_panels[idx]->stopAfterDurationCheck);
        m_panels[idx]->stopAfterDurationCheck->setChecked(state.stopAfterEnabled);
    }
    setEditText(m_panels[idx]->saveDurationEdit, state.saveDuration);

    setEditText(m_sidebarWidthEdits[idx], state.width);
    setEditText(m_sidebarHeightEdits[idx], state.height);
    setEditText(m_sidebarFpsEdits[idx], state.fps);
    setComboText(m_sidebarTestPatternCombos[idx], state.testPattern);
    setEditText(m_sidebarSavePathEdits[idx], state.savePath);
    setEditText(m_sidebarExposureEdits[idx], state.exposure);
    setEditText(m_sidebarGainEdits[idx], state.gain);
    setComboText(m_sidebarSaveFormatCombos[idx], state.saveFormat);
    setComboText(m_sidebarSaveModeCombos[idx], state.saveMode);
    setComboText(m_sidebarTdiModeCombos[idx], state.tdiMode);
    setEditText(m_sidebarTdiStagesEdits[idx], state.tdiStages);
    setEditText(m_sidebarTdiYShiftEdits[idx], state.tdiYShift);
    setEditText(m_sidebarTdiFrameCountEdits[idx], state.tdiFrameCount);
    if (m_sidebarReverseXChecks[idx] && m_sidebarReverseXChecks[idx]->isChecked() != state.reverseX) {
        QSignalBlocker blocker(m_sidebarReverseXChecks[idx]);
        m_sidebarReverseXChecks[idx]->setChecked(state.reverseX);
    }
    if (m_sidebarReverseYChecks[idx] && m_sidebarReverseYChecks[idx]->isChecked() != state.reverseY) {
        QSignalBlocker blocker(m_sidebarReverseYChecks[idx]);
        m_sidebarReverseYChecks[idx]->setChecked(state.reverseY);
    }
    if (m_sidebarStopAfterDurationChecks[idx] && m_sidebarStopAfterDurationChecks[idx]->isChecked() != state.stopAfterEnabled) {
        QSignalBlocker blocker(m_sidebarStopAfterDurationChecks[idx]);
        m_sidebarStopAfterDurationChecks[idx]->setChecked(state.stopAfterEnabled);
    }
    setEditText(m_sidebarSaveDurationEdits[idx], state.saveDuration);

    setEditText(m_topWidthEdits[idx], state.width);
    setEditText(m_topHeightEdits[idx], state.height);
    setEditText(m_topFpsEdits[idx], state.fps);
    setComboText(m_topTestPatternCombos[idx], state.testPattern);
    setEditText(m_topSavePathEdits[idx], state.savePath);
    setEditText(m_topExposureEdits[idx], state.exposure);
    setEditText(m_topGainEdits[idx], state.gain);
    setComboText(m_topSaveFormatCombos[idx], state.saveFormat);
    setComboText(m_topSaveModeCombos[idx], state.saveMode);
    setComboText(m_topTdiModeCombos[idx], state.tdiMode);
    setEditText(m_topTdiStagesEdits[idx], state.tdiStages);
    setEditText(m_topTdiYShiftEdits[idx], state.tdiYShift);
    setEditText(m_topTdiFrameCountEdits[idx], state.tdiFrameCount);
    if (m_topReverseXChecks[idx] && m_topReverseXChecks[idx]->isChecked() != state.reverseX) {
        QSignalBlocker blocker(m_topReverseXChecks[idx]);
        m_topReverseXChecks[idx]->setChecked(state.reverseX);
    }
    if (m_topReverseYChecks[idx] && m_topReverseYChecks[idx]->isChecked() != state.reverseY) {
        QSignalBlocker blocker(m_topReverseYChecks[idx]);
        m_topReverseYChecks[idx]->setChecked(state.reverseY);
    }
    if (m_topStopAfterDurationChecks[idx] && m_topStopAfterDurationChecks[idx]->isChecked() != state.stopAfterEnabled) {
        QSignalBlocker blocker(m_topStopAfterDurationChecks[idx]);
        m_topStopAfterDurationChecks[idx]->setChecked(state.stopAfterEnabled);
    }
    setEditText(m_topSaveDurationEdits[idx], state.saveDuration);
    updateTdiUiVisibility(idx);
}

void MainWindow::updateCameraUiState(int idx, const QString& width, const QString& height, const QString& fps,
                                     const QString& exposure, const QString& gain, const QString& testPattern,
                                     const QString& savePath, const QString& saveFormat, const QString& saveMode,
                                     const QString& rawPath)
{
    if (idx < 0 || idx > 1) return;
    CameraUiState& state = m_cameraUiStates[idx];
    if (state.appliedWidth.isEmpty()) state.appliedWidth = width;
    if (state.appliedHeight.isEmpty()) state.appliedHeight = height;
    state.width = width;
    state.height = height;
    state.fps = fps;
    state.exposure = exposure;
    state.gain = gain;
    state.testPattern = testPattern;
    state.savePath = savePath;
    state.rawPath = rawPath.isEmpty() ? state.rawPath : rawPath;
    state.saveFormat = saveFormat;
    state.saveMode = saveMode;

    applyCameraUiStateToWidgets(idx);
}

void MainWindow::updateWorkerParams(int idx)
{
    if (idx < 0 || idx > 1 || !m_cameraManager) return;

    CameraParams params = m_panels[idx]->collectParams();
    params.width = m_cameraUiStates[idx].width.toInt();
    params.height = m_cameraUiStates[idx].height.toInt();
    params.fps = m_cameraUiStates[idx].fps.toFloat();
    params.exposure = m_cameraUiStates[idx].exposure.toFloat();
    params.gain = m_cameraUiStates[idx].gain.toFloat();
    params.testPattern = (m_cameraUiStates[idx].testPattern == "Gray Diagonal Ramp") ? 1 : 0;
    const int tdiModeIndex = m_panels[idx]->tdiModeCombo
        ? m_panels[idx]->tdiModeCombo->findText(m_cameraUiStates[idx].tdiMode)
        : -1;
    params.tdiMode = (tdiModeIndex >= 0) ? tdiModeIndex : params.tdiMode;
    params.tdiStages = m_cameraUiStates[idx].tdiStages.toInt();
    params.tdiYShift = m_cameraUiStates[idx].tdiYShift.toInt();
    params.tdiFrameCount = m_cameraUiStates[idx].tdiFrameCount.toInt();
    if (params.tdiFrameCount <= 0) params.tdiFrameCount = 10;
    params.reverseX = m_cameraUiStates[idx].reverseX;
    m_cameraManager->applyWorkerParams(idx, params);
}

void MainWindow::updateCameraAdvancedState(int idx, const QString& tdiMode, const QString& tdiStages,
                                           const QString& tdiYShift, const QString& tdiFrameCount,
                                           bool reverseX, bool reverseY,
                                           bool stopAfterEnabled, const QString& saveDuration)
{
    if (idx < 0 || idx > 1) return;
    CameraUiState& state = m_cameraUiStates[idx];
    const bool revXChanged = (state.reverseX != reverseX);
    const bool revYChanged = (state.reverseY != reverseY);
    QString effectiveYShift = tdiYShift;
    if (state.tdiMode != "TDI" && tdiMode == "TDI")
    {
        const int height = state.height.toInt();
        const int stages = tdiStages.toInt();
        bool shiftOk = false;
        const int shift = tdiYShift.toInt(&shiftOk);
        if (shiftOk && stages > 0 && shift == height / stages)
            effectiveYShift = "16"; // Migrate the old automatic height/stage value.
    }
    const bool tdiChanged = (state.tdiMode != tdiMode || state.tdiStages != tdiStages || 
                             state.tdiYShift != effectiveYShift || state.tdiFrameCount != tdiFrameCount);

    state.tdiMode = tdiMode;
    state.tdiStages = tdiStages;
    state.tdiYShift = effectiveYShift;
    state.tdiFrameCount = tdiFrameCount;
    state.reverseX = reverseX;
    state.reverseY = reverseY;
    state.stopAfterEnabled = stopAfterEnabled;
    state.saveDuration = saveDuration;
    applyCameraUiStateToWidgets(idx);

    if (m_connected[idx] && m_cameraManager) {
        if (revXChanged) {
            m_cameraManager->setLiveReverseX(idx, reverseX);
        }
        if (revYChanged) {
            m_cameraManager->setLiveReverseY(idx, reverseY);
        }
        if (tdiChanged) {
            const int mode = (state.tdiMode == "TDI") ? 2 : ((state.tdiMode == "Record") ? 1 : 0);
            const int stages = state.tdiStages.toInt();
            const int yShift = state.tdiYShift.toInt();
            const int frameCount = state.tdiFrameCount.toInt();
            m_cameraManager->setLiveTdi(idx, mode, stages, yShift, frameCount);
        }
    }
}

void MainWindow::updateTdiUiVisibility(int idx)
{
    if (idx < 0 || idx > 1) return;
    const QString mode = m_cameraUiStates[idx].tdiMode;
    const bool isTdi = (mode == "TDI");
    const bool isRecord = (mode == "Record");

    // Left menu sidebar:
    // Remove TDI stages when TDI is off or record.
    // When TDI is record, ask framecount instead of stages.
    if (m_sidebarTdiCfgRow[idx])
        m_sidebarTdiCfgRow[idx]->setVisible(isTdi || isRecord);
    if (m_sidebarTdiStagesGroup[idx])
        m_sidebarTdiStagesGroup[idx]->setVisible(isTdi);
    if (m_sidebarTdiYShiftGroup[idx])
        m_sidebarTdiYShiftGroup[idx]->setVisible(isTdi);
    if (m_sidebarTdiFrameCountGroup[idx])
        m_sidebarTdiFrameCountGroup[idx]->setVisible(isRecord);

    // Top layout (ControlsTop preset):
    if (m_topTdiCfgRow[idx])
        m_topTdiCfgRow[idx]->setVisible(isTdi || isRecord);
    if (m_topTdiStagesGroup[idx])
        m_topTdiStagesGroup[idx]->setVisible(isTdi);
    if (m_topTdiYShiftGroup[idx])
        m_topTdiYShiftGroup[idx]->setVisible(isTdi);
    if (m_topTdiFrameCountGroup[idx])
        m_topTdiFrameCountGroup[idx]->setVisible(isRecord);

    // CameraPanel:
    if (m_panels[idx]) {
        if (m_panels[idx]->tdiStagesLabel) m_panels[idx]->tdiStagesLabel->setVisible(isTdi);
        if (m_panels[idx]->tdiStagesEdit) m_panels[idx]->tdiStagesEdit->setVisible(isTdi);
        if (m_panels[idx]->tdiYShiftLabel) m_panels[idx]->tdiYShiftLabel->setVisible(isTdi);
        if (m_panels[idx]->tdiYShiftEdit) m_panels[idx]->tdiYShiftEdit->setVisible(isTdi);
        if (m_panels[idx]->tdiFrameCountLabel) m_panels[idx]->tdiFrameCountLabel->setVisible(isRecord);
        if (m_panels[idx]->tdiFrameCountEdit) m_panels[idx]->tdiFrameCountEdit->setVisible(isRecord);
    }
}

// ── Per-camera slots ──────────────────────────────────────────────────
void MainWindow::onConnectClicked(int idx)
{
    if (idx < 0 || idx > 1 || !m_cameraManager) return;
    if (!m_connected[idx])
    {
        applyCameraSourceSelection(idx);
        int newOffset = getActiveRegionOffset(idx);
        int oldOffset = m_lastAppliedRegionOffset[idx];
        if (newOffset != oldOffset) {
            int diff = newOffset - oldOffset;
            int delayAdjustment = (idx == 1) ? diff : -diff;
            int oldDelay = getLineDelayValue();
            int newDelay = std::max(0, oldDelay + delayAdjustment);
            setLineDelayValue(newDelay);
            if (statusBar()) {
                statusBar()->showMessage(QString("Region offset changed by %1 lines. Adjusted Line Delay from %2 to %3.")
                    .arg(delayAdjustment).arg(oldDelay).arg(newDelay), 5000);
            }
            m_lastAppliedRegionOffset[idx] = newOffset;
        }

        updateWorkerParams(idx);
        m_cameraManager->connectCamera(idx);
    }
    else
    {
        m_cameraManager->disconnectCamera(idx);
    }
}

void MainWindow::onStartClicked(int idx)
{
    if (idx < 0 || idx > 1 || !m_cameraManager) return;
    if (!m_connected[idx])
    {
        QMessageBox::information(this, "Start", QString("Connect Camera %1 first.").arg(idx));
        return;
    }

    if (m_streaming[idx])
    {
        m_cameraManager->stopStream(idx);
        return;
    }

    updateWorkerParams(idx);
    m_cameraManager->startStream(idx);
}

void MainWindow::onSaveClicked(int idx)
{
    if (m_saveModeToggleBtn && m_saveModeToggleBtn->isChecked()) {
        if (m_captureController && !m_captureController->isMergeSaving()) {
            for (int i = 0; i < 2; ++i) {
                if (m_connected[i] && !m_streaming[i]) {
                    onStartClicked(i);
                }
            }
        }
        onMergeSaveClicked();
        return;
    }

    if (!m_streaming[idx]) {
        QMessageBox::information(this, "Save", QString("Start Camera %1 first.").arg(idx));
        return;
    }

    if (!m_captureController) return;

    if (!m_captureController->isSaving(idx)) {
        CaptureSaveParams params;
        params.savePath = m_cameraUiStates[idx].savePath;
        params.saveFormat = m_cameraUiStates[idx].saveFormat;
        params.saveMode = m_cameraUiStates[idx].saveMode;
        params.width = m_cameraUiStates[idx].width.toInt();
        params.height = m_cameraUiStates[idx].height.toInt();
        params.fps = m_cameraUiStates[idx].fps.toDouble();
        params.exposure = m_cameraUiStates[idx].exposure.toDouble();
        params.gain = m_cameraUiStates[idx].gain.toDouble();
        params.tdiMode = m_cameraUiStates[idx].tdiMode;
        params.tdiStages = m_cameraUiStates[idx].tdiStages.toInt();
        params.tdiFrameCount = m_cameraUiStates[idx].tdiFrameCount.toInt();
        params.stopAfterEnabled = m_cameraUiStates[idx].stopAfterEnabled;
        params.saveDuration = m_cameraUiStates[idx].saveDuration.toDouble();
        params.sensorTemp = m_lastSensorTemp[idx];
        params.grabberTemp = m_lastGrabberTemp[idx];

        QString initialLogText = m_logTerminal ? m_logTerminal->toPlainText() : QString();
        if (!m_captureController->startSave(idx, params, initialLogText)) {
            QMessageBox::critical(this, "Save Error", "Failed to start capture recording.");
        }
    } else {
        m_captureController->stopSave(idx, false);
    }
}

void MainWindow::stopSave(int idx, bool completed)
{
    if (m_captureController) {
        m_captureController->stopSave(idx, completed);
    }
}

void MainWindow::onBrowseRaw(int idx)
{
    QString p = QFileDialog::getOpenFileName(this, "Select Playback File", "", "Raw (*.raw);;Bitmap (*.bmp);;All Files (*)");
    if (p.isEmpty())
    {
        p = QFileDialog::getExistingDirectory(this, "Select Playback Folder");
    }
    if (!p.isEmpty())
    {
        updateCameraUiState(idx,
            m_cameraUiStates[idx].width,
            m_cameraUiStates[idx].height,
            m_cameraUiStates[idx].fps,
            m_cameraUiStates[idx].exposure,
            m_cameraUiStates[idx].gain,
            m_cameraUiStates[idx].testPattern,
            m_cameraUiStates[idx].savePath,
            m_cameraUiStates[idx].saveFormat,
            m_cameraUiStates[idx].saveMode,
            p);
    }
}

void MainWindow::onBrowseSave(int idx)
{
    QString d = QFileDialog::getExistingDirectory(this,"Select Save Directory");
    if (!d.isEmpty())
        updateCameraUiState(idx,
            m_cameraUiStates[idx].width,
            m_cameraUiStates[idx].height,
            m_cameraUiStates[idx].fps,
            m_cameraUiStates[idx].exposure,
            m_cameraUiStates[idx].gain,
            m_cameraUiStates[idx].testPattern,
            d,
            m_cameraUiStates[idx].saveFormat,
            m_cameraUiStates[idx].saveMode);
}

void MainWindow::stopPlayback(int idx, bool completed)
{
    if (m_playbackController) {
        m_playbackController->stopPlayback(idx, completed);
    }
}

void MainWindow::onPlaybackClicked(int idx)
{
    if (idx < 0 || idx > 1 || !m_playbackController) return;

    if (m_playbackController->isPlaying(idx)) {
        m_playbackController->stopPlayback(idx, false);
        return;
    }

    if (m_streaming[idx]) {
        QMessageBox::information(this, "Playback", QString("Stop live stream for Camera %1 first.").arg(idx));
        return;
    }

    QString rawPath = m_cameraUiStates[idx].rawPath;
    if (rawPath.isEmpty()) {
        onBrowseRaw(idx);
        rawPath = m_cameraUiStates[idx].rawPath;
        if (rawPath.isEmpty()) return;
    }

    PlaybackMetadata meta;
    if (PlaybackController::loadMetadata(rawPath, meta)) {
        if (!meta.width.isEmpty())  m_cameraUiStates[idx].width = meta.width;
        if (!meta.height.isEmpty()) m_cameraUiStates[idx].height = meta.height;
        if (!meta.fps.isEmpty())    m_cameraUiStates[idx].fps = meta.fps;
        applyCameraUiStateToWidgets(idx);
    }

    int width = m_cameraUiStates[idx].width.toInt();
    int height = m_cameraUiStates[idx].height.toInt();
    double fps = m_cameraUiStates[idx].fps.toDouble();

    m_playbackController->startPlayback(idx, rawPath, width, height, fps);
}

// ── Global controls ───────────────────────────────────────────────────
void MainWindow::onConnectAllClicked()
{
    bool allConnected = m_connected[0] && m_connected[1];
    if (allConnected) {
        for (int i = 0; i < 2; ++i) {
            if (m_connected[i]) {
                onConnectClicked(i);
            }
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            if (!m_connected[i]) {
                onConnectClicked(i);
            }
        }
    }
}

void MainWindow::onStartAllClicked()
{
    bool anyStreaming = m_streaming[0] || m_streaming[1];
    if (anyStreaming) {
        for (int i = 0; i < 2; ++i) {
            if (m_streaming[i]) onStartClicked(i);
        }
        return;
    }

    if (!m_connected[0] && !m_connected[1]) {
        QMessageBox::information(this, "Start All", "Connect at least one camera first.");
        return;
    }

    for (int i = 0; i < 2; ++i) {
        if (m_connected[i] && !m_streaming[i]) {
            updateWorkerParams(i);
            if (m_cameraManager) m_cameraManager->startStream(i);
        }
    }
}

void MainWindow::onSaveAllClicked()
{
    if (m_saveModeToggleBtn && m_saveModeToggleBtn->isChecked()) {
        onMergeSaveClicked();
        return;
    }

    bool anySaving = m_saving[0] || m_saving[1];
    if (anySaving) {
        for (int i = 0; i < 2; ++i) {
            if (m_saving[i]) {
                stopSave(i, false);
            }
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            if (m_streaming[i] && !m_saving[i]) {
                onSaveClicked(i);
            }
        }
    }
    syncSidebarButtons(0);
}

void MainWindow::onMergeSaveClicked()
{
    if (!m_captureController) return;

    if (!m_captureController->isMergeSaving()) {
        if (!m_streaming[0] || !m_streaming[1]) {
            QMessageBox::warning(this, "Merge Save", "Both Camera 0 and Camera 1 must be streaming to start a synced merged save session.");
            return;
        }
        if (!m_lastFrames[0] || !m_lastFrames[1]) {
            QMessageBox::information(this, "Merge Save", "Wait until both cameras have delivered a frame before starting merged save.");
            return;
        }

        QString baseDir = m_cameraUiStates[0].savePath.trimmed().isEmpty() ? "Capture" : m_cameraUiStates[0].savePath.trimmed();
        QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString sessionDir = QString("%1/Merged_%2").arg(baseDir).arg(timeStr);

        MergeSaveOptions opts;
        opts.saveFormat = m_cameraUiStates[0].saveFormat;
        opts.useSingleFile = (opts.saveFormat == ".raw");
        opts.expectedWidth0 = m_lastFrames[0]->width;
        opts.expectedHeight0 = (!m_lastFrames[0]->rawData.empty() && m_lastFrames[0]->height > 1)
            ? m_lastFrames[0]->height - 1 : m_lastFrames[0]->height;
        opts.expectedWidth1 = m_lastFrames[1]->width;
        opts.expectedHeight1 = (!m_lastFrames[1]->rawData.empty() && m_lastFrames[1]->height > 1)
            ? m_lastFrames[1]->height - 1 : m_lastFrames[1]->height;
        opts.fps = m_cameraUiStates[0].fps.toDouble();
        if (opts.fps <= 0) opts.fps = 10.0;

        if (!m_captureController->startMergeSave(sessionDir, opts)) {
            QMessageBox::critical(this, "Merge Save Error", QString("Failed to start merge save session in %1").arg(sessionDir));
        }
    } else {
        m_captureController->stopMergeSave();
    }
}

void MainWindow::onMergeSaverStats(quint64, quint64, quint64)
{
}

void MainWindow::onToggleControlsClicked()
{
    m_controlsHidden = !m_controlsHidden;
    applyControlVisibility();
}



// ── Worker signal handlers ────────────────────────────────────────────
void MainWindow::onFrameReady(FramePacketPtr pkt)
{
    if (!pkt) return;
    int idx = pkt->cameraIndex;
    if (idx < 0 || idx > 1) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    const int syncMode = m_calibModeCombo ? m_calibModeCombo->currentIndex() : 0;
    const double timeDelaySec = m_timeDelaySpin ? m_timeDelaySpin->value() : 0.0;
    const double fps = m_cameraUiStates[0].fps.toDouble();

    const bool mustProcessEveryFrame = (m_captureController && (m_captureController->isSaving(idx) || m_captureController->isMergeSaving()))
                                    || (m_calibrationEngine && m_calibrationEngine->isCalibrating());
    if (!mustProcessEveryFrame && pkt->timestampMs > 0 && now - pkt->timestampMs > 100)
        return;

    if (m_calibrationEngine) {
        pkt = m_calibrationEngine->processFrame(pkt, syncMode, timeDelaySec, fps);
    }

    m_lastFrames[idx] = pkt;
    m_frameCount[idx]++;

    if (m_captureController && m_captureController->isMergeSaving()) {
        if (m_lastFrames[0] && m_lastFrames[1] && idx == 1) {
            m_captureController->pushMergedPair(m_lastFrames[0], m_lastFrames[1]);
        }
    }

    double cameraFps = std::max(1.0, m_cameraUiStates[idx].fps.toDouble());
    int limitMs = std::max(1, (int)(1000.0 / cameraFps) - 2);
    const bool displayFrameFresh = (pkt->timestampMs <= 0 || now - pkt->timestampMs <= 100);
    const bool updateDisplay = displayFrameFresh && (m_lastDisplayMs[idx] == 0 || now - m_lastDisplayMs[idx] >= limitMs);
    if (updateDisplay) {
        m_lastDisplayMs[idx] = now;
        refreshDisplay(idx);
        if (m_viewFpsLabels[idx] && m_panels[idx]) m_viewFpsLabels[idx]->setText(m_panels[idx]->fpsLabel->text());
        if (m_viewStatusLabels[idx] && m_panels[idx]) m_viewStatusLabels[idx]->setText(m_panels[idx]->statusLabel->text());
    }

    if (m_captureController && m_captureController->isSaving(idx)) {
        m_captureController->writeFrame(idx, pkt);
    }
}

void MainWindow::onLogMessage(const QString& m)
{
    static const QRegularExpression fpsPattern(R"(\[Cam([01])\] Grabber instant FPS: ([0-9]+(?:\.[0-9]+)?))");
    const auto fpsMatch = fpsPattern.match(m);
    if (fpsMatch.hasMatch()) {
        const int idx = fpsMatch.captured(1).toInt();
        const double fps = fpsMatch.captured(2).toDouble();
        if (m_panels[idx]) m_panels[idx]->setAcquisitionFps(fps);
        if (m_viewFpsLabels[idx]) m_viewFpsLabels[idx]->setText(QString("%1 fps").arg(fps, 0, 'f', 1));
        const QString liveState = QString("%1 | %2 fps")
            .arg(m_streaming[idx] ? "Streaming" : (m_connected[idx] ? "Connected" : "Disconnected"))
            .arg(fps, 0, 'f', 1);
        if (m_sidebarStateLabels[idx]) m_sidebarStateLabels[idx]->setText(liveState);
        if (m_topStateLabels[idx]) m_topStateLabels[idx]->setText(liveState);
        if (m_logTerminal) {
            const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
            m_logTerminal->appendPlainText(QString("[%1] %2").arg(ts, m));
        }
        if (m_captureController) m_captureController->appendTelemetry(m);
        return;
    }

    appendLogLine(m);
    QSignalBlocker blocker(statusBar());
    statusBar()->showMessage(m, 4000);
}
void MainWindow::onStreamStarted(int i)
{
    m_streaming[i]=true;
    m_lastDisplayMs[i]=0;
    m_panels[i]->setStreaming(true);
    syncSidebarButtons(i);
    statusBar()->showMessage(QString("[Cam%1] Streaming…").arg(i),3000);
}
void MainWindow::onStreamStopped(int i)
{
    m_streaming[i]=false;
    m_panels[i]->setStreaming(false);
    m_panels[i]->setAcquisitionFps(0.0);
    syncSidebarButtons(i);
    if (m_viewFpsLabels[i]) m_viewFpsLabels[i]->setText("— fps");
    statusBar()->showMessage(QString("[Cam%1] Stopped.").arg(i),3000);
}

void MainWindow::onParamsQueried(int idx, const CameraParams& params)
{
    if (idx < 0 || idx > 1) return;

    CameraUiState& state = m_cameraUiStates[idx];
    // Do NOT overwrite user inputs (Width/Height) with camera frame dimensions.
    // Preserving user input prevents e.g. Region Height 768 from being overwritten by 4609.
    if (state.width.isEmpty() && params.width > 0)
        state.width = QString::number(params.width);
    if (state.height.isEmpty() && params.height > 0)
        state.height = QString::number(params.height);

    state.appliedWidth = QString::number(params.width);
    state.appliedHeight = QString::number(params.height);
    state.fps = QString::number(params.fps, 'f', 2);
    state.exposure = QString::number(params.exposure, 'f', 0);
    state.gain = QString::number(params.gain, 'f', 1);
    state.testPattern = (params.testPattern == 1 ? "Gray Diagonal Ramp" : "Off");
    if (params.tdiMode >= 0 && params.tdiMode <= 2)
        state.tdiMode = (params.tdiMode == 2 ? "TDI" : (params.tdiMode == 1 ? "Record" : "Off"));
    if (params.tdiStages > 0)
        state.tdiStages = QString::number(params.tdiStages);
    if (params.tdiYShift >= 0)
        state.tdiYShift = QString::number(params.tdiYShift);
    state.reverseX = params.reverseX;
    state.reverseY = params.reverseY;

    if (m_panels[idx]) {
        for (int i = 0; i < 7; ++i) {
            bool isRegionOn = (params.regionModeMask & (1 << i)) != 0;
            if (m_panels[idx]->regionModeCombos[i]) {
                m_panels[idx]->regionModeCombos[i]->setCurrentText(isRegionOn ? "On" : "Off");
            }
            if (m_panels[idx]->regionOffsetEdits[i]) {
                if (i < static_cast<int>(params.regionOffsets.size()) && params.regionOffsets[i] >= 0) {
                    m_panels[idx]->regionOffsetEdits[i]->setText(QString::number(params.regionOffsets[i]));
                }
            }
            if (m_panels[idx]->regionBinningCombos[i]) {
                if (i < static_cast<int>(params.regionBinningModes.size())) {
                    int binIdx = params.regionBinningModes[i];
                    if (binIdx >= 0 && binIdx <= 2) {
                        m_panels[idx]->regionBinningCombos[i]->setCurrentIndex(binIdx);
                    }
                }
            }
        }
        m_panels[idx]->syncBinningControlsWithRegionModes();
    }

    applyCameraUiStateToWidgets(idx);

    if (params.maxFps > 0.0f) {
        const QString tip = QString("Frame Rate (Max: %1 Hz for current height %2)")
            .arg(params.maxFps, 0, 'f', 2).arg(params.height);
        if (m_sidebarFpsEdits[idx]) {
            m_sidebarFpsEdits[idx]->setToolTip(tip);
            m_sidebarFpsEdits[idx]->setValidator(new QDoubleValidator(0.1, params.maxFps, 2, this));
        }
        if (m_topFpsEdits[idx]) {
            m_topFpsEdits[idx]->setToolTip(tip);
            m_topFpsEdits[idx]->setValidator(new QDoubleValidator(0.1, params.maxFps, 2, this));
        }
        if (m_panels[idx] && m_panels[idx]->fpsEdit) {
            m_panels[idx]->fpsEdit->setToolTip(tip);
            m_panels[idx]->fpsEdit->setValidator(new QDoubleValidator(0.1, params.maxFps, 2, this));
        }
    }

    // Show actual acquired hardware dimensions at the bottom of the image
    const QString sizeStr = QString("Connected  |  Size: %1 x %2  |  Frame: 0  |  Min: —  |  Max: —")
        .arg(params.width).arg(params.height);
    if (m_viewStatusLabels[idx]) m_viewStatusLabels[idx]->setText(sizeStr);
    if (m_panels[idx] && m_panels[idx]->statusLabel) m_panels[idx]->statusLabel->setText(sizeStr);
}

void MainWindow::onCameraOpened(int i, int actualWidth, int actualHeight)
{
    m_connected[i]=true;
    m_cameraUiStates[i].appliedWidth = QString::number(actualWidth);
    m_cameraUiStates[i].appliedHeight = QString::number(actualHeight);

    m_panels[i]->setConnected(true);
    syncSidebarButtons(i);
    const QString statusStr = QString("Connected  |  Size: %1 x %2  |  Frame: 0  |  Min: —  |  Max: —")
        .arg(actualWidth).arg(actualHeight);
    if (m_viewStatusLabels[i]) m_viewStatusLabels[i]->setText(statusStr);
    if (m_panels[i] && m_panels[i]->statusLabel) m_panels[i]->statusLabel->setText(statusStr);
    if (m_viewSensorTempLabels[i]) m_viewSensorTempLabels[i]->setText("S: — °C");
    if (m_viewGrabberTempLabels[i]) m_viewGrabberTempLabels[i]->setText("G: — °C");
    statusBar()->showMessage(QString("[Cam%1] Connected. Stream size: %2x%3").arg(i).arg(actualWidth).arg(actualHeight), 3000);
}

void MainWindow::onCameraClosed(int i)
{
    m_connected[i] = m_streaming[i] = m_saving[i] = false;
    if (m_captureController) m_captureController->stopSave(i);
    m_panels[i]->setConnected(false); m_panels[i]->setStreaming(false); m_panels[i]->setSaving(false); m_panels[i]->clearFrame();
    syncSidebarButtons(i);
    m_viewLabels[i]->clear(); m_viewLabels[i]->setText("No Signal");
    if (m_viewFpsLabels[i]) m_viewFpsLabels[i]->setText("— fps");
    if (m_viewSensorTempLabels[i]) m_viewSensorTempLabels[i]->setText("S: — °C");
    if (m_viewGrabberTempLabels[i]) m_viewGrabberTempLabels[i]->setText("G: — °C");
    if (m_viewStatusLabels[i]) m_viewStatusLabels[i]->setText("Disconnected  |  Frame: 0  |  Min: —  |  Max: —");
    if (m_panels[i] && m_panels[i]->statusLabel) m_panels[i]->statusLabel->setText("Disconnected  |  Frame: 0  |  Min: —  |  Max: —");
    m_lastFrames[i].reset();
    m_lastDisplayMs[i]=0;
    m_frameCount[i]=0; statusBar()->showMessage(QString("[Cam%1] Disconnected.").arg(i),3000);
}

void MainWindow::onCameraError(int i, const QString& m)
{ QMessageBox::critical(this,QString("Camera %1 Error").arg(i),m); }

void MainWindow::onTemperatureReady(int idx, float sensor, float grabber)
{
    if (idx < 0 || idx > 1 || !m_panels[idx]) return;

    m_lastSensorTemp[idx] = sensor;
    m_lastGrabberTemp[idx] = grabber;

    const QString sensorText = sensor > -999.0f
        ? QString("S: %1 °C").arg(sensor, 0, 'f', 1)
        : QString("S: — °C");
    const QString grabberText = grabber > -999.0f
        ? QString("G: %1 °C").arg(grabber, 0, 'f', 1)
        : QString("G: — °C");

    m_panels[idx]->sensorTempLabel->setText(sensorText);
    m_panels[idx]->grabberTempLabel->setText(grabberText);
    if (m_viewSensorTempLabels[idx])  m_viewSensorTempLabels[idx]->setText(sensorText);
    if (m_viewGrabberTempLabels[idx]) m_viewGrabberTempLabels[idx]->setText(grabberText);

    if (m_saving[idx] || isMergeSaving()) {
        const QString sensorVal = (sensor > -999.0f) ? QString("%1").arg(sensor, 0, 'f', 1) : "—";
        const QString grabberVal = (grabber > -999.0f) ? QString("%1").arg(grabber, 0, 'f', 1) : "—";
        if (m_captureController) {
            m_captureController->appendTelemetry(QString("[Cam%1] Sensor Temp: %2").arg(idx).arg(sensorVal));
            m_captureController->appendTelemetry(QString("[Cam%1] Grabber Temp: %2").arg(idx).arg(grabberVal));
        }
    }
}

void MainWindow::onDisplayModeToggled()
{
    const bool stretchMode = m_displayModeBtn && m_displayModeBtn->isChecked();
    m_fitToScreen = !stretchMode;
    setProperty("fitToScreen", m_fitToScreen);
    if (m_displayModeBtn)
        m_displayModeBtn->update();
    refreshAllDisplays();
}

void MainWindow::onPixelStretchToggled(bool checked)
{
    m_pixelStretch = checked;
    setProperty("pixelStretch", m_pixelStretch);
    refreshAllDisplays();
}

void MainWindow::onDarkModeToggled()
{
    setDarkMode(m_darkModeBtn ? m_darkModeBtn->isChecked() : !m_darkMode);
}

void MainWindow::setDarkMode(bool dark)
{
    m_darkMode = dark;
    if (m_darkModeBtn && m_darkModeBtn->isChecked() != dark) {
        const QSignalBlocker blocker(m_darkModeBtn);
        m_darkModeBtn->setChecked(dark);
    }

    applyStyles();

    if (layout())
        layout()->invalidate();

    applyControlVisibility();

    saveSettings();
}


void MainWindow::applyControlVisibility()
{
    if (m_toggleControlsBtn)
        m_toggleControlsBtn->setText(m_controlsHidden ? " Show Controls " : " Hide Controls ");

    const int totalW = std::max(1, width());
    const int defaultControlsW = 300;
    const int dualControlW = 480;
    const int controlsW = (m_currentPreset == PresetLayout::DualControl) ? dualControlW : defaultControlsW;

    switch (m_currentPreset)
    {
    case PresetLayout::SideBySide:
    case PresetLayout::TopBottom:
    case PresetLayout::DualControl:
        if (m_sidebarScroll)
            m_sidebarScroll->setVisible(!m_controlsHidden);
        if (m_mainSplitter)
            m_mainSplitter->setSizes(
                m_controlsHidden
                    ? QList<int>{0, totalW}
                    : QList<int>{controlsW, std::max(1, totalW - controlsW)});
        break;
    case PresetLayout::ControlsTop:
        if (m_sidebarScroll)
            m_sidebarScroll->hide();
        if (m_topControlsHost)
            m_topControlsHost->setVisible(!m_controlsHidden);
        if (m_controlsTopSplitter)
            m_controlsTopSplitter->setSizes(m_controlsHidden ? QList<int>{0, 1080} : QList<int>{150, 930});
        if (m_mainSplitter)
            m_mainSplitter->setSizes({0, 1920});
        break;
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    saveSettings();
    e->accept();
}

QString MainWindow::getPreferencesFilePath() const
{
    return SettingsManager::getPreferencesFilePath();
}

void MainWindow::saveSettings()
{
    GeneralAppSettings general;
    general.darkMode = m_darkMode;
    general.currentPreset = static_cast<int>(m_currentPreset);
    general.geometry = saveGeometry();
    general.windowState = saveState();
    general.displayStretch = m_displayModeBtn ? m_displayModeBtn->isChecked() : false;
    general.pixelStretch = m_pixelStretchCheck ? m_pixelStretchCheck->isChecked() : false;
    general.saveModeMerged = m_saveModeToggleBtn ? m_saveModeToggleBtn->isChecked() : false;
    general.lineDelay = getLineDelayValue();
    general.timeDelay = m_timeDelaySpin ? m_timeDelaySpin->value() : 0.0;
    general.calibMode = m_calibModeCombo ? m_calibModeCombo->currentIndex() : 0;

    std::vector<QString> regionModes[2];
    std::vector<QString> regionOffsets[2];
    std::vector<int> regionBinnings[2];

    for (int i = 0; i < 2; ++i) {
        regionModes[i].resize(7);
        regionOffsets[i].resize(7);
        regionBinnings[i].resize(7, 0);
        if (m_panels[i]) {
            for (int r = 0; r < 7; ++r) {
                if (m_panels[i]->regionModeCombos[r])
                    regionModes[i][r] = m_panels[i]->regionModeCombos[r]->currentText();
                if (m_panels[i]->regionOffsetEdits[r])
                    regionOffsets[i][r] = m_panels[i]->regionOffsetEdits[r]->text();
                if (m_panels[i]->regionBinningCombos[r])
                    regionBinnings[i][r] = m_panels[i]->regionBinningCombos[r]->currentIndex();
            }
        }
    }

    SettingsManager::saveSettings(general, m_cameraUiStates, regionModes, regionOffsets, regionBinnings);
}

void MainWindow::loadSettings()
{
    GeneralAppSettings general;
    general.darkMode = m_darkMode;
    general.currentPreset = static_cast<int>(m_currentPreset);

    std::vector<QString> regionModes[2];
    std::vector<QString> regionOffsets[2];
    std::vector<int> regionBinnings[2];
    for (int i = 0; i < 2; ++i) {
        regionModes[i].resize(7);
        regionOffsets[i].resize(7);
        regionBinnings[i].resize(7, 0);
    }

    SettingsManager::loadSettings(general, m_cameraUiStates, regionModes, regionOffsets, regionBinnings);

    m_darkMode = general.darkMode;
    if (m_darkModeBtn) {
        m_darkModeBtn->blockSignals(true);
        m_darkModeBtn->setChecked(m_darkMode);
        m_darkModeBtn->blockSignals(false);
    }

    if (!general.geometry.isEmpty()) restoreGeometry(general.geometry);
    if (!general.windowState.isEmpty()) restoreState(general.windowState);

    m_currentPreset = static_cast<PresetLayout>(general.currentPreset);
    if (m_layoutCombo) {
        m_layoutCombo->blockSignals(true);
        m_layoutCombo->setCurrentIndex(general.currentPreset);
        m_layoutCombo->blockSignals(false);
    }

    switch (m_currentPreset) {
        case PresetLayout::TopBottom: applyPresetTopBottom(); break;
        case PresetLayout::DualControl: applyPresetDualControl(); break;
        case PresetLayout::ControlsTop: applyPresetControlsTop(); break;
        default: applyPresetSideBySide(); break;
    }

    if (m_displayModeBtn) {
        m_displayModeBtn->blockSignals(true);
        m_displayModeBtn->setChecked(general.displayStretch);
        m_displayModeBtn->blockSignals(false);
        m_fitToScreen = general.displayStretch;
        setProperty("fitToScreen", m_fitToScreen);
    }

    if (m_pixelStretchCheck) {
        m_pixelStretchCheck->blockSignals(true);
        m_pixelStretchCheck->setChecked(general.pixelStretch);
        m_pixelStretchCheck->blockSignals(false);
        m_pixelStretch = general.pixelStretch;
        setProperty("pixelStretch", m_pixelStretch);
    }

    if (m_saveModeToggleBtn) {
        m_saveModeToggleBtn->blockSignals(true);
        m_saveModeToggleBtn->setChecked(general.saveModeMerged);
        m_saveModeToggleBtn->blockSignals(false);
    }

    setLineDelayValue(general.lineDelay);
    if (m_timeDelaySpin) m_timeDelaySpin->setValue(general.timeDelay);
    if (m_calibModeCombo) m_calibModeCombo->setCurrentIndex(general.calibMode);

    for (int i = 0; i < 2; ++i) {
        applyCameraUiStateToWidgets(i);
        if (m_panels[i]) {
            for (int r = 0; r < 7; ++r) {
                if (m_panels[i]->regionModeCombos[r] && !regionModes[i][r].isEmpty())
                    m_panels[i]->regionModeCombos[r]->setCurrentText(regionModes[i][r]);
                if (m_panels[i]->regionOffsetEdits[r] && !regionOffsets[i][r].isEmpty())
                    m_panels[i]->regionOffsetEdits[r]->setText(regionOffsets[i][r]);
                if (m_panels[i]->regionBinningCombos[r])
                    m_panels[i]->regionBinningCombos[r]->setCurrentIndex(regionBinnings[i][r]);
            }
        }
    }
}

void MainWindow::resetAllValuesToDefault()
{
    GeneralAppSettings general;
    std::vector<QString> regionModes[2];
    std::vector<QString> regionOffsets[2];
    std::vector<int> regionBinnings[2];
    for (int i = 0; i < 2; ++i) {
        regionModes[i].resize(7);
        regionOffsets[i].resize(7);
        regionBinnings[i].resize(7, 0);
    }

    SettingsManager::resetAllValuesToDefault(general, m_cameraUiStates, regionModes, regionOffsets, regionBinnings);

    m_darkMode = general.darkMode;
    if (m_darkModeBtn) {
        m_darkModeBtn->setChecked(m_darkMode);
    }

    applyPresetSideBySide();

    if (m_displayModeBtn) {
        m_displayModeBtn->setChecked(false);
        m_fitToScreen = false;
        setProperty("fitToScreen", false);
    }

    if (m_pixelStretchCheck) {
        m_pixelStretchCheck->setChecked(false);
        m_pixelStretch = false;
        setProperty("pixelStretch", false);
    }

    if (m_saveModeToggleBtn) {
        m_saveModeToggleBtn->setChecked(false);
    }

    setLineDelayValue(0);
    if (m_timeDelaySpin) m_timeDelaySpin->setValue(0.0);
    if (m_calibModeCombo) m_calibModeCombo->setCurrentIndex(0);

    for (int i = 0; i < 2; ++i) {
        applyCameraUiStateToWidgets(i);
        if (m_panels[i]) {
            for (int r = 0; r < 7; ++r) {
                if (m_panels[i]->regionModeCombos[r])
                    m_panels[i]->regionModeCombos[r]->setCurrentText(regionModes[i][r]);
                if (m_panels[i]->regionOffsetEdits[r])
                    m_panels[i]->regionOffsetEdits[r]->setText(regionOffsets[i][r]);
                if (m_panels[i]->regionBinningCombos[r])
                    m_panels[i]->regionBinningCombos[r]->setCurrentIndex(0);
            }
        }
        updateWorkerParams(i);
    }

    applyStyles();
    saveSettings();
    if (statusBar()) statusBar()->showMessage("All settings reset to default values.", 3000);
}

// ── Auto-detect cameras ───────────────────────────────────────────────
void MainWindow::detectCameras()
{
    if (m_cameraManager) {
        m_cameraManager->detectHardware();
    }
}

// ── Styles ────────────────────────────────────────────────────────────
void MainWindow::applyStyles()
{
    ThemeManager::applyTheme(this, m_darkMode);

#ifdef HAS_OFFLINE_VIEWER
    if (m_offlineViewer) {
        m_offlineViewer->setDarkMode(m_darkMode);
    }
#endif
}

void MainWindow::onStartCalibrationClicked()
{
    if (!m_calibrationEngine) return;

    if (m_calibrationEngine->isCalibrating()) {
        m_calibrationEngine->stopCalibration();
        if (m_startCalibrationBtn) m_startCalibrationBtn->setText(" Start ");
        if (m_markCam1Btn) m_markCam1Btn->setVisible(false);
        if (m_markCam2Btn) m_markCam2Btn->setVisible(false);
        if (m_calibModeCombo) m_calibModeCombo->setEnabled(true);
    } else {
        if (!m_streaming[0] || !m_streaming[1]) {
            QMessageBox::warning(this, "Calibration", "Please start streaming BOTH cameras before starting calibration.");
            return;
        }

        bool isAutoDetect = (m_calibModeCombo && m_calibModeCombo->currentIndex() == 2);
        setLineDelayValue(0);
        if (m_timeDelaySpin) m_timeDelaySpin->setValue(0.0);
        if (m_calibModeCombo) m_calibModeCombo->setEnabled(false);

        m_calibrationEngine->startCalibration(isAutoDetect);

        if (isAutoDetect) {
            if (m_startCalibrationBtn) m_startCalibrationBtn->setText(" Waiting for CAM1... ");
            if (m_markCam1Btn) m_markCam1Btn->setVisible(false);
            if (m_markCam2Btn) m_markCam2Btn->setVisible(false);
        } else {
            if (m_startCalibrationBtn) m_startCalibrationBtn->setText(" Stop ");
            if (m_markCam1Btn) {
                m_markCam1Btn->setText(" 🎯 MARK CAM1 LINE ");
                m_markCam1Btn->setEnabled(true);
                m_markCam1Btn->setVisible(true);
            }
            if (m_markCam2Btn) {
                m_markCam2Btn->setText(" 🎯 MARK CAM2 LINE ");
                m_markCam2Btn->setEnabled(false);
                m_markCam2Btn->setVisible(true);
            }
        }
    }
}

void MainWindow::onMarkCam1Clicked()
{
    if (!m_calibrationEngine) return;
    m_calibrationEngine->markCam1();
    if (m_markCam1Btn) {
        m_markCam1Btn->setText(" ✔ CAM1 Marked ");
        m_markCam1Btn->setEnabled(false);
    }
    if (m_markCam2Btn) {
        m_markCam2Btn->setEnabled(true);
    }
}

void MainWindow::onMarkCam2Clicked()
{
    if (!m_calibrationEngine) return;
    m_calibrationEngine->markCam2();
    if (m_calibModeCombo) m_calibModeCombo->setCurrentIndex(0);
}

int MainWindow::getLineDelayValue() const
{
    return m_lineDelaySpin ? m_lineDelaySpin->currentText().toInt() : 0;
}

void MainWindow::setLineDelayValue(int val)
{
    if (!m_lineDelaySpin) return;
    QString str = QString::number(val);
    int idx = m_lineDelaySpin->findText(str);
    if (idx < 0) {
        m_lineDelaySpin->addItem(str);
    }
    m_lineDelaySpin->setEditText(str);
}

int MainWindow::getActiveRegionOffset(int idx) const
{
    if (idx < 0 || idx > 1 || !m_panels[idx]) return 0;
    for (int i = 0; i < 7; ++i) {
        if (m_panels[idx]->regionModeCombos[i] && m_panels[idx]->regionModeCombos[i]->currentText() == "On") {
            if (m_panels[idx]->regionOffsetEdits[i]) {
                return m_panels[idx]->regionOffsetEdits[i]->text().toInt();
            }
        }
    }
    return 0;
}

bool MainWindow::isMergeSaving() const
{
    return m_captureController && m_captureController->isMergeSaving();
}
