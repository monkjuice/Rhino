// Where everything lands. resized() walks the declared modules and gives each
// component the rectangle ForgeLayout.h computes for it; the rest is the
// handful of areas the paint and input paths ask for by name.
#include "ForgeEditorInternal.h"

#include <optional>

namespace rhino::forge
{
bool Editor::moduleShown(const ui::Module& module) const
{
    // The arp is an overlay rather than a tab. Its panes appear when it is
    // opened, whichever tab is showing, and the modules they stand on step
    // aside for exactly as long as they are there — which is why this is asked
    // before the page is consulted at all.
    if (ui::onPage(module, ui::Page::arp)) return arpOpen;
    if (arpOpen && ui::coveredByArp(module)) return false;
    if (!ui::onPage(module, page)) return false;
    return page != ui::Page::fx || !fxExpanded || isFxModule(module);
}

ui::Page Editor::pageOf(const ui::Module& module) const
{
    return ui::onPage(module, ui::Page::arp) ? ui::Page::arp : page;
}

juce::Rectangle<int> Editor::moduleAreaFor(const ui::Module& module) const
{
    if (isFxModule(module))
        return ui::fxModuleBounds(getLocalBounds(), module, fxExpanded);
    return ui::moduleBounds(getLocalBounds(), module);
}

void Editor::resized()
{
    // A taller expanded rack may reveal every slot; a later compact resize can
    // reduce the window again. Keep each rack's remembered top slot legal, and
    // the matrix's, and refresh visibility before positioning the child
    // controls.
    clampFxScroll();
    clampMatrixScroll();
    applyEnableStates();

    savePreset.setBounds(ui::presetButtonBounds(getLocalBounds(), true));
    loadPreset.setBounds(ui::presetButtonBounds(getLocalBounds(), false));
    presetName.setBounds(ui::presetLabelBounds(getLocalBounds()));
    presetPlate.setBounds(ui::PresetPlate::boundsFor(getLocalBounds()));
    // On Forge's plate the two lines under the name stop at the stripes.
    presetPlate.bylineWidth = ui::isMerli() ? 0
        : juce::roundToInt(ui::identityHatchBounds(ui::identityPlateBounds(getLocalBounds())).getX())
              - presetPlate.getX() - 8;
    if (presetBrowser != nullptr) presetBrowser->setBounds(ui::contentBounds(getLocalBounds()));

    for (int i = 0; i < ui::tabCount && i < static_cast<int>(tabs.size()); ++i)
        tabs[static_cast<size_t>(i)]->setBounds(ui::tabBounds(i, getWidth()));

    const auto keys = ui::keyboardBounds(getLocalBounds());
    // Sized so the seventy-six keys span what is left of the shelf exactly,
    // rather than running out partway and leaving a blank stretch. The divisor
    // is the count actually drawn: the octave the ARP plate took is gone from
    // the range as well as from the width, so a key is the size it always was.
    keyboard.setKeyWidth(static_cast<float>(keys.getWidth())
                         / static_cast<float>(ui::keyboardWhiteKeys));
    keyboard.setBounds(keys);
    // With no title line, both rows use the whole inset face. The small extra
    // clearance also gives the fields a less cramped vertical hit target while
    // keeping the lower-left rivet visible beside the quality switch.
    const auto guide = ui::noteGuidePlateBounds(getLocalBounds()).reduced(7, 6);
    const auto rowHeight = juce::jlimit(24, 29, (guide.getHeight() - 6) / 2);
    const auto leftWidth = juce::jmin(88, guide.getWidth() * 38 / 100);
    const auto placeRow = [&] (juce::Rectangle<int> row, juce::Label& label, juce::Component& field)
    {
        label.setBounds(row.withWidth(34));
        field.setBounds(row.withTrimmedLeft(36));
    };
    auto first = guide.withHeight(rowHeight);
    auto second = guide.withTrimmedTop(rowHeight + 6).withHeight(rowHeight);
    placeRow(first.withWidth(leftWidth), guideRootLabel, guideRoot);
    placeRow(first.withTrimmedLeft(leftWidth + 7), guideChordLabel, guideChord);
    guideQuality.setBounds(second.withWidth(leftWidth));
    placeRow(second.withTrimmedLeft(leftWidth + 7), guideScaleLabel, guideScale);
    const auto plate = ui::deckLeftBounds(getLocalBounds());
    const auto wheelWidth = juce::jlimit(27, 37, (plate.getWidth() - 34) / 2);
    const auto gap = juce::jmax(8, (plate.getWidth() - 2 * wheelWidth) / 4);
    const auto wheelX = plate.getCentreX() - wheelWidth - gap / 2;
    pitchWheel.setBounds(wheelX, plate.getY() + 1, wheelWidth, plate.getHeight() - 2);
    modulationWheel.setBounds(wheelX + wheelWidth + gap, plate.getY() + 1,
                              wheelWidth, plate.getHeight() - 2);

    // Handles are positioned after their modules, because a macro's handle sits
    // on top of its knob's label.
    const auto handleFor = [this] (int source) -> ui::SourceHandle*
    {
        for (auto& handle : handles)
            if (handle->source == source) return handle.get();
        return nullptr;
    };

    if (tablePanel != nullptr)
        for (const auto& descriptor : ui::modules())
            if (juce::String(descriptor.id) == "table")
                tablePanel->setBounds(ui::controlArea(ui::moduleBounds(getLocalBounds(), descriptor),
                                                      descriptor));

    const auto diameter = ui::uniformKnobDiameter(getLocalBounds());
    const auto fxSizes = ui::fxKnobSizesFor(getLocalBounds());
    // Every control of a slot asks for the same strip, so each is laid out once.
    std::array<std::optional<ui::FxStrip>, rackCount * fxSlotCount> fxStrips;
    for (auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        const auto visible = moduleShown(descriptor);
        const auto area = moduleAreaFor(descriptor);
        const auto rackModule = isFxModule(descriptor);
        const auto matrixRows = ui::isMatrixModule(descriptor);

        if (rackModule)
            fxControlViewport.setBounds(ui::fxSlotViewportBounds(area, fxListOpen));

        if (module.enable != nullptr)
        {
            const auto led = juce::Rectangle<int>(ui::headerHeight, ui::headerHeight)
                .withPosition(area.getX() + 8, area.getY());
            module.enable->setBounds(led);
        }
        // A module that is itself a source puts its handle where the enable LED
        // would be, ahead of the title. A module showing one of several sources
        // puts the one it is showing there, and leaves the rest off the panel.
        if (descriptor.handleSource != 0)
        {
            const auto showing = descriptor.handleSource + module.bank;
            for (int bank = 0; bank < ui::bankCount(descriptor); ++bank)
                if (auto* other = handleFor(descriptor.handleSource + bank))
                    other->setVisible(visible && descriptor.handleSource + bank == showing);
            // Flush with the module's top edge and the full height of the
            // header: the handle is a card hanging off that edge, not a button
            // floating inside it.
            const auto left = area.getX() + (descriptor.enableId != nullptr ? ui::headerHeight + 8 : 10);
            auto width = ui::handleWidth;
            // A PATH LFO has two outputs, and its card splits in two for them,
            // as Serum's tile does: the Y card takes the right-hand end of the
            // name card's own room, so the bank cards and the header's rate
            // beyond it never move. Every other type leaves the card off the
            // panel and the name card whole.
            if (descriptor.display == ui::Display::lfo)
            {
                const auto path = shownLfoType() == LfoType::path;
                lfoPathHandleShown = path;
                if (path) width -= ui::lfoYHandleWidth + 2;
                for (int bank = 0; bank < lfoCount; ++bank)
                    if (auto* y = handleFor(static_cast<int>(ModSource::lfo1Y) + bank))
                    {
                        y->setVisible(visible && path && bank == module.bank);
                        y->setBounds(left + ui::handleWidth - ui::lfoYHandleWidth, area.getY(),
                                     ui::lfoYHandleWidth, ui::headerHeight);
                    }
            }
            if (auto* handle = handleFor(showing))
                handle->setBounds(left, area.getY(), width, ui::headerHeight);
        }
        for (int bank = 0; bank < static_cast<int>(module.bankButtons.size()); ++bank)
            module.bankButtons[static_cast<size_t>(bank)]
                ->setBounds(ui::bankButtonBounds(area, descriptor, bank, ui::handleWidth));

        for (auto& held : module.controls)
        {
            auto& control = *held;
            // A rack slot is laid out by the effect it holds rather than by
            // its declared row: see ForgeFxStrips.h. Its rectangles are in the
            // editor's coordinates and its controls are children of the
            // clipping viewport, so they are moved into the viewport's.
            if (rackModule)
            {
                int rack = 0, slot = 0;
                if (fxControlAt(control.id, rack, slot))
                {
                    auto& strip = fxStrips[static_cast<size_t>(rack * fxSlotCount + slot)];
                    if (!strip.has_value()) strip = fxStripFor(area, descriptor, rack, slot, fxSizes);
                    placeFxControl(control, strip->spot(ui::fxRoleOf(control.index)),
                                   -fxControlViewport.getPosition());
                    continue;
                }
            }
            auto controlsArea = area;
            // Every row of the matrix is placed, the ones past its window
            // included: they land off the table and applyEnableStates keeps
            // them hidden, so scrolling moves rows rather than making them.
            if (matrixRows)
                controlsArea = ui::matrixScrolledBounds(area, descriptor, matrixFirstRow);
            auto block = ui::controlBlock(controlsArea, descriptor, control.row, control.index, diameter);
            auto cell = ui::cellBounds(controlsArea, descriptor, control.row, control.index);

            // A macro's cell is laid out as nothing else on the panel is: the
            // knob, the plate beside it carrying the number you drag and the
            // count of what that number reaches, and the macro's own name
            // under both. The declared label — the bare number — is what the
            // plate replaced, so it stays off the panel.
            if (control.macroName != nullptr)
            {
                const auto macro = control.id.getTrailingIntValue();
                if (auto* handle = handleFor(static_cast<int>(ModSource::macro1) + macro - 1))
                {
                    handle->setVisible(visible && control.bank == module.bank);
                    handle->setBounds(ui::macroPlateBounds(cell, diameter));
                }
                control.label.setVisible(false);
                control.macroName->setBounds(ui::macroNameBounds(cell, diameter));
                control.slider.setBounds(ui::macroKnobBounds(cell, diameter));
                continue;
            }
            switch (control.style)
            {
                case ui::Style::chip:
                    control.chip->setBounds(block);
                    break;
                case ui::Style::bar:
                    control.slider.setBounds(block);
                    break;
                case ui::Style::plate:
                    control.plate->setBounds(block);
                    break;
                case ui::Style::wave:
                    control.waves->setBounds(block);
                    break;
                case ui::Style::selector:
                    // Seated, the field is the whole strip it was given and
                    // has no caption above it; everywhere else it wears the
                    // label line a stepper does.
                    if (!ui::seatedOutsideBody(descriptor, control.row))
                        control.label.setBounds(block.removeFromTop(ui::stepperLabelHeight));
                    control.selector->setBounds(block);
                    break;
                case ui::Style::fader:
                    // The same label line a knob's sits on, so a row of faders
                    // and a row of knobs line up across the mixer.
                    control.label.setBounds(ui::knobLabelBounds(block, cell));
                    control.slider.setBounds(block.withTrimmedTop(ui::knobLabelHeight));
                    break;
                case ui::Style::stepper:
                    // Inside a table the column title is the label, so the
                    // field takes the whole block rather than the half of it
                    // left under a label strip. Seated, the field carries its
                    // caption inside itself — see stepperBlock.
                    if (descriptor.columnHeaderHeight == 0
                        && !ui::seatedOutsideBody(descriptor, control.row))
                        control.label.setBounds(block.removeFromTop(ui::stepperLabelHeight));
                    control.slider.setBounds(block);
                    break;
                case ui::Style::rocker:
                    // Same label line as the knobs either side, so the switch
                    // sits exactly where their circles do.
                    control.label.setBounds(ui::knobLabelBounds(block, cell));
                    control.rocker->setBounds(ui::rockerBounds(block.withTrimmedTop(ui::knobLabelHeight)));
                    break;
                case ui::Style::knob:
                    control.label.setBounds(ui::knobLabelBounds(block, cell));
                    control.slider.setBounds(block.withTrimmedTop(ui::knobLabelHeight));
                    break;
            }
        }
    }
    placeTableFields();
}

ui::FxStrip Editor::fxStripFor(juce::Rectangle<int> moduleArea, const ui::Module& module, int rack, int slot,
                               const ui::FxKnobSizes& sizes) const
{
    const auto strip = ui::rowBounds(fxRackAreaFor(moduleArea, rack, slot), module, slot);
    return ui::fxStripLayout(fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)], strip, sizes);
}

Editor::Control* Editor::fxControl(int rack, int slot, ui::FxRole role) const
{
    if (rack < 0 || rack >= rackCount || slot < 0 || slot >= fxSlotCount) return nullptr;
    return fxControls[static_cast<size_t>((rack * fxSlotCount + slot) * ui::fxRoleCount + ui::fxRoleIndex(role))];
}

// One rack control into the spot its slot's strip gave it. `offset` moves the
// spot from the editor's coordinates into the viewport the control lives in.
void Editor::placeFxControl(Control& control, const ui::FxSpot& spot, juce::Point<int> offset)
{
    const auto component = spot.component.translated(offset.x, offset.y);
    const auto caption = spot.caption.translated(offset.x, offset.y);
    control.fxCaptionShown = !spot.caption.isEmpty();
    // What the look needs to know about this knob that the slider cannot say
    // for itself: whether it is the strip's hero, which lights its collar, and
    // otherwise that its LED is the type's colour turned down.
    if (spot.knob != ui::FxKnobSize::none)
    {
        auto& knobProperties = control.slider.getProperties();
        knobProperties.set("fxHero", spot.knob == ui::FxKnobSize::hero);
        knobProperties.set("fxDim", spot.knob != ui::FxKnobSize::hero);
        knobProperties.set("fxBipolar", spot.bipolar);
        knobProperties.set("fxRedZone", spot.redZone);
        knobProperties.set("fxSecond", spot.secondPointer);
        juce::String marks;
        for (int mark = 0; mark < spot.markCount; ++mark)
            marks << (mark > 0 ? "," : "") << juce::String(spot.marks[static_cast<size_t>(mark)], 4);
        knobProperties.set("fxMarks", marks);
        control.label.setJustificationType(spot.captionLeft ? juce::Justification::centredLeft
                                                            : juce::Justification::centred);
    }
    if (control.selector != nullptr)
    {
        control.selector->presentation = spot.presentation;
        control.selector->forceList = spot.presentation == ui::FxPresentation::list;
    }
    switch (control.style)
    {
        case ui::Style::plate:
            control.plate->setBounds(component);
            break;
        case ui::Style::chip:
            control.chip->setBounds(component);
            break;
        case ui::Style::selector:
            control.label.setBounds(caption);
            control.selector->setBounds(component);
            break;
        case ui::Style::knob:
        case ui::Style::bar:
        case ui::Style::fader:
        case ui::Style::stepper:
        case ui::Style::rocker:
        case ui::Style::wave:
            control.label.setBounds(caption);
            control.slider.setBounds(component);
            break;
    }
}

// The envelope module is painted straight onto the editor rather than being a
// component of its own, so the things on it that can be clicked or scrolled are
// hit-tested here, against the same geometry the painter lays them out with.
const ui::Module* Editor::envelopeModule() const
{
    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (descriptor.display != ui::Display::envelope) continue;
        return moduleShown(descriptor) ? &descriptor : nullptr;
    }
    return nullptr;
}

juce::Rectangle<int> Editor::envelopeDisplayBounds() const
{
    const auto* module = envelopeModule();
    return module == nullptr ? juce::Rectangle<int>()
                             : ui::displayBounds(ui::moduleBounds(getLocalBounds(), *module), *module);
}

// The window belongs to the envelope showing, not to the module: four
// envelopes are not the same length, so a window set on one is no reading of
// another.
void Editor::setEnvelopeZoom(int zoom)
{
    auto& current = envelopeZoom[static_cast<size_t>(shownEnv())];
    const auto clamped = juce::jlimit(0, ui::envelopeZoomCount - 1, zoom);
    if (clamped == current) return;
    current = clamped;
    repaint();
}
}
