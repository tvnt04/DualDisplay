#pragma once
#include <QMainWindow>
#include <QThread>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QStackedWidget>
#include <QDialog>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <vector>
#include <memory>
#include <cstdio>
#include <QSettings>
#include <QSpinBox>

class QFile;
#include <cstring>
#include <algorithm>
#include "CameraPanel.h"
#include "CameraWorker.h"
#include "DetachedView.h"
#include "MergeSaver.h"

#if !USING_STUB
#include "KYFGLibA.h"
#endif

using FramePacketPtr = std::shared_ptr<FramePacket>;

#ifdef HAS_OFFLINE_VIEWER
class OfflineViewer;
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private slots:
    void onConnectClicked (int idx);
    void onStartClicked   (int idx);
    void onSaveClicked    (int idx);
    void onPlaybackClicked(int idx);
    void onBrowseRaw      (int idx);
    void onBrowseSave     (int idx);
    void stopPlayback     (int idx, bool completed = false);
    void onPlaybackTimer  (int idx);
    void writeSaveMetadata(int idx, bool isCaptureStart = false);
    void appendCaptureTelemetry(const QString& line);
    bool loadPlaybackMetadata(const QString& path, int idx);
    void onFrameReady   (FramePacketPtr pkt);
    void onLogMessage   (const QString& msg);
    void onStreamStarted(int idx);
    void onStreamStopped(int idx);
    void onCameraOpened (int idx, int width, int height);
    void onParamsQueried(int idx, const CameraParams& params);
    void onCameraClosed (int idx);
    void onCameraError  (int idx, const QString& msg);
    void onTemperatureReady(int idx, float sensor, float grabber);
    void onDisplayModeToggled();
    void onDarkModeToggled();
    void onConnectAllClicked  ();
    void onStartAllClicked    ();
    void onSaveAllClicked     ();
    void onMergeSaveClicked   ();
    void onMergeSaverStats    (quint64 savedCount, quint64 droppedCount, quint64 queueDepth);
    void onToggleControlsClicked();
    void onPixelStretchToggled(bool checked);
    void onStreamCountdown      (int idx);
    void stopStreamCountdown    (int idx);
    void applyPresetSideBySide  ();
    void applyPresetTopBottom   ();
    void applyPresetDualControl ();
    void applyPresetControlsTop ();
    void applyPresetReset       ();
    void onFloatCam1ToMonitor2  ();
    void onStartCalibrationClicked();
    void onMarkCam1Clicked();
    void onMarkCam2Clicked();
    void openRegionSettingsDialog(int idx);
    void openBinningSettingsDialog(int idx);
    void copyCameraSettings(int sourceCamIdx, int targetCamIdx);
    int  getActiveRegionOffset(int idx) const;
    int  getLineDelayValue() const;
    void setLineDelayValue(int val);
private:
    struct CameraUiState {
        QString width = "9344";
        QString height = "3840";
        QString appliedWidth = "9344";
        QString appliedHeight = "3840";
        QString fps = "1.0";
        QString exposure = "29999";
        QString gain = "1.0";
        QString testPattern = "Off";
        QString tdiMode = "Off";
        QString tdiStages = "4";
        QString tdiYShift = "16";
        QString tdiFrameCount = "10";
        bool reverseX = false;
        bool reverseY = false;
        QString savePath = "Capture";
        QString rawPath = "";
        QString saveFormat = ".raw";
        QString saveMode = "Single File";
        bool stopAfterEnabled = false;
        QString saveDuration = "5.0";
    };

    struct DelayBuffer {
        std::vector<uint8_t> buffer;
        int writeIndex = 0;
        int delayLines = 0;
        int linePitch = 0;
        qint64 totalLinesPushed = 0;

        void reset(int delay_lines, int pitch) {
            delayLines = delay_lines;
            linePitch = pitch;
            int capacityLines = delayLines + 20000;
            size_t capacityBytes = static_cast<size_t>(capacityLines) * static_cast<size_t>(linePitch);
            try {
                buffer.assign(capacityBytes, 0);
            } catch (...) {
                delayLines = 0;
                buffer.clear();
            }
            writeIndex = 0;
            totalLinesPushed = 0;
        }

        FramePacketPtr process(FramePacketPtr pkt) {
            if (delayLines <= 0 || !pkt) return pkt;
            if (pkt->rawData.empty()) return pkt;
            int H = pkt->height;
            if (H <= 0) return pkt;
            if (pkt->rawData.size() % static_cast<size_t>(H) != 0) return pkt;
            const int rawLinePitch = static_cast<int>(pkt->rawData.size() / static_cast<size_t>(H));
            if (rawLinePitch <= 0) return pkt;

            const int H_img = (H > 1) ? (H - 1) : H;

            if (buffer.empty() || linePitch != rawLinePitch) {
                reset(delayLines, rawLinePitch);
            }

            int capacityLines = buffer.size() / linePitch;
            if (capacityLines < delayLines + H_img) {
                reset(delayLines, rawLinePitch);
                capacityLines = buffer.size() / linePitch;
            }
            if (capacityLines <= 0) return pkt;
            
            for (int i = 0; i < H_img; i++) {
                size_t destIdx = static_cast<size_t>(writeIndex) * static_cast<size_t>(linePitch);
                size_t srcIdx = static_cast<size_t>(i) * static_cast<size_t>(linePitch);
                memcpy(&buffer[destIdx], &pkt->rawData[srcIdx], linePitch);
                writeIndex = (writeIndex + 1) % capacityLines;
            }
            totalLinesPushed += H_img;
            
            FramePacketPtr out = std::make_shared<FramePacket>();
            out->width = pkt->width;
            out->height = pkt->height;
            out->cameraIndex = pkt->cameraIndex;
            out->frameId = pkt->frameId;
            out->timestampMs = pkt->timestampMs;
            out->packedMin = pkt->packedMin;
            out->packedMax = pkt->packedMax;
            out->rawData.resize(static_cast<size_t>(H) * static_cast<size_t>(linePitch));
            
            qint64 currentFrameStartLine = totalLinesPushed - H_img;
            
            for (int i = 0; i < H_img; i++) {
                qint64 delayedGlobalLine = (currentFrameStartLine + i) - delayLines;
                int destRow = static_cast<int>((delayedGlobalLine % H_img + H_img) % H_img);
                
                int bufSlot = static_cast<int>((writeIndex - H_img - delayLines + i) % capacityLines);
                while (bufSlot < 0) bufSlot += capacityLines;
                
                size_t destIdx = static_cast<size_t>(destRow) * static_cast<size_t>(linePitch);
                size_t srcIdx = static_cast<size_t>(bufSlot) * static_cast<size_t>(linePitch);
                memcpy(&out->rawData[destIdx], &buffer[srcIdx], linePitch);
            }
            
            if (H > H_img) {
                size_t destIdx = static_cast<size_t>(H_img) * static_cast<size_t>(linePitch);
                size_t srcIdx = static_cast<size_t>(H_img) * static_cast<size_t>(linePitch);
                memcpy(&out->rawData[destIdx], &pkt->rawData[srcIdx], linePitch);
            }
            
            return out;
        }
    };

    void buildUI    ();
    void applyStyles();
    void setDarkMode(bool dark);
    void saveSettings();
    void loadSettings();
    void refreshDisplay(int idx);
    void refreshAllDisplays();
    void stopSave(int idx, bool completed = false);
    void resetControlLayout();
    void resetViewLayout();
    void applyControlVisibility();
    void syncSidebarButtons(int idx);
    void updateWorkerParams(int idx);
    void openSettingsDialog(int idx);
    void detectCameras();
    void applyCameraSourceSelection(int slot);
    void applyCameraUiStateToWidgets(int idx);
    void updateCameraUiState(int idx, const QString& width, const QString& height, const QString& fps,
                             const QString& exposure, const QString& gain, const QString& testPattern,
                             const QString& savePath, const QString& saveFormat, const QString& saveMode,
                             const QString& rawPath = QString());
    void updateCameraAdvancedState(int idx, const QString& tdiMode, const QString& tdiStages,
                                   const QString& tdiYShift, const QString& tdiFrameCount,
                                   bool reverseX, bool reverseY,
                                   bool stopAfterEnabled, const QString& saveDuration);
    void updateTdiUiVisibility(int idx);
    QString getPreferencesFilePath() const;
    void appendLogLine(const QString& line);
    enum class PresetLayout {
        SideBySide,
        TopBottom,
        DualControl,
        ControlsTop
    };
    CameraWorker* m_workers      [2] = {};
    QThread*      m_workerThreads[2] = {};
    CameraPanel*  m_panels[2] = {};
    CameraUiState m_cameraUiStates[2];
    DelayBuffer   m_cam1DelayBuffer;
    DelayBuffer   m_cam2DelayBuffer;
    QWidget*      m_panelParking = nullptr;
    QWidget*      m_viewWidgets[2] = {};
    QLabel*       m_viewLabels [2] = {};
    QScrollArea*  m_viewScrolls[2] = {};
    QLabel*       m_viewFpsLabels[2] = {};
    QLabel*       m_viewSensorTempLabels[2] = {};
    QLabel*       m_viewGrabberTempLabels[2] = {};
    QLabel*       m_viewStatusLabels[2] = {};
    QSplitter*    m_mainSplitter = nullptr;
    QSplitter*    m_rootSplitter = nullptr;
    QPlainTextEdit* m_logTerminal = nullptr;
    QTabWidget*   m_mainTabs = nullptr;
#ifdef HAS_OFFLINE_VIEWER
    QWidget*      m_offlineTab = nullptr;
    OfflineViewer* m_offlineViewer = nullptr;
#endif
    float         m_lastSensorTemp[2] = {-999.0f, -999.0f};
    float         m_lastGrabberTemp[2] = {-999.0f, -999.0f};
    QScrollArea*  m_sidebarScroll = nullptr;
    QComboBox*    m_layoutCombo = nullptr;
    void resetAllValuesToDefault();
    QWidget*      m_sidebarContent = nullptr;
    QVBoxLayout*  m_sidebarLayout = nullptr;
    QWidget*      m_operatorPanel = nullptr;
    QLabel*       m_sidebarStateLabels[2] = {};
    QLineEdit*    m_sidebarRawEdits[2] = {};
    QLineEdit*    m_sidebarSavePathEdits[2] = {};
    QComboBox*    m_sidebarSaveModeCombos[2] = {};
    QLineEdit*    m_sidebarSaveDurationEdits[2] = {};
    QCheckBox*    m_sidebarStopAfterDurationChecks[2] = {};
    QLineEdit*    m_sidebarWidthEdits[2] = {};
    QLineEdit*    m_sidebarHeightEdits[2] = {};
    QLineEdit*    m_sidebarFpsEdits[2] = {};
    QLineEdit*    m_sidebarExposureEdits[2] = {};
    QLineEdit*    m_sidebarGainEdits[2] = {};
    QComboBox*    m_sidebarTestPatternCombos[2] = {};
    QComboBox*    m_sidebarTdiModeCombos[2] = {};
    QWidget*      m_sidebarTdiCfgRow[2] = {};
    QWidget*      m_sidebarTdiStagesGroup[2] = {};
    QWidget*      m_sidebarTdiYShiftGroup[2] = {};
    QWidget*      m_sidebarTdiFrameCountGroup[2] = {};
    QLineEdit*    m_sidebarTdiStagesEdits[2] = {};
    QLineEdit*    m_sidebarTdiYShiftEdits[2] = {};
    QLineEdit*    m_sidebarTdiFrameCountEdits[2] = {};
    QCheckBox*    m_sidebarReverseXChecks[2] = {};
    QCheckBox*    m_sidebarReverseYChecks[2] = {};
    QComboBox*    m_sidebarSaveFormatCombos[2] = {};
    QComboBox*    m_sidebarSourceCombos[2] = {};
    QPushButton*  m_sidebarConnectBtns[2] = {};
    QPushButton*  m_sidebarStartBtns[2] = {};
    QPushButton*  m_sidebarSaveBtns[2] = {};
    QPushButton*  m_sidebarSettingsBtns[2] = {};
    QPushButton*  m_sidebarBinningBtns[2] = {};
    QPushButton*  m_sidebarCopySettingsBtns[2] = {};
    QWidget*      m_topOperatorPanel = nullptr;
    QLabel*       m_topStateLabels[2] = {};
    QLineEdit*    m_topRawEdits[2] = {};
    QLineEdit*    m_topSavePathEdits[2] = {};
    QComboBox*    m_topSaveModeCombos[2] = {};
    QLineEdit*    m_topSaveDurationEdits[2] = {};
    QCheckBox*    m_topStopAfterDurationChecks[2] = {};
    QLineEdit*    m_topWidthEdits[2] = {};
    QLineEdit*    m_topHeightEdits[2] = {};
    QLineEdit*    m_topFpsEdits[2] = {};
    QLineEdit*    m_topExposureEdits[2] = {};
    QLineEdit*    m_topGainEdits[2] = {};
    QComboBox*    m_topTestPatternCombos[2] = {};
    QComboBox*    m_topTdiModeCombos[2] = {};
    QWidget*      m_topTdiCfgRow[2] = {};
    QWidget*      m_topTdiStagesGroup[2] = {};
    QWidget*      m_topTdiYShiftGroup[2] = {};
    QWidget*      m_topTdiFrameCountGroup[2] = {};
    QLineEdit*    m_topTdiStagesEdits[2] = {};
    QLineEdit*    m_topTdiYShiftEdits[2] = {};
    QLineEdit*    m_topTdiFrameCountEdits[2] = {};
    QCheckBox*    m_topReverseXChecks[2] = {};
    QCheckBox*    m_topReverseYChecks[2] = {};
    QComboBox*    m_topSaveFormatCombos[2] = {};
    QComboBox*    m_topSourceCombos[2] = {};
    QPushButton*  m_topConnectBtns[2] = {};
    QPushButton*  m_topStartBtns[2] = {};
    QPushButton*  m_topSaveBtns[2] = {};
    QPushButton*  m_topSettingsBtns[2] = {};
    QPushButton*  m_topBinningBtns[2] = {};
    QPushButton*  m_topCopySettingsBtns[2] = {};
    QWidget*      m_viewHost = nullptr;
    QVBoxLayout*  m_viewHostLayout = nullptr;
    QSplitter*    m_viewSplitter = nullptr;
    QSplitter*    m_controlsTopSplitter = nullptr;
    QWidget*      m_topControlsHost = nullptr;
    QHBoxLayout*  m_topControlsLayout = nullptr;
    QScrollArea*  m_topControlScrolls[2] = {};
    QTabWidget*   m_controlTabs = nullptr;
    QTabWidget*   m_viewTabs = nullptr;
    DetachedView* m_detachedView = nullptr;
    QAbstractButton* m_displayModeBtn = nullptr;
    QAbstractButton* m_darkModeBtn = nullptr;
    QPushButton*    m_detectBtn = nullptr;
    QPushButton*    m_connectAllBtn = nullptr;
    QPushButton*    m_startAllBtn = nullptr;
    QPushButton*    m_saveAllBtn = nullptr;
    QPushButton*    m_saveModeToggleBtn = nullptr;
    QPushButton*    m_toggleControlsBtn = nullptr;
    MergeSaver*     m_mergeSaver = nullptr;
    bool            m_mergeSaving = false;
    QString         m_mergedSaveDir;
    QComboBox*      m_lineDelaySpin;
    QDoubleSpinBox* m_timeDelaySpin;
    QComboBox*      m_calibModeCombo;
    QStackedWidget* m_syncStackedWidget;
    QPushButton*    m_startCalibrationBtn;
    QPushButton*    m_markCam1Btn;
    QPushButton*    m_markCam2Btn;
    QPushButton*    m_floatCam1Btn;
    
    bool        m_calibrationMode = false;
    bool        m_autoCam1Detected = false;
    bool        m_autoCam2Detected = false;
    qint64      m_cam1MarkLines = 0;
    qint64      m_cam2MarkLines = 0;
    qint64      m_totalLinesReceived[2] = {0, 0};
    bool        m_connected [2] = {};
    bool        m_streaming [2] = {};
    bool        m_saving    [2] = {};
    bool        m_playing   [2] = {};
    int         m_lastAppliedRegionOffset[2] = {0, 0};
    int         m_frameCount[2] = {};
    int         m_saveFrameCount[2] = {};
    qint64      m_firstSavedFrameMs[2] = {};
    qint64      m_lastSavedFrameMs[2] = {};
    int         m_savedFrameWidth[2] = {};
    int         m_savedFrameHeight[2] = {};
    int         m_emptyRawFrameSkips[2] = {};
    FILE*       m_saveFiles [2] = {};
    QString     m_saveDirs  [2];
    QTimer*     m_playbackTimers[2] = {};
    QFile*      m_playbackFile[2] = {};
    QStringList m_playbackFiles[2];
    int         m_playbackIndex[2] = {};
    qint64      m_playbackFrameSize[2] = {};
    bool        m_playbackRawSequence[2] = {};
    QTimer*     m_streamCountdownTimers[2] = {};
    qint64      m_streamEndMs [2] = {};
    QCheckBox*  m_pixelStretchCheck = nullptr;
    FramePacketPtr m_lastFrames[2];
    qint64      m_lastDisplayMs[2] = {};
    PresetLayout m_currentPreset = PresetLayout::SideBySide;
    bool        m_controlsHidden = false;
    bool        m_darkMode = false;
    bool        m_fitToScreen  = true;
    bool        m_pixelStretch = false;
    double      m_zoomFactors[2] = {1.0, 1.0};

#if !USING_STUB
    std::vector<FGHANDLE> m_fgHandles;
    CAMHANDLE     m_camHandles[2] = {INVALID_CAMHANDLE, INVALID_CAMHANDLE};
    CAMHANDLE     m_detectedCamHandles[KY_MAX_CAMERAS] = {};
    FGHANDLE      m_detectedCamFgs[KY_MAX_CAMERAS] = {};
    int           m_detectedCameraCount = 0;
#endif

signals:
    void displaySettingsChanged();
};
