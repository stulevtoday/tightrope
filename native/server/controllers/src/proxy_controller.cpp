#include "proxy_controller.h"

#include "chat_completions_service.h"
#include "openai/error_envelope.h"
#include "proxy_service.h"
#include "ws_proxy.h"

namespace tightrope::server::controllers {

ProxyHttpResponse post_proxy_responses_json(
    const std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers
) {
    const auto result = proxy::collect_responses_json(route, body, headers);
    return {
        .status = result.status,
        .content_type = "application/json",
        .body = result.body,
        .headers = result.headers,
    };
}

ProxySseResponse post_proxy_responses_sse(
    const std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers
) {
    const auto result = proxy::stream_responses_sse(route, body, headers);
    return {
        .status = result.status,
        .content_type = "text/event-stream",
        .events = result.events,
        .headers = result.headers,
    };
}

ProxyWsResponse proxy_responses_websocket(
    const std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers
) {
    const auto result = proxy::proxy_responses_websocket(route, body, headers);
    return {
        .status = result.status,
        .accepted = result.accepted,
        .close_code = result.close_code,
        .frames = result.frames,
    };
}

ProxyHttpResponse post_proxy_responses_compact(
    const std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers
) {
    const auto result = proxy::collect_responses_compact(route, body, headers);
    return {
        .status = result.status,
        .content_type = "application/json",
        .body = result.body,
        .headers = result.headers,
    };
}

ProxyHttpResponse get_proxy_models(
    const std::string_view route,
    const proxy::ModelListPolicy& policy,
    const proxy::openai::HeaderMap& headers
) {
    const auto result = proxy::collect_models(route, policy, headers);
    return {
        .status = result.status,
        .content_type = "application/json",
        .body = result.body,
        .headers = result.headers,
    };
}

ProxyHttpResponse post_proxy_transcribe(
    const std::string_view route,
    const std::string_view model,
    const std::string_view prompt,
    const std::string_view audio_bytes,
    const proxy::openai::HeaderMap& headers
) {
    const auto result = proxy::collect_transcribe(route, model, prompt, audio_bytes, headers);
    return {
        .status = result.status,
        .content_type = "application/json",
        .body = result.body,
        .headers = result.headers,
    };
}

bool proxy_chat_completions_stream_requested(const std::string& body) {
    return proxy::chat_completions_stream_requested(body);
}

ProxyHttpResponse post_proxy_chat_completions_json(
    const std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers
) {
    if (route != "/v1/chat/completions") {
        return {
            .status = 404,
            .content_type = "application/json",
            .body = proxy::openai::build_error_envelope("not_found", "Route not found"),
        };
    }
    const auto result = proxy::collect_chat_completions_json(body, headers);
    return {
        .status = result.status,
        .content_type = "application/json",
        .body = result.body,
        .headers = result.headers,
    };
}

ProxySseResponse post_proxy_chat_completions_sse(
    const std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers
) {
    if (route != "/v1/chat/completions") {
        return {
            .status = 404,
            .content_type = "text/event-stream",
            .events = {proxy::openai::build_error_envelope("not_found", "Route not found")},
        };
    }
    const auto result = proxy::stream_chat_completions_sse(body, headers);
    return {
        .status = result.status,
        .content_type = "text/event-stream",
        .events = result.events,
        .headers = result.headers,
    };
}

} // namespace tightrope::server::controllers
