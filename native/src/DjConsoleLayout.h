#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <array>

// Where everything on a deck's console stands (DjDeckPanel), worked out
// apart from the component so it can be checked without a Session, as
// ClipGeometry.h is.

namespace rhino::djconsole
{
constexpr int headerHeight = 24, padRowHeight = 22;

// What the console is given to lay out: its size, whether its clips grid
// shows, and the two heights it takes from its children.
struct ConsoleSpace
{
    juce::Rectangle<int> bounds;
    bool gridShown = false;
    int screenMinimum = 76;   // the screen's readouts and overview, and a sliver of waveform
    int gridCellHeight = 20;
};

struct ConsoleLayout
{
    // The name bar.
    juce::Rectangle<int> number, source, live, edit, reload, eject;
    juce::Rectangle<int> display;
    std::array<juce::Rectangle<int>, 8> hotCues;
    // The left column.
    juce::Rectangle<int> loopIn, loopOut, reloop, beatLoop, loopHalve, loopDouble, jumpBack, jumpSize, jumpForward,
                         searchBack, searchForward, reverse, quantise, cue, play, grid;
    // The right column.
    juce::Rectangle<int> sync, master, tempoRange, tempo, tempoReset, tempoCaption;
    // The centre, all empty when the jog wheel does not show.
    juce::Rectangle<int> jog, vinyl, brake, brakeCaption;
    bool showsJog = false;
};

// Cells across a row: widths are weights, a gap of three between them.
template <size_t count>
std::array<juce::Rectangle<int>, count> spread(juce::Rectangle<int> row, const std::array<int, count>& weights)
{
    std::array<juce::Rectangle<int>, count> cells {};
    int total = 0;
    for (const auto weight : weights) total += weight;
    if (total <= 0) return cells;
    constexpr int gap = 3;
    const auto usable = row.getWidth() - gap * (static_cast<int>(count) - 1);
    auto x = row.getX();
    for (size_t i = 0; i < count; ++i)
    {
        const auto width = usable * weights[i] / total;
        cells[i] = {x, row.getY(), width, row.getHeight()};
        x += width + gap;
    }
    return cells;
}

// The screen takes the top, the hot cues sit under it, and the body below is
// the CDJ's three columns: keys, the jog wheel, the tempo. The screen takes
// about a third of the console and no more than 220 px; the body - the keys,
// the jog wheel and the clips grid - takes the rest, and never less than the
// keys need while the screen can give it. The keys at full size want six
// rows and two big round keys; a short console squeezes the rows before it
// touches the screen, and the jog wheel is the first thing to go.
inline ConsoleLayout layOutConsole(const ConsoleSpace& space)
{
    ConsoleLayout layout;
    auto bounds = space.bounds;
    auto header = bounds.removeFromTop(headerHeight);
    layout.number = header.withWidth(26);
    header = header.withTrimmedLeft(28).reduced(2);
    layout.eject = header.removeFromRight(46);
    header.removeFromRight(3);
    layout.reload = header.removeFromRight(52);
    header.removeFromRight(3);
    layout.edit = header.removeFromRight(44);
    header.removeFromRight(3);
    layout.live = header.removeFromRight(44);
    header.removeFromRight(3);
    layout.source = header;
    bounds.reduce(4, 2);
    constexpr int fullRow = 20, fullGap = 3, bigFull = 52, bigCompact = 30;
    constexpr int fullKeys = fullRow * 6 + fullGap * 7 + bigFull * 2 + 6;
    const auto screenMinimum = space.screenMinimum;
    const auto padsHeight = bounds.getHeight() < 320 ? 16 : padRowHeight;
    const auto available = bounds.getHeight() - padsHeight - 3;
    const auto screen = juce::jlimit(std::min(screenMinimum, std::max(0, available)), std::max(screenMinimum, 220),
                                     available * 32 / 100);
    const auto body = juce::jlimit(std::min(fullKeys, std::max(0, available - screenMinimum)),
                                   std::max(fullKeys, available - screenMinimum), available - screen);
    const auto compact = body < fullKeys;
    const auto bigKey = compact ? bigCompact : bigFull;
    const auto gap = compact ? 2 : fullGap;
    const auto row = juce::jlimit(12, fullRow, (body - bigKey * 2 - 6 - gap * 7) / 6);
    auto bodyArea = bounds.removeFromBottom(body);
    std::array<int, 8> equal {};
    equal.fill(1);
    layout.hotCues = spread(bounds.removeFromBottom(padsHeight), equal);
    bounds.removeFromBottom(3);
    layout.display = bounds;

    bodyArea.removeFromTop(gap);
    layout.showsJog = bodyArea.getHeight() >= 150 && bodyArea.getWidth() >= 230;
    const auto width = bodyArea.getWidth();
    const auto leftWidth = layout.showsJog ? std::max(96, width * 30 / 100) : width * 58 / 100;
    const auto rightWidth = layout.showsJog ? std::max(64, width * 20 / 100) : width - leftWidth - 4;
    auto left = bodyArea.removeFromLeft(leftWidth);
    auto right = bodyArea.removeFromRight(rightWidth);
    auto centre = bodyArea.reduced(4, 0);
    const auto take = [row, gap](juce::Rectangle<int>& column)
    {
        auto next = column.removeFromTop(row);
        column.removeFromTop(gap);
        return next;
    };
    // The left column: loops, jump, cue search, direction, then the big keys.
    const auto loops = spread(take(left), std::array<int, 2> {1, 1});
    layout.loopIn = loops[0];
    layout.loopOut = loops[1];
    layout.reloop = take(left);
    const auto lengths = spread(take(left), std::array<int, 3> {3, 1, 1});
    layout.beatLoop = lengths[0];
    layout.loopHalve = lengths[1];
    layout.loopDouble = lengths[2];
    const auto jumps = spread(take(left), std::array<int, 3> {1, 1, 1});
    layout.jumpBack = jumps[0];
    layout.jumpSize = jumps[1];
    layout.jumpForward = jumps[2];
    const auto search = spread(take(left), std::array<int, 2> {1, 1});
    layout.searchBack = search[0];
    layout.searchForward = search[1];
    const auto direction = spread(take(left), std::array<int, 2> {1, 1});
    layout.reverse = direction[0];
    layout.quantise = direction[1];
    // The clips grid stands beside the big keys when the column is wide
    // enough for both, else above them, and only on a track deck.
    const auto keySize = std::min(bigKey, left.getWidth());
    const auto beside = space.gridShown && left.getWidth() >= keySize + 70;
    auto keysColumn = beside ? left.removeFromRight(keySize + 4) : left;
    auto keys = keysColumn.removeFromBottom(keySize * 2 + 6);
    layout.cue = keys.removeFromTop(keySize).withSizeKeepingCentre(keySize, keySize);
    keys.removeFromTop(6);
    layout.play = keys.removeFromTop(keySize).withSizeKeepingCentre(keySize, keySize);
    juce::Rectangle<int> gridArea;
    if (beside)
        gridArea = left.withTrimmedRight(4);
    else if (space.gridShown)
        gridArea = left.withTrimmedBottom(keySize * 2 + 6 + 4);
    // A grid with no room for a cell takes empty bounds inside the console.
    layout.grid = gridArea.getHeight() >= space.gridCellHeight ? gridArea : juce::Rectangle<int>();
    // The right column: sync, master, the range, the fader, its reset.
    layout.sync = take(right);
    layout.master = take(right);
    layout.tempoRange = take(right);
    layout.tempoReset = right.removeFromBottom(row);
    right.removeFromBottom(gap);
    right.removeFromTop(12);   // the TEMPO caption
    layout.tempo = right.withSizeKeepingCentre(std::min(28, right.getWidth()), right.getHeight());
    layout.tempoCaption = layout.tempo.withHeight(10).translated(0, -11);
    // The centre: the platter, with vinyl and brake beneath it.
    if (!layout.showsJog)
        return layout;
    auto foot = centre.removeFromBottom(row + 20);
    const auto half = foot.getWidth() / 2;
    layout.vinyl = foot.withWidth(half - 4).withSizeKeepingCentre(std::min(60, half - 4), row).withY(foot.getY());
    layout.brake = juce::Rectangle<int>(28, 28).withCentre({foot.getX() + half + half / 2, foot.getY() + 14});
    layout.brakeCaption = layout.brake.withHeight(10).translated(0, layout.brake.getHeight() + 1);
    centre.removeFromBottom(4);
    const auto size = std::min(centre.getWidth(), centre.getHeight());
    layout.jog = centre.withSizeKeepingCentre(size, size);
    return layout;
}
}
