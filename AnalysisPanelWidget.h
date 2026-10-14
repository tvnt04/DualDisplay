#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QPoint>
#include <QVector>
#include <QPainter>
#include <QPainterPath>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSize>

#include <QSpinBox>

class AnalysisGraphCanvas : public QWidget
{
    Q_OBJECT
public:
    enum AnalysisMode {
        ModeFullHistogram,
        ModeROIHistogram,
        ModeLineProfile
    };

    enum ChannelMode {
        ChannelLuminance,
        ChannelRed,
        ChannelGreen,
        ChannelBlue
    };

    explicit AnalysisGraphCanvas(QWidget* parent = nullptr);

    void setMode(AnalysisMode mode);
    void setChannel(ChannelMode channel);
    void setLogScale(bool logScale);
    void setDarkMode(bool dark);
    bool isDarkMode() const { return m_darkMode; }

    void updateImageData(const QImage& image, const QRect& roi = QRect(), const QPoint& crosshair = QPoint());
    
    AnalysisMode getMode() const { return m_mode; }
    ChannelMode getChannel() const { return m_channel; }
    bool isLogScale() const { return m_logScale; }

    int getMinVal() const { return m_minVal; }
    int getMaxVal() const { return m_maxVal; }
    double getMeanVal() const { return m_meanVal; }
    double getStdDevVal() const { return m_stdDevVal; }
    double getUnderExposurePct() const { return m_underPct; }
    double getOverExposurePct() const { return m_overPct; }

signals:
    void statsUpdated();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void computeHistogram(const QImage& img, const QRect& targetRoi);
    void computeLineProfiles(const QImage& img, const QPoint& pt);

    AnalysisMode m_mode = ModeFullHistogram;
    ChannelMode m_channel = ChannelLuminance;
    bool m_logScale = false;
    bool m_darkMode = true;

    // Histogram Bins (256 for 8-bit)
    QVector<quint64> m_histLum;
    QVector<quint64> m_histR;
    QVector<quint64> m_histG;
    QVector<quint64> m_histB;
    quint64 m_maxBinCount = 1;

    // Line Profiles
    QVector<uint8_t> m_rowProfile; // Width profile
    QVector<uint8_t> m_colProfile; // Height profile
    QPoint m_crosshairPt;

    // Statistics
    int m_minVal = 0;
    int m_maxVal = 255;
    double m_meanVal = 0.0;
    double m_stdDevVal = 0.0;
    double m_underPct = 0.0;
    double m_overPct = 0.0;
};

class AnalysisPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AnalysisPanelWidget(QWidget* parent = nullptr);

    // Use QPoint(-1,-1) as sentinel meaning "don't override existing crosshair"
    void processImageFrame(const QImage& frame, const QRect& roi = QRect(), const QPoint& crosshair = QPoint(-1, -1));
    // Feed CAM1 frame in dual-camera mode
    void processSecondCameraFrame(const QImage& frame);
    void setDualCameraMode(bool dual);
    bool isDualCameraMode() const { return m_dualCamera; }

    void setROI(const QRect& roi);
    void setCrosshair(const QPoint& pt, int cameraIndex = -1);
    void setDarkMode(bool dark);
    bool isDarkMode() const { return m_darkMode; }

    AnalysisGraphCanvas::AnalysisMode getMode() const;
    QRect getROI() const { return m_currentROI; }
    QPoint getCrosshair() const;
    QPoint getCrosshairForCamera(int cameraIndex) const;
    int getActiveCameraIndex() const { return m_activeCamera; }

signals:
    void closeRequested();

private slots:
    void onModeChanged(int index);
    void onChannelChanged(int index);
    void onLogScaleToggled(bool checked);
    void onRowSpinChanged(int val);
    void onColSpinChanged(int val);
    void onCameraRowSpinChanged(int cameraIndex, int val);
    void onCameraColSpinChanged(int cameraIndex, int val);
    void updateStatsUI();

private:
    void applyCanvasDarkMode(AnalysisGraphCanvas* canvas, bool dark);
    void syncSpinBoxesToActiveCamera();
    void syncCameraControls(int cameraIndex);
    QString statsTextForCanvas(AnalysisGraphCanvas* canvas) const;

    QLabel* m_titleLabel = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_channelCombo = nullptr;
    QPushButton* m_logScaleBtn = nullptr;

    QLabel* m_rowLabel = nullptr;
    QSpinBox* m_rowSpin = nullptr;
    QLabel* m_colLabel = nullptr;
    QSpinBox* m_colSpin = nullptr;

    QPushButton* m_closeBtn = nullptr;

    QLabel* m_statsLabel = nullptr;
    QLabel* m_cameraStatsLabels[2] = {nullptr, nullptr};
    QLabel* m_cameraRowLabels[2] = {nullptr, nullptr};
    QSpinBox* m_cameraRowSpin[2] = {nullptr, nullptr};
    QLabel* m_cameraColLabels[2] = {nullptr, nullptr};
    QSpinBox* m_cameraColSpin[2] = {nullptr, nullptr};

    // CAM0 canvas (always present)
    AnalysisGraphCanvas* m_canvas = nullptr;
    // CAM1 canvas (shown only in dual-camera mode)
    AnalysisGraphCanvas* m_canvas2 = nullptr;
    QWidget* m_dualRow = nullptr;       // row that holds both canvases side-by-side
    QLabel* m_cam0Label = nullptr;
    QLabel* m_cam1Label = nullptr;

    QImage m_currentFrame;
    QImage m_currentFrame2;  // CAM1 frame
    QRect  m_currentROI;
    QPoint m_cameraCrosshair[2] = {QPoint(-1, -1), QPoint(-1, -1)};
    QPoint m_currentCrosshair;
    QSize  m_frameSize[2];
    int    m_activeCamera = 0;
    bool   m_darkMode = true;
    bool   m_dualCamera = false;
};
