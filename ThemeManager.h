#pragma once

#include <QString>
class QWidget;

class ThemeManager
{
public:
    static QString getLightStylesheet();
    static QString getDarkStylesheet();
    static void applyTheme(QWidget* window, bool darkMode);
};

