#pragma once

#include "core/diagnostics.h"
#include "core/formatid.h"

#include <QString>
#include <QList>
#include <QUrl>

class TreeModel;
class DetailModel;
class QAbstractListModel;

class DocumentSession {
public:
    virtual ~DocumentSession() = default;

    virtual FormatId formatId() const = 0;
    virtual QString formatName() const = 0;
    virtual QString displayName() const = 0;
    virtual QString sourcePath() const = 0;
    virtual TreeModel* treeModel() = 0;
    virtual DetailModel* detailModel() = 0;
    virtual QList<DiagnosticMessage> diagnostics() const = 0;
    virtual bool hasWarnings() const = 0;
    virtual void selectNode(quint64 key) = 0;

    virtual QUrl centerPanelSource() const { return {}; }
    virtual QAbstractListModel* centerPanelModel() { return nullptr; }
};
