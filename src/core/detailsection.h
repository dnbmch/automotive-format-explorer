#pragma once

#include <QString>
#include <QList>

struct DetailField {
    QString key;
    QString value;
};

struct DetailSection {
    QString title;
    QList<DetailField> fields;
};
