#pragma once

#include <QWidget>
#include <QTimer>
#include <QFile>
#include <QLabel>

#include <QSlider>
#include <QScrollArea>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QAbstractButton>
#include <QComboBox>
#include <QDialog>
#include <QTabWidget>
#include <QVector>
#include <QPair>
#include <QMap>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "AnalysisPanelWidget.h"

class OfflineViewer : public QWidget
{
    Q_OBJECT
public:
    explicit OfflineViewer(QWidget* parent = nullptr);
    ~OfflineViewer();
    void setDarkMode(bool dark);

signals:
    void themeToggleRequested(bool dark);

private slots:
    void onOfflineBrowse();
    void onOfflinePlayClicked(int idx);
    void onOfflinePauseClicked(int idx);
    void onOfflineStopClicked(int idx);
    void onOfflinePlayAllClicked();
    void onOfflinePauseAllClicked();
    void onOfflineStopAllClicked();
    void applyOfflinePresetSideBySide();
    void applyOfflinePresetTopBottom();
    void applyOfflinePresetDetached();
    void applyOfflinePresetDualControl();
    void onPlaybackTimer(int idx);
    void onDisplaySettingsChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onPlaybackClicked(int idx);
    void restoreViewsAndControlsFlat();
    void stopPlayback(int idx, bool completed);
    bool loadPlaybackMetadata(const QString& path, int idx);
    void renderOfflineFrame(int idx, const QByteArray& pixels, int width, int height);
    void updateOfflineLogTerminal();
    void showStatusMsg(const QString& msg, int timeout = 3000);

    // Playback state variables
    bool          m_playing[2] = {false, false};
    QTimer*       m_playbackTimers[2] = {nullptr, nullptr};
    int           m_playbackIndex[2] = {0, 0};
    QFile*        m_playbackFile[2] = {nullptr, nullptr};
    QStringList   m_playbackFiles[2];
    qint64        m_playbackFrameSize[2] = {0, 0};
    bool          m_playbackRawSequence[2] = {false, false};
    bool          m_is10BitPacked[2] = {false, false};

    void applyMergedSingleViewLayout(bool merged);
    bool          m_isMergedPlayback = false;
    // True when merged file contains 2 cameras (width = 2 × singleCamWidth)
    bool          m_isMergedDualCam = false;
    int           m_singleCamWidth  = 9344;  // width of one camera in the merged frame

    // Metadata cache for offline playback
    struct PlaybackMetadata {
        int width = 0;
        int height = 0;
        double fps = 10.0;
        QString saveFormat = ".raw";
        QString saveMode = "Single File";
        QString pixelEncoding;
        QString rawPath;
        bool isMerged = false;
    };
    PlaybackMetadata m_playbackMetadata[2];

    // Log replaying variables
    QList<QPair<int, QString>> m_offlineLogs[2];
    int           m_offlineLogPrintIndex[2] = {0, 0};
    int           m_offlineLogStartTime[2] = {0, 0};
    int           m_offlineLogSaveStartSec[2] = {0, 0};
    int           m_lastOfflineLogSecond = -1;

    // UI elements
    QSplitter*    m_offlineViewSplitter = nullptr;
    QWidget*      m_offlineViewWidgets[2] = {nullptr, nullptr};
    QScrollArea*  m_offlineViewScrolls[2] = {nullptr, nullptr};
    QLabel*       m_offlineViewLabels[2] = {nullptr, nullptr};
    QLabel*       m_offlineViewTitleLabels[2] = {nullptr, nullptr};
    QLabel*       m_offlineViewFpsLabels[2] = {nullptr, nullptr};
    QLabel*       m_offlineViewSensorTempLabels[2] = {nullptr, nullptr};
    QLabel*       m_offlineViewGrabberTempLabels[2] = {nullptr, nullptr};
    QLabel*       m_offlineViewStatusLabels[2] = {nullptr, nullptr};
    QSlider*      m_offlineSliders[2] = {nullptr, nullptr};
    QLabel*       m_offlineTimeLabels[2] = {nullptr, nullptr};

    QWidget*      m_offlineHost = nullptr;
    QHBoxLayout*  m_offlineHostLayout = nullptr;
    QLineEdit*    m_offlinePathEdit = nullptr;
    QPushButton*  m_offlineBrowseBtn = nullptr;
    QPushButton*  m_offlinePlayAllBtn = nullptr;
    QPushButton*  m_offlinePauseAllBtn = nullptr;
    QPushButton*  m_offlineStopAllBtn = nullptr;
    QPushButton*  m_offlinePlayBtn[2] = {nullptr, nullptr};
    QPushButton*  m_offlinePauseBtn[2] = {nullptr, nullptr};
    QPushButton*  m_offlineStopBtn[2] = {nullptr, nullptr};
    QComboBox*    m_offlineLayoutCombo = nullptr;
    QPushButton*  m_offlineAnalysisBtn = nullptr;
    QAbstractButton* m_offlineThemeBtn = nullptr;
    AnalysisPanelWidget* m_analysisPanel = nullptr;
    QDialog*      m_offlineDetachedView = nullptr;
    QPlainTextEdit* m_offlineLogTerminal = nullptr;
    QTabWidget*   m_offlineViewTabs = nullptr;
    QWidget*      m_offlineActiveControlContainer = nullptr;
    QWidget*      m_offlineViewContainer = nullptr;
    QWidget*      m_offlineCamControlWidgets[2] = {nullptr, nullptr};
    QWidget*      m_offlineFlatControlsWidget = nullptr;
    QFrame*       m_offlineDiv1 = nullptr;
    QSplitter*    m_viewGraphSplitter = nullptr;   // vertical splitter: image ↔ graphs

    // Cache for last rendered frame
    QByteArray    m_lastFramePixels[2];
    int           m_lastFrameWidth[2] = {0, 0};
    int           m_lastFrameHeight[2] = {0, 0};

    // ROI & Crosshair Drag Tracking
    bool          m_isDraggingROI = false;
    QPoint        m_dragStartPos;
    int           m_activeDragIdx = -1;
};
