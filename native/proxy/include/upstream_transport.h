#pragma once

#include <memory>
#include <string>
#include <vector>

#include "openai/upstream_request_plan.h"

namespace tightrope::proxy {

struct UpstreamExecutionResult {
    int status = 500;
    std::string body;
    std::vector<std::string> events;
    bool accepted = false;
    int close_code = 1000;
    std::string error_code;
};

class UpstreamTransport {
public:
    virtual ~UpstreamTransport() = default;
    virtual UpstreamExecutionResult execute(const openai::UpstreamRequestPlan& plan) = 0;
};

void set_upstream_transport(std::shared_ptr<UpstreamTransport> transport);
void reset_upstream_transport();
UpstreamExecutionResult execute_upstream_plan(const openai::UpstreamRequestPlan& plan);

} // namespace tightrope::proxy
