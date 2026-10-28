#include "DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include "audio/TunerDevice.h"
#include "dsp/ScaleQuantizer.h"
#include <algorithm>
#include <cmath>

// The Tuner's face, after Live's: view switches down the left, the display in
// the middle, Reference on the right.
//
// Classic shows the pitch as a ball on an arc an octave's hundred cents wide,
// or in Strobe a band of lights that runs the way the pitch is off and stands
// still when it is in tune. Histogram shows the last few seconds of pitch
// against the semitones around it. Green is in tune and red is not, in every
// view.
//
// Like Rhino Tune's, everything but the knob is drawn and hit-tested by
// rectangle: the rack rebuilds its panels whenever the chain changes.
namespace rhino
{
namespace
{
const juce::Colour wellInk {0xff141a1f};
const juce::Colour frameInk {0xff36414a};
const juce::Colour brightText {0xffdfe6ea};
const juce::Colour dimText {0xff85929c};
const juce::Colour deadText {0xff4a555e};
const juce::Colour accentInk {0xff6fc98f};
const juce::Colour inTuneInk {0xff5fd08a};
const juce::Colour offInk {0xffe0604a};

// Within this the note is called in tune, as Live's green is.
constexpr float inTuneCents = 3.0f;
// The arc spans this either side of the note.
constexpr float arcCents = 50.0f;
constexpr float arcDegrees = 60.0f;
// Histogram: how much is shown, and how many semitones either side of the
// note it is centred on.
constexpr double historySeconds = 4.0;
constexpr int histogramSemitones = 3;
constexpr int strobeLights = 36;

struct TunerLayout
{
    juce::Rectangle<int> classic, histogram, target, strobe, hertz, cents;
    juce::Rectangle<int> display, knob;
};

TunerLayout tunerLayout(juce::Rectangle<int> panel)
{
    TunerLayout layout;
    const auto content = panel.withTrimmedTop(24).reduced(4);
    auto left = content.withWidth(86);
    layout.classic = left.removeFromTop(20);
    left.removeFromTop(3);
    layout.histogram = left.removeFromTop(20);
    left.removeFromTop(12);
    layout.target = left.removeFromTop(20);
    left.removeFromTop(3);
    layout.strobe = left.removeFromTop(20);
    const auto units = left.removeFromBottom(20);
    layout.hertz = units.withWidth(42);
    layout.cents = units.withTrimmedLeft(44);
    layout.knob = content.withTrimmedLeft(content.getWidth() - 76);
    layout.display = content.withTrimmedLeft(92).withTrimmedRight(82);
    return layout;
}

// Green in tune, shading to red by a quarter of a semitone off.
juce::Colour tuningColour(float cents)
{
    const auto off = std::abs(cents);
    if (off <= inTuneCents)
        return inTuneInk;
    return inTuneInk.interpolatedWith(offInk, std::clamp((off - inTuneCents) / 22.0f, 0.0f, 1.0f));
}

juce::String noteName(int note)
{
    return juce::String(pitchClassName(note)) + juce::String(note / 12 - 1);
}

juce::String readoutText(const TunerEngine::Reading& reading, float note, bool cents)
{
    if (!reading.voiced)
        return "-";
    if (!cents)
        return juce::String(reading.frequency, reading.frequency < 100.0f ? 2 : 1) + " Hz";
    const auto off = juce::roundToInt((note - std::round(note)) * 100.0f);
    return (off > 0 ? "+" : "") + juce::String(off) + " ct";
}

// Anything with a pitch in the histogram's window, which is as long as it
// goes on scrolling.
bool heardWithinHistory(const TunerDevice& tuner)
{
    std::array<float, TunerEngine::historyLength> history {};
    const auto wanted = std::min(TunerEngine::historyLength, static_cast<int>(historySeconds * tuner.analysesPerSecond()));
    const auto count = tuner.history(history.data(), wanted);
    return std::any_of(history.begin(), history.begin() + count, [] (float frequency) { return frequency > 0.0f; });
}

// A point on the arc at `cents`, the arc's centre being `pivot`.
juce::Point<float> onArc(juce::Point<float> pivot, float radius, float cents)
{
    const auto angle = juce::degreesToRadians(std::clamp(cents, -arcCents, arcCents) / arcCents * arcDegrees);
    return { pivot.x + radius * std::sin(angle), pivot.y - radius * std::cos(angle) };
}
}

void DeviceEditorPanel::layoutTuner()
{
    visualArea = {};
    const auto layout = tunerLayout(getLocalBounds());
    title.setBounds(27, 1, getWidth() - 32, 21);
    if (visibleParameterCount() < 1)
        return;
    const auto cell = layout.knob;
    parameterLabels[0]->setFont(uiFont(10.0f));
    parameterValues[0]->setFont(uiFont(9.5f));
    parameterLabels[0]->setBounds(cell.getX(), cell.getCentreY() - 44, cell.getWidth(), 14);
    parameterSliders[0]->setBounds(cell.withSizeKeepingCentre(48, 48));
    parameterValues[0]->setBounds(cell.getX(), cell.getCentreY() + 28, cell.getWidth(), 14);
    placeAutomationBadge(0, parameterSliders[0]->getBounds());
}

void DeviceEditorPanel::paintTuner(juce::Graphics& g)
{
    auto* tuner = dynamic_cast<TunerDevice*>(session.devicePlugin(track, pluginSlot));
    if (tuner == nullptr)
        return;
    const auto layout = tunerLayout(getLocalBounds());
    const auto reading = tuner->reading();
    const auto reference = tuner->referenceHz();
    const auto note = TunerEngine::noteFor(reading.frequency, reference);
    const auto nearest = static_cast<int>(std::round(note));
    const auto cents = reading.voiced ? (note - static_cast<float>(nearest)) * 100.0f : 0.0f;
    const auto classic = tuner->view() == TunerDevice::View::classic;
    const auto strobe = tuner->mode() == TunerDevice::Mode::strobe;
    const auto showCents = tuner->showsCents();

    const auto switchBox = [&g] (juce::Rectangle<int> bounds, const juce::String& text, bool on, bool enabled = true)
    {
        g.setColour(on ? accentInk.withAlpha(enabled ? 0.88f : 0.35f) : juce::Colour(0xff232b32));
        g.fillRoundedRectangle(bounds.toFloat(), 2.5f);
        g.setColour(on ? accentInk.brighter(0.4f).withAlpha(enabled ? 1.0f : 0.4f) : frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.5f, 1.0f);
        g.setColour(on ? juce::Colour(0xff15191d) : enabled ? dimText : deadText);
        g.setFont(uiFontBold(8.5f));
        drawSnappedText(g, text, bounds, juce::Justification::centred);
    };
    switchBox(layout.classic, "CLASSIC", classic);
    switchBox(layout.histogram, "HISTOGRAM", !classic);
    // Target and Strobe are Classic's two ways of drawing the arc, so they
    // stand unlit while the histogram shows.
    switchBox(layout.target, "TARGET", !strobe, classic);
    switchBox(layout.strobe, "STROBE", strobe, classic);
    switchBox(layout.hertz, "Hz", !showCents);
    switchBox(layout.cents, "CENTS", showCents);

    const auto screen = layout.display;
    g.setColour(wellInk);
    g.fillRoundedRectangle(screen.toFloat(), 3.0f);
    g.setColour(frameInk);
    g.drawRoundedRectangle(screen.toFloat().reduced(0.5f), 3.0f, 1.0f);
    const auto colour = reading.voiced ? tuningColour(cents) : deadText;

    if (!classic)
    {
        // ---- pitch over time ------------------------------------------------
        const auto names = screen.withTrimmedLeft(screen.getWidth() - 34);
        const auto plot = screen.withTrimmedRight(36).reduced(6, 8).toFloat();
        std::array<float, TunerEngine::historyLength> history {};
        const auto wanted = std::min(TunerEngine::historyLength,
                                     static_cast<int>(historySeconds * tuner->analysesPerSecond()));
        const auto count = tuner->history(history.data(), wanted);
        // Centred on the note being played, and left where it was when
        // nothing is, so the plot does not jump home between notes.
        if (reading.voiced)
            tunerHistogramCentre = nearest;
        const auto centre = tunerHistogramCentre;
        const auto rowHeight = plot.getHeight() / static_cast<float>(2 * histogramSemitones + 1);
        const auto yOf = [&] (float pitch)
        {
            return plot.getCentreY() - (pitch - static_cast<float>(centre)) * rowHeight;
        };
        g.setFont(uiFont(8.5f));
        for (int step = -histogramSemitones; step <= histogramSemitones; ++step)
        {
            const auto y = yOf(static_cast<float>(centre + step));
            g.setColour(step == 0 ? juce::Colour(0xff3b464f) : juce::Colour(0xff252d34));
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - 0.5f, plot.getWidth(), 1.0f));
            g.setColour(step == 0 ? brightText : dimText);
            drawSnappedText(g, noteName(centre + step),
                            juce::Rectangle<int>(names.getX(), juce::roundToInt(y) - 6, names.getWidth() - 4, 12),
                            juce::Justification::centredLeft);
        }
        const auto slots = std::max(1, wanted - 1);
        for (int i = 0; i < count; ++i)
        {
            const auto frequency = history[static_cast<size_t>(i)];
            if (frequency <= 0.0f)
                continue;
            const auto pitch = TunerEngine::noteFor(frequency, reference);
            const auto y = yOf(pitch);
            if (y < plot.getY() || y > plot.getBottom())
                continue;
            // Newest at the right edge, however much history there is yet.
            const auto x = plot.getRight() - static_cast<float>(count - 1 - i) / static_cast<float>(slots) * plot.getWidth();
            g.setColour(tuningColour((pitch - std::round(pitch)) * 100.0f));
            g.fillRect(juce::Rectangle<float>(x - 1.0f, y - 1.0f, 2.5f, 2.5f));
        }
        g.setColour(reading.voiced ? brightText : deadText);
        g.setFont(uiFontBold(10.0f));
        drawSnappedText(g, readoutText(reading, note, showCents),
                        screen.reduced(8, 5).withHeight(14), juce::Justification::centredLeft);
        return;
    }

    // ---- Classic: the arc ---------------------------------------------------
    const auto area = screen.toFloat().reduced(10.0f, 6.0f);
    const auto radius = std::min(area.getWidth() * 0.5f / std::sin(juce::degreesToRadians(arcDegrees)),
                                 area.getHeight() * 1.05f);
    const juce::Point<float> pivot { area.getCentreX(), area.getY() + 12.0f + radius };

    if (strobe)
    {
        // A band of lights whose pattern runs right when the pitch is sharp
        // and left when it is flat, faster the further off, and stands still
        // in tune.
        for (int light = 0; light < strobeLights; ++light)
        {
            const auto position = -arcCents + (light + 0.5f) * (2.0f * arcCents / strobeLights);
            const auto lit = reading.voiced
                && (static_cast<int>(std::floor((static_cast<float>(light) - tunerStrobePhase) / 3.0f)) % 2 == 0);
            const auto point = onArc(pivot, radius, position);
            g.setColour(lit ? colour : juce::Colour(0xff222a31));
            g.fillEllipse(point.x - 3.5f, point.y - 3.5f, 7.0f, 7.0f);
        }
    }
    else
    {
        for (int tick = -10; tick <= 10; ++tick)
        {
            const auto point = onArc(pivot, radius, static_cast<float>(tick) * 5.0f);
            const auto major = tick % 5 == 0;
            g.setColour(major ? juce::Colour(0xff56636c) : juce::Colour(0xff323c44));
            const auto size = major ? 4.0f : 2.5f;
            g.fillEllipse(point.x - size * 0.5f, point.y - size * 0.5f, size, size);
        }
        // The target the ball has to sit in.
        const auto top = onArc(pivot, radius, 0.0f);
        g.setColour(reading.voiced && std::abs(cents) <= inTuneCents ? inTuneInk : juce::Colour(0xff6a7781));
        g.drawEllipse(top.x - 8.0f, top.y - 8.0f, 16.0f, 16.0f, 1.5f);
        if (reading.voiced)
        {
            const auto ball = onArc(pivot, radius, cents);
            g.setColour(colour);
            g.fillEllipse(ball.x - 5.5f, ball.y - 5.5f, 11.0f, 11.0f);
        }
    }

    g.setFont(uiFont(8.0f));
    g.setColour(deadText);
    const auto leftEnd = onArc(pivot, radius, -arcCents), rightEnd = onArc(pivot, radius, arcCents);
    drawSnappedText(g, "-50", juce::Rectangle<int>(juce::roundToInt(leftEnd.x) - 12, juce::roundToInt(leftEnd.y) + 6, 24, 10),
                    juce::Justification::centred);
    drawSnappedText(g, "+50", juce::Rectangle<int>(juce::roundToInt(rightEnd.x) - 12, juce::roundToInt(rightEnd.y) + 6, 24, 10),
                    juce::Justification::centred);

    // ---- the note, which way to turn, and how far ---------------------------
    const auto noteArea = juce::Rectangle<int>(screen.getCentreX() - 50, screen.getY() + 52, 100, 34);
    g.setColour(reading.voiced ? brightText : deadText);
    g.setFont(uiFontBold(28.0f));
    drawSnappedText(g, reading.voiced ? juce::String(pitchClassName(nearest)) : juce::String("-"),
                    noteArea, juce::Justification::centred);
    if (reading.voiced)
    {
        g.setFont(uiFont(11.0f));
        g.setColour(dimText);
        drawSnappedText(g, juce::String(nearest / 12 - 1),
                        noteArea.withTrimmedLeft(72).withTrimmedTop(16), juce::Justification::centredLeft);
    }
    // The arrow on the flat side points up the scale and lights when the
    // string is flat; the one on the sharp side, the other way.
    const auto flat = reading.voiced && cents < -inTuneCents;
    const auto sharp = reading.voiced && cents > inTuneCents;
    g.setFont(uiFontBold(14.0f));
    g.setColour(flat ? offInk : juce::Colour(0xff2a333a));
    drawSnappedText(g, juce::String::fromUTF8("\xe2\x96\xb6"), noteArea.withWidth(18).translated(-26, 0),
                    juce::Justification::centred);
    g.setColour(sharp ? offInk : juce::Colour(0xff2a333a));
    drawSnappedText(g, juce::String::fromUTF8("\xe2\x97\x80"), noteArea.withTrimmedLeft(noteArea.getWidth() - 18).translated(26, 0),
                    juce::Justification::centred);

    g.setColour(reading.voiced ? colour : deadText);
    g.setFont(uiFontBold(11.0f));
    drawSnappedText(g, readoutText(reading, note, showCents),
                    juce::Rectangle<int>(screen.getX(), noteArea.getBottom() + 4, screen.getWidth(), 16),
                    juce::Justification::centred);
}

bool DeviceEditorPanel::handleTunerClick(const juce::MouseEvent& event)
{
    auto* tuner = dynamic_cast<TunerDevice*>(session.devicePlugin(track, pluginSlot));
    if (tuner == nullptr)
        return false;
    const auto layout = tunerLayout(getLocalBounds());
    const auto position = event.getEventRelativeTo(this).getPosition();
    // Each switch is one of the device's own controls, written as one undo
    // step the way a generated chooser is.
    const auto choose = [this] (int parameter, float value, const juce::String& message)
    {
        writeParameter(parameter, value);
        repaint();
        if (status) status(message);
        return true;
    };
    if (layout.classic.contains(position))
        return choose(TunerDevice::viewParameter, 0.0f, "Tuner: classic view");
    if (layout.histogram.contains(position))
        return choose(TunerDevice::viewParameter, 1.0f, "Tuner: pitch over time");
    if (tuner->view() == TunerDevice::View::classic && layout.target.contains(position))
        return choose(TunerDevice::modeParameter, 0.0f, "Tuner: target");
    if (tuner->view() == TunerDevice::View::classic && layout.strobe.contains(position))
        return choose(TunerDevice::modeParameter, 1.0f, "Tuner: strobe");
    if (layout.hertz.contains(position))
        return choose(TunerDevice::unitsParameter, 0.0f, "Tuner: reading in hertz");
    if (layout.cents.contains(position))
        return choose(TunerDevice::unitsParameter, 1.0f, "Tuner: reading in cents from the note");
    return false;
}

void DeviceEditorPanel::tickTuner()
{
    auto* tuner = dynamic_cast<TunerDevice*>(session.devicePlugin(track, pluginSlot));
    if (tuner == nullptr)
        return;
    const auto reading = tuner->reading();
    const auto note = TunerEngine::noteFor(reading.frequency, tuner->referenceHz());
    const auto cents = reading.voiced ? (note - std::round(note)) * 100.0f : 0.0f;
    if (tuner->view() == TunerDevice::View::classic && tuner->mode() == TunerDevice::Mode::strobe && reading.voiced)
        // Off by more than a few cents the band runs at a speed you can count;
        // capped so a note a quarter-tone out does not blur into a flicker.
        tunerStrobePhase = std::fmod(tunerStrobePhase + std::clamp(cents, -40.0f, 40.0f) * 0.02f
                                         + static_cast<float>(strobeLights), static_cast<float>(strobeLights));
    // Only what the display would show differently asks for a frame: a tuner
    // with nothing coming in costs nothing. The histogram scrolls for as long
    // as anything has been heard in its window.
    const auto drawn = juce::String(reading.voiced ? 1 : 0) + ":" + juce::String(juce::roundToInt(note * 200.0f))
        + ":" + juce::String(juce::roundToInt(reading.frequency * 10.0f)) + ":" + juce::String(tunerStrobePhase, 2);
    const auto scrolling = tuner->view() == TunerDevice::View::histogram && heardWithinHistory(*tuner);
    if (drawn == tunerDrawn && !scrolling)
        return;
    tunerDrawn = drawn;
    repaint(tunerLayout(getLocalBounds()).display);
}
}
