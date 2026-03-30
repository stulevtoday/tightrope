#pragma once

#include <cstddef>
#include <cstdint>

#include "mdns.h"

namespace tightrope::sync::discovery {

class MdnsRuntime {
public:
    virtual ~MdnsRuntime() = default;

    virtual int socket_open_ipv4(const sockaddr_in* saddr) = 0;
    virtual int socket_open_ipv6(const sockaddr_in6* saddr) = 0;
    virtual void socket_close(int sock) = 0;

    virtual size_t socket_listen(
        int sock, void* buffer, size_t capacity, mdns_record_callback_fn callback, void* user_data) = 0;

    virtual int query_send(
        int sock,
        mdns_record_type_t type,
        const char* name,
        size_t length,
        void* buffer,
        size_t capacity,
        std::uint16_t query_id) = 0;

    virtual size_t query_recv(
        int sock,
        void* buffer,
        size_t capacity,
        mdns_record_callback_fn callback,
        void* user_data,
        std::uint16_t query_id) = 0;

    virtual int query_answer_unicast(
        int sock,
        const void* address,
        size_t address_size,
        void* buffer,
        size_t capacity,
        std::uint16_t query_id,
        mdns_record_type_t record_type,
        const char* name,
        size_t name_length,
        mdns_record_t answer,
        const mdns_record_t* authority,
        size_t authority_count,
        const mdns_record_t* additional,
        size_t additional_count) = 0;

    virtual int query_answer_multicast(
        int sock,
        void* buffer,
        size_t capacity,
        mdns_record_t answer,
        const mdns_record_t* authority,
        size_t authority_count,
        const mdns_record_t* additional,
        size_t additional_count) = 0;

    virtual int announce_multicast(
        int sock,
        void* buffer,
        size_t capacity,
        mdns_record_t answer,
        const mdns_record_t* authority,
        size_t authority_count,
        const mdns_record_t* additional,
        size_t additional_count) = 0;

    virtual int goodbye_multicast(
        int sock,
        void* buffer,
        size_t capacity,
        mdns_record_t answer,
        const mdns_record_t* authority,
        size_t authority_count,
        const mdns_record_t* additional,
        size_t additional_count) = 0;
};

MdnsRuntime& default_mdns_runtime();

} // namespace tightrope::sync::discovery
