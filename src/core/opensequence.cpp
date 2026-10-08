#include "core/opensequence.h"

#include "core/appcontroller.h"

OpenSequence::OpenSequence(AppController& controller, QList<QUrl> files, QObject* parent)
    : QObject(parent),
      _controller(controller),
      _files(std::move(files)) {
    connect(&_controller, &AppController::fileLoaded, this, [this] { settle(QString()); });
    // A failure is an error the controller raises while no load runs: its refusal
    // of the open itself, or a load that ended without a session. An error raised
    // while a load runs refused another open.
    connect(&_controller, &AppController::lastErrorChanged, this, [this] {
        if (!_controller.lastError().isEmpty() && !_controller.fileLoading()) {
            settle(_controller.lastError());
        }
    });
}

void OpenSequence::start() {
    QMetaObject::invokeMethod(this, &OpenSequence::openNext, Qt::QueuedConnection);
}

int OpenSequence::failures() const {
    return _failures;
}

void OpenSequence::openNext() {
    if (_next == _files.size()) {
        emit finished();
        return;
    }
    _pending = true;
    _controller.openFile(_files[_next]);
}

void OpenSequence::settle(const QString& error) {
    if (!_pending) {
        return;
    }
    _pending = false;
    const QUrl file = _files[_next++];
    if (!error.isEmpty()) {
        ++_failures;
    }
    emit outcome(file, error);
    QMetaObject::invokeMethod(this, &OpenSequence::openNext, Qt::QueuedConnection);
}
