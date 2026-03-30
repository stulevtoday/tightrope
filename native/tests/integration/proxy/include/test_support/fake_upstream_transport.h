#pragma once

#include <functional>
#include <memory>

#include "upstream_transport.h"

namespace tightrope::tests::proxy {

class FakeUpstreamTransport final : public tightrope::proxy::UpstreamTransport {
public:
    using Handler = std::function<tightrope::proxy::UpstreamExecutionResult(
        const tightrope::proxy::openai::UpstreamRequestPlan&)>;

    explicit FakeUpstreamTransport(Handler handler) : handler_(std::move(handler)) {}

    tightrope::proxy::UpstreamExecutionResult execute(
        const tightrope::proxy::openai::UpstreamRequestPlan& plan
    ) override {
        ++call_count;
        last_plan = plan;
        if (handler_) {
            return handler_(plan);
        }
        return {
            .status = 500,
            .body = R"({"error":{"message":"missing fake handler","code":"missing_fake_handler","type":"server_error"}})",
            .events = {},
            .accepted = false,
            .close_code = 1011,
            .error_code = "missing_fake_handler",
        };
    }

    std::size_t call_count = 0;
    tightrope::proxy::openai::UpstreamRequestPlan last_plan{};

private:
    Handler handler_;
};

class ScopedUpstreamTransport final {
public:
    explicit ScopedUpstreamTransport(const std::shared_ptr<tightrope::proxy::UpstreamTransport>& transport) {
        tightrope::proxy::set_upstream_transport(transport);
    }

    ~ScopedUpstreamTransport() {
        tightrope::proxy::reset_upstream_transport();
    }
};

} // namespace tightrope::tests::proxy

