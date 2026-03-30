#include "reservation.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace tightrope::auth::api_keys {

std::string generate_reservation_request_id() {
    static thread_local boost::uuids::random_generator generator;
    return "resv_" + boost::uuids::to_string(generator());
}

bool is_terminal_status(const std::string_view status) noexcept {
    return status == "settled" || status == "released" || status == "cancelled" || status == "failed";
}

bool is_valid_status_transition(const std::string_view current_status, const std::string_view next_status) noexcept {
    if (current_status == next_status) {
        return true;
    }
    if (current_status == "reserved") {
        return is_terminal_status(next_status);
    }
    return false;
}

} // namespace tightrope::auth::api_keys
