#include "CameraPanel.h"
#include <algorithm>
#include <cmath>
#include <QTabWidget>

CameraPanel::CameraPanel(int camIndex, QWidget* parent)
    : QFrame(parent), m_camIndex(camIndex)
{
    setObjectName("cameraPanel");
    setMinimumWidth(300);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(4);

    // ── Title row ────────────────────────────────────────────────────
    QHBoxLayout* titleRow = new QHBoxLayout();
    titleLabel = new QLabel(QString("  CAM %1").arg(camIndex));
    titleLabel->setObjectName("panelTitle");
    fpsLabel = new QLabel("— fps");
    fpsLabel->setObjectName("fpsLabel");
    fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sensorTempLabel = new QLabel("S: — °C");
    sensorTempLabel->setObjectName("sensorTempLabel");
    sensorTempLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grabberTempLabel = new QLabel("G: — °C");
    grabberTempLabel->setObjectName("grabberTempLabel");
    grabberTempLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(titleLabel);
    titleRow->addWidget(new QLabel(" "));
    copySettingsBtn = new QPushButton(QString("Apply CAM %1 Settings").arg(1 - m_camIndex));
    copySettingsBtn->setObjectName("presetBtn");
    copySettingsBtn->setToolTip(QString("Apply all settings from CAM %1 to CAM %2").arg(1 - m_camIndex).arg(m_camIndex));
    connect(copySettingsBtn, &QPushButton::clicked, [this]{ emit copySettingsClicked(m_camIndex); });
    titleRow->addWidget(copySettingsBtn);
    titleRow->addStretch();
    titleRow->addWidget(sensorTempLabel);
    titleRow->addWidget(new QLabel(" ")); // tiny spacer
    titleRow->addWidget(grabberTempLabel);
    titleRow->addWidget(new QLabel("  ")); // spacer
    titleRow->addWidget(fpsLabel);
    root->addLayout(titleRow);

    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("separator");
    root->addWidget(sep);

    // ── Form ─────────────────────────────────────────────────────────
    QFormLayout* form = new QFormLayout();
    form->setSpacing(3);
    form->setContentsMargins(0, 1, 0, 1);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Playback path
    QHBoxLayout* rawRow = new QHBoxLayout();
    rawPathEdit = new QLineEdit();
    browseRawBtn = new QPushButton("…");
    browseRawBtn->setObjectName("smallBtn");
    browseRawBtn->setFixedWidth(26);
    connect(browseRawBtn, &QPushButton::clicked, [this]{ emit browseRaw(m_camIndex); });
    rawRow->addWidget(rawPathEdit);
    rawRow->addWidget(browseRawBtn);
    form->addRow("Playback:", rawRow);

    // Save path
    QHBoxLayout* saveRow = new QHBoxLayout();
    savePathEdit = new QLineEdit("Capture");
    browseSaveBtn = new QPushButton("…");
    browseSaveBtn->setObjectName("smallBtn");
    browseSaveBtn->setFixedWidth(26);
    connect(browseSaveBtn, &QPushButton::clicked, [this]{ emit browseSave(m_camIndex); });
    saveFormatCombo = new QComboBox();
    saveFormatCombo->addItems({".raw", ".bmp"});
    saveRow->addWidget(savePathEdit); saveRow->addWidget(browseSaveBtn); saveRow->addWidget(saveFormatCombo);
    form->addRow("Save:", saveRow);

    saveModeCombo = new QComboBox();
    saveModeCombo->addItems({"Sequence", "Single File"});
    saveModeCombo->setCurrentText("Single File");

    stopAfterDurationCheck = new QCheckBox("Enable stop after duration");
    saveDurationEdit = new QLineEdit("5.0");
    saveDurationEdit->setPlaceholderText("Seconds");
    saveDurationEdit->setEnabled(false);
    connect(stopAfterDurationCheck, &QCheckBox::toggled,
            saveDurationEdit, &QLineEdit::setEnabled);

    QWidget* durationRow = new QWidget();
    QHBoxLayout* durationRowLayout = new QHBoxLayout(durationRow);
    durationRowLayout->setContentsMargins(0, 0, 0, 0);
    durationRowLayout->setSpacing(4);
    durationRowLayout->addWidget(stopAfterDurationCheck);
    durationRowLayout->addWidget(saveDurationEdit, 1);

    form->addRow("Save Mode:", saveModeCombo);
    form->addRow("Stop After (s):", durationRow);

    widthEdit       = new QLineEdit("9344");
    heightEdit      = new QLineEdit("3840");
    fpsEdit         = new QLineEdit("1.0");
    exposureEdit    = new QLineEdit("29999");
    gainEdit        = new QLineEdit("1.0");
    testPatternCombo = new QComboBox();
    testPatternCombo->addItems({"Off", "Gray Diagonal Ramp"});

    widthEdit->setValidator(new QIntValidator(1024, 9344, widthEdit));
    heightEdit->setValidator(new QIntValidator(4, 7001, heightEdit));
    fpsEdit->setValidator(new QDoubleValidator(0.1, 100.0, 2, fpsEdit));
    exposureEdit->setValidator(new QDoubleValidator(1.0, 100000.0, 1, exposureEdit));
    gainEdit->setValidator(new QDoubleValidator(1.0, 16.0, 2, gainEdit));

    widthEdit->setToolTip("Width (min 8192, max 9344)");
    heightEdit->setToolTip("Height / Region Height per active region (Total acquired size displayed at bottom of image)");
    fpsEdit->setToolTip("Frame Rate (Hz)");
    exposureEdit->setToolTip("Exposure Time (µs)");
    gainEdit->setToolTip("Sensor Gain (1.0 to 16.0)");

    form->addRow("Width:",        widthEdit);
    form->addRow("Height:",       heightEdit);
    form->addRow("FPS:",          fpsEdit);
    form->addRow("Exposure µs:",  exposureEdit);
    form->addRow("Gain:",         gainEdit);
    form->addRow("Test Pattern:", testPatternCombo);

    tdiModeCombo = new QComboBox();
    tdiModeCombo->addItems({"Off", "Record", "TDI"});
    tdiModeCombo->setCurrentIndex(0);

    tdiStagesLabel = new QLabel("TDI Stages:");
    tdiYShiftLabel = new QLabel("TDI Y Shift:");
    tdiFrameCountLabel = new QLabel("Frame Count:");

    tdiStagesEdit = new QLineEdit("4");
    tdiYShiftEdit = new QLineEdit("16");
    tdiFrameCountEdit = new QLineEdit("10");

    tdiStagesEdit->setValidator(new QIntValidator(1, 32, tdiStagesEdit));
    tdiYShiftEdit->setValidator(new QIntValidator(0, 4096, tdiYShiftEdit));
    tdiFrameCountEdit->setValidator(new QIntValidator(1, 65535, tdiFrameCountEdit));

    auto updateTdiPanelVisibility = [this](int idx){
        const bool isTdi = (idx == 2);
        const bool isRecord = (idx == 1);
        if (tdiStagesLabel) tdiStagesLabel->setVisible(isTdi);
        if (tdiStagesEdit) {
            tdiStagesEdit->setVisible(isTdi);
            tdiStagesEdit->setEnabled(isTdi);
        }
        if (tdiYShiftLabel) tdiYShiftLabel->setVisible(isTdi);
        if (tdiYShiftEdit) {
            tdiYShiftEdit->setVisible(isTdi);
            tdiYShiftEdit->setEnabled(isTdi);
        }
        if (tdiFrameCountLabel) tdiFrameCountLabel->setVisible(isRecord);
        if (tdiFrameCountEdit) {
            tdiFrameCountEdit->setVisible(isRecord);
            tdiFrameCountEdit->setEnabled(isRecord);
        }
    };

    connect(tdiModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateTdiPanelVisibility);
    updateTdiPanelVisibility(tdiModeCombo->currentIndex());

    reverseXCheck = new QCheckBox("Reverse X");
    reverseYCheck = new QCheckBox("Reverse Y");
    
    form->addRow("TDI Mode:", tdiModeCombo);
    form->addRow(tdiStagesLabel, tdiStagesEdit);
    form->addRow(tdiYShiftLabel, tdiYShiftEdit);
    form->addRow(tdiFrameCountLabel, tdiFrameCountEdit);
    form->addRow("", reverseXCheck);
    form->addRow("", reverseYCheck);

    root->addLayout(form);

    // ── Region modes ─────────────────────────────────────────────────
    m_toggleRegionBtn = new QPushButton("▶  Region Modes");
    m_toggleRegionBtn->setObjectName("toggleBtn");
    connect(m_toggleRegionBtn, &QPushButton::clicked, this, &CameraPanel::onToggleRegions);
    root->addWidget(m_toggleRegionBtn);

    m_regionGroup = new QGroupBox();
    m_regionGroup->setObjectName("regionGroup");
    QFormLayout* rf = new QFormLayout(m_regionGroup);
    rf->setSpacing(2);
    const int defOffsets[] = {0, 900, 2000, 3000, 4000, 5000, 6000};
    for (int i = 0; i < 7; ++i)
    {
        regionModeCombos[i]  = new QComboBox();
        regionModeCombos[i]->addItems({"On","Off"});
        regionModeCombos[i]->setCurrentIndex(i == 0 ? 0 : 1);
        regionOffsetEdits[i] = new QLineEdit(QString::number(defOffsets[i]));
        regionBinningCombos[i] = new QComboBox();
        regionBinningCombos[i]->addItems({"Off", "2x2", "4x4"});
        regionBinningCombos[i]->setCurrentIndex(0);
        rf->addRow(QString("R%1 Mode:").arg(i),   regionModeCombos[i]);
        rf->addRow(QString("R%1 Offset:").arg(i), regionOffsetEdits[i]);
        rf->addRow(QString("R%1 Binning:").arg(i), regionBinningCombos[i]);
        connect(regionModeCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int){
            syncBinningControlsWithRegionModes();
        });
    }
    syncBinningControlsWithRegionModes();
    m_regionGroup->setVisible(false);
    root->addWidget(m_regionGroup);

    // ── Buttons ──────────────────────────────────────────────────────
    QHBoxLayout* b1 = new QHBoxLayout(); b1->setSpacing(4);
    connectBtn = new QPushButton("Connect");
    startBtn   = new QPushButton("Start");
    startBtn->setEnabled(false);
    connectBtn->setObjectName("connectBtn");
    startBtn->setObjectName("startBtn");
    connect(connectBtn, &QPushButton::clicked, [this]{ emit connectClicked(m_camIndex); });
    connect(startBtn,   &QPushButton::clicked, [this]{ emit startClicked  (m_camIndex); });
    b1->addWidget(connectBtn); b1->addWidget(startBtn);
    root->addLayout(b1);

    QHBoxLayout* b2 = new QHBoxLayout(); b2->setSpacing(4);
    saveBtn         = new QPushButton("Start Save");
    saveBtn->setObjectName("saveBtn");
    connect(saveBtn, &QPushButton::clicked, [this]{ emit saveClicked(m_camIndex); });
    playbackBtn     = new QPushButton("Play");
    playbackBtn->setObjectName("playBtn");
    connect(playbackBtn, &QPushButton::clicked, [this]{ emit playbackClicked(m_camIndex); });
    b2->addWidget(saveBtn);
    b2->addWidget(playbackBtn);
    root->addLayout(b2);

    root->addStretch(1);

    // ── Status ───────────────────────────────────────────────────────
    statusLabel = new QLabel("Disconnected  |  Frame: 0  |  Min: —  |  Max: —");
    statusLabel->setObjectName("statusLabel");
    root->addWidget(statusLabel);

    // ── Live view ────────────────────────────────────────────────────
    scrollArea = new QScrollArea();
    scrollArea->setObjectName("scrollArea");
    imageLabel = new QLabel("No Signal");
    imageLabel->setObjectName("imageLabel");
    imageLabel->setAlignment(Qt::AlignCenter);
    scrollArea->setWidget(imageLabel);
    scrollArea->setWidgetResizable(true);
    root->addWidget(scrollArea, 1);
}

void CameraPanel::onToggleRegions()
{
    m_regionsVisible = !m_regionsVisible;
    m_regionGroup->setVisible(m_regionsVisible);
    m_toggleRegionBtn->setText(m_regionsVisible ? "▼  Region Modes" : "▶  Region Modes");
}

void CameraPanel::setConnected(bool v)
{
    connectBtn->setText(v ? "Disconnect" : "Connect");
    connectBtn->setProperty("connected", v);
    connectBtn->style()->unpolish(connectBtn);
    connectBtn->style()->polish(connectBtn);
    startBtn->setEnabled(v);
    if (!v) { clearFrame(); statusLabel->setText("Disconnected  |  Frame: 0  |  Min: —  |  Max: —"); }
}

void CameraPanel::setStreaming(bool v)
{
    startBtn->setText(v ? "Stop" : "Start");
    startBtn->setProperty("streaming", v);
    startBtn->style()->unpolish(startBtn);
    startBtn->style()->polish(startBtn);
    if (!v) fpsLabel->setText("— fps");

    // While streaming, disable non-streamable parameters (like Vision Studio)
    if (widthEdit) widthEdit->setEnabled(!v);
    if (heightEdit) heightEdit->setEnabled(!v);
    if (tdiModeCombo) tdiModeCombo->setEnabled(!v);
    if (tdiStagesEdit) tdiStagesEdit->setEnabled(!v);
    if (tdiYShiftEdit) tdiYShiftEdit->setEnabled(!v);
    if (tdiFrameCountEdit) tdiFrameCountEdit->setEnabled(!v);
    if (connectBtn) connectBtn->setEnabled(!v);
    if (saveBtn) saveBtn->setEnabled(v);
    if (m_toggleRegionBtn) m_toggleRegionBtn->setEnabled(!v);
    if (m_regionGroup) m_regionGroup->setEnabled(!v);
}

void CameraPanel::setSaving(bool v)
{
    saveBtn->setText(v ? "Stop Save" : "Start Save");
    saveBtn->setProperty("saving", v);
    saveBtn->style()->unpolish(saveBtn);
    saveBtn->style()->polish(saveBtn);

    if (saveFormatCombo) saveFormatCombo->setEnabled(!v);
    if (saveModeCombo) saveModeCombo->setEnabled(!v);
    if (savePathEdit) savePathEdit->setEnabled(!v);
    if (browseRawBtn) browseRawBtn->setEnabled(!v);
    if (stopAfterDurationCheck) stopAfterDurationCheck->setEnabled(!v);
    if (saveDurationEdit) saveDurationEdit->setEnabled(!v && stopAfterDurationCheck && stopAfterDurationCheck->isChecked());
}

void CameraPanel::setAcquisitionFps(double fps)
{
    m_acquisitionFps = std::max(0.0, fps);
    fpsLabel->setText(m_acquisitionFps > 0.0
        ? QString("%1 fps").arg(m_acquisitionFps, 0, 'f', 1)
        : "— fps");
}

bool CameraPanel::updateFrame(const FramePacket& pkt, bool fitToScreen, bool stretch, QSize targetSize, bool isNewFrame)
{
    bool isOffline = false;
    QWidget* parentW = parentWidget();
    while (parentW) {
        if (parentW->inherits("QTabWidget")) {
            QTabWidget* tw = qobject_cast<QTabWidget*>(parentW);
            if (tw && tw->currentIndex() == 1) {
                isOffline = true;
            }
            break;
        }
        parentW = parentW->parentWidget();
    }

    const auto pixels = CameraWorker::displayPixelsWithoutMetadata(pkt);
    const int displayHeight = (pkt.rawData.empty() || pkt.height <= 1) ? pkt.height : (pkt.height - 1);
    if (pkt.width <= 0 || displayHeight <= 0) return false;
    const size_t expectedBytes = static_cast<size_t>(pkt.width) * static_cast<size_t>(displayHeight);
    if (pixels.size() < expectedBytes) return false;

    if (!isOffline)
    {
        // ── Pixel stretch ────────────────────────────────────────────────
        const uint8_t* src = pixels.data();
        std::vector<uint8_t> buf;
        if (stretch && !pixels.empty())
        {
            // Sample pixels to build histogram (stride of 64 for speed on large images)
            size_t hist[256] = {0};
            size_t sampleCount = 0;
            for (size_t i = 0; i < pixels.size(); i += 64)
            {
                hist[pixels[i]]++;
                sampleCount++;
            }

            // Find the 0.5% and 99.5% percentiles to ignore hot/cold pixel outliers
            size_t threshold = sampleCount * 0.005; // 0.5% cutoff
            
            int mn = 0;
            size_t count = 0;
            for (int v = 0; v < 256; ++v)
            {
                count += hist[v];
                if (count > threshold)
                {
                    mn = v;
                    break;
                }
            }

            int mx = 255;
            count = 0;
            for (int v = 255; v >= 0; --v)
            {
                count += hist[v];
                if (count > threshold)
                {
                    mx = v;
                    break;
                }
            }

            if (mn != mx)
            {
                buf.resize(pixels.size());
                float s = 255.0f / (mx - mn);
                for (size_t i = 0; i < pixels.size(); ++i)
                {
                    int val = pixels[i];
                    if (val < mn) val = mn;
                    if (val > mx) val = mx;
                    buf[i] = (uint8_t)((val - mn) * s);
                }
                src = buf.data();
            }
        }

        QImage img(src, pkt.width, displayHeight, pkt.width, QImage::Format_Grayscale8);
        if (fitToScreen && !targetSize.isEmpty())
        {
            QImage scaledImg = img.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            lastPixmap = QPixmap::fromImage(scaledImg);
        }
        else
        {
            lastPixmap = QPixmap::fromImage(img);
        }
    }

    if (isNewFrame)
        updateFrameInfo(pkt, pixels);

    // If detached, caller (MainWindow) will send to DetachedView instead
    if (isDetached) return false;

    if (!isOffline) {
        imageLabel->setPixmap(lastPixmap);
        imageLabel->adjustSize();
    }
    return true;
}

void CameraPanel::updateFrameInfo(const FramePacket& pkt, const std::vector<uint8_t>& pixels)
{
    ++m_fpsFrameCount;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_fpsLastMs == 0) m_fpsLastMs = now;
    if (now - m_fpsLastMs >= 1000)
    {
        lastFps = m_fpsFrameCount * 1000.0 / (now - m_fpsLastMs);
        if (m_acquisitionFps <= 0.0)
            fpsLabel->setText(QString("%1 fps").arg(lastFps, 0, 'f', 1));
        m_fpsFrameCount = 0;
        m_fpsLastMs = now;
    }

    static int statsCounter[2] = {0, 0};
    const int idx = (pkt.cameraIndex >= 0 && pkt.cameraIndex < 2) ? pkt.cameraIndex : 0;
    statsCounter[idx]++;
    if (statsCounter[idx] >= 15 || pkt.frameId < 5)
    {
        statsCounter[idx] = 0;
        const int minPixel = pixels.empty()
            ? 0
            : static_cast<int>(*std::min_element(pixels.begin(), pixels.end()));
        const int maxPixel = pixels.empty()
            ? 0
            : static_cast<int>(*std::max_element(pixels.begin(), pixels.end()));

        double meanPixel = 0.0;
        double variance = 0.0;
        double sdPixel = 0.0;
        if (!pixels.empty())
        {
            double sum = 0.0;
            double sumSq = 0.0;
            for (uint8_t value : pixels)
            {
                sum += value;
                sumSq += double(value) * double(value);
            }
            meanPixel = sum / pixels.size();
            variance = (sumSq / pixels.size()) - (meanPixel * meanPixel);
            if (variance < 0.0) variance = 0.0;
            sdPixel = std::sqrt(variance);
        }

        lastBrightestPixel = maxPixel;
        statusLabel->setText(QString("Size: %1 x %2  |  Frame: %3  |  Min: %4  |  Max: %5  |  Mean: %6  |  SD: %7  |  Var: %8")
                                 .arg(pkt.width)
                                 .arg(pkt.height)
                                 .arg(pkt.frameId)
                                 .arg(minPixel)
                                 .arg(maxPixel)
                                 .arg(meanPixel, 0, 'f', 1)
                                 .arg(sdPixel, 0, 'f', 1)
                                 .arg(variance, 0, 'f', 1));
    }
    else
    {
        QString oldText = statusLabel->text();
        int framePos = oldText.indexOf("Frame:");
        int afterFramePipe = (framePos != -1) ? oldText.indexOf('|', framePos) : -1;
        if (afterFramePipe != -1) {
            statusLabel->setText(QString("Size: %1 x %2  |  Frame: %3  ").arg(pkt.width).arg(pkt.height).arg(pkt.frameId) + oldText.mid(afterFramePipe));
        } else {
            statusLabel->setText(QString("Size: %1 x %2  |  Frame: %3").arg(pkt.width).arg(pkt.height).arg(pkt.frameId));
        }
    }

    lastFrameId = pkt.frameId;
}

void CameraPanel::setPlaying(bool v)
{
    playbackBtn->setText(v ? "Stop" : "Play");
    playbackBtn->setProperty("playing", v);
    playbackBtn->style()->unpolish(playbackBtn);
    playbackBtn->style()->polish(playbackBtn);
}

void CameraPanel::clearFrame()
{
    lastPixmap = QPixmap();
    imageLabel->clear();
    imageLabel->setText("No Signal");
    fpsLabel->setText("— fps");
    sensorTempLabel->setText("S: — °C");
    grabberTempLabel->setText("G: — °C");
    m_fpsFrameCount = 0;
    m_fpsLastMs = 0;
}

CameraParams CameraPanel::collectParams() const
{
    CameraParams p;
    p.width          = widthEdit->text().toInt();
    p.height         = heightEdit->text().toInt();
    p.fps            = fpsEdit->text().toFloat();
    p.exposure       = exposureEdit->text().toFloat();
    p.gain           = gainEdit->text().toFloat();
    p.testPattern    = testPatternCombo->currentIndex();
    p.tdiMode        = tdiModeCombo->currentIndex();
    p.tdiStages      = tdiStagesEdit->text().toInt();
    if (p.tdiStages <= 0) p.tdiStages = 4;
    p.tdiYShift      = tdiYShiftEdit->text().toInt();
    if (p.tdiYShift < 0) p.tdiYShift = 16;
    p.tdiFrameCount  = tdiFrameCountEdit ? tdiFrameCountEdit->text().toInt() : 10;
    if (p.tdiFrameCount <= 0) p.tdiFrameCount = 10;
    p.reverseX       = reverseXCheck->isChecked();
    p.reverseY       = reverseYCheck->isChecked();
    p.rawFilePath    = "";
    p.regionModeMask = 0;
    p.regionOffsets.clear();
    p.regionBinningModes.clear();
    for (int i = 0; i < 7; ++i)
    {
        if (regionModeCombos[i]->currentText() == "On")
            p.regionModeMask |= (1 << i);
        p.regionOffsets.push_back(regionOffsetEdits[i]->text().toInt());
        if (regionBinningCombos[i])
            p.regionBinningModes.push_back(regionBinningCombos[i]->currentIndex());
        else
            p.regionBinningModes.push_back(0);
    }
    return p;
}

void CameraPanel::syncBinningControlsWithRegionModes()
{
    for (int i = 0; i < 7; ++i)
    {
        if (regionBinningCombos[i] && regionModeCombos[i])
        {
            bool isOn = (regionModeCombos[i]->currentText() == "On");
            regionBinningCombos[i]->setEnabled(isOn);
        }
    }
}

void CameraPanel::disableFeature(const QString& featureName)
{
    // Do not disable basic UI controls on temporary parameter write errors or locked parameters.
    // In GenICam cameras, features may be temporarily locked while streaming or during state transitions.
    Q_UNUSED(featureName);
}
