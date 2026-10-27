#include "GroupTreeTest.h"
#include "model/GroupTree.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace rhino
{
namespace
{
void require(bool condition, const std::string& what)
{
    if (!condition)
        throw std::runtime_error("Group tree: " + what);
}

GroupSlot bus(int id, int claim = 0) { return {id, claim, false}; }
GroupSlot track(int claim = 0) { return {0, claim, false}; }
GroupSlot returnTrack() { return {0, 0, true}; }

// A bus claiming the one before it, `levels` deep, the first at the top.
std::vector<GroupSlot> chain(int levels)
{
    std::vector<GroupSlot> stack;
    for (int level = 1; level <= levels; ++level)
        stack.push_back(bus(level, level - 1));
    return stack;
}
}

void checkGroupTree()
{
    using Claims = std::vector<int>;

    // One level, as groups were before they nested.
    require(reconcileGroupClaims({bus(1), track(1), track(1), track()}) == Claims {0, 1, 1, 0},
            "a bus and its run of members read as they are");
    require(reconcileGroupClaims({bus(1), track(), track(1)}) == Claims {0, 1, 1},
            "a track directly under a bus is inside its group");
    require(reconcileGroupClaims({bus(1), track(1), track(), track(1)}) == Claims {0, 1, 1, 1},
            "a track between two members joins them");
    require(reconcileGroupClaims({bus(1), track(1), track()}) == Claims {0, 1, 0},
            "a track under the last member stays outside");
    require(reconcileGroupClaims({track(1), bus(1), track(1)}) == Claims {0, 0, 1},
            "a claim on a group not open yet lapses");
    require(reconcileGroupClaims({bus(1, 1), track(1)}) == Claims {0, 1}, "a bus is never its own member");
    require(reconcileGroupClaims({bus(1), track(1), returnTrack(), track(1)}) == Claims {0, 1, 0, 0},
            "a return closes every group and joins none");

    // Nested.
    const std::vector<GroupSlot> nested {bus(1), bus(2, 1), track(2), track(2), track(1)};
    require(reconcileGroupClaims(nested) == Claims {0, 1, 2, 2, 1}, "a nested group reads as it is");
    {
        const auto spans = readGroupSpans(nested);
        require(spans.size() == 2, "two groups");
        require(spans[0].id == 1 && spans[0].bus == 0 && spans[0].count == 4 && spans[0].level == 1
                    && spans[0].parent == 0,
                "the outer group spans the inner one and the track after it");
        require(spans[1].id == 2 && spans[1].bus == 1 && spans[1].count == 2 && spans[1].level == 2
                    && spans[1].parent == 1,
                "the inner group is a level down, inside the outer one");
    }
    require(reconcileGroupClaims({bus(1), bus(2, 1), track(2), track(1), track(2)}) == Claims {0, 1, 2, 2, 2},
            "a member of the outer group dropped between two of the inner group's joins the inner one");
    require(reconcileGroupClaims({bus(1), track(1), bus(2), track(2), track(1)}) == Claims {0, 1, 1, 2, 1},
            "a bus dropped between two members nests its group there");
    require(reconcileGroupClaims({bus(1), track(1), bus(2), track(2)}) == Claims {0, 1, 0, 2},
            "a bus under the last member stays outside, as any track does");

    // A claim carried away from its group.
    {
        const std::vector<GroupSlot> carried {bus(1), track(1), bus(2), track(2), track(1), track(2)};
        require(!claimHolds(carried, 4), "a claim on a group closed above the track does not hold");
        require(claimHolds(carried, 1) && claimHolds(carried, 3), "the claims around it do");
        auto cleared = carried;
        cleared[4].claim = 0;
        require(reconcileGroupClaims(cleared) == Claims {0, 1, 0, 2, 2, 2},
                "cleared, the carried track joins the group it landed in, and that group stays where it was");
    }

    // The limit.
    {
        auto deepest = chain(maxGroupLevels);
        deepest.push_back(track(maxGroupLevels));
        require(deepestGroupLevel(deepest) == maxGroupLevels, "a chain of buses is as deep as it is long");
        Claims asMade;
        for (const auto& slot : deepest)
            asMade.push_back(slot.claim);
        require(reconcileGroupClaims(deepest) == asMade, "groups nest as deep as the limit untouched");

        auto tooDeep = chain(maxGroupLevels + 1);
        tooDeep.push_back(track(maxGroupLevels + 1));
        require(deepestGroupLevel(tooDeep) == maxGroupLevels + 1, "one more bus is one level too many");
        const auto lifted = reconcileGroupClaims(tooDeep);
        require(std::equal(lifted.begin(), lifted.begin() + maxGroupLevels, asMade.begin()),
                "every group that fits stays where it is");
        require(lifted[static_cast<size_t>(maxGroupLevels)] == maxGroupLevels - 1,
                "and the bus past the limit is lifted beside the group it was inside");
        require(lifted.back() == maxGroupLevels + 1, "with its member still under it");
        auto reconciled = tooDeep;
        for (size_t slot = 0; slot < reconciled.size(); ++slot)
            reconciled[slot].claim = lifted[slot];
        auto levels = 0;
        for (const auto& span : readGroupSpans(reconciled))
            levels = std::max(levels, span.level);
        require(levels == maxGroupLevels, "and nothing is left deeper than the limit");

        // Three levels inside the eighth: only the one past the limit moves.
        auto dropped = chain(8);
        for (const auto& slot : {bus(100, 8), bus(101, 100), bus(102, 101), track(102)})
            dropped.push_back(slot);
        const auto fitted = reconcileGroupClaims(dropped);
        require(fitted[8] == 8 && fitted[9] == 100 && fitted[10] == 100 && fitted[11] == 102,
                "a group past the limit goes beside its own, keeping its members, and nothing above it moves");
    }

    // A selection taken whole.
    require(withGroupsWhole(nested, {1}) == std::vector<int> {1, 2, 3}, "a bus brings its group");
    require(withGroupsWhole(nested, {0}) == std::vector<int> {0, 1, 2, 3, 4}, "and every group inside it");
    require(withGroupsWhole(nested, {3, 1, 9}) == std::vector<int> {1, 2, 3},
            "in stack order, each once, nothing out of range");
    require(withGroupsWhole({bus(1), track(1), returnTrack()}, {2}).empty(), "a return is never taken");

    // Gathering: Ctrl+G on a group's bus and a loose track below it.
    {
        const std::vector<GroupSlot> loose {bus(1), track(1), track(1), track(), track()};
        const auto gathered = gatherTracks(loose, withGroupsWhole(loose, {0, 3}), -1, 7, bus(7));
        require(gathered.order == std::vector<int> {-1, 0, 1, 2, 3, 4}, "the new bus opens where the topmost was");
        const auto claims = reconcileGroupClaims(gathered.slots);
        require(claims == Claims {0, 7, 1, 1, 7, 0}, "the old group and the track go inside the new one whole");
        const auto spans = readGroupSpans(gathered.slots);
        require(spans.size() == 2 && spans[0].id == 7 && spans[0].count == 4 && spans[1].id == 1
                    && spans[1].parent == 7 && spans[1].level == 2,
                "which leaves the old group a level down");
    }
    // And out again: the nested group carried below the rest of its parent.
    {
        const auto gathered = gatherTracks(nested, withGroupsWhole(nested, {1}), 4, 0);
        require(gathered.order == std::vector<int> {0, 4, 1, 2, 3}, "the group lands below the outer one's last track");
        require(reconcileGroupClaims(gathered.slots) == Claims {0, 1, 0, 2, 2},
                "outside it, with its own members still inside it");
    }
}
}
