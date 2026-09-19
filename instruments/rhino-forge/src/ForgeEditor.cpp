#include "ForgeEditor.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
namespace
{
// The destination index a parameter corresponds to, or 0 if the matrix cannot
// point at it.
int destinationFor(const juce::String& parameterId)
{
    for (int i = 1; i < destinationCount; ++i)
        if (parameterId == destinations()[static_cast<size_t>(i)].id) return i;
    return 0;
}

juce::String slotParameter(int slot, const char* suffix)
{
    return "mod" + juce::String(slot + 1) + suffix;
}

// How many semitones of the piano the computer keys can reach at once. JUCE's
// default qwerty mapping is "awsedftgyhujkolp;" laid on the note offsets 0..16
// from the C of the mapping octave, so the reach is a C to the E an octave and
// a third above it.
constexpr int computerKeySpan = 17;
}

Editor::Editor(Processor& p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    buildModules();
    buildHandles();
    buildTabs();
    buildTablePanel();

    keyboard.setAvailableRange(21, 108);
    keyboard.setLowestVisibleKey(21);
    // Middle C is C3, which is what Ableton, Serum, FL and Logic all call MIDI
    // 60. Rhino's own grid says the same — see StepGridPainter, which was moved
    // with this — so the panel, the grid driving it and the DAW next to it all
    // name one note the same way.
    //
    // It used to be C4 here and on the grid. Both were self-consistent and both
    // disagreed with everything outside Rhino, so a player checking Forge
    // against Serum pressed two keys marked C3, got notes an octave apart, and
    // reasonably concluded the synth was out of tune. It was not; only the
    // label was. This is JUCE's own default, so the call stays to say so.
    keyboard.setOctaveForMiddleC(3);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setKeyPressBaseOctave(computerKeyOctave);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd8dcea));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff10131f));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff05070e));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, ui::electricBlue.withAlpha(0.75f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, ui::electricBlue.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha(0.4f));
    keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, ui::panelRaised);
    keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, ui::mutedText);
    addAndMakeVisible(keyboard);

    for (auto* button : {&loadPreset, &savePreset})
    {
        button->setColour(juce::TextButton::buttonColourId, ui::panelRaised);
        button->setColour(juce::TextButton::textColourOffId, ui::text);
        addAndMakeVisible(button);
    }
    loadPreset.onClick = [this] { choosePresetToLoad(); };
    savePreset.onClick = [this] { choosePresetToSave(); };

    presetName.setText("INIT SIGNAL", juce::dontSendNotification);
    presetName.setJustificationType(juce::Justification::centredRight);
    presetName.setFont(juce::FontOptions(10.0f));
    presetName.setColour(juce::Label::textColourId, ui::mutedText);
    addAndMakeVisible(presetName);

    // The default tooltip is dark text furniture on a dark panel, which leaves
    // the words floating with no edge to read them against. Given the same
    // well-and-hairline treatment the rest of the panel uses.
    tooltips.setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff05070e));
    tooltips.setColour(juce::TooltipWindow::textColourId, ui::text);
    tooltips.setColour(juce::TooltipWindow::outlineColourId, ui::electricBlue.withAlpha(0.6f));

    // So the panel itself can hold focus when nothing in it does, and the keys
    // still have somewhere to arrive from.
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    // The matrix moving into the tabbed row took a whole grid row off the
    // bottom of the panel, so the window is shorter than it was at every limit.
    setResizeLimits(ui::minPanelWidth, ui::minPanelHeight, ui::maxPanelWidth, ui::maxPanelHeight);
    setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
    addChildComponent(valueBubble);
    refreshWarpFields();
    applyEnableStates();
    applyTableCounts();
    applyPage();
    startTimerHz(24);
}

void Editor::buildTablePanel()
{
    tablePanel = std::make_unique<ui::TablePanel>(processor.tableStore());
    tablePanel->importer = [this] (int oscillator, const juce::File& file)
    {
        return processor.importTable(oscillator, file);
    };
    tablePanel->onTableChanged = [this]
    {
        // A table with a different number of frames changes what POSITION steps
        // through and what its readout says, on both tabs.
        applyTableCounts();
        if (tablePanel->status.isNotEmpty())
        {
            presetName.setText(tablePanel->status, juce::dontSendNotification);
            presetName.setColour(juce::Label::textColourId, ui::signalViolet);
            tablePanel->status.clear();
        }
        repaint();
    };
    addChildComponent(*tablePanel);
}

// POSITION picks a frame out of a table, so a hand on it should land on one
// rather than a hair short of it — which means its detents follow whatever
// table that oscillator is now reading. The readout is pushed too: a slider
// only re-reads its parameter's text when its value moves, and here the value
// has stayed put while the text it should show has changed.
void Editor::applyTableCounts()
{
    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            if (!control->id.endsWith("Position")) continue;
            const auto oscillator = control->id.startsWith("oscB") ? 1 : 0;
            control->slider.gestureSteps = processor.tableStore().frameCount(oscillator);
            control->slider.updateText();
        }
}

// "fx2s3Knob1" is rack 2, slot 3. Parsed rather than carried on the Control,
// because the id is where those numbers are actually written down and a second
// copy of them is a second thing to keep in step.
bool Editor::fxControlAt(const juce::String& id, int& rack, int& slot)
{
    if (!id.startsWith("fx")) return false;
    const auto body = id.substring(2);
    const auto split = body.indexOfChar('s');
    if (split <= 0) return false;
    const auto rackNumber = body.substring(0, split).getIntValue();
    const auto slotNumber = body.substring(split + 1).getIntValue();
    if (rackNumber < 1 || rackNumber > rackCount || slotNumber < 1 || slotNumber > fxSlotCount)
        return false;
    rack = rackNumber - 1;
    slot = slotNumber - 1;
    return true;
}

int Editor::shownRack() const
{
    for (const auto& module : moduleUis)
        if (juce::String(module.descriptor->id) == "fx")
            return juce::jlimit(0, rackCount - 1, module.bank);
    return 0;
}

juce::String Editor::fxHeaderDetail() const
{
    const auto rack = shownRack();
    juce::StringArray held;
    for (int slot = 0; slot < fxSlotCount; ++slot)
    {
        const auto type = value(fxParameterId(rack, slot, "Type"));
        if (fxTypeOf(type) != FxType::off) held.add(fxTypeName(juce::roundToInt(type)));
    }
    return held.isEmpty() ? "EMPTY" : held.joinIntoString(" > ");
}

bool Editor::fxKnobLive(const Control& control) const
{
    int rack = 0, slot = 0;
    if (!control.id.contains("Knob") || !fxControlAt(control.id, rack, slot)) return true;
    return rhino::forge::fxKnobLive(fxSlotOf(rack, slot), control.id.getTrailingIntValue() - 1);
}

bool Editor::fxControlUsed(const Control& control) const
{
    int rack = 0, slot = 0;
    if (!fxControlAt(control.id, rack, slot)) return true;
    const auto& info = processor.fxSlotType(rack, slot);
    if (control.id.contains("Knob"))
    {
        const auto knob = control.id.getTrailingIntValue() - 1;
        return knob >= 0 && knob < fxKnobCount && info.knobs[static_cast<size_t>(knob)] != nullptr;
    }
    if (control.id.endsWith("ModeA")) return info.modeA.count > 0;
    if (control.id.endsWith("ModeB")) return info.modeB.count > 0;
    // TYPE is how an empty slot is filled, so it is always there. MIX, LEVEL
    // and the bypass belong to the slot rather than to what is in it, but an
    // empty slot has nothing for them to act on — so a row set to OFF is a
    // blank row with one field on it, which is what an empty rack should look
    // like.
    return control.id.endsWith("Type")
        || fxTypeOf(value(fxParameterId(rack, slot, "Type"))) != FxType::off;
}

// A slot's twelve controls are always declared and always attached; what
// changes with the type is what they are called and what they explain.
// Whether they are on screen is applyEnableStates's to decide, because that
// runs on a timer and would otherwise put back whatever this took away.
//
// Every rack is refreshed, not only the one showing, so switching banks reveals
// a slot that is already right rather than one that corrects itself a frame
// later.
void Editor::refreshFxSlots()
{
    for (int rack = 0; rack < rackCount; ++rack)
        for (int slot = 0; slot < fxSlotCount; ++slot)
            fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)] =
                juce::roundToInt(value(fxParameterId(rack, slot, "Type")));

    for (auto& module : moduleUis)
    {
        if (juce::String(module.descriptor->id) != "fx") continue;
        for (auto& held : module.controls)
        {
            auto& control = *held;
            int rack = 0, slot = 0;
            if (!fxControlAt(control.id, rack, slot)) continue;
            const auto& info = processor.fxSlotType(rack, slot);
            const auto type = juce::roundToInt(value(fxParameterId(rack, slot, "Type")));
            const auto colour = ui::fxTypeColour(type);

            // Everything in the slot takes the type's colour, so a rack is read
            // by colour down its four rows before a single word on it is.
            control.slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
            control.slider.setColour(juce::Slider::thumbColourId, colour);
            if (control.chip != nullptr) control.chip->accent = colour;
            if (control.plate != nullptr)
            {
                control.plate->type = type;
                control.plate->setName(fxTypeName(type));
                control.plate->repaint();
            }

            const auto apply = [&control] (const char* label, const juce::String& tip)
            {
                if (label != nullptr) control.label.setText(label, juce::dontSendNotification);
                control.slider.setTooltip(tip);
            };

            if (control.id.contains("Knob"))
            {
                // A knob's reading depends on the type and on the mode fields
                // beside it — a delay's TIME is milliseconds or a division —
                // and a slider only re-reads its parameter's text when its
                // value moves, so it is pushed here instead.
                control.slider.updateText();
                const auto knob = control.id.getTrailingIntValue() - 1;
                const auto* label = knob >= 0 && knob < fxKnobCount
                    ? info.knobs[static_cast<size_t>(knob)] : nullptr;
                apply(label, label == nullptr ? juce::String()
                          : juce::String(label) + ", on the " + info.name + " in this slot");
                continue;
            }
            if (control.id.endsWith("ModeA") || control.id.endsWith("ModeB"))
            {
                const auto& mode = control.id.endsWith("ModeB") ? info.modeB : info.modeA;
                if (control.selector != nullptr)
                {
                    // Copied out of the table rather than pointed into it: the
                    // type in this slot changes underneath the selector, and a
                    // pointer to the old type's choices is a dangling read on
                    // the next repaint.
                    control.selector->choices.assign(mode.choices.begin(),
                                                     mode.choices.begin() + juce::jmax(0, mode.count));
                    control.selector->chosen = fxModeOf(mode, value(control.id));
                    control.selector->accent = colour;
                    control.selector->setTooltip(
                        mode.label == nullptr ? juce::String()
                            : juce::String(mode.label) + ", on the " + info.name + " in this slot");
                    control.selector->repaint();
                }
                apply(mode.label, mode.label == nullptr ? juce::String()
                          : juce::String(mode.label) + ", on the " + info.name + " in this slot");
                continue;
            }
        }
    }
}

// A mode parameter is a plain 0..1, because what it steps through changes with
// the type and a host's choice list is fixed when the parameter is made. So a
// choice is spread across that range, and fxModeOf reads it back the same way —
// one arithmetic, two directions.
void Editor::setFxMode(const juce::String& id, int count, int choice)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr || count <= 1) return;
    const auto at = static_cast<float>(juce::jlimit(0, count - 1, choice))
                  / static_cast<float>(count - 1);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(at));
}

// The list, for a mode field with more choices than fit across it — the
// distortion's eight shapes are the only one so far.
void Editor::showFxModeMenu(Control& control)
{
    if (control.selector == nullptr || control.selector->count() <= 0) return;
    const auto count = control.selector->count();

    juce::PopupMenu menu;
    for (int i = 0; i < count; ++i)
    {
        juce::PopupMenu::Item item(control.selector->choices[static_cast<size_t>(i)]);
        item.itemID = i + 1;
        item.isTicked = i == control.selector->chosen;
        menu.addItem(item);
    }

    const auto safe = juce::Component::SafePointer<Editor>(this);
    const auto id = control.id;
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe, id, count] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setFxMode(id, count, choice - 1);
    });
}

// --- Warp ---------------------------------------------------------------------
//
// An oscillator's two warp fields wear the same component a rack slot's modes
// do and behave differently behind it. A rack mode is a plain 0..1 because what
// it steps through depends on what is in the slot; a warp mode is a fixed list
// of twenty-six, so it is a real choice parameter and the value *is* the index.
// That is the whole of the difference, and it is why these methods sit beside
// the rack's rather than inside them.
bool Editor::isWarpControl(const juce::String& id)
{
    return id.startsWith("osc") && id.contains("Warp");
}

void Editor::setWarpMode(const juce::String& id, int choice)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) return;
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, warpModeCount - 1, choice))));
}

// What the two fields on each oscillator are showing. Called on the way in, on
// every tab change and on every tick, because a mode can move without the panel
// being touched -- a preset loaded, a host automating it, a second editor on
// the same plugin -- and the field has to say what the engine is running.
//
// Only a field that has actually moved is rebuilt, which is what makes this
// cheap enough to run on a timer.
void Editor::refreshWarpFields()
{
    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;
            if (!isWarpControl(control.id)) continue;

            // The depth knob beside a field. Most modes do nothing at nothing,
            // but four of them do nothing at twelve o'clock instead, so a
            // double-click returns the knob to whichever of the two its mode
            // actually means -- which is the only way the middle of a bipolar
            // warp is somewhere the hand can get back to.
            if (control.selector == nullptr)
            {
                const auto mode = warpModeOf(value(control.id + "Mode"));
                control.slider.setDoubleClickReturnValue(true, warpNeutralDepth(mode));
                continue;
            }

            const auto chosen = juce::jlimit(0, warpModeCount - 1, juce::roundToInt(value(control.id)));
            const auto listed = control.selector->count() == warpModeCount;
            if (listed && control.selector->chosen == chosen) continue;
            if (!listed)
            {
                control.selector->choices.clear();
                for (int mode = 0; mode < warpModeCount; ++mode)
                    control.selector->choices.push_back(warpModeName(mode));
            }
            control.selector->chosen = chosen;
            // The field says what it is set to; the tooltip says what that
            // setting does, read out of the same table the engine renders from.
            control.selector->setTooltip(ui::warpTooltipFor(chosen));
            control.selector->repaint();
        }
}

// The list, grouped the way the manual groups it: OFF and SYNC are one item
// each because they are one mode each, and the four families holding more than
// one open a submenu. Twenty-six items in a single column would be a list to
// read rather than a menu to use.
void Editor::showWarpMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = juce::jlimit(0, warpModeCount - 1, juce::roundToInt(value(control.id)));

    juce::PopupMenu menu;
    for (int category = 0; category < warpCategoryCount; ++category)
    {
        const auto group = static_cast<WarpCategory>(category);
        juce::PopupMenu submenu;
        auto held = 0;
        auto only = 0;
        for (int mode = 0; mode < warpModeCount; ++mode)
        {
            if (warpCategoryOf(static_cast<WarpMode>(mode)) != group) continue;
            ++held;
            only = mode;
            juce::PopupMenu::Item item(warpModeName(mode));
            item.itemID = mode + 1;
            item.isTicked = mode == current;
            submenu.addItem(item);
        }
        if (held == 0) continue;
        if (held == 1)
        {
            juce::PopupMenu::Item item(warpModeName(only));
            item.itemID = only + 1;
            item.isTicked = only == current;
            menu.addItem(item);
            continue;
        }
        // The family the current mode belongs to is ticked as well as the mode
        // inside it, so a closed menu still says where the setting lives.
        menu.addSubMenu(warpCategoryName(group), submenu, true, nullptr,
                        warpCategoryOf(warpModeOf(static_cast<float>(current))) == group);
    }

    // At the foot, past a rule, the way the manual's own menu has it: the two
    // stages change places. The modes swap and the depths stay put, which is
    // exactly what the manual describes -- WARP 1's knob is left driving
    // whatever was on the right, and that is the point of it, because the pair
    // of knobs is where the hand already is.
    menu.addSeparator();
    menu.addItem(warpModeCount + 1, "Swap warp 1 and warp 2");

    const auto safe = juce::Component::SafePointer<Editor>(this);
    const auto id = control.id;
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe, id] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == warpModeCount + 1) { safe->swapWarpModes(id); return; }
        safe->setWarpMode(id, choice - 1);
    });
}

// The two stages of whichever oscillator this field belongs to, exchanged. The
// id says which: "oscBWarp2Mode" is oscillator B, and which of the two stages
// was clicked does not matter because both end up holding the other's mode.
void Editor::swapWarpModes(const juce::String& id)
{
    const auto prefix = id.startsWith("oscB") ? "oscB" : "oscA";
    const auto first = juce::String(prefix) + "Warp1Mode";
    const auto second = juce::String(prefix) + "Warp2Mode";
    const auto held = juce::roundToInt(value(first));
    setWarpMode(first, juce::roundToInt(value(second)));
    setWarpMode(second, held);
    refreshWarpFields();
    repaint();
}

// One oscillator's two stages, resolved the way the engine resolves them, so
// the tube draws the very warp the voice is rendering. The note and the rate
// are the filter modes' business and the display draws none of those, so what
// is handed in for them does not matter.
std::array<WarpStage, warpSlots> Editor::warpStagesOf(const char* prefix) const
{
    std::array<WarpStage, warpSlots> stages {};
    for (int slot = 0; slot < warpSlots; ++slot)
    {
        const auto id = juce::String(prefix) + "Warp" + juce::String(slot + 1);
        stages[static_cast<size_t>(slot)] = warpStageFor(value(id + "Mode"), value(id), 0.0f, 48000.0);
    }
    return stages;
}

// One slot set to what its type opens on. The values live beside the type in
// ForgeFx.h rather than here, because what a reverb should open on is a fact
// about reverbs and not about this panel.
void Editor::initialiseFxSlot(int rack, int slot)
{
    const auto& info = processor.fxSlotType(rack, slot);
    const auto set = [this] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    for (int knob = 0; knob < fxKnobCount; ++knob)
        set(fxParameterId(rack, slot, "Knob") + juce::String(knob + 1),
            info.init[static_cast<size_t>(knob)]);
    set(fxParameterId(rack, slot, "Mix"), info.initMix);
    // The modes stay where they are: their first choice is the ordinary one for
    // every type, and a slot that has just been given a type is already on it.
    refreshFxSlots();
    repaint();
}

// Filling a slot. The list is the types in the order they are declared, with
// OFF at the top as the way to empty a slot again — the same list the type
// parameter holds, so nothing here can offer a type the engine does not have.
void Editor::showFxTypeMenu(Control& control)
{
    auto* parameter = processor.state.getParameter(control.id);
    if (parameter == nullptr) return;

    juce::PopupMenu menu;
    menu.addSectionHeader("Put in this slot");
    const auto current = juce::roundToInt(control.slider.getValue());
    for (int type = 0; type < fxTypeCount; ++type)
    {
        juce::PopupMenu::Item item(fxTypeName(type));
        item.itemID = type + 1;
        item.isTicked = type == current;
        // The list is read by colour as much as by name, so it carries the same
        // colours the plates do.
        item.colour = type == 0 ? ui::mutedText : ui::fxTypeColour(type);
        menu.addItem(item);
    }

    int rack = 0, slot = 0;
    if (!fxControlAt(control.id, rack, slot)) return;

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.plate.get()),
                       [safe, parameter, rack, slot] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        const auto type = choice - 1;
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(type)));
        // Putting an effect into a slot sets that effect up, the way adding a
        // module in Serum loads its default preset. Only from here: a type
        // arriving from a preset or from a host's automation lane must land
        // with the values that came with it, not with these on top.
        //
        // OFF is left alone deliberately, so emptying a slot and putting the
        // same type back finds it as it was.
        if (type != 0) safe->initialiseFxSlot(rack, slot);
    });
}

FxSlot Editor::fxSlotOf(int rack, int slot) const
{
    const auto id = [rack, slot] (const char* suffix) { return fxParameterId(rack, slot, suffix); };
    FxSlot held;
    held.type = value(id("Type"));
    held.modeA = value(id("ModeA"));
    held.modeB = value(id("ModeB"));
    held.bypass = value(id("Bypass"));
    for (int knob = 0; knob < fxKnobCount; ++knob)
        held.knobs[static_cast<size_t>(knob)] = value(id("Knob") + juce::String(knob + 1));
    held.mix = value(id("Mix"));
    held.level = value(id("Level"));
    return held;
}

// A shelf behind each slot, lit down its left edge in the type's colour. Drawn
// here rather than by the module shell because there are four of them inside
// one module, and which colour each takes is a parameter rather than a
// declaration.
// Only the rack's own box, rather than the whole panel: this runs for every
// step of a knob being turned.
void Editor::repaintFxDisplays()
{
    for (const auto& module : ui::modules())
        if (juce::String(module.id) == "fx")
            repaint(ui::moduleBounds(getLocalBounds(), module));
}

void Editor::paintFxShelves(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& module)
{
    const auto rack = shownRack();
    for (int slot = 0; slot < static_cast<int>(module.rows.size()) && slot < fxSlotCount; ++slot)
    {
        const auto row = ui::rowBounds(area, module, slot);
        const auto held = fxSlotOf(rack, slot);
        // Widened past the controls by the module's own padding, so the shelves
        // read as the full width of the rack rather than as a box around the
        // knobs.
        ui::drawFxShelf(g, row.expanded(6, 1), juce::roundToInt(held.type), true);
        // A slot that is bypassed still says what is in it, dimmed — the point
        // of a bypass is to hear a rack without it and put it straight back.
        ui::drawFxDisplay(g, ui::rowDisplayBounds(area, module, slot).reduced(4, 6), held,
                          processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0,
                          processor.hostTempo(), fxOn(held.bypass) ? 0.3f : 1.0f);
    }
}

void Editor::buildTabs()
{
    for (int i = 0; i < ui::tabCount; ++i)
    {
        const auto target = ui::tabPages[i];
        auto tab = std::make_unique<ui::PageTab>(ui::pageName(target));
        // Violet for the two tabs that stand something else in the
        // oscillators' place, blue for the signal path itself. The mixer takes
        // the whole row rather than standing in for a pair of modules, so it
        // keeps the signal path's colour.
        tab->accent = target == ui::Page::matrix || target == ui::Page::table
                       || target == ui::Page::fx
                          ? ui::signalViolet : ui::electricBlue;
        tab->setTooltip([target]
        {
            switch (target)
            {
                case ui::Page::matrix: return "Show the modulation matrix in place of the oscillators";
                case ui::Page::table:  return "Draw on the oscillators' wavetables";
                case ui::Page::mix:    return "Balance and route every source, the filter and the two busses";
                case ui::Page::fx:     return "The effects racks: one on the main output and one on each bus";
                case ui::Page::oscillators: break;
            }
            return "Show the oscillators";
        }());
        tab->setToggleState(target == page, juce::dontSendNotification);
        tab->onClick = [this, target] { showPage(target); };
        addAndMakeVisible(*tab);
        tabs.push_back(std::move(tab));
    }
}

void Editor::showPage(ui::Page target)
{
    if (page == target) return;
    page = target;
    for (int i = 0; i < ui::tabCount; ++i)
        tabs[static_cast<size_t>(i)]->setToggleState(ui::tabPages[i] == page, juce::dontSendNotification);
    applyPage();
}

// A module either declares a page or stays put. Hiding rather than rebuilding
// keeps every attachment alive, so a knob the tab is covering is still driven
// by the host and by the matrix while it is out of sight.
//
// What is on screen is worked out in one place, because the tab is not the only
// thing that decides it: a control sharing a cell is also hidden while it is
// not the reading in charge.
void Editor::applyPage()
{
    applyEnableStates();
    // Before the layout pass below, because which of a slot's knobs are on
    // screen is what that pass is placing.
    refreshFxSlots();
    refreshWarpFields();
    if (tablePanel != nullptr) tablePanel->setVisible(page == ui::Page::table);
    // Last, because a macro's handle takes the place of its label and the
    // layout pass is what decides that.
    resized();
    repaint();
}

void Editor::buildModules()
{
    for (const auto& descriptor : ui::modules())
    {
        ModuleUi module;
        module.descriptor = &descriptor;
        const auto accent = ui::accentFor(descriptor);

        if (descriptor.enableId != nullptr)
        {
            module.enable = std::make_unique<ui::EnableLed>();
            module.enable->accent = accent;
            module.enable->setTooltip(ui::tooltipFor(descriptor.enableId));
            // Repaint the whole panel: switching a module off dims its shell,
            // its display and every knob inside it, not just the dot.
            module.enable->onClick = [this] { applyEnableStates(); repaint(); };
            addAndMakeVisible(*module.enable);
            module.enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.state, descriptor.enableId, *module.enable);
        }

        for (int r = 0; r < static_cast<int>(descriptor.rows.size()); ++r)
        {
            const auto& row = descriptor.rows[static_cast<size_t>(r)];
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                const auto& declared = row.controls[static_cast<size_t>(i)];
                auto control = std::make_unique<Control>();
                control->style = declared.style;
                control->id = declared.id;
                control->disabledBy = declared.disabledBy;
                control->enabledBy = declared.enabledBy;
                control->row = r;
                control->index = i;
                control->bank = ui::bankOf(descriptor, r, i);

                if (declared.style == ui::Style::selector)
                {
                    control->selector = std::make_unique<ui::FxSelector>();
                    auto* held = control.get();
                    // Two fields wear the same component. A rack slot's mode
                    // means whatever the type in that slot says it means, so it
                    // is stored as a plain 0..1 and spread across the choices;
                    // an oscillator's warp mode is a fixed list, so it is a
                    // genuine choice parameter and the index is the value.
                    if (isWarpControl(declared.id))
                    {
                        control->selector->accent = accent;
                        control->selector->onChoose = [this, held] (int choice)
                        {
                            setWarpMode(held->id, choice);
                        };
                        control->selector->onOpenList = [this, held] { showWarpMenu(*held); };
                    }
                    else
                    {
                        control->selector->onChoose = [this, held] (int choice)
                        {
                            setFxMode(held->id, held->selector->count(), choice);
                        };
                        control->selector->onOpenList = [this, held] { showFxModeMenu(*held); };
                    }
                    // As with the plate: the slider is here for its attachment,
                    // never shown and never added as a child. The attachment is
                    // what a host reads and writes the mode through.
                    control->slider.setSliderStyle(juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    if (isWarpControl(declared.id))
                        control->slider.onValueChange = [this] { refreshWarpFields(); repaint(); };
                    else
                        control->slider.onValueChange = [this] { refreshFxSlots(); repaintFxDisplays(); };
                    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, declared.id, control->slider);

                    control->label.setText(declared.label, juce::dontSendNotification);
                    control->label.setJustificationType(juce::Justification::centred);
                    control->label.setColour(juce::Label::textColourId, ui::mutedText);
                    control->label.setFont(juce::FontOptions(9.0f));
                    addAndMakeVisible(control->label);
                    addAndMakeVisible(*control->selector);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::plate)
                {
                    control->plate = std::make_unique<ui::FxPlate>();
                    control->plate->setTooltip(ui::tooltipFor(declared.id));
                    auto* held = control.get();
                    control->plate->onPlateClick = [this, held] { showFxTypeMenu(*held); };
                    // The slider is never shown and never added as a child: it
                    // is here for its attachment, which is what a host reads
                    // and writes the type through. The plate draws the value
                    // and the menu sets it.
                    control->slider.setSliderStyle(juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    control->slider.onValueChange = [this] { refreshFxSlots(); resized(); repaint(); };
                    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, declared.id, control->slider);
                    addAndMakeVisible(*control->plate);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::chip)
                {
                    control->chip = std::make_unique<ui::ToggleChip>(declared.label);
                    control->chip->accent = accent;
                    control->chip->setTooltip(ui::tooltipFor(declared.id));
                    control->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, declared.id, *control->chip);
                    addAndMakeVisible(*control->chip);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                control->label.setText(declared.label, juce::dontSendNotification);
                control->label.setJustificationType(juce::Justification::centred);
                control->label.setColour(juce::Label::textColourId, ui::mutedText);
                control->label.setFont(juce::FontOptions(declared.style == ui::Style::stepper ? 9.0f : 10.0f));

                if (declared.style == ui::Style::rocker)
                {
                    // No ON/OFF readout: the switch shows its own state, by
                    // moving as well as lighting up.
                    control->rocker = std::make_unique<ui::RockerSwitch>(declared.label);
                    control->rocker->accent = accent;
                    control->rocker->setTooltip(ui::tooltipFor(declared.id));
                    control->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, declared.id, *control->rocker);

                    addAndMakeVisible(*control->rocker);
                    addAndMakeVisible(control->label);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::knob)
                {
                    control->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    // No printed value. The reading appears in the bubble while
                    // the knob is being turned, which is the only time anyone
                    // was reading it.
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                }
                else
                {
                    // Every one of these routes to drawLinearSlider. A stepper
                    // is a bar painted as a numeric field; a matrix amount is a
                    // horizontal bar, and drags along the fill it draws; a
                    // fader is a genuine vertical slider with a thumb that
                    // travels, and is the only one of the three that prints no
                    // value of its own.
                    control->slider.setSliderStyle(
                        declared.style == ui::Style::bar     ? juce::Slider::LinearBar
                        : declared.style == ui::Style::fader ? juce::Slider::LinearVertical
                                                             : juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                }
                control->slider.setLookAndFeel(&lookAndFeel);
                control->slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
                control->slider.setColour(juce::Slider::rotarySliderOutlineColourId, ui::line);
                control->slider.setColour(juce::Slider::thumbColourId, accent);
                control->slider.setColour(juce::Slider::textBoxTextColourId, ui::text);
                control->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                control->slider.setTooltip(ui::tooltipFor(declared.id));
                if (declared.style == ui::Style::knob || declared.style == ui::Style::fader)
                {
                    // onDragStart/onDragEnd/onValueChange are the editor's to
                    // use: the parameter attachment listens as a Slider
                    // ::Listener and leaves these alone.
                    auto* held = control.get();
                    control->slider.onDragStart = [this, held] { bubbleHeld = true; showValueBubble(*held); };
                    control->slider.onDragEnd = [this] { bubbleHeld = false; };
                    // Fires for a wheel notch and a double-click as well as for
                    // a drag, and for host automation, which is why it only
                    // refreshes a bubble the hand has already opened — or opens
                    // one for a gesture that never started a drag.
                    const auto rackControl = control->id.startsWith("fx");
                    control->slider.onValueChange = [this, held, rackControl]
                    {
                        if (bubbleHeld || held->slider.isMouseOverOrDragging()) showValueBubble(*held);
                        else if (bubbleControl == held) showValueBubble(*held);
                        // A knob repaints itself, not the panel around it, so a
                        // slot's display would sit still while its own knob was
                        // being turned.
                        if (rackControl) repaintFxDisplays();
                    };
                }
                // A mode field changes what the knobs beside it read — a
                // delay's time is milliseconds or a division depending on it —
                // so the readouts are pushed when it moves.
                if (control->id.startsWith("fx")
                    && (control->id.endsWith("ModeA") || control->id.endsWith("ModeB")))
                    control->slider.onValueChange = [this] { refreshFxSlots(); repaintFxDisplays(); };
                // The attachment installs the parameter's own text formatting,
                // so it must be created before anything reads the slider's text.
                control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.state, declared.id, control->slider);
                // Double-click returns a control to its default. The attachment
                // has already applied the parameter's range, so the default can
                // be read back from it here.
                if (auto* parameter = processor.state.getParameter(declared.id))
                {
                    control->slider.setDoubleClickReturnValue(
                        true, parameter->convertFrom0to1(parameter->getDefaultValue()));
                    // Semitone is a continuous parameter so the matrix can
                    // sweep pitch smoothly through it, but a tuning field
                    // should still land on whole semitones under the hand.
                    if (juce::String(declared.id).endsWith("Semitone"))
                        control->slider.setRange(-12.0, 12.0, 1.0);
                    // Position is the same bargain one step further on. It picks
                    // a frame out of the oscillator's table, so a hand on it
                    // lands on a shape rather than a hair short of one, while
                    // the matrix still sweeps the whole table smoothly. Hold the
                    // fine modifier to stop between two frames on purpose.
                    if (juce::String(declared.id).endsWith("Position"))
                        control->slider.gestureSteps = waveShapeCount;  // refined in applyTableCounts
                }

                // The editor handles right-click so a knob can offer its
                // modulation menu without each slider knowing about the matrix.
                control->slider.addMouseListener(this, false);
                // The ring knows the gesture; the editor knows which slot it
                // belongs to, and that is settled afresh on every refresh.
                control->slider.onRingDrag = [this, knob = control.get()] (float depth)
                {
                    if (knob->ringSlot >= 0) setSlotDepth(knob->ringSlot, depth);
                };
                // A table names its columns once, in the strip above its rows,
                // so its controls carry no label of their own.
                if (descriptor.columnHeaderHeight == 0) addAndMakeVisible(control->label);
                addAndMakeVisible(control->slider);
                module.controls.push_back(std::move(control));
            }
        }

        moduleUis.push_back(std::move(module));
    }
    buildBankButtons();
}

// A module declared in banks carries one numbered button per bank in its
// header. Which bank is showing is the panel's business and not the host's: it
// says which envelope or which LFO you are looking at, not what the synth is
// doing, so it is no more a parameter than which tab is open.
void Editor::buildBankButtons()
{
    for (auto& module : moduleUis)
    {
        const auto banks = ui::bankCount(*module.descriptor);
        if (banks <= 1) continue;
        const auto accent = ui::accentFor(*module.descriptor);
        // A bank is usually one of several numbered copies of one thing — ENV 3,
        // LFO 5 — and the card says the number. The rack's three banks are not
        // copies: they are the main output and the two busses, and a card
        // reading "2" would not say which. A module that asks for wider cards
        // is one whose banks have names.
        const auto named = module.descriptor->bankWidth > 0;
        for (int bank = 0; bank < banks; ++bank)
        {
            const auto caption = named ? juce::String(rackName(bank)) : juce::String(bank + 1);
            auto button = std::make_unique<ui::BankCard>(caption);
            button->accent = accent;
            button->setClickingTogglesState(false);
            button->setToggleState(bank == module.bank, juce::dontSendNotification);
            button->setTooltip(named ? "Show the effects rack on " + caption
                                     : "Show " + juce::String(module.descriptor->title) + " " + caption);
            button->onClick = [this, which = &module, bank] { showBank(*which, bank); };
            addAndMakeVisible(*button);
            module.bankButtons.push_back(std::move(button));
        }
    }
}

void Editor::showBank(ModuleUi& module, int bank)
{
    if (module.bank == bank) return;
    module.bank = bank;
    for (int i = 0; i < static_cast<int>(module.bankButtons.size()); ++i)
        module.bankButtons[static_cast<size_t>(i)]->setToggleState(i == bank, juce::dontSendNotification);
    // The handle in the header drags whichever of them is showing, so the
    // layout has to run again to put the right one there.
    applyEnableStates();
    // Switching the rack showing is switching which twelve-by-four set of
    // controls is on screen, and each slot's knobs are named by whatever type
    // that slot holds.
    refreshFxSlots();
    refreshModulationRings();
    resized();
    repaint();
}

// Which LFO the panel is showing. The module is the one that declares the LFO
// display, so nothing here has to know its id.
int Editor::shownLfo() const
{
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::lfo)
            return juce::jlimit(0, lfoCount - 1, module.bank);
    return 0;
}

// Which envelope the panel is showing. The module is the one that declares the
// envelope display, so nothing here has to know its id.
int Editor::shownEnv() const
{
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::envelope)
            return juce::jlimit(0, envCount - 1, module.bank);
    return ampEnv;
}

// What the envelope module's header says. While a note is sounding that is the
// stage the envelope showing has reached — the reading the knobs cannot give,
// because it is a position in a shape rather than the shape. At rest there is
// no stage to name, so it says what that envelope is for instead: ENV 1 is
// wired to the amplitude, and the other three go nowhere until a slot sends
// them.
juce::String Editor::envHeaderDetail() const
{
    const auto env = shownEnv();
    const juce::String stage = ui::stageName(static_cast<ui::Stage>(
        juce::jlimit(0, 4, processor.envelopeStage(env))));
    if (stage.isNotEmpty()) return stage;
    return env == ampEnv ? "AMP" : "SOURCE";
}

void Editor::applyEnableStates()
{
    for (auto& module : moduleUis)
    {
        const auto onPage = ui::onPage(*module.descriptor, page);
        if (module.enable != nullptr) module.enable->setVisible(onPage);
        for (auto& button : module.bankButtons) button->setVisible(onPage);

        for (auto& control : module.controls)
        {
            // A control is live when its module is on and nothing else has
            // taken it over — polyphony means nothing once mono is switched on,
            // and a tempo division means nothing while the rate is in Hertz.
            const auto on = module.on()
                && (control->disabledBy == nullptr || value(control->disabledBy) < 0.5f)
                && (control->enabledBy == nullptr || value(control->enabledBy) >= 0.5f)
                // A rack knob its modes have made meaningless greys out, the
                // same way polyphony does under mono. It is still here; it just
                // has nothing to do until the mode beside it moves.
                && fxKnobLive(*control);
            // A control that shares its cell leaves rather than greys out: the
            // other reading of the same setting is standing in the same place,
            // and a greyed control would be sitting on top of the live one.
            const auto shown = onPage
                // A module declared in banks has only one of them on screen.
                && control->bank == module.bank
                // A rack knob its slot's type does not have is not a control
                // at all while that type is in there.
                && fxControlUsed(*control)
                && (on || !ui::inSharedCell(*module.descriptor, control->row, control->index));

            control->label.setVisible(shown);
            if (control->plate != nullptr)
            {
                control->plate->setEnabled(on);
                control->plate->setVisible(shown);
                continue;
            }
            if (control->selector != nullptr)
            {
                control->selector->setEnabled(on);
                control->selector->setVisible(shown);
                continue;
            }
            if (control->chip != nullptr)
            {
                control->chip->setEnabled(on);
                control->chip->setVisible(shown);
                continue;
            }
            if (control->rocker != nullptr)
            {
                control->rocker->setEnabled(on);
                control->rocker->setVisible(shown);
            }
            else
            {
                control->slider.setEnabled(on);
                control->slider.setVisible(shown);
                control->slider.setColour(juce::Slider::textBoxTextColourId,
                                          ui::text.withAlpha(on ? 1.0f : 0.4f));
            }
            control->label.setColour(juce::Label::textColourId,
                                     ui::mutedText.withAlpha(on ? 1.0f : 0.4f));
        }
    }
}

// Read from the parameter itself rather than from the cached atomic beside it.
//
// That atomic is kept up to date by one of the parameter's listeners, and the
// panel's own attachments are others. The order listeners are called in is not
// defined — so a panel answering a change it made itself could be called first
// and read the value from *before* that change. It corrected itself the next
// time anything refreshed, which is why a mode field appeared to wait for an
// unrelated click or a tab switch before it caught up.
//
// The parameter's own value has no such race: setValueNotifyingHost stores it
// before it tells anybody. This costs a lookup by name per read, which at the
// rate the panel refreshes is nothing, and the atomic stays what the audio
// thread reads.
float Editor::value(const juce::String& id) const
{
    if (const auto* parameter = processor.state.getParameter(id))
        return parameter->convertFrom0to1(parameter->getValue());
    const auto* raw = processor.state.getRawParameterValue(id);
    return raw == nullptr ? 0.0f : raw->load();
}

void Editor::paint(juce::Graphics& g)
{
    ui::drawBackdrop(g, getLocalBounds());

    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (!ui::onPage(descriptor, page)) continue;
        const auto area = ui::moduleBounds(getLocalBounds(), descriptor);
        const auto on = module.on();
        const auto alpha = on ? 1.0f : 0.35f;
        // A module's header carries what it is doing rather than what it is:
        // the envelope names the stage it is in, and the LFO names the rate it
        // is actually running at, which in sync is a tempo division and so
        // cannot be read off the greyed-out rate knob.
        const auto tableModule = juce::String(descriptor.id) == "table";
        const auto rackModule = juce::String(descriptor.id) == "fx";
        ui::drawModuleShell(g, area, descriptor, on,
                            descriptor.display == ui::Display::envelope ? envHeaderDetail()
                            : descriptor.display == ui::Display::lfo ? lfoHeaderDetail()
                            : rackModule ? fxHeaderDetail()
                            : tableModule && tablePanel != nullptr ? tablePanel->headerDetail()
                                : juce::String());

        if (descriptor.columnHeaderHeight > 0) paintTable(g, area, descriptor);
        if (rackModule) paintFxShelves(g, area, descriptor);

        const auto display = ui::displayBounds(area, descriptor);
        if (display.isEmpty()) continue;
        const auto accent = ui::accentFor(descriptor);
        // An oscillator shows its wave on a picture tube; the other displays
        // stay flat wells, which is what keeps the tubes reading as screens.
        if (descriptor.display == ui::Display::oscillator)
            ui::drawCrtScreen(g, display, accent, alpha);
        else if (descriptor.display == ui::Display::envelope)
            // Short of the full strip: the zoom control has its own well beside
            // this one, and draws it with the curve.
            ui::drawDisplayWell(g, ui::envelopePlotBounds(display));
        else
            ui::drawDisplayWell(g, display);
        switch (descriptor.display)
        {
            case ui::Display::oscillator:
                // Each oscillator draws the table it is actually reading, taken
                // from the frames as authored rather than from a band-limited
                // copy — so the tube shows the table and not a formula, and not
                // whichever copy the note being held happens to want.
                if (const auto* source = ui::displaySourceId(descriptor))
                {
                    // Warped as the voice warps it, which is what the manual
                    // means when it says the 2D view shows what the mode is
                    // doing. The modes it cannot honestly draw take themselves
                    // off the picture -- see drawWaveform.
                    const auto second = juce::String(descriptor.id) == "oscB";
                    ui::drawWaveform(g, display, processor.tableStore().edit(second ? 1 : 0),
                                     value(source), warpStagesOf(second ? "oscB" : "oscA"),
                                     accent, alpha);
                }
                break;
            case ui::Display::envelope:
            {
                // Whichever envelope the module is showing, drawn from its own
                // knobs, its own window and its own live reading.
                const auto env = shownEnv();
                const auto stage = static_cast<ui::Stage>(
                    juce::jlimit(0, 4, processor.envelopeStage(env)));
                ui::drawEnvelope(g, display, value(envParameterId(env, "Attack")),
                                 value(envParameterId(env, "Decay")),
                                 value(envParameterId(env, "Sustain")),
                                 value(envParameterId(env, "Release")), accent, alpha,
                                 stage, processor.envelopeLevel(env),
                                 envelopeZoom[static_cast<size_t>(env)]);
                break;
            }
            case ui::Display::lfo:
            {
                const auto lfo = shownLfo();
                ui::drawLfo(g, display,
                            static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
                                                               juce::roundToInt(value(lfoParameterId(lfo, "Shape"))))),
                            processor.lfoPhase(lfo), processor.lfoValue(lfo), accent, alpha);
                break;
            }
            case ui::Display::filter:
                ui::drawFilterResponse(g, display, filterTypeOf(value("filterType")),
                                       value("cutoff"), value("resonance"), accent, alpha);
                break;
            case ui::Display::none:
                break;
        }
    }

    // The line follows a source handle to the cursor, and the knob under it
    // lights up, so a drop lands where it looks like it will.
    if (draggingHandle != nullptr)
    {
        const auto from = draggingHandle->getBounds().toFloat().getCentre();
        const auto to = dragPosition.toFloat();
        g.setColour(ui::signalViolet.withAlpha(0.55f));
        g.drawLine({from, to}, 2.0f);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(to));

        if (const auto* target = const_cast<Editor*>(this)->controlAt(dragPosition))
        {
            const auto reachable = destinationFor(target->id) != 0;
            g.setColour((reachable ? ui::signalViolet : ui::mutedText).withAlpha(0.9f));
            g.drawRoundedRectangle(target->slider.getBounds().toFloat().reduced(2.0f), 4.0f, 1.6f);
        }
    }
}

// The rate LFO 1 is actually running at, for its header. Set in beats that is a
// division of the host's tempo, which is the one reading the knob cannot give
// on its own — so the division is named and the Hertz it works out to is put
// beside it.
juce::String Editor::lfoHeaderDetail() const
{
    const auto lfo = shownLfo();
    const auto hertz = juce::String(processor.lfoRateHz(lfo), 2) + " HZ";
    if (value(lfoParameterId(lfo, "RateUnit")) < 0.5f) return hertz;

    const auto division = lfoDivisions()[static_cast<size_t>(
        juce::jlimit(0, lfoDivisionCount - 1,
                     juce::roundToInt(value(lfoParameterId(lfo, "Division")))))].label;
    return juce::String(division) + " // " + hertz;
}

// A slot counts as live once it has both ends: something driving it and
// somewhere to go. Depth is left out deliberately, so a slot parked at zero
// still reads as a routing you set up rather than as an empty row.
bool Editor::slotIsLive(int slot) const
{
    return juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) > 0
        && juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) > 0;
}

// The furniture that makes eight rows of fields read as a table: the column
// titles, once, above the rows; a rule under them; and every row numbered in
// the gutter, against a band on alternate rows.
void Editor::paintTable(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& descriptor)
{
    const auto accent = ui::accentFor(descriptor);
    const auto titles = ui::columnTitleBounds(area, descriptor);

    ui::drawColumnTitle(g, titles.withWidth(descriptor.rowGutter), "#");
    const auto& first = descriptor.rows.front().controls;
    for (int i = 0; i < static_cast<int>(first.size()); ++i)
    {
        const auto cell = ui::cellBounds(area, descriptor, 0, i);
        ui::drawColumnTitle(g, titles.withX(cell.getX()).withWidth(cell.getWidth()),
                            first[static_cast<size_t>(i)].label);
    }
    ui::drawColumnTitleRule(g, titles);

    for (int row = 0; row < static_cast<int>(descriptor.rows.size()); ++row)
        ui::drawTableRow(g, ui::rowBounds(area, descriptor, row),
                         ui::rowGutterBounds(area, descriptor, row),
                         row + 1, slotIsLive(row), accent);
}

void Editor::resized()
{
    const auto right = getWidth() - ui::windowMargin;
    savePreset.setBounds(right - 62, 26, 62, 26);
    loadPreset.setBounds(right - 130, 26, 62, 26);
    presetName.setBounds(right - 350, 26, 212, 26);

    for (int i = 0; i < ui::tabCount && i < static_cast<int>(tabs.size()); ++i)
        tabs[static_cast<size_t>(i)]->setBounds(ui::tabBounds(i));

    const auto keys = ui::keyboardBounds(getLocalBounds());
    // Sized so the full eighty-eight keys span the panel exactly, rather than
    // running out partway and leaving a blank stretch.
    keyboard.setKeyWidth(static_cast<float>(keys.getWidth()) / 52.0f);
    keyboard.setBounds(keys);

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
    for (auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        const auto area = ui::moduleBounds(getLocalBounds(), descriptor);

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
                    other->setVisible(descriptor.handleSource + bank == showing);
            // Flush with the module's top edge and the full height of the
            // header: the handle is a card hanging off that edge, not a button
            // floating inside it.
            if (auto* handle = handleFor(showing))
                handle->setBounds(area.getX() + (descriptor.enableId != nullptr ? ui::headerHeight + 8 : 10),
                                  area.getY(), ui::handleWidth, ui::headerHeight);
        }
        for (int bank = 0; bank < static_cast<int>(module.bankButtons.size()); ++bank)
            module.bankButtons[static_cast<size_t>(bank)]
                ->setBounds(ui::bankButtonBounds(area, descriptor, bank, ui::handleWidth));

        for (auto& held : module.controls)
        {
            auto& control = *held;
            auto block = ui::controlBlock(area, descriptor, control.row, control.index, diameter);

            // A macro's drag handle replaces its numeric label: the number is
            // the thing you grab, and the knob keeps its own drag gesture.
            if (control.id.startsWith("macro"))
            {
                const auto macro = control.id.getTrailingIntValue();
                if (auto* handle = handleFor(static_cast<int>(ModSource::macro1) + macro - 1))
                {
                    const auto labelRow = block.removeFromTop(ui::knobLabelHeight);
                    handle->setBounds(juce::Rectangle<int>(26, ui::knobLabelHeight)
                                          .withCentre(labelRow.getCentre()));
                    control.label.setVisible(false);
                    control.slider.setBounds(block);
                    continue;
                }
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
                case ui::Style::selector:
                    control.label.setBounds(block.removeFromTop(ui::stepperLabelHeight));
                    control.selector->setBounds(block);
                    break;
                case ui::Style::fader:
                    // The same label line a knob's sits on, so a row of faders
                    // and a row of knobs line up across the mixer.
                    control.label.setBounds(block.removeFromTop(ui::knobLabelHeight));
                    control.slider.setBounds(block);
                    break;
                case ui::Style::stepper:
                    // Inside a table the column title is the label, so the
                    // field takes the whole block rather than the half of it
                    // left under a label strip.
                    if (descriptor.columnHeaderHeight == 0)
                        control.label.setBounds(block.removeFromTop(ui::stepperLabelHeight));
                    control.slider.setBounds(block);
                    break;
                case ui::Style::rocker:
                    // Same label line as the knobs either side, so the switch
                    // sits exactly where their circles do.
                    control.label.setBounds(block.removeFromTop(ui::knobLabelHeight));
                    control.rocker->setBounds(ui::rockerBounds(block));
                    break;
                case ui::Style::knob:
                    control.label.setBounds(block.removeFromTop(ui::knobLabelHeight));
                    control.slider.setBounds(block);
                    break;
            }
        }
    }
}

void Editor::buildHandles()
{
    const auto add = [this] (int source, const juce::String& caption, juce::Colour accent,
                             bool card = false)
    {
        auto handle = std::make_unique<ui::SourceHandle>(source, caption);
        handle->accent = accent;
        handle->card = card;
        handle->setTooltip("Drag " + juce::String(modSourceName(source)) + " onto a knob");
        handle->addMouseListener(this, false);
        addAndMakeVisible(*handle);
        handles.push_back(std::move(handle));
    };

    // One per envelope and one per LFO, though only the one on screen is ever
    // placed: the handle is the module's own title, and each module is showing
    // one of them at a time.
    for (int env = 0; env < envCount; ++env)
        add(static_cast<int>(ModSource::env1) + env, "ENV " + juce::String(env + 1),
            ui::electricBlue, true);
    for (int lfo = 0; lfo < lfoCount; ++lfo)
        add(static_cast<int>(ModSource::lfo1) + lfo, "LFO " + juce::String(lfo + 1), ui::signalViolet, true);
    for (int macro = 0; macro < macroCount; ++macro)
        add(static_cast<int>(ModSource::macro1) + macro, juce::String(macro + 1), ui::electricBlue);
}

// What a dragged source can be dropped on. A knob always answers, whether or not
// the matrix can reach it, so a drop on one it cannot reach is refused out loud
// rather than passing through to whatever is behind it. Every other style that
// can show a routing — the numeric fields — is a target only when it really is
// a destination, which keeps the ones that merely look alike out of the way of
// a drag crossing the panel: an LFO's shape, a channel's routing and the
// matrix's own columns are all fields, and none of them is modulated.
Editor::Control* Editor::controlAt(juce::Point<int> panelPosition)
{
    for (auto& module : moduleUis)
        for (auto& control : module.controls)
            if (control->slider.isVisible() && ui::showsModulation(control->style)
                && control->slider.getBounds().contains(panelPosition)
                && (control->style == ui::Style::knob || destinationFor(control->id) != 0))
                return control.get();
    return nullptr;
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
        return ui::onPage(descriptor, page) ? &descriptor : nullptr;
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

void Editor::mouseDown(const juce::MouseEvent& event)
{
    if (auto* handle = dynamic_cast<ui::SourceHandle*>(event.eventComponent))
    {
        draggingHandle = handle;
        handle->dragging = true;
        dragPosition = event.getEventRelativeTo(this).getPosition();
        repaint();
        return;
    }
    // Only for a click that landed on the panel itself: the editor listens to
    // every knob and handle as well, and those events carry their own component.
    if (event.eventComponent == this && !event.mods.isPopupMenu())
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (const auto display = envelopeDisplayBounds(); !display.isEmpty())
        {
            const auto zoom = envelopeZoom[static_cast<size_t>(shownEnv())];
            if (ui::envelopeZoomIn(display).contains(at)) { setEnvelopeZoom(zoom - 1); return; }
            if (ui::envelopeZoomOut(display).contains(at)) { setEnvelopeZoom(zoom + 1); return; }
            // Double-clicking the plot puts the window back to three seconds,
            // which is the gesture a knob already uses to go back to the value
            // it started at.
            if (event.getNumberOfClicks() >= 2 && ui::envelopePlotBounds(display).contains(at))
            {
                setEnvelopeZoom(ui::envelopeDefaultZoom);
                return;
            }
        }
    }
    if (!event.mods.isPopupMenu()) return;
    for (const auto& module : moduleUis)
        for (const auto& control : module.controls)
            if (event.eventComponent == &control->slider)
            {
                showModulationMenu(control->id);
                return;
            }
}

// The computer keys play Forge wherever the focus happens to be. A key event is
// walked up from whatever holds focus to its parents, so the letter keys reach
// here once a knob has taken focus under the hand, and are handed on to the
// keyboard from here. Without this, touching any control silenced the keys until
// the keyboard itself was clicked back into focus — which is the wrong trade for
// a synth, where turning something while playing it is the whole point.
//
// Forwarding is safe when the keyboard already has focus and has handled the
// event itself: it tracks which notes its keys are holding down, so a second
// pass over the same key state starts and stops nothing.
bool Editor::keyStateChanged(bool isKeyDown)
{
    return keyboard.keyStateChanged(isKeyDown);
}

bool Editor::keyPressed(const juce::KeyPress& key)
{
    // Undo belongs to the editor rather than to the panel because a plugin
    // window gives its keyboard focus to whichever child last took it, and the
    // panel is often not that child. Scoped to the tab that has something to
    // undo, so Ctrl+Z is not swallowed anywhere else.
    if (page == ui::Page::table && tablePanel != nullptr
        && key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        tablePanel->undo();
        applyTableCounts();
        repaint();
        return true;
    }
    // z and x walk the computer keys up and down the piano. Neither letter is
    // in the keyboard's own mapping, so playing loses nothing by lending them.
    if (key == juce::KeyPress('z')) { shiftComputerKeyOctave(-1); return true; }
    if (key == juce::KeyPress('x')) { shiftComputerKeyOctave(1); return true; }
    return keyboard.keyPressed(key);
}

void Editor::shiftComputerKeyOctave(int delta)
{
    // The reach has to land inside the keys that exist, so the lowest octave is
    // the first whose C is on the piano and the highest is the last whose whole
    // reach still fits.
    const auto lowest = (keyboard.getRangeStart() + 11) / 12;
    const auto highest = (keyboard.getRangeEnd() - (computerKeySpan - 1)) / 12;
    const auto shifted = juce::jlimit(lowest, highest, computerKeyOctave + delta);
    if (shifted == computerKeyOctave) return;

    // Notes are tracked by number, so a key held across the shift would be
    // asked to stop on a number nothing started — the note hangs. This is the
    // keyboard's own way of letting everything go, and it is what it does when
    // the focus leaves it mid-chord.
    keyboard.focusLost(juce::Component::focusChangedDirectly);
    computerKeyOctave = shifted;
    keyboard.setKeyPressBaseOctave(computerKeyOctave);
    repaint();
}

// A wash across the keys the letters can reach, with a brighter line under
// them. Faint enough to read as a shadow on the piano rather than as another
// lit thing competing with the notes actually being held.
void Editor::paintOverChildren(juce::Graphics& g)
{
    const auto first = computerKeyOctave * 12;
    const auto last = first + computerKeySpan - 1;
    if (first < keyboard.getRangeStart() || last > keyboard.getRangeEnd()) return;

    const auto reach = (keyboard.getRectangleForKey(first)
                            .getUnion(keyboard.getRectangleForKey(last)))
                           .translated(static_cast<float>(keyboard.getX()),
                                       static_cast<float>(keyboard.getY()));

    g.setColour(ui::electricBlue.withAlpha(0.07f));
    g.fillRect(reach);
    g.setColour(ui::electricBlue.withAlpha(0.3f));
    g.fillRect(reach.withTop(reach.getBottom() - 2.0f));
}

// The wheel over the envelope display changes how much time it spans. Up
// shortens the window, so the shape grows: up is in, the way round every other
// zoom works. The notches are accumulated because a trackpad sends a stream of small
// deltas where a mouse sends one large one, and without this a flick would run
// through the whole ladder before the hand came off it.
void Editor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto display = envelopeDisplayBounds();
    if (display.isEmpty()
        || !ui::envelopePlotBounds(display).contains(event.getEventRelativeTo(this).getPosition()))
    {
        Component::mouseWheelMove(event, wheel);
        return;
    }

    envelopeWheel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
    const auto notches = static_cast<int>(envelopeWheel / wheelPerZoomStep);
    if (notches == 0) return;
    envelopeWheel -= static_cast<float>(notches) * wheelPerZoomStep;
    setEnvelopeZoom(envelopeZoom[static_cast<size_t>(shownEnv())] - notches);
}

void Editor::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingHandle == nullptr) return;
    dragPosition = event.getEventRelativeTo(this).getPosition();
    repaint();
}

void Editor::mouseUp(const juce::MouseEvent& event)
{
    if (draggingHandle == nullptr) return;
    const auto source = draggingHandle->source;
    draggingHandle->dragging = false;
    draggingHandle = nullptr;

    if (auto* control = controlAt(event.getEventRelativeTo(this).getPosition()))
    {
        const auto destination = destinationFor(control->id);
        if (destination != 0) assignModulation(source, destination);
        else
        {
            presetName.setText("THAT CONTROL CANNOT BE MODULATED", juce::dontSendNotification);
            presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        }
    }
    repaint();
}

// Each modulated knob is told three things: how far its slots can move it, how
// far they are moving it right now, and whether anything is sounding at all. Its
// look draws them without knowing anything about the matrix.
void Editor::refreshModulationRings()
{
    // A source only reaches a destination through a voice, so with nothing
    // sounding there is no modulated value to show. The knobs go quiet at the
    // same moment ENV 1's playhead does, and for the same reason.
    const auto live = processor.envelopeStage() != 0;
    std::array<float, destinationCount> depths {};
    // A ring can only be dragged when one slot is behind it. Pointed at by two,
    // it is a sum, and there is nothing a single drag could honestly mean.
    std::array<int, destinationCount> slots {};
    std::array<int, destinationCount> only {};
    only.fill(-1);
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto source = juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8()));
        const auto destination = juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8()));
        if (source <= 0 || destination <= 0 || destination >= destinationCount) continue;
        depths[static_cast<size_t>(destination)] += value(slotParameter(slot, "Depth").toRawUTF8());
        // Counted whatever its depth, so a routing sitting at zero can still be
        // dialled up by its ring rather than only in the matrix.
        ++slots[static_cast<size_t>(destination)];
        only[static_cast<size_t>(destination)] = slot;
    }

    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            // Only the styles that draw a routing are worth walking. One the
            // matrix cannot reach is still visited, because that is what takes
            // a ring back off a control whose slot has just been cleared.
            if (!ui::showsModulation(control->style)) continue;
            const auto destination = destinationFor(control->id);
            const auto depth = destination == 0 ? 0.0f : depths[static_cast<size_t>(destination)];
            const auto offset = destination == 0 ? 0.0f : processor.modulationOffset(destination);

            control->ringSlot = destination != 0 && slots[static_cast<size_t>(destination)] == 1
                ? only[static_cast<size_t>(destination)] : -1;
            control->slider.ringDraggable = control->ringSlot >= 0;
            control->slider.ringDepth = depth;
            auto& properties = control->slider.getProperties();
            if (static_cast<float>(properties.getWithDefault("modDepth", 0.0)) == depth
                && static_cast<float>(properties.getWithDefault("modOffset", 0.0)) == offset
                && static_cast<bool>(properties.getWithDefault("modLive", false)) == live)
                continue;
            properties.set("modDepth", depth);
            properties.set("modOffset", offset);
            properties.set("modLive", live);
            control->slider.repaint();
        }
}

void Editor::showModulationMenu(const juce::String& parameterId)
{
    const auto destination = destinationFor(parameterId);
    if (destination == 0) return;
    const auto label = juce::String(destinations()[static_cast<size_t>(destination)].label);

    juce::PopupMenu menu;
    menu.addSectionHeader("Modulate " + label);

    // Anything already pointed here can be taken away from the same menu, so a
    // routing can be undone where it was made rather than only in the matrix.
    juce::PopupMenu existing;
    auto found = 0;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto source = juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8()));
        if (source <= 0 || juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) != destination)
            continue;
        existing.addItem(1000 + slot, juce::String(modSourceName(source)));
        ++found;
    }

    juce::PopupMenu sources;
    for (int source = 1; source < modSourceCount; ++source)
        sources.addItem(source, modSourceName(source));
    menu.addSubMenu("Add source", sources);
    if (found > 0) menu.addSubMenu("Remove", existing);

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}, [safe, destination] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice >= 1000) safe->clearSlot(choice - 1000);
        else safe->assignModulation(choice, destination);
    });
}

void Editor::assignModulation(int source, int destination)
{
    // Reuse a slot already joining this pair rather than spending a second one
    // on the same routing.
    auto target = -1;
    for (int slot = 0; slot < modSlotCount && target < 0; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) == source
            && juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) == destination)
            target = slot;
    for (int slot = 0; slot < modSlotCount && target < 0; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) <= 0) target = slot;

    if (target < 0)
    {
        presetName.setText("ALL " + juce::String(modSlotCount) + " MOD SLOTS ARE IN USE",
                           juce::dontSendNotification);
        presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        return;
    }

    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    set(slotParameter(target, "Source"), static_cast<float>(source));
    set(slotParameter(target, "Dest"), static_cast<float>(destination));
    // A depth of zero would look like nothing happened. Half is audible, and
    // the ring it lands on is now the way to take it anywhere else.
    if (value(slotParameter(target, "Depth").toRawUTF8()) == 0.0f)
        setSlotDepth(target, 0.5f);
}

// Written through the parameter rather than into the slot, so a depth dragged
// on a ring reaches the host's automation lane and the matrix field by the same
// path a depth typed into the matrix does.
void Editor::setSlotDepth(int slot, float depth)
{
    if (slot < 0 || slot >= modSlotCount) return;
    if (auto* parameter = processor.state.getParameter(slotParameter(slot, "Depth")))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(-1.0f, 1.0f, depth)));
}

void Editor::clearSlot(int slot)
{
    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    set(slotParameter(slot, "Source"), 0.0f);
    set(slotParameter(slot, "Dest"), 0.0f);
    set(slotParameter(slot, "Depth"), 0.0f);
}

void Editor::showValueBubble(Control& control)
{
    bubbleControl = &control;
    // A macro's label is its bare number, and it is hidden anyway — the drag
    // handle stands in its place — so the bubble spells the name out. Same
    // test as the one that puts the handle there in the first place.
    valueBubble.caption = control.id.startsWith("macro")
                              ? "MACRO " + juce::String(control.id.getTrailingIntValue())
                              : control.label.getText();
    valueBubble.reading = control.slider.getTextFromValue(control.slider.getValue());
    valueBubble.accent = control.slider.findColour(juce::Slider::rotarySliderFillColourId);

    const auto knob = control.slider.getBounds();
    const auto size = juce::Rectangle<int>(valueBubble.widthFor(), ui::ValueBubble::heightFor());
    juce::Rectangle<int> placed;
    if (control.style == ui::Style::fader)
    {
        // Beside a fader rather than above it. A fader is as tall as its cell,
        // so "above" is above the label and halfway up the knob over it —
        // covering two things to report a third. Beside the thumb covers
        // nothing and follows the hand up and down the travel.
        const auto thumb = juce::roundToInt(
            ui::faderThumbY(knob.toFloat(),
                            ui::faderProportion(control.slider.getRange(), control.slider.getValue())));
        placed = size.withCentre({knob.getRight() + 6 + size.getWidth() / 2, thumb});
        // Flipped to the other side when the right-hand one would run off the
        // panel, which is what the last channel of the mixer needs.
        if (placed.getRight() > getWidth() - 4)
            placed = size.withCentre({knob.getX() - 6 - size.getWidth() / 2, thumb});
        placed.setY(juce::jlimit(4, juce::jmax(4, getHeight() - size.getHeight() - 4), placed.getY()));
    }
    else
    {
        // Above the knob by preference, below it when the knob is near the top
        // of the panel, and never off either side.
        placed = size.withCentre({knob.getCentreX(), knob.getY() - size.getHeight() / 2 - 6});
        if (placed.getY() < 4) placed.setY(knob.getBottom() + 6);
    }
    placed.setX(juce::jlimit(4, juce::jmax(4, getWidth() - size.getWidth() - 4), placed.getX()));
    valueBubble.setBounds(placed);

    valueBubble.setVisible(true);
    valueBubble.toFront(false);
    valueBubble.repaint();
    bubbleUntil = juce::Time::getMillisecondCounter() + bubbleTailMs;
}

void Editor::fadeValueBubble()
{
    if (!valueBubble.isVisible() || bubbleHeld) return;
    if (juce::Time::getMillisecondCounter() < bubbleUntil) return;
    valueBubble.setVisible(false);
    bubbleControl = nullptr;
}

void Editor::timerCallback()
{
    fadeValueBubble();
    // Cheap to re-apply every tick, and it catches a dependency changing from
    // host automation as well as from the panel. Both of these only repaint
    // when something has actually changed.
    // A type can change without the panel being touched: a preset loaded, a
    // host automating it, a second editor on the same plugin. Noticed the same
    // way a replaced wavetable is, by comparing against what is on screen, so
    // the labels are only rebuilt when one has actually moved.
    for (int rack = 0; rack < rackCount; ++rack)
        for (int slot = 0; slot < fxSlotCount; ++slot)
            if (fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)]
                != juce::roundToInt(value(fxParameterId(rack, slot, "Type"))))
            {
                refreshFxSlots();
                resized();
                repaint();
            }

    applyEnableStates();
    // A warp mode moves the same way a slot's type does, and from the same
    // places, so the field that reports it is refreshed on the same tick.
    refreshWarpFields();
    refreshModulationRings();

    // A table replaced from outside the panel — a preset loaded, a project
    // opened, a second editor on the same plugin — bumps its revision, and this
    // is where the panel notices and redraws.
    auto moved = false;
    for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
    {
        const auto revision = processor.tableStore().revision(oscillator);
        if (revision == tableRevisions[static_cast<size_t>(oscillator)]) continue;
        tableRevisions[static_cast<size_t>(oscillator)] = revision;
        moved = true;
    }
    if (moved)
    {
        if (tablePanel != nullptr) tablePanel->refresh();
        applyTableCounts();
    }
    // Frees whatever the audio thread has demonstrably moved past. Publishing
    // does this too; here it catches the table retired by the last edit of a
    // session, which would otherwise sit there until the next one.
    processor.tableStore().collect();
    repaint();
}

void Editor::choosePresetToLoad()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load Rhino Forge preset", juce::File {}, "*.forgepreset", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File {}) return;
            safe->showPresetResult(safe->processor.loadPreset(file), file);
        });
}

void Editor::choosePresetToSave()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save Rhino Forge preset", juce::File {}, "*.forgepreset", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File {}) return;
            file = file.withFileExtension("forgepreset");
            safe->showPresetResult(safe->processor.savePreset(file, file.getFileNameWithoutExtension()), file);
        });
}

void Editor::showPresetResult(const juce::Result& result, const juce::File& file)
{
    presetName.setText(result.wasOk() ? file.getFileNameWithoutExtension().toUpperCase()
                                      : "ERROR // " + result.getErrorMessage(), juce::dontSendNotification);
    presetName.setColour(juce::Label::textColourId, result.wasOk() ? ui::mutedText : ui::signalViolet);
    // Loading a preset can switch modules on or off behind the enable LEDs.
    applyEnableStates();
    repaint();
}
}
