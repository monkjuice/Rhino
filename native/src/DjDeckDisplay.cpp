#include "DjDeckDisplay.h"
#include "Theme.h"
#include "UiVisibility.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// A deck's screen: see DjDeckDisplay.h. What it shows changing, and the
// readouts, are here; its pictures and how they are painted are in
// DjDeckDisplayWave.cpp.

namespace rhino
{
namespace
{
constexpr double minimumSecondsAcross = 3.0, maximumSecondsAcross = 48.0;

// FNV-1a, a word at a time: what a picture was painted from, read once when
// the material arrives.
constexpr juce::uint64 fnvOffset = 14695981039346656037ull, fnvPrime = 1099511628211ull;

juce::uint64 mix(juce::uint64 hash, juce::uint64 word)
{
    return (hash ^ word) * fnvPrime;
}

juce::uint64 mix(juce::uint64 hash, double value)
{
    juce::uint64 word = 0;
    static_assert(sizeof(word) == sizeof(value));
    std::memcpy(&word, &value, sizeof(word));
    return mix(hash, word);
}

juce::uint64 mixColumns(juce::uint64 hash, const std::vector<DjWaveformColumn>& columns)
{
    hash = mix(hash, static_cast<juce::uint64>(columns.size()));
    for (const auto& column : columns)
    {
        juce::uint32 bits[3] {};
        static_assert(sizeof(bits) == sizeof(float) * 3);
        std::memcpy(&bits[0], &column.low, sizeof(float));
        std::memcpy(&bits[1], &column.mid, sizeof(float));
        std::memcpy(&bits[2], &column.high, sizeof(float));
        hash = mix(hash, (static_cast<juce::uint64>(bits[0]) << 32) | bits[1]);
        hash = mix(hash, static_cast<juce::uint64>(bits[2]));
    }
    return hash;
}

// Whole tenths of a second, as clock() shows them.
int tenthsOf(double seconds)
{
    const auto magnitude = std::abs(seconds);
    const auto whole = static_cast<int>(std::floor(magnitude));
    return whole * 10 + static_cast<int>(std::floor((magnitude - whole) * 10.0));
}

juce::String clockText(bool negative, int tenths)
{
    const auto whole = tenths / 10;
    return (negative ? "-" : "") + juce::String(whole / 60).paddedLeft('0', 2) + ":"
         + juce::String(whole % 60).paddedLeft('0', 2) + "." + juce::String(tenths % 10);
}

juce::String hundredthsText(long long hundredths)
{
    const auto magnitude = std::abs(hundredths);
    return juce::String(static_cast<juce::int64>(magnitude / 100)) + "."
         + juce::String(static_cast<int>(magnitude % 100)).paddedLeft('0', 2);
}

juce::String transportText(Session::DjDeckState::Transport transport)
{
    using Transport = Session::DjDeckState::Transport;
    return transport == Transport::waiting ? "WAIT"
         : transport == Transport::cueing ? "CUE"
         : transport == Transport::playing ? "PLAY" : "STOP";
}
}

DjDeckDisplay::DjDeckDisplay(Session& s, int deckIndex) : session(s), deck(deckIndex)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    // The cue flag's letter and the eight hot cues', each centred in the
    // flag's 12 x 11 box at the origin and drawn wherever a flag stands.
    const auto letterFont = uiFontBold(8.0f);
    for (size_t i = 0; i < flagLetters.size(); ++i)
        flagLetters[i].set(i == 0 ? juce::String("C") : juce::String::charToString(static_cast<juce::juce_wchar>('A' + i - 1)),
                           letterFont, {0, 0, 12, 11}, juce::Justification::centred);
    sync();
}

void DjDeckDisplay::sync()
{
    sync(session.djDeckInfo(deck));
}

void DjDeckDisplay::sync(const Session::DjDeckInfo& info)
{
    // While a load is in flight the deck goes on playing what it had, and
    // the screen goes on showing it, pictures and all: a cell switch is not
    // a blank screen. A load starting bumps the generation and names the
    // source on its way while the deck still holds the same material, so
    // only material that arrives is read again.
    const auto* next = info.loaded || info.loading ? session.djDeckTrack(deck) : nullptr;
    const auto loadStarting = next == track && info.loading;
    if (next != track || (info.generation != generation && !loadStarting))
    {
        // New material from the same source - a clip made again - may show
        // the last material's pictures until its own are painted; another
        // source's material never shows.
        // Material first seen while a load is in flight came from a source
        // the deck's info no longer names.
        std::optional<Source> source;
        if (!info.loading)
            source = Source {info.kind, info.file, info.trackId, info.slot};
        if (!source || source != materialSource)
            dropPictures();
        materialSource = source;
        track = next;
        readMaterial();
        repaint();
    }
    generation = info.generation;
    const auto nextMessage = next != nullptr ? juce::String()
                           : info.loading ? "Loading " + info.name + "..."
                           : info.error.isNotEmpty() ? info.error
                           : juce::String("No track. Choose one from the name above, or drop a file here.");
    if (nextMessage != message)
    {
        message = nextMessage;
        repaint(waveArea());
    }
    // A change the session announces - sync pressed on a standing deck, a
    // range that clamps the pitch - may change only the readouts, and no
    // later frame would find them changed.
    state = session.djDeckState(deck);
    if (const auto readout = readoutOf(state); !(readout == shownReadout))
    {
        shownReadout = readout;
        repaint(readoutArea());
    }
}

// What the pictures are painted from, read once for the material: hashed,
// so a bounce of the same notes keeps its note tiles and one of the same
// sound its overview, and with the notes paired once rather than per tile.
void DjDeckDisplay::readMaterial()
{
    drawsNotes = track != nullptr && !track->midi.empty() && track->analysis.hasGrid();
    notes.clear();
    lowestNote = 0;
    highestNote = -1;
    keyText = {};
    wavesContent = overviewContent = notesContent = 0;
    if (track == nullptr) return;
    const auto& analysis = track->analysis;
    auto grid = mix(mix(mix(fnvOffset, analysis.bpm), analysis.firstBeatSeconds), static_cast<juce::uint64>(analysis.beatsPerBar));
    wavesContent = mixColumns(mix(grid, track->sampleRate), analysis.columns);
    overviewContent = mix(wavesContent, static_cast<juce::uint64>(track->length()));
    for (const auto bar : analysis.drops)
        overviewContent = mix(overviewContent, static_cast<juce::uint64>(bar));
    if (drawsNotes)
    {
        notes = djwave::noteSpans(track->midi);
        notesContent = mix(grid, static_cast<juce::uint64>(analysis.columns.empty() ? 0 : 1));
        lowestNote = 127;
        for (const auto& note : notes)
        {
            lowestNote = std::min(lowestNote, note.note);
            highestNote = std::max(highestNote, note.note);
            notesContent = mix(mix(mix(notesContent, note.startBeat), note.endBeat),
                               (static_cast<juce::uint64>(note.note) << 32) | static_cast<juce::uint32>(note.velocity));
        }
    }
    if (analysis.keyIndex >= 0)
        keyText = DjAnalysis::keyName(analysis.keyIndex) + " " + DjAnalysis::camelotName(analysis.keyIndex);
}

void DjDeckDisplay::dropPictures()
{
    tiles = {};
    standIns = {};
    overview = {};
    overviewStandIn = {};
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

long long DjDeckDisplay::scrollFor(double seconds) const
{
    return djwave::scrollFor(seconds, pixelsPerSecond(), paintedScale);
}

DjDeckDisplay::PictureKey DjDeckDisplay::tileKey() const
{
    const auto height = static_cast<int>(std::ceil(static_cast<float>(waveArea().getHeight()) * paintedScale));
    return {drawsNotes ? notesContent : wavesContent, pixelsPerSecond(), tileWidth, std::max(1, height), paintedScale};
}

DjDeckDisplay::PictureKey DjDeckDisplay::overviewKey() const
{
    const auto area = overviewArea().toFloat() * paintedScale;
    return {overviewContent, 0.0, std::max(1, static_cast<int>(std::ceil(area.getWidth()))),
            std::max(1, static_cast<int>(std::ceil(area.getHeight()))), paintedScale};
}

DjDeckDisplay::Marks DjDeckDisplay::marksOf(const Session::DjDeckState& deckState)
{
    Marks marks;
    marks.cue = deckState.cueSeconds;
    marks.hotCues = deckState.hotCueSeconds;
    marks.loopStart = deckState.loop.startSeconds;
    marks.loopEnd = deckState.loop.endSeconds;
    marks.loopActive = deckState.loop.active;
    return marks;
}

DjDeckDisplay::Readout DjDeckDisplay::readoutOf(const Session::DjDeckState& deckState)
{
    Readout readout;
    readout.elapsedNegative = deckState.positionSeconds < 0.0;
    readout.elapsedTenths = tenthsOf(deckState.positionSeconds);
    const auto remaining = deckState.lengthSeconds - deckState.positionSeconds;
    readout.remainingNegative = -remaining < 0.0;
    readout.remainingTenths = tenthsOf(remaining);
    readout.transport = deckState.transport;
    readout.bpmHundredths = deckState.bpm > 0.0 ? std::llround(deckState.effectiveBpm * 100.0) : -1;
    readout.synced = deckState.synced;
    readout.percentHundredths = deckState.synced ? 0 : std::llround(deckState.tempoPercent * 100.0);
    readout.bar = deckState.beat >= 0.0 ? static_cast<int>(std::floor(deckState.beat / deckState.beatsPerBar)) + 1 : 0;
    readout.beatInBar = deckState.beat >= 0.0 ? static_cast<int>(std::floor(deckState.beat)) % deckState.beatsPerBar + 1 : 0;
    return readout;
}

long long DjDeckDisplay::overviewMarker(double seconds) const
{
    return std::llround(overviewArea().getWidth() * paintedScale * std::max(0.0, seconds) / std::max(1.0e-6, state.lengthSeconds));
}

void DjDeckDisplay::tick()
{
    tick(session.djDeckState(deck));
}

// Once a frame. The strip repaints when it has moved a device pixel, when
// the deck starts or stops, or when a mark is set or cleared - which on a
// stopped deck moves nothing else, so it was never shown until the deck
// moved; the overview's marker repaints a pixel's width at a time; the
// readouts when what they show changes. Then a picture or two the screen
// lacks is painted.
void DjDeckDisplay::tick(const Session::DjDeckState& now)
{
    if (isHiddenInShell(*this)) return;
    state = now;
    if (track == nullptr)
        return;
    const auto scroll = scrollFor(state.positionSeconds);
    const auto playing = state.isPlaying();
    const auto marks = marksOf(state);
    const auto marksMoved = !(marks == drawnMarks);
    if (scroll != drawnScroll || playing != drawnPlaying || marksMoved)
        repaint(waveArea());
    const auto area = overviewArea();
    const auto marker = overviewMarker(state.positionSeconds);
    if (marksMoved || drawnOverviewMarker < 0)
        repaint(area);
    else if (marker != drawnOverviewMarker)
    {
        const auto from = static_cast<float>(area.getX()) + static_cast<float>(std::min(marker, drawnOverviewMarker)) / paintedScale;
        const auto to = static_cast<float>(area.getX()) + static_cast<float>(std::max(marker, drawnOverviewMarker)) / paintedScale;
        repaint(juce::Rectangle<float>(from - 2.0f, static_cast<float>(area.getY()), to - from + 5.0f,
                                       static_cast<float>(area.getHeight())).getSmallestIntegerContainer());
    }
    drawnScroll = scroll;
    drawnPlaying = playing;
    drawnMarks = marks;
    drawnOverviewMarker = marker;
    if (const auto readout = readoutOf(state); !(readout == shownReadout))
    {
        shownReadout = readout;
        repaint(readoutArea());
    }
    paintPictures(picturesPerFrame);
}

void DjDeckDisplay::paint(juce::Graphics& g)
{
    // The pictures are painted for the scale the screen is shown at.
    paintedScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    // At a fractional scale the strip, the overview and the readouts meet
    // inside a device pixel that none of them covers whole, so the ground
    // goes down first, or what the last frame left there would show through.
    g.fillAll(palette::displayInset);
    if (g.clipRegionIntersects(waveArea())) paintWave(g, waveArea());
    if (g.clipRegionIntersects(overviewArea())) paintOverview(g, overviewArea());
    if (g.clipRegionIntersects(readoutArea())) paintReadouts(g, readoutArea());
}

// The numbers tick() compared, made into text: laid out again only when it
// changes, a few times a second at most.
void DjDeckDisplay::paintReadouts(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(palette::displayInset);
    g.fillRect(area);
    g.setColour(palette::displayTextFaint);
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1);
    if (track == nullptr) return;
    const auto& shown = shownReadout;
    const auto hasTempo = shown.bpmHundredths >= 0;
    auto row = area.reduced(6, 0);
    clockLine.set(clockText(shown.elapsedNegative, shown.elapsedTenths) + "  "
                      + clockText(shown.remainingNegative, shown.remainingTenths),
                  readoutFont, row.removeFromLeft(120));
    transportLine.set(transportText(shown.transport), readoutFont, row.removeFromLeft(36));
    tempoLine.set(hasTempo ? hundredthsText(shown.bpmHundredths) + " BPM" : juce::String("-- BPM"), readoutFont, row.removeFromLeft(78));
    pitchLine.set(shown.synced ? juce::String("SYNC")
                               : (shown.percentHundredths < 0 ? "-" : "+") + hundredthsText(shown.percentHundredths) + "%",
                  readoutFont, row.removeFromLeft(54));
    keyLine.set(keyText, readoutFont, keyText.isNotEmpty() ? row.removeFromLeft(52) : juce::Rectangle<int>());
    barLine.set(hasTempo ? juce::String(shown.bar) + "." + juce::String(shown.beatInBar) : juce::String(), readoutFont, row,
                juce::Justification::centredRight);
    g.setColour(palette::displayText);
    clockLine.draw(g);
    tempoLine.draw(g);
    barLine.draw(g);
    g.setColour(palette::displayTextDim);
    transportLine.draw(g);
    pitchLine.draw(g);
    keyLine.draw(g);
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

// The wheel zooms. The tiles of the last zoom stand in, scaled, while the
// display's refresh paints the new zoom's a couple at a time.
void DjDeckDisplay::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!waveArea().contains(event.getPosition())) return;
    const auto factor = wheel.deltaY > 0.0f ? 0.8 : 1.25;
    const auto next = juce::jlimit(minimumSecondsAcross, maximumSecondsAcross, secondsAcross * factor);
    if (next == secondsAcross) return;
    secondsAcross = next;
    drawnScroll = scrollFor(state.positionSeconds);
    repaint(waveArea());
}
}
