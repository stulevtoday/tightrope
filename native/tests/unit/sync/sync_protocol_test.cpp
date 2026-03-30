#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "sync_protocol.h"

TEST_CASE("sync protocol handshake frame round-trips", "[sync][protocol]") {
    const tightrope::sync::HandshakeFrame handshake = {
        .site_id = 7,
        .schema_version = 3,
        .last_recv_seq_from_peer = 42,
    };

    const auto encoded = tightrope::sync::encode_handshake(handshake);
    const auto decoded = tightrope::sync::decode_handshake(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->site_id == handshake.site_id);
    REQUIRE(decoded->schema_version == handshake.schema_version);
    REQUIRE(decoded->last_recv_seq_from_peer == handshake.last_recv_seq_from_peer);
}

TEST_CASE("sync protocol journal batch round-trips with compression", "[sync][protocol]") {
    tightrope::sync::JournalBatchFrame batch;
    batch.from_seq = 10;
    batch.to_seq = 11;
    batch.entries = {
        {
            .seq = 11,
            .hlc_wall = 1000,
            .hlc_counter = 1,
            .site_id = 7,
            .table_name = "accounts",
            .row_pk = R"({"id":"1"})",
            .op = "INSERT",
            .old_values = "",
            .new_values = R"({"email":"a@x.com"})",
            .checksum = "abcd",
            .applied = 1,
            .batch_id = "b1",
        },
    };

    const auto encoded = tightrope::sync::encode_journal_batch(batch);
    const auto decoded = tightrope::sync::decode_journal_batch(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->from_seq == batch.from_seq);
    REQUIRE(decoded->to_seq == batch.to_seq);
    REQUIRE(decoded->entries.size() == 1);
    REQUIRE(decoded->entries.front().row_pk == R"({"id":"1"})");
    REQUIRE(decoded->entries.front().new_values == R"({"email":"a@x.com"})");
}
