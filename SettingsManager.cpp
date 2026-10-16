#include "SettingsManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>

QString SettingsManager::getPreferencesFilePath()
{
    QString currentDir = QDir::currentPath();
    QString iniInCwd = currentDir + "/ui_preferences.ini";
    if (QFileInfo::exists(iniInCwd) && QFileInfo(iniInCwd).isWritable()) {
        return iniInCwd;
    }

    QString appDir = QCoreApplication::applicationDirPath();
    QString iniInApp = appDir + "/ui_preferences.ini";
    if (QFileInfo::exists(iniInApp) && QFileInfo(iniInApp).isWritable()) {
        return iniInApp;
    }

    QFileInfo cwdInfo(currentDir);
    if (cwdInfo.isWritable()) {
        return iniInCwd;
    }
    return iniInApp;
}

void SettingsManager::saveSettings(const GeneralAppSettings& general,
                                   const CameraUiState cameraStates[2],
                                   const std::vector<QString> regionModes[2],
                                   const std::vector<QString> regionOffsets[2],
                                   const std::vector<int> regionBinnings[2])
{
    const QString prefPath = getPreferencesFilePath();
    QSettings settings(prefPath, QSettings::IniFormat);

    settings.beginGroup("General");
    settings.setValue("darkMode", general.darkMode);
    settings.setValue("currentPreset", general.currentPreset);
    settings.setValue("geometry", general.geometry);
    settings.setValue("windowState", general.windowState);
    settings.setValue("displayStretch", general.displayStretch);
    settings.setValue("pixelStretch", general.pixelStretch);
    settings.setValue("saveModeMerged", general.saveModeMerged);
    settings.setValue("lineDelay", general.lineDelay);
    settings.setValue("timeDelay", general.timeDelay);
    settings.setValue("calibMode", general.calibMode);
    settings.endGroup();

    for (int i = 0; i < 2; ++i)
    {
        settings.beginGroup(QString("Camera%1").arg(i));
        const CameraUiState& state = cameraStates[i];
        settings.setValue("width", state.width);
        settings.setValue("height", state.height);
        settings.setValue("fps", state.fps);
        settings.setValue("exposure", state.exposure);
        settings.setValue("gain", state.gain);
        settings.setValue("testPattern", state.testPattern);
        settings.setValue("tdiMode", state.tdiMode);
        settings.setValue("tdiStages", state.tdiStages);
        settings.setValue("tdiYShift", state.tdiYShift);
        settings.setValue("tdiFrameCount", state.tdiFrameCount);
        settings.setValue("reverseX", state.reverseX);
        settings.setValue("reverseY", state.reverseY);
        settings.setValue("savePath", state.savePath);
        settings.setValue("rawPath", state.rawPath);
        settings.setValue("saveFormat", state.saveFormat);
        settings.setValue("saveMode", state.saveMode);
        settings.setValue("stopAfterEnabled", state.stopAfterEnabled);
        settings.setValue("saveDuration", state.saveDuration);

        for (int r = 0; r < 7; ++r)
        {
            if (r < static_cast<int>(regionModes[i].size()))
                settings.setValue(QString("region%1_mode").arg(r), regionModes[i][r]);
            if (r < static_cast<int>(regionOffsets[i].size()))
                settings.setValue(QString("region%1_offset").arg(r), regionOffsets[i][r]);
            if (r < static_cast<int>(regionBinnings[i].size()))
                settings.setValue(QString("region%1_binning").arg(r), regionBinnings[i][r]);
        }
        settings.endGroup();
    }

    settings.sync();

    // Mirror to legacy QSettings for backwards compatibility
    QSettings legacySettings("KAYA", "SpikeDualDisplay");
    legacySettings.setValue("darkMode", general.darkMode);
    legacySettings.setValue("currentPreset", general.currentPreset);
    legacySettings.setValue("geometry", general.geometry);
    legacySettings.setValue("windowState", general.windowState);
    for (int i = 0; i < 2; ++i)
    {
        const CameraUiState& state = cameraStates[i];
        legacySettings.setValue(QString("cam%1_width").arg(i), state.width);
        legacySettings.setValue(QString("cam%1_height").arg(i), state.height);
        legacySettings.setValue(QString("cam%1_fps").arg(i), state.fps);
        legacySettings.setValue(QString("cam%1_exposure").arg(i), state.exposure);
        legacySettings.setValue(QString("cam%1_gain").arg(i), state.gain);
        legacySettings.setValue(QString("cam%1_testPattern").arg(i), state.testPattern);
        legacySettings.setValue(QString("cam%1_tdiMode").arg(i), state.tdiMode);
        legacySettings.setValue(QString("cam%1_tdiStages").arg(i), state.tdiStages);
        legacySettings.setValue(QString("cam%1_tdiYShift").arg(i), state.tdiYShift);
        legacySettings.setValue(QString("cam%1_tdiFrameCount").arg(i), state.tdiFrameCount);
    }
}

void SettingsManager::loadSettings(GeneralAppSettings& general,
                                   CameraUiState cameraStates[2],
                                   std::vector<QString> regionModes[2],
                                   std::vector<QString> regionOffsets[2],
                                   std::vector<int> regionBinnings[2])
{
    const QString prefPath = getPreferencesFilePath();
    QSettings settings(prefPath, QSettings::IniFormat);
    QSettings legacySettings("KAYA", "SpikeDualDisplay");

    auto getValue = [&](const QString& group, const QString& key, const QVariant& defVal, const QString& legacyKey = QString()) -> QVariant {
        if (settings.contains(group + "/" + key)) {
            return settings.value(group + "/" + key);
        }
        if (!legacyKey.isEmpty() && legacySettings.contains(legacyKey)) {
            return legacySettings.value(legacyKey);
        }
        return defVal;
    };

    general.geometry = getValue("General", "geometry", QByteArray(), "geometry").toByteArray();
    general.windowState = getValue("General", "windowState", QByteArray(), "windowState").toByteArray();
    general.darkMode = getValue("General", "darkMode", false, "darkMode").toBool();
    general.displayStretch = settings.value("General/displayStretch", false).toBool();
    general.pixelStretch = settings.value("General/pixelStretch", false).toBool();
    general.saveModeMerged = settings.value("General/saveModeMerged", false).toBool();
    general.currentPreset = getValue("General", "currentPreset", 0, "currentPreset").toInt();
    general.lineDelay = settings.value("General/lineDelay", 0).toInt();
    general.timeDelay = settings.value("General/timeDelay", 0.0).toDouble();
    general.calibMode = settings.value("General/calibMode", 0).toInt();

    static const int defOffsets[7] = {0, 900, 2000, 3000, 4000, 5000, 6000};

    for (int i = 0; i < 2; ++i)
    {
        CameraUiState& state = cameraStates[i];
        const QString group = QString("Camera%1").arg(i);
        const QString legPfx = QString("cam%1_").arg(i);

        state.width = getValue(group, "width", "9344", legPfx + "width").toString();
        state.height = getValue(group, "height", "3840", legPfx + "height").toString();
        state.fps = getValue(group, "fps", "1.0", legPfx + "fps").toString();
        state.exposure = getValue(group, "exposure", "29999", legPfx + "exposure").toString();
        state.gain = getValue(group, "gain", "1.0", legPfx + "gain").toString();
        state.testPattern = getValue(group, "testPattern", "Off", legPfx + "testPattern").toString();
        state.tdiMode = getValue(group, "tdiMode", "Off", legPfx + "tdiMode").toString();
        state.tdiStages = getValue(group, "tdiStages", "4", legPfx + "tdiStages").toString();
        state.tdiYShift = getValue(group, "tdiYShift", "16", legPfx + "tdiYShift").toString();
        state.tdiFrameCount = getValue(group, "tdiFrameCount", "10", legPfx + "tdiFrameCount").toString();
        state.reverseX = settings.value(group + "/reverseX", false).toBool();
        state.reverseY = settings.value(group + "/reverseY", false).toBool();
        state.savePath = settings.value(group + "/savePath", "Capture").toString();
        state.rawPath = settings.value(group + "/rawPath", "").toString();
        state.saveFormat = settings.value(group + "/saveFormat", ".raw").toString();
        state.saveMode = settings.value(group + "/saveMode", "Single File").toString();
        state.stopAfterEnabled = settings.value(group + "/stopAfterEnabled", false).toBool();
        state.saveDuration = settings.value(group + "/saveDuration", "5.0").toString();

        state.appliedWidth = state.width;
        state.appliedHeight = state.height;

        regionModes[i].resize(7);
        regionOffsets[i].resize(7);
        regionBinnings[i].resize(7);

        for (int r = 0; r < 7; ++r)
        {
            regionModes[i][r] = settings.value(QString("%1/region%2_mode").arg(group).arg(r), r == 0 ? "On" : "Off").toString();
            regionOffsets[i][r] = settings.value(QString("%1/region%2_offset").arg(group).arg(r), QString::number(defOffsets[r])).toString();
            regionBinnings[i][r] = settings.value(QString("%1/region%2_binning").arg(group).arg(r), 0).toInt();
        }
    }
}

void SettingsManager::resetAllValuesToDefault(GeneralAppSettings& general,
                                              CameraUiState cameraStates[2],
                                              std::vector<QString> regionModes[2],
                                              std::vector<QString> regionOffsets[2],
                                              std::vector<int> regionBinnings[2])
{
    const QString prefPath = getPreferencesFilePath();
    if (QFileInfo::exists(prefPath)) {
        QFile::remove(prefPath);
    }
    QSettings legacySettings("KAYA", "SpikeDualDisplay");
    legacySettings.clear();

    general.darkMode = false;
    general.currentPreset = 0;
    general.displayStretch = false;
    general.pixelStretch = false;
    general.saveModeMerged = false;
    general.lineDelay = 0;
    general.timeDelay = 0.0;
    general.calibMode = 0;

    static const int defOffsets[7] = {0, 900, 2000, 3000, 4000, 5000, 6000};
    for (int i = 0; i < 2; ++i)
    {
        CameraUiState& state = cameraStates[i];
        state.width = "9344";
        state.height = "3840";
        state.fps = "1.0";
        state.exposure = "29999";
        state.gain = "1.0";
        state.testPattern = "Off";
        state.tdiMode = "Off";
        state.tdiStages = "4";
        state.tdiYShift = "16";
        state.tdiFrameCount = "10";
        state.reverseX = false;
        state.reverseY = false;
        state.savePath = "Capture";
        state.rawPath = "";
        state.saveFormat = ".raw";
        state.saveMode = "Single File";
        state.stopAfterEnabled = false;
        state.saveDuration = "5.0";

        state.appliedWidth = state.width;
        state.appliedHeight = state.height;

        regionModes[i].resize(7);
        regionOffsets[i].resize(7);
        regionBinnings[i].resize(7);

        for (int r = 0; r < 7; ++r)
        {
            regionModes[i][r] = (r == 0 ? "On" : "Off");
            regionOffsets[i][r] = QString::number(defOffsets[r]);
            regionBinnings[i][r] = 0;
        }
    }
}

