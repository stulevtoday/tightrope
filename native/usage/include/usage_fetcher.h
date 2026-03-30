#pragma once
// Fetch usage from upstream

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::usage {

struct UsageValidationResult {
    bool success = false;
    int status_code = 0;
    std::string message;
};

struct UsageWindowSnapshot {
    std::optional<int> used_percent;
    std::optional<int> limit_window_seconds;
    std::optional<int> reset_after_seconds;
    std::optional<std::int64_t> reset_at;
};

struct UsageRateLimitDetails {
    bool allowed = true;
    bool limit_reached = false;
    std::optional<UsageWindowSnapshot> primary_window;
    std::optional<UsageWindowSnapshot> secondary_window;
};

struct UsageCreditsDetails {
    bool has_credits = false;
    bool unlimited = false;
    std::optional<std::string> balance;
};

struct UsageAdditionalRateLimit {
    std::optional<std::string> quota_key;
    std::string limit_name;
    std::optional<std::string> display_label;
    std::string metered_feature;
    std::optional<UsageRateLimitDetails> rate_limit;
};

struct UsagePayloadSnapshot {
    std::string plan_type;
    std::optional<UsageRateLimitDetails> rate_limit;
    std::optional<UsageCreditsDetails> credits;
    std::vector<UsageAdditionalRateLimit> additional_rate_limits;
};

class UsageValidator {
public:
    virtual ~UsageValidator() = default;
    [[nodiscard]] virtual UsageValidationResult validate(std::string_view access_token, std::string_view account_id) = 0;
};

class UsagePayloadFetcher {
public:
    virtual ~UsagePayloadFetcher() = default;
    [[nodiscard]] virtual std::optional<UsagePayloadSnapshot> fetch(
        std::string_view access_token,
        std::string_view account_id
    ) = 0;
};

[[nodiscard]] UsageValidationResult validate_usage_identity(
    std::string_view access_token,
    std::string_view account_id,
    std::shared_ptr<UsageValidator> validator = {}
);

[[nodiscard]] std::optional<UsagePayloadSnapshot> fetch_usage_payload(
    std::string_view access_token,
    std::string_view account_id,
    std::shared_ptr<UsagePayloadFetcher> fetcher = {}
);

[[nodiscard]] std::optional<UsagePayloadSnapshot> cached_usage_payload(std::string_view account_id);

void set_usage_validator_for_testing(std::shared_ptr<UsageValidator> validator);
void clear_usage_validator_for_testing();
void set_usage_payload_fetcher_for_testing(std::shared_ptr<UsagePayloadFetcher> fetcher);
void clear_usage_payload_fetcher_for_testing();
void seed_usage_payload_cache_for_testing(std::string_view account_id, std::optional<UsagePayloadSnapshot> payload);
void clear_usage_payload_cache_for_testing();

} // namespace tightrope::usage
