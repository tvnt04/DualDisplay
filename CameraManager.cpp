#include "CameraManager.h"
#include <QCoreApplication>
#include <set>
#include <cstring>

CameraManager::CameraManager(QObject* parent)
    : QObject(parent)
{
}

CameraManager::~CameraManager()
{
    blockSignals(true);
    shutdown();
}

void CameraManager::initialize()
{
    for (int i = 0; i < 2; ++i)
    {
        if (m_workerThreads[i]) continue;

        m_workerThreads[i] = new QThread(this);
        m_workers[i]       = new CameraWorker(i);
        m_workers[i]->moveToThread(m_workerThreads[i]);

        connect(m_workers[i], &CameraWorker::frameReady,       this, &CameraManager::onWorkerFrameReady,       Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::logMessage,       this, &CameraManager::onWorkerLogMessage,       Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::streamStarted,    this, &CameraManager::onWorkerStreamStarted,    Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::streamStopped,    this, &CameraManager::onWorkerStreamStopped,    Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::cameraOpened,     this, &CameraManager::onWorkerCameraOpened,     Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::paramsQueried,    this, &CameraManager::onWorkerParamsQueried,    Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::cameraClosed,     this, &CameraManager::onWorkerCameraClosed,     Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::errorOccurred,    this, &CameraManager::onWorkerErrorOccurred,    Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::temperatureReady, this, &CameraManager::onWorkerTemperatureReady, Qt::QueuedConnection);
        connect(m_workers[i], &CameraWorker::fpsClamped,       this, &CameraManager::onWorkerFpsClamped,       Qt::QueuedConnection);

        m_workerThreads[i]->start();
    }
}

void CameraManager::shutdown()
{
    for (int i = 0; i < 2; ++i)
    {
        if (m_workers[i])
        {
            QMetaObject::invokeMethod(m_workers[i], "closeCamera", Qt::BlockingQueuedConnection);
        }

        if (m_workerThreads[i])
        {
            m_workerThreads[i]->quit();
            m_workerThreads[i]->wait();
        }

        delete m_workers[i];
        m_workers[i] = nullptr;
        m_workerThreads[i] = nullptr;
        m_connected[i] = false;
        m_streaming[i] = false;
    }

#if !USING_STUB
    std::set<FGHANDLE> closedFgs;
    for (FGHANDLE fg : m_fgHandles)
    {
        if (fg != INVALID_FGHANDLE && closedFgs.find(fg) == closedFgs.end())
        {
            closedFgs.insert(fg);
            try { KYFG_Close(fg); } catch (...) {}
        }
    }
    m_fgHandles.clear();
#endif
}

void CameraManager::detectHardware()
{
    emit statusMessage("Detecting KAYA frame grabbers & CoaXPress cameras...", 3000);
    QCoreApplication::processEvents();

#if !USING_STUB
    if (m_streaming[0] || m_streaming[1] || m_connected[0] || m_connected[1])
    {
        emit statusMessage("Stop and disconnect cameras before running detection again.", 5000);
        return;
    }

    KYFGLib_InitParameters kyInit{};
    kyInit.version = 2;
    kyInit.concurrency_mode = 0;
    kyInit.logging_mode = 0;
    kyInit.noVideoStreamProcess = KYFALSE;

    if (FGSTATUS_OK != KYFGLib_Initialize(&kyInit))
    {
        emit statusMessage("Failed to initialize KYFGLib", 5000);
        return;
    }

    std::set<FGHANDLE> closedFgs;
    for (FGHANDLE fg : m_fgHandles)
    {
        if (fg != INVALID_FGHANDLE && closedFgs.find(fg) == closedFgs.end())
        {
            closedFgs.insert(fg);
            try { KYFG_Close(fg); } catch (...) {}
        }
    }
    m_fgHandles.clear();

    int nKayaDevicesCount = 0;
    KY_DeviceScan(&nKayaDevicesCount);
    if (nKayaDevicesCount == 0)
    {
        emit statusMessage("No KAYA devices found", 5000);
        return;
    }

    KY_DEVICE_INFO* grabbersInfoArray = (KY_DEVICE_INFO*)malloc(nKayaDevicesCount * sizeof(KY_DEVICE_INFO));
    for (int i = 0; i < nKayaDevicesCount; ++i)
    {
        grabbersInfoArray[i].version = KY_MAX_DEVICE_INFO_VERSION;
        if (FGSTATUS_OK != KY_DeviceInfo(i, &grabbersInfoArray[i]))
        {
            emit statusMessage(QString("Failed to get info for device %1").arg(i), 5000);
            free(grabbersInfoArray);
            return;
        }
    }

    m_detectedCameraCount = 0;
    m_detectedCameraNames.clear();
    CAMHANDLE allCamHandles[KY_MAX_CAMERAS];
    FGHANDLE allCamFgs[KY_MAX_CAMERAS];

    for (int g = 0; g < nKayaDevicesCount; ++g)
    {
        QString deviceName = QString::fromLatin1(grabbersInfoArray[g].szDeviceDisplayName);
        if (deviceName.contains("Emulator", Qt::CaseInsensitive))
        {
            continue;
        }

        FGHANDLE fg = KYFG_Open(g);
        if (fg == INVALID_FGHANDLE)
        {
            continue;
        }
        m_fgHandles.push_back(fg);

        CAMHANDLE devCamHandles[KY_MAX_CAMERAS];
        int nDevCameras = KY_MAX_CAMERAS;
        KYFG_UpdateCameraList(fg, devCamHandles, &nDevCameras);
        QThread::msleep(200);
        nDevCameras = KY_MAX_CAMERAS;
        if (FGSTATUS_OK == KYFG_UpdateCameraList(fg, devCamHandles, &nDevCameras))
        {
            for (int c = 0; c < nDevCameras; ++c)
            {
                if (m_detectedCameraCount >= KY_MAX_CAMERAS)
                    break;

                allCamHandles[m_detectedCameraCount] = devCamHandles[c];
                allCamFgs[m_detectedCameraCount] = fg;

                KYFGCAMERA_INFO2 info{};
                info.version = 1;
                QString label;
                if (FGSTATUS_OK == KYFG_CameraInfo2(devCamHandles[c], &info))
                {
                    const QString model = QString::fromLatin1(
                        info.deviceModelName,
                        strnlen(info.deviceModelName, sizeof(info.deviceModelName))).trimmed();
                    const QString firmware = QString::fromLatin1(
                        info.deviceFirmwareVersion,
                        strnlen(info.deviceFirmwareVersion, sizeof(info.deviceFirmwareVersion))).trimmed();
                    label = QString("[%1] %2%3")
                        .arg(m_detectedCameraCount)
                        .arg(model.isEmpty() ? QString("Camera") : model)
                        .arg(firmware.isEmpty() ? QString() : QString(" FW %1").arg(firmware));
                }
                else
                {
                    label = QString("[%1] Camera").arg(m_detectedCameraCount);
                }

                m_detectedCameraNames.append(label);
                m_detectedCameraCount++;
            }
        }
    }

    free(grabbersInfoArray);

    for (int i = 0; i < m_detectedCameraCount; ++i)
    {
        m_detectedCamHandles[i] = allCamHandles[i];
        m_detectedCamFgs[i] = allCamFgs[i];
    }

    emit hardwareDetected(m_detectedCameraCount, m_detectedCameraNames);
    emit statusMessage(QString("Detected %1 camera%2. Assign sources, then connect.")
        .arg(m_detectedCameraCount)
        .arg(m_detectedCameraCount == 1 ? "" : "s"), 5000);
#else
    m_detectedCameraCount = 1;
    m_detectedCameraNames = QStringList{"[0] Camera Stub (Playback)"};
    emit hardwareDetected(m_detectedCameraCount, m_detectedCameraNames);
    emit statusMessage("Stub mode: 1 camera detected.", 3000);
#endif
}

void CameraManager::assignCameraSource(int slot, int detectedIndex)
{
    if (slot < 0 || slot > 1 || !m_workers[slot]) return;

#if !USING_STUB
    if (detectedIndex >= 0 && detectedIndex < m_detectedCameraCount)
    {
        m_camHandles[slot] = m_detectedCamHandles[detectedIndex];
        m_workers[slot]->setFgHandle(m_detectedCamFgs[detectedIndex]);
        m_workers[slot]->setCamHandle(m_camHandles[slot]);
    }
    else
    {
        m_camHandles[slot] = INVALID_CAMHANDLE;
        m_workers[slot]->setFgHandle(INVALID_FGHANDLE);
        m_workers[slot]->setCamHandle(INVALID_CAMHANDLE);
    }
#else
    if (detectedIndex >= 0)
    {
        m_workers[slot]->setFgHandle(0);
        m_workers[slot]->setCamHandle(slot);
    }
    else
    {
        m_workers[slot]->setFgHandle(0);
        m_workers[slot]->setCamHandle(INVALID_CAMHANDLE);
    }
#endif
}

bool CameraManager::isConnected(int idx) const
{
    if (idx < 0 || idx > 1) return false;
    return m_connected[idx];
}

bool CameraManager::isStreaming(int idx) const
{
    if (idx < 0 || idx > 1) return false;
    return m_streaming[idx];
}

void CameraManager::connectCamera(int idx)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "openCamera", Qt::QueuedConnection);
}

void CameraManager::disconnectCamera(int idx)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    if (m_streaming[idx]) {
        QMetaObject::invokeMethod(m_workers[idx], "stopStream", Qt::QueuedConnection);
    }
    QMetaObject::invokeMethod(m_workers[idx], "closeCamera", Qt::QueuedConnection);
}

void CameraManager::startStream(int idx)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "startStream", Qt::QueuedConnection);
}

void CameraManager::stopStream(int idx)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "stopStream", Qt::QueuedConnection);
}

void CameraManager::applyWorkerParams(int idx, const CameraParams& params)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setParams", Qt::QueuedConnection,
                              Q_ARG(CameraParams, params));
}

void CameraManager::setLiveFps(int idx, double fps)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveFps", Qt::QueuedConnection, Q_ARG(double, fps));
}

void CameraManager::setLiveExposure(int idx, double exposureUs)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveExposure", Qt::QueuedConnection, Q_ARG(double, exposureUs));
}

void CameraManager::setLiveGain(int idx, double gainDb)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveGain", Qt::QueuedConnection, Q_ARG(double, gainDb));
}

void CameraManager::setLiveTestPattern(int idx, int pattern)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveTestPattern", Qt::QueuedConnection, Q_ARG(int, pattern));
}

void CameraManager::setLiveReverseX(int idx, bool rev)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveReverseX", Qt::QueuedConnection, Q_ARG(bool, rev));
}

void CameraManager::setLiveReverseY(int idx, bool rev)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveReverseY", Qt::QueuedConnection, Q_ARG(bool, rev));
}

void CameraManager::setLiveTdi(int idx, int mode, int stages, int yShift, int frameCount)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setLiveTdi", Qt::QueuedConnection,
                              Q_ARG(int, mode), Q_ARG(int, stages), Q_ARG(int, yShift), Q_ARG(int, frameCount));
}

void CameraManager::applyGeometry(int idx, int width, int height)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "applyGeometry", Qt::QueuedConnection,
                              Q_ARG(int, width), Q_ARG(int, height));
}

void CameraManager::setSaving(int idx, bool saving)
{
    if (idx < 0 || idx > 1 || !m_workers[idx]) return;
    QMetaObject::invokeMethod(m_workers[idx], "setSaving", Qt::QueuedConnection, Q_ARG(bool, saving));
}

// ── Worker Slot Callbacks ────────────────────────────────────────────────
void CameraManager::onWorkerFrameReady(std::shared_ptr<FramePacket> pkt)
{
    emit frameReady(pkt);
}

void CameraManager::onWorkerLogMessage(const QString& msg)
{
    emit logMessage(msg);
}

void CameraManager::onWorkerStreamStarted(int idx)
{
    if (idx >= 0 && idx < 2) m_streaming[idx] = true;
    emit streamStarted(idx);
}

void CameraManager::onWorkerStreamStopped(int idx)
{
    if (idx >= 0 && idx < 2) m_streaming[idx] = false;
    emit streamStopped(idx);
}

void CameraManager::onWorkerCameraOpened(int idx, int width, int height)
{
    if (idx >= 0 && idx < 2) m_connected[idx] = true;
    emit cameraOpened(idx, width, height);
}

void CameraManager::onWorkerCameraClosed(int idx)
{
    if (idx >= 0 && idx < 2) {
        m_connected[idx] = false;
        m_streaming[idx] = false;
    }
    emit cameraClosed(idx);
}

void CameraManager::onWorkerParamsQueried(int idx, const CameraParams& params)
{
    emit paramsQueried(idx, params);
}

void CameraManager::onWorkerErrorOccurred(int idx, const QString& msg)
{
    emit errorOccurred(idx, msg);
}

void CameraManager::onWorkerTemperatureReady(int idx, float sensor, float grabber)
{
    emit temperatureReady(idx, sensor, grabber);
}

void CameraManager::onWorkerFpsClamped(int idx, double requestedFps, double maxFps)
{
    emit fpsClamped(idx, requestedFps, maxFps);
}
