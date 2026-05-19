#pragma once

#include "sessions/documentsession.h"

#include <QAbstractListModel>

#include <memory>
#include <vector>

class TabModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        FormatRole,
        SourcePathRole,
        HasWarningsRole
    };

    explicit TabModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int addSession(std::unique_ptr<DocumentSession> session);
    void closeSession(int index);
    DocumentSession* sessionAt(int index);
    const DocumentSession* sessionAt(int index) const;

private:
    std::vector<std::unique_ptr<DocumentSession>> _sessions;
};
