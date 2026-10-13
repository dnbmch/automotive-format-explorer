#include "models/tabledata.h"

namespace {

// Marks a cut text; three UTF-8 Bytes.
constexpr std::string_view kCutMarker = "…";

bool continuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

} // namespace

std::string_view TextPage::text(std::size_t row) const {
    const std::uint32_t begin = row == 0 ? 0 : ends[row - 1];
    return {utf8.data() + begin, ends[row] - begin};
}

std::uint64_t TextPage::bytes() const {
    return sizeof(TextPage) + time.capacity() * sizeof(double) +
           ends.capacity() * sizeof(std::uint32_t) + utf8.capacity();
}

TextPageBuilder::TextPageBuilder(std::uint64_t first, std::uint64_t rows)
    : _page(std::make_shared<TextPage>()) {
    const auto count = static_cast<std::size_t>(rows);
    _page->firstSample = first;
    _page->time.reserve(count);
    _page->ends.reserve(count);
    _page->utf8.reserve(count * kTextCellBytes);
}

std::uint64_t TextPageBuilder::reservation(std::uint64_t rows) {
    return sizeof(TextPageBuilder) + sizeof(TextPage) +
           rows * (sizeof(double) + sizeof(std::uint32_t) + kTextCellBytes);
}

void TextPageBuilder::add(const double* time, const std::string_view* text, std::size_t size) {
    std::vector<char>& utf8 = _page->utf8;
    for (std::size_t i = 0; i < size; ++i) {
        std::string_view kept = text[i];
        std::string_view marker;
        if (kept.size() > kTextCellBytes) {
            // The first Byte left out must not continue a character.
            std::size_t cut = kTextCellBytes - kCutMarker.size();
            while (cut > 0 && continuationByte(kept[cut])) {
                --cut;
            }
            kept = kept.substr(0, cut);
            marker = kCutMarker;
        }
        utf8.insert(utf8.end(), kept.begin(), kept.end());
        utf8.insert(utf8.end(), marker.begin(), marker.end());
        _page->time.push_back(time[i]);
        _page->ends.push_back(static_cast<std::uint32_t>(utf8.size()));
    }
}

TextPagePtr TextPageBuilder::finish(std::uint64_t invalidValues) {
    _page->invalidValues = invalidValues;
    return std::move(_page);
}

std::uint64_t ArraySlice::bytes() const {
    return sizeof(ArraySlice) +
           (values.capacity() + rowAxis.values.capacity() + columnAxis.values.capacity()) *
               sizeof(double);
}
