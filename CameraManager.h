#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <vector>
#include <set>
#include <memory>
#include "CameraWorker.h"

#if !USING_STUB
#include "KYFGLibA.h"
#endif

class CameraManager : public QObject
{
    Q_OBJECT
public:
    explicit CameraManager(QObject* parent = nullptr);
    ~CameraManager();

    void initialize();
    void shutdown();

    void detectHardware();
    int getDetectedCameraCount() const { return m_detectedCameraCount; }
    QStringList getDetectedCameraNames() const { return m_detectedCameraNames; }

    void assignCameraSource(int slot, int detectedIndex);

    bool isConnected(int idx) const;
    bool isStreaming(int idx) const;

    void connectCamera(int idx);
    void disconnectCamera(int idx);
    void startStream(int idx);
    void stopStream(int idx);

    void applyWorkerParams(int idx, const CameraParams& params);
    void setLiveFps(int idx, double fps);
    void setLiveExposure(int idx, double exposureUs);
    void setLiveGain(int idx, double gainDb);
    void setLiveTestPattern(int idx, int pattern);
    void setLiveReverseX(int idx, bool rev);
    void setLiveReverseY(int idx, bool rev);
    void setLiveTdi(int idx, int mode, int stages, int yShift, int frameCount);
    void applyGeometry(int idx, int width, int height);
    void setSaving(int idx, bool saving);

signals:
    void frameReady(std::shared_ptr<FramePacket> pkt);
    void logMessage(const QString& msg);
    void streamStarted(int idx);
    void streamStopped(int idx);
    void cameraOpened(int idx, int width, int height);
    void cameraClosed(int idx);
    void paramsQueried(int idx, const CameraParams& params);
    void errorOccurred(int idx, const QString& msg);
    void temperatureReady(int idx, float sensor, float grabber);
    void fpsClamped(int idx, double requestedFps, double maxFps);
    void hardwareDetected(int count, const QStringList& cameraNames);
    void statusMessage(const QString& msg, int timeoutMs);

private slots:
    void onWorkerFrameReady(std::shared_ptr<FramePacket> pkt);
    void onWorkerLogMessage(const QString& msg);
    void onWorkerStreamStarted(int idx);
    void onWorkerStreamStopped(int idx);
    void onWorkerCameraOpened(int idx, int width, int height);
    void onWorkerCameraClosed(int idx);
    void onWorkerParamsQueried(int idx, const CameraParams& params);
    void onWorkerErrorOccurred(int idx, const QString& msg);
    void onWorkerTemperatureReady(int idx, float sensor, float grabber);
    void onWorkerFpsClamped(int idx, double requestedFps, double maxFps);

private:
    CameraWorker* m_workers[2] = {nullptr, nullptr};
    QThread*      m_workerThreads[2] = {nullptr, nullptr};
    bool          m_connected[2] = {false, false};
    bool          m_streaming[2] = {false, false};

#if !USING_STUB
    std::vector<FGHANDLE> m_fgHandles;
    CAMHANDLE     m_camHandles[2] = {INVALID_CAMHANDLE, INVALID_CAMHANDLE};
    CAMHANDLE     m_detectedCamHandles[KY_MAX_CAMERAS] = {};
    FGHANDLE      m_detectedCamFgs[KY_MAX_CAMERAS] = {};
#endif
    int           m_detectedCameraCount = 0;
    QStringList   m_detectedCameraNames;
};

