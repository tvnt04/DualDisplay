#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  CameraPanel.h  —  Controls + live view for ONE camera
// ═══════════════════════════════════════════════════════════════════════

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDateTime>
#include <vector>
#include "CameraWorker.h"

class CameraPanel : public QFrame
{
    Q_OBJECT
public:
    explicit CameraPanel(int camIndex, QWidget* parent = nullptr);

    void setConnected(bool v);
    void setStreaming (bool v);
    void setSaving   (bool v);

    // Returns true if the frame was painted here (false if detached)
    bool updateFrame(const FramePacket& pkt, bool fitToScreen, bool stretch, QSize targetSize = QSize(), bool isNewFrame = true);
    void updateFrameInfo(const FramePacket& pkt, const std::vector<uint8_t>& pixels);
    void clearFrame();

    CameraParams collectParams() const;
    void disableFeature(const QString& featureName);

    // ── Public widget refs ──────────────────────────────────────────
    QLabel*      titleLabel;
    QLabel*      statusLabel;
    QLabel*      fpsLabel;
    QLabel*      sensorTempLabel;
    QLabel*      grabberTempLabel;
    QScrollArea* scrollArea;
    QLabel*      imageLabel;

    QLineEdit*   savePathEdit;
    QPushButton* browseSaveBtn;
    QComboBox*   saveFormatCombo;
    QComboBox*   saveModeCombo;
    QCheckBox*   stopAfterDurationCheck;
    QLineEdit*   saveDurationEdit;
    QLineEdit*   widthEdit;
    QLineEdit*   heightEdit;
    QLineEdit*   fpsEdit;
    QLineEdit*   exposureEdit;
    QLineEdit*   gainEdit;
    QComboBox*   testPatternCombo;
    QComboBox*   tdiModeCombo;
    QLineEdit*   tdiStagesEdit;
    QLineEdit*   tdiYShiftEdit;
    QLineEdit*   tdiFrameCountEdit;
    QLabel*      tdiStagesLabel;
    QLabel*      tdiYShiftLabel;
    QLabel*      tdiFrameCountLabel;
    QCheckBox*   reverseXCheck;
    QCheckBox*   reverseYCheck;
    QComboBox*   regionModeCombos[7];
    QLineEdit*   regionOffsetEdits[7];
    QComboBox*   regionBinningCombos[7];
    QPushButton* connectBtn;
    QPushButton* startBtn;
    QPushButton* saveBtn;
    QPushButton* playbackBtn;
    QPushButton* copySettingsBtn;
    QLineEdit*   rawPathEdit;
    QPushButton* browseRawBtn;

    // Last rendered pixmap (used by DetachedView)
    QPixmap      lastPixmap;
    int          lastFrameId        = 0;
    int          lastBrightestPixel = 0;
    double       lastFps            = 0.0;

    bool isDetached = false;   // true when Cam1 is on monitor 2

signals:
    void connectClicked(int idx);
    void startClicked  (int idx);
    void saveClicked   (int idx);
    void playbackClicked(int idx);
    void browseSave    (int idx);
    void browseRaw     (int idx);
    void copySettingsClicked(int idx);

public:
    void setPlaying(bool v);
    void syncBinningControlsWithRegionModes();

private slots:
    void onToggleRegions();

private:
    int          m_camIndex;
    bool         m_regionsVisible = false;
    QGroupBox*   m_regionGroup;
    QPushButton* m_toggleRegionBtn;

    int          m_fpsFrameCount = 0;
    qint64       m_fpsLastMs     = 0;
};
