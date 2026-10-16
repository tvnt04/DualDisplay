#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>
#include "CameraWorker.h"

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

    std::shared_ptr<FramePacket> process(std::shared_ptr<FramePacket> pkt) {
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

        auto out = std::make_shared<FramePacket>();
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

class CalibrationEngine : public QObject
{
    Q_OBJECT
public:
    explicit CalibrationEngine(QObject* parent = nullptr);
    ~CalibrationEngine();

    void startCalibration(bool isAutoDetect);
    void stopCalibration();
    bool isCalibrating() const { return m_calibrationMode; }

    void markCam1();
    void markCam2();

    void setLineDelay(int lines) { m_lineDelay = lines; }
    int getLineDelay() const { return m_lineDelay; }

    // Processes an incoming frame packet (updates line counts, performs auto-detection if active, and applies DelayBuffer)
    std::shared_ptr<FramePacket> processFrame(std::shared_ptr<FramePacket> pkt, int calibMode, double timeDelaySec, double fps);

signals:
    void lineDelayCalculated(int diffLines);
    void statusMessageChanged(const QString& msg);

private:
    bool m_calibrationMode = false;
    bool m_autoCam1Detected = false;
    bool m_autoCam2Detected = false;
    qint64 m_cam1MarkLines = 0;
    qint64 m_cam2MarkLines = 0;
    qint64 m_totalLinesReceived[2] = {0, 0};
    int m_lineDelay = 0;

    DelayBuffer m_cam1DelayBuffer;
    DelayBuffer m_cam2DelayBuffer;
};

