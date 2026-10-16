#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Format-neutral value-table data. A producer's worker builds a page from the
// chunks of one bounded read per column, or an array slice from one sample of
// an array channel; the table installs it immutable and shared, so a
// producer's cache and the table hold one copy.

// A page holds this many samples, aligned on its multiples.
constexpr std::uint64_t kPageSamples = 1024;
// A text cell keeps at most this many UTF-8 Bytes of its text, its cut marker
// included.
constexpr std::size_t kCellBytes = 1024;

// The first sample of the page that holds `sample`.
constexpr std::uint64_t pageFirst(std::uint64_t sample) {
    return sample - sample % kPageSamples;
}

// One column of a page: a value per row, or a UTF-8 text per row. A column may
// hold fewer rows than its page; its cells past its end are empty.
struct TableColumn {
    enum class Kind : std::uint8_t { Values, Text };
    Kind kind = Kind::Values;
    std::size_t cellBytes = 0;        // Text: the most one cell holds, its cut marker included
    std::vector<double> values;       // Values: one per row, NaN where invalid
    std::vector<std::uint32_t> ends;  // Text: each row's end offset in `text`
    std::vector<char> text;           // Text: the rows' texts, one after another

    std::string_view textAt(std::size_t row) const;
    // The structure and its array capacities.
    std::uint64_t bytes() const;
};

// Consecutive samples of a channel: a time each and the page's columns.
struct TablePage {
    std::uint64_t firstSample = 0;    // absolute index of row 0
    std::uint64_t invalidValues = 0;  // of the rows, those the producer's own lane delivered invalid
    std::vector<double> time;
    std::vector<TableColumn> columns;

    std::size_t rows() const { return time.size(); }
    // The structure, its columns' slots and their capacities: what the page
    // retains.
    std::uint64_t bytes() const;
};

using TablePagePtr = std::shared_ptr<const TablePage>;

// What a producer asks of one column: its kind and, for a Text column, its
// cell cap in Bytes, its cut marker included.
struct TableColumnSpec {
    TableColumn::Kind kind = TableColumn::Kind::Values;
    std::size_t cellBytes = kCellBytes;
};

// Builds the page of at most `rows` samples from `first` out of the chunks of
// one read per column, each delivered in sample order. Construction allocates
// the time for `rows` and per column its values for `rows`, or its offsets for
// `rows` and its text at `rows` × its cell cap, the most its cells take, so
// nothing reallocates; finish() hands the page over as built. For every page
// of `rows`, delivered in full or short, page->bytes() +
// sizeof(TablePageBuilder) == reservation(rows, columns).
class TablePageBuilder {
public:
    TablePageBuilder(std::uint64_t first, std::uint64_t rows, const std::vector<TableColumnSpec>& columns);
    // What the builder allocates, before it exists: the page's structure, its
    // column slots and arrays, and sizeof(TablePageBuilder), which covers the
    // 16-Byte header of the make_shared block that holds the page on libstdc++
    // (tst_tablestorage measures it; a runtime with a larger header exceeds the
    // reservation by the difference).
    static std::uint64_t reservation(std::uint64_t rows, const std::vector<TableColumnSpec>& columns);

    void addTime(const double* time, std::size_t size);
    void addValues(std::size_t column, const double* values, std::size_t size);
    // A text longer than the column's cap keeps its longest prefix that ends on
    // a character boundary and leaves room for "…".
    void addText(std::size_t column, const std::string_view* texts, std::size_t size);
    // Each value as two uppercase hex digits per Byte, separated by spaces; a
    // value whose text would pass the column's cap keeps its first (cap − 3) / 3
    // Bytes and ends with "…". An empty value is an empty cell.
    void addBytes(std::size_t column, const std::string_view* values, std::size_t size);
    // Forgets a column's cells, as of a read that did not end Ok; its storage
    // stays allocated.
    void clearColumn(std::size_t column);
    // After the reads succeeded, with the count of invalid samples of the
    // producer's own lane.
    TablePagePtr finish(std::uint64_t invalidValues);

private:
    std::shared_ptr<TablePage> _page;
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
