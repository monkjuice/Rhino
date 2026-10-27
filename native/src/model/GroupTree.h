#pragma once
#include <algorithm>
#include <climits>
#include <cstddef>
#include <vector>

// The shape of the track groups, read from the stack and nothing else.
//
// A group is a bus followed by the tracks inside it, and a bus can itself be
// inside another group, up to maxGroupLevels deep. Nothing stores a member
// list: each track names the one group it is directly inside (its claim), and
// where it sits decides whether that claim holds. These are the rules for
// that, kept free of JUCE and the engine so they can be checked against
// stacks made by hand (tests/GroupTreeTest.cpp). Session reads the tracks
// into slots, asks, and writes the answers back.
namespace rhino
{
// A group at the top of the stack is level 1, a group inside it level 2.
inline constexpr int maxGroupLevels = 10;

struct GroupSlot
{
    int bus = 0;         // the group this track is the bus of, or 0
    int claim = 0;       // the group it is directly inside, or 0
    bool fixed = false;  // a return: inside no group, and it closes any open one
};

struct GroupSpan
{
    int id = 0;
    int bus = -1;     // its bus's place in the stack
    int parent = 0;   // the group its bus is inside, or 0
    int level = 1;
    int count = 0;    // the tracks after the bus inside it, nested groups whole
};

namespace groupTree
{
// How many groups deep `group` is open, or 0 for none and -1 for a group that
// is not open here.
inline int depthIn(const std::vector<int>& open, int group)
{
    if (group == 0)
        return 0;
    for (size_t level = open.size(); level > 0; --level)
        if (open[level - 1] == group)
            return static_cast<int>(level);
    return -1;
}

// One past the last track a bus brings along by what the tracks under it
// claim: its members, and theirs where one of them is a bus. Any other track
// brings only itself.
inline size_t claimedEnd(const std::vector<GroupSlot>& stack, size_t first)
{
    if (stack[first].bus == 0)
        return first + 1;
    std::vector<int> inside {stack[first].bus};
    auto end = first + 1;
    for (; end < stack.size() && !stack[end].fixed; ++end)
    {
        if (std::find(inside.begin(), inside.end(), stack[end].claim) == inside.end())
            break;
        if (stack[end].bus != 0)
            inside.push_back(stack[end].bus);
    }
    return end;
}

}

// The group each track is directly inside once its place in the stack is
// honoured, walking down it with the groups open at each track:
//  - a claim holds while its group is open where the track sits;
//  - a track between two tracks of a deeper open group is inside that group,
//    the way a clip dropped on a lane is on that lane. Directly under a bus
//    counts, since the bus is what opens its group. A bus is judged by what
//    follows everything it brings, not by its own first member;
//  - a claim on a group that is not open there was carried away, and lapses;
//  - no group nests past `levels`: a bus that would is lifted to the deepest
//    level there is, beside the group it would have been inside, with
//    everything inside it. Commands refuse to nest that deep in the first
//    place, so this only repairs a stack that arrived that way.
// A bus is never its own member, and a fixed track is nobody's. The claim a
// newcomer is judged by is its neighbour's, so a stale one misleads it: a
// track carried somewhere new has its claim cleared first unless it holds
// there (claimHolds).
inline std::vector<int> reconcileGroupClaims(const std::vector<GroupSlot>& stack, int levels = maxGroupLevels)
{
    using namespace groupTree;
    std::vector<int> claims(stack.size(), 0);
    std::vector<int> open;
    for (size_t track = 0; track < stack.size(); ++track)
    {
        const auto& slot = stack[track];
        if (slot.fixed)
        {
            open.clear();
            continue;
        }
        auto group = slot.claim == slot.bus ? 0 : slot.claim;
        auto depth = depthIn(open, group);
        if (depth < 0)
        {
            group = 0;
            depth = 0;
        }
        if (const auto next = claimedEnd(stack, track); next < stack.size() && !stack[next].fixed)
            if (const auto between = depthIn(open, stack[next].claim); between > depth)
            {
                group = stack[next].claim;
                depth = between;
            }
        if (slot.bus != 0 && depth >= levels)
        {
            depth = std::max(0, levels - 1);
            group = depth > 0 ? open[static_cast<size_t>(depth) - 1] : 0;
        }
        open.resize(static_cast<size_t>(depth));
        claims[track] = group;
        if (slot.bus != 0 && depthIn(open, slot.bus) < 0)
            open.push_back(slot.bus);
    }
    return claims;
}

// The groups as the claims describe them, in stack order, so a group comes
// before every group inside it. Read literally: a claim on a group that is
// not open where the track sits leaves it outside every group. Reconciled
// claims read back exactly as reconciling decided them.
inline std::vector<GroupSpan> readGroupSpans(const std::vector<GroupSlot>& stack)
{
    std::vector<GroupSpan> spans;
    std::vector<size_t> open;
    for (size_t track = 0; track < stack.size(); ++track)
    {
        const auto& slot = stack[track];
        if (slot.fixed)
        {
            open.clear();
            continue;
        }
        size_t depth = 0;
        if (slot.claim != 0 && slot.claim != slot.bus)
            for (size_t level = 0; level < open.size(); ++level)
                if (spans[open[level]].id == slot.claim)
                    depth = level + 1;
        open.resize(depth);
        for (const auto index : open)
            spans[index].count = static_cast<int>(track) - spans[index].bus;
        if (slot.bus != 0)
        {
            GroupSpan span;
            span.id = slot.bus;
            span.bus = static_cast<int>(track);
            span.parent = open.empty() ? 0 : spans[open.back()].id;
            span.level = static_cast<int>(open.size()) + 1;
            open.push_back(spans.size());
            spans.push_back(span);
        }
    }
    return spans;
}

// Whether a track's claim holds where it sits, read literally: it names no
// group, or one that is open there. A claim left on a group a track was
// carried away from would pull the track above it into that group.
inline bool claimHolds(const std::vector<GroupSlot>& stack, int track)
{
    const auto claim = stack[static_cast<size_t>(track)].claim;
    if (claim == 0)
        return true;
    for (const auto& span : readGroupSpans(stack))
        if (span.id == claim && track > span.bus && track <= span.bus + span.count)
            return true;
    return false;
}

// The level of the deepest group once the stack is reconciled with no limit:
// what a command asks of the stack it is about to make, so it can refuse one
// nested past maxGroupLevels rather than have reconciling lift a group out.
inline int deepestGroupLevel(std::vector<GroupSlot> stack)
{
    const auto claims = reconcileGroupClaims(stack, INT_MAX);
    for (size_t track = 0; track < stack.size(); ++track)
        stack[track].claim = claims[track];
    auto deepest = 0;
    for (const auto& span : readGroupSpans(stack))
        deepest = std::max(deepest, span.level);
    return deepest;
}

// The chosen tracks with everything inside any bus among them, in stack order,
// each once. Out of range and fixed tracks are left out.
inline std::vector<int> withGroupsWhole(const std::vector<GroupSlot>& stack, const std::vector<int>& chosen)
{
    const auto spans = readGroupSpans(stack);
    std::vector<int> covered;
    for (const auto track : chosen)
    {
        if (track < 0 || track >= static_cast<int>(stack.size()) || stack[static_cast<size_t>(track)].fixed)
            continue;
        covered.push_back(track);
        for (const auto& span : spans)
            if (span.bus == track)
                for (auto inside = track + 1; inside <= track + span.count; ++inside)
                    covered.push_back(inside);
    }
    std::sort(covered.begin(), covered.end());
    covered.erase(std::unique(covered.begin(), covered.end()), covered.end());
    return covered;
}

// Tracks carried in one gesture: `moving` (in stack order, every bus among
// them with everything inside it) is lifted out and put back as one block
// after the stack's track `after` (-1 opens the stack), which must not be
// moving, led by a new bus when `newBus` has one. The tracks that lead the
// block - the new bus aside, every one whose group is not carried with it -
// claim `claim`; the rest keep theirs, so groups inside the block come whole.
// `order` names each track by its place in the stack it was planned from, -1
// for the new bus, and `slots` is what each will say once it lands.
struct GroupGathering
{
    std::vector<int> order;
    std::vector<GroupSlot> slots;
};

inline GroupGathering gatherTracks(const std::vector<GroupSlot>& stack, const std::vector<int>& moving, int after,
                                   int claim, GroupSlot newBus = {})
{
    std::vector<int> carriedBuses;
    for (const auto track : moving)
        if (const auto bus = stack[static_cast<size_t>(track)].bus; bus != 0)
            carriedBuses.push_back(bus);
    const auto isMoving = [&moving](int track) { return std::find(moving.begin(), moving.end(), track) != moving.end(); };

    GroupGathering gathering;
    const auto land = [&]
    {
        if (newBus.bus != 0)
        {
            gathering.order.push_back(-1);
            gathering.slots.push_back(newBus);
        }
        for (const auto track : moving)
        {
            auto slot = stack[static_cast<size_t>(track)];
            if (std::find(carriedBuses.begin(), carriedBuses.end(), slot.claim) == carriedBuses.end())
                slot.claim = claim;
            gathering.order.push_back(track);
            gathering.slots.push_back(slot);
        }
    };
    if (after < 0)
        land();
    for (int track = 0; track < static_cast<int>(stack.size()); ++track)
    {
        if (isMoving(track))
            continue;
        gathering.order.push_back(track);
        gathering.slots.push_back(stack[static_cast<size_t>(track)]);
        if (track == after)
            land();
    }
    return gathering;
}
}
