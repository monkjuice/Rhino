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

inline constexpr int keyboardHeight = 80;

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

// What the strip has left once both end plates have taken theirs: the ARP plate and
// the keys share this between them.
inline juce::Rectangle<int> keyboardShelf(juce::Rectangle<int> bounds)
{
    const auto flank = deckPlateWidth(bounds) + deckPlateGap;
    return keyboardStrip(bounds).withTrimmedLeft(flank).withTrimmedRight(flank);
}

// The keyboard was eighty-eight keys and is now seventy-six: the bottom octave
// came off, and the ARP plate stands where it was. Both counts are kept because
// the plate is sized as the difference between them — the keys are divided out
// of the span they always had, so every key is exactly the width it was before
// the arp existed and the octave is what actually pays for the plate.
inline constexpr int keyboardWhiteKeys = 45;       // A1 to C8
inline constexpr int keyboardWhiteKeysWithoutArp = 52; // A0 to C8, as it was
inline constexpr int keyboardLowestNote = 33;      // A1
inline constexpr int keyboardHighestNote = 108;    // C8
inline constexpr int arpPlateGap = 8;

inline int arpPlateWidth(juce::Rectangle<int> bounds)
{
    const auto octave = keyboardShelf(bounds).getWidth()
        * (keyboardWhiteKeysWithoutArp - keyboardWhiteKeys) / keyboardWhiteKeysWithoutArp;
    return juce::jmax(48, octave - arpPlateGap);
}

// Serum puts the ARP switch immediately left of the keys, and so does this: it
// is a thing you reach for with the hand that is already on the keyboard. The
// plate outboard of it holds the pitch and modulation wheels.
inline juce::Rectangle<int> arpPlateBounds(juce::Rectangle<int> bounds)
{
    return keyboardShelf(bounds).withWidth(arpPlateWidth(bounds));
}

// The power circle on that plate. Pressing it switches the arp on; pressing the
// rest of the plate opens its settings. Two things one plate, exactly as
// Serum's is, because "is it running" and "let me see it" are different
// questions and a patch asks them at different times.
inline juce::Rectangle<int> arpPlateLedBounds(juce::Rectangle<int> bounds)
{
    const auto plate = arpPlateBounds(bounds);
    return juce::Rectangle<int>(plate.getX() + 13, plate.getCentreY() - 9, 18, 18);
}

// The keys themselves: what the shelf has left once the ARP plate has taken
// its octave.
inline juce::Rectangle<int> keyboardBounds(juce::Rectangle<int> bounds)
{
    return keyboardShelf(bounds).withTrimmedLeft(arpPlateWidth(bounds) + arpPlateGap);
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

    const auto columnWidth = content.getWidth() / gridColumns;
    const auto x = content.getX() + module.column * columnWidth;
    // The last column in a row absorbs the integer-division remainder so the
    // right edge lines up with the content instead of drifting inward.
    const auto reachesRightEdge = module.column + module.columnSpan == gridColumns;
    const auto width = reachesRightEdge ? content.getRight() - x : module.columnSpan * columnWidth;
    return juce::Rectangle<int>(x, y, width, height).reduced(moduleGap / 2, 0);
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

// Everything on a plate between its header and the legend along its foot.
// Every carve of a module's area begins here, so the footer is reserved in one
// place and no caller can forget it.
inline juce::Rectangle<int> moduleInterior(juce::Rectangle<int> moduleArea)
{
    return moduleArea.withTrimmedTop(headerHeight).withTrimmedBottom(plateFooterHeight);
}

// The legend strip itself, inset to the same margin the header's title uses so
// the two line up down the plate's left edge.
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

inline juce::Rectangle<int> plateFooterBounds(juce::Rectangle<int> moduleArea)
{
    return moduleArea.withTop(moduleArea.getBottom() - plateFooterHeight).reduced(10, 0);
}

inline int fxListWidth(bool open) { return open ? fxListOpenWidth : fxListFoldedWidth; }

inline juce::Rectangle<int> fxListBounds(juce::Rectangle<int> moduleArea, bool open)
{
    return moduleInterior(moduleArea).reduced(8, 6)
        .withWidth(fxListWidth(open));
}

// A synthetic module rectangle for the controls on the right of the list. It
// retains the real header position, so all existing row/control geometry can
// be reused without teaching it about the sidebar.
inline juce::Rectangle<int> fxRackBounds(juce::Rectangle<int> moduleArea, bool listOpen)
{
    return moduleArea.withTrimmedLeft(fxListWidth(listOpen) + fxListGap);
}

// The shared vertical viewport for the overview and the editable rack. The
// horizontal pieces differ, but their slot edges must remain on one baseline.
inline juce::Rectangle<int> fxViewportBounds(juce::Rectangle<int> moduleArea)
{
    return moduleInterior(moduleArea).reduced(0, 6);
}

inline juce::Rectangle<int> fxSlotViewportBounds(juce::Rectangle<int> moduleArea)
{
    return fxViewportBounds(moduleArea).withTrimmedTop(fxListAddHeight);
}

inline juce::Rectangle<int> fxAddButtonBounds(juce::Rectangle<int> moduleArea, bool listOpen)
{
    return fxListBounds(moduleArea, listOpen).withHeight(fxListAddHeight).reduced(4, 3);
}

inline int fxVisibleSlotCount(juce::Rectangle<int> moduleArea)
{
    return juce::jlimit(1, fxSlotCount, fxSlotViewportBounds(moduleArea).getHeight() / fxSlotHeight);
}

// Controls in the row crossing the bottom edge still belong on screen. The
// viewport clips them at its boundary; this count is only about intersection,
// whereas fxVisibleSlotCount deliberately remains the number of complete rows
// used by scrolling and its thumb.
inline int fxIntersectingSlotCount(juce::Rectangle<int> moduleArea)
{
    const auto height = fxSlotViewportBounds(moduleArea).getHeight();
    return juce::jlimit(1, fxSlotCount, (height + fxSlotHeight - 1) / fxSlotHeight);
}

inline int fxMaxFirstSlot(juce::Rectangle<int> moduleArea, int slotCount = fxSlotCount)
{
    return juce::jmax(0, slotCount - fxVisibleSlotCount(moduleArea));
}

inline juce::Rectangle<int> fxScrolledRackBounds(juce::Rectangle<int> moduleArea, bool listOpen,
                                                 int firstSlot)
{
    return fxRackBounds(moduleArea, listOpen)
        .translated(0, fxListAddHeight - firstSlot * fxSlotHeight);
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
inline juce::Rectangle<int> displayCarve(juce::Rectangle<int> moduleArea, const Module& module,
                                         int wanted)
{
    if (module.display == Display::none) return {};
    auto inner = displayBounds(moduleArea, module).reduced(displaySeatInset);
    for (int i = 0; i < static_cast<int>(module.rows.size()); ++i)
    {
        const auto& row = module.rows[static_cast<size_t>(i)];
        if (row.seat == Seat::body) continue;
        const auto atTop = row.seat == Seat::displayTop;
        // Never more than the display has: a window small enough to make the
        // plot vanish should take the plot, not hand a strip a negative height
        // and let it wander out of the well.
        const auto height = juce::jlimit(0, juce::jmax(0, inner.getHeight()), row.weight);
        const auto strip = atTop ? inner.removeFromTop(height) : inner.removeFromBottom(height);
        if (i == wanted) return strip;
        if (atTop) inner.removeFromTop(displaySeatGap);
        else       inner.removeFromBottom(displaySeatGap);
    }
    return wanted < 0 ? inner : juce::Rectangle<int>();
}

// What the display has left for the thing it is a display *of*. Everything that
// draws into a display asks for this rather than for displayBounds, so a curve
// can never be plotted underneath the controls seated on top of it.
inline juce::Rectangle<int> displayPlotBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    return displayCarve(moduleArea, module, -1);
}

// The parameter an oscillator's display draws: the first knob of the module,
// which is POSITION by declaration in both oscillators.
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

inline juce::Rectangle<int> rowBounds(juce::Rectangle<int> moduleArea, const Module& module, int rowIndex)
{
    // A seated row is laid out in the display rather than in the body, and is
    // absent from the division below — which is what lets the body's rows be
    // declared as shares of what is actually theirs.
    if (module.rows[static_cast<size_t>(rowIndex)].seat != Seat::body)
        return displayCarve(moduleArea, module, rowIndex);

    const auto area = controlArea(moduleArea, module);
    if (juce::String(module.id) == "fx")
        return {area.getX(), area.getY() + rowIndex * fxSlotHeight, area.getWidth(), fxSlotHeight};

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

inline juce::Rectangle<int> fxListItemBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                             int displayRow, bool listOpen, int firstSlot = 0)
{
    juce::ignoreUnused(module);
    const auto list = fxListBounds(moduleArea, listOpen);
    const auto viewport = fxSlotViewportBounds(moduleArea);
    return {list.getX(), viewport.getY() + (displayRow - firstSlot) * fxSlotHeight,
            list.getWidth(), fxSlotHeight};
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
