#include "journal_batch_id.h"

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace tightrope::sync {

std::string generate_batch_id() {
    thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string(generator());
}

bool is_valid_batch_id(const std::string_view batch_id) {
    try {
        boost::uuids::string_generator parser;
        const auto parsed = parser(std::string(batch_id));
        (void)parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace tightrope::sync
