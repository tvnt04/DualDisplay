#include "ThemeManager.h"
#include <QApplication>
#include <QWidget>
#include <QStyle>

QString ThemeManager::getLightStylesheet()
{
    return R"(
        /* ═══════════════════════════════════════════════════════ */
        /* PROFESSIONAL MODERN THEME - LIGHT UI                    */
        /* ═══════════════════════════════════════════════════════ */
        
        QMainWindow, QWidget {
            background-color: #F4F6F8;
            color: #1F2937;
            font-family: 'Segoe UI','Ubuntu','Liberation Sans',sans-serif;
            font-size: 11px;
        }
        
        /* ─────── Dock Widget ──────────────────────────────────────── */
        QDockWidget {
            color: #1F2937;
            font-weight: 600;
            font-size: 11px;
            border: 1px solid #DDE2E7;
            border-radius: 4px;
        }
        
        QDockWidget::title {
            background: #ECEFF3;
            padding: 8px 12px;
            border-bottom: 2px solid #3B82F6;
            text-align: left;
            color: #1F2937;
            font-weight: 700;
        }
        
        QDockWidget::close-button, QDockWidget::float-button {
            background: #F3F4F6;
            border: 1px solid #DDE2E7;
            border-radius: 3px;
            padding: 3px;
            subcontrol-position: top right;
            margin-right: 2px;
        }
        
        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
            background: #3B82F6;
            border-color: #2563EB;
            color: white;
        }
        
        /* ─────── Tab Widget ───────────────────────────────────────── */
        QTabWidget::pane {
            border: 1px solid #DDE2E7;
            background: #F4F6F8;
        }

        QTabBar::tab {
            background: #ECEFF3;
            color: #1F2937;
            padding: 5px 14px;
            font-weight: 600;
            min-width: 100px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            margin-right: 2px;
        }

        QTabBar::tab:selected {
            background: #FFFFFF;
            color: #3B82F6;
            border-bottom: 2px solid #3B82F6;
        }

        QTabBar::tab:hover:!selected {
            background: #DDE2E7;
        }

        /* ─────── Toolbar ──────────────────────────────────────────── */
        QToolBar#mainToolbar {
            background: #ECEFF3;
            border-bottom: 1px solid #DDE2E7;
            padding: 6px 8px;
        }

        QFrame#capsuleGroup {
            background: #ffffff;
            border: 1px solid #d0d7de;
            border-radius: 12px;
            padding: 2px 4px;
        }

        #capsuleLabel {
            font-size: 10px;
            font-weight: 700;
            color: #57606a;
            padding: 0 4px;
        }

        QToolBar#mainToolbar QPushButton {
            border-radius: 4px;
            padding: 1px 6px;
            font-size: 10.5px;
            font-weight: 600;
            min-height: 16px;
            max-height: 20px;
        }

        #appNameBtn {
            font-size: 11px;
            font-weight: 700;
            padding: 1px 6px;
            border-radius: 4px;
        }
        
        #appName {
            font-size: 12px;
            font-weight: bold;
            color: #3B82F6;
        }
        
        #tbDiv {
            background: #DDE2E7;
            max-width: 1px;
            min-width: 1px;
            margin: 4px 6px;
        }
        
        #syncLabel {
            color: #F59E0B;
            font-weight: bold;
            font-size: 11px;
        }
        
        #syncStatus {
            color: #3B82F6;
            font-size: 10px;
        }
        
        /* ─────── View Dock Internals ──────────────────────────────── */
        #viewBar {
            background: #ECEFF3;
            border-bottom: 1px solid #DDE2E7;
        }
        
        #viewBarTitle {
            color: #1F2937;
            font-size: 10px;
            font-weight: 600;
        }
        
        #viewWidget {
            background: #FFFFFF;
        }
        
        #centerPlaceholder, #viewHost {
            background: #F4F6F8;
        }

        #sidebarScroll {
            background: #F4F6F8;
            border: none;
        }

        #offlineToolbar {
            background: #ECEFF3;
            border-bottom: 1px solid #DDE2E7;
        }

        #offlineHost {
            background: #F4F6F8;
            border-top: 1px solid #DDE2E7;
        }

        #operatorPanel {
            background: transparent;
        }

        #operatorCard {
            background: #FFFFFF;
            border: 1px solid #DDE2E7;
            border-radius: 6px;
        }

        #operatorTitle {
            color: #3B82F6;
            font-weight: 700;
            font-size: 11px;
        }

        #operatorState {
            color: #6B7280;
            font-size: 10px;
        }

        #compactLabel {
            color: #6B7280;
            font-size: 10px;
            font-weight: 600;
        }

        #compactEdit {
            min-height: 18px;
        }
        
        /* ─────── Camera Panel ─────────────────────────────────────── */
        #cameraPanel {
            background: #FFFFFF;
            border: 1px solid #DDE2E7;
            border-radius: 6px;
        }
        
        #panelTitle {
            font-size: 12px;
            font-weight: bold;
            color: #3B82F6;
            padding: 8px;
            background: #ECEFF3;
            border-bottom: 1px solid #DDE2E7;
            border-radius: 3px 3px 0 0;
        }
        
        #fpsLabel {
            color: #22C55E;
            font-weight: bold;
            font-size: 10px;
        }
        
        #separator {
            color: #E5E7EB;
            max-height: 1px;
            background: #e0e0e0;
        }
        
        /* ─────── Labels ───────────────────────────────────────────── */
        QLabel {
            color: #34495e;
            font-weight: 500;
        }
        
        /* ─────── Input Fields ─────────────────────────────────────── */
        QLineEdit, #compactEdit {
            background: #ffffff;
            border: 1px solid #bdc3c7;
            border-radius: 4px;
            padding: 4px 6px;
            color: #2c3e50;
            selection-background-color: #3498db;
            font-size: 11px;
        }

        QLineEdit:disabled, #compactEdit:disabled {
            background-color: #e9ecef;
            color: #adb5bd;
            border: 1px solid #ced4da;
        }
        
        QLineEdit:focus {
            border: 2px solid #3498db;
            padding: 5px 7px;
            background: #f0f7ff;
        }
        
        QLineEdit::placeholder-text {
            color: #95a5a6;
        }
        
        QComboBox, #compactCombo {
            background: #ffffff;
            border: 1px solid #bdc3c7;
            border-radius: 4px;
            padding: 4px 6px;
            color: #2c3e50;
            font-size: 11px;
        }

        QComboBox:disabled, #compactCombo:disabled {
            background-color: #e9ecef;
            color: #adb5bd;
            border: 1px solid #ced4da;
        }
        
        QComboBox:focus {
            border: 2px solid #3498db;
            padding: 5px 7px;
            background: #f0f7ff;
        }
        
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #3498db;
            width: 0px;
            height: 0px;
        }
        
        QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #bdc3c7;
            selection-background-color: #3498db;
            color: #2c3e50;
        }
        
        QDoubleSpinBox {
            background: #ffffff;
            border: 1px solid #bdc3c7;
            border-radius: 4px;
            padding: 3px 4px;
            color: #2c3e50;
            font-size: 11px;
        }

        QDoubleSpinBox:disabled {
            background-color: #e9ecef;
            color: #adb5bd;
            border: 1px solid #ced4da;
        }
        
        QDoubleSpinBox:focus {
            border: 2px solid #3498db;
            background: #f0f7ff;
        }
        
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            background: #ecf0f1;
            border: 1px solid #bdc3c7;
            border-radius: 2px;
            width: 20px;
        }
        
        QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
            background: #3498db;
        }
        
        /* ─────── Group Box ────────────────────────────────────────── */
        QGroupBox {
            border: 1px solid #e0e0e0;
            border-radius: 4px;
            margin-top: 10px;
            padding-top: 6px;
            color: #2980b9;
            font-weight: 600;
        }
        
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 3px;
        }
        
        /* ─────── Checkbox ─────────────────────────────────────────── */
        QCheckBox {
            color: #34495e;
            spacing: 6px;
        }

        QCheckBox:disabled {
            color: #adb5bd;
        }
        
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 2px solid #bdc3c7;
            border-radius: 3px;
            background: #ffffff;
        }

        QCheckBox::indicator:disabled {
            background: #e9ecef;
            border: 1px solid #ced4da;
        }
        
        QCheckBox::indicator:hover {
            border: 2px solid #3498db;
        }
        
        QCheckBox::indicator:checked {
            background: #3498db;
            border: 2px solid #bdc3c7;
        }

        QCheckBox::indicator:checked:hover {
            background: #2980b9;
            border: 2px solid #3498db;
        }
        
        /* ─────── Buttons ──────────────────────────────────────────── */
        QPushButton {
            background: #ecf0f1;
            color: #34495e;
            border: 1px solid #bdc3c7;
            border-radius: 4px;
            padding: 3px 8px;
            font-weight: 600;
            font-size: 11px;
            min-height: 16px;
        }
        
        QPushButton:hover {
            background: #d5d8dc;
            border-color: #95a5a6;
            color: #2c3e50;
        }
        
        QPushButton:pressed {
            background: #bdc3c7;
            border-color: #7f8c8d;
            color: #212529;
        }
        
        QPushButton:disabled,
        #presetBtn:disabled,
        #resetBtn:disabled,
        #connectBtn:disabled,
        #connectAllBtn:disabled,
        #startBtn:disabled,
        #startAllBtn:disabled,
        #saveBtn:disabled,
        #saveAllBtn:disabled,
        #syncBtn:disabled,
        #detectBtn:disabled,
        #modeToggleBtn:disabled,
        #floatBtn:disabled {
            background-color: #e9ecef;
            color: #adb5bd;
            border: 1px solid #ced4da;
        }
        
        /* ───── Action Buttons ───────────────────────────────────────── */
        #connectBtn, #connectAllBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #27ae60, stop:1 #229954);
            color: white;
            border: 1px solid #1e8449;
            font-weight: 700;
        }
        
        #connectBtn:hover, #connectAllBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #2ecc71, stop:1 #27ae60);
        }
        
        #connectBtn:pressed, #connectAllBtn:pressed {
            background: #1e8449;
        }
        
        #connectBtn[connected="true"], #connectAllBtn[connected="true"] {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #e74c3c, stop:1 #c0392b);
            border-color: #a93226;
        }
        
        #connectBtn[connected="true"]:hover, #connectAllBtn[connected="true"]:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #ec7063, stop:1 #c0392b);
        }
        
        #detectBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #475569, stop:1 #334155);
            color: white;
            border: 1px solid #1e293b;
            font-weight: 700;
        }
        
        #detectBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #64748b, stop:1 #475569);
        }

        #modeToggleBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #5b7290, stop:1 #475870);
            color: white;
            border: 1px solid #3b4b60;
            font-weight: 700;
        }
        
        #modeToggleBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #6d85a4, stop:1 #475870);
        }
        
        #startBtn, #startAllBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #2563eb, stop:1 #1d4ed8);
            color: white;
            border: 1px solid #1e40af;
            font-weight: 700;
        }
        
        #startBtn:hover, #startAllBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #3b82f6, stop:1 #1d4ed8);
        }
        
        #startBtn:pressed, #startAllBtn:pressed {
            background: #1d4ed8;
        }
        
        #startBtn[streaming="true"], #startAllBtn[streaming="true"] {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #f39c12, stop:1 #d68910);
            border-color: #b5690f;
        }
        
        #startBtn[streaming="true"]:hover, #startAllBtn[streaming="true"]:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #f5ad3d, stop:1 #d68910);
        }
        
        #saveBtn, #saveAllBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #9333ea, stop:1 #7e22ce);
            color: white;
            border: 1px solid #6b21a8;
            font-weight: 700;
        }
        
        #saveBtn:hover, #saveAllBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #a855f7, stop:1 #7e22ce);
        }
        
        #saveBtn[saving="true"], #saveAllBtn[saving="true"] {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #be185d, stop:1 #9d174d);
            border-color: #831843;
        }
        
        #saveBtn[saving="true"]:hover, #saveAllBtn[saving="true"]:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #db2777, stop:1 #9d174d);
        }
        
        #syncBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #e67e22, stop:1 #d35400);
            color: white;
            border: 1px solid #ba4a00;
            font-weight: 700;
        }
        
        #syncBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #f39c12, stop:1 #d35400);
        }
        
        #floatBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #9b59b6, stop:1 #8e44ad);
            color: white;
            border: 1px solid #7d3c98;
            font-weight: 700;
        }
        
        #floatBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #af7ac5, stop:1 #8e44ad);
        }
        
        #presetBtn {
            background: #ecf0f1;
            border-color: #bdc3c7;
            color: #34495e;
            font-size: 10px;
            padding: 2px 6px;
            min-height: 16px;
        }
        
        #presetBtn:hover {
            background: #d5d8dc;
            border-color: #95a5a6;
            color: #2c3e50;
        }
        
        #resetBtn {
            background: #ecf0f1;
            border-color: #bdc3c7;
            color: #34495e;
            font-size: 10px;
            padding: 2px 6px;
            min-height: 16px;
        }
        
        #resetBtn:hover {
            background: #d5d8dc;
            border-color: #95a5a6;
        }

        #presetBtn:disabled, #resetBtn:disabled {
            background-color: #e9ecef;
            color: #adb5bd;
            border: 1px solid #ced4da;
        }
        
        #smallBtn {
            padding: 2px 4px;
            font-size: 10px;
            min-height: 16px;
        }
        
        #toggleBtn {
            background: #ffffff;
            border: 1px solid #bdc3c7;
            color: #34495e;
            text-align: left;
            padding-left: 8px;
            border-radius: 3px;
        }
        
        #toggleBtn:hover {
            border-color: #3498db;
            color: #2980b9;
            background: #f0f7ff;
        }
        
        #exitBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #e74c3c, stop:1 #c0392b);
            color: white;
            border: 1px solid #a93226;
            font-weight: 700;
            font-size: 10px;
            padding: 0px;
            border-radius: 4px;
        }
        
        #exitBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #ec7063, stop:1 #c0392b);
        }
        
        /* ─────── Status and Display ───────────────────────────────── */
        #statusLabel {
            color: #34495e;
            font-size: 10px;
            border-top: 1px solid #e0e0e0;
            padding: 6px 8px;
            background: #fafafa;
        }
        
        #scrollArea {
            background: #ffffff;
            border: none;
        }

        #logTerminal {
            background: #ffffff;
            color: #2c3e50;
            border-top: 1px solid #d5d8dc;
            font-family: "DejaVu Sans Mono","Consolas","Monaco",monospace;
            font-size: 10px;
            padding: 4px;
        }
        
        #imageLabel {
            background: #f8f9fa;
            color: #95a5a6;
            font-size: 16px;
            font-weight: bold;
        }
        
        /* ─────── Scrollbars ───────────────────────────────────────── */
        QScrollBar:vertical, QScrollBar:horizontal {
            background: #f0f0f0;
            width: 10px;
            height: 10px;
        }
        
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: #bdc3c7;
            border-radius: 5px;
            min-height: 20px;
            min-width: 20px;
        }
        
        QScrollBar::handle:hover {
            background: #95a5a6;
        }
        
        QScrollBar::add-line, QScrollBar::sub-line {
            background: none;
            border: none;
        }
        
        /* ─────── Status Bar ───────────────────────────────────────── */
        QStatusBar {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #ffffff, stop:1 #f8f9fa);
            color: #34495e;
            font-size: 10px;
            border-top: 1px solid #d5d8dc;
        }
    )";
}

QString ThemeManager::getDarkStylesheet()
{
    return R"(
        QMainWindow, QWidget {
            background-color: #101010;
            color: #f0f0f0;
        }

        /* ─────── Tab Widget ───────────────────────────────────────── */
        QTabWidget::pane {
            border: 1px solid #2e2e2e;
            background: #101010;
        }

        QTabBar::tab {
            background: #1c1c1c;
            color: #8b949e;
            padding: 6px 14px;
            font-weight: 600;
            min-width: 100px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            margin-right: 2px;
        }

        QTabBar::tab:selected {
            background: #262626;
            color: #f0f0f0;
            border-bottom: 2px solid #c9a15d;
        }

        QTabBar::tab:hover:!selected {
            background: #222222;
            color: #c9a15d;
        }

        QToolBar#mainToolbar {
            background: #1c1c1c;
            border-bottom: 1px solid #2e2e2e;
            padding: 6px 8px;
        }

        QFrame#capsuleGroup {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 12px;
            padding: 2px 4px;
        }

        #capsuleLabel {
            font-size: 10px;
            font-weight: 700;
            color: #8b949e;
            padding: 0 4px;
        }

        QToolBar#mainToolbar QPushButton {
            border-radius: 4px;
            padding: 1px 6px;
            font-size: 10.5px;
            font-weight: 600;
            min-height: 16px;
            max-height: 20px;
        }

        #appNameBtn {
            font-size: 11px;
            font-weight: 700;
            padding: 1px 6px;
            border-radius: 4px;
        }

        #appName {
            color: #c9a15d;
        }

        #tbDiv {
            background: #2e2e2e;
        }

        #syncLabel {
            color: #c9a15d;
        }

        #syncStatus {
            color: #9b9b9b;
        }

        #viewBar {
            background: #1c1c1c;
            border-bottom: 1px solid rgba(255,255,255,0.08);
        }

        #viewBarTitle, QLabel {
            color: #f0f0f0;
            background: transparent;
        }

        #centerPlaceholder, #viewHost, #viewWidget, #scrollArea, #sidebarScroll {
            background: #0c0c0c;
        }

        #sidebarContent, #operatorPanel, #topControlsHost, #sidebarScroll > QWidget {
            background: #101010;
        }

        #offlineToolbar {
            background: #1c1c1c;
            border-bottom: 1px solid #2e2e2e;
        }

        #offlineHost {
            background: #101010;
            border-top: 1px solid #2e2e2e;
        }

        #operatorCard, #cameraPanel, QGroupBox {
            background: #1c1c1c;
            border: 1px solid rgba(255,255,255,0.08);
            color: #f0f0f0;
        }

        #operatorTitle, #panelTitle {
            color: #c9a15d;
            background: transparent;
            border: none;
        }

        #operatorState, #compactLabel, #statusLabel {
            color: #94a3b8;
            background: transparent;
        }

        #viewBarTitle, #modeSideLabel, #syncLabel, #syncStatus,
        #fpsLabel, #sensorTempLabel, #grabberTempLabel {
            background: transparent;
        }

        QLineEdit, #compactEdit, QComboBox, #compactCombo, QDoubleSpinBox {
            background: rgba(255,255,255,0.04);
            border: 1px solid rgba(255,255,255,0.12);
            color: #f0f0f0;
        }

        QLineEdit:disabled, #compactEdit:disabled,
        QComboBox:disabled, #compactCombo:disabled,
        QDoubleSpinBox:disabled {
            background-color: rgba(255,255,255,0.02);
            border: 1px solid rgba(255,255,255,0.06);
            color: rgba(255,255,255,0.22);
        }

        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus {
            background: rgba(255,255,255,0.04);
            border: 2px solid #c9a15d;
        }

        QComboBox QAbstractItemView {
            background: #1c1c1c;
            border: 1px solid rgba(255,255,255,0.12);
            selection-background-color: rgba(200, 164, 95, 0.35);
            color: #f0f0f0;
        }

        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            background: rgba(255,255,255,0.08);
            border: 1px solid rgba(255,255,255,0.16);
        }

        QPushButton {
            background: rgba(255,255,255,0.08);
            color: #f0f0f0;
            border: 1px solid rgba(255,255,255,0.16);
            border-radius: 6px;
        }

        QPushButton:hover {
            background: rgba(255,255,255,0.12);
            border-color: rgba(255,255,255,0.20);
            color: #ffffff;
        }

        QPushButton:disabled,
        #presetBtn:disabled,
        #resetBtn:disabled,
        #toggleBtn:disabled,
        #detectBtn:disabled,
        #connectBtn:disabled,
        #connectAllBtn:disabled,
        #startBtn:disabled,
        #startAllBtn:disabled,
        #saveBtn:disabled,
        #saveAllBtn:disabled,
        #syncBtn:disabled,
        #floatBtn:disabled,
        #modeToggleBtn:disabled {
            background-color: rgba(255,255,255,0.02);
            color: rgba(255,255,255,0.22);
            border: 1px solid rgba(255,255,255,0.06);
        }

        #presetBtn, #resetBtn {
            background: rgba(255,255,255,0.08);
            border-color: rgba(255,255,255,0.14);
            color: #f0f0f0;
        }

        #presetBtn:hover, #resetBtn:hover {
            background: rgba(255,255,255,0.14);
            border-color: rgba(255,255,255,0.20);
            color: #ffffff;
        }

        #presetBtn:disabled, #resetBtn:disabled {
            background-color: rgba(255,255,255,0.02);
            color: rgba(255,255,255,0.22);
            border: 1px solid rgba(255,255,255,0.06);
        }

        #toggleBtn {
            background: rgba(255,255,255,0.08);
            border: 1px solid rgba(255,255,255,0.16);
            color: #f0f0f0;
        }

        #toggleBtn:hover {
            border-color: #c9a15d;
            color: #c9a15d;
            background: rgba(255,255,255,0.12);
        }

        #detectBtn {
            background: #374151;
            border: 1px solid rgba(75, 85, 99, 0.55);
            color: #f3f4f6;
        }

        #detectBtn:hover {
            background: #4b5563;
        }

        #modeToggleBtn {
            background: #4b5563;
            border: 1px solid rgba(107, 114, 128, 0.45);
            color: #f4f7fb;
        }

        #modeToggleBtn:hover {
            background: #6b7280;
        }

        #connectAllBtn, #connectBtn {
            background: #2e7d32;
            border: 1px solid rgba(76, 175, 80, 0.45);
            color: #e8f5e9;
            font-weight: 700;
        }

        #connectAllBtn:hover, #connectBtn:hover {
            background: #388e3c;
        }

        #connectBtn[connected="true"], #connectAllBtn[connected="true"] {
            background: #a63636;
            border-color: rgba(183,90,90,0.55);
            color: #ffffff;
        }

        #connectBtn[connected="true"]:hover, #connectAllBtn[connected="true"]:hover {
            background: #b84a4a;
        }

        #startBtn, #startAllBtn {
            background: #1e40af;
            border: 1px solid rgba(59, 130, 246, 0.45);
            color: #eef3ff;
            font-weight: 700;
        }

        #startBtn:hover, #startAllBtn:hover {
            background: #2563eb;
        }

        #startBtn[streaming="true"], #startAllBtn[streaming="true"] {
            background: #d27f2b;
            border-color: rgba(210,140,81,0.55);
            color: #ffffff;
        }

        #startBtn[streaming="true"]:hover, #startAllBtn[streaming="true"]:hover {
            background: #de9147;
        }

        #saveBtn, #saveAllBtn {
            background: #6b21a8;
            border: 1px solid rgba(147, 51, 234, 0.45);
            color: #f3e8ff;
            font-weight: 700;
        }

        #saveBtn:hover, #saveAllBtn:hover {
            background: #7e22ce;
        }

        #saveBtn[saving="true"], #saveAllBtn[saving="true"] {
            background: #9d174d;
            border-color: rgba(190, 24, 93, 0.55);
            color: #ffffff;
        }

        #saveBtn[saving="true"]:hover, #saveAllBtn[saving="true"]:hover {
            background: #be185d;
        }

        #floatBtn {
            background: #52525b;
            border: 1px solid rgba(113, 113, 122, 0.45);
            color: #f4f4f5;
        }

        #floatBtn:hover {
            background: #71717a;
        }

        #syncBtn {
            background: #7a5537;
            border: 1px solid rgba(140,101,65,0.45);
            color: #f6e4c8;
        }

        #syncBtn:hover {
            background: #8f6b4d;
        }

        QCheckBox {
            color: #f0f0f0;
            background: transparent;
        }

        QCheckBox:disabled {
            color: rgba(255,255,255,0.22);
        }

        QCheckBox::indicator {
            background: #1f2937;
            border: 1px solid #64748b;
        }

        QCheckBox::indicator:disabled {
            background: rgba(255,255,255,0.02);
            border: 1px solid rgba(255,255,255,0.06);
        }

        QCheckBox::indicator:hover {
            border-color: #c9a15d;
        }

        QCheckBox::indicator:checked {
            background: #22c55e;
            border-color: #64748b;
        }

        QCheckBox::indicator:checked:hover {
            background: #16a34a;
            border-color: #c9a15d;
        }

        #imageLabel {
            background: #0c0c0c;
            color: #9b9b9b;
        }

        #logTerminal {
            background: #0c0c0c;
            color: #d6d6d6;
            border-top: 1px solid #2e2e2e;
            font-family: "DejaVu Sans Mono","Consolas","Monaco",monospace;
            font-size: 10px;
            padding: 4px;
        }

        #fpsLabel {
            color: #7fbf8f;
        }

        #separator {
            color: rgba(255,255,255,0.08);
            background: rgba(255,255,255,0.08);
        }

        QScrollBar:vertical, QScrollBar:horizontal {
            background: #101010;
        }

        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: #2e2e2e;
        }

        QScrollBar::handle:hover {
            background: #3a3a3a;
        }

        QStatusBar {
            background: #1c1c1c;
            color: #f0f0f0;
            border-top: 1px solid #2e2e2e;
        }
    )";
}

void ThemeManager::applyTheme(QWidget* window, bool darkMode)
{
    QString stylesheet = getLightStylesheet();
    if (darkMode) {
        stylesheet += getDarkStylesheet();
    }

    if (window) {
        window->setStyleSheet(stylesheet);
    }

    if (qApp) {
        qApp->setStyleSheet(stylesheet);
        for (QWidget* w : qApp->allWidgets()) {
            w->style()->unpolish(w);
            w->style()->polish(w);
            w->update();
        }
    }
}
