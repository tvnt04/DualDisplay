#include "MergeSaver.h"
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <cstring>
#include <chrono>
#include <algorithm>

#include <QJsonObject>
#include <QJsonDocument>

MergeSaver::MergeSaver(QObject* parent)
    : QThread(parent)
{
}

MergeSaver::~MergeSaver()
{
    stopSession();
}

bool MergeSaver::startSession(const QString& sessionDir, const MergeSaveOptions& opts)
{
    if (m_running.load()) {
        emit logMessage("[MergeSaver] Session already active.");
        return false;
    }

    m_sessionDir = sessionDir;
    m_options = opts;

    QDir dir;
    if (!dir.mkpath(m_sessionDir)) {
        emit logMessage(QString("[MergeSaver] Failed to create directory: %1").arg(m_sessionDir));
        return false;
    }

    // Reset stats
    m_savedFrameCount.store(0);
    m_droppedFrameCount.store(0);
    m_totalBytesWritten.store(0);
    m_sessionFrameIndex = 0;
    m_sessionStartTimeMs = QDateTime::currentMSecsSinceEpoch();
    m_firstSavedFrameTimestampMs = 0;
    m_lastSavedFrameTimestampMs = 0;
    m_totalWriteLatencyMs = 0.0;

    // Write metadata.json for OfflineViewer compatibility
    {
        QFile jsonFile(m_sessionDir + "/metadata.json");
        if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QJsonObject metaObj;
            metaObj["isMerged"] = true;
            metaObj["Width"] = m_options.expectedWidth0 + m_options.expectedWidth1;
            metaObj["BandHeight"] = std::min(m_options.expectedHeight0 > 0 ? m_options.expectedHeight0 : 3840,
                                             m_options.expectedHeight1 > 0 ? m_options.expectedHeight1 : 3840);
            metaObj["FPS"] = (m_options.fps > 0) ? m_options.fps : 10.0;
            metaObj["ConfiguredFPS"] = (m_options.fps > 0) ? m_options.fps : 10.0;
            metaObj["FPSMeasured"] = false;
            metaObj["PixelEncoding"] = "Gray8";
            metaObj["saveFormat"] = m_options.saveFormat;
            metaObj["saveMode"] = m_options.useSingleFile ? "Single File" : "Image Sequence";
            jsonFile.write(QJsonDocument(metaObj).toJson(QJsonDocument::Indented));
            jsonFile.close();
        }
    }

    // Open metadata CSV log
    const QString metaPath = m_sessionDir + "/metadata.csv";
    m_metaFile.setFileName(metaPath);
    if (!m_metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage(QString("[MergeSaver] Failed to open metadata file: %1").arg(metaPath));
        return false;
    }
    m_metaStream.setDevice(&m_metaFile);
    m_metaStream << "frame_index,cam0_frame_id,cam0_timestamp,cam1_frame_id,cam1_timestamp,write_latency_ms,dropped_count,filename\n";
    m_metaStream.flush();

    // Open single raw file if option selected
    if (m_options.useSingleFile) {
        const QString rawPath = m_sessionDir + "/Merged_Capture.raw";
        m_rawFile = fopen(rawPath.toLocal8Bit().constData(), "wb");
        if (!m_rawFile) {
            emit logMessage(QString("[MergeSaver] Failed to open single raw output file: %1").arg(rawPath));
            m_metaFile.close();
            return false;
        }
    } else {
        m_rawFile = nullptr;
    }

    // Preallocate pool of memory-aligned buffers up front
    {
        QMutexLocker locker(&m_mutex);
        while (!m_taskQueue.empty()) m_taskQueue.pop();
        m_bufferPool.clear();
        m_freeBuffers.clear();

        const int poolCount = std::max(4, m_options.poolBufferCount);
        size_t requiredSize = opts.customBufferSize;
        if (requiredSize == 0) {
            const int w0 = opts.expectedWidth0 > 0 ? opts.expectedWidth0 : 9344;
            const int h0 = opts.expectedHeight0 > 0 ? opts.expectedHeight0 : 3840;
            const int w1 = opts.expectedWidth1 > 0 ? opts.expectedWidth1 : 9344;
            const int h1 = opts.expectedHeight1 > 0 ? opts.expectedHeight1 : 3840;

            // Both merged output formats contain the composed 8-bit grayscale
            // image, whose canvas is (w0 + w1) by max(h0, h1).
            requiredSize = (static_cast<size_t>(w0) + static_cast<size_t>(w1))
                * static_cast<size_t>(std::max(h0, h1));
        }

        m_bufferPool.reserve(poolCount);
        m_freeBuffers.reserve(poolCount);
        for (int i = 0; i < poolCount; ++i) {
            auto buf = std::make_unique<AlignedBuffer>(requiredSize, 4096);
            m_freeBuffers.push_back(buf.get());
            m_bufferPool.push_back(std::move(buf));
        }
    }

    m_stopRequested.store(false);
    m_running.store(true);
    start(QThread::HighPriority);

    emit logMessage("[MergeSave] Started");
    emit sessionStarted(m_sessionDir);
    return true;
}

void MergeSaver::stopSession()
{
    if (!m_running.load()) return;

    m_stopRequested.store(true);
    m_cond.wakeAll();

    // Wait for save thread to finish flushing queue
    wait(10000);

    double recordedFps = m_options.fps > 0.0 ? m_options.fps : 10.0;
    const qint64 captureSpanMs = m_lastSavedFrameTimestampMs - m_firstSavedFrameTimestampMs;
    const uint64_t savedFrames = m_savedFrameCount.load();
    const bool fpsMeasured = savedFrames > 1 && captureSpanMs > 0;
    if (fpsMeasured) {
        recordedFps = static_cast<double>(savedFrames - 1) * 1000.0 / captureSpanMs;
    }

    QFile metadataFile(m_sessionDir + "/metadata.json");
    if (metadataFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
        metadataFile.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject metaObj = doc.object();
            metaObj["FPS"] = recordedFps;
            metaObj["FPSMeasured"] = fpsMeasured;
            metaObj["ConfiguredFPS"] = m_options.fps > 0.0 ? m_options.fps : 10.0;
            metaObj["TotalFrames"] = static_cast<qint64>(savedFrames);
            if (metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                metadataFile.write(QJsonDocument(metaObj).toJson(QJsonDocument::Indented));
                metadataFile.close();
            }
        }
    }

    cleanupSession();
    m_running.store(false);

    const double totalMB = static_cast<double>(m_totalBytesWritten.load()) / (1024.0 * 1024.0);
    emit logMessage("[MergeSave] Stopped");
    emit sessionStopped(m_savedFrameCount.load(), m_droppedFrameCount.load(), totalMB);
}

void MergeSaver::cleanupSession()
{
    if (m_rawFile) {
        fflush(m_rawFile);
        fclose(m_rawFile);
        m_rawFile = nullptr;
    }

    if (m_metaFile.isOpen()) {
        m_metaStream.flush();
        m_metaFile.close();
    }

    QMutexLocker locker(&m_mutex);
    while (!m_taskQueue.empty()) {
        MergeTask task = m_taskQueue.front();
        m_taskQueue.pop();
        if (task.buffer) {
            m_freeBuffers.push_back(task.buffer);
        }
    }
    m_bufferPool.clear();
    m_freeBuffers.clear();
}

bool MergeSaver::pushSyncedPair(std::shared_ptr<FramePacket> pkt0, std::shared_ptr<FramePacket> pkt1)
{
    if (!m_running.load() || !pkt0 || !pkt1) return false;

    const bool isBmp = (m_options.saveFormat == ".bmp");
    if (isBmp) {
        if (CameraWorker::displayPixels(*pkt0).empty() || CameraWorker::displayPixels(*pkt1).empty()) {
            m_droppedFrameCount.fetch_add(1);
            return false;
        }
    } else {
        if (pkt0->rawData.empty() || pkt1->rawData.empty()) {
            m_droppedFrameCount.fetch_add(1);
            return false;
        }
    }

    QMutexLocker locker(&m_mutex);

    // Check if free buffer is available in pool
    if (m_freeBuffers.empty()) {
        // Pool exhausted -> Drop frame to avoid blocking GUI thread
        m_droppedFrameCount.fetch_add(1);
        emit statsUpdated(m_savedFrameCount.load(), m_droppedFrameCount.load(), m_taskQueue.size());
        return false; // Dropped
    }

    // Reserve buffer from pool right here in producer!
    AlignedBuffer* reservedBuf = m_freeBuffers.back();
    m_freeBuffers.pop_back();

    // Enqueue task for background save thread
    MergeTask task;
    task.pkt0 = pkt0;
    task.pkt1 = pkt1;
    task.buffer = reservedBuf;
    task.frameIndex = ++m_sessionFrameIndex;
    task.enqueueTimeMs = QDateTime::currentMSecsSinceEpoch();

    m_taskQueue.push(task);
    m_cond.wakeOne();
    return true;
}

size_t MergeSaver::getQueueDepth() const
{
    QMutexLocker locker(&m_mutex);
    return m_taskQueue.size();
}

double MergeSaver::getAverageWriteLatencyMs() const
{
    uint64_t count = m_savedFrameCount.load();
    return count > 0 ? m_totalWriteLatencyMs / count : 0.0;
}

double MergeSaver::getWriteThroughputMBps() const
{
    qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_sessionStartTimeMs;
    if (elapsedMs <= 0) return 0.0;
    double mb = static_cast<double>(m_totalBytesWritten.load()) / (1024.0 * 1024.0);
    return (mb * 1000.0) / elapsedMs;
}

bool MergeSaver::composeMergedRaw(const FramePacket& p0, const FramePacket& p1, AlignedBuffer& buf)
{
    int outW = 0, outH = 0;
    return composeMergedPixels(p0, p1, buf, outW, outH);
}

bool MergeSaver::composeMergedPixels(const FramePacket& p0, const FramePacket& p1, AlignedBuffer& buf, int& outW, int& outH)
{
    const auto pix0 = CameraWorker::displayPixelsWithoutMetadata(p0);
    const auto pix1 = CameraWorker::displayPixelsWithoutMetadata(p1);
    if (pix0.empty() || pix1.empty()) return false;

    const int w0 = p0.width > 0 ? p0.width : 1;
    const int h0 = (p0.rawData.empty() || p0.height <= 1) ? (p0.height > 0 ? p0.height : 1) : (p0.height - 1);
    const int w1 = p1.width > 0 ? p1.width : 1;
    const int h1 = (p1.rawData.empty() || p1.height <= 1) ? (p1.height > 0 ? p1.height : 1) : (p1.height - 1);

    outW = w0 + w1;
    outH = std::max(h0, h1);

    const size_t totalPixels = static_cast<size_t>(outW) * outH;
    if (buf.capacity < totalPixels) {
        emit logMessage(QString("[MergeSaver] Error: Preallocated buffer capacity (%1) smaller than merged pixel image size (%2)").arg(buf.capacity).arg(totalPixels));
        return false;
    }
    if (!buf.memory) return false;

    const uint8_t* src0 = pix0.data();
    const uint8_t* src1 = pix1.data();
    uint8_t* dst = buf.memory;

    std::memset(dst, 0, totalPixels);

    for (int y = 0; y < outH; ++y) {
        uint8_t* rowDst = dst + y * outW;
        if (y < h0) {
            std::memcpy(rowDst, src0 + y * w0, w0);
        }
        if (y < h1) {
            std::memcpy(rowDst + w0, src1 + y * w1, w1);
        }
    }

    buf.bytesUsed = totalPixels;
    return true;
}

void MergeSaver::run()
{
    auto recordSavedPair = [this](const MergeTask& task) {
        const qint64 ts0 = task.pkt0->timestampMs;
        const qint64 ts1 = task.pkt1->timestampMs;
        const qint64 pairTimestampMs = ts0 + (ts1 - ts0) / 2;
        if (m_savedFrameCount.load() == 0)
            m_firstSavedFrameTimestampMs = pairTimestampMs;
        m_lastSavedFrameTimestampMs = pairTimestampMs;
        m_savedFrameCount.fetch_add(1);
    };

    while (!m_stopRequested.load()) {
        MergeTask task;

        {
            QMutexLocker locker(&m_mutex);
            while (m_taskQueue.empty() && !m_stopRequested.load()) {
                m_cond.wait(&m_mutex, 100);
            }

            if (m_taskQueue.empty()) {
                if (m_stopRequested.load()) break;
                continue;
            }

            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        if (!task.pkt0 || !task.pkt1 || !task.buffer) continue;

        AlignedBuffer* targetBuf = task.buffer;

        const auto startTime = std::chrono::high_resolution_clock::now();

        const bool isBmp = (m_options.saveFormat == ".bmp");
        bool writeSuccess = false;
        QString fileNameStr;

        if (isBmp) {
            int outW = 0, outH = 0;
            bool composed = composeMergedPixels(*task.pkt0, *task.pkt1, *targetBuf, outW, outH);
            if (composed && targetBuf->bytesUsed > 0) {
                fileNameStr = QString("merged_frame_%1.bmp").arg(task.frameIndex, 5, 10, QChar('0'));
                const QString fullPath = m_sessionDir + "/" + fileNameStr;
                QImage img(targetBuf->memory, outW, outH, outW, QImage::Format_Grayscale8);
                writeSuccess = img.save(fullPath, "BMP");
            }
        } else {
            bool composed = composeMergedRaw(*task.pkt0, *task.pkt1, *targetBuf);
            if (composed && targetBuf->bytesUsed > 0) {
                if (m_options.useSingleFile) {
                    if (m_rawFile) {
                        size_t written = fwrite(targetBuf->memory, 1, targetBuf->bytesUsed, m_rawFile);
                        if (written == targetBuf->bytesUsed) {
                            writeSuccess = true;
                            fileNameStr = "Merged_Capture.raw";
                        }
                    }
                } else {
                    fileNameStr = QString("merged_frame_%1.raw").arg(task.frameIndex, 5, 10, QChar('0'));
                    const QString fullPath = m_sessionDir + "/" + fileNameStr;
                    FILE* f = fopen(fullPath.toLocal8Bit().constData(), "wb");
                    if (f) {
                        size_t written = fwrite(targetBuf->memory, 1, targetBuf->bytesUsed, f);
                        fclose(f);
                        if (written == targetBuf->bytesUsed) {
                            writeSuccess = true;
                        }
                    }
                }
            }
        }

        const auto endTime = std::chrono::high_resolution_clock::now();
        const double latencyMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        if (writeSuccess) {
            recordSavedPair(task);
            m_totalBytesWritten.fetch_add(targetBuf->bytesUsed);
            m_totalWriteLatencyMs += latencyMs;

            // 3. Append telemetry to metadata.csv
            m_metaStream << task.frameIndex << ","
                         << task.pkt0->frameId << "," << task.pkt0->timestampMs << ","
                         << task.pkt1->frameId << "," << task.pkt1->timestampMs << ","
                         << QString::number(latencyMs, 'f', 2) << ","
                         << m_droppedFrameCount.load() << ","
                         << fileNameStr << "\n";
            m_metaStream.flush();
        } else {
            m_droppedFrameCount.fetch_add(1);
        }

        // Return buffer to pool
        {
            QMutexLocker locker(&m_mutex);
            m_freeBuffers.push_back(targetBuf);
        }

        emit statsUpdated(m_savedFrameCount.load(), m_droppedFrameCount.load(), m_taskQueue.size());
    }

    // Flush any remaining tasks in queue during shutdown
    while (true) {
        MergeTask task;
        {
            QMutexLocker locker(&m_mutex);
            if (m_taskQueue.empty()) break;
            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        if (task.buffer) {
            const bool isBmp = (m_options.saveFormat == ".bmp");
            if (isBmp) {
                int outW = 0, outH = 0;
                if (composeMergedPixels(*task.pkt0, *task.pkt1, *task.buffer, outW, outH) && task.buffer->bytesUsed > 0) {
                    const QString fileNameStr = QString("merged_frame_%1.bmp").arg(task.frameIndex, 5, 10, QChar('0'));
                    const QString fullPath = m_sessionDir + "/" + fileNameStr;
                    QImage img(task.buffer->memory, outW, outH, outW, QImage::Format_Grayscale8);
                    if (img.save(fullPath, "BMP")) {
                        recordSavedPair(task);
                        m_totalBytesWritten.fetch_add(task.buffer->bytesUsed);
                    }
                }
            } else {
                if (composeMergedRaw(*task.pkt0, *task.pkt1, *task.buffer) && task.buffer->bytesUsed > 0) {
                    if (m_options.useSingleFile && m_rawFile) {
                        size_t written = fwrite(task.buffer->memory, 1, task.buffer->bytesUsed, m_rawFile);
                        if (written == task.buffer->bytesUsed) {
                            recordSavedPair(task);
                            m_totalBytesWritten.fetch_add(task.buffer->bytesUsed);
                        }
                    }
                }
            }

            {
                QMutexLocker locker(&m_mutex);
                m_freeBuffers.push_back(task.buffer);
            }
        }
    }
}
