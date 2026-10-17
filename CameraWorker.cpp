#include "CameraWorker.h"
#include "AlignedMemory.h"
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include <QMutexLocker>
#include <QDateTime>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <cmath>

namespace {
// Camera diagnostics are forwarded through logMessage to the in-app terminal.
// Do not also write them to the process stdout/launching shell.
void printTerminalLog(const QString&) {}

#if !USING_STUB
std::vector<uint8_t> unpackPackedWithKaya(const uint8_t* src, int width, int height, size_t bufferSize, const char* pixelFormatName)
{
    const int total = width * height;
    if (!src || total <= 0 || bufferSize <= 0 || !pixelFormatName)
        return {};

    KYIMGPROC_PIXEL_DECODER decoder;
    decoder.version = KYIMGPROC_PIXEL_DECODER_STRUCT_VERSION;
    decoder.pszPixelFormatName = pixelFormatName;

    KYImgProc_GetPixelFormatByName_Args pixelArgs;
    pixelArgs.uVersion = KYIMGPROC_GETPIXELFORMATBYNAME_STRUCT_VERSION;
    pixelArgs.pszPixelFormatName = pixelFormatName;
    pixelArgs.pPixelFormatDecoder = &decoder;
    if (KY_RESULT_FAILED(KYImgProc_GetPixelFormatByName(&pixelArgs)))
        return {};

    PackedDataInfo packedInfo;
    packedInfo.uVersion = KYIMGPROC_PACKEDDATAINFO_STRUCT_VERSION;

    KYImgProc_GetPackedDataInfo_Args infoArgs;
    infoArgs.uVersion = KYIMGPROC_GETPACKEDDATAINFO_STRUCT_VERSION;
    infoArgs.pPixelFormat = &decoder;
    infoArgs.ePackedModeType = DS_PACKED_DATA_MODE_LINE32ALIGNED;
    infoArgs.eProcessingModeType = DS_PACKED_DATA_PROCESSING_MODE_MSB8;
    infoArgs.uWidth = static_cast<size_t>(width);
    infoArgs.uHeight = static_cast<size_t>(height);
    infoArgs.pPackedDataInfoStruct = &packedInfo;
    if (KY_RESULT_FAILED(KYImgProc_GetPackedDataInfo(&infoArgs)) || packedInfo.uUnpackedBufferSize == 0)
        return {};

    std::vector<uint8_t> out(packedInfo.uUnpackedBufferSize);
    KYImgProc_PreproccessPackedData_Args unpackArgs;
    unpackArgs.uVersion = KYIMGPROC_PREPROCCESSPACKEDDATA_STRUCT_VERSION;
    unpackArgs.pPackedDataInfoStruct = &packedInfo;
    unpackArgs.pSourceData = src;
    unpackArgs.uSourceDataSize = bufferSize;
    unpackArgs.pDestinationData = out.data();
    unpackArgs.uDestinationDataSize = out.size();
    if (KY_RESULT_FAILED(KYImgProc_PreproccessPackedData(&unpackArgs)))
        return {};

    if (out.size() > static_cast<size_t>(total))
        out.resize(static_cast<size_t>(total));
    return out;
}
#endif
}

CameraWorker::CameraWorker(int camIndex, QObject* parent)
    : QObject(parent), m_camIndex(camIndex)
{
    memset(m_streamBuffers, 0, sizeof(m_streamBuffers));
    memset(m_pBuffers, 0, sizeof(m_pBuffers));

    m_tempTimer = new QTimer(this);
    m_tempTimer->setInterval(2000); // Poll every 2 seconds
    connect(m_tempTimer, &QTimer::timeout, this, &CameraWorker::pollTemperature);
}

CameraWorker::~CameraWorker() {
    blockSignals(true);
    closeCamera();
}

// ── processFrame slot ──────────────────────────────────────────────────
void CameraWorker::processFrame(quint64 epoch, STREAM_BUFFER_HANDLE streamBufferHandle, void* pFrameMem, size_t bufferSize)
{
    if (streamBufferHandle == NULL_STREAM_BUFFER_HANDLE)
        return;

    // Safely discard frame if this buffer belongs to an older/recreated stream session,
    // or if we've stopped streaming or closed the camera.
    if (epoch != m_streamEpoch.load() || !m_streaming || !m_cameraOpened || m_streamHandle == INVALID_STREAMHANDLE) {
        return;
    }

    try {
        if (!pFrameMem) {
            if (epoch == m_streamEpoch.load() && m_streaming && m_streamHandle != INVALID_STREAMHANDLE) {
                KYFG_BufferToQueue(streamBufferHandle, KY_ACQ_QUEUE_INPUT);
            }
            return;
        }

        unsigned char* pFrameMemory = (unsigned char*)pFrameMem;

        double instantFps = 0.0;
        if (FGSTATUS_OK == KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_INSTANTFPS, &instantFps, NULL, NULL))
        {
            static std::chrono::steady_clock::time_point lastLogTime[2];
            const auto now = std::chrono::steady_clock::now();
            if (lastLogTime[m_camIndex].time_since_epoch().count() == 0 ||
                now - lastLogTime[m_camIndex] >= std::chrono::seconds(1))
            {
                lastLogTime[m_camIndex] = now;
                const QString msg = QString("[Cam%1] Grabber instant FPS: %2")
                    .arg(m_camIndex)
                    .arg(instantFps, 0, 'f', 1);
                printTerminalLog(msg);
                emit logMessage(msg);
            }
        }

        uint32_t bufferId = 0;
        KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_ID, &bufferId, NULL, NULL);

        auto pkt = std::make_shared<FramePacket>();
        pkt->cameraIndex = m_camIndex;
        pkt->width       = m_params.width;
        pkt->height      = m_params.height;
        pkt->frameId     = m_frameId++;
        pkt->instantFps  = instantFps;
        pkt->timestampMs = QDateTime::currentMSecsSinceEpoch();

        const size_t deliveredSize = (m_actualPayloadSize > 0)
            ? std::min(bufferSize, m_actualPayloadSize)
            : bufferSize;
        if (pFrameMemory && deliveredSize > 0)
            pkt->rawData.assign(pFrameMemory, pFrameMemory + deliveredSize);

        if (pkt->frameId == 0) {
            emit logMessage(QString("[Cam%1] >>> Grabber callback delivered first frame (buffer size: %2 bytes)")
                .arg(m_camIndex).arg(deliveredSize));
        }

        emit frameReady(pkt);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in processFrame: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in processFrame").arg(m_camIndex));
    }

    if (epoch == m_streamEpoch.load() && m_streaming && m_streamHandle != INVALID_STREAMHANDLE) {
        try {
            KYFG_BufferToQueue(streamBufferHandle, KY_ACQ_QUEUE_INPUT);
        }
        catch (...) {}
    }
}

void CameraWorker::setParams(const CameraParams& p)
{
    QMutexLocker lk(&m_mutex);
    m_params = p;
}

size_t CameraWorker::queryActualPayloadSize() const
{
    if (m_streamHandle == INVALID_STREAMHANDLE)
        return m_frameDataSize;

    size_t actualPayloadSize = 0;
    const auto status = KYFG_StreamGetInfo(
        m_streamHandle,
        KY_STREAM_INFO_ACTUAL_PAYLOAD_SIZE,
        &actualPayloadSize,
        nullptr,
        nullptr);
    return (status == FGSTATUS_OK && actualPayloadSize > 0)
        ? actualPayloadSize
        : m_frameDataSize;
}

void CameraWorker::setFgHandle (FGHANDLE fg)             { m_fgHandle  = fg; }
void CameraWorker::setCamHandle(CAMHANDLE cam)           { m_camHandle = cam; }

bool CameraWorker::applyCameraParameters(bool includeGeometry)
{
    if (m_camHandle == INVALID_CAMHANDLE)
        return false;

    auto setInt = [this](const char* name, int value) {
        try {
            const auto status = KYFG_SetCameraValueInt(m_camHandle, name, value);
            if (status != FGSTATUS_OK)
            {
                const QString msg = QString("[Cam%1] %2=%3 set returned %4")
                    .arg(m_camIndex).arg(name).arg(value).arg(status);
                printTerminalLog(msg);
                emit logMessage(msg);
                if (status == 12314) emit featureUnsupported(m_camIndex, QString(name));
            }
            return status == FGSTATUS_OK || status == 12295;
        } catch (...) {
            return false;
        }
    };
    auto setFloat = [this](const char* name, double value) {
        try {
            const auto status = KYFG_SetCameraValueFloat(m_camHandle, name, value);
            if (status != FGSTATUS_OK)
            {
                const QString msg = QString("[Cam%1] %2=%3 set returned %4")
                    .arg(m_camIndex)
                    .arg(name)
                    .arg(value, 0, 'f', 3)
                    .arg(status);
                printTerminalLog(msg);
                emit logMessage(msg);
                if (status == 12314) emit featureUnsupported(m_camIndex, QString(name));
            }
            return status == FGSTATUS_OK || status == 12295;
        } catch (...) {
            return false;
        }
    };
    auto setEnum = [this](const char* name, int64_t value) {
        try {
            const auto status = KYFG_SetCameraValueEnum(m_camHandle, name, value);
            if (status != FGSTATUS_OK)
            {
                const QString msg = QString("[Cam%1] %2=%3 set returned %4")
                    .arg(m_camIndex).arg(name).arg(value).arg(status);
                printTerminalLog(msg);
                emit logMessage(msg);
                if (status == 12314) emit featureUnsupported(m_camIndex, QString(name));
            }
            return status == FGSTATUS_OK || status == 12295;
        } catch (...) {
            return false;
        }
    };
    auto setBool = [this](const char* name, bool value) {
        try {
            const auto status = KYFG_SetCameraValueBool(m_camHandle, name, value);
            if (status != FGSTATUS_OK)
            {
                const QString msg = QString("[Cam%1] %2=%3 set returned %4")
                    .arg(m_camIndex).arg(name).arg(value ? "true" : "false").arg(status);
                printTerminalLog(msg);
                emit logMessage(msg);
                if (status == 12314) emit featureUnsupported(m_camIndex, QString(name));
            }
            return status == FGSTATUS_OK || status == 12295;
        } catch (...) {
            return false;
        }
    };

    bool ok = true;

    try {
        if (includeGeometry)
        {
            ok = setEnum("TDIMode", m_params.tdiMode) && ok;
            if (m_params.tdiMode == 2)
                ok = setInt("TDIStages", m_params.tdiStages) && ok;
            else if (m_params.tdiMode == 1)
            {
                if (m_params.tdiFrameCount > 0)
                    ok = setInt("TDIFrameCount", m_params.tdiFrameCount) && ok;
            }

            ok = setInt("Width", m_params.width) && ok;
            if (m_params.tdiMode == 0)
            {
                try { KYFG_SetCameraValueInt(m_camHandle, "Height", m_params.height); } catch (...) {}
            }
            ok = setInt("RegionHeight", m_params.height) && ok;

            for (int i = 0; i < 7; ++i)
            {
                const QString regMode = QString("Region%1Mode").arg(i);
                const int mode = (m_params.regionModeMask & (1 << i)) ? 1 : 0;
                ok = setEnum(regMode.toStdString().c_str(), mode) && ok;

                if (mode == 1)
                {
                    if (i < static_cast<int>(m_params.regionOffsets.size()))
                    {
                        const QString regOffset = QString("Region%1OffsetY").arg(i);
                        setInt(regOffset.toStdString().c_str(), m_params.regionOffsets[i]);
                    }

                    if (i < static_cast<int>(m_params.regionBinningModes.size()))
                    {
                        const QString regBinning = QString("Region%1BinningMode").arg(i);
                        const int binningVal = m_params.regionBinningModes[i];
                        const char* binningNames[] = {"Off", "2x2", "4x4"};
                        const char* binningName = (binningVal >= 0 && binningVal <= 2) ? binningNames[binningVal] : "Off";
                        auto binStatus = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, regBinning.toStdString().c_str(), binningName);
                        if (binStatus != FGSTATUS_OK)
                        {
                            setEnum(regBinning.toStdString().c_str(), binningVal);
                        }
                    }
                }
            }
        }

        auto status = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "PixelFormat", "Mono10");
        if (status != FGSTATUS_OK)
        {
            status = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "PixelFormat", "Mono12");
        }
        if (status != FGSTATUS_OK)
        {
            emit logMessage(QString("[Cam%1] PixelFormat set returned %2").arg(m_camIndex).arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "PixelFormat");
        }

        const char* patternName = (m_params.testPattern == 0) ? "Off" : "SensorGrayDiagonalRamp";
        auto patStatus = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "TestPattern", patternName);
        if (patStatus != FGSTATUS_OK)
        {
            patStatus = KYFG_SetCameraValueEnum(m_camHandle, "TestPattern", m_params.testPattern);
        }
        if (patStatus == FGSTATUS_OK)
        {
            emit logMessage(QString("[Cam%1] TestPattern configured to %2 (%3)").arg(m_camIndex).arg(patternName).arg(m_params.testPattern));
        }
        else
        {
            emit logMessage(QString("[Cam%1] TestPattern=%2 set returned %3").arg(m_camIndex).arg(patternName).arg(patStatus));
            if (patStatus == 12314) emit featureUnsupported(m_camIndex, "TestPattern");
            ok = false;
        }
        status = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "ExposureMode", "Timed");
        if (status != FGSTATUS_OK) {
            emit logMessage(QString("[Cam%1] ExposureMode set returned %2").arg(m_camIndex).arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "ExposureMode");
        }
        status = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "TriggerMode", "Off");
        if (status != FGSTATUS_OK) {
            emit logMessage(QString("[Cam%1] TriggerMode set returned %2").arg(m_camIndex).arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "TriggerMode");
        }

        try {
            KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "AcquisitionMode", "Continuous");
        } catch (...) {}
        try {
            KYFG_SetCameraValueBool(m_camHandle, "AcquisitionFrameRateEnable", true);
        } catch (...) {}

        // 1. Configure AcquisitionFrameRate first
        const double requestedFps = (m_params.fps > 0.05f) ? static_cast<double>(m_params.fps) : 10.0;
        double targetFps = requestedFps;
        double maxFpsBeforeSet = 0.0;
        try {
            maxFpsBeforeSet = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRateMax");
        } catch (...) {}
        if (maxFpsBeforeSet > 0.0 && targetFps > maxFpsBeforeSet)
        {
            targetFps = maxFpsBeforeSet;
            const QString msg = QString("[Cam%1] Requested FPS %2 is above camera max %3 for current geometry; setting to max %3")
                .arg(m_camIndex)
                .arg(requestedFps, 0, 'f', 2)
                .arg(maxFpsBeforeSet, 0, 'f', 2);
            printTerminalLog(msg);
            emit logMessage(msg);
            emit fpsClamped(m_camIndex, requestedFps, maxFpsBeforeSet);
        }

        ok = setFloat("AcquisitionFrameRate", targetFps) && ok;

        double actualFps = targetFps;
        try {
            actualFps = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRate");
        } catch (...) {}
        if (actualFps <= 0.05) actualFps = targetFps;
        m_params.fps = static_cast<float>(actualFps);
        m_params.maxFps = static_cast<float>(maxFpsBeforeSet > 0.0 ? maxFpsBeforeSet : actualFps);

        // 2. Set Exposure Time AFTER setting AcquisitionFrameRate so camera's ExposureTimeMax is updated
        // and exposure time does not exceed the frame period (preventing half-FPS drop)
        double expMax = 0.0;
        try {
            expMax = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTimeMax");
        } catch (...) {}
        const double framePeriodUs = 1000000.0 / actualFps;
        double exposureUs = std::max(1.0, static_cast<double>(m_params.exposure));
        // Safe exposure threshold: sensor readout requires ~35-40% margin to prevent halving FPS
        const double safeMaxExp = (actualFps > 1.0) ? (framePeriodUs * 0.60) : framePeriodUs;
        double maxAllowedExp = (expMax > 0.0) ? std::min(expMax, safeMaxExp) : safeMaxExp;
        if (exposureUs > maxAllowedExp)
        {
            exposureUs = std::max(1.0, std::floor(maxAllowedExp));
            const QString expClampMsg = QString("[Cam%1] Exposure adjusted to %2 us to achieve %3 FPS without half-rate drop")
                .arg(m_camIndex).arg(exposureUs, 0, 'f', 0).arg(actualFps, 0, 'f', 2);
            printTerminalLog(expClampMsg);
            emit logMessage(expClampMsg);
        }

        ok = setFloat("ExposureTime", exposureUs) && ok;
        m_params.exposure = static_cast<float>(exposureUs);
        ok = setFloat("Gain", m_params.gain) && ok;
        if (m_params.tdiMode == 2)
        {
            ok = setInt("TDIYShift", m_params.tdiYShift) && ok;
        }
        ok = setBool("ReverseX", m_params.reverseX) && ok;
        ok = setBool("ReverseY", m_params.reverseY) && ok;

        double maxFps = 0.0;
        double actualExposure = 0.0;
        try {
            actualFps = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRate");
            maxFps = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRateMax");
            actualExposure = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTime");
        } catch (...) {}
        if (maxFps > 0.0) m_params.maxFps = static_cast<float>(maxFps);
        if (actualFps > 0.05) m_params.fps = static_cast<float>(actualFps);
        if (actualExposure > 0.0) m_params.exposure = static_cast<float>(actualExposure);
        const QString acceptedMsg = QString("[Cam%1] Camera accepted: FPS=%2 MaxFPS=%3 Exposure=%4 us")
            .arg(m_camIndex)
            .arg(actualFps, 0, 'f', 2)
            .arg(maxFps, 0, 'f', 2)
            .arg(actualExposure, 0, 'f', 0);
        printTerminalLog(acceptedMsg);
        emit logMessage(acceptedMsg);

        try {
            m_params.width = KYFG_GetCameraValueInt(m_camHandle, "Width");
            m_params.height = KYFG_GetCameraValueInt(m_camHandle, "Height");
        } catch (...) {}

        emit logMessage(QString("[Cam%1] Applied params: W=%2 H=%3 FPS=%4 Exposure=%5 Gain=%6 Test=%7 Regions=0x%8 TDI=%9 Stages=%10 YShift=%11")
            .arg(m_camIndex)
            .arg(m_params.width)
            .arg(m_params.height)
            .arg(m_params.fps, 0, 'f', 2)
            .arg(m_params.exposure, 0, 'f', 1)
            .arg(m_params.gain, 0, 'f', 2)
            .arg(m_params.testPattern)
            .arg(static_cast<int>(m_params.regionModeMask), 0, 16)
            .arg(m_params.tdiMode)
            .arg(m_params.tdiStages)
            .arg(m_params.tdiYShift));

        emit paramsQueried(m_camIndex, m_params);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in applyCameraParameters: %2").arg(m_camIndex).arg(e.what()));
        ok = false;
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in applyCameraParameters").arg(m_camIndex));
        ok = false;
    }

    return ok;
}

// ── 10-bit packed → 8-bit display ───────────────────────────────────────
std::vector<uint8_t> CameraWorker::unpack10bit(const uint8_t* src, int width, int height, size_t bufferSize)
{
    const int total = width * height;
    std::vector<uint8_t> out(total, 0);
    if (!src || total <= 0 || bufferSize <= 0)
        return out;

    uint8_t* dest = out.data();
    const int limit = total & ~3;

    int safeLimit = limit;
    while (safeLimit > 0 && (((safeLimit - 4) / 4) * 5 + 4) >= static_cast<int>(bufferSize))
    {
        safeLimit -= 4;
    }

    #pragma omp parallel for schedule(static)
    for (int di = 0; di < safeLimit; di += 4)
    {
        const int bi = (di / 4) * 5;
        const uint8_t b0 = src[bi];
        const uint8_t b1 = src[bi + 1];
        const uint8_t b2 = src[bi + 2];
        const uint8_t b3 = src[bi + 3];
        const uint8_t b4 = src[bi + 4];

        // PFNC Mono10p: four 10-bit pixels are bit-packed into five bytes.
        dest[di]     = static_cast<uint8_t>((b0 | ((b1 & 0x03) << 8)) >> 2);
        dest[di + 1] = static_cast<uint8_t>(((b1 >> 2) | ((b2 & 0x0F) << 6)) >> 2);
        dest[di + 2] = static_cast<uint8_t>(((b2 >> 4) | ((b3 & 0x3F) << 4)) >> 2);
        dest[di + 3] = static_cast<uint8_t>(((b3 >> 6) | (b4 << 2)) >> 2);
    }

    for (int di = safeLimit; di < total; ++di)
    {
        const int groupPixel = di % 4;
        const int bi = (di / 4) * 5;
        if (bi >= static_cast<int>(bufferSize))
            break;

        const uint8_t b0 = src[bi];
        const uint8_t b1 = (bi + 1 < static_cast<int>(bufferSize)) ? src[bi + 1] : 0;
        const uint8_t b2 = (bi + 2 < static_cast<int>(bufferSize)) ? src[bi + 2] : 0;
        const uint8_t b3 = (bi + 3 < static_cast<int>(bufferSize)) ? src[bi + 3] : 0;
        const uint8_t b4 = (bi + 4 < static_cast<int>(bufferSize)) ? src[bi + 4] : 0;

        switch (groupPixel)
        {
        case 0:
            dest[di] = static_cast<uint8_t>((b0 | ((b1 & 0x03) << 8)) >> 2);
            break;
        case 1:
            dest[di] = static_cast<uint8_t>(((b1 >> 2) | ((b2 & 0x0F) << 6)) >> 2);
            break;
        case 2:
            dest[di] = static_cast<uint8_t>(((b2 >> 4) | ((b3 & 0x3F) << 4)) >> 2);
            break;
        case 3:
            dest[di] = static_cast<uint8_t>(((b3 >> 6) | (b4 << 2)) >> 2);
            break;
        }
    }

    return out;
}

// ── 12-bit packed → 8-bit display ───────────────────────────────────────
std::vector<uint8_t> CameraWorker::unpack12bit(const uint8_t* src, int width, int height, size_t bufferSize)
{
    const int total = width * height;
    std::vector<uint8_t> out(total, 0);
    if (!src || total <= 0 || bufferSize <= 0)
        return out;

    uint8_t* dest = out.data();
    const int limit = total & ~1;

    int safeLimit = limit;
    while (safeLimit > 0 && (((safeLimit - 2) / 2) * 3 + 2) >= static_cast<int>(bufferSize))
    {
        safeLimit -= 2;
    }

    #pragma omp parallel for schedule(static)
    for (int di = 0; di < safeLimit; di += 2)
    {
        int bi = (di / 2) * 3;
        uint8_t b0 = src[bi];
        uint8_t b1 = src[bi+1];
        uint8_t b2 = src[bi+2];

        // Standard Mono12p: 2 pixels packed into 3 bytes.
        // Pixel 0 MSB (8 bits) = (b0 >> 4) | ((b1 & 0x0F) << 4)
        // Pixel 1 MSB (8 bits) = b2
        dest[di]   = static_cast<uint8_t>((b0 >> 4) | ((b1 & 0x0F) << 4));
        dest[di+1] = b2;
    }

    for (int di = safeLimit; di < total; ++di)
    {
        int bi = (di / 2) * 3;
        if (bi + 2 < static_cast<int>(bufferSize))
        {
            uint8_t b0 = src[bi];
            uint8_t b1 = src[bi+1];
            uint8_t b2 = src[bi+2];
            if (di % 2 == 0)
                dest[di] = static_cast<uint8_t>((b0 >> 4) | ((b1 & 0x0F) << 4));
            else
                dest[di] = b2;
        }
    }

    return out;
}

// ── Smart Raw Unpacker ──────────────────────────────────────────────────
std::vector<uint8_t> CameraWorker::unpackRawTo8bit(const uint8_t* src, int width, int height, size_t bufferSize)
{
    if (!src || width <= 0 || height <= 0 || bufferSize <= 0)
        return std::vector<uint8_t>(width > 0 && height > 0 ? width * height : 0, 0);

    // Adjust height if bufferSize matches (height - 1) lines due to metadata footer line
    int actualHeight = height;
    if (height > 1 && width > 0)
    {
        const size_t exp12Full   = (static_cast<size_t>(width) * static_cast<size_t>(height) * 3 + 1) / 2;
        const size_t exp12Minus1 = (static_cast<size_t>(width) * static_cast<size_t>(height - 1) * 3 + 1) / 2;
        if (bufferSize < exp12Full && bufferSize >= exp12Minus1)
        {
            actualHeight = height - 1;
        }
        else
        {
            const size_t exp10Full   = (static_cast<size_t>(width) * static_cast<size_t>(height) * 5 + 3) / 4;
            const size_t exp10Minus1 = (static_cast<size_t>(width) * static_cast<size_t>(height - 1) * 5 + 3) / 4;
            if (bufferSize < exp10Full && bufferSize >= exp10Minus1)
            {
                actualHeight = height - 1;
            }
        }
    }

    const int total = width * actualHeight;
    const size_t expected10Packed = (static_cast<size_t>(total) * 5 + 3) / 4;
    const size_t expected12Packed = (static_cast<size_t>(total) * 3 + 1) / 2;
    const size_t expected16Bit = static_cast<size_t>(total) * 2;
    const double bpp = static_cast<double>(bufferSize) / total;

    // 1. 8-bit raw (approx 1.0 byte/pixel)
    if (bpp >= 0.90 && bpp <= 1.10)
    {
        std::vector<uint8_t> out(total, 0);
        const size_t copyBytes = std::min(static_cast<size_t>(total), bufferSize);
        std::memcpy(out.data(), src, copyBytes);
        return out;
    }
    // 2. 10-bit packed raw (4 pixels in 5 bytes)
    if (bufferSize >= expected10Packed && bufferSize < expected12Packed)
    {
#if !USING_STUB
        auto kayaPixels = unpackPackedWithKaya(src, width, actualHeight, bufferSize, "Mono10");
        if (!kayaPixels.empty())
            return kayaPixels;
#endif
        return unpack10bit(src, width, actualHeight, bufferSize);
    }
    // 3. 12-bit packed raw (2 pixels in 3 bytes)
    if (bufferSize >= expected12Packed && bufferSize < expected16Bit)
    {
#if !USING_STUB
        auto kayaPixels = unpackPackedWithKaya(src, width, actualHeight, bufferSize, "Mono12");
        if (!kayaPixels.empty())
            return kayaPixels;
#endif
        return unpack12bit(src, width, actualHeight, bufferSize);
    }
    // 4. 16-bit uint16 container (approx 2.0 bytes/pixel)
    if (bpp >= 1.80 && bpp <= 2.20)
    {
        std::vector<uint8_t> out(total, 0);
        const uint16_t* src16 = reinterpret_cast<const uint16_t*>(src);
        uint8_t* dest = out.data();
        const size_t availablePixels = bufferSize / 2;
        const int safeLimit = static_cast<int>(std::min(static_cast<size_t>(total), availablePixels));
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < safeLimit; ++i)
        {
            uint16_t val = src16[i];
            dest[i] = (val > 4095) ? static_cast<uint8_t>(val >> 8) : static_cast<uint8_t>(val >> 4);
        }
        return out;
    }

    // Default: 12-bit packed (using LSB unpacking)
    return unpack12bit(src, width, actualHeight, bufferSize);
}

// ── Stream callback (real hardware) ────────────────────────────────────
void CameraWorker::streamCallback(STREAM_BUFFER_HANDLE streamBufferHandle, void* userContext)
{
    CameraWorker* worker = static_cast<CameraWorker*>(userContext);
    if (!worker || !worker->beginStreamCallback())
        return;

    try {
        if (streamBufferHandle != NULL_STREAM_BUFFER_HANDLE) {
            unsigned char* pFrameMemory = nullptr;
            size_t bufferSize = 0;
            if (FGSTATUS_OK != KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_BASE, &pFrameMemory, NULL, NULL) || !pFrameMemory) {
                emit worker->errorOccurred(worker->m_camIndex,
                    QString("Camera %1 callback did not provide frame memory").arg(worker->m_camIndex));
                KYFG_BufferToQueue(streamBufferHandle, KY_ACQ_QUEUE_INPUT);
            } else {
                KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_SIZE, &bufferSize, NULL, NULL);

                const quint64 currentEpoch = worker->m_streamEpoch.load();
                // Queue processing to the worker's own thread to avoid blocking the callback thread.
                QMetaObject::invokeMethod(worker, "processFrame",
                                          Qt::QueuedConnection,
                                          Q_ARG(quint64, currentEpoch),
                                          Q_ARG(STREAM_BUFFER_HANDLE, streamBufferHandle),
                                          Q_ARG(void*, pFrameMemory),
                                          Q_ARG(size_t, bufferSize));
            }
        }
    }
    catch (const std::exception& e) {
        emit worker->logMessage(QString("[Cam%1] Exception in streamCallback: %2").arg(worker->m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit worker->logMessage(QString("[Cam%1] Unknown exception in streamCallback").arg(worker->m_camIndex));
    }

    worker->endStreamCallback();
}

bool CameraWorker::beginStreamCallback()
{
    QMutexLocker locker(&m_callbackMutex);
    if (!m_acceptStreamCallbacks) return false;
    ++m_activeStreamCallbacks;
    return true;
}

void CameraWorker::endStreamCallback()
{
    QMutexLocker locker(&m_callbackMutex);
    if (m_activeStreamCallbacks > 0 && --m_activeStreamCallbacks == 0)
        m_callbacksDrained.wakeAll();
}

void CameraWorker::stopAcceptingStreamCallbacks()
{
    QMutexLocker locker(&m_callbackMutex);
    m_acceptStreamCallbacks = false;
    while (m_activeStreamCallbacks > 0)
        m_callbacksDrained.wait(&m_callbackMutex);
}

void CameraWorker::pollTemperature()
{
    try {
        QMutexLocker lk(&m_mutex);
        if (m_cameraOpened && m_camHandle != INVALID_CAMHANDLE)
        {
            float sensorTemp  = 0.0f;
            float grabberTemp = 0.0f;
            try {
                sensorTemp = KYFG_GetCameraValueFloat(m_camHandle, "DeviceTemperature");
            } catch (...) {}
            if (m_fgHandle != INVALID_FGHANDLE) {
                try {
                    int64_t gTempInt = KYFG_GetGrabberValueInt(m_fgHandle, "DeviceTemperature");
                    if (gTempInt > 1000 || gTempInt < -1000) {
                        grabberTemp = (float)gTempInt / 1000.0f;
                    } else if (gTempInt != 0) {
                        grabberTemp = (float)gTempInt;
                    } else {
                        grabberTemp = (float)KYFG_GetGrabberValueFloat(m_fgHandle, "DeviceTemperature");
                    }
                } catch (...) {}
            }
            emit temperatureReady(m_camIndex, sensorTemp, grabberTemp);
        }
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in pollTemperature: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in pollTemperature").arg(m_camIndex));
    }
}

CameraParams CameraWorker::queryCameraParameters()
{
    CameraParams p = m_params;
    if (m_camHandle == INVALID_CAMHANDLE) return p;

    try {
        int64_t w = KYFG_GetCameraValueInt(m_camHandle, "Width");
        if (w > 0) p.width = static_cast<int>(w);

        int64_t h = KYFG_GetCameraValueInt(m_camHandle, "Height");
        if (h <= 0) h = KYFG_GetCameraValueInt(m_camHandle, "RegionHeight");
        if (h > 0) p.height = static_cast<int>(h);

        double fps = 0.0;
        try {
            fps = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRate");
        } catch (...) {}
        if (fps > 0.05) p.fps = static_cast<float>(fps);
        else if (p.fps <= 0.05f) p.fps = 10.0f;

        try {
            double maxFps = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRateMax");
            if (maxFps > 0.0) p.maxFps = static_cast<float>(maxFps);
        } catch (...) {}

        double exp = 0.0;
        try {
            exp = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTime");
        } catch (...) {}
        if (exp > 0.0) p.exposure = static_cast<float>(exp);
        else if (p.exposure <= 0.0f) p.exposure = 5000.0f;

        double gain = KYFG_GetCameraValueFloat(m_camHandle, "Gain");
        if (gain >= 0.0) p.gain = static_cast<float>(gain);

        int64_t testPattern = KYFG_GetCameraValueEnum(m_camHandle, "TestPattern");
        if (testPattern >= 0) p.testPattern = static_cast<int>(testPattern);

        int64_t tdiMode = KYFG_GetCameraValueEnum(m_camHandle, "TDIMode");
        if (tdiMode >= 0) p.tdiMode = static_cast<int>(tdiMode);

        int64_t tdiStages = KYFG_GetCameraValueInt(m_camHandle, "TDIStages");
        if (tdiStages > 0) p.tdiStages = static_cast<int>(tdiStages);

        int64_t tdiFrameCount = KYFG_GetCameraValueInt(m_camHandle, "TDIFrameCount");
        if (tdiFrameCount > 0) p.tdiFrameCount = static_cast<int>(tdiFrameCount);

        int64_t tdiYShift = KYFG_GetCameraValueInt(m_camHandle, "TDIYShift");
        if (tdiYShift >= 0) p.tdiYShift = static_cast<int>(tdiYShift);

        try {
            p.reverseX = KYFG_GetCameraValueBool(m_camHandle, "ReverseX");
        } catch (...) {}
        try {
            p.reverseY = KYFG_GetCameraValueBool(m_camHandle, "ReverseY");
        } catch (...) {}

        const int defOffsets[] = {0, 900, 2000, 3000, 4000, 5000, 6000};
        p.regionOffsets.resize(7);
        for (int i = 0; i < 7; ++i) {
            if (i < static_cast<int>(m_params.regionOffsets.size()) && m_params.regionOffsets[i] >= 0) {
                p.regionOffsets[i] = m_params.regionOffsets[i];
            } else {
                p.regionOffsets[i] = defOffsets[i];
            }
        }
        p.regionBinningModes.resize(7, 0);

        for (int i = 0; i < 7; ++i)
        {
            int64_t mode = 0;
            try {
                const QString regMode = QString("Region%1Mode").arg(i);
                mode = KYFG_GetCameraValueEnum(m_camHandle, regMode.toStdString().c_str());
                if (mode == 1) {
                    p.regionModeMask |= (1 << i);
                }
            } catch (...) {}

            if (mode == 1 || i == 0) {
                try {
                    const QString regOffset = QString("Region%1OffsetY").arg(i);
                    int64_t offset = KYFG_GetCameraValueInt(m_camHandle, regOffset.toStdString().c_str());
                    if (offset >= 0) {
                        p.regionOffsets[i] = static_cast<int>(offset);
                    }
                } catch (...) {}
            }

            try {
                const QString regBinning = QString("Region%1BinningMode").arg(i);
                int64_t bin = KYFG_GetCameraValueEnum(m_camHandle, regBinning.toStdString().c_str());
                if (bin >= 0 && bin <= 2) p.regionBinningModes[i] = static_cast<int>(bin);
            } catch (...) {}
        }
    } catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception querying camera parameters: %2").arg(m_camIndex).arg(e.what()));
    } catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception querying camera parameters").arg(m_camIndex));
    }

    return p;
}

// ── openCamera ─────────────────────────────────────────────────────────
// REAL: set camera parameters, create stream, register callback, allocate buffers.
void CameraWorker::openCamera()
{
    try {
        QMutexLocker lk(&m_mutex);

        if (m_camHandle == INVALID_CAMHANDLE)
        {
            emit errorOccurred(m_camIndex, QString("Camera %1 handle is invalid. Run detection first.").arg(m_camIndex));
            return;
        }

        if (!m_cameraOpened)
        {
            auto status = KYFG_CameraOpen2(m_camHandle, NULL);
            if (status != FGSTATUS_OK)
            {
                // If camera link was left locked/open by a previous crash, close it and retry
                KYFG_CameraClose(m_camHandle);
                QThread::msleep(100);
                status = KYFG_CameraOpen2(m_camHandle, NULL);
            }
            if (status != FGSTATUS_OK)
            {
                emit errorOccurred(m_camIndex, QString("Camera #%1 was not connected successfully (status: 0x%2)").arg(m_camIndex).arg(status, 0, 16));
                return;
            }
            m_cameraOpened = true;
            emit logMessage(QString("Camera #%1 was connected successfully").arg(m_camIndex));
        }

        // Apply user-configured parameters to camera first
        applyCameraParameters(true);
        m_paramsApplied = true;

        m_params = queryCameraParameters();
        emit paramsQueried(m_camIndex, m_params);

        emit logMessage(QString("[Cam%1] Opening camera... W=%2 H=%3 FPS=%4 Test=%5")
            .arg(m_camIndex).arg(m_params.width).arg(m_params.height).arg(m_params.fps).arg(m_params.testPattern));

        // Create stream
        auto status = KYFG_StreamCreate(m_camHandle, &m_streamHandle, 0);
        if (status != FGSTATUS_OK || m_streamHandle == INVALID_STREAMHANDLE)
        {
            cleanupCameraLocked();
            emit errorOccurred(m_camIndex, QString("Camera %1 stream create failed: %2").arg(m_camIndex).arg(status));
            return;
        }

        status = KYFG_StreamBufferCallbackRegister(m_streamHandle, streamCallback, this);
        if (status != FGSTATUS_OK)
        {
            cleanupCameraLocked();
            emit errorOccurred(m_camIndex, QString("Camera %1 callback register failed: %2").arg(m_camIndex).arg(status));
            return;
        }
        {
            QMutexLocker callbackLocker(&m_callbackMutex);
            m_acceptStreamCallbacks = true;
        }

        // Get buffer info
        size_t frameDataSize = 0;
        size_t frameDataAligment = 0;
        status = KYFG_StreamGetInfo(m_streamHandle, KY_STREAM_INFO_PAYLOAD_SIZE, &frameDataSize, NULL, NULL);
        if (status != FGSTATUS_OK || frameDataSize == 0)
        {
            cleanupCameraLocked();
            emit errorOccurred(m_camIndex, QString("Camera %1 payload size query failed: %2").arg(m_camIndex).arg(status));
            return;
        }

        status = KYFG_StreamGetInfo(m_streamHandle, KY_STREAM_INFO_BUF_ALIGNMENT, &frameDataAligment, NULL, NULL);
        if (status != FGSTATUS_OK || frameDataAligment == 0)
        {
            cleanupCameraLocked();
            emit errorOccurred(m_camIndex, QString("Camera %1 buffer alignment query failed: %2").arg(m_camIndex).arg(status));
            return;
        }

        // Allocate buffers
        for (int i = 0; i < 16; ++i) {
            m_pBuffers[i] = alignedAllocate(frameDataSize, frameDataAligment);
            if (!m_pBuffers[i])
            {
                cleanupCameraLocked();
                emit errorOccurred(m_camIndex, QString("Camera %1 buffer allocation failed at index %2").arg(m_camIndex).arg(i));
                return;
            }

            status = KYFG_BufferAnnounce(m_streamHandle, m_pBuffers[i], frameDataSize, NULL, &m_streamBuffers[i]);
            if (status != FGSTATUS_OK)
            {
                cleanupCameraLocked();
                emit errorOccurred(m_camIndex, QString("Camera %1 buffer announce failed at index %2: %3").arg(m_camIndex).arg(i).arg(status));
                return;
            }
        }
        m_frameId = 0;
        m_frameDataSize = frameDataSize;
        m_streamReopenRequired = false;
        m_paramsApplied = true;
        m_actualPayloadSize = queryActualPayloadSize();
        if (m_actualPayloadSize > m_frameDataSize)
            m_actualPayloadSize = m_frameDataSize;
        m_streamEpoch.fetch_add(1);
        emit logMessage(QString("[Cam%1] Opened: W=%2 H=%3 buffer=%4 actualPayload=%5 alignment=%6")
            .arg(m_camIndex).arg(m_params.width).arg(m_params.height).arg(frameDataSize)
            .arg(m_actualPayloadSize).arg(frameDataAligment));
        emit cameraOpened(m_camIndex, m_params.width, m_params.height);
        m_tempTimer->start();
        QTimer::singleShot(0, this, &CameraWorker::pollTemperature);
    }
    catch (const std::exception& e) {
        cleanupCameraLocked();
        emit errorOccurred(m_camIndex, QString("Exception in openCamera: %1").arg(e.what()));
    }
    catch (...) {
        cleanupCameraLocked();
        emit errorOccurred(m_camIndex, "Unknown exception in openCamera");
    }
}

// ── cleanupCameraLocked ────────────────────────────────────────────────
void CameraWorker::cleanupCameraLocked()
{
    // Unregister callback and delete stream cleanly
    if (m_streamHandle != INVALID_STREAMHANDLE) {
        const STREAM_HANDLE streamToDelete = m_streamHandle;
        m_streamHandle = INVALID_STREAMHANDLE;

        KYFG_StreamBufferCallbackUnregister(streamToDelete, streamCallback);
        KYFG_StreamDelete(streamToDelete);
    }

    // Free buffers only after stream is completely deleted
    for (int i = 0; i < 16; ++i) {
        if (m_pBuffers[i]) {
            alignedFree(m_pBuffers[i]);
            m_pBuffers[i] = nullptr;
        }
        m_streamBuffers[i] = 0;
    }
    m_frameDataSize = 0;
    m_actualPayloadSize = 0;
    m_streamReopenRequired = false;

    if (m_cameraOpened.exchange(false) && m_camHandle != INVALID_CAMHANDLE) {
        const auto status = KYFG_CameraClose(m_camHandle);
        if (status != FGSTATUS_OK) {
            emit logMessage(QString("[Cam%1] Camera close returned %2").arg(m_camIndex).arg(status));
        }
    }
}

// ── closeCamera ─────────────────────────────────────────────────────────
void CameraWorker::closeCamera()
{
    try {
        if (m_tempTimer) m_tempTimer->stop();
    } catch (...) {}

    stopStream();
    stopAcceptingStreamCallbacks();
    m_streamEpoch.fetch_add(1);

    try {
        QMutexLocker lk(&m_mutex);
        cleanupCameraLocked();
        emit cameraClosed(m_camIndex);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in closeCamera: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in closeCamera").arg(m_camIndex));
    }
}

// ── startStream ─────────────────────────────────────────────────────────
// REAL: KYFG_BufferQueueAll() + KYFG_CameraStart()
void CameraWorker::startStream()
{
    try {
        QMutexLocker lk(&m_mutex);
        if (m_streaming) return;
        if (m_streamHandle == INVALID_STREAMHANDLE || m_camHandle == INVALID_CAMHANDLE)
        {
            emit errorOccurred(m_camIndex, QString("Camera %1 stream is not ready. Connect first.").arg(m_camIndex));
            return;
        }
        if (m_streamReopenRequired)
        {
            emit errorOccurred(m_camIndex,
                QString("Camera %1 payload size changed; disconnect and reconnect before starting with the new TDI/geometry settings.").arg(m_camIndex));
            return;
        }

        if (!m_paramsApplied)
        {
            applyCameraParameters(false);
            m_paramsApplied = true;
        }

        // Queue all buffers
        auto status = KYFG_BufferQueueAll(m_streamHandle, KY_ACQ_QUEUE_UNQUEUED, KY_ACQ_QUEUE_INPUT);
        if (status != FGSTATUS_OK)
        {
            emit errorOccurred(m_camIndex, QString("Camera %1 buffer queue failed: %2").arg(m_camIndex).arg(status));
            return;
        }

        // Start acquisition
        status = KYFG_CameraStart(m_camHandle, m_streamHandle, 0);
        if (status != FGSTATUS_OK)
        {
            emit errorOccurred(m_camIndex, QString("Camera %1 start failed: %2").arg(m_camIndex).arg(status));
            return;
        }

        m_streaming = true;
        emit logMessage(QString("[Cam%1] Stream started").arg(m_camIndex));
        const double payloadMb = static_cast<double>(m_actualPayloadSize) / (1024.0 * 1024.0);
        const double cxpLinkMaxFps = (payloadMb > 0.0) ? (1068.0 / payloadMb) : 0.0;
        QString streamMsg = QString("[Cam%1] Stream started: %2x%3 (%4 MB/frame)")
            .arg(m_camIndex).arg(m_params.width).arg(m_params.height).arg(payloadMb, 0, 'f', 2);
        if (m_params.fps > (cxpLinkMaxFps * 1.05) && cxpLinkMaxFps > 0.0) {
            streamMsg += QString(". Note: Single CXP-12 link transfer rate is capped at %1 FPS for this resolution (sensor rate: %2 FPS).")
                .arg(cxpLinkMaxFps, 0, 'f', 1).arg(m_params.fps, 0, 'f', 2);
        }
        printTerminalLog(streamMsg);
        emit logMessage(streamMsg);
        emit streamStarted(m_camIndex);
    }
    catch (const std::exception& e) {
        emit errorOccurred(m_camIndex, QString("Exception in startStream: %1").arg(e.what()));
    }
    catch (...) {
        emit errorOccurred(m_camIndex, "Unknown exception in startStream");
    }
}

// ── stopStream ──────────────────────────────────────────────────────────
void CameraWorker::stopStream()
{
    try {
        QMutexLocker lk(&m_mutex);
        if (!m_streaming) return;
        m_streaming = false;

        if (m_camHandle != INVALID_CAMHANDLE)
        {
            KYFG_CameraStop(m_camHandle);
        }

        emit streamStopped(m_camIndex);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in stopStream: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in stopStream").arg(m_camIndex));
    }
}

void CameraWorker::setSaving(bool saving)
{
    m_saving = saving;
}


void CameraWorker::setLiveExposure(double exposureUs)
{
    try {
        QMutexLocker lk(&m_mutex);
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        double targetExp = std::max(1.0, exposureUs);
        double expMax = 0.0;
        try {
            expMax = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTimeMax");
        } catch (...) {}
        if (expMax > 0.0 && targetExp > expMax) {
            targetExp = expMax;
        }

        const auto status = KYFG_SetCameraValueFloat(m_camHandle, "ExposureTime", targetExp);
        if (status != FGSTATUS_OK)
        {
            emit logMessage(QString("[Cam%1] Live ExposureTime=%2 set returned %3")
                .arg(m_camIndex)
                .arg(targetExp, 0, 'f', 3)
                .arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "ExposureTime");
            return;
        }

        double actualExp = targetExp;
        try {
            actualExp = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTime");
        } catch (...) {}
        m_params.exposure = static_cast<float>(actualExp);

        const QString msg = QString("[Cam%1] Live ExposureTime set to %2 us (status %3)")
            .arg(m_camIndex).arg(actualExp, 0, 'f', 0).arg(status);
        printTerminalLog(msg);
        emit logMessage(msg);

        emit paramsQueried(m_camIndex, m_params);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveExposure: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveExposure").arg(m_camIndex));
    }
}

void CameraWorker::setLiveGain(double gainDb)
{
    try {
        QMutexLocker lk(&m_mutex);
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        const auto status = KYFG_SetCameraValueFloat(m_camHandle, "Gain", gainDb);
        if (status != FGSTATUS_OK)
        {
            emit logMessage(QString("[Cam%1] Live Gain=%2 set returned %3")
                .arg(m_camIndex)
                .arg(gainDb, 0, 'f', 2)
                .arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "Gain");
            return;
        }

        m_params.gain = static_cast<float>(gainDb);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveGain: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveGain").arg(m_camIndex));
    }
}

void CameraWorker::setLiveReverseX(bool reverseX)
{
    try {
        QMutexLocker lk(&m_mutex);
        m_params.reverseX = reverseX;
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        const auto status = KYFG_SetCameraValueBool(m_camHandle, "ReverseX", reverseX);
        if (status != FGSTATUS_OK)
        {
            emit logMessage(QString("[Cam%1] Live ReverseX=%2 set returned %3")
                .arg(m_camIndex)
                .arg(reverseX ? "true" : "false")
                .arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "ReverseX");
            return;
        }
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveReverseX: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveReverseX").arg(m_camIndex));
    }
}

void CameraWorker::setLiveReverseY(bool reverseY)
{
    try {
        QMutexLocker lk(&m_mutex);
        m_params.reverseY = reverseY;
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        const auto status = KYFG_SetCameraValueBool(m_camHandle, "ReverseY", reverseY);
        if (status != FGSTATUS_OK)
        {
            emit logMessage(QString("[Cam%1] Live ReverseY=%2 set returned %3")
                .arg(m_camIndex)
                .arg(reverseY ? "true" : "false")
                .arg(status));
            if (status == 12314) emit featureUnsupported(m_camIndex, "ReverseY");
            return;
        }
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveReverseY: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveReverseY").arg(m_camIndex));
    }
}

void CameraWorker::setLiveFps(double fps)
{
    try {
        QMutexLocker lk(&m_mutex);
        const double requestedFps = std::max(0.001, fps);
        m_params.fps = static_cast<float>(requestedFps);
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        const bool wasStreaming = m_streaming.load();
        if (wasStreaming) {
            KYFG_CameraStop(m_camHandle);
        }

        double maxFpsBeforeSet = 0.0;
        try {
            maxFpsBeforeSet = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRateMax");
        } catch (...) {}
        double targetFps = requestedFps;
        if (maxFpsBeforeSet > 0.0 && targetFps > maxFpsBeforeSet) {
            targetFps = maxFpsBeforeSet;
            const QString clampMsg = QString("[Cam%1] Requested FPS %2 is above camera max %3 for current geometry; setting to max %3")
                .arg(m_camIndex).arg(requestedFps, 0, 'f', 2).arg(maxFpsBeforeSet, 0, 'f', 2);
            printTerminalLog(clampMsg);
            emit logMessage(clampMsg);
            emit fpsClamped(m_camIndex, requestedFps, maxFpsBeforeSet);
        }

        const auto status = KYFG_SetCameraValueFloat(m_camHandle, "AcquisitionFrameRate", targetFps);
        double actualFps = targetFps;
        try {
            actualFps = KYFG_GetCameraValueFloat(m_camHandle, "AcquisitionFrameRate");
            if (actualFps > 0.05) m_params.fps = static_cast<float>(actualFps);
            m_params.maxFps = static_cast<float>(maxFpsBeforeSet > 0.0 ? maxFpsBeforeSet : (actualFps > 0.05 ? actualFps : targetFps));
        } catch (...) {}

        // Adjust ExposureTime if needed so exposure fits within the new frame period with readout margin
        double expMax = 0.0;
        double currentExp = 0.0;
        try {
            expMax = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTimeMax");
            currentExp = KYFG_GetCameraValueFloat(m_camHandle, "ExposureTime");
        } catch (...) {}
        const double framePeriodUs = 1000000.0 / actualFps;
        double newExp = currentExp;
        const double safeMaxExp = (actualFps > 1.0) ? (framePeriodUs * 0.60) : framePeriodUs;
        double maxAllowedExp = (expMax > 0.0) ? std::min(expMax, safeMaxExp) : safeMaxExp;
        if (newExp > maxAllowedExp) {
            newExp = std::max(1.0, std::floor(maxAllowedExp));
        }
        if (newExp != currentExp && newExp > 0.0) {
            KYFG_SetCameraValueFloat(m_camHandle, "ExposureTime", newExp);
            m_params.exposure = static_cast<float>(newExp);
            const QString expMsg = QString("[Cam%1] Exposure adjusted to %2 us to achieve %3 FPS without half-rate drop")
                .arg(m_camIndex).arg(newExp, 0, 'f', 0).arg(actualFps, 0, 'f', 2);
            printTerminalLog(expMsg);
            emit logMessage(expMsg);
        }

        const QString msg = QString("[Cam%1] Live AcquisitionFrameRate set to %2 (status %3)")
            .arg(m_camIndex).arg(actualFps, 0, 'f', 2).arg(status);
        printTerminalLog(msg);
        emit logMessage(msg);

        emit paramsQueried(m_camIndex, m_params);

        if (wasStreaming && m_streamHandle != INVALID_STREAMHANDLE) {
            KYFG_BufferQueueAll(m_streamHandle, KY_ACQ_QUEUE_UNQUEUED, KY_ACQ_QUEUE_INPUT);
            KYFG_CameraStart(m_camHandle, m_streamHandle, 0);
        }
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveFps: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveFps").arg(m_camIndex));
    }
}

void CameraWorker::setLiveTestPattern(int pattern)
{
    try {
        QMutexLocker lk(&m_mutex);
        m_params.testPattern = pattern;
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        const bool wasStreaming = m_streaming.load();
        if (wasStreaming) {
            KYFG_CameraStop(m_camHandle);
        }

        const char* patternName = (pattern == 0) ? "Off" : "SensorGrayDiagonalRamp";
        auto status = KYFG_SetCameraValueEnum_ByValueName(m_camHandle, "TestPattern", patternName);
        if (status != FGSTATUS_OK) {
            status = KYFG_SetCameraValueEnum(m_camHandle, "TestPattern", pattern);
        }

        const QString msg = QString("[Cam%1] Live TestPattern set to %2 (%3) result: %4")
            .arg(m_camIndex).arg(patternName).arg(pattern).arg(status);
        printTerminalLog(msg);
        emit logMessage(msg);

        if (wasStreaming && m_streamHandle != INVALID_STREAMHANDLE) {
            KYFG_BufferQueueAll(m_streamHandle, KY_ACQ_QUEUE_UNQUEUED, KY_ACQ_QUEUE_INPUT);
            KYFG_CameraStart(m_camHandle, m_streamHandle, 0);
        }
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveTestPattern: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveTestPattern").arg(m_camIndex));
    }
}

void CameraWorker::setLiveTdi(int mode, int stages, int yShift, int frameCount)
{
    try {
        int width = 0;
        int height = 0;
        {
            QMutexLocker lk(&m_mutex);
            if (m_streaming)
            {
                emit logMessage(QString("[Cam%1] Stop streaming before changing TDI mode or stages.").arg(m_camIndex));
                return;
            }
            m_params.tdiMode = mode;
            m_params.tdiStages = stages;
            m_params.tdiYShift = yShift;
            if (frameCount > 0)
                m_params.tdiFrameCount = frameCount;
            width = m_params.width;
            height = m_params.height;
        }

        if (!m_cameraOpened || m_camHandle == INVALID_CAMHANDLE)
            return;

        applyGeometry(width, height);

        const QString msg = QString("[Cam%1] TDI reconfigured: Mode=%2 Stages=%3 FrameCount=%4 YShift=%5; stream buffers rebuilt.")
            .arg(m_camIndex).arg(mode).arg(stages).arg(m_params.tdiFrameCount).arg(yShift);
        printTerminalLog(msg);
        emit logMessage(msg);
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in setLiveTdi: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in setLiveTdi").arg(m_camIndex));
    }
}

void CameraWorker::applyAllParameters(const CameraParams& p)
{
    try {
        QMutexLocker lk(&m_mutex);
        m_params = p;
        if (m_camHandle == INVALID_CAMHANDLE || !m_cameraOpened)
            return;

        const bool wasStreaming = m_streaming.load();
        if (wasStreaming) {
            KYFG_CameraStop(m_camHandle);
        }

        applyCameraParameters(false);

        if (wasStreaming && m_streamHandle != INVALID_STREAMHANDLE) {
            KYFG_BufferQueueAll(m_streamHandle, KY_ACQ_QUEUE_UNQUEUED, KY_ACQ_QUEUE_INPUT);
            KYFG_CameraStart(m_camHandle, m_streamHandle, 0);
        }
    }
    catch (const std::exception& e) {
        emit logMessage(QString("[Cam%1] Exception in applyAllParameters: %2").arg(m_camIndex).arg(e.what()));
    }
    catch (...) {
        emit logMessage(QString("[Cam%1] Unknown exception in applyAllParameters").arg(m_camIndex));
    }
}

void CameraWorker::applyGeometry(int width, int height)
{
    try {
        QMutexLocker lk(&m_mutex);
        m_params.width = width;
        m_params.height = height;

        if (m_cameraOpened && m_camHandle != INVALID_CAMHANDLE)
        {
            stopAcceptingStreamCallbacks();
            m_streamEpoch.fetch_add(1);

            // We must release the old stream and buffers first because payload size will change!
            if (m_streamHandle != INVALID_STREAMHANDLE)
            {
                KYFG_StreamBufferCallbackUnregister(m_streamHandle, streamCallback);
                KYFG_BufferQueueAll(m_streamHandle, KY_ACQ_QUEUE_INPUT, KY_ACQ_QUEUE_UNQUEUED);
                KYFG_StreamDelete(m_streamHandle);
                m_streamHandle = INVALID_STREAMHANDLE;
            }
            for (int i = 0; i < 16; ++i)
            {
                if (m_pBuffers[i])
                {
                    alignedFree(m_pBuffers[i]);
                    m_pBuffers[i] = nullptr;
                }
                m_streamBuffers[i] = 0;
            }

            // Apply new parameters to camera
            applyCameraParameters(true);

            // Recreate stream
            auto status = KYFG_StreamCreate(m_camHandle, &m_streamHandle, 0);
            if (status != FGSTATUS_OK || m_streamHandle == INVALID_STREAMHANDLE)
            {
                emit errorOccurred(m_camIndex, QString("Camera %1 stream recreate failed: %2").arg(m_camIndex).arg(status));
                return;
            }

            status = KYFG_StreamBufferCallbackRegister(m_streamHandle, streamCallback, this);
            if (status != FGSTATUS_OK)
            {
                emit errorOccurred(m_camIndex, QString("Camera %1 callback register failed: %2").arg(m_camIndex).arg(status));
                KYFG_StreamDelete(m_streamHandle);
                m_streamHandle = INVALID_STREAMHANDLE;
                return;
            }

            size_t frameDataSize = 0;
            size_t frameDataAligment = 0;
            status = KYFG_StreamGetInfo(m_streamHandle, KY_STREAM_INFO_PAYLOAD_SIZE, &frameDataSize, NULL, NULL);
            if (status != FGSTATUS_OK || frameDataSize == 0)
            {
                emit errorOccurred(m_camIndex, QString("Camera %1 payload size query failed: %2").arg(m_camIndex).arg(status));
                KYFG_StreamDelete(m_streamHandle);
                m_streamHandle = INVALID_STREAMHANDLE;
                return;
            }

            status = KYFG_StreamGetInfo(m_streamHandle, KY_STREAM_INFO_BUF_ALIGNMENT, &frameDataAligment, NULL, NULL);
            if (status != FGSTATUS_OK || frameDataAligment == 0)
            {
                emit errorOccurred(m_camIndex, QString("Camera %1 buffer alignment query failed: %2").arg(m_camIndex).arg(status));
                KYFG_StreamDelete(m_streamHandle);
                m_streamHandle = INVALID_STREAMHANDLE;
                return;
            }

            for (int i = 0; i < 16; ++i)
            {
                m_pBuffers[i] = alignedAllocate(frameDataSize, frameDataAligment);
                if (!m_pBuffers[i])
                {
                    emit errorOccurred(m_camIndex, QString("Camera %1 buffer allocation failed at index %2").arg(m_camIndex).arg(i));
                    for (int j = 0; j < i; ++j) {
                        if (m_pBuffers[j]) {
                            alignedFree(m_pBuffers[j]);
                            m_pBuffers[j] = nullptr;
                        }
                        m_streamBuffers[j] = 0;
                    }
                    KYFG_StreamBufferCallbackUnregister(m_streamHandle, streamCallback);
                    KYFG_StreamDelete(m_streamHandle);
                    m_streamHandle = INVALID_STREAMHANDLE;
                    return;
                }
                status = KYFG_BufferAnnounce(m_streamHandle, m_pBuffers[i], frameDataSize, NULL, &m_streamBuffers[i]);
                if (status != FGSTATUS_OK)
                {
                    emit errorOccurred(m_camIndex, QString("Camera %1 buffer announce failed at index %2: %3").arg(m_camIndex).arg(i).arg(status));
                    alignedFree(m_pBuffers[i]);
                    m_pBuffers[i] = nullptr;
                    for (int j = 0; j < i; ++j) {
                        if (m_pBuffers[j]) {
                            alignedFree(m_pBuffers[j]);
                            m_pBuffers[j] = nullptr;
                        }
                        m_streamBuffers[j] = 0;
                    }
                    KYFG_StreamBufferCallbackUnregister(m_streamHandle, streamCallback);
                    KYFG_StreamDelete(m_streamHandle);
                    m_streamHandle = INVALID_STREAMHANDLE;
                    return;
                }
            }
            {
                QMutexLocker callbackLocker(&m_callbackMutex);
                m_acceptStreamCallbacks = true;
            }
            m_frameDataSize = frameDataSize;
            m_streamReopenRequired = false;
            m_paramsApplied = true;
            m_actualPayloadSize = queryActualPayloadSize();
            if (m_actualPayloadSize > m_frameDataSize)
                m_actualPayloadSize = m_frameDataSize;
            emit geometryApplied(m_camIndex, m_params.width, m_params.height);
        }
    }
    catch (const std::exception& e) {
        emit errorOccurred(m_camIndex, QString("Exception in applyGeometry: %1").arg(e.what()));
    }
    catch (...) {
        emit errorOccurred(m_camIndex, "Unknown exception in applyGeometry");
    }
}
