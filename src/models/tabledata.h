#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Format-neutral value-table data. A producer's worker builds a text page from
// the text chunks of one bounded read, or an array slice from one sample of an
// array channel; the table installs it immutable and shared, so a producer's
// cache and the table hold one copy.

// A text table moves in pages of this many samples, aligned on its multiples.
constexpr std::uint64_t kTextPageSamples = 1024;
// A cell keeps at most this many UTF-8 Bytes of its text, its cut marker included.
constexpr std::size_t kTextCellBytes = 1024;

// The first sample of the page that holds `sample`.
constexpr std::uint64_t textPageFirst(std::uint64_t sample) {
    return sample - sample % kTextPageSamples;
}

// Consecutive samples of a text channel, a time and a UTF-8 text each. An
// invalid sample is an empty text.
struct TextPage {
    std::uint64_t firstSample = 0;    // absolute index of row 0
    std::uint64_t invalidValues = 0;  // of the rows, those the producer delivered invalid
    std::vector<double> time;
    std::vector<std::uint32_t> ends;  // each row's end offset in `utf8`
    std::vector<char> utf8;           // the rows' texts, one after another

    std::size_t rows() const { return time.size(); }
    std::string_view text(std::size_t row) const;
    // The structure and its array capacities: what the page retains.
    std::uint64_t bytes() const;
};

using TextPagePtr = std::shared_ptr<const TextPage>;

// Builds the page of at most `rows` samples from `first` out of the chunks of
// one read, delivered in sample order. A text longer than kTextCellBytes keeps
// its longest prefix that ends on a character boundary and leaves room for "…".
// Construction allocates the time and offset arrays for `rows` and
// kTextCellBytes of text arena per row, the most a cell takes, so nothing
// reallocates; finish() hands the page over as built. For every page of
// `rows`, delivered in full or short, page->bytes() + sizeof(TextPageBuilder)
// == reservation(rows).
class TextPageBuilder {
public:
    TextPageBuilder(std::uint64_t first, std::uint64_t rows);
    // What the builder allocates, before it exists: the page's structure and
    // arrays, and sizeof(TextPageBuilder), which covers the 16-Byte header of the
    // make_shared block that holds the page on libstdc++ (tst_tablestorage measures
    // it; a runtime with a larger header exceeds the reservation by the difference).
    static std::uint64_t reservation(std::uint64_t rows);

    void add(const double* time, const std::string_view* text, std::size_t size);
    // After the read succeeded, with the producer's count of invalid samples.
    TextPagePtr finish(std::uint64_t invalidValues);

private:
    std::shared_ptr<TextPage> _page;
};

// The labels along one axis of an array table: zero-based indices, one point
// per row or column, or one interval per row or column between consecutive
// borders.
struct TableAxis {
    enum class Kind : std::uint8_t { Index, Points, Intervals };
    Kind kind = Kind::Index;
    std::vector<double> values;  // Points: one per row or column; Intervals: one border more
    bool leftOpen = false;       // Intervals: ]a, b] instead of [a, b[
    std::string note;            // why the axis shows indices, when there is a reason to say
};

// One sample of an array channel as rows × columns values, row-major: the whole
// array of one or two dimensions, or one slice of more.
struct ArraySlice {
    std::uint64_t sample = 0;
    double time = std::numeric_limits<double>::quiet_NaN();
    std::uint64_t rows = 0;
    std::uint64_t columns = 0;
    std::vector<double> values;  // rows × columns; NaN where invalid
    TableAxis rowAxis;
    TableAxis columnAxis;

    // The structure and its value and label capacities; a note is not counted.
    std::uint64_t bytes() const;
};

using ArraySlicePtr = std::shared_ptr<const ArraySlice>;
