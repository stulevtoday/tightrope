#include "compact_handler.h"

namespace tightrope::proxy::stream {

std::string build_compact_success_payload() {
    return R"({"object":"response.compact","id":"resp_compact_1","status":"completed","output":[]})";
}

} // namespace tightrope::proxy::stream
