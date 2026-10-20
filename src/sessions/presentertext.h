#pragma once

// Cross-format text/detail helpers shared by the A2L, DBC, and LDF presenters
// and their document sessions. Format-specific number formatting and
// protobuf-typed helpers stay in the per-format presenter headers.

#include "core/detailsection.h"
#include "core/hextext.h"

#include <QByteArrayView>
#include <QString>
#include <QStringConverter>
#include <QStringList>

#include <string>

#pragma push_macro("signals")
#undef signals
#include <google/protobuf/descriptor.h>
#include <google/protobuf/generated_enum_reflection.h>
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

// The length of the prefix every value name of the enum shares, up to its
// last underscore: "CHARACTERISTIC_TYPE_" for CharacteristicType.
// (Descriptor names are std::string in older protobuf, string_view in newer.)
inline size_t enumPrefixLength(const google::protobuf::EnumDescriptor& descriptor) {
    const std::string first(descriptor.value(0)->name());
    size_t shared = first.size();
    for (int i = 1; i < descriptor.value_count(); ++i) {
        const std::string name(descriptor.value(i)->name());
        size_t n = 0;
        while (n < shared && n < name.size() && name[n] == first[n]) {
            ++n;
        }
        shared = n;
    }
    if (shared == 0) {
        return 0;
    }
    const size_t underscore = first.rfind('_', shared - 1);
    return underscore == std::string::npos ? 0 : underscore + 1;
}

// A proto enum value as the file's own keyword: its name without the shared
// prefix, since each value names its keyword after it (CHARACTERISTIC_TYPE_MAP
// is MAP). An UNSPECIFIED value is empty, so addField leaves it out; a number
// the enum does not declare reads as that number.
template<typename E>
QString enumKeyword(E value) {
    const google::protobuf::EnumDescriptor* descriptor = google::protobuf::GetEnumDescriptor<E>();
    static const size_t prefix = enumPrefixLength(*descriptor);
    const google::protobuf::EnumValueDescriptor* entry = descriptor->FindValueByNumber(value);
    if (!entry) {
        return QString::number(static_cast<int>(value));
    }
    const std::string keyword = std::string(entry->name()).substr(prefix);
    return keyword == "UNSPECIFIED" ? QString() : QString::fromStdString(keyword);
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
