#pragma once

#include "ForgePlacement.h"

// Where one control lands inside the row it was declared in: which bank and
// cell it owns, and then the block its own look needs — a knob is square and
// wants a label under it, a fader is tall and narrow, a plate is wide.
//
// Knobs in a module are sized together rather than each to its own cell, so a
// row of four and a row of two below it draw the same circle.
namespace rhino::forge::ui
{
// which is also what keeps rowHasKnobs from reading past the end of it.
inline int controlsPerBank(const Row& row)
{
    if (row.controls.empty()) return 0;
    const auto banks = juce::jmax(1, row.banks);
    return juce::jmax(1, static_cast<int>(row.controls.size()) / banks);
}

// Which bank a control belongs to, counting from zero. Always zero in a row
// that declares none.
inline int bankOf(const Module& module, int rowIndex, int index)
{
    return index / controlsPerBank(module.rows[static_cast<size_t>(rowIndex)]);
}

// The control whose cell this one occupies: itself, unless it shares the cell
// of the control before it, or it belongs to a bank behind the first — in which
// case it stands exactly where its opposite number in the first bank does.
inline int cellOwner(const Module& module, int rowIndex, int index)
{
    const auto& row = module.rows[static_cast<size_t>(rowIndex)];
    const auto& controls = row.controls;
    // A control never shares a cell across a bank boundary: the control before
    // the first of a bank belongs to the bank in front of it.
    const auto first = (index / controlsPerBank(row)) * controlsPerBank(row);
    while (index > first && controls[static_cast<size_t>(index)].sharesCell) --index;
    return index;
}

// Whether this control's cell holds more than one control — either because it
// says it shares the one before it, or because the one after it shares this.
inline bool inSharedCell(const Module& module, int rowIndex, int index)
{
    const auto& row = module.rows[static_cast<size_t>(rowIndex)];
    const auto& controls = row.controls;
    if (controls[static_cast<size_t>(index)].sharesCell) return true;
    const auto next = index + 1;
    // The control after the last of a bank belongs to the next bank, so it
    // cannot be sharing this one's cell.
    if (next % controlsPerBank(row) == 0) return false;
    return next < static_cast<int>(controls.size()) && controls[static_cast<size_t>(next)].sharesCell;
}

// A row is divided between its cells, not between its controls: a control that
// shares the cell before it takes no width of its own and lands exactly on top
// of the one it shares with.
inline juce::Rectangle<int> cellBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                       int rowIndex, int index)
{
    const auto row = rowBounds(moduleArea, module, rowIndex);
    const auto& declared = module.rows[static_cast<size_t>(rowIndex)];
    const auto& controls = declared.controls;
    // Divided between one bank's cells. The banks behind it declare the same
    // cells and land on top of them.
    const auto perBank = controlsPerBank(declared);
    // The display takes its share of the row before the cells divide what is
    // left, so it is counted in the same total they are.
    auto total = juce::jmax(0, declared.displayWeight);
    for (int i = 0; i < perBank; ++i)
        if (!controls[static_cast<size_t>(i)].sharesCell)
            total += juce::jmax(1, controls[static_cast<size_t>(i)].weight);
    if (total <= 0) return row;

    // Reduced to its own bank, because every bank stands on the same cells.
    const auto owner = cellOwner(module, rowIndex, index) % perBank;
    auto x = row.getX();
    for (int i = 0; i < owner; ++i)
        if (!controls[static_cast<size_t>(i)].sharesCell)
            x += row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(i)].weight) / total;
    // Everything past the display is pushed along by it.
    if (declared.displayWeight > 0 && owner >= declared.displayAfter)
        x += row.getWidth() * declared.displayWeight / total;
    const auto width = row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(owner)].weight) / total;
    return {x, row.getY(), width, row.getHeight()};
}

// The strip a row has reserved for a display, or an empty rectangle when it has
// reserved none. Worked out the same way the cells are, so the two cannot
// disagree about where one ends and the other begins — which the layout test
// checks by intersecting them.
inline juce::Rectangle<int> rowDisplayBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                             int rowIndex)
{
    const auto& declared = module.rows[static_cast<size_t>(rowIndex)];
    if (declared.displayWeight <= 0) return {};
    const auto row = rowBounds(moduleArea, module, rowIndex);
    const auto& controls = declared.controls;
    const auto perBank = controlsPerBank(declared);

    auto total = declared.displayWeight;
    for (int i = 0; i < perBank; ++i)
        if (!controls[static_cast<size_t>(i)].sharesCell)
            total += juce::jmax(1, controls[static_cast<size_t>(i)].weight);
    if (total <= 0) return {};

    auto x = row.getX();
    for (int i = 0; i < declared.displayAfter && i < perBank; ++i)
        if (!controls[static_cast<size_t>(i)].sharesCell)
            x += row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(i)].weight) / total;
    return {x, row.getY(), row.getWidth() * declared.displayWeight / total, row.getHeight()};
}

// Every knob on the panel is drawn at one diameter, taken from whichever cell
// on the panel is tightest in either direction. Sizing each knob to its own
// module instead makes SUB's single knob several times the diameter of one in
// GLOBAL, which reads as a mistake rather than as emphasis.
inline int uniformKnobDiameter(juce::Rectangle<int> bounds)
{
    auto smallest = static_cast<int>(maxKnobWidth);
    for (const auto& module : modules())
    {
        const auto area = moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& row = module.rows[static_cast<size_t>(r)];
            if (module.compactKnobs) continue;
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                if (row.controls[static_cast<size_t>(i)].style != Style::knob) continue;
                const auto cell = cellBounds(area, module, r, i);
                // The cell also has to hold the label above and the readout below.
                smallest = juce::jmin(smallest, cell.getWidth() - 6,
                                      cell.getHeight() - knobLabelHeight);
            }
        }
    }
    return juce::jmax(40, smallest);
}

// The label-plus-knob block, centred in its cell at the one size the whole panel
// shares.
//
// No value is printed under a knob. A readout costs every knob on the panel a
// line of height whether or not anyone is reading it, and it was the readout
// rather than the knob that set how small a macro could be drawn. The value is
// shown instead where the hand already is, in a bubble beside the knob being
// turned.
inline juce::Rectangle<int> knobBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int rowIndex, int index, int diameter)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    juce::ignoreUnused(module);
    return juce::Rectangle<int>(diameter, knobLabelHeight + diameter).withCentre(cell.getCentre());
}

// The circle inside a knob's block, which is everything below its label.
inline int knobDiameterOf(juce::Rectangle<int> block)
{
    return block.getHeight() - knobLabelHeight;
}

// A stepper is a fixed-height numeric field, so it does not scale with the
// window the way a knob does. Inside a table it carries no label of its own —
// the column titles say what it is — so it takes the height of its row instead
// of sharing that height with a label strip.
inline juce::Rectangle<int> stepperBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    if (module.columnHeaderHeight > 0)
        return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 12, maxTableFieldWidth),
                                    juce::jmin(cell.getHeight() - 6, tableFieldHeight))
            .withCentre(cell.getCentre());
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 8, maxStepperWidth),
                                juce::jmin(cell.getHeight(), stepperLabelHeight + stepperHeight))
        .withCentre(cell.getCentre());
}

// A bar is given more room than the fields beside it, because it is read as a
// distance rather than as a number.
inline juce::Rectangle<int> barBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                     int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 12, maxTableBarWidth),
                                juce::jmin(cell.getHeight() - 6, tableFieldHeight))
        .withCentre(cell.getCentre());
}

// A fader takes its cell's full height, less the label line above it. Unlike a
// knob it is not square and does not follow the panel's shared diameter: a
// mixer channel is balanced by how far the thumb has travelled, and that
// distance is worth every pixel of the cell.
inline juce::Rectangle<int> faderBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                       int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(juce::jmax(10, cell.getWidth() - 6), maxFaderWidth),
                                juce::jmax(1, cell.getHeight() - 2))
        .withCentre(cell.getCentre());
}

// Laid out like a stepper, so a selector's label sits on the same line as the
// labels either side of it however tall the field under it is.
inline juce::Rectangle<int> selectorBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                          int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 10, maxSelectorWidth),
                                juce::jmin(cell.getHeight(), stepperLabelHeight + selectorHeight))
        .withCentre(cell.getCentre());
}

// A wave grid carries no label, so it takes its cell whole, less the inset that
// keeps its outer cells off the cells of the controls above and below it.
inline juce::Rectangle<int> waveGridBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                          int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(juce::jmax(12, cell.getWidth() - 6), maxWaveGridWidth),
                                juce::jmin(juce::jmax(12, cell.getHeight() - 6), maxWaveGridHeight))
        .withCentre(cell.getCentre());
}

inline juce::Rectangle<int> plateBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 10, maxPlateWidth),
                                juce::jmin(cell.getHeight() - 8, maxPlateHeight))
        .withCentre(cell.getCentre());
}

// A chip carries its own label, so unlike a knob or a stepper it needs no
// separate label strip above it.
inline juce::Rectangle<int> chipBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 8, maxChipWidth),
                                juce::jmin(cell.getHeight(), chipHeight))
        .withCentre(cell.getCentre());
}

// A compact module's knobs are sized to their own cell, and then held below the
// diameter the rest of the panel shares. The macro column is tall enough that
// sizing to the cell alone would make the macros the largest knobs in Forge,
// which is backwards: they drive the other controls, they do not outrank them.
inline constexpr int compactKnobPercent = 80;

// A compact module is the only one that stacks knobs several deep, so it is the
// only one that needs a gap between one block and the next: a knob sized to
// exactly its cell puts its readout against the label of the row below, which
// reads as a collision rather than as a column.
inline constexpr int compactKnobGap = 8;

inline int knobDiameterFor(const Module& module, juce::Rectangle<int> moduleArea,
                           int rowIndex, int index, int shared)
{
    if (!module.compactKnobs) return shared;
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::jmax(24, juce::jmin(cell.getWidth() - 6,
                                     cell.getHeight() - knobLabelHeight - compactKnobGap,
                                     shared * compactKnobPercent / 100));
}

// Whether a row mixes short controls in among full-height knobs. A row of like
// controls centres them all; a mixed row has a label line to line up with.
inline bool rowHasKnobs(const Module& module, int rowIndex)
{
    const auto& row = module.rows[static_cast<size_t>(rowIndex)];
    const auto perBank = controlsPerBank(row);
    for (int i = 0; i < perBank; ++i)
    {
        const auto style = row.controls[static_cast<size_t>(i)].style;
        if (style == Style::knob || style == Style::rocker) return true;
    }
    return false;
}

inline juce::Rectangle<int> controlBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index, int diameter)
{
    switch (module.rows[static_cast<size_t>(rowIndex)].controls[static_cast<size_t>(index)].style)
    {
        case Style::stepper:
        {
            auto block = stepperBlock(moduleArea, module, rowIndex, index);
            // Beside knobs, a stepper puts its label on their label line rather
            // than floating in the middle of its cell, which is what made LFO
            // 1's SHAPE and DIV sit lower than the SYNC and RATE beside them.
            if (module.columnHeaderHeight == 0 && rowHasKnobs(module, rowIndex))
            {
                const auto knob = knobBlock(moduleArea, module, rowIndex, index, diameter);
                block.setY(knob.getY() + knobLabelHeight - stepperLabelHeight);
            }
            return block;
        }
        case Style::bar:     return barBlock(moduleArea, module, rowIndex, index);
        case Style::chip:    return chipBlock(moduleArea, module, rowIndex, index);
        case Style::wave:    return waveGridBlock(moduleArea, module, rowIndex, index);
        case Style::fader:   return faderBlock(moduleArea, module, rowIndex, index);
        case Style::plate:   return plateBlock(moduleArea, module, rowIndex, index);
        case Style::selector:
        {
            auto block = selectorBlock(moduleArea, module, rowIndex, index);
            // Beside knobs, on their label line, exactly as a stepper is.
            if (module.columnHeaderHeight == 0 && rowHasKnobs(module, rowIndex))
            {
                const auto knob = knobBlock(moduleArea, module, rowIndex, index, diameter);
                block.setY(knob.getY() + knobLabelHeight - stepperLabelHeight);
            }
            return block;
        }
        // A rocker takes a knob's whole block so its label and readout sit on
        // the same lines as the knobs either side of it.
        case Style::rocker:
        case Style::knob: break;
    }
    return knobBlock(moduleArea, module, rowIndex, index,
                     knobDiameterFor(module, moduleArea, rowIndex, index, diameter));
}

// The rocker inside that block: narrow and tall, centred, and sitting exactly
// where a knob's circle sits. The 4px inset is the same one the knob's look
// applies, so the gap under the label is identical for both.
inline constexpr int knobOpticalInset = 4;

// The label strip over a knob, seated on the circle rather than on the box
// around it.
//
// A knob's look insets its circle by knobOpticalInset, so the top of the area
// handed to the slider is empty by construction. A label that stops at the
// boundary instead of at the metal therefore floats: measured against the
// reference, the gap under the word came out at 12 pixels where the reference
// draws 8, and the gap above it at 8 where the reference draws 16 — the word
// read as belonging to the row above rather than to the knob it names.
//
// The strip keeps its height and moves down into that empty margin, so nothing
// about the knob's own size or position changes. The labels are click-through,
// which is what keeps the overlap from costing the knob the start of a drag.
inline juce::Rectangle<int> knobLabelBounds(juce::Rectangle<int> block)
{
    return block.withHeight(knobLabelHeight).translated(0, knobOpticalInset);
}

inline juce::Rectangle<int> rockerBounds(juce::Rectangle<int> knobArea)
{
    const auto area = knobArea.reduced(0, knobOpticalInset);
    const auto width = juce::jmax(14, juce::roundToInt(area.getHeight() * 0.46f));
    return juce::Rectangle<int>(width, area.getHeight()).withCentre(area.getCentre());
}

// --- The macro cell -----------------------------------------------------------
//
// A macro is not a knob with a label over it. It is three things at once: a
// knob to turn, a source to drag, and a set of routings to keep track of. So
// the cell is a knob with a plate beside it carrying the second and the third,
// and a name under it that you give it yourself.
//
// The plate is paid for out of the knob's width rather than out of the cell's
// height, and the name strip costs a pixel less than the number strip it
// replaces. What used to limit a macro's diameter was the height left over
// under that strip; now it is the width left over beside the plate, and the
// macros come out larger than before at every size the panel allows.

// The name is a tag, not a sentence: the strip holds about fifteen characters
// at the size the panel opens at, and ellipsises whatever will not fit. One
// pixel taller than the text needs, because the strip is also what a name is
// typed into and a text editor sized to exactly its font clips the descenders.
inline constexpr int macroNameHeight = 14;
// Between the knob's drawn circle and the plate. Enough to read as a gap rather
// than as a seam at the size the cell is tightest.
inline constexpr int macroPlateGap = 4;
inline constexpr int macroCellInset = 3;

// A plate never carries more than one digit per storey, so past a point more
// width buys nothing and is simply width the knob did not get. Below the other
// point it stops being a grip.
inline constexpr int minMacroPlateWidth = 20;
inline constexpr int maxMacroPlateWidth = 28;
// A plate as tall as the widest knob the panel allows would be a stripe. It
// stops here and stays centred on the knob's own middle.
inline constexpr int maxMacroPlateHeight = 44;

inline int macroPlateWidth(juce::Rectangle<int> cell)
{
    return juce::jlimit(minMacroPlateWidth, maxMacroPlateWidth, cell.getWidth() / 3);
}

// Everything below is measured against the knob's drawn circle rather than
// against the rectangle the slider occupies. The look insets that rectangle by
// knobOpticalInset on every side before it draws anything, so a plate lined up
// with the rectangle's edge stands twice the gap clear of the metal and twice
// the inset taller than it — which is the difference between a plate beside a
// knob and a plate looming over one. Against the circle the pair read as one
// piece of hardware.
inline constexpr int macroCircleInset = knobOpticalInset * 2;

// Whatever the cell has left once the plate, the gaps and the name have taken
// theirs — and never more than the share of the panel's own diameter a compact
// module is allowed. The macros drive the other controls; they do not outrank
// them, and without that cap a wide window would make them the largest knobs
// in Forge.
inline int macroKnobDiameter(juce::Rectangle<int> cell, int shared)
{
    // No allowance for the inset here, though the group is measured in circles.
    // Centring the group leaves the same slack on both sides, and the slider's
    // rectangle spends exactly one inset of the left-hand slack reaching back
    // past its own circle. Adding the inset to this budget spends that slack
    // twice, and the rectangle then hangs a pixel outside the cell at the
    // sizes where the width is what binds — which is a hit target reaching into
    // the cell next door, not a mark on the panel, and so invisible until the
    // swept layout check went looking for it.
    const auto wide = cell.getWidth() - macroCellInset * 2 - macroPlateGap
                    - macroPlateWidth(cell);
    const auto tall = cell.getHeight() - macroNameHeight - macroCellInset * 2;
    return juce::jmax(20, juce::jmin(wide, tall, maxKnobWidth,
                                     juce::jmax(20, shared * compactKnobPercent / 100)));
}

// What the eye actually sees of the knob, which is what the plate is sized and
// placed against.
inline int macroCircleDiameter(juce::Rectangle<int> cell, int shared)
{
    return juce::jmax(8, macroKnobDiameter(cell, shared) - macroCircleInset);
}

// The circle and the plate stand as one group, centred across the cell. The
// name takes the cell's full width underneath instead of the group's, because a
// name wants every pixel the cell has and the group does not.
inline juce::Rectangle<int> macroKnobBounds(juce::Rectangle<int> cell, int shared)
{
    const auto diameter = macroKnobDiameter(cell, shared);
    const auto group = macroCircleDiameter(cell, shared) + macroPlateGap + macroPlateWidth(cell);
    const auto top = cell.getY() + (cell.getHeight() - diameter - macroNameHeight) / 2;
    // The slider's rectangle starts an inset left of where its circle does.
    return {cell.getCentreX() - group / 2 - knobOpticalInset, top, diameter, diameter};
}

inline juce::Rectangle<int> macroPlateBounds(juce::Rectangle<int> cell, int shared)
{
    const auto knob = macroKnobBounds(cell, shared);
    const auto circle = macroCircleDiameter(cell, shared);
    const auto width = macroPlateWidth(cell);
    const auto left = knob.getX() + knobOpticalInset + circle + macroPlateGap;
    return juce::Rectangle<int>(width, juce::jmin(circle, maxMacroPlateHeight))
        .withCentre({left + width / 2, knob.getCentreY()});
}

inline juce::Rectangle<int> macroNameBounds(juce::Rectangle<int> cell, int shared)
{
    const auto knob = macroKnobBounds(cell, shared);
    return {cell.getX(), knob.getBottom(), cell.getWidth(), macroNameHeight};
}
}
