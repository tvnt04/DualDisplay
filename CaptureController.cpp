#include "CaptureController.h"
#include "NativeFile.h"
#include <QDir>
#include <QDateTime>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <cmath>

CaptureController::CaptureController(QObject* parent)
    : QObject(parent)
{
    m_mergeSaver = new MergeSaver(this);
    connect(m_mergeSaver, &MergeSaver::logMessage, this, &CaptureController::logMessage);
    connect(m_mergeSaver, &MergeSaver::statsUpdated, this, &CaptureController::mergeStatsUpdated);
}

CaptureController::~CaptureController()
{
    blockSignals(true);
    for (int i = 0; i < 2; ++i) {
        stopSave(i, false);
    }
    stopMergeSave();
}

bool CaptureController::isSaving(int idx) const
{
    if (idx < 0 || idx > 1) return false;
    return m_saving[idx];
}

bool CaptureController::startSave(int idx, const CaptureSaveParams& params, const QString& initialLogText)
{
    if (idx < 0 || idx > 1) return false;
    if (m_saving[idx]) {
        stopSave(idx, false);
        return false;
    }

    m_saveParams[idx] = params;
    QString baseDir = params.savePath.trimmed().isEmpty() ? "Capture" : params.savePath.trimmed();
    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_saveDirs[idx] = QString("%1/Cam%2_%3").arg(baseDir).arg(idx).arg(timeStr);

    QDir d;
    if (!d.mkpath(m_saveDirs[idx])) {
        emit logMessage(QString("[Cam%1] Failed to create save directory: %2").arg(idx).arg(m_saveDirs[idx]));
        return false;
    }

    m_saveFrameCount[idx] = 0;
    m_firstSavedFrameMs[idx] = 0;
    m_lastSavedFrameMs[idx] = 0;
    m_savedFrameWidth[idx] = params.width;
    m_savedFrameHeight[idx] = params.height;
    m_emptyRawFrameSkips[idx] = 0;

    if (params.saveFormat == ".raw" && params.saveMode == "Single File") {
        m_saveFiles[idx] = openBinaryFile(m_saveDirs[idx] + "/Capture.raw");
        if (!m_saveFiles[idx]) {
            emit logMessage(QString("[Cam%1] Failed to open save file in %2").arg(idx).arg(m_saveDirs[idx]));
            return false;
        }
    } else {
        m_saveFiles[idx] = nullptr;
    }

    m_saving[idx] = true;
    emit saveStateChanged(idx, true);

    writeSaveMetadata(idx, true, initialLogText);

    if (params.stopAfterEnabled && params.saveDuration > 0.0) {
        if (!m_countdownTimers[idx]) {
            m_countdownTimers[idx] = new QTimer(this);
            connect(m_countdownTimers[idx], &QTimer::timeout, this, [this, idx](){ onStreamCountdown(idx); });
        }
        m_streamEndMs[idx] = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(params.saveDuration * 1000.0);
        m_countdownTimers[idx]->start(100);
        onStreamCountdown(idx);
    } else {
        stopStreamCountdown(idx);
    }

    emit statusMessage(QString("[Cam%1] Recording started in %2").arg(idx).arg(m_saveDirs[idx]), 3000);
    return true;
}

void CaptureController::stopSave(int idx, bool completed)
{
    if (idx < 0 || idx > 1 || !m_saving[idx]) return;

    stopStreamCountdown(idx);
    m_saving[idx] = false;

    if (m_saveFiles[idx]) {
        fflush(m_saveFiles[idx]);
        fclose(m_saveFiles[idx]);
        m_saveFiles[idx] = nullptr;
    }

    emit saveStateChanged(idx, false);
    writeSaveMetadata(idx, false);

    if (completed) {
        emit statusMessage(QString("[Cam%1] Save complete (%2 frames).").arg(idx).arg(m_saveFrameCount[idx]), 5000);
    } else {
        emit statusMessage(QString("[Cam%1] Save stopped.").arg(idx), 3000);
    }
}

void CaptureController::writeFrame(int idx, std::shared_ptr<FramePacket> pkt)
{
    if (idx < 0 || idx > 1 || !m_saving[idx] || !pkt) return;

    const QString saveMode = m_saveParams[idx].saveMode;
    const QString saveFormat = m_saveParams[idx].saveFormat;
    bool writeSuccess = true;
    QString errorMsg;

    auto recordSavedFrame = [this, idx, &pkt]() {
        if (m_saveFrameCount[idx] == 0) {
            m_firstSavedFrameMs[idx] = pkt->timestampMs;
            m_savedFrameWidth[idx] = pkt->width;
            m_savedFrameHeight[idx] = (!pkt->rawData.empty() && pkt->height > 1) ? pkt->height - 1 : pkt->height;
        }
        m_lastSavedFrameMs[idx] = pkt->timestampMs;
        ++m_saveFrameCount[idx];
    };

    try {
        if (saveFormat == ".bmp") {
            const auto pixels = CameraWorker::displayPixelsWithoutMetadata(*pkt);
            const int displayHeight = (pkt->rawData.empty() || pkt->height <= 1) ? pkt->height : (pkt->height - 1);
            if (pkt->width <= 0 || displayHeight <= 0) return;
            const size_t expectedBytes = static_cast<size_t>(pkt->width) * static_cast<size_t>(displayHeight);
            if (pixels.size() < expectedBytes) return;

            const QString fileName = QString("%1/frame_%2.bmp")
                .arg(m_saveDirs[idx])
                .arg(m_saveFrameCount[idx] + 1, 5, 10, QChar('0'));

            QImage img(pixels.data(), pkt->width, displayHeight, pkt->width, QImage::Format_Grayscale8);
            writeSuccess = img.save(fileName, "BMP");
            if (!writeSuccess) {
                errorMsg = QString("Failed to save BMP frame: %1").arg(fileName);
            } else {
                recordSavedFrame();
            }
        } else {
            if (pkt->rawData.empty()) {
                if (m_emptyRawFrameSkips[idx]++ == 0) {
                    emit logMessage(QString("[Cam%1] Skipping queued frame without raw camera bytes at save start.").arg(idx));
                }
                return;
            }

            const size_t rawBytesToWrite = CameraWorker::rawBytesWithoutMetadata(*pkt);
            if (rawBytesToWrite == 0) {
                if (m_emptyRawFrameSkips[idx]++ == 0) {
                    emit logMessage(QString("[Cam%1] Skipping raw frame: metadata stripping produced no image bytes.").arg(idx));
                }
                return;
            }

            if (saveMode == "Single File") {
                if (m_saveFiles[idx]) {
                    size_t written = fwrite(pkt->rawData.data(), 1, rawBytesToWrite, m_saveFiles[idx]);
                    if (written != rawBytesToWrite) {
                        writeSuccess = false;
                        errorMsg = "fwrite returned incomplete write count (disk full?)";
                    }
                } else {
                    writeSuccess = false;
                    errorMsg = "Single File handle was null";
                }
                if (writeSuccess) {
                    recordSavedFrame();
                }
            } else {
                const QString fileName = QString("%1/frame_%2.raw")
                    .arg(m_saveDirs[idx])
                    .arg(m_saveFrameCount[idx] + 1, 5, 10, QChar('0'));
                QFile f(fileName);
                if (f.open(QIODevice::WriteOnly)) {
                    qint64 written = f.write(reinterpret_cast<const char*>(pkt->rawData.data()), rawBytesToWrite);
                    f.close();
                    if (written != static_cast<qint64>(rawBytesToWrite)) {
                        writeSuccess = false;
                        errorMsg = "QFile::write failed to write complete frame data (disk full?)";
                    }
                } else {
                    writeSuccess = false;
                    errorMsg = QString("Failed to open file for write: %1").arg(fileName);
                }
                if (writeSuccess) {
                    recordSavedFrame();
                }
            }
        }
    } catch (const std::exception& e) {
        writeSuccess = false;
        errorMsg = QString("Exception writing frame: %1").arg(e.what());
    } catch (...) {
        writeSuccess = false;
        errorMsg = "Unknown exception writing frame";
    }

    if (!writeSuccess) {
        stopSave(idx, false);
        emit logMessage(QString("[Cam%1] Save stopped due to write failure: %2").arg(idx).arg(errorMsg));
    }
}

void CaptureController::writeSaveMetadata(int idx, bool isCaptureStart, const QString& initialLogText)
{
    if (idx < 0 || idx > 1 || m_saveDirs[idx].isEmpty()) return;

    const int frameWidth = m_savedFrameWidth[idx] > 0 ? m_savedFrameWidth[idx] : m_saveParams[idx].width;
    const int bandHeight = m_savedFrameHeight[idx] > 0 ? m_savedFrameHeight[idx] : m_saveParams[idx].height;
    int tdiStages = m_saveParams[idx].tdiStages;
    int tdiModeInt = (m_saveParams[idx].tdiMode == "Record") ? 1 : ((m_saveParams[idx].tdiMode == "TDI") ? 2 : 0);

    int regionHeight = bandHeight;
    if (tdiModeInt == 2 && tdiStages > 0) {
        regionHeight = bandHeight * tdiStages;
    }

    QString capturesFolder = m_saveDirs[idx];
    if (capturesFolder.startsWith(QDir::currentPath() + "/")) {
        capturesFolder = capturesFolder.mid(QDir::currentPath().length() + 1);
    }
    if (!capturesFolder.endsWith("/")) capturesFolder += "/";

    int totalFrames = m_saveFrameCount[idx];
    int totalHeight = bandHeight * totalFrames;
    double recordedFps = m_saveParams[idx].fps;
    const qint64 captureSpanMs = m_lastSavedFrameMs[idx] - m_firstSavedFrameMs[idx];
    const bool fpsMeasured = totalFrames > 1 && captureSpanMs > 0;
    if (fpsMeasured) {
        recordedFps = static_cast<double>(totalFrames - 1) * 1000.0 / captureSpanMs;
    }

    QJsonObject metadata;
    metadata["BandHeight"] = bandHeight;
    metadata["CapturesFolder"] = capturesFolder;
    metadata["ExposureTime"] = m_saveParams[idx].exposure;
    metadata["FPS"] = recordedFps;
    metadata["ConfiguredFPS"] = m_saveParams[idx].fps;
    metadata["FPSMeasured"] = fpsMeasured;
    metadata["PixelEncoding"] = (m_saveParams[idx].saveFormat == ".bmp") ? "Gray8" : "CameraRaw";
    metadata["Gain"] = m_saveParams[idx].gain;
    metadata["Height"] = totalHeight;
    metadata["RegionHeight"] = regionHeight;
    metadata["TDIMode"] = tdiModeInt;
    metadata["TDIStages"] = (tdiModeInt == 2) ? tdiStages : 0;
    metadata["TDIFrameCount"] = (tdiModeInt == 1) ? m_saveParams[idx].tdiFrameCount : 0;
    metadata["TotalFrames"] = totalFrames;
    metadata["Width"] = frameWidth;
    metadata["coreTemperature"] = m_saveParams[idx].grabberTemp;
    metadata["sensorTemperature"] = m_saveParams[idx].sensorTemp;

    QFile metadataFile(m_saveDirs[idx] + "/metadata.json");
    if (metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
        metadataFile.close();
    }

    if (isCaptureStart && !initialLogText.isEmpty()) {
        QFile logFile(m_saveDirs[idx] + "/session.log");
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << initialLogText;
            if (!initialLogText.endsWith("\n")) out << "\n";
            logFile.close();
        }

        QFile termFile(m_saveDirs[idx] + "/terminal.log");
        if (termFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&termFile);
            out << initialLogText;
            if (!initialLogText.endsWith("\n")) out << "\n";
            termFile.close();
        }
    }
}

void CaptureController::appendTelemetry(const QString& line)
{
    for (int i = 0; i < 2; ++i) {
        if (m_saving[i] && !m_saveDirs[i].isEmpty()) {
            QFile logFile(m_saveDirs[i] + "/session.log");
            if (logFile.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&logFile);
                out << line << "\n";
                logFile.close();
            }
        }
    }
}

void CaptureController::onStreamCountdown(int idx)
{
    if (idx < 0 || idx > 1 || !m_saving[idx] || m_streamEndMs[idx] <= 0) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 remainMs = m_streamEndMs[idx] - now;

    if (remainMs <= 0) {
        stopStreamCountdown(idx);
        stopSave(idx, true);
        return;
    }

    double remainSec = static_cast<double>(remainMs) / 1000.0;
    emit countdownUpdated(idx, QString("Stop: %1s").arg(remainSec, 0, 'f', 1));
}

void CaptureController::stopStreamCountdown(int idx)
{
    if (idx < 0 || idx > 1) return;
    if (m_countdownTimers[idx]) {
        m_countdownTimers[idx]->stop();
    }
    m_streamEndMs[idx] = 0;
    emit countdownUpdated(idx, QString());
}

bool CaptureController::isMergeSaving() const
{
    return m_mergeSaving;
}

bool CaptureController::startMergeSave(const QString& sessionDir, const MergeSaveOptions& opts)
{
    if (m_mergeSaving) return false;
    m_mergedSaveDir = sessionDir;
    m_mergeSaving = m_mergeSaver->startSession(m_mergedSaveDir, opts);
    emit mergeSaveStateChanged(m_mergeSaving);
    return m_mergeSaving;
}

void CaptureController::stopMergeSave()
{
    if (!m_mergeSaving) return;
    m_mergeSaver->stopSession();
    m_mergeSaving = false;
    emit mergeSaveStateChanged(false);
}

bool CaptureController::pushMergedPair(std::shared_ptr<FramePacket> pkt0, std::shared_ptr<FramePacket> pkt1)
{
    if (!m_mergeSaving || !m_mergeSaver) return false;
    return m_mergeSaver->pushSyncedPair(pkt0, pkt1);
}

