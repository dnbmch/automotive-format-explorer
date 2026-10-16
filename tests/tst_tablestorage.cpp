// A value-table read never holds more live heap than the reservation the
// session admits for it: TablePageBuilder::reservation for a page,
// mdf4array::reservation for an array sample. This executable's operator new
// records each allocation's requested size by its pointer and hands out
// malloc's pointer unchanged, so a shared library's delete of such a pointer
// and a delete here of one a shared library allocated both free it. Each case
// builds its inputs before its baseline and counts the Bytes live above it.
// Qt-free: exits 0 when every case holds, 1 otherwise, printing every case's
// numbers, and the hex cut of a page's Byte cells.

#include "models/tabledata.h"
#include "sessions/mdf4arrayread.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace heap {

// The live allocations made through this executable's operator new, by
// pointer: open addressing with linear probing, removal by backward shift. The
// test runs on one thread.
constexpr unsigned kBits = 20;
constexpr std::size_t kSlots = std::size_t{1} << kBits;
constexpr std::size_t kMask = kSlots - 1;

struct Slot {
    void* pointer;
    std::size_t size;
};

Slot slots[kSlots];
std::size_t entries = 0;
std::size_t live = 0;  // Bytes
std::size_t peak = 0;  // Bytes, since a case set it to its baseline

std::size_t home(const void* pointer) {
    return static_cast<std::size_t>(
        (std::uint64_t{reinterpret_cast<std::uintptr_t>(pointer)} * 0x9E3779B97F4A7C15ull) >> (64 - kBits));
}

void record(void* pointer, std::size_t size) {
    if (entries == kSlots / 2) {
        std::fputs("tst_tablestorage: more live allocations than the table holds\n", stderr);
        std::abort();
    }
    std::size_t at = home(pointer);
    while (slots[at].pointer != nullptr) {
        at = (at + 1) & kMask;
    }
    slots[at] = Slot{pointer, size};
    ++entries;
    live += size;
    peak = std::max(peak, live);
}

// A pointer allocated where the table does not see it has no entry.
void forget(void* pointer) {
    std::size_t at = home(pointer);
    while (slots[at].pointer != pointer) {
        if (slots[at].pointer == nullptr) {
            return;
        }
        at = (at + 1) & kMask;
    }
    live -= slots[at].size;
    --entries;
    // An entry further along the run moves into the hole when the hole lies
    // between its home slot and its own.
    std::size_t hole = at;
    for (std::size_t next = (at + 1) & kMask; slots[next].pointer != nullptr; next = (next + 1) & kMask) {
        if (((next - home(slots[next].pointer)) & kMask) >= ((next - hole) & kMask)) {
            slots[hole] = slots[next];
            hole = next;
        }
    }
    slots[hole] = Slot{};
}

} // namespace heap

void* operator new(std::size_t size) {
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc();
    }
    heap::record(pointer, size);
    return pointer;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    if (pointer != nullptr) {
        heap::forget(pointer);
        std::free(pointer);
    }
}

void operator delete[](void* pointer) noexcept {
    ::operator delete(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

namespace {

constexpr std::uint64_t kElements = 65536;

int failures = 0;

// One case's numbers, its peak counted above `baseline`. It fails when the
// peak passes the reservation, or stays below `arrays`, the Bytes its result
// holds in its arrays (then the result or the instrument came short), and a
// page also when its retained Bytes and the builder miss the reservation.
void report(const char* name, std::uint64_t reservation, std::uint64_t retained, std::uint64_t arrays,
            std::size_t baseline, bool page) {
    const std::uint64_t peak = heap::peak - baseline;
    const bool exact = !page || retained + sizeof(TablePageBuilder) == reservation;
    const bool pass = arrays <= peak && peak <= reservation && exact;
    std::printf("%s %s: reservation %llu, retained %llu, peak %llu%s\n", pass ? "PASS" : "FAIL", name,
                static_cast<unsigned long long>(reservation), static_cast<unsigned long long>(retained),
                static_cast<unsigned long long>(peak),
                exact ? "" : "; retained + sizeof(TablePageBuilder) is not the reservation");
    failures += pass ? 0 : 1;
}

// The page of `rows` and `columns`, filled by `fill(builder)`: its live Bytes
// stay within its reservation, which its retained Bytes and the builder make
// exactly.
template <typename Fill>
void page(const char* name, std::uint64_t rows, const std::vector<TableColumnSpec>& columns, Fill fill) {
    const std::size_t baseline = heap::live;
    heap::peak = baseline;
    TablePageBuilder builder(0, rows, columns);
    fill(builder);
    const TablePagePtr page = builder.finish(0);
    report(name, TablePageBuilder::reservation(rows, columns), page->bytes(),
           page->bytes() - sizeof(TablePage), baseline, true);
}

// A Byte value whose text would pass its column's cap keeps its first
// (cap - 3) / 3 Bytes and "…"; one within its cap stays whole.
void hexCut() {
    const std::string wide(400, '\x5A');
    const std::string frame(15, '\xA5');
    const std::string_view values[] = {wide, frame};
    TablePageBuilder builder(0, 1, {{TableColumn::Kind::Text}, {TableColumn::Kind::Text, 45}});
    builder.addBytes(0, &values[0], 1);
    builder.addBytes(1, &values[1], 1);
    const TablePagePtr page = builder.finish(0);
    std::string kept = "5A";
    for (int i = 1; i < 340; ++i) {
        kept += " 5A";
    }
    const std::string_view cut = page->columns[0].textAt(0);
    const std::string_view whole = page->columns[1].textAt(0);
    const bool pass = cut.size() == 1022 && cut == kept + "…" && whole.size() == 44 &&
                      whole.substr(0, 6) == "A5 A5 " && whole.substr(39) == "A5 A5";
    std::printf("%s hex cut: a 400-Byte value under the 1 KiB cap keeps %llu Bytes of text, a 15-Byte value "
                "under its 45-Byte cap %llu\n",
                pass ? "PASS" : "FAIL", static_cast<unsigned long long>(cut.size()),
                static_cast<unsigned long long>(whole.size()));
    failures += pass ? 0 : 1;
}

// Sample 0 of `document`'s array, planned as the session plans it and owned
// twice, as Active and the task hold the plan. The sample holds its values and
// the fixed axis's labels.
void arraySample(const mdf4::File& document, const mdf4array::AxisFunction& axis,
                 const mdf4array::ReadArrayFunction& readArray, const mdf4array::ScanFunction& scan) {
    const std::size_t baseline = heap::live;
    heap::peak = baseline;
    const mdf4array::ArrayPlan activePlan = mdf4array::arrayPlan(document, axis, 0, 0, 0, {});
    const mdf4array::ArrayPlan workerPlan = activePlan;
    const mdf4array::ArraySample sample = mdf4array::readArraySample(workerPlan, readArray, scan, {});
    report("array sample: 65,536 elements on a fixed axis of 65,536 points", mdf4array::reservation(activePlan),
           sample.bytes(), 2 * kElements * sizeof(double), baseline, false);
}

} // namespace

int main() {
    // The texts and values of one read, borrowed as the reader lends its chunk
    // storage, and the columns of a text page and of a frame page: eight value
    // columns, an 8-Byte MLSD payload at its 24-Byte cap and the 15-Byte
    // structure at its 45-Byte cap.
    const std::string full(kCellBytes, 'a');
    const std::string partial(700, 'b');
    const std::vector<double> times(kPageSamples, 0.0);
    const std::vector<std::string_view> fullTexts(kPageSamples, full);
    const std::vector<std::string_view> partialTexts(kPageSamples, partial);
    const std::vector<TableColumnSpec> text{{TableColumn::Kind::Text}};
    std::vector<TableColumnSpec> frame(8, TableColumnSpec{TableColumn::Kind::Values});
    frame.push_back({TableColumn::Kind::Text, 24});
    frame.push_back({TableColumn::Kind::Text, 45});
    const std::vector<double> values(kPageSamples, 7.0);
    const std::string payload(8, '\x55');
    const std::string structure(15, '\x3C');
    const std::vector<std::string_view> payloads(kPageSamples, payload);
    const std::vector<std::string_view> structures(kPageSamples, structure);

    // One group of one cycle; its array channel has one dimension of 65,536
    // elements on a fixed axis of as many points.
    mdf4::File document;
    mdf4::ChannelGroup* group = document.add_groups();
    group->set_cycle_count(1);
    mdf4::Channel* channel = group->add_channels();
    channel->set_sample_count(1);
    channel->set_decodable(true);
    mdf4::ChannelArray* array = channel->mutable_array();
    array->set_element_count(kElements);
    mdf4::ArrayDimension* dimension = array->add_dimensions();
    dimension->set_size(kElements);
    for (std::uint64_t i = 0; i < kElements; ++i) {
        dimension->add_axis_values(static_cast<double>(i));
    }
    const mdf4array::AxisFunction axis = [](std::uint32_t) {
        return mdf4::Axis{mdf4::AxisKind::SampleIndex, 0, 0};
    };
    // As the reader reads one sample: its time and the window's values.
    const mdf4array::ReadArrayFunction readArray = [](std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
                                                      const mdf4::ElementWindow& window, const mdf4::Control&) {
        mdf4::ReadArrayResult result;
        result.outcome.status = mdf4::Status::Ok;
        result.samplesRead = 1;
        result.series.time = {0.0};
        result.series.elements = window.count;
        result.series.value.assign(static_cast<std::size_t>(window.count), 7.0);
        return result;
    };
    const mdf4array::ScanFunction unusedScan;

    page("full page: 1,024 texts of 1,024 Bytes", kPageSamples, text, [&](TablePageBuilder& builder) {
        builder.addTime(times.data(), fullTexts.size());
        builder.addText(0, fullTexts.data(), fullTexts.size());
    });
    page("growth: 1,024 texts of 700 Bytes", kPageSamples, text, [&](TablePageBuilder& builder) {
        builder.addTime(times.data(), partialTexts.size());
        builder.addText(0, partialTexts.data(), partialTexts.size());
    });
    page("short delivery: 3 texts into 1,024 rows", kPageSamples, text, [&](TablePageBuilder& builder) {
        builder.addTime(times.data(), 3);
        builder.addText(0, fullTexts.data(), 3);
    });
    page("frame page: 1,024 rows, 8 value columns, an 8-Byte MLSD hex column and a 15-Byte structure column",
         kPageSamples, frame, [&](TablePageBuilder& builder) {
             builder.addTime(times.data(), times.size());
             for (std::size_t column = 0; column < 8; ++column) {
                 builder.addValues(column, values.data(), values.size());
             }
             builder.addBytes(8, payloads.data(), payloads.size());
             builder.addBytes(9, structures.data(), structures.size());
         });
    page("cleared column: the payload column emptied after its read failed", kPageSamples, frame,
         [&](TablePageBuilder& builder) {
             builder.addTime(times.data(), times.size());
             builder.addValues(0, values.data(), values.size());
             builder.addBytes(8, payloads.data(), payloads.size());
             builder.clearColumn(8);
         });
    hexCut();
    arraySample(document, axis, readArray, unusedScan);
    return failures == 0 ? 0 : 1;
}
