#include "AnalysisPanelWidget.h"
#include <cmath>
#include <algorithm>
#include <QPaintEvent>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QColor>

namespace {
QPoint clampPointToImage(const QPoint& pt, const QImage& img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) {
        return QPoint(-1, -1);
    }
    return QPoint(qBound(0, pt.x(), img.width() - 1), qBound(0, pt.y(), img.height() - 1));
}
}

// ═══════════════════════════════════════════════════════════════════════════
// AnalysisGraphCanvas Implementation
// ═══════════════════════════════════════════════════════════════════════════

AnalysisGraphCanvas::AnalysisGraphCanvas(QWidget* parent)
    : QWidget(parent),
      m_histLum(256, 0),
      m_histR(256, 0),
      m_histG(256, 0),
      m_histB(256, 0)
{
    setMinimumHeight(140);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void AnalysisGraphCanvas::setMode(AnalysisMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        update();
    }
}

void AnalysisGraphCanvas::setChannel(ChannelMode channel)
{
    if (m_channel != channel) {
        m_channel = channel;
        update();
    }
}

void AnalysisGraphCanvas::setLogScale(bool logScale)
{
    if (m_logScale != logScale) {
        m_logScale = logScale;
        update();
    }
}

void AnalysisGraphCanvas::updateImageData(const QImage& image, const QRect& roi, const QPoint& crosshair)
{
    if (image.isNull()) return;

    if (m_mode == ModeFullHistogram || m_mode == ModeROIHistogram) {
        QRect targetRoi = (m_mode == ModeROIHistogram && !roi.isEmpty() && roi.isValid()) 
                            ? roi.intersected(image.rect()) 
                            : image.rect();
        computeHistogram(image, targetRoi);
    } else if (m_mode == ModeLineProfile) {
        computeLineProfiles(image, crosshair);
    }

    emit statsUpdated();
    update();
}

void AnalysisGraphCanvas::computeHistogram(const QImage& img, const QRect& targetRoi)
{
    m_histLum.fill(0);
    m_histR.fill(0);
    m_histG.fill(0);
    m_histB.fill(0);

    if (img.isNull() || targetRoi.isEmpty()) return;

    int xStart = qMax(0, targetRoi.left());
    int yStart = qMax(0, targetRoi.top());
    int xEnd = qMin(img.width(), targetRoi.right() + 1);
    int yEnd = qMin(img.height(), targetRoi.bottom() + 1);

    quint64 totalPixels = 0;
    double sumLum = 0.0;
    double sumSqLum = 0.0;

    m_minVal = 255;
    m_maxVal = 0;

    // Fast pixel inspection
    bool isGrayscale = (img.format() == QImage::Format_Grayscale8 || img.format() == QImage::Format_Indexed8);

    for (int y = yStart; y < yEnd; ++y) {
        const uchar* scanLine = img.constScanLine(y);
        for (int x = xStart; x < xEnd; ++x) {
            int r = 0, g = 0, b = 0, lum = 0;
            if (isGrayscale) {
                lum = scanLine[x];
                r = g = b = lum;
            } else if (img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32) {
                const QRgb* rgbScan = reinterpret_cast<const QRgb*>(scanLine);
                r = qRed(rgbScan[x]);
                g = qGreen(rgbScan[x]);
                b = qBlue(rgbScan[x]);
                lum = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
            } else {
                QRgb p = img.pixel(x, y);
                r = qRed(p); g = qGreen(p); b = qBlue(p);
                lum = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
            }

            lum = qBound(0, lum, 255);
            m_histLum[lum]++;
            m_histR[qBound(0, r, 255)]++;
            m_histG[qBound(0, g, 255)]++;
            m_histB[qBound(0, b, 255)]++;

            if (lum < m_minVal) m_minVal = lum;
            if (lum > m_maxVal) m_maxVal = lum;

            sumLum += lum;
            sumSqLum += (lum * lum);
            totalPixels++;
        }
    }

    if (totalPixels > 0) {
        m_meanVal = sumLum / totalPixels;
        double variance = (sumSqLum / totalPixels) - (m_meanVal * m_meanVal);
        m_stdDevVal = std::sqrt(qMax(0.0, variance));

        m_underPct = (static_cast<double>(m_histLum[0]) / totalPixels) * 100.0;
        m_overPct = (static_cast<double>(m_histLum[255]) / totalPixels) * 100.0;
    } else {
        m_minVal = 0;
        m_maxVal = 0;
        m_meanVal = 0.0;
        m_stdDevVal = 0.0;
        m_underPct = 0.0;
        m_overPct = 0.0;
    }

    m_maxBinCount = 1;
    const QVector<quint64>& currentHist = (m_channel == ChannelRed) ? m_histR :
                                          (m_channel == ChannelGreen) ? m_histG :
                                          (m_channel == ChannelBlue) ? m_histB : m_histLum;

    for (int i = 0; i < 256; ++i) {
        if (currentHist[i] > m_maxBinCount) {
            m_maxBinCount = currentHist[i];
        }
    }
}

void AnalysisGraphCanvas::computeLineProfiles(const QImage& img, const QPoint& pt)
{
    if (img.isNull()) return;

    int cx = qBound(0, pt.x(), img.width() - 1);
    int cy = qBound(0, pt.y(), img.height() - 1);
    m_crosshairPt = QPoint(cx, cy);

    int w = img.width();
    int h = img.height();

    m_rowProfile.resize(w);
    m_colProfile.resize(h);

    bool isGrayscale = (img.format() == QImage::Format_Grayscale8 || img.format() == QImage::Format_Indexed8);

    // Row Profile (Row cy across width)
    const uchar* rowLine = img.constScanLine(cy);
    for (int x = 0; x < w; ++x) {
        if (isGrayscale) {
            m_rowProfile[x] = rowLine[x];
        } else if (img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32) {
            const QRgb* rgbScan = reinterpret_cast<const QRgb*>(rowLine);
            m_rowProfile[x] = static_cast<uint8_t>(qGray(rgbScan[x]));
        } else {
            m_rowProfile[x] = static_cast<uint8_t>(qGray(img.pixel(x, cy)));
        }
    }

    // Column Profile (Col cx down height)
    for (int y = 0; y < h; ++y) {
        const uchar* colLine = img.constScanLine(y);
        if (isGrayscale) {
            m_colProfile[y] = colLine[cx];
        } else if (img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32) {
            const QRgb* rgbScan = reinterpret_cast<const QRgb*>(colLine);
            m_colProfile[y] = static_cast<uint8_t>(qGray(rgbScan[cx]));
        } else {
            m_colProfile[y] = static_cast<uint8_t>(qGray(img.pixel(cx, y)));
        }
    }
}

void AnalysisGraphCanvas::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        update();
    }
}

void AnalysisGraphCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();

    // Theme Colors according to user specification
    QColor bgCol = m_darkMode ? QColor(22, 24, 28) : QColor("#FFFFFF");
    QColor gridCol = m_darkMode ? QColor(50, 55, 65) : QColor("#E5E7EB");
    QColor borderCol = m_darkMode ? QColor(80, 88, 100) : QColor("#DDE2E7");
    QColor labelCol = m_darkMode ? QColor(160, 170, 185) : QColor("#6B7280");

    QColor cyanCol = m_darkMode ? QColor(0, 220, 220) : QColor("#06B6D4");
    QColor yellowCol = m_darkMode ? QColor(255, 190, 40) : QColor("#F59E0B");

    // Canvas Background
    p.fillRect(0, 0, w, h, bgCol);

    int leftMargin = 45;
    int rightMargin = 20;
    int topMargin = 20;
    int bottomMargin = 25;

    int plotW = w - leftMargin - rightMargin;
    int plotH = h - topMargin - bottomMargin;

    if (plotW < 50 || plotH < 30) return;

    // Grid lines
    p.setPen(QPen(gridCol, 1, Qt::DashLine));
    for (int i = 0; i <= 4; ++i) {
        int yGrid = topMargin + (plotH * i) / 4;
        p.drawLine(leftMargin, yGrid, leftMargin + plotW, yGrid);
    }
    for (int j = 0; j <= 4; ++j) {
        int xGrid = leftMargin + (plotW * j) / 4;
        p.drawLine(xGrid, topMargin, xGrid, topMargin + plotH);
    }

    // Border box
    p.setPen(QPen(borderCol, 1));
    p.drawRect(leftMargin, topMargin, plotW, plotH);

    // Axis Labels text styling
    QFont labelFont = p.font();
    labelFont.setPointSize(8);
    p.setFont(labelFont);
    p.setPen(labelCol);

    if (m_mode == ModeFullHistogram || m_mode == ModeROIHistogram) {
        // Render Histogram
        const QVector<quint64>& hist = (m_channel == ChannelRed) ? m_histR :
                                       (m_channel == ChannelGreen) ? m_histG :
                                       (m_channel == ChannelBlue) ? m_histB : m_histLum;

        QColor curveColor = (m_channel == ChannelRed) ? QColor(239, 68, 68) :
                            (m_channel == ChannelGreen) ? QColor(34, 197, 94) :
                            (m_channel == ChannelBlue) ? QColor(59, 130, 246) : cyanCol;

        QPainterPath path;
        path.moveTo(leftMargin, topMargin + plotH);

        double maxValLog = std::log10(m_maxBinCount + 1.0);

        for (int i = 0; i < 256; ++i) {
            double normHeight = 0.0;
            if (m_logScale) {
                normHeight = std::log10(hist[i] + 1.0) / (maxValLog > 0 ? maxValLog : 1.0);
            } else {
                normHeight = static_cast<double>(hist[i]) / (m_maxBinCount > 0 ? m_maxBinCount : 1.0);
            }

            int binX = leftMargin + (i * plotW) / 255;
            int binY = topMargin + plotH - static_cast<int>(normHeight * plotH);
            path.lineTo(binX, binY);
        }
        path.lineTo(leftMargin + plotW, topMargin + plotH);
        path.closeSubpath();

        // Fill under curve
        QColor fillColor = curveColor;
        fillColor.setAlpha(m_darkMode ? 60 : 40);
        p.fillPath(path, QBrush(fillColor));

        // Outline
        p.setPen(QPen(curveColor, 2));
        p.drawPath(path);

        // X-Axis Labels (0, 64, 128, 192, 255)
        for (int i = 0; i <= 4; ++i) {
            int val = i * 64;
            if (val > 255) val = 255;
            int xPos = leftMargin + (i * plotW) / 4;
            p.drawText(QRect(xPos - 20, topMargin + plotH + 3, 40, 18), Qt::AlignCenter, QString::number(val));
        }
        p.drawText(QRect(leftMargin, topMargin + plotH + 12, plotW, 15), Qt::AlignCenter, "Pixel Intensity (0-255)");

        // Y-Axis Labels
        p.drawText(QRect(0, topMargin - 8, leftMargin - 5, 16), Qt::AlignRight | Qt::AlignVCenter, m_logScale ? "Log" : QString::number(m_maxBinCount));
        p.drawText(QRect(0, topMargin + plotH - 8, leftMargin - 5, 16), Qt::AlignRight | Qt::AlignVCenter, "0");

    } else if (m_mode == ModeLineProfile) {
        // Render 2 distinct side-by-side subplots (Left for Width, Right for Height)
        int gap = 45;
        int subW = (plotW - gap) / 2;
        if (subW < 50) subW = 50;

        // ═══════════════════════════════════════════════════════════════════
        // LEFT GRAPH: WIDTH PROFILE (Horizontal Row Y)
        // ═══════════════════════════════════════════════════════════════════
        int g1Left = leftMargin;
        int g1Top = topMargin;

        // Left Subplot Grid & Border
        p.setPen(QPen(gridCol, 1, Qt::DashLine));
        for (int i = 1; i < 4; ++i) {
            int yGrid = g1Top + (plotH * i) / 4;
            p.drawLine(g1Left, yGrid, g1Left + subW, yGrid);
        }
        p.setPen(QPen(cyanCol, 1));
        p.drawRect(g1Left, g1Top, subW, plotH);

        // Left Subplot Title
        p.setPen(QPen(cyanCol, 1));
        p.drawText(QRect(g1Left + 6, g1Top + 4, subW - 10, 18), Qt::AlignLeft, 
                   QString("↔ WIDTH PROFILE (Row Y = %1)").arg(m_crosshairPt.y()));

        // Y-axis labels for Left Subplot
        p.setPen(labelCol);
        p.drawText(QRect(0, g1Top - 2, g1Left - 5, 14), Qt::AlignRight, "255");
        p.drawText(QRect(0, g1Top + plotH - 10, g1Left - 5, 14), Qt::AlignRight, "0");

        // Left Subplot Curve (Cyan)
        if (!m_rowProfile.isEmpty()) {
            QPainterPath rowPath;
            int rw = m_rowProfile.size();
            for (int x = 0; x < rw; ++x) {
                double normVal = m_rowProfile[x] / 255.0;
                int px = g1Left + (x * subW) / (rw > 1 ? rw - 1 : 1);
                int py = g1Top + plotH - static_cast<int>(normVal * plotH);
                if (x == 0) rowPath.moveTo(px, py);
                else rowPath.lineTo(px, py);
            }
            p.setPen(QPen(cyanCol, 2));
            p.drawPath(rowPath);

            // X-axis label (Width)
            p.setPen(labelCol);
            p.drawText(QRect(g1Left, g1Top + plotH + 4, subW, 16), Qt::AlignCenter, 
                       QString("Width X (0 → %1 px)").arg(rw));
        }

        // ═══════════════════════════════════════════════════════════════════
        // RIGHT GRAPH: HEIGHT PROFILE (Vertical Column X)
        // ═══════════════════════════════════════════════════════════════════
        int g2Left = g1Left + subW + gap;
        int g2Top = topMargin;

        // Right Subplot Grid & Border
        p.setPen(QPen(gridCol, 1, Qt::DashLine));
        for (int i = 1; i < 4; ++i) {
            int yGrid = g2Top + (plotH * i) / 4;
            p.drawLine(g2Left, yGrid, g2Left + subW, yGrid);
        }
        p.setPen(QPen(yellowCol, 1));
        p.drawRect(g2Left, g2Top, subW, plotH);

        // Right Subplot Title
        p.setPen(QPen(yellowCol, 1));
        p.drawText(QRect(g2Left + 6, g2Top + 4, subW - 10, 18), Qt::AlignLeft, 
                   QString("↕ HEIGHT PROFILE (Col X = %1)").arg(m_crosshairPt.x()));

        // Y-axis labels for Right Subplot
        p.setPen(labelCol);
        p.drawText(QRect(g2Left - gap + 2, g2Top - 2, gap - 6, 14), Qt::AlignRight, "255");
        p.drawText(QRect(g2Left - gap + 2, g2Top + plotH - 10, gap - 6, 14), Qt::AlignRight, "0");

        // Right Subplot Curve (Yellow)
        if (!m_colProfile.isEmpty()) {
            QPainterPath colPath;
            int ch = m_colProfile.size();
            for (int y = 0; y < ch; ++y) {
                double normVal = m_colProfile[y] / 255.0;
                int px = g2Left + (y * subW) / (ch > 1 ? ch - 1 : 1);
                int py = g2Top + plotH - static_cast<int>(normVal * plotH);
                if (y == 0) colPath.moveTo(px, py);
                else colPath.lineTo(px, py);
            }
            p.setPen(QPen(yellowCol, 2));
            p.drawPath(colPath);

            // X-axis label (Height)
            p.setPen(labelCol);
            p.drawText(QRect(g2Left, g2Top + plotH + 4, subW, 16), Qt::AlignCenter, 
                       QString("Height Y (0 → %1 px)").arg(ch));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// AnalysisPanelWidget Implementation
// ═══════════════════════════════════════════════════════════════════════════

AnalysisPanelWidget::AnalysisPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("AnalysisPanel");
    setStyleSheet(
        "#AnalysisPanel { background-color: #1a1c20; border-top: 2px solid #2d3139; }"
        "QComboBox { background-color: #262930; color: #e0e0e0; border: 1px solid #3c424e; border-radius: 3px; padding: 3px 6px; font-weight: bold; }"
        "QComboBox::drop-down { border: none; }"
        "QPushButton { background-color: #2a2e37; color: #d0d5e0; border: 1px solid #3d4350; border-radius: 3px; padding: 4px 10px; font-weight: bold; }"
        "QPushButton:hover { background-color: #353b47; color: #ffffff; }"
        "QPushButton:checked { background-color: #007acc; color: #ffffff; border-color: #0098ff; }"
        "QLabel { color: #b0b8c5; font-size: 11px; }"
    );

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(4);

    // Top Header Control Bar
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel("📊 IMAGE ANALYSIS & PROFILER", this);
    m_titleLabel->setStyleSheet("font-weight: bold; color: #00d2d2; font-size: 12px;");
    headerLayout->addWidget(m_titleLabel);

    headerLayout->addSpacing(15);

    headerLayout->addWidget(new QLabel("Mode:", this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem("📊 Full Image Histogram", AnalysisGraphCanvas::ModeFullHistogram);
    m_modeCombo->addItem("🔲 Sub-Region (ROI)", AnalysisGraphCanvas::ModeROIHistogram);
    m_modeCombo->addItem("➕ Dual Line Profile", AnalysisGraphCanvas::ModeLineProfile);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisPanelWidget::onModeChanged);
    headerLayout->addWidget(m_modeCombo);

    headerLayout->addSpacing(10);

    headerLayout->addWidget(new QLabel("Channel:", this));
    m_channelCombo = new QComboBox(this);
    m_channelCombo->addItem("Luminance (Gray)", AnalysisGraphCanvas::ChannelLuminance);
    m_channelCombo->addItem("Red Channel",      AnalysisGraphCanvas::ChannelRed);
    m_channelCombo->addItem("Green Channel",    AnalysisGraphCanvas::ChannelGreen);
    m_channelCombo->addItem("Blue Channel",     AnalysisGraphCanvas::ChannelBlue);
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisPanelWidget::onChannelChanged);
    headerLayout->addWidget(m_channelCombo);

    headerLayout->addSpacing(10);

    m_logScaleBtn = new QPushButton("Log Scale", this);
    m_logScaleBtn->setCheckable(true);
    connect(m_logScaleBtn, &QPushButton::toggled, this, &AnalysisPanelWidget::onLogScaleToggled);
    headerLayout->addWidget(m_logScaleBtn);

    headerLayout->addSpacing(15);

    // SpinBoxes for direct Row Y and Col X selection
    m_rowLabel = new QLabel("Row (Y):", this);
    m_rowSpin = new QSpinBox(this);
    m_rowSpin->setRange(0, 3839);
    m_rowSpin->setValue(1920);
    m_rowSpin->setStyleSheet("QSpinBox { background-color: #262930; color: #00e0e0; font-weight: bold; border: 1px solid #00a0a0; padding: 2px 4px; border-radius: 3px; }");
    connect(m_rowSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AnalysisPanelWidget::onRowSpinChanged);

    m_colLabel = new QLabel("Col (X):", this);
    m_colSpin = new QSpinBox(this);
    m_colSpin->setRange(0, 18687);
    m_colSpin->setValue(4672);   // start at mid-point of a single 9344-wide cam
    m_colSpin->setStyleSheet("QSpinBox { background-color: #262930; color: #ffe040; font-weight: bold; border: 1px solid #d0a000; padding: 2px 4px; border-radius: 3px; }");
    connect(m_colSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AnalysisPanelWidget::onColSpinChanged);

    // Initialise crosshair to match default spin values
    m_cameraCrosshair[0] = QPoint(m_colSpin->value(), m_rowSpin->value());
    m_cameraCrosshair[1] = m_cameraCrosshair[0];
    m_currentCrosshair = m_cameraCrosshair[0];

    // Hide row/col spinboxes by default until Line Profile mode is selected
    m_rowLabel->hide();
    m_rowSpin->hide();
    m_colLabel->hide();
    m_colSpin->hide();

    headerLayout->addWidget(m_rowLabel);
    headerLayout->addWidget(m_rowSpin);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(m_colLabel);
    headerLayout->addWidget(m_colSpin);

    headerLayout->addStretch(1);

    m_statsLabel = new QLabel("Min: 0 | Max: 0 | Mean: 0.0 | StdDev: 0.0 | Clip: 0.0%", this);
    m_statsLabel->setStyleSheet("font-family: monospace; font-size: 11px; color: #a0f0a0; background-color: #121417; padding: 3px 8px; border-radius: 3px;");
    headerLayout->addWidget(m_statsLabel);

    headerLayout->addSpacing(10);

    m_closeBtn = new QPushButton("✖", this);
    m_closeBtn->setFixedWidth(28);
    m_closeBtn->setToolTip("Close Analysis Panel");
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit closeRequested(); });
    headerLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(headerLayout);

    // ── Dual-row widget: holds both canvases side by side in dual-camera mode ──
    m_dualRow = new QWidget(this);
    QHBoxLayout* dualRowLayout = new QHBoxLayout(m_dualRow);
    dualRowLayout->setContentsMargins(0, 0, 0, 0);
    dualRowLayout->setSpacing(4);

    // CAM0 column
    QWidget* cam0Col = new QWidget(m_dualRow);
    QVBoxLayout* cam0Layout = new QVBoxLayout(cam0Col);
    cam0Layout->setContentsMargins(0, 0, 0, 0);
    cam0Layout->setSpacing(2);
    QWidget* cam0Header = new QWidget(cam0Col);
    QHBoxLayout* cam0HeaderLayout = new QHBoxLayout(cam0Header);
    cam0HeaderLayout->setContentsMargins(0, 0, 0, 0);
    cam0HeaderLayout->setSpacing(6);
    m_cam0Label = new QLabel("── CAM 0 ──", cam0Header);
    m_cam0Label->setAlignment(Qt::AlignCenter);
    m_cam0Label->setStyleSheet("font-weight: bold; color: #00d2d2; font-size: 11px;");
    cam0HeaderLayout->addWidget(m_cam0Label);
    m_cameraRowLabels[0] = new QLabel("Y:", cam0Header);
    m_cameraRowSpin[0] = new QSpinBox(cam0Header);
    m_cameraRowSpin[0]->setRange(0, 3839);
    m_cameraRowSpin[0]->setValue(m_cameraCrosshair[0].y());
    m_cameraRowSpin[0]->setStyleSheet("QSpinBox { background-color: #262930; color: #00e0e0; font-weight: bold; border: 1px solid #00a0a0; padding: 2px 4px; border-radius: 3px; }");
    connect(m_cameraRowSpin[0], QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ onCameraRowSpinChanged(0, v); });
    m_cameraColLabels[0] = new QLabel("X:", cam0Header);
    m_cameraColSpin[0] = new QSpinBox(cam0Header);
    m_cameraColSpin[0]->setRange(0, 9343);
    m_cameraColSpin[0]->setValue(m_cameraCrosshair[0].x());
    m_cameraColSpin[0]->setStyleSheet("QSpinBox { background-color: #262930; color: #ffe040; font-weight: bold; border: 1px solid #d0a000; padding: 2px 4px; border-radius: 3px; }");
    connect(m_cameraColSpin[0], QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ onCameraColSpinChanged(0, v); });
    m_cameraStatsLabels[0] = new QLabel("Min: 0 | Max: 0 | Mean: 0.0 | StdDev: 0.0", cam0Header);
    m_cameraStatsLabels[0]->setStyleSheet("font-family: monospace; font-size: 10px; color: #1F2937; background-color: #F3F4F6; border: 1px solid #DDE2E7; padding: 2px 6px; border-radius: 3px; font-weight: bold;");
    cam0HeaderLayout->addWidget(m_cameraRowLabels[0]);
    cam0HeaderLayout->addWidget(m_cameraRowSpin[0]);
    cam0HeaderLayout->addWidget(m_cameraColLabels[0]);
    cam0HeaderLayout->addWidget(m_cameraColSpin[0]);
    cam0HeaderLayout->addStretch(1);
    cam0HeaderLayout->addWidget(m_cameraStatsLabels[0]);
    m_canvas = new AnalysisGraphCanvas(cam0Col);
    connect(m_canvas, &AnalysisGraphCanvas::statsUpdated, this, &AnalysisPanelWidget::updateStatsUI);
    cam0Layout->addWidget(cam0Header);
    cam0Layout->addWidget(m_canvas, 1);
    dualRowLayout->addWidget(cam0Col, 1);

    // Vertical divider
    QFrame* vDiv = new QFrame(m_dualRow);
    vDiv->setFrameShape(QFrame::VLine);
    vDiv->setStyleSheet("color: #3c424e;");
    dualRowLayout->addWidget(vDiv);

    // CAM1 column
    QWidget* cam1Col = new QWidget(m_dualRow);
    QVBoxLayout* cam1Layout = new QVBoxLayout(cam1Col);
    cam1Layout->setContentsMargins(0, 0, 0, 0);
    cam1Layout->setSpacing(2);
    QWidget* cam1Header = new QWidget(cam1Col);
    QHBoxLayout* cam1HeaderLayout = new QHBoxLayout(cam1Header);
    cam1HeaderLayout->setContentsMargins(0, 0, 0, 0);
    cam1HeaderLayout->setSpacing(6);
    m_cam1Label = new QLabel("── CAM 1 ──", cam1Header);
    m_cam1Label->setAlignment(Qt::AlignCenter);
    m_cam1Label->setStyleSheet("font-weight: bold; color: #ffa040; font-size: 11px;");
    cam1HeaderLayout->addWidget(m_cam1Label);
    m_cameraRowLabels[1] = new QLabel("Y:", cam1Header);
    m_cameraRowSpin[1] = new QSpinBox(cam1Header);
    m_cameraRowSpin[1]->setRange(0, 3839);
    m_cameraRowSpin[1]->setValue(m_cameraCrosshair[1].y());
    m_cameraRowSpin[1]->setStyleSheet("QSpinBox { background-color: #262930; color: #00e0e0; font-weight: bold; border: 1px solid #00a0a0; padding: 2px 4px; border-radius: 3px; }");
    connect(m_cameraRowSpin[1], QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ onCameraRowSpinChanged(1, v); });
    m_cameraColLabels[1] = new QLabel("X:", cam1Header);
    m_cameraColSpin[1] = new QSpinBox(cam1Header);
    m_cameraColSpin[1]->setRange(0, 9343);
    m_cameraColSpin[1]->setValue(m_cameraCrosshair[1].x());
    m_cameraColSpin[1]->setStyleSheet("QSpinBox { background-color: #262930; color: #ffe040; font-weight: bold; border: 1px solid #d0a000; padding: 2px 4px; border-radius: 3px; }");
    connect(m_cameraColSpin[1], QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ onCameraColSpinChanged(1, v); });
    m_cameraStatsLabels[1] = new QLabel("Min: 0 | Max: 0 | Mean: 0.0 | StdDev: 0.0", cam1Header);
    m_cameraStatsLabels[1]->setStyleSheet("font-family: monospace; font-size: 10px; color: #1F2937; background-color: #F3F4F6; border: 1px solid #DDE2E7; padding: 2px 6px; border-radius: 3px; font-weight: bold;");
    cam1HeaderLayout->addWidget(m_cameraRowLabels[1]);
    cam1HeaderLayout->addWidget(m_cameraRowSpin[1]);
    cam1HeaderLayout->addWidget(m_cameraColLabels[1]);
    cam1HeaderLayout->addWidget(m_cameraColSpin[1]);
    cam1HeaderLayout->addStretch(1);
    cam1HeaderLayout->addWidget(m_cameraStatsLabels[1]);
    m_canvas2 = new AnalysisGraphCanvas(cam1Col);
    connect(m_canvas2, &AnalysisGraphCanvas::statsUpdated, this, &AnalysisPanelWidget::updateStatsUI);
    cam1Layout->addWidget(cam1Header);
    cam1Layout->addWidget(m_canvas2, 1);
    dualRowLayout->addWidget(cam1Col, 1);

    // In single-camera mode, hide the labels and cam1 column by default
    m_cam0Label->hide();
    m_cam1Label->hide();
    for (int cam = 0; cam < 2; ++cam) {
        if (m_cameraRowLabels[cam]) m_cameraRowLabels[cam]->hide();
        if (m_cameraRowSpin[cam]) m_cameraRowSpin[cam]->hide();
        if (m_cameraColLabels[cam]) m_cameraColLabels[cam]->hide();
        if (m_cameraColSpin[cam]) m_cameraColSpin[cam]->hide();
        if (m_cameraStatsLabels[cam]) m_cameraStatsLabels[cam]->hide();
    }
    cam1Col->hide();   // hide CAM1 column initially
    // Store cam1Col pointer for toggling via setDualCameraMode
    m_canvas2->setProperty("cam1Col", QVariant::fromValue<QObject*>(cam1Col));

    mainLayout->addWidget(m_dualRow, 1);
}


void AnalysisPanelWidget::onModeChanged(int index)
{
    AnalysisGraphCanvas::AnalysisMode m = static_cast<AnalysisGraphCanvas::AnalysisMode>(m_modeCombo->itemData(index).toInt());
    m_canvas->setMode(m);
    if (m_canvas2) m_canvas2->setMode(m);
    m_channelCombo->setEnabled(m != AnalysisGraphCanvas::ModeLineProfile);
    m_logScaleBtn->setEnabled(m != AnalysisGraphCanvas::ModeLineProfile);

    bool isProfile = (m == AnalysisGraphCanvas::ModeLineProfile);
    const bool showSharedProfileControls = isProfile && !m_dualCamera;
    if (m_statsLabel) m_statsLabel->setVisible(!m_dualCamera);
    if (m_rowLabel) m_rowLabel->setVisible(showSharedProfileControls);
    if (m_rowSpin)  m_rowSpin->setVisible(showSharedProfileControls);
    if (m_colLabel) m_colLabel->setVisible(showSharedProfileControls);
    if (m_colSpin)  m_colSpin->setVisible(showSharedProfileControls);
    for (int cam = 0; cam < 2; ++cam) {
        const bool visible = m_dualCamera && isProfile;
        if (m_cameraRowLabels[cam]) m_cameraRowLabels[cam]->setVisible(visible);
        if (m_cameraRowSpin[cam]) m_cameraRowSpin[cam]->setVisible(visible);
        if (m_cameraColLabels[cam]) m_cameraColLabels[cam]->setVisible(visible);
        if (m_cameraColSpin[cam]) m_cameraColSpin[cam]->setVisible(visible);
        if (m_cameraStatsLabels[cam]) m_cameraStatsLabels[cam]->setVisible(m_dualCamera);
    }

    if (!m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
}

void AnalysisPanelWidget::onChannelChanged(int index)
{
    AnalysisGraphCanvas::ChannelMode ch = static_cast<AnalysisGraphCanvas::ChannelMode>(m_channelCombo->itemData(index).toInt());
    m_canvas->setChannel(ch);
    if (m_canvas2) m_canvas2->setChannel(ch);
    if (!m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
}

void AnalysisPanelWidget::onLogScaleToggled(bool checked)
{
    m_canvas->setLogScale(checked);
    if (m_canvas2) m_canvas2->setLogScale(checked);
}

void AnalysisPanelWidget::onRowSpinChanged(int val)
{
    const int cam = qBound(0, m_activeCamera, 1);
    m_cameraCrosshair[cam].setY(val);
    m_currentCrosshair = m_cameraCrosshair[cam];
    if (cam == 0 && !m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (cam == 1 && m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
    updateStatsUI();
}

void AnalysisPanelWidget::onCameraRowSpinChanged(int cameraIndex, int val)
{
    const int cam = qBound(0, cameraIndex, 1);
    m_activeCamera = cam;
    m_cameraCrosshair[cam].setY(val);
    m_currentCrosshair = m_cameraCrosshair[cam];
    if (cam == 0 && !m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (cam == 1 && m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
    syncSpinBoxesToActiveCamera();
    updateStatsUI();
}

void AnalysisPanelWidget::onCameraColSpinChanged(int cameraIndex, int val)
{
    const int cam = qBound(0, cameraIndex, 1);
    m_activeCamera = cam;
    m_cameraCrosshair[cam].setX(val);
    m_currentCrosshair = m_cameraCrosshair[cam];
    if (cam == 0 && !m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (cam == 1 && m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
    syncSpinBoxesToActiveCamera();
    updateStatsUI();
}

void AnalysisPanelWidget::onColSpinChanged(int val)
{
    const int cam = qBound(0, m_activeCamera, 1);
    m_cameraCrosshair[cam].setX(val);
    m_currentCrosshair = m_cameraCrosshair[cam];
    if (cam == 0 && !m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (cam == 1 && m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
    updateStatsUI();
}

AnalysisGraphCanvas::AnalysisMode AnalysisPanelWidget::getMode() const
{
    return m_canvas ? m_canvas->getMode() : AnalysisGraphCanvas::ModeFullHistogram;
}

void AnalysisPanelWidget::setDarkMode(bool dark)
{
    m_darkMode = dark;
    if (dark) {
        setStyleSheet(
            "#AnalysisPanel { background-color: #14161a; border-top: 2px solid #2d3139; }"
            "QComboBox { background-color: #262930; color: #e0e0e0; border: 1px solid #3c424e; border-radius: 3px; padding: 3px 6px; font-weight: bold; }"
            "QPushButton { background-color: #2a2e37; color: #d0d5e0; border: 1px solid #3d4350; border-radius: 3px; padding: 4px 10px; font-weight: bold; }"
            "QPushButton:hover { background-color: #353b47; color: #ffffff; }"
            "QPushButton:checked { background-color: #007acc; color: #ffffff; border-color: #0098ff; }"
            "QLabel { color: #b0b8c5; font-size: 11px; }"
        );
        if (m_titleLabel) m_titleLabel->setStyleSheet("font-weight: bold; color: #00d2d2; font-size: 12px;");
        if (m_cam0Label)  m_cam0Label->setStyleSheet("font-weight: bold; color: #00d2d2; font-size: 11px;");
        if (m_cam1Label)  m_cam1Label->setStyleSheet("font-weight: bold; color: #ffa040; font-size: 11px;");
        if (m_statsLabel) m_statsLabel->setStyleSheet("font-family: monospace; font-size: 11px; color: #a0f0a0; background-color: #121417; padding: 3px 8px; border-radius: 3px;");
        for (int cam = 0; cam < 2; ++cam) {
            if (m_cameraStatsLabels[cam]) m_cameraStatsLabels[cam]->setStyleSheet("font-family: monospace; font-size: 10px; color: #a0f0a0; background-color: #121417; border: 1px solid #3c424e; padding: 2px 6px; border-radius: 3px;");
            if (m_cameraRowSpin[cam]) m_cameraRowSpin[cam]->setStyleSheet("QSpinBox { background-color: #262930; color: #00e0e0; font-weight: bold; border: 1px solid #00a0a0; padding: 2px 4px; border-radius: 3px; }");
            if (m_cameraColSpin[cam]) m_cameraColSpin[cam]->setStyleSheet("QSpinBox { background-color: #262930; color: #ffe040; font-weight: bold; border: 1px solid #d0a000; padding: 2px 4px; border-radius: 3px; }");
        }
        if (m_rowSpin) m_rowSpin->setStyleSheet("QSpinBox { background-color: #262930; color: #00e0e0; font-weight: bold; border: 1px solid #00a0a0; padding: 2px 4px; border-radius: 3px; }");
        if (m_colSpin) m_colSpin->setStyleSheet("QSpinBox { background-color: #262930; color: #ffe040; font-weight: bold; border: 1px solid #d0a000; padding: 2px 4px; border-radius: 3px; }");
    } else {
        setStyleSheet(
            "#AnalysisPanel { background-color: #FFFFFF; border-top: 2px solid #DDE2E7; }"
            "QComboBox { background-color: #FFFFFF; color: #1F2937; border: 1px solid #D7DEE5; border-radius: 4px; padding: 4px 6px; font-weight: 600; }"
            "QComboBox:focus { border: 2px solid #3B82F6; }"
            "QPushButton { background-color: #F3F4F6; color: #374151; border: 1px solid #DDE2E7; border-radius: 4px; padding: 4px 10px; font-weight: 600; }"
            "QPushButton:hover { background-color: #E5E7EB; color: #1F2937; }"
            "QPushButton:checked { background-color: #3B82F6; color: #FFFFFF; border-color: #2563EB; }"
            "QLabel { color: #1F2937; font-size: 11px; font-weight: 600; }"
        );
        if (m_titleLabel) m_titleLabel->setStyleSheet("font-weight: bold; color: #06B6D4; font-size: 12px;");
        if (m_cam0Label)  m_cam0Label->setStyleSheet("font-weight: bold; color: #06B6D4; font-size: 11px;");
        if (m_cam1Label)  m_cam1Label->setStyleSheet("font-weight: bold; color: #D97706; font-size: 11px;");
        if (m_statsLabel) m_statsLabel->setStyleSheet("font-family: monospace; font-size: 11px; color: #1F2937; background-color: #F3F4F6; border: 1px solid #DDE2E7; padding: 3px 8px; border-radius: 4px; font-weight: bold;");
        for (int cam = 0; cam < 2; ++cam) {
            if (m_cameraStatsLabels[cam]) m_cameraStatsLabels[cam]->setStyleSheet("font-family: monospace; font-size: 10px; color: #1F2937; background-color: #F3F4F6; border: 1px solid #DDE2E7; padding: 2px 6px; border-radius: 3px; font-weight: bold;");
            if (m_cameraRowSpin[cam]) m_cameraRowSpin[cam]->setStyleSheet("QSpinBox { background-color: #FFFFFF; color: #06B6D4; font-weight: bold; border: 1px solid #06B6D4; padding: 2px 4px; border-radius: 4px; }");
            if (m_cameraColSpin[cam]) m_cameraColSpin[cam]->setStyleSheet("QSpinBox { background-color: #FFFFFF; color: #F59E0B; font-weight: bold; border: 1px solid #F59E0B; padding: 2px 4px; border-radius: 4px; }");
        }
        if (m_rowSpin) m_rowSpin->setStyleSheet("QSpinBox { background-color: #FFFFFF; color: #06B6D4; font-weight: bold; border: 1px solid #06B6D4; padding: 2px 4px; border-radius: 4px; }");
        if (m_colSpin) m_colSpin->setStyleSheet("QSpinBox { background-color: #FFFFFF; color: #F59E0B; font-weight: bold; border: 1px solid #F59E0B; padding: 2px 4px; border-radius: 4px; }");
    }

    if (m_canvas)  m_canvas->setDarkMode(dark);
    if (m_canvas2) m_canvas2->setDarkMode(dark);
}


void AnalysisPanelWidget::processImageFrame(const QImage& frame, const QRect& roi, const QPoint& crosshair)
{
    m_currentFrame = frame;
    m_frameSize[0] = frame.size();

    // Preserve existing ROI if default / empty ROI was passed during video playback
    if (!roi.isEmpty() && roi.isValid()) {
        m_currentROI = roi;
    }

    // Only override crosshair if caller explicitly provides one (sentinel = QPoint(-1,-1))
    if (crosshair.x() >= 0 && crosshair.y() >= 0) {
        m_cameraCrosshair[0] = crosshair;
    }

    if (!frame.isNull()) {
        m_cameraCrosshair[0] = clampPointToImage(m_cameraCrosshair[0], frame);
    }

    if (m_activeCamera == 0) {
        m_currentCrosshair = m_cameraCrosshair[0];
        syncSpinBoxesToActiveCamera();
    }
    syncCameraControls(0);
    m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
}

void AnalysisPanelWidget::processSecondCameraFrame(const QImage& frame)
{
    if (!m_canvas2 || frame.isNull()) return;
    m_currentFrame2 = frame;
    m_frameSize[1] = frame.size();
    if (m_cameraCrosshair[1].x() < 0 || m_cameraCrosshair[1].y() < 0) {
        m_cameraCrosshair[1] = QPoint(frame.width() / 2, frame.height() / 2);
    }
    m_cameraCrosshair[1] = clampPointToImage(m_cameraCrosshair[1], frame);
    if (m_activeCamera == 1) {
        m_currentCrosshair = m_cameraCrosshair[1];
        syncSpinBoxesToActiveCamera();
    }
    syncCameraControls(1);
    m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
}

void AnalysisPanelWidget::setDualCameraMode(bool dual)
{
    if (m_dualCamera == dual) return;
    m_dualCamera = dual;
    if (!m_dualCamera) {
        m_activeCamera = 0;
        m_currentCrosshair = m_cameraCrosshair[0];
        syncSpinBoxesToActiveCamera();
    }

    // Find the cam1Col widget we stored in the canvas2 property
    QObject* cam1ColObj = m_canvas2 ? m_canvas2->property("cam1Col").value<QObject*>() : nullptr;
    QWidget* cam1Col = qobject_cast<QWidget*>(cam1ColObj);

    if (dual) {
        if (m_cam0Label) m_cam0Label->show();
        if (m_cam1Label) m_cam1Label->show();
        if (cam1Col)     cam1Col->show();
    } else {
        if (m_cam0Label) m_cam0Label->hide();
        if (m_cam1Label) m_cam1Label->hide();
        if (cam1Col)     cam1Col->hide();
        m_currentFrame2 = QImage();
    }

    const bool isProfile = getMode() == AnalysisGraphCanvas::ModeLineProfile;
    if (m_statsLabel) m_statsLabel->setVisible(!dual);
    if (m_rowLabel) m_rowLabel->setVisible(!dual && isProfile);
    if (m_rowSpin) m_rowSpin->setVisible(!dual && isProfile);
    if (m_colLabel) m_colLabel->setVisible(!dual && isProfile);
    if (m_colSpin) m_colSpin->setVisible(!dual && isProfile);
    for (int cam = 0; cam < 2; ++cam) {
        if (m_cameraRowLabels[cam]) m_cameraRowLabels[cam]->setVisible(dual && isProfile);
        if (m_cameraRowSpin[cam]) m_cameraRowSpin[cam]->setVisible(dual && isProfile);
        if (m_cameraColLabels[cam]) m_cameraColLabels[cam]->setVisible(dual && isProfile);
        if (m_cameraColSpin[cam]) m_cameraColSpin[cam]->setVisible(dual && isProfile);
        if (m_cameraStatsLabels[cam]) m_cameraStatsLabels[cam]->setVisible(dual);
        syncCameraControls(cam);
    }
    updateStatsUI();
}


void AnalysisPanelWidget::setROI(const QRect& roi)
{
    m_currentROI = roi;
    if (!m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
}

QPoint AnalysisPanelWidget::getCrosshair() const
{
    return m_cameraCrosshair[qBound(0, m_activeCamera, 1)];
}

QPoint AnalysisPanelWidget::getCrosshairForCamera(int cameraIndex) const
{
    return m_cameraCrosshair[qBound(0, cameraIndex, 1)];
}

void AnalysisPanelWidget::syncSpinBoxesToActiveCamera()
{
    const int cam = qBound(0, m_activeCamera, 1);
    const QSize size = m_frameSize[cam].isValid() ? m_frameSize[cam] : QSize(9344, 3840);
    if (m_rowSpin) {
        QSignalBlocker b(m_rowSpin);
        m_rowSpin->setMaximum(qMax(0, size.height() - 1));
        m_rowSpin->setValue(m_cameraCrosshair[cam].y());
    }
    if (m_colSpin) {
        QSignalBlocker b(m_colSpin);
        m_colSpin->setMaximum(qMax(0, size.width() - 1));
        m_colSpin->setValue(m_cameraCrosshair[cam].x());
    }
}

void AnalysisPanelWidget::syncCameraControls(int cameraIndex)
{
    const int cam = qBound(0, cameraIndex, 1);
    const QSize size = m_frameSize[cam].isValid() ? m_frameSize[cam] : QSize(9344, 3840);
    if (m_cameraCrosshair[cam].x() < 0 || m_cameraCrosshair[cam].y() < 0) {
        m_cameraCrosshair[cam] = QPoint(size.width() / 2, size.height() / 2);
    }
    if (m_cameraRowSpin[cam]) {
        QSignalBlocker b(m_cameraRowSpin[cam]);
        m_cameraRowSpin[cam]->setMaximum(qMax(0, size.height() - 1));
        m_cameraRowSpin[cam]->setValue(qBound(0, m_cameraCrosshair[cam].y(), qMax(0, size.height() - 1)));
    }
    if (m_cameraColSpin[cam]) {
        QSignalBlocker b(m_cameraColSpin[cam]);
        m_cameraColSpin[cam]->setMaximum(qMax(0, size.width() - 1));
        m_cameraColSpin[cam]->setValue(qBound(0, m_cameraCrosshair[cam].x(), qMax(0, size.width() - 1)));
    }
}

void AnalysisPanelWidget::setCrosshair(const QPoint& pt, int cameraIndex)
{
    const int cam = qBound(0, cameraIndex < 0 ? m_activeCamera : cameraIndex, 1);
    m_activeCamera = cam;
    m_cameraCrosshair[cam] = pt;
    m_currentCrosshair = pt;
    syncSpinBoxesToActiveCamera();
    syncCameraControls(cam);

    if (cam == 0 && !m_currentFrame.isNull()) {
        m_canvas->updateImageData(m_currentFrame, m_currentROI, m_cameraCrosshair[0]);
    }
    if (cam == 1 && m_canvas2 && !m_currentFrame2.isNull()) {
        m_canvas2->updateImageData(m_currentFrame2, QRect(), m_cameraCrosshair[1]);
    }
    updateStatsUI();
}

QString AnalysisPanelWidget::statsTextForCanvas(AnalysisGraphCanvas* canvas) const
{
    if (!canvas) return "Min: 0 | Max: 0 | Mean: 0.0 | StdDev: 0.0";
    return QString("Min: %1 | Max: %2 | Mean: %3 | StdDev: %4 | Under: %5% | Over: %6%")
        .arg(canvas->getMinVal())
        .arg(canvas->getMaxVal())
        .arg(canvas->getMeanVal(), 0, 'f', 1)
        .arg(canvas->getStdDevVal(), 0, 'f', 1)
        .arg(canvas->getUnderExposurePct(), 0, 'f', 1)
        .arg(canvas->getOverExposurePct(), 0, 'f', 1);
}

void AnalysisPanelWidget::updateStatsUI()
{
    const bool isProfile = m_canvas->getMode() == AnalysisGraphCanvas::ModeLineProfile;
    if (m_dualCamera) {
        if (m_statsLabel) m_statsLabel->hide();
        if (m_cameraStatsLabels[0]) {
            m_cameraStatsLabels[0]->setText(isProfile
                ? QString("Y: %1 | X: %2").arg(m_cameraCrosshair[0].y()).arg(m_cameraCrosshair[0].x())
                : statsTextForCanvas(m_canvas));
            m_cameraStatsLabels[0]->show();
        }
        if (m_cameraStatsLabels[1]) {
            m_cameraStatsLabels[1]->setText(isProfile
                ? QString("Y: %1 | X: %2").arg(m_cameraCrosshair[1].y()).arg(m_cameraCrosshair[1].x())
                : statsTextForCanvas(m_canvas2));
            m_cameraStatsLabels[1]->show();
        }
        return;
    }

    if (m_statsLabel) m_statsLabel->show();
    if (isProfile) {
        if (m_dualCamera) {
            m_statsLabel->setText(QString("C0 Y: %1 | C0 X: %2    |    C1 Y: %3 | C1 X: %4")
                .arg(m_cameraCrosshair[0].y())
                .arg(m_cameraCrosshair[0].x())
                .arg(m_cameraCrosshair[1].y())
                .arg(m_cameraCrosshair[1].x()));
        } else {
            const QPoint pt = getCrosshair();
            m_statsLabel->setText(QString("Selected Row Y: %1 | Selected Col X: %2")
                .arg(pt.y())
                .arg(pt.x()));
        }
    } else {
        m_statsLabel->setText(QString("Min: %1 | Max: %2 | Mean: %3 | StdDev: %4 | Under: %5% | Over: %6%")
            .arg(m_canvas->getMinVal())
            .arg(m_canvas->getMaxVal())
            .arg(m_canvas->getMeanVal(), 0, 'f', 1)
            .arg(m_canvas->getStdDevVal(), 0, 'f', 1)
            .arg(m_canvas->getUnderExposurePct(), 0, 'f', 1)
            .arg(m_canvas->getOverExposurePct(), 0, 'f', 1));
    }
}
