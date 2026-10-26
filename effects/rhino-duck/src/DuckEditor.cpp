#include "DuckEditor.h"
#include "../ui/DuckTheme.h"

namespace rhino::duck
{
using ui::colour::accent;
using ui::colour::text;
using ui::colour::textDim;

namespace
{
constexpr float coarseShift = 1.0f / 16.0f;
constexpr float fineShift = 1.0f / 256.0f;
constexpr std::array<const char*, 3> triggerNames { "SYNC", "MIDI", "AUDIO" };
constexpr std::array<const char*, 4> shiftGlyphs { "<<", "<", ">", ">>" };
constexpr std::array<float, 6> windowScales { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

juce::String signedPercent(float value)
{
    const auto percent = juce::roundToInt(value * 100.0f);
    return (percent > 0 ? "+" : "") + juce::String(percent) + "%";
}
}

Editor::Editor(Processor& owner)
    : AudioProcessorEditor(owner), processor(owner), scope(owner), tooltips(this)
{
    setOpaque(true);
    using Slider = juce::AudioProcessorValueTreeState::SliderAttachment;
    using Button = juce::AudioProcessorValueTreeState::ButtonAttachment;

    addAndMakeVisible(mixKnob);
    mixKnob.setTooltip("Mix: how deep the duck goes");
    mixAttachment = std::make_unique<Slider>(processor.state, id::mix, mixKnob);
    addAndMakeVisible(cutoffSlider);
    cutoffSlider.setTooltip("Band cutoff: only what lies below it is ducked");
    cutoffAttachment = std::make_unique<Slider>(processor.state, id::cutoff, cutoffSlider);

    for (auto* chip : { &bandChip, &soloChip, &loopChip })
    {
        chip->setClickingTogglesState(true);
        addAndMakeVisible(*chip);
    }
    bandChip.setTooltip("Band split: duck only the frequencies under the cutoff");
    soloChip.setTooltip("Hear only the band being ducked");
    loopChip.setTooltip("Loop: repeat the curve until the next trigger, or run it once to End");
    bandAttachment = std::make_unique<Button>(processor.state, id::band, bandChip);
    soloAttachment = std::make_unique<Button>(processor.state, id::bandSolo, soloChip);
    loopAttachment = std::make_unique<Button>(processor.state, id::loop, loopChip);

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
        button = std::make_unique<ui::Chip>(shiftGlyphs[static_cast<size_t>(i)]);
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
    menuButton.bars = true;
    menuButton.setTooltip("Trigger options, window size and curve resets");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible(menuButton);
    addAndMakeVisible(scope);

    setSize(ui::panelWidth, ui::panelHeight);
    setScaleFactor(processor.uiScale());
    refresh();
    startTimerHz(30);
}

Editor::~Editor()
{
    stopTimer();
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

void Editor::resized()
{
    placed = ui::layoutFor(getLocalBounds());
    mixKnob.setBounds(placed.knob);
    cutoffSlider.setBounds(placed.cutoffSlider);
    bandChip.setBounds(placed.bandChip);
    soloChip.setBounds(placed.soloChip);
    loopChip.setBounds(placed.loopChip);
    menuButton.setBounds(placed.menuButton);
    for (size_t i = 0; i < lengthChips.size(); ++i) lengthChips[i]->setBounds(placed.lengthChips[i]);
    for (size_t i = 0; i < triggerChips.size(); ++i) triggerChips[i]->setBounds(placed.triggerChips[i]);
    for (size_t i = 0; i < shiftButtons.size(); ++i) shiftButtons[i]->setBounds(placed.shiftButtons[i]);
    for (size_t i = 0; i < cells.size(); ++i) cells[i]->setBounds(placed.curveCells[i]);
    scope.setBounds(placed.scope);
}

// Follows the parameters and the curves' memory, repainting only what moved.
void Editor::refresh()
{
    const auto curve = clampCurve(juce::roundToInt(valueOf(id::curve)));
    if (curve != shownCurve)
    {
        for (int i = 0; i < curveCount; ++i)
            cells[static_cast<size_t>(i)]->setToggleState(i == curve, juce::dontSendNotification);
        shownCurve = curve;
        repaint(placed.curveCaption);
    }
    const auto revision = processor.curveRevision();
    if (revision != shownRevision)
    {
        for (int i = 0; i < curveCount; ++i)
        {
            auto& cell = *cells[static_cast<size_t>(i)];
            const auto adjust = processor.curveAdjust(i);
            if (adjust == cell.adjust) continue;
            cell.adjust = adjust;
            cell.repaint();
        }
        shownRevision = revision;
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
        soloChip.setVisible(band);
        // Still draggable while the band is off, so the cutoff can be set
        // before it is switched on; only drawn muted.
        cutoffSlider.muted = !band;
        cutoffSlider.repaint();
        shownBand = band;
        shownBandOnce = true;
        repaint(placed.cutoffReading);
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

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(ui::colour::panel);
    g.setColour(ui::colour::panelEdge);
    g.drawRect(getLocalBounds(), 1);

    juce::AttributedString title;
    title.append("RHINO ", ui::font(ui::Face::header, 27.0f), textDim);
    title.append("DUCK", ui::font(ui::Face::header, 27.0f), accent);
    title.setJustification(juce::Justification::centredLeft);
    title.draw(g, placed.title.toFloat());
    g.setColour(textDim);
    g.setFont(ui::font(ui::Face::label, 13.0f));
    g.drawText("SIDECHAIN SHAPER", placed.tagline, juce::Justification::centredLeft, false);

    juce::AttributedString mix;
    mix.append("MIX  ", ui::font(ui::Face::label, 18.0f), textDim);
    mix.append(shownMix, ui::font(ui::Face::reading, 18.0f), text);
    mix.setJustification(juce::Justification::centred);
    mix.draw(g, placed.mixReading.toFloat());

    juce::AttributedString cutoff;
    if (shownBand)
    {
        cutoff.append("BELOW  ", ui::font(ui::Face::label, 14.0f), textDim);
        cutoff.append(shownCutoff, ui::font(ui::Face::reading, 14.0f), text);
    }
    else
    {
        cutoff.append("WHOLE SIGNAL", ui::font(ui::Face::label, 14.0f), textDim);
    }
    cutoff.setJustification(juce::Justification::centred);
    cutoff.draw(g, placed.cutoffReading.toFloat());

    const auto curve = shownCurve < 0 ? 0 : shownCurve;
    const auto adjust = processor.curveAdjust(curve);
    juce::AttributedString caption;
    caption.append(juce::String(curve + 1) + "  ", ui::font(ui::Face::reading, 14.0f), accent);
    caption.append(juce::String(curveName(curve)).toUpperCase(), ui::font(ui::Face::header, 14.0f), text);
    caption.append("   " + juce::String(familyName(curveFamily(curve))).toUpperCase(), ui::font(ui::Face::label, 13.0f), textDim);
    if (adjust.shift != 0.0f)
        caption.append("   SHIFT " + signedPercent(adjust.shift), ui::font(ui::Face::label, 13.0f), textDim);
    caption.setJustification(juce::Justification::centred);
    caption.draw(g, placed.curveCaption.toFloat());

    // A divider where one family ends and the next begins.
    g.setColour(ui::colour::divider);
    for (const auto [before, after] : { std::pair { 3, 8 }, std::pair { 8, 10 }, std::pair { 10, 12 }, std::pair { 12, 14 } })
    {
        const auto& left = placed.curveCells[static_cast<size_t>(before)];
        const auto& right = placed.curveCells[static_cast<size_t>(after)];
        const auto x = (left.getRight() + right.getX()) / 2;
        g.fillRect(x, left.getY(), 1, placed.curveCells[static_cast<size_t>(before == 3 ? 7 : before + 1)].getBottom() - left.getY());
    }
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
