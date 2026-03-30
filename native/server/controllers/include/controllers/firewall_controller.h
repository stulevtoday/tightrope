#pragma once
// firewall API controller

#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct FirewallIpEntryPayload {
    std::string ip_address;
    std::string created_at;
};

struct FirewallListResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string mode = "allow_all";
    std::vector<FirewallIpEntryPayload> entries;
};

struct FirewallMutationResponse {
    int status = 500;
    std::string code;
    std::string message;
    FirewallIpEntryPayload entry;
};

struct FirewallDeleteResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string result;
};

[[nodiscard]] FirewallListResponse list_firewall_ips(sqlite3* db = nullptr);
[[nodiscard]] FirewallMutationResponse add_firewall_ip(std::string_view ip_address, sqlite3* db = nullptr);
[[nodiscard]] FirewallDeleteResponse delete_firewall_ip(std::string_view ip_address, sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
