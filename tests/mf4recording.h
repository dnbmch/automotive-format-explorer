#pragma once

// Writes a sorted MDF 4.20 recording for tests: one data group, one channel
// group, row records of little-endian doubles, the time master "time" in
// seconds followed by the signals, in a single ##DT block. Streamed, so the
// sample count is bounded only by the disk. Block layouts follow ASAM MDF 4.2
// (the IDBLOCK, then 24-byte block headers with their links and data; every
// block starts 8-byte aligned).

#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <algorithm>
#include <vector>

namespace mf4recording {

struct Signal {
    std::string name;
    std::string unit;
};
// The time of a record, and a signal's value in it.
using Time = std::function<double(std::uint64_t index)>;
using Value = std::function<double(std::size_t signal, std::uint64_t index)>;

inline void put(std::vector<char>& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

inline void putDouble(std::vector<char>& out, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put(out, bits, 8);
}

// Appends a block at the next 8-byte boundary and returns its offset.
inline std::uint64_t block(std::vector<char>& out, const char* id,
                           const std::vector<std::uint64_t>& links, const std::vector<char>& data,
                           std::uint64_t extraData = 0) {
    while (out.size() % 8 != 0) {
        out.push_back(0);
    }
    const std::uint64_t at = out.size();
    out.insert(out.end(), id, id + 4);
    put(out, 0, 4);
    put(out, 24 + 8 * links.size() + data.size() + extraData, 8);
    put(out, links.size(), 8);
    for (std::uint64_t link : links) {
        put(out, link, 8);
    }
    out.insert(out.end(), data.begin(), data.end());
    return at;
}

inline std::uint64_t text(std::vector<char>& out, const std::string& value) {
    std::vector<char> data(value.begin(), value.end());
    data.push_back(0);
    return block(out, "##TX", {}, data);
}

// A float64 channel at byteOffset; the master is cn_type 2 with time sync.
inline std::vector<char> channelData(bool master, std::uint32_t byteOffset) {
    std::vector<char> data;
    put(data, master ? 2 : 0, 1);  // cn_type
    put(data, master ? 1 : 0, 1);  // cn_sync_type
    put(data, 4, 1);               // cn_data_type: IEEE float, little endian
    put(data, 0, 1);               // cn_bit_offset
    put(data, byteOffset, 4);
    put(data, 64, 4);              // cn_bit_count
    put(data, 0, 4);               // cn_flags
    put(data, 0, 4);               // cn_inval_bit_pos
    put(data, 0xFF, 1);            // cn_precision
    put(data, 0, 1);
    put(data, 0, 2);               // cn_attachment_count
    for (int i = 0; i < 6; ++i) {
        putDouble(data, 0.0);      // value and limit ranges
    }
    return data;
}

inline bool write(const std::string& path, std::uint64_t count,
                  const std::vector<Signal>& channels, const Time& timeOf, const Value& valueOf) {
    std::vector<char> out;
    const char fileId[8] = {'M', 'D', 'F', ' ', ' ', ' ', ' ', ' '};
    const char version[8] = {'4', '.', '2', '0', 0, 0, 0, 0};
    const char program[8] = {'t', 'e', 's', 't', ' ', ' ', ' ', ' '};
    out.insert(out.end(), fileId, fileId + 8);
    out.insert(out.end(), version, version + 8);
    out.insert(out.end(), program, program + 8);
    put(out, 0, 4);
    put(out, 420, 2);
    out.resize(out.size() + 30, 0);
    put(out, 0, 2);
    put(out, 0, 2);

    std::vector<char> header;
    put(header, 0, 8);  // hd_start_time_ns
    put(header, 0, 8);  // offsets, time flags and class, hd_flags, reserved
    putDouble(header, 0.0);
    putDouble(header, 0.0);
    const std::uint64_t hd = block(out, "##HD", {0, 0, 0, 0, 0, 0}, header);

    // The chain is emitted last channel first, so each next link is known.
    std::uint64_t next = 0;
    for (std::size_t i = channels.size(); i-- > 0;) {
        const std::uint64_t name = text(out, channels[i].name);
        const std::uint64_t unit = channels[i].unit.empty() ? 0 : text(out, channels[i].unit);
        next = block(out, "##CN", {next, 0, name, 0, 0, 0, unit, 0},
                     channelData(false, static_cast<std::uint32_t>(8 * (i + 1))));
    }
    const std::uint64_t timeName = text(out, "time");
    const std::uint64_t timeUnit = text(out, "s");
    const std::uint64_t time = block(out, "##CN", {next, 0, timeName, 0, 0, 0, timeUnit, 0},
                                     channelData(true, 0));
    const std::uint64_t recordBytes = 8 * (channels.size() + 1);
    std::vector<char> group;
    put(group, 0, 8);            // cg_record_id
    put(group, count, 8);        // cg_cycle_count
    put(group, 0, 8);            // flags, path separator, reserved
    put(group, recordBytes, 4);  // cg_data_bytes
    put(group, 0, 4);            // cg_inval_bytes
    const std::uint64_t cg = block(out, "##CG", {0, time, 0, 0, 0, 0}, group);
    while (out.size() % 8 != 0) {
        out.push_back(0);
    }
    const std::uint64_t dt = out.size() + 64;  // after the DGBLOCK
    std::vector<char> dataGroup(8, 0);        // dg_rec_id_size 0, reserved
    const std::uint64_t dg = block(out, "##DG", {0, cg, dt, 0}, dataGroup);
    std::memcpy(out.data() + hd + 24, &dg, 8);  // hd_dg_first
    block(out, "##DT", {}, {}, recordBytes * count);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    std::vector<char> records;
    for (std::uint64_t first = 0; first < count && file;) {
        const std::uint64_t end = std::min<std::uint64_t>(count, first + 65536);
        records.clear();
        for (std::uint64_t i = first; i < end; ++i) {
            putDouble(records, timeOf(i));
            for (std::size_t channel = 0; channel < channels.size(); ++channel) {
                putDouble(records, valueOf(channel, i));
            }
        }
        file.write(records.data(), static_cast<std::streamsize>(records.size()));
        first = end;
    }
    return static_cast<bool>(file);
}

} // namespace mf4recording
