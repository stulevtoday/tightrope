#include "internal/addon_bindings_support.h"

#include <string>

namespace tightrope::bridge::addon_support {

namespace {

bool has_key(const Napi::Object& object, const char* key) {
    return object.Has(key) && !object.Get(key).IsUndefined() && !object.Get(key).IsNull();
}

std::string require_string(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key) || !object.Get(key).IsString()) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be a string");
    }
    return object.Get(key).As<Napi::String>().Utf8Value();
}

std::uint32_t require_u32(const Napi::Object& object, const char* key, const char* context) {
    if (!has_key(object, key) || !object.Get(key).IsNumber()) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be a number");
    }
    const auto value = object.Get(key).As<Napi::Number>().Uint32Value();
    if (value == 0) {
        throw Napi::TypeError::New(object.Env(), std::string(context) + "." + key + " must be greater than zero");
    }
    return value;
}

std::string role_to_string(const ClusterRole role) {
    switch (role) {
    case ClusterRole::Leader:
        return "leader";
    case ClusterRole::Follower:
        return "follower";
    case ClusterRole::Candidate:
        return "candidate";
    case ClusterRole::Standalone:
    default:
        return "standalone";
    }
}

std::string peer_state_to_string(const PeerState state) {
    switch (state) {
    case PeerState::Connected:
        return "connected";
    case PeerState::Unreachable:
        return "unreachable";
    case PeerState::Disconnected:
    default:
        return "disconnected";
    }
}

std::string peer_source_to_string(const PeerSource source) {
    switch (source) {
    case PeerSource::Mdns:
        return "mdns";
    case PeerSource::Manual:
    default:
        return "manual";
    }
}

Napi::Object peer_to_js(Napi::Env env, const PeerStatus& peer) {
    auto object = Napi::Object::New(env);
    object.Set("site_id", peer.site_id);
    object.Set("address", peer.address);
    object.Set("state", peer_state_to_string(peer.state));
    object.Set("role", role_to_string(peer.role));
    object.Set("match_index", Napi::Number::New(env, static_cast<double>(peer.match_index)));
    if (peer.last_heartbeat_at.has_value()) {
        object.Set("last_heartbeat_at", Napi::Number::New(env, static_cast<double>(*peer.last_heartbeat_at)));
    } else {
        object.Set("last_heartbeat_at", env.Null());
    }
    object.Set("discovered_via", peer_source_to_string(peer.discovered_via));
    return object;
}

} // namespace

Bridge& bridge_instance() {
    static Bridge bridge;
    return bridge;
}

std::optional<std::chrono::steady_clock::time_point>& started_at() {
    static std::optional<std::chrono::steady_clock::time_point> value;
    return value;
}

std::mutex& bridge_mutex() {
    static std::mutex value;
    return value;
}

BridgeConfig parse_bridge_config(const Napi::Value& value) {
    BridgeConfig config;
    if (!value.IsObject()) {
        return config;
    }
    const auto object = value.As<Napi::Object>();

    if (has_key(object, "host")) {
        if (!object.Get("host").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.host must be a string");
        }
        config.host = object.Get("host").As<Napi::String>().Utf8Value();
    }
    if (has_key(object, "port")) {
        if (!object.Get("port").IsNumber()) {
            throw Napi::TypeError::New(object.Env(), "init.port must be a number");
        }
        const auto port = object.Get("port").As<Napi::Number>().Uint32Value();
        if (port == 0 || port > 65535) {
            throw Napi::TypeError::New(object.Env(), "init.port must be between 1 and 65535");
        }
        config.port = static_cast<std::uint16_t>(port);
    }
    if (has_key(object, "oauth_callback_port")) {
        if (!object.Get("oauth_callback_port").IsNumber()) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_port must be a number");
        }
        const auto callback_port = object.Get("oauth_callback_port").As<Napi::Number>().Uint32Value();
        if (callback_port == 0 || callback_port > 65535) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_port must be between 1 and 65535");
        }
        config.oauth_callback_port = static_cast<std::uint16_t>(callback_port);
    }
    if (has_key(object, "oauth_callback_host")) {
        if (!object.Get("oauth_callback_host").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_host must be a string");
        }
        config.oauth_callback_host = object.Get("oauth_callback_host").As<Napi::String>().Utf8Value();
        if (config.oauth_callback_host.empty()) {
            throw Napi::TypeError::New(object.Env(), "init.oauth_callback_host must not be empty");
        }
    }
    if (has_key(object, "db_path")) {
        if (!object.Get("db_path").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.db_path must be a string");
        }
        config.db_path = object.Get("db_path").As<Napi::String>().Utf8Value();
    }
    if (has_key(object, "config_path")) {
        if (!object.Get("config_path").IsString()) {
            throw Napi::TypeError::New(object.Env(), "init.config_path must be a string");
        }
        config.config_path = object.Get("config_path").As<Napi::String>().Utf8Value();
    }
    return config;
}

ClusterConfig parse_cluster_config(const Napi::Object& object) {
    ClusterConfig config;
    config.cluster_name = require_string(object, "cluster_name", "clusterEnable");
    config.sync_port = static_cast<std::uint16_t>(require_u32(object, "sync_port", "clusterEnable"));

    if (has_key(object, "site_id")) {
        config.site_id = require_u32(object, "site_id", "clusterEnable");
    }
    if (has_key(object, "discovery_enabled")) {
        if (!object.Get("discovery_enabled").IsBoolean()) {
            throw Napi::TypeError::New(object.Env(), "clusterEnable.discovery_enabled must be a boolean");
        }
        config.discovery_enabled = object.Get("discovery_enabled").As<Napi::Boolean>().Value();
    }
    if (has_key(object, "manual_peers")) {
        if (!object.Get("manual_peers").IsArray()) {
            throw Napi::TypeError::New(object.Env(), "clusterEnable.manual_peers must be an array");
        }
        const auto peers = object.Get("manual_peers").As<Napi::Array>();
        config.manual_peers.reserve(peers.Length());
        for (std::uint32_t i = 0; i < peers.Length(); ++i) {
            const auto peer = peers.Get(i);
            if (!peer.IsString()) {
                throw Napi::TypeError::New(object.Env(), "clusterEnable.manual_peers entries must be strings");
            }
            config.manual_peers.push_back(peer.As<Napi::String>().Utf8Value());
        }
    }
    return config;
}

Napi::Object cluster_status_to_js(Napi::Env env, const ClusterStatus& status) {
    auto object = Napi::Object::New(env);
    object.Set("enabled", Napi::Boolean::New(env, status.enabled));
    object.Set("site_id", status.site_id);
    object.Set("cluster_name", status.cluster_name);
    object.Set("role", role_to_string(status.role));
    object.Set("term", Napi::Number::New(env, static_cast<double>(status.term)));
    object.Set("commit_index", Napi::Number::New(env, static_cast<double>(status.commit_index)));
    if (status.leader_id.has_value()) {
        object.Set("leader_id", *status.leader_id);
    } else {
        object.Set("leader_id", env.Null());
    }

    auto peers = Napi::Array::New(env, status.peers.size());
    for (std::size_t i = 0; i < status.peers.size(); ++i) {
        peers.Set(static_cast<std::uint32_t>(i), peer_to_js(env, status.peers[i]));
    }
    object.Set("peers", peers);
    object.Set("journal_entries", Napi::Number::New(env, static_cast<double>(status.journal_entries)));
    object.Set("pending_raft_entries", Napi::Number::New(env, static_cast<double>(status.pending_raft_entries)));
    if (status.last_sync_at.has_value()) {
        object.Set("last_sync_at", Napi::Number::New(env, static_cast<double>(*status.last_sync_at)));
    } else {
        object.Set("last_sync_at", env.Null());
    }
    return object;
}

} // namespace tightrope::bridge::addon_support
