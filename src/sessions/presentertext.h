#pragma once

// Cross-format text/detail helpers shared by the A2L, DBC, and LDF presenters
// and their document sessions. Format-specific number formatting and
// protobuf-typed helpers stay in the per-format presenter headers.

#include "core/detailsection.h"

#include <QByteArrayView>
#include <QString>
#include <QStringConverter>
#include <QStringList>

#include <string>

#pragma push_macro("signals")
#undef signals
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/util/json_util.h>
#pragma pop_macro("signals")

// Strict UTF-8 decode: a stateless decoder flags any invalid or truncated
// sequence, so legitimate UTF-8 carrying U+FFFD survives and only genuine
// non-UTF-8 falls back to Latin-1.
inline QString text(const std::string& value) {
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    QString decoded = decoder.decode(QByteArrayView(value.data(), qsizetype(value.size())));
    if (decoder.hasError()) {
        return QString::fromLatin1(value.data(), qsizetype(value.size()));
    }
    return decoded;
}

inline QString boolText(bool value) {
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

inline QString hexId(quint32 value) {
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper();
}

inline QString hexValue(quint32 value) {
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper();
}

inline void addField(QList<DetailField>& fields, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        fields.push_back(DetailField{key, value});
    }
}

inline void pushSection(QList<DetailSection>& sections, const QString& title, QList<DetailField> fields) {
    if (!fields.isEmpty()) {
        sections.push_back(DetailSection{title, std::move(fields)});
    }
}

inline QString joinStrings(const google::protobuf::RepeatedPtrField<std::string>& values) {
    QStringList items;
    for (const std::string& value : values) {
        items.push_back(text(value));
    }
    return items.join(QStringLiteral(", "));
}

inline QString messageToJsonText(const google::protobuf::Message& message) {
    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = true;
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(message, &json, opts).ok()) {
        return {};
    }
    return text(json);
}
