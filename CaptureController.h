#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <cstdio>
#include "CameraWorker.h"
#include "MergeSaver.h"

struct CaptureSaveParams {
    QString savePath;
    QString saveFormat; // ".raw" or ".bmp"
    QString saveMode;   // "Single File" or "Sequence"
    int width = 9344;
    int height = 3840;
    double fps = 1.0;
    double exposure = 29999.0;
    double gain = 1.0;
    QString tdiMode = "Off";
    int tdiStages = 4;
    int tdiFrameCount = 10;
    bool stopAfterEnabled = false;
    double saveDuration = 5.0;
    float sensorTemp = 0.0f;
    float grabberTemp = 0.0f;
};

class CaptureController : public QObject
{
    Q_OBJECT
public:
    explicit CaptureController(QObject* parent = nullptr);
    ~CaptureController();

    // Single camera recording
    bool isSaving(int idx) const;
    bool startSave(int idx, const CaptureSaveParams& params, const QString& initialLogText = QString());
    void stopSave(int idx, bool completed = false);
    void writeFrame(int idx, std::shared_ptr<FramePacket> pkt);

    // Merged recording
    bool isMergeSaving() const;
    bool startMergeSave(const QString& sessionDir, const MergeSaveOptions& opts);
    void stopMergeSave();
    bool pushMergedPair(std::shared_ptr<FramePacket> pkt0, std::shared_ptr<FramePacket> pkt1);

    void appendTelemetry(const QString& line);

signals:
    void saveStateChanged(int idx, bool saving);
    void mergeSaveStateChanged(bool saving);
    void countdownUpdated(int idx, const QString& countdownText);
    void statusMessage(const QString& msg, int timeoutMs);
    void logMessage(const QString& msg);
    void mergeStatsUpdated(quint64 savedCount, quint64 droppedCount, quint64 queueDepth);

private slots:
    void onStreamCountdown(int idx);

private:
    void writeSaveMetadata(int idx, bool isCaptureStart = false, const QString& initialLogText = QString());
    void stopStreamCountdown(int idx);

    // Per-camera saving state
    bool        m_saving[2] = {false, false};
    FILE*       m_saveFiles[2] = {nullptr, nullptr};
    QString     m_saveDirs[2];
    CaptureSaveParams m_saveParams[2];
    int         m_saveFrameCount[2] = {0, 0};
    qint64      m_firstSavedFrameMs[2] = {0, 0};
    qint64      m_lastSavedFrameMs[2] = {0, 0};
    int         m_savedFrameWidth[2] = {0, 0};
    int         m_savedFrameHeight[2] = {0, 0};
    int         m_emptyRawFrameSkips[2] = {0, 0};

    // Auto-stop countdown timers
    QTimer*     m_countdownTimers[2] = {nullptr, nullptr};
    qint64      m_streamEndMs[2] = {0, 0};

    // Merged saving
    MergeSaver* m_mergeSaver = nullptr;
    bool        m_mergeSaving = false;
    QString     m_mergedSaveDir;
};

