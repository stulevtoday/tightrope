#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "provider_client.h"

namespace tightrope::auth::oauth {

struct RefreshAccessTokenResult {
    bool refreshed = false;
    std::string error_code;
    std::string error_message;
};

RefreshAccessTokenResult refresh_access_token_for_account(
    sqlite3* db,
    std::string_view chatgpt_account_id,
    std::shared_ptr<ProviderClient> provider = {}
);
void set_token_refresh_provider_for_testing(std::shared_ptr<ProviderClient> provider);
void clear_token_refresh_provider_for_testing();

} // namespace tightrope::auth::oauth
