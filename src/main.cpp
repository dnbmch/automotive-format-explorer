#include "core/appcontroller.h"
#include "ui/memorygriditem.h"
#include "ui/signalgriditem.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QIcon>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Automotive Format Explorer");
    app.setOrganizationName("DanubeMechatronics");

    QIcon appIcon;
    for (int size : {16, 24, 32, 48, 64, 96, 128, 256, 512})
        appIcon.addFile(
            QString(":/resources/icons/explorer_%1.png").arg(size),
            QSize(size, size));
    app.setWindowIcon(appIcon);

    QQuickStyle::setStyle("Fusion");

    qmlRegisterType<MemoryGridItem>("ExplorerApp", 1, 0, "MemoryGridItem");
    qmlRegisterType<SignalGridItem>("ExplorerApp", 1, 0, "SignalGridItem");

    AppController controller;
    qmlRegisterSingletonInstance("ExplorerApp", 1, 0, "AppController", &controller);

    QQmlApplicationEngine engine;

    // On window creation: cloak so DWM doesn't flash a white frame,
    // show it (scene graph renders while cloaked), uncloak on first frame.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) return;
#ifdef Q_OS_WIN
            auto hwnd = reinterpret_cast<HWND>(window->winId());
            BOOL cloak = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));

            window->show();

            QObject::connect(window, &QQuickWindow::frameSwapped, window, [hwnd]() {
                BOOL uncloak = FALSE;
                DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &uncloak, sizeof(uncloak));
            }, Qt::SingleShotConnection);
#else
            window->show();
#endif
        });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("ExplorerApp", "Main");

    return app.exec();
}
