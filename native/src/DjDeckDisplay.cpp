#include "DjDeckDisplay.h"
#include "Theme.h"
#include "UiVisibility.h"
#include <algorithm>
#include <cmath>

// A deck's screen: see DjDeckDisplay.h.

namespace rhino
{
namespace
{
constexpr double minimumSecondsAcross = 3.0, maximumSecondsAcross = 48.0;

// The loudest of a run of columns in each band, which is what a pixel shows.
DjWaveformColumn pooled(const std::vector<DjWaveformColumn>& columns, int from, int to)
{
    DjWaveformColumn out;
    const auto count = static_cast<int>(columns.size());
    from = std::max(0, from);
    to = std::min(count, std::max(to, from + 1));
    for (int c = from; c < to; ++c)
    {
        const auto& column = columns[static_cast<size_t>(c)];
        out.low = std::max(out.low, column.low);
        out.mid = std::max(out.mid, column.mid);
        out.high = std::max(out.high, column.high);
    }
    return out;
}

// Three bands mirrored about a centre line, lows behind, highs in front.
void drawColumn(juce::Graphics& g, float x, float width, float centreY, float halfHeight, const DjWaveformColumn& column)
{
    const auto bar = [&](float level, juce::Colour colour)
    {
        const auto h = std::max(0.5f, level * halfHeight);
        g.setColour(colour);
        g.fillRect(x, centreY - h, width, h * 2.0f);
    };
    bar(column.low, palette::djWaveLow);
    bar(column.mid, palette::djWaveMid);
    bar(column.high * 0.85f, palette::djWaveHigh);
}
}

DjDeckDisplay::DjDeckDisplay(Session& s, int deckIndex) : session(s), deck(deckIndex)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    sync();
}

void DjDeckDisplay::setDeck(int deckIndex)
{
    deck = deckIndex;
    generation = -1;
    sync();
}

void DjDeckDisplay::sync()
{
    const auto info = session.djDeckInfo(deck);
    const auto* next = info.loaded ? session.djDeckTrack(deck) : nullptr;
    if (next != track || info.generation != generation)
    {
        track = next;
        generation = info.generation;
        tiles.clear();
        overview = {};
        overviewWidth = 0;
        drawnPosition = -1.0;
    }
    state = session.djDeckState(deck);
    repaint();
}

juce::Rectangle<int> DjDeckDisplay::readoutArea() const
{
    return getLocalBounds().removeFromBottom(readoutHeight);
}

juce::Rectangle<int> DjDeckDisplay::overviewArea() const
{
    return getLocalBounds().withTrimmedBottom(readoutHeight).removeFromBottom(overviewHeight);
}

juce::Rectangle<int> DjDeckDisplay::waveArea() const
{
    return getLocalBounds().withTrimmedBottom(readoutHeight + overviewHeight);
}

double DjDeckDisplay::pixelsPerSecond() const
{
    return std::max(1, waveArea().getWidth()) / secondsAcross;
}

void DjDeckDisplay::resized()
{
    tiles.clear();
    overview = {};
    overviewWidth = 0;
}

// Once a frame. The scrolling strip moves whenever the deck does, and the
// overview's marker with it; the readouts change at most a few times a
// second and repaint only when their text does.
void DjDeckDisplay::tick()
{
    if (isHiddenInShell(*this)) return;
    state = session.djDeckState(deck);
    if (track == nullptr)
        return;
    if (state.positionSeconds != drawnPosition || state.jumps != drawnJumps)
    {
        repaint(waveArea());
        // The marker in the overview moves by a pixel at a time.
        const auto area = overviewArea();
        const auto length = std::max(1.0e-6, state.lengthSeconds);
        const auto before = area.getX() + static_cast<int>(area.getWidth() * drawnPosition / length);
        const auto after = area.getX() + static_cast<int>(area.getWidth() * state.positionSeconds / length);
        if (before != after || drawnPosition < 0.0)
            repaint(juce::Rectangle<int>(std::min(before, after) - 2, area.getY(), std::abs(after - before) + 5, area.getHeight()));
    }
    // The readouts change a few times a second at most, and a deck standing
    // still changes none of them; only new text is drawn.
    if (readoutText() != drawnReadout)
        repaint(readoutArea());
}

juce::String DjDeckDisplay::readoutText() const
{
    if (track == nullptr) return {};
    const auto remaining = state.lengthSeconds - state.positionSeconds;
    const auto bars = state.beat >= 0.0 ? static_cast<int>(std::floor(state.beat / state.beatsPerBar)) + 1 : 0;
    const auto beatInBar = state.beat >= 0.0 ? static_cast<int>(std::floor(state.beat)) % state.beatsPerBar + 1 : 0;
    const auto what = state.transport == Session::DjDeckState::Transport::waiting ? juce::String("WAIT")
                    : state.transport == Session::DjDeckState::Transport::cueing ? juce::String("CUE")
                    : state.isPlaying() ? juce::String("PLAY") : juce::String("STOP");
    const auto tempo = state.bpm > 0.0 ? juce::String(state.effectiveBpm, 2) + " BPM" : juce::String("-- BPM");
    const auto percent = (state.tempoPercent >= 0.0f ? "+" : "") + juce::String(state.tempoPercent, 2) + "%";
    return clock(state.positionSeconds) + "  " + clock(-remaining) + "|" + what + "|" + tempo + "|"
         + (state.synced ? "SYNC" : percent) + "|" + juce::String(bars) + "." + juce::String(beatInBar);
}

void DjDeckDisplay::paint(juce::Graphics& g)
{
    g.fillAll(palette::displayInset);
    const auto clip = g.getClipBounds();
    if (clip.intersects(waveArea())) paintWave(g, waveArea());
    if (clip.intersects(overviewArea())) paintOverview(g, overviewArea());
    if (clip.intersects(readoutArea())) paintReadouts(g, readoutArea());
}

const juce::Image* DjDeckDisplay::tileFor(int index)
{
    for (auto& tile : tiles)
        if (tile.index == index)
            return &tile.image;
    if (track == nullptr || index < 0) return nullptr;
    Tile tile;
    tile.index = index;
    tile.image = juce::Image(juce::Image::ARGB, tileWidth, std::max(1, waveArea().getHeight()), true);
    paintTile(tile.image, index);
    tiles.push_back(std::move(tile));
    // Tiles far from the playhead are dropped: a long track at a fine zoom
    // would otherwise keep a hundred of them.
    if (tiles.size() > 24)
        tiles.erase(tiles.begin());
    return &tiles.back().image;
}

void DjDeckDisplay::paintTile(juce::Image& image, int index) const
{
    juce::Graphics g(image);
    g.fillAll(palette::displayInset);
    if (track == nullptr || track->analysis.columns.empty()) return;
    const auto& columns = track->analysis.columns;
    const auto pps = pixelsPerSecond();
    const auto columnsPerPixel = track->sampleRate / DjAnalysis::columnFrames / pps;
    const auto height = static_cast<float>(image.getHeight());
    const auto centreY = height * 0.5f;
    const auto halfHeight = height * 0.46f;
    for (int x = 0; x < tileWidth; ++x)
    {
        const auto first = (index * tileWidth + x) * columnsPerPixel;
        const auto column = pooled(columns, static_cast<int>(first), static_cast<int>(first + columnsPerPixel));
        drawColumn(g, static_cast<float>(x), 1.0f, centreY, halfHeight, column);
    }
}

void DjDeckDisplay::paintWave(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(palette::displayInset);
    g.fillRect(area);
    if (track == nullptr)
    {
        g.setColour(palette::displayTextFaint);
        g.setFont(uiFont(11.0f));
        const auto info = session.djDeckInfo(deck);
        drawSnappedText(g, info.loading ? "Loading " + info.name + "..."
                           : info.error.isNotEmpty() ? info.error
                           : "No track. Choose one from the name above, or drop a file here.",
                        area.reduced(8, 0), juce::Justification::centred, true);
        return;
    }
    const auto pps = pixelsPerSecond();
    const auto centreX = area.getX() + area.getWidth() / 2;
    const auto positionPx = state.positionSeconds * pps;
    const auto leftEdgePx = positionPx - area.getWidth() / 2.0;
    // Tiles cover the strip from the left edge; the first one may be cut.
    {
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(area);
        const auto firstTile = static_cast<int>(std::floor(leftEdgePx / tileWidth));
        const auto lastTile = static_cast<int>(std::floor((leftEdgePx + area.getWidth()) / tileWidth));
        for (int index = std::max(0, firstTile); index <= lastTile; ++index)
        {
            const auto* image = tileFor(index);
            if (image == nullptr) continue;
            const auto x = area.getX() + static_cast<int>(std::round(index * tileWidth - leftEdgePx));
            g.drawImageAt(*image, x, area.getY());
        }
    }
    const auto xFor = [&](double seconds) { return static_cast<float>(area.getX() + (seconds * pps - leftEdgePx)); };
    const auto visibleStart = leftEdgePx / pps, visibleEnd = (leftEdgePx + area.getWidth()) / pps;
    // The beat grid: a bar line full height, a beat a short tick top and bottom.
    if (track->analysis.hasGrid())
    {
        const auto beat = track->analysis.secondsPerBeat();
        const auto perBar = track->analysis.beatsPerBar;
        const auto origin = track->analysis.firstBeatSeconds;
        const auto firstBeat = static_cast<long long>(std::floor((visibleStart - origin) / beat)) - 1;
        const auto lastBeat = static_cast<long long>(std::ceil((visibleEnd - origin) / beat)) + 1;
        if (lastBeat - firstBeat < 4000)
            for (auto b = firstBeat; b <= lastBeat; ++b)
            {
                const auto x = xFor(origin + b * beat);
                if (x < area.getX() || x > area.getRight()) continue;
                const auto bar = ((b % perBar) + perBar) % perBar == 0;
                g.setColour(bar ? palette::displayTextDim : palette::displayTextFaint.withMultipliedAlpha(0.6f));
                if (bar)
                    g.fillRect(x, static_cast<float>(area.getY()), 1.0f, static_cast<float>(area.getHeight()));
                else
                {
                    g.fillRect(x, static_cast<float>(area.getY()), 1.0f, 4.0f);
                    g.fillRect(x, static_cast<float>(area.getBottom() - 4), 1.0f, 4.0f);
                }
            }
    }
    // The loop, shaded, with its edges.
    if (state.loop.valid())
    {
        const auto from = std::max(static_cast<float>(area.getX()), xFor(state.loop.startSeconds));
        const auto to = std::min(static_cast<float>(area.getRight()), xFor(state.loop.endSeconds));
        if (to > from)
        {
            g.setColour((state.loop.active ? palette::djCue : palette::displayTextDim).withAlpha(state.loop.active ? 0.18f : 0.1f));
            g.fillRect(from, static_cast<float>(area.getY()), to - from, static_cast<float>(area.getHeight()));
            g.setColour(state.loop.active ? palette::djCue : palette::displayTextDim);
            g.fillRect(from, static_cast<float>(area.getY()), 1.0f, static_cast<float>(area.getHeight()));
            g.fillRect(to - 1.0f, static_cast<float>(area.getY()), 1.0f, static_cast<float>(area.getHeight()));
        }
    }
    // Cue points: a flag at the top in the cue's colour.
    const auto flag = [&](double seconds, juce::Colour colour, const juce::String& letter)
    {
        if (seconds < visibleStart - 1.0 || seconds > visibleEnd + 1.0) return;
        const auto x = xFor(seconds);
        g.setColour(colour);
        g.fillRect(x - 0.5f, static_cast<float>(area.getY()), 2.0f, static_cast<float>(area.getHeight()));
        const juce::Rectangle<int> box(static_cast<int>(x) + 1, area.getY(), 12, 11);
        g.fillRect(box);
        g.setColour(colour.contrasting(0.9f));
        g.setFont(uiFontBold(8.0f));
        drawSnappedText(g, letter, box, juce::Justification::centred);
    };
    flag(state.cueSeconds, palette::djCue, "C");
    for (int i = 0; i < 8; ++i)
        if (state.hotCueSeconds[static_cast<size_t>(i)] >= 0.0)
            flag(state.hotCueSeconds[static_cast<size_t>(i)], palette::djHotCue[static_cast<size_t>(i)], juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)));
    // The playhead, fixed in the middle.
    g.setColour(state.isPlaying() ? palette::djPlay : palette::text);
    g.fillRect(centreX - 1, area.getY(), 2, area.getHeight());
    drawnPosition = state.positionSeconds;
    drawnJumps = state.jumps;
}

void DjDeckDisplay::buildOverview()
{
    const auto area = overviewArea();
    overviewWidth = area.getWidth();
    overview = juce::Image(juce::Image::ARGB, std::max(1, area.getWidth()), std::max(1, area.getHeight()), true);
    juce::Graphics g(overview);
    g.fillAll(palette::displayInset.brighter(0.04f));
    if (track == nullptr || track->analysis.columns.empty()) return;
    const auto& columns = track->analysis.columns;
    const auto columnsPerPixel = static_cast<double>(columns.size()) / std::max(1, area.getWidth());
    const auto centreY = area.getHeight() * 0.5f;
    for (int x = 0; x < area.getWidth(); ++x)
    {
        const auto column = pooled(columns, static_cast<int>(x * columnsPerPixel), static_cast<int>((x + 1) * columnsPerPixel));
        drawColumn(g, static_cast<float>(x), 1.0f, centreY, centreY * 0.9f, column);
    }
    // Drops: a marker at the top of the bar they land on.
    if (track->analysis.hasGrid())
    {
        const auto barSeconds = track->analysis.secondsPerBeat() * track->analysis.beatsPerBar;
        for (const auto bar : track->analysis.drops)
        {
            const auto x = static_cast<float>((track->analysis.firstBeatSeconds + bar * barSeconds) / track->seconds() * area.getWidth());
            juce::Path marker;
            marker.addTriangle(x - 4.0f, 0.0f, x + 4.0f, 0.0f, x, 6.0f);
            g.setColour(palette::djDrop);
            g.fillPath(marker);
        }
    }
}

void DjDeckDisplay::paintOverview(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (track == nullptr)
    {
        g.setColour(palette::displayInset.brighter(0.04f));
        g.fillRect(area);
        return;
    }
    if (overview.isNull() || overviewWidth != area.getWidth())
        buildOverview();
    g.drawImageAt(overview, area.getX(), area.getY());
    const auto length = std::max(1.0e-6, state.lengthSeconds);
    const auto xFor = [&](double seconds) { return area.getX() + static_cast<float>(seconds / length * area.getWidth()); };
    // What has been played is dimmed, as a CDJ dims it.
    const auto played = xFor(state.positionSeconds);
    g.setColour(palette::displayInset.withAlpha(0.45f));
    g.fillRect(static_cast<float>(area.getX()), static_cast<float>(area.getY()), played - area.getX(), static_cast<float>(area.getHeight()));
    if (state.loop.valid())
    {
        g.setColour((state.loop.active ? palette::djCue : palette::displayTextDim).withAlpha(0.25f));
        g.fillRect(xFor(state.loop.startSeconds), static_cast<float>(area.getY()),
                   xFor(state.loop.endSeconds) - xFor(state.loop.startSeconds), static_cast<float>(area.getHeight()));
    }
    g.setColour(palette::djCue);
    g.fillRect(xFor(state.cueSeconds) - 0.5f, static_cast<float>(area.getBottom() - 5), 1.0f, 5.0f);
    for (int i = 0; i < 8; ++i)
    {
        const auto at = state.hotCueSeconds[static_cast<size_t>(i)];
        if (at < 0.0) continue;
        g.setColour(palette::djHotCue[static_cast<size_t>(i)]);
        g.fillRect(xFor(at) - 1.0f, static_cast<float>(area.getBottom() - 6), 3.0f, 6.0f);
    }
    g.setColour(palette::text);
    g.fillRect(played - 0.5f, static_cast<float>(area.getY()), 1.5f, static_cast<float>(area.getHeight()));
}

juce::String DjDeckDisplay::clock(double seconds)
{
    const auto whole = static_cast<int>(std::floor(std::abs(seconds)));
    const auto tenths = static_cast<int>(std::floor((std::abs(seconds) - whole) * 10.0));
    return (seconds < 0.0 ? "-" : "") + juce::String(whole / 60).paddedLeft('0', 2) + ":"
         + juce::String(whole % 60).paddedLeft('0', 2) + "." + juce::String(tenths);
}

void DjDeckDisplay::paintReadouts(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(palette::displayInset);
    g.fillRect(area);
    g.setColour(palette::displayTextFaint);
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1);
    if (track == nullptr) return;
    // The same fields tick() compares, drawn in their columns.
    drawnReadout = readoutText();
    juce::StringArray fields;
    fields.addTokens(drawnReadout, "|", "");
    if (fields.size() < 5) return;
    g.setFont(uiFont(10.0f));
    auto row = area.reduced(6, 0);
    g.setColour(palette::displayText);
    drawSnappedText(g, fields[0], row.removeFromLeft(120));
    g.setColour(palette::displayTextDim);
    drawSnappedText(g, fields[1], row.removeFromLeft(36));
    g.setColour(palette::displayText);
    drawSnappedText(g, fields[2], row.removeFromLeft(78));
    g.setColour(palette::displayTextDim);
    drawSnappedText(g, fields[3], row.removeFromLeft(54));
    if (track->analysis.keyIndex >= 0)
        drawSnappedText(g, DjAnalysis::keyName(track->analysis.keyIndex) + " " + DjAnalysis::camelotName(track->analysis.keyIndex),
                        row.removeFromLeft(52));
    g.setColour(palette::displayText);
    if (state.bpm > 0.0)
        drawSnappedText(g, fields[4], row, juce::Justification::centredRight);
}

void DjDeckDisplay::seekAt(juce::Point<int> point, juce::Rectangle<int> area)
{
    if (track == nullptr || area.getWidth() <= 0) return;
    const auto fraction = juce::jlimit(0.0, 1.0, static_cast<double>(point.x - area.getX()) / area.getWidth());
    session.djSeek(deck, fraction * state.lengthSeconds);
}

// The overview seeks on a click or a drag. The scrolling strip is the jog:
// dragging it pushes the tempo while the button is down, as a hand on the
// platter does.
void DjDeckDisplay::mouseDown(const juce::MouseEvent& event)
{
    if (overviewArea().contains(event.getPosition()))
    {
        seekAt(event.getPosition(), overviewArea());
        return;
    }
    if (waveArea().contains(event.getPosition()) && track != nullptr)
    {
        nudging = true;
        nudgeStart = event.position;
    }
}

void DjDeckDisplay::mouseDrag(const juce::MouseEvent& event)
{
    if (nudging)
    {
        const auto push = juce::jlimit(-8.0f, 8.0f, (event.position.x - nudgeStart.x) / 20.0f);
        session.djNudge(deck, push);
        return;
    }
    if (overviewArea().contains(event.getMouseDownPosition()))
        seekAt(event.getPosition(), overviewArea());
}

void DjDeckDisplay::mouseUp(const juce::MouseEvent&)
{
    if (nudging)
    {
        nudging = false;
        session.djNudge(deck, 0.0f);
    }
}

void DjDeckDisplay::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!waveArea().contains(event.getPosition())) return;
    const auto factor = wheel.deltaY > 0.0f ? 0.8 : 1.25;
    const auto next = juce::jlimit(minimumSecondsAcross, maximumSecondsAcross, secondsAcross * factor);
    if (next == secondsAcross) return;
    secondsAcross = next;
    tiles.clear();
    repaint(waveArea());
}
}
