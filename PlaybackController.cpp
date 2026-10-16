#include "PlaybackController.h"
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
}

PlaybackController::~PlaybackController()
{
    blockSignals(true);
    for (int i = 0; i < 2; ++i) {
        stopPlayback(i, false);
    }
}

bool PlaybackController::isPlaying(int idx) const
{
    if (idx < 0 || idx > 1) return false;
    return m_playing[idx];
}

bool PlaybackController::startPlayback(int idx, const QString& path, int width, int height, double fps)
{
    if (idx < 0 || idx > 1) return false;
    if (m_playing[idx]) {
        stopPlayback(idx, false);
        return false;
    }

    if (path.trimmed().isEmpty()) {
        emit errorMessage("Playback Error", "Select a saved raw/bmp file or directory first.");
        return false;
    }

    QFileInfo info(path);
    if (!info.exists()) {
        emit errorMessage("Playback Error", QString("Playback source does not exist: %1").arg(path));
        return false;
    }

    if (fps <= 0.0) {
        emit errorMessage("Playback Error", "Invalid FPS value for playback.");
        return false;
    }

    m_playbackWidth[idx] = width;
    m_playbackHeight[idx] = height;

    if (!m_playbackTimers[idx]) {
        m_playbackTimers[idx] = new QTimer(this);
        connect(m_playbackTimers[idx], &QTimer::timeout, this, [this, idx](){ onTimerTimeout(idx); });
    }
    m_playbackTimers[idx]->setInterval(int(1000.0 / std::max(0.001, fps)));

    if (info.isDir()) {
        QDir dir(path);
        if (dir.exists("Capture.raw")) {
            if (width <= 0 || height <= 0) {
                emit errorMessage("Playback Error", "Width and height must be set for raw playback.");
                return false;
            }
            m_playbackFile[idx] = new QFile(dir.absoluteFilePath("Capture.raw"), this);
            if (!m_playbackFile[idx]->open(QIODevice::ReadOnly)) {
                emit errorMessage("Playback Error", QString("Unable to open Capture.raw in %1").arg(path));
                delete m_playbackFile[idx];
                m_playbackFile[idx] = nullptr;
                return false;
            }
            m_playbackFrameSize[idx] = qint64(width) * qint64(height);
            m_playbackRawSequence[idx] = true;
        } else {
            QStringList rawFiles = dir.entryList({"*.raw"}, QDir::Files, QDir::Name);
            QStringList bmpFiles = dir.entryList({"*.bmp"}, QDir::Files, QDir::Name);
            QStringList files;
            for (const QString& file : rawFiles) files << dir.absoluteFilePath(file);
            for (const QString& file : bmpFiles) files << dir.absoluteFilePath(file);
            if (files.isEmpty()) {
                emit errorMessage("Playback Error", QString("No .raw or .bmp files found in %1").arg(path));
                return false;
            }
            m_playbackFiles[idx] = files;
        }
    } else if (info.isFile()) {
        const QString extension = info.suffix().toLower();
        if (extension == "raw") {
            if (width <= 0 || height <= 0) {
                emit errorMessage("Playback Error", "Width and height must be set for raw playback.");
                return false;
            }
            m_playbackFile[idx] = new QFile(path, this);
            if (!m_playbackFile[idx]->open(QIODevice::ReadOnly)) {
                emit errorMessage("Playback Error", QString("Unable to open %1").arg(path));
                delete m_playbackFile[idx];
                m_playbackFile[idx] = nullptr;
                return false;
            }
            m_playbackFrameSize[idx] = qint64(width) * qint64(height);
            m_playbackRawSequence[idx] = true;
        } else if (extension == "bmp") {
            m_playbackFiles[idx].append(path);
        } else {
            emit errorMessage("Playback Error", "Unsupported playback file format.");
            return false;
        }
    } else {
        emit errorMessage("Playback Error", "Playback source is invalid.");
        return false;
    }

    m_playbackIndex[idx] = 0;
    m_playing[idx] = true;
    emit playbackStateChanged(idx, true);
    m_playbackTimers[idx]->start();
    emit statusMessage(QString("[Cam%1] Playback started.").arg(idx), 3000);
    onTimerTimeout(idx);
    return true;
}

void PlaybackController::stopPlayback(int idx, bool completed)
{
    if (idx < 0 || idx > 1) return;
    m_playing[idx] = false;
    if (m_playbackTimers[idx]) {
        m_playbackTimers[idx]->stop();
    }
    if (m_playbackFile[idx]) {
        if (m_playbackFile[idx]->isOpen()) {
            m_playbackFile[idx]->close();
        }
        delete m_playbackFile[idx];
        m_playbackFile[idx] = nullptr;
    }
    m_playbackFiles[idx].clear();
    m_playbackIndex[idx] = 0;
    m_playbackFrameSize[idx] = 0;
    m_playbackRawSequence[idx] = false;

    emit playbackStateChanged(idx, false);
    if (completed) {
        emit statusMessage(QString("[Cam%1] Playback complete.").arg(idx), 5000);
    } else {
        emit statusMessage(QString("[Cam%1] Playback stopped.").arg(idx), 3000);
    }
}

void PlaybackController::onTimerTimeout(int idx)
{
    if (idx < 0 || idx > 1 || !m_playing[idx]) return;

    if (!m_playbackFiles[idx].isEmpty()) {
        if (m_playbackIndex[idx] >= m_playbackFiles[idx].size()) {
            stopPlayback(idx, true);
            return;
        }

        const QString path = m_playbackFiles[idx].at(m_playbackIndex[idx]++);
        const QString extension = QFileInfo(path).suffix().toLower();

        auto pkt = std::make_shared<FramePacket>();
        pkt->cameraIndex = idx;

        if (extension == "raw") {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                stopPlayback(idx, false);
                emit errorMessage("Playback Error", QString("Unable to open %1").arg(path));
                return;
            }
            const int width = m_playbackWidth[idx];
            const int height = m_playbackHeight[idx];
            const qint64 bytesPerFrame = qint64(width) * qint64(height);
            if (bytesPerFrame <= 0) {
                file.close();
                stopPlayback(idx, false);
                emit errorMessage("Playback Error", "Invalid frame dimensions for raw playback.");
                return;
            }
            QByteArray raw = file.read(bytesPerFrame);
            file.close();
            if (raw.size() != bytesPerFrame) {
                stopPlayback(idx, true);
                return;
            }
            pkt->width = width;
            pkt->height = height;
            pkt->pixels.assign(reinterpret_cast<const uint8_t*>(raw.constData()),
                               reinterpret_cast<const uint8_t*>(raw.constData()) + raw.size());
        } else {
            QImage img(path);
            if (img.isNull()) {
                stopPlayback(idx, false);
                emit errorMessage("Playback Error", QString("Unable to load image %1").arg(path));
                return;
            }
            if (img.format() != QImage::Format_Grayscale8) {
                img = img.convertToFormat(QImage::Format_Grayscale8);
            }
            pkt->width = img.width();
            pkt->height = img.height();
            pkt->pixels.assign(img.constBits(), img.constBits() + img.sizeInBytes());
        }

        pkt->frameId = m_playbackIndex[idx];
        emit frameReady(pkt);
        return;
    }

    if (m_playbackFile[idx]) {
        if (!m_playbackFile[idx]->isOpen()) {
            stopPlayback(idx, false);
            return;
        }

        qint64 targetPos = qint64(m_playbackIndex[idx]) * m_playbackFrameSize[idx];
        if (m_playbackFile[idx]->pos() != targetPos) {
            m_playbackFile[idx]->seek(targetPos);
        }

        QByteArray raw = m_playbackFile[idx]->read(m_playbackFrameSize[idx]);
        if (raw.size() != m_playbackFrameSize[idx]) {
            stopPlayback(idx, true);
            return;
        }

        auto pkt = std::make_shared<FramePacket>();
        pkt->cameraIndex = idx;
        pkt->width = m_playbackWidth[idx];
        pkt->height = m_playbackHeight[idx];
        m_playbackIndex[idx]++;
        pkt->frameId = m_playbackIndex[idx];
        pkt->pixels.assign(reinterpret_cast<const uint8_t*>(raw.constData()),
                           reinterpret_cast<const uint8_t*>(raw.constData()) + raw.size());

        emit frameReady(pkt);
        return;
    }

    stopPlayback(idx, true);
}

bool PlaybackController::loadMetadata(const QString& path, PlaybackMetadata& outMeta)
{
    QFileInfo info(path);
    QString jsonPath;
    if (info.isDir()) {
        jsonPath = QDir(info.absoluteFilePath()).filePath("metadata.json");
    } else {
        jsonPath = QDir(info.absolutePath()).filePath("metadata.json");
    }

    QFile metadataFile(jsonPath);
    if (!metadataFile.exists() || !metadataFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray raw = metadataFile.readAll();
    metadataFile.close();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    outMeta.width = obj.value("Width").toVariant().toString();
    if (outMeta.width.isEmpty()) outMeta.width = obj.value("width").toVariant().toString();

    outMeta.height = obj.value("BandHeight").toVariant().toString();
    if (outMeta.height.isEmpty()) outMeta.height = obj.value("height").toVariant().toString();

    outMeta.fps = obj.value("FPS").toVariant().toString();
    if (outMeta.fps.isEmpty()) outMeta.fps = obj.value("fps").toVariant().toString();

    outMeta.saveFormat = obj.value("saveFormat").toString();
    if (outMeta.saveFormat.isEmpty()) {
        if (info.isDir()) {
            QDir dir(path);
            if (dir.exists("Capture.raw")) outMeta.saveFormat = ".raw";
            else {
                QStringList bmpFiles = dir.entryList({"*.bmp"}, QDir::Files);
                outMeta.saveFormat = (!bmpFiles.isEmpty()) ? ".bmp" : ".raw";
            }
        } else {
            outMeta.saveFormat = "." + info.suffix().toLower();
        }
    }

    outMeta.saveMode = obj.value("saveMode").toString();
    if (outMeta.saveMode.isEmpty()) {
        if (info.isDir()) {
            QDir dir(path);
            outMeta.saveMode = dir.exists("Capture.raw") ? "Single File" : "Image Sequence";
        } else {
            outMeta.saveMode = "Single File";
        }
    }

    return (!outMeta.width.isEmpty() && !outMeta.height.isEmpty());
}

