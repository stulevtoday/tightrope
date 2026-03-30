#include "sync_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <lz4.h>

#include "sync_logging.h"

namespace tightrope::sync {

namespace {

constexpr std::uint8_t kCompressedFlag = 0x1;

void write_u8(std::vector<std::uint8_t>& out, const std::uint8_t value) {
    out.push_back(value);
}

void write_u32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void write_u64(std::vector<std::uint8_t>& out, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void write_i32(std::vector<std::uint8_t>& out, const std::int32_t value) {
    write_u32(out, static_cast<std::uint32_t>(value));
}

bool write_string(std::vector<std::uint8_t>& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    const auto offset = out.size();
    out.resize(offset + value.size());
    boost::asio::buffer_copy(
        boost::asio::buffer(out.data() + offset, value.size()),
        boost::asio::buffer(value.data(), value.size())
    );
    return true;
}

bool read_u8(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::uint8_t& out) {
    if (cursor + 1 > in.size()) {
        return false;
    }
    out = in[cursor];
    ++cursor;
    return true;
}

bool read_u32(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::uint32_t& out) {
    if (cursor + 4 > in.size()) {
        return false;
    }
    out = static_cast<std::uint32_t>(in[cursor]) | (static_cast<std::uint32_t>(in[cursor + 1]) << 8U) |
          (static_cast<std::uint32_t>(in[cursor + 2]) << 16U) | (static_cast<std::uint32_t>(in[cursor + 3]) << 24U);
    cursor += 4;
    return true;
}

bool read_u64(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::uint64_t& out) {
    if (cursor + 8 > in.size()) {
        return false;
    }
    out = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        out |= (static_cast<std::uint64_t>(in[cursor++]) << shift);
    }
    return true;
}

bool read_i32(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::int32_t& out) {
    std::uint32_t raw = 0;
    if (!read_u32(cursor, in, raw)) {
        return false;
    }
    out = static_cast<std::int32_t>(raw);
    return true;
}

bool read_string(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::string& out) {
    std::uint32_t size = 0;
    if (!read_u32(cursor, in, size)) {
        return false;
    }
    if (cursor + size > in.size()) {
        return false;
    }
    out.resize(size);
    boost::asio::buffer_copy(
        boost::asio::buffer(out.data(), size),
        boost::asio::buffer(in.data() + cursor, size)
    );
    cursor += size;
    return true;
}

std::optional<std::vector<std::uint8_t>> compress_lz4(const std::vector<std::uint8_t>& payload) {
    const auto max_size = LZ4_compressBound(static_cast<int>(payload.size()));
    if (max_size <= 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(max_size));
    const auto written = LZ4_compress_default(
        reinterpret_cast<const char*>(payload.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(payload.size()),
        max_size
    );
    if (written <= 0) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

std::optional<std::vector<std::uint8_t>>
decompress_lz4(const std::vector<std::uint8_t>& payload, const std::size_t uncompressed_size) {
    std::vector<std::uint8_t> out(uncompressed_size);
    const auto written = LZ4_decompress_safe(
        reinterpret_cast<const char*>(payload.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(payload.size()),
        static_cast<int>(uncompressed_size)
    );
    if (written < 0 || static_cast<std::size_t>(written) != uncompressed_size) {
        return std::nullopt;
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> encode_handshake(const HandshakeFrame& frame) {
    std::vector<std::uint8_t> out;
    out.reserve(16);
    write_u32(out, frame.site_id);
    write_u32(out, frame.schema_version);
    write_u64(out, frame.last_recv_seq_from_peer);
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_protocol",
        "encode_handshake",
        "site_id=" + std::to_string(frame.site_id) + " schema=" + std::to_string(frame.schema_version) +
            " last_recv=" + std::to_string(frame.last_recv_seq_from_peer) + " bytes=" + std::to_string(out.size()));
    return out;
}

std::optional<HandshakeFrame> decode_handshake(const std::vector<std::uint8_t>& bytes) {
    std::size_t cursor = 0;
    HandshakeFrame frame;
    if (!read_u32(cursor, bytes, frame.site_id) || !read_u32(cursor, bytes, frame.schema_version) ||
        !read_u64(cursor, bytes, frame.last_recv_seq_from_peer) || cursor != bytes.size()) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_handshake_failed",
            "bytes=" + std::to_string(bytes.size()));
        return std::nullopt;
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_protocol",
        "decode_handshake",
        "site_id=" + std::to_string(frame.site_id) + " schema=" + std::to_string(frame.schema_version) +
            " last_recv=" + std::to_string(frame.last_recv_seq_from_peer));
    return frame;
}

std::vector<std::uint8_t> encode_journal_batch(const JournalBatchFrame& frame) {
    std::vector<std::uint8_t> payload;
    payload.reserve(64 + frame.entries.size() * 256);
    write_u64(payload, frame.from_seq);
    write_u64(payload, frame.to_seq);
    write_u32(payload, static_cast<std::uint32_t>(frame.entries.size()));

    for (const auto& entry : frame.entries) {
        write_u64(payload, entry.seq);
        write_u64(payload, entry.hlc_wall);
        write_u32(payload, entry.hlc_counter);
        write_u32(payload, entry.site_id);
        write_string(payload, entry.table_name);
        write_string(payload, entry.row_pk);
        write_string(payload, entry.op);
        write_string(payload, entry.old_values);
        write_string(payload, entry.new_values);
        write_string(payload, entry.checksum);
        write_i32(payload, entry.applied);
        write_string(payload, entry.batch_id);
    }

    const auto compressed = compress_lz4(payload);
    const bool use_compressed = compressed.has_value() && compressed->size() < payload.size();
    const auto& wire_payload = use_compressed ? *compressed : payload;

    std::vector<std::uint8_t> wire;
    wire.reserve(1 + 4 + 4 + wire_payload.size());
    write_u8(wire, use_compressed ? kCompressedFlag : 0);
    write_u32(wire, static_cast<std::uint32_t>(payload.size()));
    write_u32(wire, static_cast<std::uint32_t>(wire_payload.size()));
    wire.insert(wire.end(), wire_payload.begin(), wire_payload.end());
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_protocol",
        "encode_journal_batch",
        "entries=" + std::to_string(frame.entries.size()) + " from_seq=" + std::to_string(frame.from_seq) +
            " to_seq=" + std::to_string(frame.to_seq) + " payload_bytes=" + std::to_string(payload.size()) +
            " wire_bytes=" + std::to_string(wire.size()) + " compressed=" + std::string(use_compressed ? "1" : "0"));
    return wire;
}

std::optional<JournalBatchFrame> decode_journal_batch(const std::vector<std::uint8_t>& bytes) {
    std::size_t cursor = 0;
    std::uint8_t flags = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t payload_size = 0;

    if (!read_u8(cursor, bytes, flags) || !read_u32(cursor, bytes, uncompressed_size) ||
        !read_u32(cursor, bytes, payload_size) || cursor + payload_size != bytes.size()) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_journal_batch_invalid_header",
            "bytes=" + std::to_string(bytes.size()));
        return std::nullopt;
    }

    std::vector<std::uint8_t> payload(payload_size);
    boost::asio::buffer_copy(
        boost::asio::buffer(payload.data(), payload_size),
        boost::asio::buffer(bytes.data() + cursor, payload_size)
    );

    if ((flags & kCompressedFlag) != 0) {
        auto decompressed = decompress_lz4(payload, uncompressed_size);
        if (!decompressed.has_value()) {
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_protocol",
                "decode_journal_batch_decompress_failed",
                "payload_bytes=" + std::to_string(payload.size()) + " uncompressed_bytes=" +
                    std::to_string(uncompressed_size));
            return std::nullopt;
        }
        payload = std::move(*decompressed);
    }

    std::size_t parse_cursor = 0;
    JournalBatchFrame batch;
    std::uint32_t count = 0;
    if (!read_u64(parse_cursor, payload, batch.from_seq) || !read_u64(parse_cursor, payload, batch.to_seq) ||
        !read_u32(parse_cursor, payload, count)) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_journal_batch_invalid_prefix",
            "payload_bytes=" + std::to_string(payload.size()));
        return std::nullopt;
    }

    batch.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        JournalWireEntry entry;
        if (!read_u64(parse_cursor, payload, entry.seq) || !read_u64(parse_cursor, payload, entry.hlc_wall) ||
            !read_u32(parse_cursor, payload, entry.hlc_counter) || !read_u32(parse_cursor, payload, entry.site_id) ||
            !read_string(parse_cursor, payload, entry.table_name) || !read_string(parse_cursor, payload, entry.row_pk) ||
            !read_string(parse_cursor, payload, entry.op) || !read_string(parse_cursor, payload, entry.old_values) ||
            !read_string(parse_cursor, payload, entry.new_values) || !read_string(parse_cursor, payload, entry.checksum) ||
            !read_i32(parse_cursor, payload, entry.applied) || !read_string(parse_cursor, payload, entry.batch_id)) {
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_protocol",
                "decode_journal_batch_invalid_entry",
                "index=" + std::to_string(i));
            return std::nullopt;
        }
        batch.entries.push_back(std::move(entry));
    }

    if (parse_cursor != payload.size()) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_journal_batch_trailing_bytes",
            "remaining=" + std::to_string(payload.size() - parse_cursor));
        return std::nullopt;
    }
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_protocol",
        "decode_journal_batch",
        "entries=" + std::to_string(batch.entries.size()) + " from_seq=" + std::to_string(batch.from_seq) +
            " to_seq=" + std::to_string(batch.to_seq));
    return batch;
}

} // namespace tightrope::sync
