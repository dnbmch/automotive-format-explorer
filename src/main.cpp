#include "builtinformats.h"
#include "core/appcontroller.h"
#include "core/opensequence.h"
#include "ui/memorygriditem.h"
#include "ui/signalgriditem.h"
#include "ui/signalplotitem.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QIcon>

#include <cstdio>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Automotive Format Explorer");
    app.setOrganizationName("DanubeMechatronics");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Inspect A2L, DBC, LDF and MDF4 automotive files."));
    parser.addHelpOption();
    // The launch gates run the packaged application this way.
    const QCommandLineOption checkOption(QStringLiteral("check"),
        QStringLiteral("Open the files, or every bundled sample when none is named, then exit: "
                       "0 when each opened and the window drew them without a QML warning, "
                       "1 otherwise."));
    parser.addOption(checkOption);
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to open."),
                                 QStringLiteral("[files...]"));
    parser.process(app);
    const bool check = parser.isSet(checkOption);

    QIcon appIcon;
    for (int size : {16, 24, 32, 48, 64, 96, 128, 256, 512})
        appIcon.addFile(
            QString(":/resources/icons/explorer_%1.png").arg(size),
            QSize(size, size));
    app.setWindowIcon(appIcon);

    QQuickStyle::setStyle("Fusion");

    qmlRegisterType<MemoryGridItem>("ExplorerApp", 1, 0, "MemoryGridItem");
    qmlRegisterType<SignalGridItem>("ExplorerApp", 1, 0, "SignalGridItem");
    qmlRegisterType<SignalPlotItem>("ExplorerApp", 1, 0, "SignalPlotItem");
    // For its states in QML; a document session creates it.
    qmlRegisterUncreatableType<SignalPlotModel>("ExplorerApp", 1, 0, "SignalPlotModel",
                                                QStringLiteral("Provided by a document session"));

    AppController controller(builtInFormats());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, &AppController::shutdown);
    qmlRegisterSingletonInstance("ExplorerApp", 1, 0, "AppController", &controller);

    QQmlApplicationEngine engine;

    // A check counts every warning, so it listens before the window loads.
    int qmlWarnings = 0;
    if (check) {
        QObject::connect(&engine, &QQmlEngine::warnings, &app,
            [&qmlWarnings](const QList<QQmlError>& warnings) {
                for (const QQmlError& warning : warnings)
                    std::fprintf(stderr, "QML warning: %s\n", qPrintable(warning.toString()));
                qmlWarnings += warnings.size();
            });
    }

    // On window creation: cloak so DWM doesn't flash a white frame,
    // show it (scene graph renders while cloaked), uncloak on first frame.
    QPointer<QQuickWindow> mainWindow;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [&mainWindow](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) return;
            mainWindow = window;
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

    // Files named on the command line open one after another, each as a tab.
    QList<QUrl> files;
    for (const QString& argument : parser.positionalArguments())
        files.push_back(QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile));
    if (check && files.isEmpty()) {
        for (const QVariant& sample : controller.sampleFiles())
            files.push_back(sample.toMap().value(QStringLiteral("url")).toUrl());
    }
    OpenSequence opening(controller, files);

    if (check) {
        QObject::connect(&opening, &OpenSequence::outcome, &app,
            [](const QUrl& file, const QString& error) {
                std::fprintf(stderr, "%s: %s\n", qPrintable(file.fileName()),
                             error.isEmpty() ? "opened" : qPrintable(error));
            });
        // The verdict waits for the window to draw what the last open left on screen.
        QObject::connect(&opening, &OpenSequence::finished, &app, [&]() {
            if (!mainWindow) {
                std::fprintf(stderr, "check: no main window\n");
                QCoreApplication::exit(1);
                return;
            }
            QObject::connect(mainWindow, &QQuickWindow::frameSwapped, &app, [&]() {
                bool shown = mainWindow->isVisible();
#ifdef Q_OS_WIN
                DWORD cloaked = 0;
                DwmGetWindowAttribute(reinterpret_cast<HWND>(mainWindow->winId()),
                                      DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
                shown = shown && cloaked == 0;
#endif
                const bool passed = !files.isEmpty() && opening.failures() == 0
                                    && qmlWarnings == 0 && shown;
                std::fprintf(stderr, "check: %s: %lld file(s), %d not opened, %d QML warning(s), "
                             "main window %s\n", passed ? "passed" : "failed",
                             static_cast<long long>(files.size()), opening.failures(),
                             qmlWarnings, shown ? "shown" : "not shown");
                QCoreApplication::exit(passed ? 0 : 1);
            }, Qt::SingleShotConnection);
            mainWindow->update();
        });
    }

    opening.start();
    return app.exec();
}
