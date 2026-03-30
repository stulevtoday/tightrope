#pragma once

#include <chrono>
#include <mutex>
#include <optional>

#include <napi.h>

#include "bridge.h"

namespace tightrope::bridge::addon_support {

Bridge& bridge_instance();
std::optional<std::chrono::steady_clock::time_point>& started_at();
std::mutex& bridge_mutex();

BridgeConfig parse_bridge_config(const Napi::Value& value);
ClusterConfig parse_cluster_config(const Napi::Object& object);
Napi::Object cluster_status_to_js(Napi::Env env, const ClusterStatus& status);

} // namespace tightrope::bridge::addon_support
