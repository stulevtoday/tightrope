#pragma once
// Wire format, handshake, batch exchange

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tightrope::sync {

struct HandshakeFrame {
    std::uint32_t site_id = 0;
    std::uint32_t schema_version = 0;
    std::uint64_t last_recv_seq_from_peer = 0;
};

struct JournalWireEntry {
    std::uint64_t seq = 0;
    std::uint64_t hlc_wall = 0;
    std::uint32_t hlc_counter = 0;
    std::uint32_t site_id = 0;
    std::string table_name;
    std::string row_pk;
    std::string op;
    std::string old_values;
    std::string new_values;
    std::string checksum;
    int applied = 1;
    std::string batch_id;
};

struct JournalBatchFrame {
    std::uint64_t from_seq = 0;
    std::uint64_t to_seq = 0;
    std::vector<JournalWireEntry> entries;
};

std::vector<std::uint8_t> encode_handshake(const HandshakeFrame& frame);
std::optional<HandshakeFrame> decode_handshake(const std::vector<std::uint8_t>& bytes);

std::vector<std::uint8_t> encode_journal_batch(const JournalBatchFrame& frame);
std::optional<JournalBatchFrame> decode_journal_batch(const std::vector<std::uint8_t>& bytes);

} // namespace tightrope::sync
