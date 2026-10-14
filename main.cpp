#include "KYFGLibA.h"
#include <QApplication>
#include "MainWindow.h"
#include <memory>

// QApplication must be created before MainWindow
// so that screen geometry is available for dual monitor detection.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<std::shared_ptr<FramePacket>>("std::shared_ptr<FramePacket>");
    qRegisterMetaType<STREAM_BUFFER_HANDLE>("STREAM_BUFFER_HANDLE");
    qRegisterMetaType<size_t>("size_t");
    app.setApplicationName("DualDisplay");
    app.setOrganizationName("DualDisplay");
    MainWindow win;
    win.showMaximized();
    return app.exec();
}
