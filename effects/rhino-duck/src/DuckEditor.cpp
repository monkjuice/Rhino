#include "DuckEditor.h"
#include "../ui/DuckTheme.h"

// The panel's construction, its layout and the tick that keeps it in step
// with the processor. Its painting is DuckEditorPaint.cpp.
namespace rhino::duck
{
namespace
{
constexpr float coarseShift = 1.0f / 16.0f;
constexpr float fineShift = 1.0f / 256.0f;
constexpr std::array<const char*, 3> triggerNames { "SYNC", "MIDI", "AUDIO" };
constexpr std::array<ui::Glyph, 4> shiftGlyphs { ui::Glyph::rewind, ui::Glyph::back, ui::Glyph::forward, ui::Glyph::fastForward };
constexpr std::array<float, 6> windowScales { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

// A time as the strip reads it: tenths below 10 ms, whole milliseconds up to
// a second, then seconds.
juce::String millisecondsText(double milliseconds)
{
    if (milliseconds >= 1000.0) return juce::String(milliseconds / 1000.0, 2) + " s";
    if (milliseconds >= 9.95) return juce::String(juce::roundToInt(milliseconds)) + " ms";
    return juce::String(milliseconds, 1) + " ms";
}

juce::String percentText(float value) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; }

// Sets a knob over a span of phase that may have closed up, which a slider
// cannot hold, and the value a double-click returns it to.
void setSpan(ui::Knob& knob, PhaseSpan span, float value, float design)
{
    const auto open = span.to - span.from > 1.0e-4f;
    knob.setRange(span.from, open ? span.to : span.from + 1.0e-4f, 0.0);
    knob.setEnabled(open);
    knob.setDoubleClickReturnValue(true, std::clamp(design, span.from, std::max(span.from, span.to)));
    knob.setValue(value, juce::dontSendNotification);
}
}

Editor::Editor(Processor& owner)
    : AudioProcessorEditor(owner), processor(owner), scope(owner), tooltips(this)
{
    setOpaque(true);
    using Slider = juce::AudioProcessorValueTreeState::SliderAttachment;
    using Button = juce::AudioProcessorValueTreeState::ButtonAttachment;

    addAndMakeVisible(meter);
    meter.setSource([this] { return ui::DuckMeter::Reading { processor.engine().takeDeepestGain(), processor.engine().cycleCount() }; });

    addAndMakeVisible(mixKnob);
    mixKnob.setTooltip("Mix: how much of the curve reaches the sound, from dry to wet");
    mixAttachment = std::make_unique<Slider>(processor.state, id::mix, mixKnob);
    mixKnob.setDoubleClickReturnValue(true, 1.0);

    const std::array<const char*, ui::paramCount> paramTips {
        "Attack: how long the cut on the beat takes, from the level the last cycle ended at",
        "Hold: how long the curve holds before its sloped part; drag the sloped part in the scope too",
        "Release: how long the sloped part takes; drag its end in the scope too",
        "Depth: how much of the curve's dip is kept"
    };
    for (size_t i = 0; i < paramKnobs.size(); ++i)
    {
        auto& knob = paramKnobs[i];
        knob.setTooltip(paramTips[i]);
        knob.onValueChange = [this, i] { paramMoved(static_cast<Param>(i)); };
        addAndMakeVisible(knob);
    }
    paramKnob(attack).setRange(0.0, longestAttackMs, 0.0);
    paramKnob(attack).setSkewFactorFromMidPoint(10.0);
    paramKnob(attack).setDoubleClickReturnValue(true, 0.0);
    paramKnob(depth).setRange(0.0, 1.0, 0.0);
    paramKnob(depth).setDoubleClickReturnValue(true, 1.0);

    addAndMakeVisible(cutoffSlider);
    cutoffSlider.setTooltip("Band cutoff: only what lies below it is ducked");
    cutoffAttachment = std::make_unique<Slider>(processor.state, id::cutoff, cutoffSlider);

    for (auto* chip : { &soloChip, &loopChip })
    {
        chip->setClickingTogglesState(true);
        addAndMakeVisible(*chip);
    }
    soloChip.setTooltip("Hear only the band being ducked, the duck applied");
    loopChip.setTooltip("Loop: repeat the curve until the next trigger, or run it once to End");
    soloAttachment = std::make_unique<Button>(processor.state, id::bandSolo, soloChip);
    loopAttachment = std::make_unique<Button>(processor.state, id::loop, loopChip);
    wholeSwitch.setTooltip("Whole signal: duck everything, or only the band under the cutoff");
    wholeSwitch.onClick = [this] { setChoice(id::band, valueOf(id::band) > 0.5f ? 0 : 1); };
    addAndMakeVisible(wholeSwitch);

    for (int i = 0; i < lengthCount; ++i)
    {
        auto& chip = lengthChips[static_cast<size_t>(i)];
        chip = std::make_unique<ui::Chip>(lengthNames[static_cast<size_t>(i)]);
        chip->setTooltip("Length: the curve repeats every " + juce::String(lengthNames[static_cast<size_t>(i)]) + " note");
        chip->onClick = [this, i] { setChoice(id::length, i); };
        addAndMakeVisible(*chip);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto& chip = triggerChips[static_cast<size_t>(i)];
        chip = std::make_unique<ui::Chip>(triggerNames[static_cast<size_t>(i)]);
        chip->setTooltip(i == 0 ? "Sync: the curve follows the song's beat"
                         : i == 1 ? "MIDI: a note restarts the curve"
                                  : "Audio: a hit on the sidechain input restarts the curve");
        chip->onClick = [this, i] { setChoice(id::trigger, i); };
        addAndMakeVisible(*chip);
    }
    const std::array<float, 4> steps { -coarseShift, -fineShift, fineShift, coarseShift };
    for (int i = 0; i < 4; ++i)
    {
        auto& button = shiftButtons[static_cast<size_t>(i)];
        button = std::make_unique<ui::Chip>("SHIFT", shiftGlyphs[static_cast<size_t>(i)]);
        button->setTooltip(i == 0 || i == 3 ? "Shift the curve by 1/16 of its length" : "Shift the curve by 1/256 of its length");
        button->onClick = [this, step = steps[static_cast<size_t>(i)]] { nudgeShift(step); };
        addAndMakeVisible(*button);
    }
    for (int i = 0; i < curveCount; ++i)
    {
        auto& cell = cells[static_cast<size_t>(i)];
        cell = std::make_unique<ui::CurveCell>(i);
        cell->setTooltip(juce::String(i + 1) + "  " + curveName(i) + "  (" + familyName(curveFamily(i)) + ")");
        cell->onClick = [this, i] { setChoice(id::curve, i); };
        addAndMakeVisible(*cell);
    }
    menuButton.setTooltip("Trigger options, window size and curve resets");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible(menuButton);

    presetBack.setTooltip("The previous curve");
    presetBack.onClick = [this] { stepCurve(-1); };
    presetForward.setTooltip("The next curve");
    presetForward.onClick = [this] { stepCurve(1); };
    presetBox.setTooltip("Choose one of the sixteen curves");
    presetBox.onClick = [this] { showCurveList(); };
    for (auto* control : std::initializer_list<juce::Component*> { &presetBack, &presetForward, &presetBox })
        addAndMakeVisible(*control);
    addAndMakeVisible(scope);

    setSize(ui::panelWidth, ui::panelHeight);
    setScaleFactor(processor.uiScale());
    refresh();
    startTimerHz(30);
}

Editor::~Editor()
{
    stopTimer();
    meter.setSource(nullptr);
}

float Editor::valueOf(const char* id) const
{
    const auto* value = processor.state.getRawParameterValue(id);
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}

juce::String Editor::textOf(const char* id) const
{
    const auto* parameter = processor.state.getParameter(id);
    return parameter != nullptr ? parameter->getCurrentValueAsText() : juce::String();
}

void Editor::setChoice(const char* id, int index)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
    parameter->endChangeGesture();
    refresh();
}

void Editor::nudgeShift(float step)
{
    const auto curve = clampCurve(juce::roundToInt(valueOf(id::curve)));
    auto shift = processor.curveAdjust(curve).shift + step;
    // A shift is a place round the cycle, so it wraps rather than stops.
    if (shift > 0.5f) shift -= 1.0f;
    if (shift < -0.5f) shift += 1.0f;
    processor.setCurveShift(curve, shift);
    refresh();
}

void Editor::stepCurve(int by)
{
    const auto curve = clampCurve(juce::roundToInt(valueOf(id::curve)));
    setChoice(id::curve, (curve + by + curveCount) % curveCount);
}

void Editor::paramMoved(Param which)
{
    if (syncing || shownCurve < 0) return;
    const auto value = static_cast<float>(paramKnob(which).getValue());
    switch (which)
    {
        case attack:  processor.setCurveAttack(shownCurve, value); break;
        case hold:    processor.setCurveHold(shownCurve, value); break;
        case release: processor.setCurveRelease(shownCurve, value); break;
        case depth:   processor.setCurveDepth(shownCurve, value); break;
    }
    refresh();
}

void Editor::advanceMeter(float seconds)
{
    if (meter.step({ processor.engine().takeDeepestGain(), processor.engine().cycleCount() }, seconds)) meter.repaint();
}

void Editor::resized()
{
    placed = ui::layoutFor(getLocalBounds());
    meter.setBounds(placed.meter);
    mixKnob.setBounds(placed.knob);
    for (size_t i = 0; i < paramKnobs.size(); ++i) paramKnobs[i].setBounds(placed.paramKnobs[i]);
    cutoffSlider.setBounds(placed.cutoffSlider);
    soloChip.setBounds(placed.soloChip);
    wholeSwitch.setBounds(placed.wholeSwitch);
    loopChip.setBounds(placed.loopChip);
    menuButton.setBounds(placed.menuButton);
    for (size_t i = 0; i < lengthChips.size(); ++i) lengthChips[i]->setBounds(placed.lengthChips[i]);
    for (size_t i = 0; i < triggerChips.size(); ++i) triggerChips[i]->setBounds(placed.triggerChips[i]);
    for (size_t i = 0; i < shiftButtons.size(); ++i) shiftButtons[i]->setBounds(placed.shiftButtons[i]);
    for (size_t i = 0; i < cells.size(); ++i) cells[i]->setBounds(placed.curveCells[i]);
    presetBack.setBounds(placed.presetBack);
    presetForward.setBounds(placed.presetForward);
    presetBox.setBounds(placed.presetBox);
    scope.setBounds(placed.scope);
}

// Moves the strip to the chosen curve's memory: each knob's travel, where it
// stands, and the readings under it in milliseconds at the current tempo.
void Editor::syncStrip(int curve)
{
    const juce::ScopedValueSetter<bool> quiet(syncing, true);
    const auto adjust = processor.curveAdjust(curve);
    const auto room = mobileRoom(curveDef(curve));
    const auto part = slopedPart(curve, adjust);
    const auto holdSpan = holdRoom(curve, adjust);
    const auto releaseSpan = releaseRoom(curve, adjust);
    const auto cycleMs = processor.cycleSeconds() * 1000.0;
    const auto attackMs = processor.curveAttackMs(curve);

    paramKnob(attack).setValue(attackMs, juce::dontSendNotification);
    setSpan(paramKnob(hold), holdSpan, part.start, room.first);
    setSpan(paramKnob(release), releaseSpan, part.length, room.last - room.first);
    paramKnob(depth).setValue(adjust.depth, juce::dontSendNotification);
    paramKnob(depth).setTooltip("Depth: how much of the curve's dip is kept ("
                                + ui::reductionText(ui::gainToDb(1.0f - adjust.depth)) + " at its deepest, before Mix)");

    const auto ms = [cycleMs] (float phase) { return millisecondsText(static_cast<double>(phase) * cycleMs); };
    const std::array<juce::String, ui::paramCount> values { millisecondsText(attackMs), ms(part.start), ms(part.length), percentText(adjust.depth) };
    const std::array<juce::String, ui::paramCount> lows { "0 ms", ms(holdSpan.from), ms(releaseSpan.from), "0%" };
    const std::array<juce::String, ui::paramCount> highs { millisecondsText(longestAttackMs), ms(holdSpan.to), ms(releaseSpan.to), "100%" };
    for (size_t i = 0; i < ui::paramCount; ++i)
    {
        if (values[i] != shownValues[i]) repaint(placed.paramValues[i]);
        if (lows[i] != shownLows[i]) repaint(placed.paramLows[i]);
        if (highs[i] != shownHighs[i]) repaint(placed.paramHighs[i]);
    }
    shownValues = values;
    shownLows = lows;
    shownHighs = highs;
}

// Follows the parameters and the curves' memory, repainting only what moved.
void Editor::refresh()
{
    const auto curve = clampCurve(juce::roundToInt(valueOf(id::curve)));
    const auto revision = processor.curveRevision();
    const auto cycle = processor.cycleSeconds();
    const auto curveMoved = curve != shownCurve;
    const auto memoryMoved = revision != shownRevision || cycle != shownCycle;
    if (curveMoved)
    {
        for (int i = 0; i < curveCount; ++i)
            cells[static_cast<size_t>(i)]->setToggleState(i == curve, juce::dontSendNotification);
        shownCurve = curve;
        presetBox.setButtonText(juce::String(curve + 1) + "   " + curveName(curve));
    }
    if (memoryMoved)
    {
        // Attack is drawn in phase, so a new tempo or Length redraws it too.
        for (int i = 0; i < curveCount; ++i)
        {
            auto& cell = *cells[static_cast<size_t>(i)];
            const auto adjust = processor.curveAdjust(i);
            if (adjust == cell.adjust) continue;
            cell.adjust = adjust;
            cell.repaint();
        }
        shownRevision = revision;
        shownCycle = cycle;
    }
    if (curveMoved || memoryMoved)
    {
        syncStrip(curve);
        repaint(placed.curveCaption);
    }
    const auto trigger = juce::roundToInt(valueOf(id::trigger));
    if (trigger != shownTrigger)
    {
        for (int i = 0; i < 3; ++i) triggerChips[static_cast<size_t>(i)]->setToggleState(i == trigger, juce::dontSendNotification);
        loopChip.setVisible(trigger != 0);
        shownTrigger = trigger;
    }
    const auto length = juce::roundToInt(valueOf(id::length));
    if (length != shownLength)
    {
        for (int i = 0; i < lengthCount; ++i) lengthChips[static_cast<size_t>(i)]->setToggleState(i == length, juce::dontSendNotification);
        shownLength = length;
    }
    const auto band = valueOf(id::band) > 0.5f;
    if (band != shownBand || !shownBandOnce)
    {
        wholeSwitch.setToggleState(!band, juce::dontSendNotification);
        // Solo only means something with the band split on; it keeps its
        // setting, shown faintly, while the whole signal is ducked.
        soloChip.setEnabled(band);
        // Still draggable while the band is off, so the cutoff can be set
        // before it is switched on; only drawn muted.
        cutoffSlider.muted = !band;
        cutoffSlider.repaint();
        shownBand = band;
        shownBandOnce = true;
        repaint(placed.cutoffReading);
        repaint(placed.bandTitle);
    }
    const auto mix = textOf(id::mix);
    if (mix != shownMix)
    {
        shownMix = mix;
        repaint(placed.mixReading);
    }
    const auto cutoff = textOf(id::cutoff);
    if (cutoff != shownCutoff)
    {
        shownCutoff = cutoff;
        repaint(placed.cutoffReading);
    }
    scope.repaint();
}

void Editor::showCurveList()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<Editor> safe(this);
    const auto chosen = clampCurve(juce::roundToInt(valueOf(id::curve)));
    for (int curve = 0; curve < curveCount; ++curve)
    {
        if (curve == 0 || curveFamily(curve) != curveFamily(curve - 1))
            menu.addSectionHeader(juce::String(familyName(curveFamily(curve))).toUpperCase());
        menu.addItem(juce::String(curve + 1) + "   " + curveName(curve), true, curve == chosen,
                     [safe, curve] { if (safe != nullptr) safe->setChoice(id::curve, curve); });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetBox).withMinimumWidth(presetBox.getWidth()));
}

void Editor::showMenu()
{
    juce::PopupMenu menu;
    const auto choiceIs = [this] (const char* id) { return juce::roundToInt(valueOf(id)); };
    const juce::Component::SafePointer<Editor> safe(this);

    menu.addSectionHeader("MIDI & AUDIO TRIGGER");
    menu.addItem("Anti-click smoothing (adds 6 ms)", true, valueOf(id::antiClick) > 0.5f, [safe]
    {
        if (safe != nullptr) safe->setChoice(id::antiClick, safe->valueOf(id::antiClick) > 0.5f ? 0 : 1);
    });
    juce::PopupMenu channels;
    for (int channel = 0; channel <= 16; ++channel)
        channels.addItem(channel == 0 ? "All channels" : "Channel " + juce::String(channel), true, choiceIs(id::midiChannel) == channel,
                         [safe, channel] { if (safe != nullptr) safe->setChoice(id::midiChannel, channel); });
    menu.addSubMenu("MIDI channel", channels);
    juce::PopupMenu notes;
    notes.addItem("All notes", true, choiceIs(id::midiNote) == 0, [safe] { if (safe != nullptr) safe->setChoice(id::midiNote, 0); });
    for (int octave = -2; octave <= 8; ++octave)
    {
        juce::PopupMenu inOctave;
        for (int key = 0; key < 12; ++key)
        {
            const auto note = (octave + 2) * 12 + key;
            if (note > 127) break;
            inOctave.addItem(juce::MidiMessage::getMidiNoteName(note, true, true, 3), true, choiceIs(id::midiNote) == note + 1,
                             [safe, note] { if (safe != nullptr) safe->setChoice(id::midiNote, note + 1); });
        }
        notes.addSubMenu("Octave " + juce::String(octave), inOctave);
    }
    menu.addSubMenu("MIDI note", notes);

    menu.addSectionHeader("CURVES");
    menu.addItem("Reset this curve", [safe]
    {
        if (safe != nullptr) safe->processor.resetCurve(juce::roundToInt(safe->valueOf(id::curve)));
    });
    menu.addItem("Reset every curve", [safe] { if (safe != nullptr) safe->processor.resetAllCurves(); });

    menu.addSectionHeader("WINDOW SIZE");
    for (const auto scale : windowScales)
        menu.addItem(juce::String(juce::roundToInt(scale * 100.0f)) + "%", true, std::abs(processor.uiScale() - scale) < 0.01f, [safe, scale]
        {
            if (safe == nullptr) return;
            safe->processor.setUiScale(scale);
            safe->setScaleFactor(scale);
        });

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton));
}
}
