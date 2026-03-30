#include "discovery/internal/mdns_service_records.h"

#include <cstring>
#include <string_view>

#include <boost/asio/ip/address.hpp>

#include <netinet/in.h>

#include "discovery/mdns_common.h"

namespace tightrope::sync::discovery::internal {

namespace {

constexpr std::string_view kTxtKeyCluster = "cluster";
constexpr std::string_view kTxtKeySiteId = "site_id";
constexpr std::string_view kTxtKeySyncPort = "sync_port";

mdns_string_t mdns_string(const std::string& value) {
    return {value.c_str(), value.size()};
}

mdns_string_t mdns_string_const(const std::string_view value) {
    return {value.data(), value.size()};
}

} // namespace

std::optional<ServiceRecords>
build_service_records(const std::string& raw_service_name, const ServiceAnnouncement& announcement) {
    ServiceRecords records;
    records.service_name = canonical_mdns_service_name(raw_service_name);

    boost::system::error_code address_error;
    const auto host_address = boost::asio::ip::make_address(announcement.endpoint.host, address_error);
    const bool host_is_ip_literal = !address_error;
    records.host_name = canonical_mdns_hostname(announcement.endpoint.host, announcement.site_id, host_is_ip_literal);
    records.instance_name = "site-" + std::to_string(announcement.site_id) + "." + records.service_name;
    records.txt_cluster = announcement.cluster_name;
    records.txt_site_id = std::to_string(announcement.site_id);
    records.txt_sync_port = std::to_string(announcement.endpoint.port);

    records.ptr_record.name = mdns_string(records.service_name);
    records.ptr_record.type = MDNS_RECORDTYPE_PTR;
    records.ptr_record.data.ptr.name = mdns_string(records.instance_name);

    records.srv_record.name = mdns_string(records.instance_name);
    records.srv_record.type = MDNS_RECORDTYPE_SRV;
    records.srv_record.data.srv.name = mdns_string(records.host_name);
    records.srv_record.data.srv.port = announcement.endpoint.port;
    records.srv_record.data.srv.priority = 0;
    records.srv_record.data.srv.weight = 0;

    records.txt_records[0].name = mdns_string(records.instance_name);
    records.txt_records[0].type = MDNS_RECORDTYPE_TXT;
    records.txt_records[0].data.txt.key = mdns_string_const(kTxtKeyCluster);
    records.txt_records[0].data.txt.value = mdns_string(records.txt_cluster);

    records.txt_records[1].name = mdns_string(records.instance_name);
    records.txt_records[1].type = MDNS_RECORDTYPE_TXT;
    records.txt_records[1].data.txt.key = mdns_string_const(kTxtKeySiteId);
    records.txt_records[1].data.txt.value = mdns_string(records.txt_site_id);

    records.txt_records[2].name = mdns_string(records.instance_name);
    records.txt_records[2].type = MDNS_RECORDTYPE_TXT;
    records.txt_records[2].data.txt.key = mdns_string_const(kTxtKeySyncPort);
    records.txt_records[2].data.txt.value = mdns_string(records.txt_sync_port);

    records.additional_records[records.additional_count++] = records.srv_record;

    if (host_is_ip_literal && host_address.is_v4()) {
        records.a_record.name = mdns_string(records.host_name);
        records.a_record.type = MDNS_RECORDTYPE_A;
        records.a_record.data.a.addr.sin_family = AF_INET;
        records.a_record.data.a.addr.sin_port = htons(announcement.endpoint.port);
#ifdef __APPLE__
        records.a_record.data.a.addr.sin_len = sizeof(sockaddr_in);
#endif
        const auto bytes = host_address.to_v4().to_bytes();
        std::memcpy(&records.a_record.data.a.addr.sin_addr.s_addr, bytes.data(), bytes.size());
        records.additional_records[records.additional_count++] = records.a_record;
    } else if (host_is_ip_literal && host_address.is_v6()) {
        records.aaaa_record.name = mdns_string(records.host_name);
        records.aaaa_record.type = MDNS_RECORDTYPE_AAAA;
        records.aaaa_record.data.aaaa.addr.sin6_family = AF_INET6;
        records.aaaa_record.data.aaaa.addr.sin6_port = htons(announcement.endpoint.port);
#ifdef __APPLE__
        records.aaaa_record.data.aaaa.addr.sin6_len = sizeof(sockaddr_in6);
#endif
        const auto bytes = host_address.to_v6().to_bytes();
        std::memcpy(&records.aaaa_record.data.aaaa.addr.sin6_addr, bytes.data(), bytes.size());
        records.additional_records[records.additional_count++] = records.aaaa_record;
    }

    records.additional_records[records.additional_count++] = records.txt_records[0];
    records.additional_records[records.additional_count++] = records.txt_records[1];
    records.additional_records[records.additional_count++] = records.txt_records[2];
    return records;
}

} // namespace tightrope::sync::discovery::internal
