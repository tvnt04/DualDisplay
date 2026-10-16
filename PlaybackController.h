#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QFile>
#include <memory>
#include "CameraWorker.h"

struct PlaybackMetadata {
    QString width;
    QString height;
    QString fps;
    QString saveFormat;
    QString saveMode;
};

class PlaybackController : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);
    ~PlaybackController();

    bool isPlaying(int idx) const;
    bool startPlayback(int idx, const QString& path, int width, int height, double fps);
    void stopPlayback(int idx, bool completed = false);

    static bool loadMetadata(const QString& path, PlaybackMetadata& outMeta);

signals:
    void frameReady(std::shared_ptr<FramePacket> pkt);
    void playbackStateChanged(int idx, bool playing);
    void statusMessage(const QString& msg, int timeoutMs);
    void errorMessage(const QString& title, const QString& msg);

private slots:
    void onTimerTimeout(int idx);

private:
    bool        m_playing[2] = {false, false};
    QTimer*     m_playbackTimers[2] = {nullptr, nullptr};
    QFile*      m_playbackFile[2] = {nullptr, nullptr};
    QStringList m_playbackFiles[2];
    int         m_playbackIndex[2] = {0, 0};
    qint64      m_playbackFrameSize[2] = {0, 0};
    bool        m_playbackRawSequence[2] = {false, false};
    int         m_playbackWidth[2] = {0, 0};
    int         m_playbackHeight[2] = {0, 0};
};

