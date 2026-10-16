#pragma once

#include <QString>
#include <QSettings>
#include <QByteArray>
#include <vector>

struct CameraUiState {
    QString width = "9344";
    QString height = "3840";
    QString appliedWidth = "9344";
    QString appliedHeight = "3840";
    QString fps = "1.0";
    QString exposure = "29999";
    QString gain = "1.0";
    QString testPattern = "Off";
    QString tdiMode = "Off";
    QString tdiStages = "4";
    QString tdiYShift = "16";
    QString tdiFrameCount = "10";
    bool reverseX = false;
    bool reverseY = false;
    QString savePath = "Capture";
    QString rawPath = "";
    QString saveFormat = ".raw";
    QString saveMode = "Single File";
    bool stopAfterEnabled = false;
    QString saveDuration = "5.0";
};

struct GeneralAppSettings {
    bool darkMode = false;
    int currentPreset = 0;
    QByteArray geometry;
    QByteArray windowState;
    bool displayStretch = false;
    bool pixelStretch = false;
    bool saveModeMerged = false;
    int lineDelay = 0;
    double timeDelay = 0.0;
    int calibMode = 0;
};

class SettingsManager
{
public:
    static QString getPreferencesFilePath();
    static void saveSettings(const GeneralAppSettings& general,
                             const CameraUiState cameraStates[2],
                             const std::vector<QString> regionModes[2],
                             const std::vector<QString> regionOffsets[2],
                             const std::vector<int> regionBinnings[2]);

    static void loadSettings(GeneralAppSettings& general,
                             CameraUiState cameraStates[2],
                             std::vector<QString> regionModes[2],
                             std::vector<QString> regionOffsets[2],
                             std::vector<int> regionBinnings[2]);

    static void resetAllValuesToDefault(GeneralAppSettings& general,
                                        CameraUiState cameraStates[2],
                                        std::vector<QString> regionModes[2],
                                        std::vector<QString> regionOffsets[2],
                                        std::vector<int> regionBinnings[2]);
};

