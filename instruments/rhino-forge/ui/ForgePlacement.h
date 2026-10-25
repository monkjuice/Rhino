#pragma once

#include "ForgeModules.h"
#include <limits>

// Where the window puts things: what size the panel may be, where the title
// bar, the keyboard and the deck plates sit, and the rectangle each module,
// display and row lands in. Pure arithmetic on a bounds rectangle — it reads
// the declared modules and owns no state.
namespace rhino::forge::ui
{
//
// The panel is laid out by weight rather than at fixed sizes, so these are the
// range over which that has been checked rather than sizes anything is designed
// against. The layout test sweeps the whole of it.
inline constexpr int minPanelWidth = 1180;
inline constexpr int minPanelHeight = 820;
inline constexpr int maxPanelWidth = 2000;
inline constexpr int maxPanelHeight = 1180;

// Forge is a wide panel, and the limits say so: at every size it allows it is
// broader than it is tall. Two rows of modules over a keyboard is a shape that
// wants width, and height is what a plugin window inside a host has least of.
inline constexpr int defaultPanelWidth = 1440;
inline constexpr int defaultPanelHeight = 900;

// Eighty-eight since 2026-10-25, up from eighty: the keys were short for their
// width and their names had no room to be read. The eight pixels come off the
// two rows of modules, and eight is all there is: at the smallest window the
// signal row's 56% of what is left has to hold the FX rack's three 92-pixel
// strips whole, and at eighty it had four pixels over (forge_layout checks it).
inline constexpr int keyboardHeight = 88;

// The inset every edge of the panel shares: the wordmark, the preset controls,
// the modules and the keyboard all start here, so they line up down both sides.
// It is deliberately small, so the modules get the width instead of the frame.
inline constexpr int windowMargin = 12;

// Measured from the top of the window rather than from the margin, so the
// header keeps its own spacing when the margin changes.
inline constexpr int titleBarHeight = 104;
inline constexpr int keyboardGap = 22;

// The full width the keyboard and the two end plates beside it share.
inline juce::Rectangle<int> keyboardStrip(juce::Rectangle<int> bounds)
{
    return bounds.reduced(windowMargin, 0)
        .withTop(bounds.getBottom() - keyboardGap - keyboardHeight)
        .withHeight(keyboardHeight);
}

// The plates either side of the keyboard. The left holds the performance
// wheels, while the right carries the maker's marking. Both give up width
// before the keys do, keeping the keyboard's scale consistent on resize.
inline constexpr int deckPlateGap = 8;

inline int deckPlateWidth(juce::Rectangle<int> bounds)
{
    return juce::jlimit(72, 132, bounds.getWidth() / 11);
}

inline juce::Rectangle<int> deckLeftBounds(juce::Rectangle<int> bounds)
{
    return keyboardStrip(bounds).withWidth(deckPlateWidth(bounds));
}

inline juce::Rectangle<int> deckRightBounds(juce::Rectangle<int> bounds)
{
    const auto strip = keyboardStrip(bounds);
    return strip.withLeft(strip.getRight() - deckPlateWidth(bounds));
}

// What the strip has left once both end plates have taken theirs: the keys and
// the ARP plate share this between them.
inline juce::Rectangle<int> keyboardShelf(juce::Rectangle<int> bounds)
{
    const auto flank = deckPlateWidth(bounds) + deckPlateGap;
    return keyboardStrip(bounds).withTrimmedLeft(flank).withTrimmedRight(flank);
}

// The keyboard was eighty-eight keys and is now seventy-six: the bottom octave
// came off, and the ARP plate is the width it was. Both counts are kept because
// the plate is sized as the difference between them. The keys themselves are
// divided out of whatever the plates leave, so they grew when the note guide
// became a door on 2026-10-25 and handed the keys the width its four fields had.
inline constexpr int keyboardWhiteKeys = 38;       // A0 to C6 as the keys are labelled (middle C is C3)
inline constexpr int keyboardWhiteKeysWithoutArp = 52;
inline constexpr int keyboardLowestNote = 33;      // A0
inline constexpr int keyboardHighestNote = 96;     // C6; MIDI itself remains unrestricted
inline constexpr int arpPlateGap = 8;
inline constexpr int noteGuidePlateGap = 8;

inline int arpPlateWidth(juce::Rectangle<int> bounds)
{
    const auto octave = keyboardShelf(bounds).getWidth()
        * (keyboardWhiteKeysWithoutArp - keyboardWhiteKeys) / keyboardWhiteKeysWithoutArp;
    return juce::jmax(48, octave - arpPlateGap);
}

// The note guide's plate, at the strip's right-hand end where the second deck
// plate would be. It is a door, as the ARP plate's name is: it says what the
// guide is set to and opens the guide over the modulators, so it needs only
// the width of what it says. 168 holds "Cm(maj7)" and "MIXOLYDIAN" at their
// sizes; the layout test measures them.
inline int noteGuidePlateWidth(juce::Rectangle<int> bounds)
{
    return juce::jlimit(168, 210, keyboardShelf(bounds).getWidth() / 6);
}

inline juce::Rectangle<int> noteGuidePlateBounds(juce::Rectangle<int> bounds)
{
    const auto strip = keyboardStrip(bounds);
    return strip.withLeft(strip.getRight() - noteGuidePlateWidth(bounds));
}

// The well inside that plate, which is the door itself: the housing lets it in
// and the lit state is drawn to its outline, so both read this one rectangle
// and this one cut.
inline juce::Rectangle<int> noteGuideFace(juce::Rectangle<int> bounds)
{
    return noteGuidePlateBounds(bounds).reduced(6);
}

inline constexpr float noteGuideFaceCut = 5.0f;

// The ARP plate stands between the keys and the note guide. Serum puts its
// switch at the keyboard's left end and so did this until 2026-10-13; it moved
// to the right so the lamp, the slot buttons and the chord helper are found
// together at the same end, and so the keys start where the wheels end.
inline juce::Rectangle<int> arpPlateBounds(juce::Rectangle<int> bounds)
{
    const auto width = arpPlateWidth(bounds);
    return keyboardShelf(bounds).withX(noteGuidePlateBounds(bounds).getX() - noteGuidePlateGap - width)
                                .withWidth(width);
}

// The keys themselves: whatever the shelf has left once the ARP plate and the
// note guide have taken theirs. They start at the shelf's left edge, against
// the wheels.
inline juce::Rectangle<int> keyboardBounds(juce::Rectangle<int> bounds)
{
    return keyboardShelf(bounds).withRight(arpPlateBounds(bounds).getX() - arpPlateGap);
}

// The face inside the plate's housing: the well everything on the plate stands
// in. The lit state is drawn to its outline, so the two are worked out from
// the one rectangle and the one cut.
inline juce::Rectangle<int> arpPlateFace(juce::Rectangle<int> bounds)
{
    return arpPlateBounds(bounds).reduced(7, 9);
}

inline constexpr float arpPlateFaceCut = 5.0f;

// The power circle on that plate. Pressing it switches the arp on; pressing the
// name beside it, or any of the plate the buttons and the well do not cover,
// opens its settings. Two things one plate, exactly as Serum's is, because "is
// it running" and "let me see it" are different questions and a patch asks
// them at different times.
inline juce::Rectangle<int> arpPlateLedBounds(juce::Rectangle<int> bounds)
{
    const auto plate = arpPlateBounds(bounds);
    return juce::Rectangle<int>(plate.getX() + 13, plate.getCentreY() - 9, 18, 18);
}

// The column carrying the lamp, the name and the word under it: the door.
inline constexpr int arpPlateNameWidth = 72;

inline juce::Rectangle<int> arpPlateNameBounds(juce::Rectangle<int> bounds)
{
    return arpPlateFace(bounds).withWidth(arpPlateNameWidth);
}

// The four slot buttons, two by two, after the name. Square, and sized by the
// face's height: two rows of them have to fit in it with their gap, and they
// are not worth more than 24 pixels a side.
inline constexpr int arpSlotButtonGap = 4;

inline juce::Rectangle<int> arpSlotGridBounds(juce::Rectangle<int> bounds)
{
    const auto face = arpPlateFace(bounds);
    const auto side = juce::jlimit(14, 24, (face.getHeight() - arpSlotButtonGap) / 2);
    const auto size = side * 2 + arpSlotButtonGap;
    return {face.getX() + arpPlateNameWidth + 12, face.getCentreY() - size / 2, size, size};
}

inline juce::Rectangle<int> arpSlotButtonBounds(juce::Rectangle<int> bounds, int slot)
{
    const auto grid = arpSlotGridBounds(bounds);
    const auto side = (grid.getWidth() - arpSlotButtonGap) / 2;
    const auto column = slot % 2, row = slot / 2;
    return {grid.getX() + column * (side + arpSlotButtonGap),
            grid.getY() + row * (side + arpSlotButtonGap), side, side};
}

// Which slot button is under a point, or -1.
inline int arpSlotAt(juce::Rectangle<int> bounds, juce::Point<int> at)
{
    for (int slot = 0; slot < arpSlotCount; ++slot)
        if (arpSlotButtonBounds(bounds, slot).contains(at)) return slot;
    return -1;
}

// The well the notes fall through: whatever the face has left past the slot
// buttons. It gives up width first when the window narrows, because a spark is
// a spark at any width and a button is not.
inline juce::Rectangle<int> arpWaterfallBounds(juce::Rectangle<int> bounds)
{
    const auto face = arpPlateFace(bounds);
    return face.withLeft(arpSlotGridBounds(bounds).getRight() + 10).withTrimmedRight(5).reduced(0, 4);
}

// --- The title bar's right-hand end -----------------------------------------
//
// The preset controls sit in the lower-right cut-out of the maker assembly,
// with the red unit mark on a separate plate above them.
inline constexpr int presetButtonWidth = 62;
inline constexpr int presetButtonHeight = 26;
inline constexpr int presetRowTop = 58;

inline juce::Rectangle<int> presetButtonBounds(juce::Rectangle<int> bounds, bool save)
{
    const auto right = bounds.getRight() - 30;
    return {right - presetButtonWidth - (save ? 0 : presetButtonWidth + 8),
            presetRowTop, presetButtonWidth, presetButtonHeight};
}

inline juce::Rectangle<int> presetLabelBounds(juce::Rectangle<int> bounds)
{
    const auto left = headerSplit(bounds.getWidth());
    const auto nameLeft = left + (bounds.getWidth() - left) * 49 / 100;
    return {nameLeft, presetRowTop, presetButtonBounds(bounds, false).getX() - 8 - nameLeft,
            presetButtonHeight};
}

// The mark at the very top right: what this unit is, in the two lines a plate
// riveted to a machine would carry.
inline juce::Rectangle<int> unitMarkBounds(juce::Rectangle<int> bounds)
{
    const auto right = bounds.getRight() - 42;
    return juce::Rectangle<int>(right - 110, 13, 110, 32);
}

// The complete right-hand assembly contains the maker's plate, unit cap and
// preset shelf. Its seam moves with the left header plate when resized.
inline juce::Rectangle<int> identityPlateBounds(juce::Rectangle<int> bounds)
{
    const auto left = headerSplit(bounds.getWidth());
    return {left, 10, bounds.getWidth() - left - 12, 80};
}

// The area the modules are laid out inside: below the title bar, above the
// keyboard.
inline juce::Rectangle<int> contentBounds(juce::Rectangle<int> bounds)
{
    return bounds.reduced(windowMargin)
        .withTrimmedTop(titleBarHeight - windowMargin)
        .withBottom(bounds.getBottom() - keyboardGap - keyboardHeight - 12);
}

inline juce::Rectangle<int> moduleBounds(juce::Rectangle<int> bounds, const Module& module)
{
    const auto content = contentBounds(bounds);
    const auto& weights = rowWeights();
    auto total = 0;
    for (const auto weight : weights) total += weight;
    const auto rows = static_cast<int>(weights.size());
    const auto available = content.getHeight() - moduleGap * (rows - 1);

    auto y = content.getY();
    for (int row = 0; row < module.row; ++row)
        y += available * weights[static_cast<size_t>(row)] / total + moduleGap;
    // A module spanning grid rows swallows the gaps between them, so the macro
    // column runs unbroken past the two rows of modules it stands beside.
    auto height = 0;
    for (int row = module.row; row < module.row + module.rowSpan; ++row)
    {
        if (row > module.row) height += moduleGap;
        height += available * weights[static_cast<size_t>(row)] / total;
    }

    const auto columnWidth = static_cast<float>(content.getWidth()) / static_cast<float>(gridColumns);
    const auto x = content.getX() + juce::roundToInt(module.column * columnWidth);
    // Work both edges from the same origin. Adjacent fractional columns then
    // round to the same pixel and retain the exact gap their plates reduce by.
    const auto reachesRightEdge = module.column + module.columnSpan
                               >= static_cast<float>(gridColumns) - 0.001f;
    const auto right = reachesRightEdge
        ? content.getRight()
        : content.getX() + juce::roundToInt((module.column + module.columnSpan) * columnWidth);
    return juce::Rectangle<int>(x, y, right - x, height).reduced(moduleGap / 2, 0);
}

// The ordinary FX page occupies the signal row. Expanded, it occupies the
// complete module field above the keyboard and hides the modulators underneath
// it. This is view geometry only: all controls remain attached to the same
// parameters and the engine sees no state change.
inline juce::Rectangle<int> fxModuleBounds(juce::Rectangle<int> bounds, const Module& module,
                                           bool expanded)
{
    return expanded ? contentBounds(bounds) : moduleBounds(bounds, module);
}

// Everything on a plate between its header and the margin along its foot.
// Every carve of a module's area begins here, so the margin is reserved in one
// place and no caller can forget it.
inline juce::Rectangle<int> moduleInterior(juce::Rectangle<int> moduleArea)
{
    return moduleArea.withTrimmedTop(headerHeight).withTrimmedBottom(plateFootMargin);
}

// How far a grouped module's own panel sits inside the area it was given, so
// the plate underneath shows around it.
inline constexpr int innerPanelInset = 3;

// The plate a group shares: everything its members between them occupy. Worked
// out from the members rather than declared, so a module moving column moves
// the plate with it.
inline juce::Rectangle<int> groupBounds(juce::Rectangle<int> bounds, const char* group, Page page)
{
    juce::Rectangle<int> plate;
    for (const auto& module : modules())
    {
        if (module.group == nullptr || juce::String(module.group) != group) continue;
        if (!onPage(module, page)) continue;
        const auto area = moduleBounds(bounds, module);
        plate = plate.isEmpty() ? area : plate.getUnion(area);
    }
    return plate;
}

inline int fxListWidth(bool open) { return open ? fxListOpenWidth : fxListFoldedWidth; }

inline juce::Rectangle<int> fxListBounds(juce::Rectangle<int> moduleArea, bool open)
{
    return moduleInterior(moduleArea).reduced(8, 6)
        .withWidth(fxListWidth(open));
}

// A synthetic module rectangle for the controls on the right of the list. It
// retains the real header position, so all existing row/control geometry can
// be reused without teaching it about the sidebar. The strip down its right
// edge is the rack's own scroll rail, kept whether or not there is anything to
// scroll so the knobs do not move when a ninth row would have been needed.
inline juce::Rectangle<int> fxRackBounds(juce::Rectangle<int> moduleArea, bool listOpen)
{
    return moduleArea.withTrimmedLeft(fxListWidth(listOpen) + fxListGap)
        .withTrimmedRight(fxRackScrollGutter);
}

// Everything between the header and the foot. The list and the rack each cut
// their own window from it and scroll it separately, so nothing across the two
// has to share a baseline.
inline juce::Rectangle<int> fxViewportBounds(juce::Rectangle<int> moduleArea)
{
    return moduleInterior(moduleArea).reduced(0, 6);
}

// The rack's window onto its slots: beside the list, the full height.
inline juce::Rectangle<int> fxSlotViewportBounds(juce::Rectangle<int> moduleArea, bool listOpen)
{
    return fxViewportBounds(moduleArea).getIntersection(fxRackBounds(moduleArea, listOpen));
}

inline juce::Rectangle<int> fxRackScrollBounds(juce::Rectangle<int> moduleArea)
{
    return fxViewportBounds(moduleArea)
        .withLeft(moduleArea.getRight() - fxRackScrollGutter).withWidth(3).reduced(0, 4);
}

inline juce::Rectangle<int> fxAddButtonBounds(juce::Rectangle<int> moduleArea, bool listOpen)
{
    return fxListBounds(moduleArea, listOpen).withHeight(fxListAddHeight).reduced(4, 3);
}

// The list's window onto its rows, under the add action that stays put above
// them.
inline juce::Rectangle<int> fxListViewportBounds(juce::Rectangle<int> moduleArea, bool listOpen)
{
    return fxListBounds(moduleArea, listOpen).withTrimmedTop(fxListAddHeight).withTrimmedBottom(3);
}

inline int fxListVisibleRowCount(juce::Rectangle<int> moduleArea)
{
    return juce::jlimit(1, fxSlotCount,
                        fxListViewportBounds(moduleArea, true).getHeight() / fxListRowHeight);
}

inline int fxListIntersectingRowCount(juce::Rectangle<int> moduleArea)
{
    const auto height = fxListViewportBounds(moduleArea, true).getHeight();
    return juce::jlimit(1, fxSlotCount, (height + fxListRowHeight - 1) / fxListRowHeight);
}

inline int fxListMaxFirstRow(juce::Rectangle<int> moduleArea, int rowCount = fxSlotCount)
{
    return juce::jmax(0, rowCount - fxListVisibleRowCount(moduleArea));
}

// One row of the list, inset from its border so a selected row's outline is
// not drawn on top of the list's own.
inline juce::Rectangle<int> fxListItemBounds(juce::Rectangle<int> moduleArea, int displayRow,
                                             bool listOpen, int firstRow = 0)
{
    const auto viewport = fxListViewportBounds(moduleArea, listOpen).reduced(4, 0);
    return {viewport.getX(), viewport.getY() + (displayRow - firstRow) * fxListRowHeight,
            viewport.getWidth(), fxListRowHeight};
}

inline int fxVisibleSlotCount(juce::Rectangle<int> moduleArea)
{
    return juce::jlimit(1, fxSlotCount, fxViewportBounds(moduleArea).getHeight() / fxSlotHeight);
}

// Controls in the row crossing the bottom edge still belong on screen. The
// viewport clips them at its boundary; this count is only about intersection,
// whereas fxVisibleSlotCount deliberately remains the number of complete rows
// used by scrolling and its thumb.
inline int fxIntersectingSlotCount(juce::Rectangle<int> moduleArea)
{
    const auto height = fxViewportBounds(moduleArea).getHeight();
    return juce::jlimit(1, fxSlotCount, (height + fxSlotHeight - 1) / fxSlotHeight);
}

inline int fxMaxFirstSlot(juce::Rectangle<int> moduleArea, int slotCount = fxSlotCount)
{
    return juce::jmax(0, slotCount - fxVisibleSlotCount(moduleArea));
}

inline juce::Rectangle<int> fxScrolledRackBounds(juce::Rectangle<int> moduleArea, bool listOpen,
                                                 int firstSlot)
{
    return fxRackBounds(moduleArea, listOpen).translated(0, -firstSlot * fxSlotHeight);
}

// View controls live at the far right of the rack header: the outer one grows
// the rack through both module rows, the inner one folds the list to its icon
// rail. They are painted rather than heavyweight child components because they
// are view state, are hit only on the panel background and never automate.
inline juce::Rectangle<int> fxExpandButtonBounds(juce::Rectangle<int> moduleArea)
{
    return {moduleArea.getRight() - 8 - fxViewButtonSize,
            moduleArea.getY() + (headerHeight - fxViewButtonSize) / 2,
            fxViewButtonSize, fxViewButtonSize};
}

inline juce::Rectangle<int> fxListButtonBounds(juce::Rectangle<int> moduleArea)
{
    return fxExpandButtonBounds(moduleArea).translated(-fxViewButtonSize - fxViewButtonGap, 0);
}

inline juce::Rectangle<int> fxListBypassBounds(juce::Rectangle<int> item)
{
    return item.withLeft(item.getRight() - 48).withWidth(20).withSizeKeepingCentre(20, 20);
}

inline juce::Rectangle<int> fxListRemoveBounds(juce::Rectangle<int> item)
{
    return item.withLeft(item.getRight() - 24).withWidth(20).withSizeKeepingCentre(20, 20);
}

// Everything inside a module below its header and its display: what the column
// titles, the row gutter and the control rows divide between them.
inline juce::Rectangle<int> moduleBody(juce::Rectangle<int> moduleArea, const Module& module)
{
    auto body = moduleInterior(moduleArea).reduced(10, 6);
    if (module.display != Display::none)
    {
        body.removeFromTop(body.getHeight() * displayShareOf(module) / 100);
        body.removeFromTop(4);
    }
    return body;
}

// The strip a module's display occupies, or an empty rectangle when it has none.
inline juce::Rectangle<int> displayBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    if (module.display == Display::none) return {};
    auto body = moduleInterior(moduleArea).reduced(10, 6);
    return body.removeFromTop(body.getHeight() * displayShareOf(module) / 100);
}

// --- What a display is divided into ------------------------------------------
//
// A display with seated rows is three things stacked: the strips along its top
// edge, the plot, and the strips along its foot. One walk carves all of them
// out of the one rectangle, so the plot cannot disagree with a strip about
// where either ends — which is what the layout test intersects them to check.
//
// `wanted` is the row whose strip is being asked for, or -1 for the plot, which
// is simply whatever the strips left.
//
// `mode` is what the module's mode parameter reads, and it decides only which
// gated rows the *plot* gives up room to. The strips that are always there are
// carved first; a gated strip is carved after them, as if it were the only
// gated one, whenever it is the strip being asked for. So a gated row lands in
// the same place in every mode, no ungated row ever moves, and -1 — no mode —
// is a plot with no gated strip taken out of it.
inline juce::Rectangle<int> displayCarve(juce::Rectangle<int> moduleArea, const Module& module,
                                         int wanted, int mode = -1)
{
    if (module.display == Display::none) return {};
    auto inner = displayBounds(moduleArea, module).reduced(displaySeatInset);
    for (const auto gated : {false, true})
        for (int i = 0; i < static_cast<int>(module.rows.size()); ++i)
        {
            const auto& row = module.rows[static_cast<size_t>(i)];
            if (row.seat == Seat::body || row.seat == Seat::header) continue;
            if ((row.modeBy != nullptr) != gated) continue;
            if (gated && i != wanted && !(wanted < 0 && row.modeIs == mode)) continue;
            const auto atTop = row.seat == Seat::displayTop;
            // Never more than the display has: a window small enough to make
            // the plot vanish should take the plot, not hand a strip a negative
            // height and let it wander out of the well.
            const auto height = juce::jlimit(0, juce::jmax(0, inner.getHeight()), row.weight);
            const auto strip = atTop ? inner.removeFromTop(height) : inner.removeFromBottom(height);
            if (i == wanted) return strip;
            if (atTop) inner.removeFromTop(displaySeatGap);
            else       inner.removeFromBottom(displaySeatGap);
        }
    return wanted < 0 ? inner : juce::Rectangle<int>();
}

// A row seated in the module's header: the strip between the title plate and
// the module's right edge.
//
// There is only ever one such row and it holds one control, so unlike
// displayCarve there is nothing to divide — the field takes what the title
// leaves. The title gives up width to it rather than the other way round; see
// headerBadgeWidth.
inline juce::Rectangle<int> headerCarve(juce::Rectangle<int> moduleArea, const Module& module)
{
    const auto left = moduleArea.getX() + 6 + headerBadgeWidth(moduleArea, module) + 6;
    const auto right = moduleArea.getRight() - 12;
    return {left, moduleArea.getY() + 5, juce::jmax(0, right - left), headerHeight - 11};
}

// What the display has left for the thing it is a display *of*. Everything that
// draws into a display asks for this rather than for displayBounds, so a curve
// can never be plotted underneath the controls seated on top of it. A module
// with a mode says which one it is drawing; see displayCarve.
inline juce::Rectangle<int> displayPlotBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                              int mode = -1)
{
    return displayCarve(moduleArea, module, -1, mode);
}

// The parameter an oscillator's display draws: the first knob of the module,
// which is POSITION by declaration in every oscillator.
inline const char* displaySourceId(const Module& module)
{
    for (const auto& row : module.rows)
        for (const auto& control : row.controls)
            if (control.style == Style::knob) return control.id;
    return nullptr;
}

// The strip carrying a table's column titles, or an empty rectangle when the
// module is not a table. It spans the gutter too, so the row numbers get a
// heading of their own.
inline juce::Rectangle<int> columnTitleBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    if (module.columnHeaderHeight <= 0) return {};
    return moduleBody(moduleArea, module).withHeight(module.columnHeaderHeight);
}

// Everything below the header, the display and the column titles, and right of
// the row gutter: the area the control rows share.
inline juce::Rectangle<int> controlArea(juce::Rectangle<int> moduleArea, const Module& module)
{
    auto body = moduleBody(moduleArea, module);
    body.removeFromTop(module.columnHeaderHeight);
    body.removeFromLeft(module.rowGutter);
    return body;
}

// --- The matrix ---------------------------------------------------------------
//
// Thirty slots, and a table that shows eight of them to begin with. A row is a
// fixed height rather than a share of the module, as a rack strip is, so the
// table scrolls past the window it has instead of squeezing thirty fields into
// it. The height is what eight rows would get, so the table opens looking
// exactly as the eight-row one did and gains a scroll rather than a squeeze
// when a ninth routing arrives.
inline constexpr int matrixDefaultRows = 8;
inline constexpr int minMatrixRowHeight = 30;
inline constexpr int maxMatrixRowHeight = 40;
// The strip down the table's right edge its scroll thumb runs in, kept whether
// or not there is anything to scroll so the fields do not move when a ninth
// row arrives.
inline constexpr int matrixScrollGutter = 8;

inline bool isMatrixModule(const Module& module)
{
    return module.id != nullptr && juce::String(module.id) == "matrix";
}

inline int matrixRowHeight(juce::Rectangle<int> moduleArea, const Module& module)
{
    return juce::jlimit(minMatrixRowHeight, maxMatrixRowHeight,
                        controlArea(moduleArea, module).getHeight() / matrixDefaultRows);
}

// How many whole rows the table's window holds. Whole rows only: a field cut
// in half at the window's foot would read as a broken control, where a rack
// strip cut in half reads as the next shelf.
inline int matrixVisibleRowCount(juce::Rectangle<int> moduleArea, const Module& module)
{
    return juce::jlimit(1, modSlotCount,
                        controlArea(moduleArea, module).getHeight() / matrixRowHeight(moduleArea, module));
}

// How many rows the table shows for a patch whose last slot with either end
// set is `lastOccupiedSlot` (-1 for none): eight to begin with, and past that
// one blank row after the last one in use, so there is always a row to set a
// routing up in by hand until the thirty are spent.
inline int matrixShownRowCount(int lastOccupiedSlot)
{
    return juce::jlimit(matrixDefaultRows, modSlotCount, lastOccupiedSlot + 2);
}

inline int matrixMaxFirstRow(juce::Rectangle<int> moduleArea, const Module& module, int shownRows)
{
    return juce::jmax(0, shownRows - matrixVisibleRowCount(moduleArea, module));
}

// The module's rectangle moved up by the rows scrolled past, so the row
// geometry below lays the rows showing where the window is. Everything but
// the rows -- the header, the column titles, the rail -- is laid out from the
// unmoved area.
inline juce::Rectangle<int> matrixScrolledBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                                 int firstRow)
{
    return moduleArea.translated(0, -firstRow * matrixRowHeight(moduleArea, module));
}

inline juce::Rectangle<int> matrixScrollBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    const auto area = controlArea(moduleArea, module);
    return area.withLeft(area.getRight() - matrixScrollGutter).withWidth(3).reduced(0, 4);
}

inline juce::Rectangle<int> rowBounds(juce::Rectangle<int> moduleArea, const Module& module, int rowIndex)
{
    // A seated row is laid out in the display rather than in the body, and is
    // absent from the division below — which is what lets the body's rows be
    // declared as shares of what is actually theirs.
    const auto seat = module.rows[static_cast<size_t>(rowIndex)].seat;
    if (seat == Seat::header) return headerCarve(moduleArea, module);
    if (seat != Seat::body) return displayCarve(moduleArea, module, rowIndex);

    const auto area = controlArea(moduleArea, module);
    if (juce::String(module.id) == "fx")
        return {area.getX(), area.getY() + rowIndex * fxSlotHeight, area.getWidth(), fxSlotHeight};
    // The matrix's rows run on past the window at their fixed height, exactly
    // as the rack's do, and leave the rail its strip.
    if (isMatrixModule(module))
    {
        const auto height = matrixRowHeight(moduleArea, module);
        return {area.getX(), area.getY() + rowIndex * height, area.getWidth() - matrixScrollGutter, height};
    }

    auto total = 0;
    for (const auto& row : module.rows)
        if (row.seat == Seat::body) total += row.weight;
    if (total <= 0) return area;

    auto y = area.getY();
    for (int i = 0; i < rowIndex; ++i)
        if (module.rows[static_cast<size_t>(i)].seat == Seat::body)
            y += area.getHeight() * module.rows[static_cast<size_t>(i)].weight / total;
    const auto height = area.getHeight() * module.rows[static_cast<size_t>(rowIndex)].weight / total;
    return {area.getX(), y, area.getWidth(), height};
}

// Which of an oscillator's rows the table field is seated in, or -1 for a
// module without one: the wavetable mode's strip along the top of the display,
// declared with no controls because the editor seats its own field there.
inline int tableStripRow(const Module& module)
{
    if (module.display != Display::oscillator) return -1;
    for (int i = 0; i < static_cast<int>(module.rows.size()); ++i)
    {
        const auto& row = module.rows[static_cast<size_t>(i)];
        if (row.seat == Seat::displayTop && row.modeBy != nullptr && row.controls.empty()
            && row.modeIs == static_cast<int>(OscMode::wavetable))
            return i;
    }
    return -1;
}

// The picture tube a wavetable oscillator draws on: the display less the table
// strip along its top. Only the top moves. The tube keeps the display's sides
// and foot, so the glass sits where it always has and the field reads as a lid
// on it rather than the picture shrinking all round.
inline juce::Rectangle<int> wavetableTubeBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    const auto display = displayBounds(moduleArea, module);
    const auto row = tableStripRow(module);
    if (row < 0 || display.isEmpty()) return display;
    const auto strip = rowBounds(moduleArea, module, row);
    return display.withTop(juce::jmin(display.getBottom(), strip.getBottom() + displaySeatGap));
}

// The strip to the left of a table row, where its number is drawn.
inline juce::Rectangle<int> rowGutterBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                            int rowIndex)
{
    if (module.rowGutter <= 0) return {};
    const auto row = rowBounds(moduleArea, module, rowIndex);
    return {row.getX() - module.rowGutter, row.getY(), module.rowGutter, row.getHeight()};
}

// How many controls one bank of a row declares. Every bank declares the same
// ones, so this is the length of the row divided between them.
//
// A row can be empty: a mixer strip declares the same four rows as the strips
// beside it so their faders land on one line, and leaves out whatever it has
// nothing to put in. Such a row has no controls per bank rather than one,
}
