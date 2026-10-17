#include "models/tabledata.h"

namespace {

// Marks a cut cell; three UTF-8 Bytes.
constexpr std::string_view kCutMarker = "…";
constexpr char kHexDigits[] = "0123456789ABCDEF";

bool continuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

} // namespace

std::string_view TableColumn::textAt(std::size_t row) const {
    const std::uint32_t begin = row == 0 ? 0 : ends[row - 1];
    return {text.data() + begin, ends[row] - begin};
}

std::uint64_t TableColumn::bytes() const {
    return sizeof(TableColumn) + values.capacity() * sizeof(double) +
           ends.capacity() * sizeof(std::uint32_t) + text.capacity();
}

// A column's structure sits in its slot, so the slots count once.
std::uint64_t TablePage::bytes() const {
    std::uint64_t bytes = sizeof(TablePage) + time.capacity() * sizeof(double) +
                          (columns.capacity() - columns.size()) * sizeof(TableColumn);
    for (const TableColumn& column : columns) {
        bytes += column.bytes();
    }
    return bytes;
}

TablePageBuilder::TablePageBuilder(std::uint64_t first, std::uint64_t rows,
                                   const std::vector<TableColumnSpec>& columns)
    : _page(std::make_shared<TablePage>()) {
    const auto count = static_cast<std::size_t>(rows);
    _page->firstSample = first;
    _page->time.reserve(count);
    _page->columns.reserve(columns.size());
    for (const TableColumnSpec& spec : columns) {
        TableColumn& column = _page->columns.emplace_back();
        column.kind = spec.kind;
        if (spec.kind == TableColumn::Kind::Values) {
            column.values.reserve(count);
        } else {
            column.cellBytes = spec.cellBytes;
            column.ends.reserve(count);
            column.text.reserve(count * spec.cellBytes);
        }
    }
}

std::uint64_t TablePageBuilder::reservation(std::uint64_t rows, const std::vector<TableColumnSpec>& columns) {
    std::uint64_t bytes = sizeof(TablePageBuilder) + sizeof(TablePage) +
                          columns.size() * sizeof(TableColumn) + rows * sizeof(double);
    for (const TableColumnSpec& column : columns) {
        bytes += column.kind == TableColumn::Kind::Values
            ? rows * sizeof(double)
            : rows * (sizeof(std::uint32_t) + column.cellBytes);
    }
    return bytes;
}

void TablePageBuilder::addTime(const double* time, std::size_t size) {
    _page->time.insert(_page->time.end(), time, time + size);
}

void TablePageBuilder::addValues(std::size_t column, const double* values, std::size_t size) {
    std::vector<double>& target = _page->columns[column].values;
    target.insert(target.end(), values, values + size);
}

void TablePageBuilder::addText(std::size_t column, const std::string_view* texts, std::size_t size) {
    TableColumn& target = _page->columns[column];
    for (std::size_t i = 0; i < size; ++i) {
        std::string_view kept = texts[i];
        std::string_view marker;
        if (kept.size() > target.cellBytes) {
            // The first Byte left out must not continue a character.
            std::size_t cut = target.cellBytes - kCutMarker.size();
            while (cut > 0 && continuationByte(kept[cut])) {
                --cut;
            }
            kept = kept.substr(0, cut);
            marker = kCutMarker;
        }
        target.text.insert(target.text.end(), kept.begin(), kept.end());
        target.text.insert(target.text.end(), marker.begin(), marker.end());
        target.ends.push_back(static_cast<std::uint32_t>(target.text.size()));
    }
}

void TablePageBuilder::addBytes(std::size_t column, const std::string_view* values, std::size_t size) {
    TableColumn& target = _page->columns[column];
    for (std::size_t i = 0; i < size; ++i) {
        const std::string_view value = values[i];
        // n Bytes take 3n − 1 characters; a cap below the marker's own size
        // keeps nothing, the marker included.
        const bool cut = !value.empty() && 3 * value.size() - 1 > target.cellBytes;
        const bool marked = cut && target.cellBytes >= kCutMarker.size();
        const std::size_t kept = !cut ? value.size() : marked ? (target.cellBytes - kCutMarker.size()) / 3 : 0;
        for (std::size_t b = 0; b < kept; ++b) {
            const auto byte = static_cast<unsigned char>(value[b]);
            if (b > 0) {
                target.text.push_back(' ');
            }
            target.text.push_back(kHexDigits[byte >> 4]);
            target.text.push_back(kHexDigits[byte & 0x0F]);
        }
        if (marked) {
            target.text.insert(target.text.end(), kCutMarker.begin(), kCutMarker.end());
        }
        target.ends.push_back(static_cast<std::uint32_t>(target.text.size()));
    }
}

// clear() keeps a vector's capacity, so the column's charge is unchanged.
void TablePageBuilder::clearColumn(std::size_t column) {
    TableColumn& target = _page->columns[column];
    target.values.clear();
    target.ends.clear();
    target.text.clear();
}

TablePagePtr TablePageBuilder::finish(std::uint64_t invalidValues, std::string note) {
    _page->invalidValues = invalidValues;
    _page->note = std::move(note);
    return std::move(_page);
}

std::uint64_t ArraySlice::bytes() const {
    return sizeof(ArraySlice) +
           (values.capacity() + rowAxis.values.capacity() + columnAxis.values.capacity()) *
               sizeof(double);
}
