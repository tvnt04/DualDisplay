#pragma once
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include "AlignedMemory.h"
#include "CameraWorker.h"

struct MergeSaveOptions {
    int poolBufferCount  = 16;             // Number of preallocated buffers in pool
    size_t customBufferSize = 0;           // Optional explicit buffer size override (0 = auto-calculate)
    bool useSingleFile   = true;           // True: Merged_Capture.raw, False: frame_XXXXX.raw sequence
    QString saveFormat   = ".raw";         // ".raw" or ".bmp"
    int expectedWidth0   = 9344;
    int expectedHeight0  = 3840;
    int expectedWidth1   = 9344;
    int expectedHeight1  = 3840;
    double fps           = 10.0;
};

struct AlignedBuffer {
    uint8_t* memory = nullptr;
    size_t capacity = 0;
    size_t bytesUsed = 0;

    AlignedBuffer() = default;
    AlignedBuffer(size_t cap, size_t align = 4096) {
        allocate(cap, align);
    }
    ~AlignedBuffer() {
        freeMemory();
    }

    void allocate(size_t cap, size_t align = 4096) {
        freeMemory();
        capacity = cap;
        if (cap > 0) {
            memory = static_cast<uint8_t*>(alignedAllocate(cap, align));
            if (!memory) {
                memory = nullptr;
                capacity = 0;
            }
        }
    }

    void freeMemory() {
        if (memory) {
            alignedFree(memory);
            memory = nullptr;
        }
        capacity = 0;
        bytesUsed = 0;
    }

    // Prevent copying
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    // Allow move
    AlignedBuffer(AlignedBuffer&& o) noexcept {
        memory = o.memory; capacity = o.capacity; bytesUsed = o.bytesUsed;
        o.memory = nullptr; o.capacity = 0; o.bytesUsed = 0;
    }
    AlignedBuffer& operator=(AlignedBuffer&& o) noexcept {
        if (this != &o) {
            freeMemory();
            memory = o.memory; capacity = o.capacity; bytesUsed = o.bytesUsed;
            o.memory = nullptr; o.capacity = 0; o.bytesUsed = 0;
        }
        return *this;
    }
};

struct MergeTask {
    std::shared_ptr<FramePacket> pkt0;
    std::shared_ptr<FramePacket> pkt1;
    AlignedBuffer* buffer = nullptr;
    int frameIndex = 0;
    qint64 enqueueTimeMs = 0;
};

class MergeSaver : public QThread
{
    Q_OBJECT
public:
    explicit MergeSaver(QObject* parent = nullptr);
    ~MergeSaver();

    // Session Lifecycle
    bool startSession(const QString& sessionDir, const MergeSaveOptions& opts = MergeSaveOptions());
    void stopSession();
    bool isSaving() const { return m_running.load(); }

    // Enqueue synchronized camera frame pair (called from MainWindow onFrameReady)
    bool pushSyncedPair(std::shared_ptr<FramePacket> pkt0, std::shared_ptr<FramePacket> pkt1);

    // Diagnostics & Statistics
    uint64_t getSavedFrameCount() const { return m_savedFrameCount.load(); }
    uint64_t getDroppedFrameCount() const { return m_droppedFrameCount.load(); }
    size_t getQueueDepth() const;
    double getAverageWriteLatencyMs() const;
    double getWriteThroughputMBps() const;

signals:
    void logMessage(const QString& msg);
    void sessionStarted(const QString& dir);
    void sessionStopped(quint64 savedCount, quint64 droppedCount, double totalMB);
    void statsUpdated(quint64 savedCount, quint64 droppedCount, quint64 queueDepth);

protected:
    void run() override;

private:
    void cleanupSession();
    bool composeMergedRaw(const FramePacket& p0, const FramePacket& p1, AlignedBuffer& buf);
    bool composeMergedPixels(const FramePacket& p0, const FramePacket& p1, AlignedBuffer& buf, int& outW, int& outH);

    // Thread control & queue
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    std::queue<MergeTask> m_taskQueue;

    // Buffer Pool
    std::vector<std::unique_ptr<AlignedBuffer>> m_bufferPool;
    std::vector<AlignedBuffer*> m_freeBuffers;

    // Session Files & Options
    MergeSaveOptions m_options;
    QString m_sessionDir;
    FILE* m_rawFile = nullptr;
    QFile m_metaFile;
    QTextStream m_metaStream;

    // Stats
    std::atomic<uint64_t> m_savedFrameCount{0};
    std::atomic<uint64_t> m_droppedFrameCount{0};
    std::atomic<uint64_t> m_totalBytesWritten{0};
    int m_sessionFrameIndex = 0;
    qint64 m_sessionStartTimeMs = 0;
    qint64 m_firstSavedFrameTimestampMs = 0;
    qint64 m_lastSavedFrameTimestampMs = 0;
    double m_totalWriteLatencyMs = 0.0;
};
