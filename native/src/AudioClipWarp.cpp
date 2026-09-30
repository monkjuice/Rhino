#include "AudioClipPanel.h"
#include "Theme.h"
#include "WaveformLanes.h"
#include <algorithm>
#include <cmath>

// The warp half of the audio clip editor: the strip along the foot of the
// panel that says whether this clip follows the tempo and how, and the markers
// over the waveform that pin moments in the file to moments in the song.
//
// AudioClipPanel is defined across this file and AudioClipPanel.cpp, which is
// the mechanism the rest of this codebase uses for a class that has grown a
// second responsibility. Nothing in the header moved and no call site changed.
//
// The picture is the point of this file. A warp is a map from file time to
// played time, and a list of numbers is a terrible way to read one; a waveform
// drawn in played time shows it directly - the bars of a loop that drifts are
// visibly crowded before a marker and thin after it, and pulling the marker
// until they even up is the whole gesture.

namespace rhino
{
namespace
{
constexpr auto warpAccent = 0xff7fc9dd;
constexpr auto markerColour = 0xffe6c56a;

void styleWarpButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a30));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(warpAccent));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff9aa6af));
    button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff10161a));
}
}

void AudioClipPanel::configureWarpControls()
{
    warpToggle.setButtonText("Warp");
    warpToggle.setTooltip("Follow the song's tempo instead of playing at the speed this clip was recorded at");
    warpToggle.onClick = [this] { report(session.setClipFollowsTempo(clip, !warp.followsTempo)); };
    // A button rather than a ComboBox: the list is five items each of which
    // needs a line saying what it is for, and a combo box has nowhere to put
    // one. The menu is rebuilt each time for the same reason the MIDI input
    // chooser is - it is cheap and it cannot go stale.
    warpModeButton.setTooltip("How this clip is stretched to fit the tempo");
    warpModeButton.onClick = [this] { showWarpModeMenu(); };
    markerToggle.setButtonText("Markers");
    markerToggle.setTooltip("Pin moments in the file to moments in the song, and stretch what is between them");
    markerToggle.onClick = [this] { report(session.setClipWarpMarkersEnabled(clip, !warp.markersEnabled)); };
    halveBpm.setButtonText(":2");
    halveBpm.setTooltip("Half the clip tempo: use this when the clip plays at half the speed it should");
    halveBpm.onClick = [this] { report(session.scaleClipBpm(clip, 0.5)); };
    doubleBpm.setButtonText("*2");
    doubleBpm.setTooltip("Double the clip tempo: use this when the clip plays at twice the speed it should");
    doubleBpm.onClick = [this] { report(session.scaleClipBpm(clip, 2.0)); };
    detectBpm.setButtonText("Detect");
    detectBpm.setTooltip("Work the clip's tempo out from the audio");
    detectBpm.onClick = [this] { report(session.detectClipBpm(clip)); };
    straighten.setButtonText("Straighten");
    straighten.setTooltip("Take every warp marker back to where the file puts it");
    straighten.onClick = [this] { report(session.resetClipWarpMarkers(clip)); };
    for (auto* button : {&warpToggle, &markerToggle, &warpModeButton, &halveBpm, &doubleBpm,
                         &detectBpm, &straighten})
    {
        styleWarpButton(*button);
        addAndMakeVisible(button);
    }
    clipBpm.setRange(Session::minimumClipBpm, Session::maximumClipBpm, 1.0, 0.01);
    clipBpm.setDecimalPlaces(2);
    // A suffix rather than a caption: the strip gives this box one line, and a
    // caption stacked over a value needs two.
    clipBpm.setSuffix("BPM");
    clipBpm.setJustification(juce::Justification::centredLeft);
    clipBpm.setFontSize(12.0f, 7.5f);
    clipBpm.setTooltip("How fast this clip's own material is - drag up or down, or double-click to type");
    clipBpm.onDragStart = [this] { session.beginAudioClipGesture("Clip tempo"); };
    clipBpm.onDragEnd = [this] { session.endAudioClipGesture(); };
    clipBpm.onValueChange = [this](double bpm)
    {
        if (!syncing) report(session.setClipBpm(clip, bpm));
    };
    addAndMakeVisible(clipBpm);
}

void AudioClipPanel::report(const juce::Result& result)
{
    if (result.failed() && status) status(result.getErrorMessage());
}

void AudioClipPanel::showWarpModeMenu()
{
    if (!warp.valid) return;
    juce::PopupMenu menu;
    menu.addSectionHeader("WARP MODE");
    for (int i = 0; i < Session::warpModeCount; ++i)
    {
        const auto mode = static_cast<Session::WarpMode>(i);
        juce::PopupMenu::Item item {Session::warpModeName(mode)};
        item.itemID = i + 1;
        item.isTicked = warp.mode == mode;
        // The blurb is the whole reason this is a menu: five algorithm names
        // mean nothing on their own, and which one to reach for is the only
        // question anyone actually has here.
        item.shortcutKeyDescription = Session::warpModeBlurb(mode);
        menu.addItem(std::move(item));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(warpModeButton),
        [safe = juce::Component::SafePointer<AudioClipPanel>(this)](int result)
        {
            if (safe == nullptr || result <= 0) return;
            safe->report(safe->session.setClipWarpMode(safe->clip,
                                                       static_cast<Session::WarpMode>(result - 1)));
        });
}

void AudioClipPanel::syncWarpControls()
{
    warp = session.clipWarp(clip);
    const auto on = warp.valid && warp.followsTempo;
    warpToggle.setEnabled(warp.valid);
    warpToggle.setToggleState(on, juce::dontSendNotification);
    warpModeButton.setEnabled(warp.valid);
    warpModeButton.setButtonText(warp.valid ? Session::warpModeName(warp.mode) : juce::String("Warp mode"));
    // Repitch plays the file faster or slower whole, so there is nothing for a
    // marker to stretch. Saying so by greying the switch is better than
    // refusing the click afterwards.
    const auto markersPossible = on && warp.mode != Session::WarpMode::repitch;
    markerToggle.setEnabled(markersPossible);
    markerToggle.setToggleState(warp.markersEnabled, juce::dontSendNotification);
    straighten.setEnabled(warp.markersEnabled);
    for (auto* button : {&halveBpm, &doubleBpm, &detectBpm})
        button->setEnabled(warp.valid && warp.sourceLengthSeconds > 0.0);
    clipBpm.setEnabled(warp.valid && warp.sourceLengthSeconds > 0.0);
    {
        const juce::ScopedValueSetter<bool> scope(syncing, true);
        clipBpm.setValue(warp.clipBpm > 0.0 ? warp.clipBpm : Session::minimumClipBpm);
    }
    // The transient scan runs on a worker and is not ready in the frame that
    // asked for it, so the panel looks again a few times a second until it is
    // and then stops. Nothing else here is polled.
    if (warp.markersEnabled && !warp.transientsReady)
        startTimerHz(4);
    else
        stopTimer();
}

void AudioClipPanel::timerCallback()
{
    const auto ready = session.clipWarp(clip).transientsReady;
    if (!ready) return;
    stopTimer();
    warp = session.clipWarp(clip);
    repaint(waveArea);
}

void AudioClipPanel::layoutWarpControls(juce::Rectangle<int> strip)
{
    warpStrip = strip;
    auto row = strip.reduced(6, 4);
    const auto take = [&row](int width)
    {
        auto cell = row.removeFromLeft(std::min(width, std::max(0, row.getWidth())));
        row.removeFromLeft(std::min(5, std::max(0, row.getWidth())));
        return cell;
    };
    warpToggle.setBounds(take(56));
    warpModeButton.setBounds(take(74));
    clipBpm.setBounds(take(96));
    halveBpm.setBounds(take(30));
    doubleBpm.setBounds(take(30));
    // The last three go when the panel is too narrow for them rather than
    // being squeezed into slivers. Warp, the mode and the tempo are what the
    // strip is for; everything after it is a convenience.
    detectBpm.setVisible(row.getWidth() >= 190);
    if (detectBpm.isVisible()) detectBpm.setBounds(take(56));
    markerToggle.setVisible(row.getWidth() >= 130);
    if (markerToggle.isVisible()) markerToggle.setBounds(take(64));
    straighten.setVisible(row.getWidth() >= 70);
    if (straighten.isVisible()) straighten.setBounds(take(68));
}

void AudioClipPanel::paintWarpStrip(juce::Graphics& g)
{
    if (warpStrip.isEmpty()) return;
    g.setColour(juce::Colour(0xff1f262c));
    g.fillRect(warpStrip);
    g.setColour(juce::Colour(0xff2b343b));
    g.fillRect(warpStrip.getX(), warpStrip.getY(), warpStrip.getWidth(), 1);
}

bool AudioClipPanel::warpMarkersVisible() const
{
    return warp.valid && warp.markersEnabled && warp.markers.size() >= 2 && warpedLength() > 0.0;
}

double AudioClipPanel::warpedLength() const
{
    return warp.markers.empty() ? 0.0 : warp.markers.back().warpSeconds;
}

// The span the warp is drawn across. Inset far enough that the handle at each
// end of the file is drawn whole rather than half off the edge of the box, and
// shared by the handles and the waveform under them so the two cannot drift.
juce::Rectangle<int> AudioClipPanel::warpSpan() const
{
    return waveArea.reduced(9, 0);
}

float AudioClipPanel::xForWarpTime(double warpSeconds) const
{
    const auto length = warpedLength();
    const auto span = warpSpan();
    if (!(length > 0.0) || span.getWidth() <= 0) return static_cast<float>(waveArea.getX());
    return static_cast<float>(span.getX())
         + static_cast<float>(warpSeconds / length) * static_cast<float>(span.getWidth());
}

double AudioClipPanel::warpTimeAtX(float x) const
{
    const auto length = warpedLength();
    const auto span = warpSpan();
    if (!(length > 0.0) || span.getWidth() <= 0) return 0.0;
    return juce::jlimit(0.0, length,
                        (x - static_cast<double>(span.getX())) / span.getWidth() * length);
}

// The forward half of the map, which only the transient ticks need: they are
// positions in the file and have to be drawn where the clip plays them.
double AudioClipPanel::warpTimeForSourceTime(double sourceSeconds) const
{
    if (warp.markers.size() < 2) return sourceSeconds;
    for (size_t i = 1; i < warp.markers.size(); ++i)
    {
        const auto& before = warp.markers[i - 1];
        const auto& after = warp.markers[i];
        if (sourceSeconds > after.sourceSeconds && i + 1 < warp.markers.size())
            continue;
        const auto span = after.sourceSeconds - before.sourceSeconds;
        if (!(span > 0.0)) return before.warpSeconds;
        const auto fraction = (sourceSeconds - before.sourceSeconds) / span;
        return before.warpSeconds + fraction * (after.warpSeconds - before.warpSeconds);
    }
    return warp.markers.back().warpSeconds;
}

double AudioClipPanel::sourceTimeForWarpTime(double warpSeconds) const
{
    if (warp.markers.size() < 2) return warpSeconds;
    for (size_t i = 1; i < warp.markers.size(); ++i)
    {
        const auto& before = warp.markers[i - 1];
        const auto& after = warp.markers[i];
        if (warpSeconds > after.warpSeconds && i + 1 < warp.markers.size())
            continue;
        const auto span = after.warpSeconds - before.warpSeconds;
        if (!(span > 0.0)) return before.sourceSeconds;
        const auto fraction = (warpSeconds - before.warpSeconds) / span;
        return before.sourceSeconds + fraction * (after.sourceSeconds - before.sourceSeconds);
    }
    return warp.markers.back().sourceSeconds;
}

int AudioClipPanel::warpMarkerAt(juce::Point<float> point) const
{
    if (!warpMarkersVisible() || !markerBar.contains(point.toInt())) return -1;
    auto best = -1;
    auto bestDistance = markerGrabPixels;
    for (int i = 0; i < static_cast<int>(warp.markers.size()); ++i)
    {
        const auto distance = std::abs(xForWarpTime(warp.markers[static_cast<size_t>(i)].warpSeconds) - point.x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

// One drawChannel per segment. Each pair of markers maps a span of the file
// onto a span of played time, and the engine's mapping is linear inside a
// segment, so drawing the source span into the segment's own width is the
// warped waveform exactly - no resampling of the thumbnail and no per-column
// loop. Clipped per segment, because the thumbnail draws the span it is given
// across the whole rectangle it is given.
void AudioClipPanel::paintWarpedWaveform(juce::Graphics& g, juce::Rectangle<int> inner)
{
    const auto colour = juce::Colour(0xff8cc5d2).withAlpha(mix.muted ? 0.32f : 1.0f);
    for (size_t i = 1; i < warp.markers.size(); ++i)
    {
        const auto& before = warp.markers[i - 1];
        const auto& after = warp.markers[i];
        const auto left = xForWarpTime(before.warpSeconds);
        const auto right = xForWarpTime(after.warpSeconds);
        if (!(right > left + 0.5f)) continue;
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion({juce::roundToInt(left), inner.getY(),
                            std::max(1, juce::roundToInt(right) - juce::roundToInt(left)), inner.getHeight()});
        paintWaveformLanes(g, *thumbnail,
                           inner.withX(juce::roundToInt(left))
                                .withWidth(std::max(1, juce::roundToInt(right - left))),
                           before.sourceSeconds, after.sourceSeconds, 1.45f, colour);
    }
    // Where the clip actually starts and ends in all this. The rest of the
    // file is still drawn, dimmed, because a marker often needs placing just
    // outside the part being played.
    const auto speed = std::max(0.0001, mix.speedRatio);
    const auto playedFrom = xForWarpTime(mix.offsetSeconds * speed);
    const auto playedTo = xForWarpTime((mix.offsetSeconds + mix.lengthSeconds()) * speed);
    g.setColour(juce::Colour(0xcc13181c));
    if (playedFrom > inner.getX())
        g.fillRect(juce::Rectangle<float>(static_cast<float>(inner.getX()), static_cast<float>(inner.getY()),
                                          playedFrom - inner.getX(), static_cast<float>(inner.getHeight())));
    if (playedTo < inner.getRight())
        g.fillRect(juce::Rectangle<float>(playedTo, static_cast<float>(inner.getY()),
                                          inner.getRight() - playedTo, static_cast<float>(inner.getHeight())));
}

void AudioClipPanel::paintWarpMarkers(juce::Graphics& g)
{
    if (!warpMarkersVisible() || markerBar.isEmpty()) return;
    g.setColour(juce::Colour(0xff10161a));
    g.fillRect(markerBar);
    g.setColour(juce::Colour(0xff2b343b));
    g.fillRect(markerBar.getX(), markerBar.getBottom() - 1, markerBar.getWidth(), 1);
    // The attacks the file was found to have, drawn where the warp puts them
    // rather than where they sit in the file, so a marker can be aimed at one.
    g.setColour(juce::Colour(0xff3d4a52));
    for (const auto transient : warp.transients)
    {
        const auto x = xForWarpTime(warpTimeForSourceTime(transient));
        if (x < markerBar.getX() || x > markerBar.getRight()) continue;
        g.fillRect(juce::Rectangle<float>(x, static_cast<float>(markerBar.getBottom() - 4), 1.0f, 3.0f));
    }
    for (int i = 0; i < static_cast<int>(warp.markers.size()); ++i)
    {
        const auto x = xForWarpTime(warp.markers[static_cast<size_t>(i)].warpSeconds);
        const auto held = i == draggingMarker;
        const auto ends = i == 0 || i == static_cast<int>(warp.markers.size()) - 1;
        g.setColour(juce::Colour(markerColour).withAlpha(ends ? 0.55f : 1.0f).brighter(held ? 0.4f : 0.0f));
        // A handle in the bar and a hairline down the waveform, so the marker
        // can be grabbed at the top and read against the audio below it.
        juce::Path handle;
        handle.addTriangle(x - 4.0f, static_cast<float>(markerBar.getY()),
                           x + 4.0f, static_cast<float>(markerBar.getY()),
                           x, static_cast<float>(markerBar.getBottom() - 1));
        g.fillPath(handle);
        g.setColour(juce::Colour(markerColour).withAlpha(held ? 0.75f : ends ? 0.20f : 0.42f));
        g.fillRect(juce::Rectangle<float>(x - 0.5f, static_cast<float>(markerBar.getBottom()), 1.0f,
                                          static_cast<float>(waveArea.getBottom() - markerBar.getBottom())));
    }
}

void AudioClipPanel::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(warpMarkerAt(event.position) >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                     : juce::MouseCursor::NormalCursor);
}

void AudioClipPanel::mouseDown(const juce::MouseEvent& event)
{
    draggingMarker = -1;
    const auto index = warpMarkerAt(event.position);
    if (index < 0) return;
    // Right-click or Ctrl-click takes a marker away, which is the same gesture
    // every marker in every DAW answers to.
    if (event.mods.isPopupMenu() || event.mods.isCtrlDown())
    {
        report(session.removeClipWarpMarker(clip, index));
        return;
    }
    draggingMarker = index;
    warpGestureOpen = true;
    session.beginAudioClipGesture("Move warp marker");
    repaint(waveArea);
}

void AudioClipPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingMarker < 0) return;
    report(session.moveClipWarpMarker(clip, draggingMarker, warpTimeAtX(event.position.x)));
    warp = session.clipWarp(clip);
    repaint(waveArea);
}

void AudioClipPanel::mouseUp(const juce::MouseEvent&)
{
    if (draggingMarker < 0) return;
    draggingMarker = -1;
    if (warpGestureOpen)
    {
        warpGestureOpen = false;
        session.endAudioClipGesture();
    }
    repaint(waveArea);
}

void AudioClipPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!warpMarkersVisible() || !waveArea.contains(event.position.toInt())) return;
    if (warpMarkerAt(event.position) >= 0) return;
    report(session.addClipWarpMarker(clip, sourceTimeForWarpTime(warpTimeAtX(event.position.x))));
}

}
