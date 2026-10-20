#pragma once

#include <QString>
#include <QList>

// One row of a detail card. A row whose value names another entity carries
// that entity's tree row in `target`, and the panel shows it as a link that
// selects it; 0 is a plain value. An empty key spans the row.
struct DetailField {
    QString key;
    QString value;
    quint64 target = 0;
};

struct DetailSection {
    QString title;
    QList<DetailField> fields;
};
