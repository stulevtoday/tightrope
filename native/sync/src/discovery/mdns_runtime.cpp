#include "discovery/mdns_runtime.h"

namespace tightrope::sync::discovery {

namespace {

class DefaultMdnsRuntime final : public MdnsRuntime {
public:
    int socket_open_ipv4(const sockaddr_in* saddr) override {
        return mdns_socket_open_ipv4(saddr);
    }

    int socket_open_ipv6(const sockaddr_in6* saddr) override {
        return mdns_socket_open_ipv6(saddr);
    }

    void socket_close(const int sock) override {
        mdns_socket_close(sock);
    }

    size_t socket_listen(
        const int sock,
        void* buffer,
        const size_t capacity,
        mdns_record_callback_fn callback,
        void* user_data) override {
        return mdns_socket_listen(sock, buffer, capacity, callback, user_data);
    }

    int query_send(
        const int sock,
        const mdns_record_type_t type,
        const char* name,
        const size_t length,
        void* buffer,
        const size_t capacity,
        const std::uint16_t query_id) override {
        return mdns_query_send(sock, type, name, length, buffer, capacity, query_id);
    }

    size_t query_recv(
        const int sock,
        void* buffer,
        const size_t capacity,
        mdns_record_callback_fn callback,
        void* user_data,
        const std::uint16_t query_id) override {
        return mdns_query_recv(sock, buffer, capacity, callback, user_data, query_id);
    }

    int query_answer_unicast(
        const int sock,
        const void* address,
        const size_t address_size,
        void* buffer,
        const size_t capacity,
        const std::uint16_t query_id,
        const mdns_record_type_t record_type,
        const char* name,
        const size_t name_length,
        const mdns_record_t answer,
        const mdns_record_t* authority,
        const size_t authority_count,
        const mdns_record_t* additional,
        const size_t additional_count) override {
        return mdns_query_answer_unicast(
            sock,
            address,
            address_size,
            buffer,
            capacity,
            query_id,
            record_type,
            name,
            name_length,
            answer,
            authority,
            authority_count,
            additional,
            additional_count);
    }

    int query_answer_multicast(
        const int sock,
        void* buffer,
        const size_t capacity,
        const mdns_record_t answer,
        const mdns_record_t* authority,
        const size_t authority_count,
        const mdns_record_t* additional,
        const size_t additional_count) override {
        return mdns_query_answer_multicast(
            sock, buffer, capacity, answer, authority, authority_count, additional, additional_count);
    }

    int announce_multicast(
        const int sock,
        void* buffer,
        const size_t capacity,
        const mdns_record_t answer,
        const mdns_record_t* authority,
        const size_t authority_count,
        const mdns_record_t* additional,
        const size_t additional_count) override {
        return mdns_announce_multicast(
            sock, buffer, capacity, answer, authority, authority_count, additional, additional_count);
    }

    int goodbye_multicast(
        const int sock,
        void* buffer,
        const size_t capacity,
        const mdns_record_t answer,
        const mdns_record_t* authority,
        const size_t authority_count,
        const mdns_record_t* additional,
        const size_t additional_count) override {
        return mdns_goodbye_multicast(
            sock, buffer, capacity, answer, authority, authority_count, additional, additional_count);
    }
};

} // namespace

MdnsRuntime& default_mdns_runtime() {
    static DefaultMdnsRuntime runtime;
    return runtime;
}

} // namespace tightrope::sync::discovery
