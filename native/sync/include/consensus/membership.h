#pragma once
// Joint consensus membership changes

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace tightrope::sync::consensus {

class Membership {
public:
    explicit Membership(std::vector<std::uint32_t> members);

    std::size_t size() const;
    std::size_t quorum_size() const;
    const std::vector<std::uint32_t>& members() const;
    bool contains(std::uint32_t node_id) const;

    bool has_majority(const std::unordered_set<std::uint32_t>& votes) const;
    bool has_joint_majority(const std::unordered_set<std::uint32_t>& votes) const;

    bool begin_joint_consensus(std::vector<std::uint32_t> next_members);
    bool commit_joint_consensus();
    bool in_joint_consensus() const;

private:
    static std::vector<std::uint32_t> normalize(std::vector<std::uint32_t> members);
    static bool has_majority_for(
        const std::vector<std::uint32_t>& members,
        const std::unordered_set<std::uint32_t>& votes
    );

    std::vector<std::uint32_t> members_;
    std::optional<std::vector<std::uint32_t>> joint_members_;
};

} // namespace tightrope::sync::consensus
