#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class AppController;

// Opens files one after another through the controller, which runs one load at a
// time, and reports each outcome; finished() follows the last. Every step runs
// from the event loop, start() included. The controller outlives the sequence.
class OpenSequence : public QObject {
    Q_OBJECT

public:
    OpenSequence(AppController& controller, QList<QUrl> files, QObject* parent = nullptr);

    void start();
    int failures() const;

signals:
    // An empty error means a usable document, not just a diagnostic tab.
    void outcome(const QUrl& file, const QString& error);
    void finished();

private:
    void openNext();
    void settle(const QString& error);

    AppController& _controller;
    const QList<QUrl> _files;
    qsizetype _next = 0;
    bool _pending = false;   // an open this sequence started awaits its outcome
    int _failures = 0;
};
