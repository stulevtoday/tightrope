#include "transcribe_handler.h"

namespace tightrope::proxy::stream {

std::string build_transcription_payload() {
    return R"({"text":"transcribed"})";
}

} // namespace tightrope::proxy::stream
