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

class HardwareWorker : public QObject
{
    Q_OBJECT
public:
    explicit HardwareWorker(QObject* parent = nullptr) : QObject(parent) {}
    ~HardwareWorker() override = default;

    int detectedCameraCount() const { return m_detectedCameraCount; }
    QStringList detectedCameraNames() const { return m_detectedCameraNames; }
#if !USING_STUB
    CAMHANDLE detectedCamHandle(int i) const { return (i >= 0 && i < KY_MAX_CAMERAS) ? m_detectedCamHandles[i] : INVALID_CAMHANDLE; }
    FGHANDLE detectedCamFg(int i) const { return (i >= 0 && i < KY_MAX_CAMERAS) ? m_detectedCamFgs[i] : INVALID_FGHANDLE; }
    std::vector<FGHANDLE> fgHandles() const { return m_fgHandles; }
#endif

public slots:
    void doDetectHardware();
    void doCloseHardware();

signals:
    void hardwareDetected(int count, const QStringList& names);
    void statusMessage(const QString& msg, int timeoutMs);

private:
#if !USING_STUB
    std::vector<FGHANDLE> m_fgHandles;
    CAMHANDLE             m_detectedCamHandles[KY_MAX_CAMERAS] = {};
    FGHANDLE              m_detectedCamFgs[KY_MAX_CAMERAS] = {};
#endif
    int                   m_detectedCameraCount = 0;
    QStringList           m_detectedCameraNames;
};

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
    CAMHANDLE camHandle(int idx) const;

    bool isConnected(int idx) const;
    bool isStreaming(int idx) const;

    void connectCamera(int idx);
    void disconnectCamera(int idx);
    void startStream(int idx);
    void stopStream(int idx);
    void startAllStreams();
    void stopAllStreams();

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
    void onHardwareDetected(int count, const QStringList& cameraNames);

private:
    CameraWorker*   m_workers[2] = {nullptr, nullptr};
    QThread*        m_workerThreads[2] = {nullptr, nullptr};
    bool            m_connected[2] = {false, false};
    bool            m_streaming[2] = {false, false};

    QThread*        m_hardwareThread = nullptr;
    HardwareWorker* m_hardwareWorker = nullptr;

#if !USING_STUB
    std::vector<FGHANDLE> m_fgHandles;
    CAMHANDLE     m_camHandles[2] = {INVALID_CAMHANDLE, INVALID_CAMHANDLE};
    CAMHANDLE     m_detectedCamHandles[KY_MAX_CAMERAS] = {};
    FGHANDLE      m_detectedCamFgs[KY_MAX_CAMERAS] = {};
#endif
    int           m_detectedCameraCount = 0;
    QStringList   m_detectedCameraNames;
};

