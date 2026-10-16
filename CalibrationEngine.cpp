#include "CalibrationEngine.h"
#include <QDateTime>
#include <cmath>

CalibrationEngine::CalibrationEngine(QObject* parent)
    : QObject(parent)
{
}

CalibrationEngine::~CalibrationEngine()
{
    blockSignals(true);
    stopCalibration();
}

void CalibrationEngine::startCalibration(bool isAutoDetect)
{
    m_calibrationMode = true;
    m_lineDelay = 0;
    m_cam1MarkLines = 0;
    m_cam2MarkLines = 0;
    m_autoCam1Detected = false;
    m_autoCam2Detected = false;

    if (isAutoDetect) {
        emit statusMessageChanged(" Waiting for CAM1... ");
    }
}

void CalibrationEngine::stopCalibration()
{
    m_calibrationMode = false;
    m_autoCam1Detected = false;
    m_autoCam2Detected = false;
}

void CalibrationEngine::markCam1()
{
    m_cam1MarkLines = m_totalLinesReceived[0];
}

void CalibrationEngine::markCam2()
{
    m_cam2MarkLines = m_totalLinesReceived[1];
    qint64 diff = m_cam2MarkLines - m_cam1MarkLines;
    m_lineDelay = static_cast<int>(diff);
    emit lineDelayCalculated(m_lineDelay);
}

std::shared_ptr<FramePacket> CalibrationEngine::processFrame(std::shared_ptr<FramePacket> pkt, int calibMode, double timeDelaySec, double fps)
{
    if (!pkt) return pkt;
    int idx = pkt->cameraIndex;
    if (idx < 0 || idx > 1) return pkt;

    const int H_total = pkt->height;
    const int H_img = (!pkt->rawData.empty() && H_total > 1) ? (H_total - 1) : H_total;
    m_totalLinesReceived[idx] += H_img;

    // Auto-detect algorithm
    if (m_calibrationMode && calibMode == 2) {
        int W = pkt->width;
        int H = H_img;
        const auto pixels = CameraWorker::displayPixelsWithoutMetadata(*pkt);
        if (W > 0 && H > 0 && pixels.size() >= static_cast<size_t>(W * H)) {
            int activeStart = H > 500 ? 100 : 0;
            int activeEnd = H > 500 ? H - 100 : H;

            double frameSum = 0;
            size_t count = 0;
            size_t startIdx = static_cast<size_t>(activeStart * W);
            size_t endIdx = static_cast<size_t>(activeEnd * W);
            for (size_t i = startIdx; i < endIdx; i += 256) {
                frameSum += pixels[i];
                count++;
            }
            double avgFrameVal = count > 0 ? frameSum / count : 0.0;
            bool isWhiteScreen = (avgFrameVal > 128);
            int detectedRow = -1;
            bool detected = false;

            if (isWhiteScreen) {
                double minRowAvg = 255.0;
                int minRow = activeStart;
                for (int y = activeStart; y < activeEnd; ++y) {
                    long sum = 0;
                    int countX = 0;
                    const uint8_t* row = &pixels[y * W];
                    for (int x = 0; x < W; x += 32) {
                        sum += row[x];
                        countX++;
                    }
                    double rowAvg = countX > 0 ? (double)sum / countX : 255.0;
                    if (rowAvg < minRowAvg) {
                        minRowAvg = rowAvg;
                        minRow = y;
                    }
                }
                if (minRowAvg < avgFrameVal - 30) {
                    detectedRow = minRow;
                    detected = true;
                }
            } else {
                double maxRowAvg = 0.0;
                int maxRow = activeStart;
                for (int y = activeStart; y < activeEnd; ++y) {
                    long sum = 0;
                    int countX = 0;
                    const uint8_t* row = &pixels[y * W];
                    for (int x = 0; x < W; x += 32) {
                        sum += row[x];
                        countX++;
                    }
                    double rowAvg = countX > 0 ? (double)sum / countX : 0.0;
                    if (rowAvg > maxRowAvg) {
                        maxRowAvg = rowAvg;
                        maxRow = y;
                    }
                }
                if (maxRowAvg > avgFrameVal + 30) {
                    detectedRow = maxRow;
                    detected = true;
                }
            }

            if (detected) {
                if (idx == 0 && !m_autoCam1Detected) {
                    m_cam1MarkLines = m_totalLinesReceived[0] - H + detectedRow;
                    m_autoCam1Detected = true;
                    emit statusMessageChanged(" Waiting for CAM2... ");
                }
                if (idx == 1 && m_autoCam1Detected && !m_autoCam2Detected) {
                    m_cam2MarkLines = m_totalLinesReceived[1];
                    m_autoCam2Detected = true;
                    qint64 diff = m_cam2MarkLines - m_cam1MarkLines;
                    m_lineDelay = static_cast<int>(diff);
                    emit lineDelayCalculated(m_lineDelay);
                }
            }
        }
    }

    int delayLines = 0;
    if (calibMode == 1) {
        double effectiveFps = fps <= 0 ? 1.0 : fps;
        delayLines = static_cast<int>(timeDelaySec * effectiveFps * H_img);
    } else {
        delayLines = m_lineDelay;
    }

    bool appliedLineDelay = false;
    if (idx == 0) {
        m_cam1DelayBuffer.delayLines = std::max(0, delayLines);
        appliedLineDelay = (m_cam1DelayBuffer.delayLines > 0);
        pkt = m_cam1DelayBuffer.process(pkt);
    } else if (idx == 1) {
        m_cam2DelayBuffer.delayLines = std::max(0, -delayLines);
        appliedLineDelay = (m_cam2DelayBuffer.delayLines > 0);
        pkt = m_cam2DelayBuffer.process(pkt);
    }

    if (appliedLineDelay) {
        pkt->timestampMs = QDateTime::currentMSecsSinceEpoch();
    }

    return pkt;
}

