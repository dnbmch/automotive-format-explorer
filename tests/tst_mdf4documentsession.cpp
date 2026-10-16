#include "core/appcontroller.h"
#include "models/detailmodel.h"
#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "models/valuetablemodel.h"
#include "plotscan.h"
#include "sessions/mdf4arrayread.h"
#include "sessions/mdf4documentsession.h"
#include "sessions/mdf4pageread.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace {

// The heap blocks of a result the session never hands out. The allocation
// functions below replace the global ones and track, by address, the blocks
// of one watched size: the size of that result's largest arrays.
namespace heap {

std::atomic<std::size_t> watchedSize{0};
std::array<std::atomic<void*>, 4> watched{};

// Tracks the blocks of `size` allocated from now on; 0 tracks none.
void watch(std::size_t size) {
    watchedSize = 0;
    for (std::atomic<void*>& block : watched) {
        block = nullptr;
    }
    watchedSize = size;
}

int live() {
    int count = 0;
    for (const std::atomic<void*>& block : watched) {
        count += block.load() != nullptr ? 1 : 0;
    }
    return count;
}

} // namespace heap

} // namespace

void* operator new(std::size_t size) {
    void* block = std::malloc(size > 0 ? size : 1);
    if (!block) {
        throw std::bad_alloc();
    }
    if (size > 0 && size == heap::watchedSize.load()) {
        for (std::atomic<void*>& slot : heap::watched) {
            void* empty = nullptr;
            if (slot.compare_exchange_strong(empty, block)) {
                break;
            }
        }
    }
    return block;
}

void operator delete(void* block) noexcept {
    for (std::atomic<void*>& slot : heap::watched) {
        void* expected = block;
        if (block && slot.load() == block && slot.compare_exchange_strong(expected, nullptr)) {
            break;
        }
    }
    std::free(block);
}

void operator delete(void* block, std::size_t) noexcept {
    operator delete(block);
}

namespace {

using namespace std::chrono_literals;

// Channels of group 0: three signals, the group's time master, then a channel
// outside the read set. Group 1 has one signal and no master of its own.
enum Channel : int { Engine, Vehicle, Coolant, Master, Unsupported };

mdf4::File makeDocument(std::uint64_t sampleCount = 4) {
    mdf4::File document;
    document.set_version("4.20");
    mdf4::ChannelGroup* group = document.add_groups();
    group->set_name("Powertrain");
    group->set_cycle_count(sampleCount);
    group->set_storage(mdf4::ROW_DT);

    for (const char* name : {"EngineSpeed", "VehicleSpeed", "CoolantTemp"}) {
        mdf4::Channel* channel = group->add_channels();
        channel->set_name(name);
        channel->set_unit("rpm");
        channel->set_sample_count(sampleCount);
        channel->set_decodable(true);
        channel->set_data_type(mdf4::FLOAT_LE);
        channel->set_bit_count(64);
    }

    mdf4::Channel* master = group->add_channels();
    master->set_name("Acquisition time");
    master->set_unit("ms");
    master->set_sample_count(sampleCount);
    master->set_is_master(true);
    master->set_sync_type(1);
    master->set_cn_type(2);
    master->set_decodable(true);

    mdf4::Channel* unsupported = group->add_channels();
    unsupported->set_name("Label");
    unsupported->set_sample_count(sampleCount);
    unsupported->set_decodable(false);
    unsupported->set_not_decodable_reason("string data type");

    mdf4::ChannelGroup* body = document.add_groups();
    body->set_name("Body");
    body->set_cycle_count(sampleCount);
    mdf4::Channel* door = body->add_channels();
    door->set_name("DoorState");
    door->set_sample_count(sampleCount);
    door->set_decodable(true);
    return document;
}

// makeDocument() with a text channel after the unsupported one in group 0: the
// reader decodes it, and its samples are text.
constexpr int kText = Unsupported + 1;

mdf4::File makeTextDocument(std::uint64_t sampleCount = 4) {
    mdf4::File document = makeDocument(sampleCount);
    mdf4::Channel* message = document.mutable_groups(0)->add_channels();
    message->set_name("DriverMessage");
    message->set_sample_count(sampleCount);
    message->set_data_type(mdf4::STRING);
    message->set_raw_data_type(7);
    message->set_decodable(true);
    message->set_textual(true);
    return document;
}

// makeDocument() with a Byte channel after the unsupported one in group 0: the
// reader decodes it, and each of its samples is raw Bytes.
constexpr int kBytes = Unsupported + 1;

mdf4::File makeBytesDocument() {
    mdf4::File document = makeDocument();
    mdf4::Channel* frame = document.mutable_groups(0)->add_channels();
    frame->set_name("CAN_DataFrame");
    frame->set_sample_count(4);
    frame->set_data_type(mdf4::BYTE_ARRAY);
    frame->set_raw_data_type(10);
    frame->set_bit_count(120);
    frame->set_decodable(true);
    frame->set_binary(true);
    return document;
}

// makeDocument() with a bus log's structure after the unsupported channel in
// group 0, whose path separator is ".": the 15-Byte Byte channel CAN_DataFrame,
// its members ID (numeric), DataBytes (MLSD, 8 Bytes reserved), Note (text),
// Matrix (an array) and Odd (not decodable), then a signal of the group.
enum FrameChannel : int { kFrame = Unsupported + 1, kId, kDataBytes, kNote, kMatrix, kOdd, kBrake };

mdf4::File makeFrameDocument(std::uint64_t sampleCount = 4) {
    mdf4::File document = makeDocument(sampleCount);
    mdf4::ChannelGroup* group = document.mutable_groups(0);
    group->set_path_separator(".");
    const auto add = [group, sampleCount](const char* name, bool member) {
        mdf4::Channel* channel = group->add_channels();
        channel->set_name(name);
        channel->set_sample_count(sampleCount);
        channel->set_decodable(true);
        if (member) {
            channel->set_member_of(kFrame);
        }
        return channel;
    };
    mdf4::Channel* frame = add("CAN_DataFrame", false);
    frame->set_data_type(mdf4::BYTE_ARRAY);
    frame->set_raw_data_type(10);
    frame->set_bit_count(120);
    frame->set_binary(true);
    mdf4::Channel* id = add("CAN_DataFrame.ID", true);
    id->set_data_type(mdf4::UINT_LE);
    id->set_bit_count(32);
    mdf4::Channel* payload = add("CAN_DataFrame.DataBytes", true);
    payload->set_cn_type(5);
    payload->set_data_type(mdf4::BYTE_ARRAY);
    payload->set_raw_data_type(10);
    payload->set_bit_count(64);
    payload->set_binary(true);
    mdf4::Channel* note = add("CAN_DataFrame.Note", true);
    note->set_data_type(mdf4::STRING);
    note->set_raw_data_type(7);
    note->set_textual(true);
    mdf4::ChannelArray* matrix = add("CAN_DataFrame.Matrix", true)->mutable_array();
    matrix->add_dimensions()->set_size(2);
    matrix->add_dimensions()->set_size(3);
    matrix->set_element_count(6);
    mdf4::Channel* odd = add("CAN_DataFrame.Odd", true);
    odd->set_decodable(false);
    odd->set_not_decodable_reason("complex number data type");
    mdf4::Channel* brake = add("BrakePressure", false);
    brake->set_unit("bar");
    brake->set_data_type(mdf4::FLOAT_LE);
    brake->set_bit_count(64);
    return document;
}

// makeDocument() with an array channel after the unsupported one in group 0: the
// reader decodes it, and each of its samples is a whole array on index axes, by
// default a 52 x 142 classification result.
constexpr int kArray = Unsupported + 1;

mdf4::File makeArrayDocument(std::initializer_list<std::uint64_t> sizes = {52, 142},
                             std::uint32_t caType = 4) {
    mdf4::File document = makeDocument();
    mdf4::Channel* result = document.mutable_groups(0)->add_channels();
    result->set_name("2D_classification");
    result->set_unit("s");
    result->set_sample_count(4);
    result->set_data_type(mdf4::FLOAT_LE);
    result->set_bit_count(32);
    result->set_decodable(true);
    mdf4::ChannelArray* array = result->mutable_array();
    array->set_ca_type(caType);
    std::uint64_t elements = 1;
    for (const std::uint64_t size : sizes) {
        array->add_dimensions()->set_size(size);
        elements *= size;
    }
    array->set_element_count(elements);
    return document;
}

// The source's axes: group 0 has its own time master, group 1 none.
mdf4::Axis timeMaster(std::uint32_t group) {
    return group == 0 ? mdf4::Axis{mdf4::AxisKind::Master, 0, Master}
                      : mdf4::Axis{mdf4::AxisKind::SampleIndex, group, 0};
}

// A document whose array channels' axes are channels, one of each pairing.
// Arrays: 4 cycles behind its time master t; Map 3 x 2 on the interval axis
// Classes and the same-group scaling axis Local, Curve 3 x 4 on Speed and Count.
// Classes: one cycle, no master, 3 left-open intervals of 4 borders. Speeds: 3
// cycles behind a time master. Counts: 4 cycles, no master.
enum AxisGroup : std::uint32_t { kArrays, kClasses, kSpeeds, kCounts };
enum AxisChannel : int { kMap = 1, kLocal = 2, kCurve = 3 };

mdf4::ChannelArray* addArrayChannel(mdf4::ChannelGroup* group, const char* name,
                                    std::uint32_t caType,
                                    std::initializer_list<std::uint64_t> sizes) {
    mdf4::Channel* channel = group->add_channels();
    channel->set_name(name);
    channel->set_sample_count(group->cycle_count());
    channel->set_decodable(true);
    mdf4::ChannelArray* array = channel->mutable_array();
    array->set_ca_type(caType);
    std::uint64_t elements = 1;
    for (const std::uint64_t size : sizes) {
        array->add_dimensions()->set_size(size);
        elements *= size;
    }
    array->set_element_count(elements);
    return array;
}

void setAxisChannel(mdf4::ChannelArray* array, int dimension, std::uint32_t group,
                    std::uint32_t channel) {
    mdf4::ArrayDimension* axis = array->mutable_dimensions(dimension);
    axis->set_axis_from_channel(true);
    axis->set_axis_group(group);
    axis->set_axis_channel(channel);
}

mdf4::ChannelGroup* addAxisGroup(mdf4::File& document, const char* name, std::uint64_t cycles,
                                 bool master) {
    mdf4::ChannelGroup* group = document.add_groups();
    group->set_name(name);
    group->set_cycle_count(cycles);
    if (master) {
        mdf4::Channel* t = group->add_channels();
        t->set_name("t");
        t->set_unit("s");
        t->set_sample_count(cycles);
        t->set_is_master(true);
        t->set_sync_type(1);
        t->set_cn_type(2);
        t->set_decodable(true);
    }
    return group;
}

mdf4::File makeAxisDocument() {
    mdf4::File document;
    document.set_version("4.20");
    mdf4::ChannelGroup* arrays = addAxisGroup(document, "Arrays", 4, true);
    mdf4::ChannelArray* map = addArrayChannel(arrays, "Map", 2, {3, 2});
    setAxisChannel(map, 0, kClasses, 0);
    setAxisChannel(map, 1, kArrays, kLocal);
    addArrayChannel(arrays, "Local", 1, {2});
    mdf4::ChannelArray* curve = addArrayChannel(arrays, "Curve", 2, {3, 4});
    setAxisChannel(curve, 0, kSpeeds, 1);
    setAxisChannel(curve, 1, kCounts, 0);
    addArrayChannel(addAxisGroup(document, "Classes", 1, false), "Classes", 3, {4})
        ->set_left_open(true);
    addArrayChannel(addAxisGroup(document, "Speeds", 3, true), "Speed", 1, {3});
    addArrayChannel(addAxisGroup(document, "Counts", 4, false), "Count", 1, {4});
    return document;
}

// makeAxisDocument()'s axes: the groups with a master at channel 0 have it as
// their time master.
mdf4::Axis axisDocumentAxis(std::uint32_t group) {
    return group == kArrays || group == kSpeeds
        ? mdf4::Axis{mdf4::AxisKind::Master, group, 0}
        : mdf4::Axis{mdf4::AxisKind::SampleIndex, group, 0};
}

mdf4::ScanResult finished(mdf4::Status status, const char* message, std::string location = {}) {
    mdf4::ScanResult result;
    result.outcome.status = status;
    result.outcome.message = message;
    result.outcome.location = std::move(location);
    return result;
}

// The test's view of the session's reads, shared with the source's functions so
// no completion timing can leave either side dangling. Every channel holds
// `count` samples. A value scan delivers coordinate i and value 100 * (c + 1) + i
// of channel c, a text scan text "text i" at time i, a Byte scan the Bytes
// {c, i, 0x10 + i, 0x20 + i} at time i, an MLSD channel's cut to (i % 4) + 1 of
// them, an array read element k of sample s as 1000 s + k at time s. Each read
// is logged, delivers its clamped
// request in chunks, checks the cancel flag before each chunk, and when gated
// blocks before its first chunk until the test releases it or the session
// cancels it. Waits are bounded only as deadlock guards and record a timeout.
class Scans {
    // Defined first: the accessors below deduce their types from locked().
    template <typename F>
    bool waitUntil(F condition) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_changed.wait_for(lock, 10s, condition)) {
            return true;
        }
        _timedOut = true;
        return false;
    }
    template <typename F>
    void update(F change) {
        std::lock_guard<std::mutex> lock(_mutex);
        change();
        _changed.notify_all();
    }
    template <typename F>
    auto locked(F value) {
        std::lock_guard<std::mutex> lock(_mutex);
        return value();
    }

public:
    struct Request {
        enum Lane { Values, Text, Array, Bytes };
        Lane lane = Values;
        std::uint32_t group = 0;
        std::uint32_t channel = 0;
        std::uint64_t first = 0;
        std::uint64_t count = 0;
        mdf4::ElementWindow elements;     // Array
        std::size_t returnedBefore = 0;  // reads that had returned when this one began
    };

    explicit Scans(bool gated = true, std::uint64_t count = 4) : _count(count), _open(!gated) {}

    // Set before the first scan.
    std::function<std::pair<double, double>(std::uint32_t, std::uint64_t)> sample =
        [](std::uint32_t channel, std::uint64_t index) {
            return std::make_pair(static_cast<double>(index),
                                  100.0 * static_cast<double>(channel + 1) + static_cast<double>(index));
        };
    // A finished outcome in place of the samples.
    std::function<std::optional<mdf4::ScanResult>(const Request&)> outcome;
    // When at least 0, `outcome` is consulted after this many chunks were
    // delivered instead of before the first.
    int failAfterChunks = -1;
    // The source ends after this many samples: Ok and Short.
    std::uint64_t available = std::numeric_limits<std::uint64_t>::max();
    std::size_t chunk = 4096;
    bool honorCancel = true;
    // Reports a quarter done before waiting at the gate.
    bool progressBeforeGate = false;
    // A misbehaving source that reports progress after it saw the cancellation.
    bool progressAfterCancel = false;
    // Progress calls per chunk.
    int progressBurst = 1;
    // Time each chunk takes.
    std::chrono::milliseconds chunkTime{0};
    // An array channel's elements per sample, and those read as invalid.
    std::uint64_t elementCount = 52 * 142;
    std::vector<std::uint64_t> invalidElements;

    // Worker side: the source's four read lanes.
    mdf4::ScanResult scan(const Request& request, const mdf4::Control& control,
                          const mdf4::Visitor& visitor) {
        std::vector<double> time;
        std::vector<double> value;
        return run(request, control, [&](std::uint64_t first, std::size_t size) {
            time.resize(size);
            value.resize(size);
            for (std::size_t i = 0; i < size; ++i) {
                std::tie(time[i], value[i]) = sample(request.channel, first + i);
            }
            return visitor(mdf4::SampleChunk{first, time.data(), value.data(), size});
        });
    }

    mdf4::ScanResult scanText(const Request& request, const mdf4::Control& control,
                              const mdf4::TextVisitor& visitor) {
        std::vector<double> time;
        std::vector<std::string> texts;
        std::vector<std::string_view> views;
        return run(request, control, [&](std::uint64_t first, std::size_t size) {
            time.resize(size);
            texts.resize(size);
            views.resize(size);
            for (std::size_t i = 0; i < size; ++i) {
                time[i] = static_cast<double>(first + i);
                texts[i] = "text " + std::to_string(first + i);
                views[i] = texts[i];
            }
            return visitor(mdf4::TextChunk{first, time.data(), views.data(), size});
        });
    }

    mdf4::ScanResult scanBytes(const Request& request, bool mlsd, const mdf4::Control& control,
                               const mdf4::BytesVisitor& visitor) {
        const auto byte = [](std::uint64_t value) { return static_cast<char>(value & 0xFF); };
        std::vector<double> time;
        std::vector<std::string> values;
        std::vector<std::string_view> views;
        return run(request, control, [&](std::uint64_t first, std::size_t size) {
            time.resize(size);
            values.resize(size);
            views.resize(size);
            for (std::size_t i = 0; i < size; ++i) {
                const std::uint64_t index = first + i;
                time[i] = static_cast<double>(index);
                values[i] = {byte(request.channel), byte(index), byte(0x10 + index), byte(0x20 + index)};
                if (mlsd) {
                    values[i].resize(static_cast<std::size_t>(index % 4 + 1));
                }
                views[i] = values[i];
            }
            return visitor(mdf4::BytesChunk{first, time.data(), views.data(), size});
        });
    }

    mdf4::ReadArrayResult readArray(const Request& request, const mdf4::Control& control) {
        const std::uint64_t firstElement = std::min(request.elements.first, elementCount);
        const std::uint64_t elements =
            std::min(request.elements.count, elementCount - firstElement);
        // As the reader does: the series reserved for the clamped request.
        const std::uint64_t first = std::min(request.first, _count);
        const std::uint64_t samples = std::min(request.count, _count - first);
        mdf4::ArraySeries series{{}, {}, firstElement, elements};
        series.time.reserve(static_cast<std::size_t>(samples));
        series.value.reserve(static_cast<std::size_t>(samples * elements));
        mdf4::ReadArrayResult result;
        static_cast<mdf4::ScanResult&>(result) =
            run(request, control, [&](std::uint64_t first, std::size_t size) {
                for (std::uint64_t s = first; s < first + size; ++s) {
                    series.time.push_back(static_cast<double>(s));
                    for (std::uint64_t k = firstElement; k < firstElement + elements; ++k) {
                        const bool invalid = std::find(invalidElements.begin(), invalidElements.end(),
                                                       k) != invalidElements.end();
                        series.value.push_back(invalid ? std::nan("")
                                                       : 1000.0 * static_cast<double>(s) +
                                                             static_cast<double>(k));
                    }
                }
                return mdf4::Visit::Continue;
            });
        if (result.outcome.status == mdf4::Status::Ok) {
            result.series = std::move(series);
        }
        return result;
    }

    void note(const QString& event) {
        update([&] { _events.push_back(event); });
    }

    // Test side.
    bool waitForScans(int count) {
        return waitUntil([&] { return int(_requests.size()) >= count; });
    }
    bool waitForEvent(const QString& event) {
        return waitUntil([&] { return _events.contains(event); });
    }
    void release() { update([this] { _releases++; }); }
    void openGate() { update([this] { _open = true; }); }
    void closeGate() { update([this] { _open = false; }); }
    void startTeardown() { update([this] { _teardown = true; }); }
    bool waitUntilTeardown() { return waitUntil([this] { return _teardown; }); }
    void noteTimeout() { update([this] { _timedOut = true; }); }

    std::vector<std::uint32_t> channels() {
        return locked([this] {
            std::vector<std::uint32_t> out;
            for (const Request& r : _requests) out.push_back(r.channel);
            return out;
        });
    }
    std::vector<Request> requests() { return locked([this] { return _requests; }); }
    int maxConcurrent() { return locked([this] { return _maxInside; }); }
    bool timedOut() { return locked([this] { return _timedOut; }); }
    QStringList events() { return locked([this] { return _events; }); }

private:
    // Logs the read, waits at the gate, delivers the clamped request through
    // `send(first, size)` and logs its return.
    template <typename Send>
    mdf4::ScanResult run(Request request, const mdf4::Control& control, Send send) {
        const auto cancelled = [&] {
            return honorCancel && control.cancel && control.cancel->load();
        };
        if (progressBeforeGate && control.progress) {
            control.progress(request.count * 4, request.count * 16);
        }
        {
            std::unique_lock<std::mutex> lock(_mutex);
            request.returnedBefore = _returned;
            _requests.push_back(request);
            _inside++;
            _maxInside = std::max(_maxInside, _inside);
            _changed.notify_all();
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (!_open && _releases == 0 && !cancelled()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    _timedOut = true;
                    break;
                }
                _changed.wait_for(lock, 1ms);
            }
            if (!_open && _releases > 0 && !cancelled()) {
                _releases--;
            }
        }
        mdf4::ScanResult result = deliver(request, control, cancelled, send);
        update([&] {
            _inside--;
            _returned++;
            _events.push_back(QStringLiteral("scan %1 returned").arg(request.channel));
        });
        return result;
    }

    template <typename Cancelled, typename Send>
    mdf4::ScanResult deliver(const Request& request, const mdf4::Control& control,
                             Cancelled cancelled, Send send) {
        mdf4::ScanResult result;
        if (outcome && failAfterChunks < 0) {
            if (std::optional<mdf4::ScanResult> given = outcome(request)) {
                return *given;
            }
        }
        int delivered = 0;
        result.firstSample = std::min(request.first, _count);
        result.sampleCount = std::min(request.count, _count - result.firstSample);
        const std::uint64_t end = std::min(result.firstSample + result.sampleCount,
                                           std::max(available, result.firstSample));
        const std::uint64_t total = result.sampleCount * 16;
        for (std::uint64_t first = result.firstSample; first < end;) {
            if (cancelled()) {
                if (progressAfterCancel && control.progress) {
                    std::this_thread::sleep_for(150ms);
                    control.progress(total * 3 / 4, total);
                }
                result.outcome.status = mdf4::Status::Cancelled;
                result.outcome.message = "cancelled";
                return result;
            }
            for (int call = 1; control.progress && call <= progressBurst; ++call) {
                control.progress((first - result.firstSample) * 16 + std::uint64_t(call), total);
            }
            const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, end - first));
            switch (send(first, size)) {
            case mdf4::Visit::Continue:
                break;
            case mdf4::Visit::Cancel:
                result.outcome.status = mdf4::Status::Cancelled;
                result.outcome.message = "cancelled by the visitor";
                return result;
            case mdf4::Visit::ResourceLimit:
                result.outcome.status = mdf4::Status::ResourceLimit;
                result.outcome.resource = mdf4::Resource::Consumer;
                result.outcome.message = "refused by the visitor";
                return result;
            }
            first += size;
            result.samplesRead = first - result.firstSample;
            if (outcome && ++delivered == failAfterChunks) {
                if (std::optional<mdf4::ScanResult> given = outcome(request)) {
                    return *given;
                }
            }
            std::this_thread::sleep_for(chunkTime);
        }
        result.outcome.status = mdf4::Status::Ok;
        result.outcome.message = result.samplesRead < result.sampleCount ? "the source ends early" : "";
        result.coverage = result.samplesRead < result.sampleCount ? mdf4::Coverage::Short
                                                                  : mdf4::Coverage::Complete;
        return result;
    }

    const std::uint64_t _count;
    std::mutex _mutex;
    std::condition_variable _changed;
    std::vector<Request> _requests;
    QStringList _events;
    int _inside = 0;
    int _maxInside = 0;
    std::size_t _returned = 0;
    int _releases = 0;
    bool _open = false;
    bool _teardown = false;
    bool _timedOut = false;
};

// Stands in for the reader: owned only by the session's source functions and
// the copies its tasks capture, so its release marks the end of the source's
// life.
class Source {
public:
    explicit Source(std::shared_ptr<Scans> scans) : _scans(std::move(scans)) {}
    ~Source() { _scans->note(QStringLiteral("source released")); }

    Scans& scans() { return *_scans; }

private:
    std::shared_ptr<Scans> _scans;
};

std::unique_ptr<Mdf4DocumentSession> openSession(
    const std::shared_ptr<Scans>& scans, mdf4::File document = makeDocument(),
    const Mdf4SessionLimits& limits = {},
    Mdf4Source::AxisFunction axis = timeMaster) {
    auto source = std::make_shared<Source>(scans);
    using Request = Scans::Request;
    // The document's MLSD Byte channels: their values keep their valid Bytes.
    std::set<std::pair<std::uint32_t, std::uint32_t>> mlsd;
    for (int g = 0; g < document.groups_size(); ++g) {
        for (int c = 0; c < document.groups(g).channels_size(); ++c) {
            const mdf4::Channel& channel = document.groups(g).channels(c);
            if (channel.binary() && channel.cn_type() == 5) {
                mlsd.emplace(g, c);
            }
        }
    }
    return std::make_unique<Mdf4DocumentSession>(
        QStringLiteral("test.mf4"),
        QStringLiteral("test.mf4"),
        std::make_shared<const mdf4::File>(std::move(document)),
        Mdf4Source{
            [source](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                     std::uint64_t count, const mdf4::Control& control,
                     const mdf4::Visitor& visitor) {
                return source->scans().scan({Request::Values, group, channel, first, count},
                                            control, visitor);
            },
            [source](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                     std::uint64_t count, const mdf4::Control& control,
                     const mdf4::TextVisitor& visitor) {
                return source->scans().scanText({Request::Text, group, channel, first, count},
                                                control, visitor);
            },
            [source](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                     std::uint64_t count, const mdf4::ElementWindow& elements,
                     const mdf4::Control& control) {
                return source->scans().readArray(
                    {Request::Array, group, channel, first, count, elements}, control);
            },
            std::move(axis),
            [source, mlsd](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                           std::uint64_t count, const mdf4::Control& control,
                           const mdf4::BytesVisitor& visitor) {
                return source->scans().scanBytes({Request::Bytes, group, channel, first, count},
                                                 mlsd.count({group, channel}) > 0, control, visitor);
            }},
        QList<DiagnosticMessage>{}, limits);
}

// Opens through the controller: the adapter hands over a session whose reads
// the test gates.
class SessionAdapter final : public FormatAdapter {
public:
    explicit SessionAdapter(std::shared_ptr<Scans> scans, mdf4::File document = makeDocument())
        : _scans(std::move(scans)), _document(std::move(document)) {}

    LoadResult load(const QString&, const std::atomic<bool>&) const override {
        LoadResult result;
        result.session = openSession(_scans, _document);
        return result;
    }

private:
    std::shared_ptr<Scans> _scans;
    mdf4::File _document;
};

// The center view's model: the plot's, or the table's after a text selection.
SignalPlotModel* plotOf(Mdf4DocumentSession& session) {
    return qobject_cast<SignalPlotModel*>(session.centerPanelModel());
}

ValueTableModel* tableOf(Mdf4DocumentSession& session) {
    return qobject_cast<ValueTableModel*>(session.centerPanelModel());
}

bool centerIs(Mdf4DocumentSession& session, const char* view) {
    return session.centerPanelSource().toString().endsWith(QLatin1Char('/') + QLatin1String(view));
}

quint64 channelKey(Mdf4DocumentSession& session, int channelIndex, int groupIndex = 0) {
    TreeModel* tree = session.treeModel();
    const QModelIndex group = tree->index(groupIndex, 0, tree->index(0, 0));
    return tree->data(tree->index(channelIndex, 0, group), TreeModel::NodeKeyRole).toULongLong();
}

// The row of the first group's row `parent` at `row`: a structure's member.
quint64 memberKey(Mdf4DocumentSession& session, int parent, int row) {
    TreeModel* tree = session.treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    return tree->data(tree->index(row, 0, tree->index(parent, 0, group)), TreeModel::NodeKeyRole)
        .toULongLong();
}

quint64 groupKey(Mdf4DocumentSession& session) {
    TreeModel* tree = session.treeModel();
    return tree->data(tree->index(0, 0, tree->index(0, 0)), TreeModel::NodeKeyRole).toULongLong();
}

// A row of the first group: one of its channels, or kGroupRow for the group.
constexpr int kGroupRow = -1;

quint64 rowKey(Mdf4DocumentSession& session, int row) {
    return row == kGroupRow ? groupKey(session) : channelKey(session, row);
}

// The Name field of the detail panel's first card.
QString detailName(DetailModel* detail) {
    const QVariantList fields = detail->data(detail->index(0), DetailModel::FieldsRole).toList();
    for (const QVariant& field : fields) {
        const QVariantMap entry = field.toMap();
        if (entry.value(QStringLiteral("key")) == QStringLiteral("Name")) {
            return entry.value(QStringLiteral("value")).toString();
        }
    }
    return {};
}

// No scan in flight and none waiting for the event loop: runs the pool dry and
// delivers what it posted, again while completions start further scans. Only
// call it when no scan is blocked.
bool settle(Mdf4DocumentSession& session) {
    for (int round = 0; round < 50; ++round) {
        if (!QThreadPool::globalInstance()->waitForDone(10000)) {
            return false;
        }
        QCoreApplication::processEvents();
        if (session.resultBytes().reserved == 0 &&
            QThreadPool::globalInstance()->activeThreadCount() == 0) {
            return true;
        }
    }
    return false;
}

// The plot shows channel `name` settled: its samples exact, starting at `first`.
bool showsExact(SignalPlotModel* model, const char* name, double first) {
    return model->name() == QLatin1String(name) &&
           model->plotState() == SignalPlotModel::Detail && !model->busy() &&
           model->window()->value.front() == first;
}

// Releases every scan from another thread once teardown has been requested,
// so a scan is still blocked when destruction starts on the test thread.
// Destruction releases and joins on every path, including a failed check;
// declare it after the session.
class Releaser {
public:
    explicit Releaser(std::shared_ptr<Scans> scans)
        : _scans(std::move(scans)),
          _thread([this] {
              if (!_scans->waitUntilTeardown()) {
                  _scans->noteTimeout();
              }
              _scans->openGate();
          }) {}
    ~Releaser() {
        _scans->startTeardown();
        _scans->openGate();
        _thread.join();
    }

private:
    std::shared_ptr<Scans> _scans;
    std::thread _thread;
};

// Runs action once, from inside the first emission of signal for which when()
// holds: an ordinary same-thread observer calling back into the session.
template <typename Model, typename Signal, typename When, typename Action>
void callOnce(Model* model, Signal signal, When when, Action action) {
    auto done = std::make_shared<bool>(false);
    QObject::connect(model, signal, model, [done, when, action]() {
        if (*done || !when()) {
            return;
        }
        *done = true;
        action();
    });
}

// A selection's notifications, in the order it makes them: the detail panel's
// pre-reset, reset and raw form, then the plot's content.
enum Hook : int { DetailAboutToReset, DetailReset, DetailRawForm, PlotContent };

const char* hookName(int hook) {
    switch (hook) {
    case DetailAboutToReset: return "detail pre-reset";
    case DetailReset: return "detail reset";
    case DetailRawForm: return "detail raw form";
    default: return "plot content";
    }
}

// Runs action once, from inside the first `hook` notification after this call.
template <typename Action>
void callOnSelection(Mdf4DocumentSession& session, int hook, Action action) {
    const auto always = [] { return true; };
    DetailModel* detail = session.detailModel();
    switch (hook) {
    case DetailAboutToReset:
        callOnce(detail, &DetailModel::modelAboutToBeReset, always, action);
        break;
    case DetailReset:
        callOnce(detail, &DetailModel::modelReset, always, action);
        break;
    case DetailRawForm:
        callOnce(detail, &DetailModel::rawJsonChanged, always, action);
        break;
    default:
        callOnce(plotOf(session), &SignalPlotModel::contentChanged, always, action);
        break;
    }
}

// A channel too dense for one window at full view: 4.3 M samples.
constexpr std::uint64_t kDense = 4'300'000;

// The largest heap block of an overview of `stated` samples: its bins.
std::size_t binBytes(std::uint64_t stated) {
    return PlotOverviewBuilder(stated).finish(stated)->bins.capacity() * sizeof(PlotBin);
}

} // namespace

class TestMdf4DocumentSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void overviewThenExactWindowOfTheView();
    void domainComesFromTheSourceAxis();
    void masterChannelIsAnAxisNotASignal();
    void textChannelIsAnOrdinaryTreeChannel();
    void byteChannelShowsItsBytes();
    void structureMembersNestUnderTheirParent();
    void frameTableShown();
    void memberKeepsItsOwnView();
    void failedMemberLeavesItsColumnEmpty();
    void masterTypedMemberIsAValueChannel();
    void textPageShown();
    void textPagesAreBounded();
    void textReservationBecomesThePageCharge();
    void pageReservationBecomesThePageCharge();
    void numericSelectionCancelsTheTextRead();
    void textPageRefusedByTheAllowance();
    void cachedTextPageNeedsNoRead();
    void shutdownDuringTextRead();
    void closeCancelsTheTableRead_data();
    void closeCancelsTheTableRead();
    void reentryFromTableNotifications_data();
    void reentryFromTableNotifications();
    void arrayChannelIsAnOrdinaryTreeChannel();
    void arraySampleShown();
    void arraySamplePastTheEnd();
    void invalidArrayElementIsAnEmptyCell();
    void sliceSelectsTheWindow();
    void arraySliceRefusedByTheReader();
    void arrayAxesFollowTheAxisRule();
    void regressingTimePlotsSampleIndices();
    void supersededScansAreCancelled();
    void reselectingAfterCancelScansAgain();
    void reselectingTheRunningChannelKeepsItsScan();
    void cancelledWorkIsNeitherShownNorKept();
    void settledSelectionDropsPendingAndCancels();
    void outcomesStayDistinct();
    void zoomScansTheWindowTheViewNeeds();
    void rapidZoomCancelsObsoleteWindows();
    void staleProgressNeverReachesThePlot();
    void progressIsCoalesced();
    void reservationBecomesTheResultChargeWithoutGap();
    void obsoleteResultReleasedBeforeTheNextAdmission_data();
    void obsoleteResultReleasedBeforeTheNextAdmission();
    void unheldResultsEvictedLeastRecentlyUsed();
    void heldResultsRefuseAdmission();
    void sessionFromWorkerThreadDeliversOnOwnerThread();
    void closeCancelsScanInFlightAndDropsPending();
    void closeDiscardsFinishedUndeliveredScan();
    void selectionFromCompletionNotification();
    void selectionFromBusyNotificationStaysBusy();
    void closeFromCompletionNotification();
    void selectionFromSelectionNotification_data();
    void selectionFromSelectionNotification();
    void closeFromSelectionNotification_data();
    void closeFromSelectionNotification();
    void closingTabWithScanInFlight();
    void treeAdmittedUpToItsAllowance();
    void treeOverAllowanceShowsOnlyTheFileRow();
};

void TestMdf4DocumentSession::initTestCase() {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

// The overview scans the whole metadata range; the view it opens with fits one
// window, so the exact samples follow. Selecting the channel again shows both
// from the cache.
void TestMdf4DocumentSession::overviewThenExactWindowOfTheView() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(requests[0].channel, std::uint32_t(Engine));
    QCOMPARE(requests[0].first, std::uint64_t(0));
    QCOMPARE(requests[0].count, std::uint64_t(4));
    QCOMPARE(requests[1].first, std::uint64_t(0));
    QCOMPARE(requests[1].count, std::uint64_t(4));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(model->countText(), QStringLiteral("4 samples"));
    QCOMPARE(model->window()->time.size(), std::size_t(4));

    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(settle(*session));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QVERIFY(settle(*session));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Coolant, Coolant}));
    QVERIFY(!scans->timedOut());
}

// The domain is the axis the source resolved: the group's own master, another
// group's master, or the sample index. The session looks for no master itself.
void TestMdf4DocumentSession::domainComesFromTheSourceAxis() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Vehicle));
    QCOMPARE(model->domainName(), QStringLiteral("Acquisition time"));
    QCOMPARE(model->domainUnit(), QStringLiteral("ms"));
    session->selectNode(channelKey(*session, 0, 1));
    QCOMPARE(model->name(), QStringLiteral("DoorState"));
    QCOMPARE(model->domainName(), QStringLiteral("Sample index"));
    QVERIFY(model->domainUnit().isEmpty());
    QVERIFY(settle(*session));

    auto remote = openSession(scans, makeDocument(), {}, [](std::uint32_t) {
        return mdf4::Axis{mdf4::AxisKind::Master, 0, Master};
    });
    SignalPlotModel* remoteModel = plotOf(*remote);
    remote->selectNode(channelKey(*remote, 0, 1));
    QCOMPARE(remoteModel->domainName(), QStringLiteral("Acquisition time"));
    QCOMPARE(remoteModel->domainUnit(), QStringLiteral("ms"));
    QVERIFY(settle(*remote));
}

void TestMdf4DocumentSession::masterChannelIsAnAxisNotASignal() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    TreeModel* tree = session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    const QModelIndex master = tree->index(Master, 0, group);
    QCOMPARE(tree->data(master, TreeModel::TitleRole).toString(),
             QStringLiteral("Acquisition time"));
    QCOMPARE(tree->data(master, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Attribute));
    QVERIFY(tree->data(master, TreeModel::SubtitleRole).toString()
                .contains(QStringLiteral("Master channel")));

    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Master));
    QVERIFY(!model->busy());
    QVERIFY(!model->hasSamples());
    QCOMPARE(model->plotState(), SignalPlotModel::Empty);
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QCOMPARE(model->message(), QStringLiteral("Master channel — this group's time axis"));
    QVERIFY(settle(*session));
    QVERIFY(scans->channels().empty());
}

// Readable, so listed like any channel rather than as unsupported; its role
// says what it is.
void TestMdf4DocumentSession::textChannelIsAnOrdinaryTreeChannel() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeTextDocument());
    TreeModel* tree = session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    const QModelIndex row = tree->index(kText, 0, group);
    QCOMPARE(tree->data(row, TreeModel::TitleRole).toString(), QStringLiteral("DriverMessage"));
    QCOMPARE(tree->data(row, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Entity));
    QCOMPARE(tree->data(row, TreeModel::IconKeyRole).toString(), QStringLiteral("channel"));
    QCOMPARE(tree->data(row, TreeModel::SubtitleRole).toString(), QStringLiteral("Text channel"));
}

// Readable, so listed like any channel with its role; selecting it turns the
// center to the table, a page of its samples' times and Bytes from one Byte
// read.
void TestMdf4DocumentSession::byteChannelShowsItsBytes() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeBytesDocument());
    TreeModel* tree = session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    const QModelIndex row = tree->index(kBytes, 0, group);
    QCOMPARE(tree->data(row, TreeModel::TitleRole).toString(), QStringLiteral("CAN_DataFrame"));
    QCOMPARE(tree->data(row, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Entity));
    QCOMPARE(tree->data(row, TreeModel::IconKeyRole).toString(), QStringLiteral("channel"));
    QCOMPARE(tree->data(row, TreeModel::SubtitleRole).toString(), QStringLiteral("Byte channel"));

    session->selectNode(channelKey(*session, kBytes));
    QVERIFY(centerIs(*session, "ValueTableView.qml"));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QCOMPARE(table->message(), QStringLiteral("Reading bytes…"));
    QVERIFY(settle(*session));
    QCOMPARE(table->kind(), ValueTableModel::Page);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->role(), QStringLiteral("Byte channel"));
    QVERIFY(table->hasTimeColumn());
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Acquisition time [ms]"));
    QCOMPARE(table->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Bytes"));
    QVERIFY(!table->numericColumn(1));
    QCOMPARE(table->data(table->index(1, 0)).toString(), QStringLiteral("1"));
    QCOMPARE(table->data(table->index(1, 1)).toString(), QStringLiteral("05 01 11 21"));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(1));
    QCOMPARE(int(requests[0].lane), int(Scans::Request::Bytes));
    QCOMPARE(requests[0].channel, std::uint32_t(kBytes));
    QCOMPARE(requests[0].first, std::uint64_t(0));
    QCOMPARE(requests[0].count, std::uint64_t(4));
}

// A structure's direct members are listed under it, titled without the
// parent's name and the group's separator, each with its own role, icon and
// kind; the structure's role counts them, the next channel of the group is
// listed under the group again, and a member's details keep its full name.
void TestMdf4DocumentSession::structureMembersNestUnderTheirParent() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeFrameDocument());
    TreeModel* tree = session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    QCOMPARE(tree->rowCount(group), kFrame + 2);
    const QModelIndex frame = tree->index(kFrame, 0, group);
    QCOMPARE(tree->data(frame, TreeModel::TitleRole).toString(), QStringLiteral("CAN_DataFrame"));
    QCOMPARE(tree->data(frame, TreeModel::SubtitleRole).toString(),
             QStringLiteral("Structure of 5 members"));
    QCOMPARE(tree->data(frame, TreeModel::IconKeyRole).toString(), QStringLiteral("channel"));

    struct Member {
        const char* title;
        const char* subtitle;
        SemanticKind kind;
        const char* icon;
    };
    const Member members[] = {
        {"ID", "", SemanticKind::Entity, "channel"},
        {"DataBytes", "Byte channel", SemanticKind::Entity, "channel"},
        {"Note", "Text channel", SemanticKind::Entity, "channel"},
        {"Matrix", "Array 2 × 3", SemanticKind::Entity, "channel"},
        {"Odd", "Not plottable", SemanticKind::Diagnostic, "channel-unsupported"},
    };
    QCOMPARE(tree->rowCount(frame), int(std::size(members)));
    for (int row = 0; row < int(std::size(members)); ++row) {
        const QModelIndex member = tree->index(row, 0, frame);
        QCOMPARE(tree->data(member, TreeModel::TitleRole).toString(),
                 QString::fromUtf8(members[row].title));
        QCOMPARE(tree->data(member, TreeModel::SubtitleRole).toString(),
                 QString::fromUtf8(members[row].subtitle));
        QCOMPARE(tree->data(member, TreeModel::SemanticKindRole).toInt(),
                 static_cast<int>(members[row].kind));
        QCOMPARE(tree->data(member, TreeModel::IconKeyRole).toString(),
                 QString::fromUtf8(members[row].icon));
        QCOMPARE(tree->rowCount(member), 0);
    }
    const QModelIndex brake = tree->index(kFrame + 1, 0, group);
    QCOMPARE(tree->data(brake, TreeModel::TitleRole).toString(), QStringLiteral("BrakePressure"));
    QCOMPARE(tree->rowCount(brake), 0);

    session->selectNode(memberKey(*session, kFrame, 0));
    QCOMPARE(detailName(session->detailModel()), QStringLiteral("CAN_DataFrame.ID"));
    QVERIFY(settle(*session));
}

// Selecting the structure turns the center to its frame table: the time, a
// column per direct member that has one, then the structure's own Bytes. The
// page's lanes are read in order in one task, the structure's own first, each
// over the page's rows: the one reservation stands throughout and nothing
// reaches the table between them. The page is held while shown.
void TestMdf4DocumentSession::frameTableShown() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans, makeFrameDocument());
    session->selectNode(channelKey(*session, kFrame));
    QVERIFY(centerIs(*session, "ValueTableView.qml"));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QCOMPARE(table->kind(), ValueTableModel::Page);
    QCOMPARE(table->role(), QStringLiteral("Structure of 5 members"));
    QCOMPARE(table->message(), QStringLiteral("Reading frames…"));
    QVERIFY(table->hasTimeColumn());
    const QStringList headers{QStringLiteral("Acquisition time [ms]"), QStringLiteral("ID"),
                              QStringLiteral("DataBytes"), QStringLiteral("Note"),
                              QStringLiteral("Bytes")};
    QCOMPARE(table->columnCount(), int(headers.size()));
    for (int column = 0; column < int(headers.size()); ++column) {
        QCOMPARE(table->headerData(column, Qt::Horizontal).toString(), headers[column]);
        QCOMPARE(table->numericColumn(column), column == 1);
    }

    QVERIFY(scans->waitForScans(1));
    const std::uint64_t reserved = session->resultBytes().reserved;
    for (int read = 2; read <= 4; ++read) {
        scans->release();
        QVERIFY(scans->waitForScans(read));
        QCOMPARE(session->resultBytes().reserved, reserved);
        QCOMPARE(table->tableState(), ValueTableModel::Pending);
    }
    scans->release();
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    const std::pair<int, int> lanes[] = {{Scans::Request::Bytes, kFrame},
                                         {Scans::Request::Values, kId},
                                         {Scans::Request::Bytes, kDataBytes},
                                         {Scans::Request::Text, kNote}};
    QCOMPARE(requests.size(), std::size(lanes));
    for (std::size_t read = 0; read < requests.size(); ++read) {
        QCOMPARE(int(requests[read].lane), lanes[read].first);
        QCOMPARE(requests[read].channel, std::uint32_t(lanes[read].second));
        QCOMPARE(requests[read].first, std::uint64_t(0));
        QCOMPARE(requests[read].count, std::uint64_t(4));
        QCOMPARE(requests[read].returnedBefore, read);
    }
    QCOMPARE(scans->maxConcurrent(), 1);

    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(table->headerData(1, Qt::Vertical).toString(), QStringLiteral("1"));
    QCOMPARE(table->data(table->index(1, 0)).toString(), QStringLiteral("1"));
    QCOMPARE(table->data(table->index(1, 1)).toString(), QStringLiteral("701"));
    QCOMPARE(table->data(table->index(1, 2)).toString(), QStringLiteral("07 01"));
    QCOMPARE(table->data(table->index(1, 3)).toString(), QStringLiteral("text 1"));
    QCOMPARE(table->data(table->index(1, 4)).toString(), QStringLiteral("05 01 11 21"));
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 0–3 of 4"));
    QVERIFY(table->message().isEmpty());
    const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(1));
    QCOMPARE(bytes.held, std::size_t(1));
    QCOMPARE(bytes.retained, table->page()->bytes());
    QVERIFY(!scans->timedOut());
}

// A member keeps the view of its kind: ID plots, an overview scan of its own
// index; DataBytes shows a page of its own Bytes, an MLSD channel's valid ones.
void TestMdf4DocumentSession::memberKeepsItsOwnView() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeFrameDocument());
    SignalPlotModel* plot = plotOf(*session);
    session->selectNode(memberKey(*session, kFrame, 0));
    QVERIFY(centerIs(*session, "SignalPlotView.qml"));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(plot, "CAN_DataFrame.ID", 700.0));
    std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(int(requests.front().lane), int(Scans::Request::Values));
    QCOMPARE(requests.front().channel, std::uint32_t(kId));
    QCOMPARE(requests.front().first, std::uint64_t(0));
    QCOMPARE(requests.front().count, std::uint64_t(4));

    session->selectNode(memberKey(*session, kFrame, 1));
    QVERIFY(centerIs(*session, "ValueTableView.qml"));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->role(), QStringLiteral("Byte channel"));
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Bytes"));
    QCOMPARE(table->data(table->index(0, 1)).toString(), QStringLiteral("07"));
    QCOMPARE(table->data(table->index(3, 1)).toString(), QStringLiteral("07 03 13 23"));
    requests = scans->requests();
    QCOMPARE(int(requests.back().lane), int(Scans::Request::Bytes));
    QCOMPARE(requests.back().channel, std::uint32_t(kDataBytes));
}

// A member read that fails after a chunk it delivered leaves its whole column
// empty, the reader's chunks being provisional until a read ends Ok, and says
// why, the page shown and kept; a member read that finds the file changed
// fails the page, and the lanes after it are not read.
void TestMdf4DocumentSession::failedMemberLeavesItsColumnEmpty() {
    auto scans = std::make_shared<Scans>(false);
    scans->chunk = 1;
    scans->failAfterChunks = 1;
    scans->outcome = [](const Scans::Request& request) -> std::optional<mdf4::ScanResult> {
        if (request.lane == Scans::Request::Text && request.channel == std::uint32_t(kNote)) {
            return finished(mdf4::Status::Error, "block cannot be read", "DG[0]/CG[0]/CN[8]");
        }
        return std::nullopt;
    };
    auto session = openSession(scans, makeFrameDocument());
    session->selectNode(channelKey(*session, kFrame));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->message(),
             QStringLiteral("Note could not be read: block cannot be read (DG[0]/CG[0]/CN[8])"));
    QCOMPARE(table->rowCount(), 4);
    for (int row = 0; row < 4; ++row) {
        QCOMPARE(table->data(table->index(row, 3)).toString(), QString());
    }
    QCOMPARE(table->data(table->index(2, 1)).toString(), QStringLiteral("702"));
    QCOMPARE(table->data(table->index(2, 4)).toString(), QStringLiteral("05 02 12 22"));
    QCOMPARE(session->resultBytes().results, std::size_t(1));

    auto changed = std::make_shared<Scans>(false);
    changed->outcome = [](const Scans::Request& request) -> std::optional<mdf4::ScanResult> {
        if (request.lane == Scans::Request::Values && request.channel == std::uint32_t(kId)) {
            return finished(mdf4::Status::SourceChanged, "source file changed since it was opened");
        }
        return std::nullopt;
    };
    auto other = openSession(changed, makeFrameDocument());
    other->selectNode(channelKey(*other, kFrame));
    ValueTableModel* failed = tableOf(*other);
    QVERIFY(failed);
    QVERIFY(settle(*other));
    QCOMPARE(failed->tableState(), ValueTableModel::Failed);
    QCOMPARE(failed->message(), QStringLiteral("Frames could not be read: the file changed since it "
                                               "was opened; reopen it"));
    QCOMPARE(failed->rowCount(), 0);
    QCOMPARE(other->resultBytes().results, std::size_t(0));
    QCOMPARE(changed->requests().size(), std::size_t(2));
}

// A master-typed member is never its group's axis (the reader reads it as a
// value channel): listed under its structure without a role, plotted when
// selected, and a value column of the frame table.
void TestMdf4DocumentSession::masterTypedMemberIsAValueChannel() {
    mdf4::File document = makeFrameDocument();
    mdf4::Channel* stamp = document.mutable_groups(0)->add_channels();
    stamp->set_name("CAN_DataFrame.Timestamp");
    stamp->set_unit("s");
    stamp->set_sample_count(4);
    stamp->set_is_master(true);
    stamp->set_sync_type(1);
    stamp->set_cn_type(2);
    stamp->set_decodable(true);
    stamp->set_member_of(kFrame);
    const int kStamp = document.groups(0).channels_size() - 1;
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, std::move(document));
    TreeModel* tree = session->treeModel();
    const QModelIndex frame = tree->index(kFrame, 0, tree->index(0, 0, tree->index(0, 0)));
    QCOMPARE(tree->data(frame, TreeModel::SubtitleRole).toString(),
             QStringLiteral("Structure of 6 members"));
    const QModelIndex row = tree->index(5, 0, frame);
    QCOMPARE(tree->data(row, TreeModel::TitleRole).toString(), QStringLiteral("Timestamp"));
    QCOMPARE(tree->data(row, TreeModel::SubtitleRole).toString(), QStringLiteral("s"));
    QCOMPARE(tree->data(row, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Entity));

    SignalPlotModel* plot = plotOf(*session);
    session->selectNode(memberKey(*session, kFrame, 5));
    QVERIFY(centerIs(*session, "SignalPlotView.qml"));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(plot, "CAN_DataFrame.Timestamp", 100.0 * (kStamp + 1)));

    session->selectNode(channelKey(*session, kFrame));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->columnCount(), 6);
    QCOMPARE(table->headerData(4, Qt::Horizontal).toString(), QStringLiteral("Timestamp"));
    QVERIFY(table->numericColumn(4));
    QCOMPARE(table->data(table->index(1, 4)).toString(),
             QString::number(100.0 * (kStamp + 1) + 1.0));
}

// The center turns to the table, which shows the channel's first page: one
// bounded read, rows indexed by sample with their time and text, the page held
// while shown.
void TestMdf4DocumentSession::textPageShown() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeTextDocument());
    SignalPlotModel* plot = plotOf(*session);
    session->selectNode(channelKey(*session, kText));
    QCOMPARE(detailName(session->detailModel()), QStringLiteral("DriverMessage"));
    QVERIFY(centerIs(*session, "ValueTableView.qml"));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(!plot->busy());
    QVERIFY(plot->name().isEmpty());
    QVERIFY(settle(*session));

    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->name(), QStringLiteral("DriverMessage"));
    QCOMPARE(table->role(), QStringLiteral("Text channel"));
    QVERIFY(!table->busy());
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->headerData(0, Qt::Vertical).toString(), QStringLiteral("0"));
    QCOMPARE(table->data(table->index(0, 0)).toString(), QStringLiteral("0"));
    QCOMPARE(table->data(table->index(0, 1)).toString(), QStringLiteral("text 0"));
    QCOMPARE(table->data(table->index(3, 1)).toString(), QStringLiteral("text 3"));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(1));
    QCOMPARE(int(requests[0].lane), int(Scans::Request::Text));
    QCOMPARE(requests[0].channel, std::uint32_t(kText));
    QCOMPARE(requests[0].first, std::uint64_t(0));
    QCOMPARE(requests[0].count, std::uint64_t(4));
    const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(1));
    QCOMPARE(bytes.held, std::size_t(1));
}

// A page is 1,024 samples aligned on its multiples, one read each; the last
// page holds what remains. While a new page is read, the old rows are gone.
void TestMdf4DocumentSession::textPagesAreBounded() {
    auto scans = std::make_shared<Scans>(false, 3000);
    auto session = openSession(scans, makeTextDocument(3000));
    session->selectNode(channelKey(*session, kText));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->rowCount(), 1024);
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 0–1,023 of 3,000"));

    table->showSample(2500);
    QCOMPARE(table->tableState(), ValueTableModel::Pending);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(int(requests[1].lane), int(Scans::Request::Text));
    QCOMPARE(requests[1].first, std::uint64_t(2048));
    QCOMPARE(requests[1].count, std::uint64_t(952));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->rowCount(), 952);
    QCOMPARE(table->headerData(0, Qt::Vertical).toString(), QStringLiteral("2048"));
    QCOMPARE(table->data(table->index(951, 1)).toString(), QStringLiteral("text 2999"));
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 2,048–2,999 of 3,000"));
}

// The page's reservation is admitted before its read launches; the completion
// turns it into the page's own charge, which never exceeds it.
void TestMdf4DocumentSession::textReservationBecomesThePageCharge() {
    auto scans = std::make_shared<Scans>(true, 3000);
    auto session = openSession(scans, makeTextDocument(3000));
    session->selectNode(channelKey(*session, kText));
    QVERIFY(scans->waitForScans(1));
    Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, TablePageBuilder::reservation(1024, {{TableColumn::Kind::Text}}));
    QCOMPARE(bytes.retained, std::uint64_t(0));

    scans->release();
    QVERIFY(settle(*session));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table && table->page());
    bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, std::uint64_t(0));
    QCOMPARE(bytes.retained, table->page()->bytes());
    QVERIFY(bytes.retained <= TablePageBuilder::reservation(1024, {{TableColumn::Kind::Text}}));
    QVERIFY(!scans->timedOut());
}

// A frame page's reservation, the time and every lane's column at its cap, is
// admitted before its first read launches; the completion turns it into the
// page's own charge, which with the builder makes the reservation exactly.
void TestMdf4DocumentSession::pageReservationBecomesThePageCharge() {
    auto scans = std::make_shared<Scans>(true, 3000);
    auto session = openSession(scans, makeFrameDocument(3000));
    session->selectNode(channelKey(*session, kFrame));
    QVERIFY(scans->waitForScans(1));
    // The caps, stated apart from the plan: the 120-bit structure's 45
    // characters of hex, the MLSD payload's 24 for its 8 Bytes, a text's 1,024;
    // the reservation is the structures, the slots and the rows at those caps.
    const mdf4page::PagePlan plan = mdf4page::pagePlan(makeFrameDocument(3000), 0, kFrame, 0);
    QCOMPARE(plan.cellBytes, std::size_t(45));
    QCOMPARE(plan.members.size(), std::size_t(3));
    QCOMPARE(plan.members[1].cellBytes, std::size_t(24));
    QCOMPARE(plan.members[2].cellBytes, std::size_t(1024));
    const std::uint64_t reservation = mdf4page::reservation(plan);
    QCOMPARE(reservation, sizeof(TablePageBuilder) + sizeof(TablePage) + 4 * sizeof(TableColumn) +
                              1024 * (8 + 8 + (4 + 24) + (4 + 1024) + (4 + 45)));
    Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, reservation);
    QCOMPARE(bytes.retained, std::uint64_t(0));

    scans->openGate();
    QVERIFY(settle(*session));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table && table->page());
    QCOMPARE(table->rowCount(), 1024);
    bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, std::uint64_t(0));
    QCOMPARE(bytes.retained, table->page()->bytes());
    QCOMPARE(bytes.retained + sizeof(TablePageBuilder), reservation);
    QVERIFY(!scans->timedOut());
}

// Selecting a numeric channel while a page is read turns the center back to the
// plot and cancels the read; its completion is taken before the overview
// starts, so the reader never runs two operations at once, and no page stays.
void TestMdf4DocumentSession::numericSelectionCancelsTheTextRead() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans, makeTextDocument());
    SignalPlotModel* plot = plotOf(*session);
    session->selectNode(channelKey(*session, kText));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(scans->waitForScans(1));
    QVERIFY(table->busy());

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(centerIs(*session, "SignalPlotView.qml"));
    QVERIFY(session->centerPanelModel() == plot);
    QCOMPARE(table->tableState(), ValueTableModel::NoChannel);
    QVERIFY(!table->busy());
    QVERIFY(plot->busy());
    QVERIFY(scans->waitForEvent(QStringLiteral("scan %1 returned").arg(kText)));
    QTRY_COMPARE_WITH_TIMEOUT(scans->requests().size(), std::size_t(2), 5000);
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(plot, "EngineSpeed", 100.0));

    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(3));
    QCOMPARE(int(requests[0].lane), int(Scans::Request::Text));
    QCOMPARE(int(requests[1].lane), int(Scans::Request::Values));
    QCOMPARE(requests[1].channel, std::uint32_t(Engine));
    QCOMPARE(requests[1].returnedBefore, std::size_t(1));
    const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(2));
    QCOMPARE(bytes.retained, plot->overview()->bytes() + plot->window()->bytes());
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// A page the allowance cannot hold is refused before any read, with the
// numbers, where the rows would have shown.
void TestMdf4DocumentSession::textPageRefusedByTheAllowance() {
    auto scans = std::make_shared<Scans>(false, 3000);
    const Mdf4SessionLimits limits{Mdf4SessionLimits{}.treeBytes,
                                   TablePageBuilder::reservation(1024, {{TableColumn::Kind::Text}}) - 1};
    auto session = openSession(scans, makeTextDocument(3000), limits);
    session->selectNode(channelKey(*session, kText));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Refused);
    QVERIFY2(table->message().startsWith(QStringLiteral("Texts need ")) &&
                 table->message().contains(QStringLiteral("but results in use hold")),
             qPrintable(table->message()));
    QVERIFY(!table->busy());
    QVERIFY(settle(*session));
    QVERIFY(scans->requests().empty());
    QCOMPARE(session->resultBytes().reserved, std::uint64_t(0));
}

// A page read once is kept like a plot result: selecting its channel again
// shows it at once, without a read.
void TestMdf4DocumentSession::cachedTextPageNeedsNoRead() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeTextDocument());
    session->selectNode(channelKey(*session, kText));
    QVERIFY(settle(*session));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));

    session->selectNode(channelKey(*session, kText));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QVERIFY(!table->busy());
    QCOMPARE(table->data(table->index(2, 1)).toString(), QStringLiteral("text 2"));
    QVERIFY(settle(*session));
    std::vector<int> lanes;
    for (const Scans::Request& request : scans->requests()) {
        lanes.push_back(request.lane);
    }
    QCOMPARE(lanes, (std::vector<int>{Scans::Request::Text, Scans::Request::Values,
                                      Scans::Request::Values}));
}

// Shutting down while a page is read: the controller's teardown destroys the
// session, which cancels the read and waits for it before its models go.
void TestMdf4DocumentSession::shutdownDuringTextRead() {
    auto scans = std::make_shared<Scans>();
    FormatList formats;
    formats.push_back({FormatId::MDF4, {QStringLiteral("mf4")},
                       std::make_unique<SessionAdapter>(scans, makeTextDocument())});
    auto controller = std::make_unique<AppController>(std::move(formats));
    QSignalSpy loaded(controller.get(), &AppController::fileLoaded);
    controller->openFile(QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("test.mf4"))));
    QVERIFY(loaded.wait(10000));
    auto* session = static_cast<Mdf4DocumentSession*>(controller->tabModel()->tabAt(0)->session());

    controller->selectCurrentNode(channelKey(*session, kText));
    QVERIFY(scans->waitForScans(1));
    auto* table = qobject_cast<ValueTableModel*>(controller->centerPanelModel());
    QVERIFY(table);
    QObject::connect(table, &QObject::destroyed,
                     [scans] { scans->note(QStringLiteral("table destroyed")); });
    controller->shutdown();
    controller.reset();

    const QStringList events = scans->events();
    const qsizetype returned = events.indexOf(QStringLiteral("scan %1 returned").arg(kText));
    const qsizetype destroyed = events.indexOf(QStringLiteral("table destroyed"));
    QVERIFY2(returned >= 0 && destroyed > returned, qPrintable(events.join(QStringLiteral(", "))));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(scans->requests().size(), std::size_t(1));
    QVERIFY(!scans->timedOut());
}

void TestMdf4DocumentSession::closeCancelsTheTableRead_data() {
    QTest::addColumn<int>("lane");
    QTest::newRow("text page") << int(Scans::Request::Text);
    QTest::newRow("array sample") << int(Scans::Request::Array);
    QTest::newRow("frames") << int(Scans::Request::Bytes);
}

// Closing while a page or an array sample is read cancels the read and waits
// for it; the source goes once the read's task let go of it, and a frame page
// reads no member after its cancelled first lane. A sample's reservation is
// admitted before its read launches.
void TestMdf4DocumentSession::closeCancelsTheTableRead() {
    QFETCH(int, lane);
    const bool array = lane == Scans::Request::Array;
    const bool frames = lane == Scans::Request::Bytes;
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans, array    ? makeArrayDocument()
                                      : frames ? makeFrameDocument()
                                               : makeTextDocument());
    session->selectNode(channelKey(*session, array ? kArray : frames ? kFrame : kText));
    QVERIFY(scans->waitForScans(1));
    QCOMPARE(int(scans->requests().front().lane), lane);
    if (array) {
        QCOMPARE(session->resultBytes().reserved,
                 mdf4array::reservation(
                     mdf4array::arrayPlan(makeArrayDocument(), timeMaster, 0, kArray, 0, {})));
    }

    session.reset();
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan %1 returned").arg(array ? kArray : frames ? kFrame : kText),
                          QStringLiteral("source released")}));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(scans->requests().size(), std::size_t(1));
    QVERIFY(!scans->timedOut());
}

void TestMdf4DocumentSession::reentryFromTableNotifications_data() {
    QTest::addColumn<bool>("frames");
    QTest::addColumn<bool>("arrival");
    QTest::addColumn<bool>("pre");
    QTest::addColumn<bool>("close");
    for (const bool frames : {false, true}) {
        for (const bool arrival : {false, true}) {
            for (const bool pre : {true, false}) {
                for (const bool close : {false, true}) {
                    QTest::addRow("%s, %s, %s, %s", frames ? "frames" : "texts",
                                  arrival ? "page arrives" : "channel selected",
                                  pre ? "pre-reset" : "reset", close ? "close" : "select Engine")
                        << frames << arrival << pre << close;
                }
            }
        }
    }
}

// An observer of the table's reset selects Engine or closes the session from
// inside it, when a selection shows the text channel or the structure, or when
// a page arrives: Engine ends shown and the table empty, or the session stops
// there and nothing touches it afterwards.
void TestMdf4DocumentSession::reentryFromTableNotifications() {
    QFETCH(bool, frames);
    QFETCH(bool, arrival);
    QFETCH(bool, pre);
    QFETCH(bool, close);
    auto scans = std::make_shared<Scans>(false, 3000);
    auto session = openSession(scans, frames ? makeFrameDocument(3000) : makeTextDocument(3000));
    SignalPlotModel* plot = plotOf(*session);
    const quint64 text = channelKey(*session, frames ? kFrame : kText);
    const quint64 engine = channelKey(*session, Engine);
    session->selectNode(text);
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    if (!arrival) {
        // Away from the table, so selecting the channel again resets it.
        session->selectNode(groupKey(*session));
        QVERIFY(settle(*session));
    }

    // The selection's reset, or the arriving page's after the request's own.
    auto resets = std::make_shared<int>(0);
    const int at = arrival ? 2 : 1;
    bool closed = false;
    callOnce(table, pre ? &QAbstractItemModel::modelAboutToBeReset : &QAbstractItemModel::modelReset,
             [resets, at] { return ++*resets == at; },
             [&] {
                 if (close) {
                     session.reset();
                     closed = true;
                 } else {
                     session->selectNode(engine);
                 }
             });
    if (arrival) {
        table->showSample(2500);
    } else {
        session->selectNode(text);
    }

    if (close) {
        QTRY_VERIFY_WITH_TIMEOUT(closed, 5000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
        QCoreApplication::processEvents();
        QCOMPARE(scans->events().last(), QStringLiteral("source released"));
        // A page is one read, a frame page one per lane.
        QCOMPARE(scans->requests().size(), std::size_t((arrival ? 2 : 1) * (frames ? 4 : 1)));
    } else {
        QVERIFY(settle(*session));
        QVERIFY(session->centerPanelModel() == plot);
        QVERIFY(showsExact(plot, "EngineSpeed", 100.0));
        QCOMPARE(table->tableState(), ValueTableModel::NoChannel);
        QCOMPARE(int(scans->requests().back().lane), int(Scans::Request::Values));
    }
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// Readable, so listed like any channel rather than as unsupported; its role
// names its type and shape after the unit.
void TestMdf4DocumentSession::arrayChannelIsAnOrdinaryTreeChannel() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeArrayDocument());
    TreeModel* tree = session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    const QModelIndex row = tree->index(kArray, 0, group);
    QCOMPARE(tree->data(row, TreeModel::TitleRole).toString(), QStringLiteral("2D_classification"));
    QCOMPARE(tree->data(row, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Entity));
    QCOMPARE(tree->data(row, TreeModel::IconKeyRole).toString(), QStringLiteral("channel"));
    QCOMPARE(tree->data(row, TreeModel::SubtitleRole).toString(),
             QStringLiteral("s  ·  Classification result 52 × 142"));
}

// The center turns to the table, which shows sample 0 of the array: one read of
// its whole element window, its rows and columns on index headers, held while
// shown and charged within the read's reservation.
void TestMdf4DocumentSession::arraySampleShown() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeArrayDocument());
    SignalPlotModel* plot = plotOf(*session);
    session->selectNode(channelKey(*session, kArray));
    QCOMPARE(detailName(session->detailModel()), QStringLiteral("2D_classification"));
    QVERIFY(centerIs(*session, "ValueTableView.qml"));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(plot->name().isEmpty());
    QVERIFY(settle(*session));

    QCOMPARE(table->kind(), ValueTableModel::Array);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->name(), QStringLiteral("2D_classification"));
    QCOMPARE(table->role(), QStringLiteral("Classification result 52 × 142"));
    QCOMPARE(table->dimensions(), (QVariantList{qulonglong(52), qulonglong(142)}));
    QCOMPARE(table->cornerText(), QStringLiteral("Axis 1 \\ Axis 2"));
    QCOMPARE(table->rowCount(), 52);
    QCOMPARE(table->columnCount(), 142);
    QCOMPARE(table->data(table->index(1, 0)).toString(), QStringLiteral("142"));
    QCOMPARE(table->headerData(51, Qt::Vertical).toString(), QStringLiteral("51"));
    QCOMPARE(table->headerData(141, Qt::Horizontal).toString(), QStringLiteral("141"));
    QCOMPARE(table->timeText(), QStringLiteral("0 ms"));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(1));
    QCOMPARE(int(requests[0].lane), int(Scans::Request::Array));
    QCOMPARE(requests[0].channel, std::uint32_t(kArray));
    QCOMPARE(requests[0].first, std::uint64_t(0));
    QCOMPARE(requests[0].count, std::uint64_t(1));
    QCOMPARE(requests[0].elements.first, std::uint64_t(0));
    QCOMPARE(requests[0].elements.count, std::uint64_t(7384));

    const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(1));
    QCOMPARE(bytes.held, std::size_t(1));
    QCOMPARE(bytes.retained, table->arraySlice()->bytes());
    QVERIFY(bytes.retained <= mdf4array::reservation(mdf4array::arrayPlan(
                                  makeArrayDocument(), timeMaster, 0, kArray, 0, {})));
}

// The source holds fewer samples than it states: the sample past its end reads
// as no sample, with a note, and the request clamps to the stated samples.
void TestMdf4DocumentSession::arraySamplePastTheEnd() {
    auto scans = std::make_shared<Scans>(false);
    scans->available = 2;
    auto session = openSession(scans, makeArrayDocument());
    session->selectNode(channelKey(*session, kArray));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);

    table->showSample(10);
    QVERIFY(settle(*session));
    QCOMPARE(scans->requests().back().first, std::uint64_t(3));
    QCOMPARE(table->sample(), qulonglong(3));
    QCOMPARE(table->tableState(), ValueTableModel::Empty);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY2(table->message().startsWith(QStringLiteral("No sample 3: ")) &&
                 table->message().contains(QStringLiteral("ends after")),
             qPrintable(table->message()));
}

// An element the source marks invalid is an empty cell beside its valid
// neighbours.
void TestMdf4DocumentSession::invalidArrayElementIsAnEmptyCell() {
    auto scans = std::make_shared<Scans>(false);
    scans->invalidElements = {5};
    auto session = openSession(scans, makeArrayDocument());
    session->selectNode(channelKey(*session, kArray));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->data(table->index(0, 5)).toString(), QString());
    QCOMPARE(table->data(table->index(0, 4)).toString(), QStringLiteral("4"));
    QCOMPARE(table->data(table->index(0, 6)).toString(), QStringLiteral("6"));
}

// A 2 x 3 x 4 array shows its last two dimensions; the outer index picks the
// slice, whose element window the read asks for. A slice read once comes from
// the cache.
void TestMdf4DocumentSession::sliceSelectsTheWindow() {
    auto scans = std::make_shared<Scans>(false);
    scans->elementCount = 24;
    auto session = openSession(scans, makeArrayDocument({2, 3, 4}, 0));
    session->selectNode(channelKey(*session, kArray));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->role(), QStringLiteral("Array 2 × 3 × 4"));
    QCOMPARE(table->slice(), QVariantList{qulonglong(0)});
    QCOMPARE(table->cornerText(), QStringLiteral("Axis 2 \\ Axis 3"));
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->data(table->index(2, 3)).toString(), QStringLiteral("11"));

    table->setSlice(0, 1);
    QCOMPARE(table->tableState(), ValueTableModel::Pending);
    QVERIFY(settle(*session));
    std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(requests[0].elements.first, std::uint64_t(0));
    QCOMPARE(requests[0].elements.count, std::uint64_t(12));
    QCOMPARE(requests[1].first, std::uint64_t(0));
    QCOMPARE(requests[1].elements.first, std::uint64_t(12));
    QCOMPARE(requests[1].elements.count, std::uint64_t(12));
    QCOMPARE(table->slice(), QVariantList{qulonglong(1)});
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->data(table->index(0, 0)).toString(), QStringLiteral("12"));
    QCOMPARE(table->data(table->index(2, 3)).toString(), QStringLiteral("23"));

    table->setSlice(0, 0);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->data(table->index(0, 0)).toString(), QStringLiteral("0"));
    QVERIFY(settle(*session));
    QCOMPARE(scans->requests().size(), std::size_t(2));
}

// A slice holding more values than the reader lets one read hold is refused by
// the reader: the table says so with the reader's reason, and keeps nothing.
void TestMdf4DocumentSession::arraySliceRefusedByTheReader() {
    auto scans = std::make_shared<Scans>(false);
    scans->outcome = [](const Scans::Request& request) -> std::optional<mdf4::ScanResult> {
        if (request.lane != Scans::Request::Array) {
            return std::nullopt;
        }
        mdf4::ScanResult refused = finished(mdf4::Status::ResourceLimit,
                                            "the values exceed the read allowance",
                                            "DG[0]/CG[0]/CN[5]");
        refused.outcome.resource = mdf4::Resource::Output;
        return refused;
    };
    auto session = openSession(scans, makeArrayDocument());
    session->selectNode(channelKey(*session, kArray));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->tableState(), ValueTableModel::Refused);
    QCOMPARE(table->message(), QStringLiteral("Values exceed a reader limit: the values exceed "
                                              "the read allowance (DG[0]/CG[0]/CN[5])"));
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(session->resultBytes().results, std::size_t(0));
}

// Each axis channel is read at the sample the spec's rule pairs with the array
// sample: a one-cycle group without a master at its only sample, the array's
// own group at the same sample, a group with a time master as the array's at
// its latest sample at or before the array sample's time, and none without a
// common master. Unpaired axes show indices with a note; the values show
// regardless.
void TestMdf4DocumentSession::arrayAxesFollowTheAxisRule() {
    auto scans = std::make_shared<Scans>(false, 4);
    // Only the Speeds master is scanned: its samples at 0.5, 1.5 and 2.5 s.
    scans->sample = [](std::uint32_t, std::uint64_t index) {
        return std::make_pair(0.5 + static_cast<double>(index), 0.0);
    };
    auto session = openSession(scans, makeAxisDocument(), {}, axisDocumentAxis);
    const auto arrayReads = [&scans] {
        std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t>> reads;
        for (const Scans::Request& request : scans->requests()) {
            if (request.lane == Scans::Request::Array) {
                reads.emplace_back(request.group, request.channel, request.first,
                                   request.elements.count);
            }
        }
        return reads;
    };
    using Read = std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t>;

    // Map: Classes' left-open intervals at sample 0 for samples 0 and 3; Local
    // at the array's sample.
    session->selectNode(channelKey(*session, kMap, kArrays));
    ValueTableModel* table = tableOf(*session);
    QVERIFY(table);
    QVERIFY(settle(*session));
    QCOMPARE(table->cornerText(), QStringLiteral("Classes \\ Local"));
    QCOMPARE(table->headerData(0, Qt::Vertical).toString(), QStringLiteral("]0, 1]"));
    QCOMPARE(table->headerData(2, Qt::Vertical).toString(), QStringLiteral("]2, 3]"));
    QCOMPARE(table->headerData(1, Qt::Horizontal).toString(), QStringLiteral("1"));
    table->showSample(3);
    QVERIFY(settle(*session));
    QCOMPARE(table->headerData(0, Qt::Vertical).toString(), QStringLiteral("]0, 1]"));
    QCOMPARE(table->headerData(1, Qt::Horizontal).toString(), QStringLiteral("3001"));
    QCOMPARE(arrayReads(), (std::vector<Read>{{kArrays, kMap, 0, 6},
                                              {kClasses, 0, 0, 4},
                                              {kArrays, kLocal, 0, 2},
                                              {kArrays, kMap, 3, 6},
                                              {kClasses, 0, 0, 4},
                                              {kArrays, kLocal, 3, 2}}));

    // Curve: Speed at the latest sample at or before the array sample's time,
    // none before the first; Count has no common master.
    session->selectNode(channelKey(*session, kCurve, kArrays));
    QVERIFY(settle(*session));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->arraySlice()->rowAxis.kind, TableAxis::Kind::Index);
    QVERIFY2(table->message().contains(QStringLiteral("no Speed sample at or before this one")) &&
                 table->message().contains(QStringLiteral("no common master with Count")),
             qPrintable(table->message()));
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->columnCount(), 4);
    for (const auto& [sample, axisSample] : {std::pair<std::uint64_t, std::uint64_t>{1, 0},
                                            std::pair<std::uint64_t, std::uint64_t>{3, 2}}) {
        table->showSample(static_cast<double>(sample));
        QVERIFY(settle(*session));
        QCOMPARE(table->arraySlice()->rowAxis.kind, TableAxis::Kind::Points);
        QCOMPARE(arrayReads().back(), (Read{kSpeeds, 1, axisSample, 3}));
        QCOMPARE(table->headerData(0, Qt::Vertical).toString(),
                 QString::number(1000 * axisSample));
        QVERIFY2(!table->message().contains(QStringLiteral("Speed")), qPrintable(table->message()));
    }
    std::vector<Scans::Request> masterScans;
    for (const Scans::Request& request : scans->requests()) {
        if (request.lane == Scans::Request::Values) {
            masterScans.push_back(request);
        }
    }
    QCOMPARE(masterScans.size(), std::size_t(3));
    for (const Scans::Request& request : masterScans) {
        QCOMPARE(request.group, std::uint32_t(kSpeeds));
        QCOMPARE(request.channel, std::uint32_t(0));
        QCOMPARE(request.first, std::uint64_t(0));
        QCOMPARE(request.count, std::uint64_t(3));
    }
    QCOMPARE(scans->maxConcurrent(), 1);
}

// One coordinate going back makes the whole result set, overview and window,
// plot against sample indices.
void TestMdf4DocumentSession::regressingTimePlotsSampleIndices() {
    auto scans = std::make_shared<Scans>(false);
    scans->sample = [](std::uint32_t, std::uint64_t index) {
        return std::make_pair(index == 2 ? 0.5 : static_cast<double>(index),
                              10.0 + static_cast<double>(index));
    };
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));

    QCOMPARE(model->domainName(), QStringLiteral("Sample index"));
    QVERIFY(model->domainUnit().isEmpty());
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QCOMPARE(model->window()->time, (std::vector<double>{0.0, 1.0, 2.0, 3.0}));
    QCOMPARE(model->window()->value[2], 12.0);
}

// A -> B -> C while A scans: A is cancelled, C replaces B as the pending scan,
// so the source sees A, then C, one at a time, and B never.
void TestMdf4DocumentSession::supersededScansAreCancelled() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Vehicle));
    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(model->busy());
    QCOMPARE(model->name(), QStringLiteral("CoolantTemp"));
    QCOMPARE(model->plotState(), SignalPlotModel::Pending);

    // A sees its cancellation; its completion starts C, then C's window.
    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "CoolantTemp", 300.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Coolant, Coolant}));
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// A -> B -> A: A's scan was cancelled when B came, so A waits as the pending
// scan and runs again once the cancelled one has returned.
void TestMdf4DocumentSession::reselectingAfterCancelScansAgain() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Vehicle));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(model->busy());
    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Engine}));
    QVERIFY(!scans->timedOut());
}

// Selecting the channel whose overview is being scanned keeps that scan.
void TestMdf4DocumentSession::reselectingTheRunningChannelKeepsItsScan() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(model->busy());
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine}));
}

// A source that finishes the scan although the session cancelled it: the
// result neither reaches the plot nor the cache, so the channel is scanned
// again when it is selected again.
void TestMdf4DocumentSession::cancelledWorkIsNeitherShownNorKept() {
    auto scans = std::make_shared<Scans>();
    scans->honorCancel = false;
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Master));
    scans->release();
    QVERIFY(settle(*session));
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QVERIFY(!model->hasSamples());
    QVERIFY(!model->busy());
    QCOMPARE(session->resultBytes().results, std::size_t(0));
    QCOMPARE(session->resultBytes().retained, std::uint64_t(0));

    scans->openGate();
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Engine}));
}

// A cached channel, a non-channel row, the master and an unsupported channel
// each settle the view without a scan: the pending scan is dropped and the one
// in flight cancelled.
void TestMdf4DocumentSession::settledSelectionDropsPendingAndCancels() {
    enum class Settle { Cached, GroupRow, Master, Unsupported };
    for (Settle kind : {Settle::Cached, Settle::GroupRow, Settle::Master, Settle::Unsupported}) {
        auto scans = std::make_shared<Scans>();
        auto session = openSession(scans);
        SignalPlotModel* model = plotOf(*session);
        std::vector<std::uint32_t> expected;
        if (kind == Settle::Cached) {
            scans->openGate();
            session->selectNode(channelKey(*session, Vehicle));
            QVERIFY(settle(*session));
            scans->closeGate();
            expected = {Vehicle, Vehicle};
        }

        session->selectNode(channelKey(*session, Engine));
        QVERIFY(scans->waitForScans(int(expected.size()) + 1));
        session->selectNode(channelKey(*session, Coolant));
        switch (kind) {
        case Settle::Cached:
            session->selectNode(channelKey(*session, Vehicle));
            break;
        case Settle::GroupRow:
            session->selectNode(groupKey(*session));
            break;
        case Settle::Master:
            session->selectNode(channelKey(*session, Master));
            break;
        case Settle::Unsupported:
            session->selectNode(channelKey(*session, Unsupported));
            break;
        }
        QVERIFY(!model->busy());
        QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
        QVERIFY(settle(*session));
        expected.push_back(Engine);
        QCOMPARE(scans->channels(), expected);
        QVERIFY(!model->busy());
        if (kind == Settle::Cached) {
            QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
        } else {
            QVERIFY(!model->hasSamples());
        }
        QVERIFY(!scans->timedOut());
    }
}

// Each way a scan can end reads differently: a failure explains itself and is
// scanned again next time, a changed file asks to be reopened, a refusal names
// the limit, a short source says how much of it arrived, and an empty success
// says there are no samples and is kept like any other result.
void TestMdf4DocumentSession::outcomesStayDistinct() {
    auto scans = std::make_shared<Scans>(false);
    scans->outcome = [](const Scans::Request& request) -> std::optional<mdf4::ScanResult> {
        switch (request.channel) {
        case Engine:
            return finished(mdf4::Status::Error, "block cannot be read", "DG[0]/CG[0]/CN[0]");
        case Vehicle:
            return finished(mdf4::Status::SourceChanged, "source file changed since it was opened");
        case Coolant: {
            mdf4::ScanResult refused = finished(mdf4::Status::ResourceLimit,
                                                "fragment exceeds the scratch allowance",
                                                "DG[0]/CG[0]/CN[2]");
            refused.outcome.resource = mdf4::Resource::Scratch;
            refused.outcome.required = std::uint64_t{80} << 20;
            refused.outcome.limit = std::uint64_t{64} << 20;
            return refused;
        }
        default:
            return std::nullopt;
        }
    };
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Failed);
    QCOMPARE(model->name(), QStringLiteral("EngineSpeed"));
    QCOMPARE(model->message(),
             QStringLiteral("Samples could not be read: block cannot be read (DG[0]/CG[0]/CN[0])"));

    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Failed);
    QCOMPARE(model->message(), QStringLiteral("Samples could not be read: the file changed since "
                                              "it was opened; reopen it"));

    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Refused);
    QCOMPARE(model->message(),
             QStringLiteral("Samples need more working memory than the decoder allows: 80 MiB "
                            "for one step, 64 MiB allowed (DG[0]/CG[0]/CN[2])"));

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Failed);
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Vehicle, Coolant, Engine}));
    QCOMPARE(session->resultBytes().results, std::size_t(0));

    auto shortScans = std::make_shared<Scans>(false);
    shortScans->available = 2;
    auto shortSession = openSession(shortScans);
    SignalPlotModel* shortModel = plotOf(*shortSession);
    shortSession->selectNode(channelKey(*shortSession, Engine));
    QVERIFY(settle(*shortSession));
    QVERIFY(shortModel->incomplete());
    QCOMPARE(shortModel->countText(), QStringLiteral("2 of 4 samples"));
    QCOMPARE(shortModel->fullEnd(), 1.0);
    QCOMPARE(shortModel->plotState(), SignalPlotModel::Detail);

    auto emptyScans = std::make_shared<Scans>(false, 0);
    auto emptySession = openSession(emptyScans, makeDocument(0));
    SignalPlotModel* emptyModel = plotOf(*emptySession);
    emptySession->selectNode(channelKey(*emptySession, Engine));
    QVERIFY(settle(*emptySession));
    QCOMPARE(emptyModel->plotState(), SignalPlotModel::Empty);
    QCOMPARE(emptyModel->message(), QStringLiteral("No samples recorded"));
    emptySession->selectNode(channelKey(*emptySession, Vehicle));
    QVERIFY(settle(*emptySession));
    emptySession->selectNode(channelKey(*emptySession, Engine));
    QCOMPARE(emptyModel->message(), QStringLiteral("No samples recorded"));
    QVERIFY(settle(*emptySession));
    QCOMPARE(emptyScans->channels(), (std::vector<std::uint32_t>{Engine, Vehicle}));
}

// Four million samples do not fit one window, so the whole view stays an
// overview; zoomed in, the session scans exactly the cover the plot names and
// installs the window for the view.
void TestMdf4DocumentSession::zoomScansTheWindowTheViewNeeds() {
    auto scans = std::make_shared<Scans>(false, kDense);
    scans->chunk = 65536;
    auto session = openSession(scans, makeDocument(kDense));
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);
    QCOMPARE(scans->channels().size(), std::size_t(1));
    QVERIFY(model->message().startsWith(QStringLiteral("More than 4,194,304 samples in view")));

    model->setVisibleRange(1'000'000.0, 1'001'000.0);
    const std::optional<PlotWindowRequest> wanted = model->detailRequest();
    QVERIFY(wanted);
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(requests[1].first, wanted->firstSample);
    QCOMPARE(requests[1].count, wanted->sampleCount);
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QCOMPARE(model->viewStart(), 1'000'000.0);
    QCOMPARE(model->viewEnd(), 1'001'000.0);
    QVERIFY(model->window()->covers(1'000'000.0, 1'001'000.0));
    const std::size_t first = static_cast<std::size_t>(1'000'000 - model->window()->firstSample);
    QCOMPARE(model->window()->value[first], 100.0 + 1'000'000.0);

    // Within the window's reach the view needs no other scan.
    model->setVisibleRange(1'000'200.0, 1'000'400.0);
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QVERIFY(settle(*session));
    QCOMPARE(scans->channels().size(), std::size_t(2));
}

// Zooming three times while the first window scans: that scan is cancelled, the
// second view's window is never scanned, and only the last view's window is
// installed.
void TestMdf4DocumentSession::rapidZoomCancelsObsoleteWindows() {
    auto scans = std::make_shared<Scans>(true, kDense);
    scans->chunk = 65536;
    auto session = openSession(scans, makeDocument(kDense));
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    scans->release();
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);

    model->setVisibleRange(1'000'000.0, 1'001'000.0);
    QVERIFY(scans->waitForScans(2));
    QVERIFY(model->busy());
    QCOMPARE(model->message(), QStringLiteral("Reading exact samples…"));
    model->setVisibleRange(2'000'000.0, 2'001'000.0);
    model->setVisibleRange(3'000'000.0, 3'001'000.0);
    const std::optional<PlotWindowRequest> last = model->detailRequest();
    QVERIFY(last);
    QVERIFY(model->busy());

    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(3), 5000);
    scans->release();
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(3));
    QVERIFY(requests[1].first < 1'000'000 && requests[1].first + requests[1].count > 1'001'000);
    QCOMPARE(requests[2].first, last->firstSample);
    QCOMPARE(requests[2].count, last->sampleCount);
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QVERIFY(model->window()->covers(3'000'000.0, 3'001'000.0));
    QVERIFY(!model->busy());
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// Progress belongs to the scan in flight for the selection: once that scan is
// cancelled, what it still reports never reaches the plot.
void TestMdf4DocumentSession::staleProgressNeverReachesThePlot() {
    auto scans = std::make_shared<Scans>();
    scans->progressBeforeGate = true;
    scans->progressAfterCancel = true;
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    std::vector<double> shown;
    QObject::connect(model, &SignalPlotModel::busyChanged, model,
                     [&] { shown.push_back(model->progress()); });

    session->selectNode(channelKey(*session, Engine));
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    session->selectNode(channelKey(*session, Vehicle));
    QCOMPARE(model->progress(), -1.0);
    QVERIFY(model->busy());

    // Engine reports three quarters after its cancellation, then returns;
    // Vehicle's scan starts and reports its own quarter.
    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    QVERIFY(std::find(shown.begin(), shown.end(), 0.75) == shown.end());
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    QCOMPARE(model->progress(), -1.0);
}

// A source reporting progress two thousand times a second, for over a second,
// reaches the plot at most once per 100 ms: the worker coalesces it. Without
// that, the progress callouts alone arrive about twice as often.
void TestMdf4DocumentSession::progressIsCoalesced() {
    constexpr std::uint64_t samples = 120 * 512;
    auto scans = std::make_shared<Scans>(false, samples);
    scans->chunk = 512;
    scans->progressBurst = 20;
    scans->chunkTime = 10ms;
    auto session = openSession(scans, makeDocument(samples));
    SignalPlotModel* model = plotOf(*session);
    // The overview scan's progress; the window scan's follows the overview.
    std::vector<double> shown;
    QObject::connect(model, &SignalPlotModel::busyChanged, model, [&] {
        if (!model->hasSamples() && model->progress() >= 0.0) {
            shown.push_back(model->progress());
        }
    });

    QElapsedTimer timer;
    timer.start();
    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(model->hasSamples(), 10000);
    const qint64 elapsed = timer.elapsed();
    const std::size_t updates = shown.size();
    QVERIFY2(updates >= 2 && updates <= std::size_t(elapsed / 100 + 2),
             qPrintable(QStringLiteral("%1 progress updates in %2 ms").arg(updates).arg(elapsed)));
    QVERIFY(std::is_sorted(shown.begin(), shown.end()));
    QVERIFY(settle(*session));
}

// Admission reserves a scan's bytes before it launches; its completion turns
// the reservation into the result's own charge in one step, never both, never
// neither, and the sum stays within the allowance throughout.
void TestMdf4DocumentSession::reservationBecomesTheResultChargeWithoutGap() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    const std::uint64_t allowance = Mdf4SessionLimits{}.resultBytes;
    const auto within = [&] {
        const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
        return bytes.retained + bytes.reserved <= allowance;
    };

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, PlotOverviewBuilder::reservation(4));
    QCOMPARE(bytes.retained, std::uint64_t(0));
    QVERIFY(within());

    scans->release();
    QTRY_VERIFY_WITH_TIMEOUT(plotOf(*session)->hasSamples(), 5000);
    QVERIFY(scans->waitForScans(2));
    const PlotOverviewPtr overview = plotOf(*session)->overview();
    const PlotWindowRequest wanted = overview->windowRequest(0.0, 3.0);
    bytes = session->resultBytes();
    QCOMPARE(bytes.retained, overview->bytes());
    QCOMPARE(bytes.reserved, PlotWindowBuilder::reservation(wanted));
    QCOMPARE(overview->bytes() + sizeof(PlotOverviewBuilder), PlotOverviewBuilder::reservation(4));
    QVERIFY(within());

    scans->release();
    QVERIFY(settle(*session));
    bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, std::uint64_t(0));
    QCOMPARE(bytes.retained, overview->bytes() + plotOf(*session)->window()->bytes());
    QCOMPARE(bytes.results, std::size_t(2));
    QCOMPARE(bytes.held, std::size_t(2));
}

void TestMdf4DocumentSession::obsoleteResultReleasedBeforeTheNextAdmission_data() {
    QTest::addColumn<bool>("window");
    QTest::addColumn<bool>("refused");
    QTest::newRow("overview, next scan pending") << false << false;
    QTest::newRow("overview, next scan asked by an observer") << false << true;
    QTest::newRow("window, next scan pending") << true << false;
    QTest::newRow("window, next scan asked by an observer") << true << true;
}

// A scan that finished while no event loop ran is made obsolete by a new
// selection. Its result, an overview's bins or a window's arrays, goes before
// anything else is admitted or announced, and so does its request's hold on
// the overview of its result set: when the session starts the pending scan
// itself, and when it refuses the pending scan and an observer of that refusal
// at once asks for another channel. The test looks at the heap and the
// allowance from inside the completion's handling, once that next scan has
// started.
void TestMdf4DocumentSession::obsoleteResultReleasedBeforeTheNextAdmission() {
    QFETCH(bool, window);
    QFETCH(bool, refused);
    // Engine's result arrays have sizes no other block here shares. Refused,
    // Vehicle's overview needs more than the whole allowance. Otherwise the
    // allowance holds an overview with its window but never two overviews:
    // after Engine's window, Vehicle's overview of the same size gets in only
    // by evicting Engine's, which nothing may hold any more.
    constexpr std::uint64_t kEngine = 3000;
    constexpr std::uint64_t kWide = 100'000;
    const std::uint64_t vehicle = refused ? kWide : window ? kEngine : 4;
    auto scans = std::make_shared<Scans>(true, kEngine);
    scans->chunk = 1000;
    mdf4::File document = makeDocument();
    document.mutable_groups(0)->mutable_channels(Engine)->set_sample_count(kEngine);
    document.mutable_groups(0)->mutable_channels(Vehicle)->set_sample_count(vehicle);
    const Mdf4SessionLimits limits{Mdf4SessionLimits{}.treeBytes,
                                   refused ? PlotOverviewBuilder::reservation(kWide) - 1
                                           : PlotOverviewBuilder::reservation(kEngine) * 3 / 2};
    auto session = openSession(scans, std::move(document), limits);
    SignalPlotModel* model = plotOf(*session);

    // An overview's builder reserves its bins when it starts; a window's
    // allocates its arrays at its first sample, past the gate.
    if (!window) {
        heap::watch(binBytes(kEngine));
    }
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    if (window) {
        scans->release();
        QTRY_VERIFY_WITH_TIMEOUT(model->hasSamples(), 5000);
        QVERIFY(scans->waitForScans(2));
        const std::optional<PlotWindowRequest> request = model->detailRequest();
        QVERIFY(request);
        const plotscan::Samples engine = [&scans](std::uint64_t index) {
            return scans->sample(Engine, index);
        };
        heap::watch(plotscan::windowOf(*request, engine)->time.capacity() * sizeof(double));
    }
    scans->release();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCOMPARE(heap::live(), window ? 2 : 1);

    session->selectNode(channelKey(*session, Vehicle));
    // The next scan: Vehicle's, or Coolant's that the observer asks for.
    const int next = window ? 3 : 2;
    const std::uint64_t reservation = PlotOverviewBuilder::reservation(refused ? 4 : vehicle);
    int live = -1;
    bool started = false;
    Mdf4DocumentSession::ResultBytes bytes;
    const auto look = [&] {
        bytes = session->resultBytes();
        started = bytes.reserved > 0 && scans->waitForScans(next);
        live = heap::live();
    };
    if (refused) {
        const quint64 coolant = channelKey(*session, Coolant);
        callOnce(model, &SignalPlotModel::stateChanged,
                 [model] { return model->plotState() == SignalPlotModel::Refused; },
                 [&session, coolant, look] {
                     session->selectNode(coolant);
                     look();
                 });
    } else {
        // Progress on show makes the session announce its busy state after it
        // started the pending scan.
        model->setProgress(0.5);
        callOnce(model, &SignalPlotModel::busyChanged, [model] { return model->progress() < 0.0; },
                 look);
    }
    QTRY_VERIFY_WITH_TIMEOUT(live >= 0, 5000);
    heap::watch(0);
    QVERIFY2(live == 0 && started && bytes.reserved == reservation,
             qPrintable(QStringLiteral("%1 blocks of the discarded result live; next scan %2; "
                                       "%3 bytes reserved, %4 expected")
                            .arg(live)
                            .arg(started ? QStringLiteral("started") : QStringLiteral("not started"))
                            .arg(bytes.reserved)
                            .arg(reservation)));

    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(refused ? showsExact(model, "CoolantTemp", 300.0)
                    : showsExact(model, "VehicleSpeed", 200.0));
    QVERIFY(!scans->timedOut());
}

// Past the allowance the least recently used results nobody else holds go
// first; a window goes before the overview of its result set.
void TestMdf4DocumentSession::unheldResultsEvictedLeastRecentlyUsed() {
    auto probeScans = std::make_shared<Scans>(false);
    auto probe = openSession(probeScans);
    probe->selectNode(channelKey(*probe, Engine));
    QVERIFY(settle(*probe));
    const std::uint64_t channel = probe->resultBytes().retained;

    // Room for two channels and a third's overview, not its window.
    const std::uint64_t allowance = 2 * channel + PlotOverviewBuilder::reservation(4) + 8;
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeDocument(), Mdf4SessionLimits{Mdf4SessionLimits{}.treeBytes, allowance});
    SignalPlotModel* model = plotOf(*session);
    for (int channelIndex : {Engine, Vehicle, Coolant}) {
        session->selectNode(channelKey(*session, channelIndex));
        QVERIFY(settle(*session));
        const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
        QVERIFY(bytes.retained + bytes.reserved <= allowance);
    }
    QVERIFY(showsExact(model, "CoolantTemp", 300.0));
    QCOMPARE(session->resultBytes().results, std::size_t(4));
    QCOMPARE(session->resultBytes().retained, 2 * channel);

    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Vehicle, Vehicle,
                                                            Coolant, Coolant, Engine, Engine}));
}

// Results someone else still holds are never evicted: an old window kept alive
// by another owner, with its overview, plus the shown overview leave no room
// for the replacement window's reservation, which is refused with the numbers.
// Once the old owner lets go, the same request is admitted.
void TestMdf4DocumentSession::heldResultsRefuseAdmission() {
    auto probeScans = std::make_shared<Scans>(false);
    auto probe = openSession(probeScans);
    probe->selectNode(channelKey(*probe, Engine));
    QVERIFY(settle(*probe));
    const std::uint64_t channel = probe->resultBytes().retained;
    const std::uint64_t overviewBytes = probe->resultBytes().retained - plotOf(*probe)->window()->bytes();

    // Engine's results and Vehicle's overview fit; Vehicle's window does not.
    const std::uint64_t allowance = channel + overviewBytes + 64;
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeDocument(), Mdf4SessionLimits{Mdf4SessionLimits{}.treeBytes, allowance});
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    PlotWindowPtr old = model->window();

    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);
    QVERIFY2(model->message().startsWith(QStringLiteral("Exact samples need ")) &&
                 model->message().contains(QStringLiteral("but results in use hold")),
             qPrintable(model->message()));
    Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(3));
    QCOMPARE(bytes.held, std::size_t(3));
    QCOMPARE(bytes.reserved, std::uint64_t(0));
    QVERIFY(bytes.retained <= allowance);

    old.reset();
    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(2));
    QVERIFY(bytes.retained <= allowance);
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Vehicle, Vehicle}));
}

// The open worker builds the session and hands it to the controller thread;
// the watcher receiving completions moves with the models, or no completion
// would ever be delivered.
void TestMdf4DocumentSession::sessionFromWorkerThreadDeliversOnOwnerThread() {
    auto scans = std::make_shared<Scans>(false);
    std::unique_ptr<Mdf4DocumentSession> session;
    QThread* owner = QThread::currentThread();
    std::thread worker([&] {
        session = openSession(scans);
        session->moveModelsToThread(owner);
    });
    worker.join();
    SignalPlotModel* model = plotOf(*session);
    QCOMPARE(model->thread(), owner);

    session->selectNode(channelKey(*session, Coolant));
    QTRY_VERIFY_WITH_TIMEOUT(showsExact(model, "CoolantTemp", 300.0), 5000);
}

// Closing with a scan in flight and another pending: the running scan is
// cancelled, the pending one never starts, and destruction returns only after
// the running scan did. The source stays alive until that scan's task has let
// go of it.
void TestMdf4DocumentSession::closeCancelsScanInFlightAndDropsPending() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Coolant));

    session.reset();
    QVERIFY(scans->events().contains(QStringLiteral("scan 0 returned")));
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan 0 returned"), QStringLiteral("source released")}));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(scans->channels(), std::vector<std::uint32_t>{Engine});
    QVERIFY(!scans->timedOut());
}

// A scan that finished while no event loop ran is discarded by destruction;
// its queued completion is never delivered to the closed session.
void TestMdf4DocumentSession::closeDiscardsFinishedUndeliveredScan() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    scans->release();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));

    session.reset();
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QCoreApplication::processEvents();
    QCOMPARE(scans->channels(), std::vector<std::uint32_t>{Engine});
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan 0 returned"), QStringLiteral("source released")}));
    QVERIFY(!scans->timedOut());
}

// An observer of the installed overview selects another uncached channel from
// inside the notification. The completion already settled its own result, so
// the new scan gets its own, and the plot stays busy for it.
void TestMdf4DocumentSession::selectionFromCompletionNotification() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    Mdf4DocumentSession* raw = session.get();
    const quint64 coolant = channelKey(*session, Coolant);
    callOnce(model, &SignalPlotModel::contentChanged,
             [model] { return model->hasSamples() && model->name() == QStringLiteral("EngineSpeed"); },
             [raw, coolant] { raw->selectNode(coolant); });

    session->selectNode(channelKey(*session, Engine));
    scans->release();
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    QCOMPARE(model->name(), QStringLiteral("CoolantTemp"));
    QVERIFY(model->busy());
    QVERIFY(!model->hasSamples());

    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "CoolantTemp", 300.0));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Coolant, Coolant, Engine}));
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// The exact samples settle the plot, and its progress resets. An observer of
// that busy notification selects another uncached channel from inside it: the
// new scan starts and waits at the gate, and the plot stays busy for it.
void TestMdf4DocumentSession::selectionFromBusyNotificationStaysBusy() {
    auto scans = std::make_shared<Scans>();
    scans->progressBeforeGate = true;
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    Mdf4DocumentSession* raw = session.get();
    const quint64 vehicle = channelKey(*session, Vehicle);
    callOnce(model, &SignalPlotModel::busyChanged,
             [model] {
                 return model->plotState() == SignalPlotModel::Detail && model->progress() < 0.0;
             },
             [raw, vehicle] { raw->selectNode(vehicle); });

    session->selectNode(channelKey(*session, Engine));
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    scans->release();
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    scans->release();
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(3), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model->name(), QStringLiteral("VehicleSpeed"), 5000);
    QCOMPARE(model->plotState(), SignalPlotModel::Pending);
    QVERIFY(model->busy());

    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Vehicle, Vehicle}));
    QVERIFY(!scans->timedOut());
}

// The completion's final notification is the busy state. Its observer selects
// another channel, which starts a scan, then closes the session: destruction
// from inside the notification cancels and waits for that scan, and nothing
// touches the session afterwards.
void TestMdf4DocumentSession::closeFromCompletionNotification() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    Releaser releaser(scans);
    const quint64 coolant = channelKey(*session, Coolant);
    bool closed = false;
    callOnce(model, &SignalPlotModel::busyChanged,
             [model] { return !model->busy() && model->plotState() == SignalPlotModel::Detail; },
             [&] {
                 session->selectNode(coolant);
                 scans->startTeardown();
                 session.reset();
                 closed = true;
             });

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    scans->release();
    QTRY_VERIFY_WITH_TIMEOUT(scans->channels().size() == 2, 5000);
    scans->release();
    QTRY_VERIFY_WITH_TIMEOUT(closed, 5000);
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Coolant}));
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan 0 returned"), QStringLiteral("scan 0 returned"),
                          QStringLiteral("scan 2 returned"), QStringLiteral("source released")}));
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

void TestMdf4DocumentSession::selectionFromSelectionNotification_data() {
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("then");
    QTest::addColumn<int>("hook");
    for (const int hook : {DetailAboutToReset, DetailReset, DetailRawForm, PlotContent}) {
        const char* name = hookName(hook);
        QTest::addRow("channel then channel, %s", name) << int(Engine) << int(Coolant) << hook;
        QTest::addRow("channel then group, %s", name) << int(Engine) << kGroupRow << hook;
        QTest::addRow("group then channel, %s", name) << kGroupRow << int(Coolant) << hook;
        QTest::addRow("master then channel, %s", name) << int(Master) << int(Coolant) << hook;
    }
}

// An observer selects another row from inside one of a selection's
// notifications: the detail panel and the plot both end on that newer row, and
// the superseded selection scans nothing.
void TestMdf4DocumentSession::selectionFromSelectionNotification() {
    QFETCH(int, first);
    QFETCH(int, then);
    QFETCH(int, hook);
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    DetailModel* detail = session->detailModel();
    Mdf4DocumentSession* raw = session.get();
    const quint64 newer = rowKey(*session, then);
    callOnSelection(*session, hook, [raw, newer] { raw->selectNode(newer); });

    session->selectNode(rowKey(*session, first));
    QVERIFY(settle(*session));
    if (then == kGroupRow) {
        QCOMPARE(detailName(detail), QStringLiteral("Powertrain"));
        QVERIFY(model->name().isEmpty());
        QVERIFY(!model->busy());
        QVERIFY(scans->channels().empty());
    } else {
        QCOMPARE(detailName(detail), QStringLiteral("CoolantTemp"));
        QVERIFY(showsExact(model, "CoolantTemp", 300.0));
        QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Coolant, Coolant}));
    }
    QVERIFY(!scans->timedOut());
}

void TestMdf4DocumentSession::closeFromSelectionNotification_data() {
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("hook");
    for (const int hook : {DetailAboutToReset, DetailReset, DetailRawForm, PlotContent}) {
        const char* name = hookName(hook);
        QTest::addRow("channel, %s", name) << int(Engine) << hook;
        QTest::addRow("group, %s", name) << kGroupRow << hook;
    }
}

// An observer closes the session from inside one of a selection's
// notifications: the selection stops there, scans nothing and touches neither
// the destroyed session nor its models.
void TestMdf4DocumentSession::closeFromSelectionNotification() {
    QFETCH(int, row);
    QFETCH(int, hook);
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    bool closed = false;
    callOnSelection(*session, hook, [&] {
        session.reset();
        closed = true;
    });

    session->selectNode(rowKey(*session, row));
    QVERIFY(closed);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QVERIFY(scans->channels().empty());
    QCOMPARE(scans->events(), QStringList{QStringLiteral("source released")});
}

// A tab closed while its channel scans: the close notifies while the session
// is alive; then the session cancels and waits for the scan and is destroyed,
// and the finished scan reaches nobody.
void TestMdf4DocumentSession::closingTabWithScanInFlight() {
    auto scans = std::make_shared<Scans>();
    FormatList formats;
    formats.push_back({FormatId::MDF4, {QStringLiteral("mf4")}, std::make_unique<SessionAdapter>(scans)});
    AppController controller(std::move(formats));
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    controller.openFile(QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("test.mf4"))));
    QVERIFY(loaded.wait(10000));
    auto* session = static_cast<Mdf4DocumentSession*>(controller.tabModel()->tabAt(0)->session());
    QObject::connect(plotOf(*session), &QObject::destroyed, &controller,
                     [scans] { scans->note(QStringLiteral("plot destroyed")); });
    QObject::connect(&controller, &AppController::currentSessionChanged, &controller,
                     [scans] { scans->note(QStringLiteral("tab closed")); });

    controller.selectCurrentNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    controller.closeTab(0);

    const QStringList events = scans->events();
    const qsizetype closed = events.indexOf(QStringLiteral("tab closed"));
    const qsizetype returned = events.indexOf(QStringLiteral("scan %1 returned").arg(Engine));
    const qsizetype destroyed = events.indexOf(QStringLiteral("plot destroyed"));
    QVERIFY(closed >= 0 && returned >= 0);
    QVERIFY(closed < destroyed);
    QVERIFY(returned < destroyed);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(controller.tabModel()->rowCount(), 0);
    QCOMPARE(scans->channels(), std::vector<std::uint32_t>{Engine});
    QVERIFY(!scans->timedOut());
}

// The estimate is the admission: a tree needing exactly the allowance is built,
// one byte less refuses it.
void TestMdf4DocumentSession::treeAdmittedUpToItsAllowance() {
    auto scans = std::make_shared<Scans>(false);
    const std::uint64_t required = Mdf4DocumentSession::treeBytes(makeDocument());
    auto admitted = openSession(scans, makeDocument(), Mdf4SessionLimits{required});
    TreeModel* tree = admitted->treeModel();
    QCOMPARE(tree->rowCount(tree->index(0, 0)), 2);
    QCOMPARE(tree->rowCount(tree->index(0, 0, tree->index(0, 0))), 5);
    QVERIFY(!admitted->hasDiagnostics());

    auto refused = openSession(scans, makeDocument(), Mdf4SessionLimits{required - 1});
    QCOMPARE(refused->treeModel()->rowCount(refused->treeModel()->index(0, 0)), 0);
    QVERIFY(refused->hasDiagnostics());
}

// Past the allowance the session still opens: the file row with its details and
// one error saying why no group or channel is listed. No row stands for part of
// the tree.
void TestMdf4DocumentSession::treeOverAllowanceShowsOnlyTheFileRow() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeDocument(), Mdf4SessionLimits{1024});
    TreeModel* tree = session->treeModel();
    QCOMPARE(tree->rowCount(), 1);
    const QModelIndex file = tree->index(0, 0);
    QCOMPARE(tree->rowCount(file), 0);
    QCOMPARE(tree->data(file, TreeModel::TitleRole).toString(), QStringLiteral("test.mf4"));
    QCOMPARE(tree->data(file, TreeModel::SubtitleRole).toString(),
             QStringLiteral("MDF 4.20  ·  channel tree not shown"));

    const QList<DiagnosticMessage> diagnostics = session->diagnostics();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.front().severity, DiagnosticSeverity::Error);
    QCOMPARE(diagnostics.front().title, QStringLiteral("Channel tree not shown"));
    QCOMPARE(diagnostics.front().detail,
             QStringLiteral("Listing 6 channels in 2 channel groups needs about 4 KiB; the "
                            "navigation tree's allowance is 1 KiB."));

    session->selectNode(tree->data(file, TreeModel::NodeKeyRole).toULongLong());
    QVERIFY(session->detailModel()->rowCount() > 0);
    QVERIFY(!plotOf(*session)->busy());
    QVERIFY(settle(*session));
    QVERIFY(scans->channels().empty());
}

QTEST_MAIN(TestMdf4DocumentSession)
#include "tst_mdf4documentsession.moc"
