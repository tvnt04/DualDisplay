#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  CameraWorker.h
//  Encapsulates all camera I/O for ONE camera.
//  Runs in its own QThread.
//
//  STUB MODE (current):
//    openCamera()  → reads .raw file, emits cameraOpened
//    startStream() → loops raw file frames at set FPS, emits frameReady
//    stopStream()  → stops loop
//
//  REAL HARDWARE (swap in later — only this file + CameraWorker.cpp change):
//    Replace raw-file loop with KYFGLib calls.
//    All signals/slots, threading, UI stay identical.
// ═══════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <vector>
#include <atomic>
#include <cstdint>
#include <cstdio>

#if USING_STUB
#include "KYFGLib_Stub.h"
#else
#include "KYFGLibA.h"
#endif

#include <QDateTime>
#include <memory>

// ── FramePacket ──────────────────────────────────
struct FramePacket {
    std::vector<uint8_t> pixels;   // 8-bit grayscale, width*height bytes
    std::vector<uint8_t> rawData;  // Raw 10-bit packed data from camera
    int   width          = 0;
    int   height         = 0;
    int   frameId        = 0;
    int   brightestPixel = 0;
    int   cameraIndex    = 0;
    int   packedMin      = 0;
    int   packedMax      = 0;
    double instantFps    = 0.0;
    qint64 timestampMs   = 0;
};

Q_DECLARE_METATYPE(std::shared_ptr<FramePacket>)

// ── CameraParams ─────────────────────────────────────────────────────────
struct CameraParams {
    int     width          = 8192;
    int     height         = 5700;
    float   fps            = 1.0f;
    float   exposure       = 29999.0f;
    float   gain           = 1.0f;
    int     testPattern    = 0;
    uint8_t regionModeMask = 0x01;
    std::vector<int> regionOffsets = {0, 900, 2000, 3000, 4000, 5000, 6000};
    std::vector<int> regionBinningModes = {0,0,0,0,0,0,0};
    int     tdiMode        = 0;
    int     tdiStages      = 4;
    int     tdiYShift      = 16;
    int     tdiFrameCount  = 10;
    bool    reverseX       = false;
    bool    reverseY       = false;
    float   maxFps         = 0.0f;
    QString rawFilePath;           // path to .raw file for stub playback
};

Q_DECLARE_METATYPE(CameraParams)

// ════════════════════════════════════════════════════════════════════════
class CameraWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraWorker(int camIndex, QObject* parent = nullptr);
    ~CameraWorker();

    // ── SWAP POINT: real hardware only needs these two setters ──────────
    void setFgHandle (FGHANDLE fg);
    void setCamHandle(CAMHANDLE cam);

    // Utility: packed / unpacked raw formats -> 8-bit display
    static std::vector<uint8_t> unpack10bit(const uint8_t* src, int width, int height, size_t bufferSize);
    static std::vector<uint8_t> unpack12bit(const uint8_t* src, int width, int height, size_t bufferSize);
    static std::vector<uint8_t> unpackRawTo8bit(const uint8_t* src, int width, int height, size_t bufferSize);
    static std::vector<uint8_t> displayPixels(const FramePacket& pkt) {
        if (!pkt.pixels.empty()) return pkt.pixels;
        if (!pkt.rawData.empty()) return unpackRawTo8bit(pkt.rawData.data(), pkt.width, pkt.height, pkt.rawData.size());
        return {};
    }
    static std::vector<uint8_t> displayPixelsWithoutMetadata(const FramePacket& pkt) {
        std::vector<uint8_t> pixels = displayPixels(pkt);
        if (pixels.empty() || pkt.rawData.empty() || pkt.width <= 0 || pkt.height <= 1)
            return pixels;

        const size_t displayHeight = static_cast<size_t>(pkt.height - 1);
        const size_t expected = static_cast<size_t>(pkt.width) * displayHeight;
        if (pixels.size() < expected + static_cast<size_t>(pkt.width))
            return pixels;

        pixels.resize(expected);
        return pixels;
    }
    static size_t rawLinePitch(const FramePacket& pkt) {
        return (pkt.height > 0 && !pkt.rawData.empty() && pkt.rawData.size() % static_cast<size_t>(pkt.height) == 0)
            ? (pkt.rawData.size() / static_cast<size_t>(pkt.height))
            : 0;
    }
    static size_t rawBytesWithoutMetadata(const FramePacket& pkt) {
        const size_t linePitch = rawLinePitch(pkt);
        if (linePitch == 0 || pkt.height <= 0) return 0;
        if (pkt.height <= 1) return pkt.rawData.size();
        return linePitch * static_cast<size_t>(pkt.height - 1);
    }

signals:
    void frameReady   (std::shared_ptr<FramePacket> pkt);
    void logMessage   (const QString& msg);
    void streamStarted(int camIdx);
    void streamStopped(int camIdx);
    void cameraOpened (int camIdx, int width, int height);
    void cameraClosed (int camIdx);
    void geometryApplied(int camIdx, int width, int height);
    void errorOccurred(int camIdx, const QString& msg);
    void temperatureReady(int camIdx, float sensorTemp, float grabberTemp);
    void featureUnsupported(int camIdx, const QString& featureName);
    void paramsQueried(int camIdx, const CameraParams& p);
    void fpsClamped(int camIdx, double requestedFps, double maxFps);

public slots:
    // ── These slots are the entire API surface ───────────────────────
    void setParams  (const CameraParams& p);
    void openCamera ();   // connect to hardware / open raw file
    void closeCamera();   // disconnect
    void startStream();   // begin frame acquisition
    void stopStream ();   // halt acquisition
    void setSaving  (bool saving);
    void setLiveExposure(double exposureUs);
    void setLiveGain(double gainDb);
    void setLiveFps(double fps);
    void setLiveTestPattern(int pattern);
    void setLiveTdi(int mode, int stages, int yShift, int frameCount = 0);
    void setLiveReverseX(bool reverseX);
    void setLiveReverseY(bool reverseY);
    void applyGeometry(int width, int height);
    void applyAllParameters(const CameraParams& p);

private slots:
    void processFrame(quint64 epoch, STREAM_BUFFER_HANDLE streamBufferHandle, void* pFrameMemory, size_t bufferSize);
    void pollTemperature();

private:
    static void streamCallback(STREAM_BUFFER_HANDLE streamBufferHandle, void* userContext);
    bool beginStreamCallback();
    void endStreamCallback();
    void stopAcceptingStreamCallbacks();
    bool applyCameraParameters(bool includeGeometry);
    void restartStreamAfterLiveUpdate(bool wasStreaming);
    CameraParams queryCameraParameters();
    size_t queryActualPayloadSize() const;
    void cleanupCameraLocked();

    // ── Real hardware members ──────────────────────────────────────────
    STREAM_HANDLE       m_streamHandle = INVALID_STREAMHANDLE;
    STREAM_BUFFER_HANDLE m_streamBuffers[16];
    void*               m_pBuffers[16];
    size_t              m_frameDataSize = 0;
    size_t              m_actualPayloadSize = 0;

    int            m_camIndex;
    FGHANDLE       m_fgHandle  = INVALID_FGHANDLE;
    CAMHANDLE      m_camHandle = INVALID_CAMHANDLE;

    CameraParams   m_params;
    bool           m_paramsApplied = false;
    std::atomic<bool> m_streaming{false};
    std::atomic<bool> m_cameraOpened{false};
    std::atomic<bool> m_fileOpen{false};
    std::atomic<bool> m_saving{false};
    std::atomic<bool> m_streamReopenRequired{false};
    std::atomic<quint64> m_streamEpoch{1};

    FILE*          m_rawFile       = nullptr;
    long           m_bytesPerFrame = 0;
    int            m_frameId       = 0;
    QThread*       m_streamThread  = nullptr;
    QTimer*        m_tempTimer     = nullptr;
    QMutex         m_mutex;
    QMutex         m_callbackMutex;
    QWaitCondition m_callbacksDrained;
    bool           m_acceptStreamCallbacks = false;
    int            m_activeStreamCallbacks = 0;
};
