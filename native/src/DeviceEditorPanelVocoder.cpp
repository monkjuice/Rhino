#include "DeviceEditorPanel.h"
#include "Theme.h"
#include "audio/VocoderDevice.h"
#include <algorithm>
#include <cmath>

// Rhino Vocoder's face, read left to right: where the carrier comes from, what
// the bank is doing with it, and the controls that shape it.
//
// The chooser on the left is the whole device, in the sense that nothing else
// on the face matters until it is set: a vocoder with no carrier has nothing
// to play the voice with. It is drawn first, drawn large, and says what to do
// when it is empty rather than leaving a silent device to be puzzled over.
//
// As with Rhino Tune's face, the parts that are not knobs are hit-tested by
// rectangle rather than made into child components, because the rack destroys
// and rebuilds every panel whenever anything about a track changes.
namespace rhino
{
namespace
{
const juce::Colour accentInk {0xff7d8fc4};
const juce::Colour wellInk {0xff141a1f};
const juce::Colour frameInk {0xff36414a};
const juce::Colour brightText {0xffdfe6ea};
const juce::Colour dimText {0xff85929c};
const juce::Colour deadText {0xff4a555e};
const juce::Colour warnInk {0xffd8a25a};

constexpr int gridColumns = 7;
constexpr int vocoderParameterCount = 13;

struct VocoderLayout
{
    juce::Rectangle<int> title, carrierLamp, voiceLamp;
    juce::Rectangle<int> sourceWell, sourceCaption, source, hint;
    juce::Rectangle<int> displayWell, display, scaleLow, scaleHigh;
    juce::Rectangle<int> knob[vocoderParameterCount];
};

VocoderLayout vocoderLayout(juce::Rectangle<int> panel)
{
    VocoderLayout layout;
    const auto width = panel.getWidth();
    layout.carrierLamp = {width - 74, 3, 68, 18};
    layout.voiceLamp = {width - 146, 3, 68, 18};
    layout.title = {27, 1, std::max(40, width - 177), 21};

    const auto content = panel.withTrimmedTop(24).reduced(4);
    const auto left = content.getX();
    const auto top = content.getY();

    layout.sourceWell = {left, top, 132, content.getHeight()};
    layout.sourceCaption = {left + 6, top + 5, 120, 12};
    layout.source = {left + 6, top + 20, 120, 34};
    layout.hint = {left + 6, top + 58, 120, content.getHeight() - 64};

    const auto displayLeft = left + 140;
    layout.displayWell = {displayLeft, top, 200, content.getHeight()};
    layout.display = {displayLeft + 5, top + 5, 190, content.getHeight() - 22};
    layout.scaleLow = {displayLeft + 5, content.getBottom() - 15, 92, 12};
    layout.scaleHigh = {displayLeft + 103, content.getBottom() - 15, 92, 12};

    const auto gridLeft = layout.displayWell.getRight() + 8;
    const auto cellWidth = std::max(30, (content.getRight() - gridLeft) / gridColumns);
    const auto rowHeight = (content.getHeight() - 2) / 2;
    for (int parameter = 0; parameter < vocoderParameterCount; ++parameter)
        layout.knob[parameter] = {gridLeft + (parameter % gridColumns) * cellWidth,
                                  top + (parameter / gridColumns) * (rowHeight + 2),
                                  cellWidth, rowHeight};
    return layout;
}

juce::String hertzLabel(float value)
{
    return value >= 1000.0f ? juce::String(value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String(juce::roundToInt(value)) + " Hz";
}

// The bars are drawn on a decibel scale over 60 dB. A band envelope is an
// amplitude, and on a linear scale everything but the loudest two bands sits
// on the floor -- which is exactly the picture a vocoder display should not
// give, because the quiet bands are the consonants.
float barHeightFor(float envelope)
{
    if (envelope <= 1.0e-6f)
        return 0.0f;
    const auto db = 20.0f * std::log10(envelope);
    return std::clamp(1.0f + db / 60.0f, 0.0f, 1.0f);
}
}

void DeviceEditorPanel::layoutVocoder()
{
    visualArea = {};
    const auto layout = vocoderLayout(getLocalBounds());
    title.setBounds(layout.title);

    const auto count = visibleParameterCount();
    for (int i = 0; i < count; ++i)
    {
        const auto cell = layout.knob[i];
        parameterLabels[i]->setFont(uiFont(10.0f));
        parameterValues[i]->setFont(uiFont(9.5f));
        parameterLabels[i]->setBounds(cell.getX() + 1, cell.getY(), cell.getWidth() - 2, 13);
        const auto knobSize = std::min({40, std::max(24, cell.getWidth() - 12), std::max(24, cell.getHeight() - 30)});
        parameterSliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 1));
        parameterValues[i]->setBounds(cell.getX() + 1, cell.getBottom() - 14, cell.getWidth() - 2, 13);
        parameterAutomation[i]->setBounds(cell.getRight() - 17, cell.getY() + 13, 16, 14);
    }
}

void DeviceEditorPanel::paintVocoder(juce::Graphics& g)
{
    auto* vocoder = dynamic_cast<VocoderDevice*>(session.devicePlugin(track, pluginSlot));
    if (vocoder == nullptr)
        return;
    const auto layout = vocoderLayout(getLocalBounds());
    const auto readout = vocoder->readout();
    const auto sourceTrack = session.deviceSidechainSource(track, pluginSlot);
    const auto haveSource = sourceTrack >= 0;

    const auto well = [&g] (juce::Rectangle<int> bounds)
    {
        g.setColour(wellInk);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
    };
    well(layout.sourceWell);
    well(layout.displayWell);

    // ---- is anything actually arriving ------------------------------------
    const auto lamp = [&g] (juce::Rectangle<int> bounds, const juce::String& text, bool lit)
    {
        g.setColour(juce::Colour(0xff232b32));
        g.fillRoundedRectangle(bounds.toFloat(), 2.5f);
        g.setColour(frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.5f, 1.0f);
        g.setColour(lit ? accentInk : juce::Colour(0xff39434b));
        g.fillEllipse(static_cast<float>(bounds.getX() + 6), static_cast<float>(bounds.getCentreY() - 3), 6.0f, 6.0f);
        g.setColour(lit ? brightText : dimText);
        g.setFont(uiFontBold(8.5f));
        drawSnappedText(g, text, bounds.withTrimmedLeft(17), juce::Justification::centredLeft);
    };
    lamp(layout.voiceLamp, "VOICE", readout.modulatorPresent);
    lamp(layout.carrierLamp, "CARRIER", readout.carrierPresent);

    // ---- where the carrier comes from --------------------------------------
    g.setColour(dimText);
    g.setFont(uiFont(8.5f));
    drawSnappedText(g, "AUDIO FROM", layout.sourceCaption, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff1f262c));
    g.fillRoundedRectangle(layout.source.toFloat(), 2.5f);
    g.setColour(haveSource ? accentInk : warnInk);
    g.drawRoundedRectangle(layout.source.toFloat().reduced(0.5f), 2.5f, 1.0f);
    g.setColour(haveSource ? brightText : warnInk);
    g.setFont(uiFontBold(11.0f));
    drawSnappedText(g, haveSource ? session.trackName(sourceTrack) : juce::String("Pick a track"),
                    layout.source.reduced(6, 0), juce::Justification::centredLeft);

    // A device that makes no sound should say why on its own face rather than
    // leaving the rack to be searched for the reason.
    g.setColour(haveSource ? deadText : dimText);
    g.setFont(uiFont(9.0f));
    g.drawFittedText(haveSource
                         ? "This track's audio is the voice. Mute " + session.trackName(sourceTrack)
                           + " so you hear the vocoder rather than both."
                         : "Choose the track whose synth should play this track's voice. "
                           "Until then the voice passes through.",
                     layout.hint, juce::Justification::topLeft, 6);

    // ---- what the bank is doing -------------------------------------------
    const auto bands = std::max(1, readout.bands > 0 ? readout.bands : vocoder->bandCount());
    const auto slot = static_cast<float>(layout.display.getWidth()) / static_cast<float>(bands);
    const auto barWidth = std::max(1.5f, slot - 1.5f);
    const auto floorY = static_cast<float>(layout.display.getBottom());
    const auto fullHeight = static_cast<float>(layout.display.getHeight());
    for (int i = 0; i < bands; ++i)
    {
        const auto x = static_cast<float>(layout.display.getX()) + i * slot;
        g.setColour(juce::Colour(0xff1d242a));
        g.fillRect(juce::Rectangle<float>(x, static_cast<float>(layout.display.getY()), barWidth, fullHeight));
        const auto height = fullHeight * barHeightFor(readout.level[static_cast<size_t>(i)]);
        if (height <= 0.5f)
            continue;
        // Higher bands are drawn brighter, so the picture reads as a spectrum
        // rather than as a row of identical lamps.
        const auto tint = accentInk.withRotatedHue(static_cast<float>(i) / static_cast<float>(bands) * 0.06f)
                              .brighter(static_cast<float>(i) / static_cast<float>(bands) * 0.35f);
        g.setColour(tint);
        g.fillRect(juce::Rectangle<float>(x, floorY - height, barWidth, height));
    }
    g.setColour(dimText);
    g.setFont(uiFont(8.0f));
    drawSnappedText(g, hertzLabel(vocoder->bandCentreHz(0)), layout.scaleLow,
                    juce::Justification::centredLeft);
    drawSnappedText(g, hertzLabel(vocoder->bandCentreHz(bands - 1)), layout.scaleHigh,
                    juce::Justification::centredRight);
}

bool DeviceEditorPanel::handleVocoderClick(const juce::MouseEvent& event)
{
    auto* vocoder = dynamic_cast<VocoderDevice*>(session.devicePlugin(track, pluginSlot));
    if (vocoder == nullptr)
        return false;
    const auto layout = vocoderLayout(getLocalBounds());
    const auto position = event.getEventRelativeTo(this).getPosition();
    if (!layout.source.contains(position) && !layout.sourceCaption.contains(position))
        return false;

    const auto sources = session.deviceSidechainSources(track, pluginSlot);
    const auto current = session.deviceSidechainSource(track, pluginSlot);
    juce::PopupMenu menu;
    menu.addSectionHeader("AUDIO FROM");
    menu.addItem(1, "None", true, current < 0);
    if (!sources.empty())
        menu.addSeparator();
    for (size_t i = 0; i < sources.size(); ++i)
        menu.addItem(static_cast<int>(i) + 2, sources[i].name, true, sources[i].track == current);
    if (sources.empty())
        menu.addItem(1000, "Add another track to use as a carrier", false, false);

    // The menu is asynchronous, so neither the panel nor the device can be
    // assumed to still be there when it closes: the panel is guarded by a
    // SafePointer and the device is looked up again through it.
    const auto safe = juce::Component::SafePointer<DeviceEditorPanel>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withTargetScreenArea(localAreaToGlobal(layout.source)),
        [safe, sources] (int result)
        {
            if (safe == nullptr || result == 0 || result >= 1000) return;
            const auto chosen = result == 1 ? -1 : sources[static_cast<size_t>(result - 2)].track;
            const auto done = safe->session.setDeviceSidechainSource(safe->track, safe->pluginSlot, chosen);
            safe->repaint();
            if (safe->status == nullptr) return;
            if (done.failed())
                safe->status(done.getErrorMessage());
            else if (chosen < 0)
                safe->status("Rhino Vocoder has no carrier: the voice passes through");
            else
                safe->status("Rhino Vocoder is played by " + safe->session.trackName(chosen)
                             + ". Mute that track so you hear the vocoder rather than both.");
        });
    return true;
}

// Only the band display and the two lamps move, so only they are repainted.
void DeviceEditorPanel::tickVocoder()
{
    auto* vocoder = dynamic_cast<VocoderDevice*>(session.devicePlugin(track, pluginSlot));
    if (vocoder == nullptr)
        return;
    const auto readout = vocoder->readout();
    auto sum = 0.0f;
    for (int i = 0; i < readout.bands; ++i)
        sum += readout.level[static_cast<size_t>(i)];
    const auto lamps = (readout.carrierPresent ? 1 : 0) | (readout.modulatorPresent ? 2 : 0);
    // A device with nothing going through it asks for no frames at all, which
    // matters because the rack can hold several panels at once.
    if (std::abs(sum - lastDrawnBandSum) < 1.0e-5f && lamps == lastDrawnLamps)
        return;
    lastDrawnBandSum = sum;
    lastDrawnLamps = lamps;
    const auto layout = vocoderLayout(getLocalBounds());
    repaint(layout.display);
    repaint(layout.voiceLamp.getUnion(layout.carrierLamp));
}
}
