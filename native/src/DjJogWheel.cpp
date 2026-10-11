#include "DjJogWheel.h"
#include "DevicePixels.h"
#include <cmath>
#include <utility>

// The booth's jog wheel: see DjJogWheel.h.

namespace rhino
{
DjJogWheel::DjJogWheel()
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void DjJogWheel::setTrackName(const juce::String& name)
{
    if (trackName == name) return;
    trackName = name;
    dropFace();
}

void DjJogWheel::setPosition(double seconds, bool isPlaying)
{
    const auto angle = static_cast<float>(std::fmod(std::max(0.0, seconds) / secondsPerTurn, 1.0) * juce::MathConstants<double>::twoPi);
    // A turn the screen cannot show, under half a pixel at the marker, is
    // not drawn; the marker catches up once it has moved one.
    if (playing == isPlaying && markerBox(angle).getCentre().getDistanceFrom(markerBox(markerAngle).getCentre()) < 0.5f)
        return;
    const auto previous = std::exchange(markerAngle, angle);
    playing = isPlaying;
    repaint(markerDamage(previous));
    repaint(markerDamage(markerAngle));
}

void DjJogWheel::setVinylMode(bool vinyl)
{
    if (vinylMode == vinyl) return;
    vinylMode = vinyl;
    dropFace();
}

void DjJogWheel::resized()
{
    dropFace();
}

void DjJogWheel::enablementChanged()
{
    dropFace();
}

void DjJogWheel::lookAndFeelChanged()
{
    dropFace();
}

void DjJogWheel::dropFace()
{
    face = {};
    repaint();
}

void DjJogWheel::paint(juce::Graphics& g)
{
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto phase = devicePhase(*this, scale);
    if (face.isNull() || faceScale != scale || facePhase != phase)
        renderFace(g, scale, phase);
    {
        // Drawn in device pixels from the device pixel the wheel's origin
        // falls in, the face lands whole on the grid it was painted for.
        // Drawn from the origin itself, between two device pixels, it was
        // resampled, and the ticks and the name came out half a pixel soft.
        const juce::Graphics::ScopedSaveState devicePixels(g);
        g.addTransform(juce::AffineTransform::scale(1.0f / faceScale));
        g.drawImageTransformed(face, juce::AffineTransform::translation(-facePhase.x, -facePhase.y));
    }
    // The marker, turning with the track.
    g.setColour(playing ? palette::djPlay : palette::textDim);
    g.fillEllipse(markerBox(markerAngle));
    if (dragging)
    {
        g.setColour(palette::selection.withAlpha(0.12f));
        g.fillEllipse(onRing ? outerArea() : platterArea());
    }
}

// Painted in software and handed to the context's own image type once:
// opening a Graphics on a Direct2D image costs milliseconds whatever is drawn
// (wiki/pages/profile-paint-on-a-software-image.md), a software one nothing.
// The picture starts at the device pixel the wheel's origin falls in, and the
// face is painted `phase` into it.
void DjJogWheel::renderFace(juce::Graphics& g, float scale, juce::Point<float> phase)
{
    const auto width = std::max(1, static_cast<int>(std::ceil(static_cast<float>(getWidth()) * scale + phase.x)));
    const auto height = std::max(1, static_cast<int>(std::ceil(static_cast<float>(getHeight()) * scale + phase.y)));
    juce::Image picture(juce::Image::RGB, width, height, false, juce::SoftwareImageType());
    {
        juce::Graphics painter(picture);
        painter.addTransform(juce::AffineTransform::scale(scale).translated(phase.x, phase.y));
        // The console's own ground: the wheel stands on it and is opaque.
        painter.fillAll(palette::sideSurface);
        const auto outer = outerArea();
        const auto platter = platterArea();
        // The ring, with the ticks a CDJ's jog ring carries, as one path.
        painter.setColour(palette::trackCard);
        painter.fillEllipse(outer);
        painter.setColour(palette::border);
        painter.drawEllipse(outer, 1.0f);
        const auto centre = outer.getCentre();
        const auto outerRadius = outer.getWidth() * 0.5f;
        const auto ringInner = platter.getWidth() * 0.5f + 2.0f;
        juce::Path ticks;
        for (int i = 0; i < 48; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 48.0f;
            ticks.addLineSegment({centre.getPointOnCircumference(ringInner + 2.0f, angle),
                                  centre.getPointOnCircumference(outerRadius - 3.0f, angle)},
                                 i % 4 == 0 ? 1.5f : 1.0f);
        }
        painter.setColour(palette::border.brighter(0.25f));
        painter.fillPath(ticks);
        // The platter and its centre window.
        painter.setColour(palette::displayInset);
        painter.fillEllipse(platter);
        painter.setColour(vinylMode ? palette::djCue.withAlpha(0.6f) : palette::border);
        painter.drawEllipse(platter, 1.5f);
        const auto window = platter.reduced(platter.getWidth() * 0.22f);
        painter.setColour(palette::sideSurface);
        painter.fillEllipse(window);
        painter.setColour(palette::border);
        painter.drawEllipse(window, 1.0f);
        // The name, two lines at most.
        painter.setColour(palette::text);
        painter.setFont(window.getWidth() >= 70.0f ? uiFontBold(10.0f) : uiFontBold(8.0f));
        const auto text = window.toNearestInt().reduced(static_cast<int>(window.getWidth() * 0.12f), 0);
        if (trackName.isEmpty())
        {
            painter.setColour(palette::textDim);
            drawSnappedText(painter, "NO TRACK", text, juce::Justification::centred, true);
        }
        else
            painter.drawFittedText(trackName.toUpperCase(), text, juce::Justification::centred, 2, 0.8f);
    }
    face = g.getInternalContext().getPreferredImageTypeForTemporaryImages()->convert(picture);
    faceScale = scale;
    facePhase = phase;
}

juce::Rectangle<float> DjJogWheel::markerBox(float angle) const
{
    const auto platter = platterArea();
    const auto marker = outerArea().getCentre().getPointOnCircumference(platter.getWidth() * 0.5f - 6.0f, angle);
    return juce::Rectangle<float>(6.0f, 6.0f).withCentre(marker);
}

juce::Rectangle<int> DjJogWheel::markerDamage(float angle) const
{
    return markerBox(angle).expanded(1.0f).getSmallestIntegerContainer();
}

void DjJogWheel::mouseDown(const juce::MouseEvent& event)
{
    const auto platter = platterArea();
    const auto centre = platter.getCentre();
    const auto distance = event.position.getDistanceFrom(centre);
    if (distance > outerArea().getWidth() * 0.5f) return;
    dragging = true;
    onRing = distance > platter.getWidth() * 0.5f;
    lastAngle = angleOf(event.position);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    scratchActive = false;
    scrubbing = false;
    repaint();
    startTimerHz(30);
}

void DjJogWheel::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging) return;
    const auto angle = angleOf(event.position);
    auto delta = angle - lastAngle;
    if (delta > juce::MathConstants<float>::pi) delta -= juce::MathConstants<float>::twoPi;
    if (delta < -juce::MathConstants<float>::pi) delta += juce::MathConstants<float>::twoPi;
    lastAngle = angle;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto seconds = std::max(0.001, (now - lastTime) / 1000.0);
    lastTime = now;
    // One turn of the platter is 1.8 s of the record at 33 1/3.
    const auto turns = static_cast<double>(delta) / juce::MathConstants<double>::twoPi;
    const auto rate = turns * secondsPerTurn / seconds;
    const auto isPlaying = deckPlaying ? deckPlaying() : true;
    if (onRing || !vinylMode)
    {
        lastNudge = juce::jlimit(-8.0f, 8.0f, static_cast<float>(rate * 4.0));
        if (onNudge) onNudge(lastNudge);
        return;
    }
    if (!isPlaying)
    {
        // The travel is the drag's own, summed here: several drag steps can
        // arrive between two reads of where the deck stands.
        const auto starting = !scrubbing;
        if (starting)
        {
            scrubbing = true;
            scrubbed = 0.0;
        }
        scrubbed += turns * secondsPerTurn;
        if (onScrub) onScrub(scrubbed, starting);
        return;
    }
    scrubbing = false;
    scratchActive = true;
    lastRate = juce::jlimit(-8.0, 8.0, rate);
    if (onScratch) onScratch(lastRate, true);
}

void DjJogWheel::mouseUp(const juce::MouseEvent&)
{
    if (!dragging) return;
    dragging = false;
    stopTimer();
    if (scratchActive && onScratch) onScratch(0.0, false);
    if (lastNudge != 0.0f && onNudge) onNudge(0.0f);
    scratchActive = false;
    scrubbing = false;
    lastNudge = 0.0f;
    repaint();
}

void DjJogWheel::timerCallback()
{
    if (!dragging) return;
    const auto idle = juce::Time::getMillisecondCounterHiRes() - lastTime > 80.0;
    if (!idle) return;
    if (scratchActive && lastRate != 0.0)
    {
        lastRate = 0.0;
        if (onScratch) onScratch(0.0, true);
    }
    if (lastNudge != 0.0f)
    {
        lastNudge = 0.0f;
        if (onNudge) onNudge(0.0f);
    }
}

juce::Rectangle<float> DjJogWheel::outerArea() const
{
    const auto size = static_cast<float>(std::min(getWidth(), getHeight())) - 2.0f;
    return getLocalBounds().toFloat().withSizeKeepingCentre(size, size);
}

juce::Rectangle<float> DjJogWheel::platterArea() const
{
    return outerArea().reduced(outerArea().getWidth() * 0.11f);
}

float DjJogWheel::angleOf(juce::Point<float> point) const
{
    const auto centre = outerArea().getCentre();
    return std::atan2(point.x - centre.x, -(point.y - centre.y));
}
}
