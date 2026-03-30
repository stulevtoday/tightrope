#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "eligibility.h"
#include "scorer.h"
#include "strategies/cost_aware.h"
#include "strategies/deadline_aware.h"
#include "strategies/headroom.h"
#include "strategies/latency_ewma.h"
#include "strategies/least_outstanding.h"
#include "strategies/power_of_two.h"
#include "strategies/round_robin.h"
#include "strategies/success_rate.h"
#include "strategies/weighted_round_robin.h"

namespace {

std::vector<tightrope::balancer::AccountCandidate> sample_accounts() {
    return {
        {.id = "a1", .active = true, .healthy = true, .enabled = true, .usage_ratio = 0.10, .static_weight = 1.0},
        {.id = "a2", .active = true, .healthy = true, .enabled = true, .usage_ratio = 0.40, .static_weight = 1.0},
        {.id = "a3", .active = true, .healthy = false, .enabled = true, .usage_ratio = 0.05, .static_weight = 1.0},
    };
}

} // namespace

TEST_CASE("round robin rotates across eligible accounts", "[proxy][balancer]") {
    auto accounts = sample_accounts();
    std::size_t cursor = 0;

    const auto* first = tightrope::balancer::pick_round_robin(accounts, cursor);
    const auto* second = tightrope::balancer::pick_round_robin(accounts, cursor);
    const auto* third = tightrope::balancer::pick_round_robin(accounts, cursor);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(first->id == "a1");
    REQUIRE(second->id == "a2");
    REQUIRE(third->id == "a1");
}

TEST_CASE("eligibility excludes unhealthy and saturated accounts", "[proxy][balancer]") {
    auto accounts = sample_accounts();
    accounts[1].usage_ratio = 0.99;
    const tightrope::balancer::EligibilityOptions options{.max_usage_ratio = 0.95};

    const auto eligible = tightrope::balancer::filter_eligible_accounts(accounts, options);
    REQUIRE(eligible.size() == 1);
    REQUIRE(eligible.front()->id == "a1");
}

TEST_CASE("weighted round robin favors lower usage ratio", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1", .active = true, .healthy = true, .enabled = true, .usage_ratio = 0.05, .static_weight = 1.0},
        {.id = "a2", .active = true, .healthy = true, .enabled = true, .usage_ratio = 0.85, .static_weight = 1.0},
    };

    tightrope::balancer::WeightedRoundRobinPicker picker;
    int a1_hits = 0;
    int a2_hits = 0;
    for (int i = 0; i < 40; ++i) {
        const auto* selected = picker.pick(accounts);
        REQUIRE(selected != nullptr);
        if (selected->id == "a1") {
            ++a1_hits;
        } else if (selected->id == "a2") {
            ++a2_hits;
        }
    }

    REQUIRE(a1_hits > a2_hits);
}

TEST_CASE("power of two chooses the stronger of two sampled accounts", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.05,
         .ewma_latency_ms = 100.0,
         .success_rate = 0.99},
        {.id = "a2",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.90,
         .ewma_latency_ms = 3000.0,
         .success_rate = 0.50},
    };

    tightrope::balancer::PowerOfTwoPicker picker(/*seed=*/7);
    const auto* selected = picker.pick(accounts);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->id == "a1");
}

TEST_CASE("composite scorer penalizes latency and outstanding load", "[proxy][balancer]") {
    tightrope::balancer::AccountCandidate fast{
        .id = "a1",
        .active = true,
        .healthy = true,
        .enabled = true,
        .usage_ratio = 0.20,
        .outstanding_requests = 1,
        .ewma_latency_ms = 80.0,
        .success_rate = 0.98,
    };
    tightrope::balancer::AccountCandidate slow{
        .id = "a2",
        .active = true,
        .healthy = true,
        .enabled = true,
        .usage_ratio = 0.20,
        .outstanding_requests = 8,
        .ewma_latency_ms = 800.0,
        .success_rate = 0.98,
    };

    const auto weights = tightrope::balancer::ScoreWeights{
        .headroom = 1.0,
        .success_rate = 1.0,
        .latency_penalty = 0.001,
        .outstanding_penalty = 0.1,
    };
    const auto fast_score = tightrope::balancer::compute_composite_score(fast, weights);
    const auto slow_score = tightrope::balancer::compute_composite_score(slow, weights);
    REQUIRE(fast_score > slow_score);
}

TEST_CASE("least outstanding picks account with smallest in-flight load", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1", .active = true, .healthy = true, .enabled = true, .outstanding_requests = 9},
        {.id = "a2", .active = true, .healthy = true, .enabled = true, .outstanding_requests = 2},
    };
    const auto* selected = tightrope::balancer::pick_least_outstanding(accounts);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->id == "a2");
}

TEST_CASE("latency ewma picks account with lowest latency", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1", .active = true, .healthy = true, .enabled = true, .ewma_latency_ms = 240.0},
        {.id = "a2", .active = true, .healthy = true, .enabled = true, .ewma_latency_ms = 95.0},
    };
    const auto* selected = tightrope::balancer::pick_lowest_latency_ewma(accounts);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->id == "a2");
}

TEST_CASE("headroom strategy prefers higher weighted quota headroom", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.20,
         .secondary_usage_ratio = 0.80},
        {.id = "a2",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.30,
         .secondary_usage_ratio = 0.10},
    };
    const tightrope::balancer::HeadroomWeights weights{.primary = 0.5, .secondary = 0.5};
    const auto* selected = tightrope::balancer::pick_headroom_score(accounts, {}, weights);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->id == "a2");
}

TEST_CASE("success-rate weighted strategy favors higher reliability", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1", .active = true, .healthy = true, .enabled = true, .success_rate = 0.95},
        {.id = "a2", .active = true, .healthy = true, .enabled = true, .success_rate = 0.25},
    };

    tightrope::balancer::SuccessRateWeightedPicker picker(/*seed=*/42);
    int a1_hits = 0;
    int a2_hits = 0;
    for (int i = 0; i < 200; ++i) {
        const auto* selected = picker.pick(accounts, {}, /*rho=*/2.0);
        REQUIRE(selected != nullptr);
        if (selected->id == "a1") {
            ++a1_hits;
        } else if (selected->id == "a2") {
            ++a2_hits;
        }
    }
    REQUIRE(a1_hits > a2_hits);
}

TEST_CASE("cost-aware picks cheapest compliant candidate and falls back to headroom", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "a1",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.35,
         .error_ewma = 0.10,
         .normalized_cost = 0.20},
        {.id = "a2",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.98,
         .error_ewma = 0.05,
         .normalized_cost = 0.05},
        {.id = "a3",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.25,
         .error_ewma = 0.90,
         .normalized_cost = 0.30},
    };

    const tightrope::balancer::CostAwareGuardrails guardrails{
        .min_headroom = 0.05,
        .max_error_ewma = 0.50,
    };

    const auto* selected = tightrope::balancer::pick_cost_aware(accounts, {}, guardrails);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->id == "a1");

    // Tight error guardrail disqualifies all -> fallback chooses highest headroom (a3).
    const tightrope::balancer::CostAwareGuardrails strict{
        .min_headroom = 0.05,
        .max_error_ewma = 0.01,
    };
    const auto* fallback = tightrope::balancer::pick_cost_aware(accounts, {}, strict);
    REQUIRE(fallback != nullptr);
    REQUIRE(fallback->id == "a3");
}

TEST_CASE("deadline-aware strategy increases latency pressure under tight slack", "[proxy][balancer]") {
    std::vector<tightrope::balancer::AccountCandidate> accounts = {
        {.id = "slow_headroom",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.05,
         .ewma_latency_ms = 1000.0,
         .success_rate = 0.90},
        {.id = "fast_loaded",
         .active = true,
         .healthy = true,
         .enabled = true,
         .usage_ratio = 0.80,
         .ewma_latency_ms = 50.0,
         .success_rate = 0.98},
    };

    const tightrope::balancer::DeadlineAwareOptions relaxed{
        .deadline_slack_ms = 2000.0,
        .tight_deadline_ms = 200.0,
        .urgency_scale = 4.0,
        .base_weights =
            tightrope::balancer::ScoreWeights{
                .headroom = 2.0,
                .success_rate = 1.0,
                .latency_penalty = 0.001,
                .outstanding_penalty = 0.0,
            },
    };
    const tightrope::balancer::DeadlineAwareOptions tight{
        .deadline_slack_ms = 0.0,
        .tight_deadline_ms = 200.0,
        .urgency_scale = 4.0,
        .base_weights = relaxed.base_weights,
    };

    const auto* relaxed_pick = tightrope::balancer::pick_deadline_aware(accounts, {}, relaxed);
    const auto* tight_pick = tightrope::balancer::pick_deadline_aware(accounts, {}, tight);
    REQUIRE(relaxed_pick != nullptr);
    REQUIRE(tight_pick != nullptr);
    REQUIRE(relaxed_pick->id == "slow_headroom");
    REQUIRE(tight_pick->id == "fast_loaded");
}
