#include "DrumRackDevice.h"
#include "DrumRackDeviceInternal.h"
#include "DrumSlicer.h"
#include <algorithm>
#include <cmath>
#include <functional>

// What the Drum Rack's face asks of the device: the pad and the bank it
// shows, strikes from the face and the counts that light pads and keys, the
// pictures it draws, and slicing a sample across pads.
namespace rhino
{
using namespace drumrack;

namespace
{
// The longest stretch of a strike a picture shows. A crash rings on for
// longer, and the start of it is what tells one sample from another.
constexpr double pictureSeconds = 3.0;
// The rate a picture is played at. It is drawn as the lows and highs of a few
// hundred columns, each tens of samples wide even at this rate, and a dragged
// Decay asks for a new one every frame: at the device's own rate a long tom
// took a millisecond and a half.
constexpr double pictureRate = 16000.0;
// A slice spread to a pad of its own ends in a fade this long, so the cut into
// the next hit does not click.
constexpr float sliceFadeOut = 0.004f;

// The lows and highs of a run of frames, the louder side for each.
void drawInto(DrumRackDevice::Picture& picture, int columns, int frames,
              const std::function<float(int)>& left, const std::function<float(int)>& right)
{
    picture.lows.assign(static_cast<size_t>(columns), 0.0f);
    picture.highs.assign(static_cast<size_t>(columns), 0.0f);
    picture.peak = 0.0f;
    if (frames <= 0)
        return;
    for (int column = 0; column < columns; ++column)
    {
        const auto from = static_cast<int>(static_cast<juce::int64>(frames) * column / columns);
        const auto to = std::max(from + 1, static_cast<int>(static_cast<juce::int64>(frames) * (column + 1) / columns));
        auto low = 0.0f, high = 0.0f;
        for (int i = from; i < to && i < frames; ++i)
        {
            // The louder side, so a pad panned hard over still shows its sound.
            const auto l = left(i), r = right(i);
            const auto value = std::abs(l) >= std::abs(r) ? l : r;
            low = std::min(low, value);
            high = std::max(high, value);
        }
        picture.lows[static_cast<size_t>(column)] = low;
        picture.highs[static_cast<size_t>(column)] = high;
        picture.peak = std::max({ picture.peak, -low, high });
    }
}
}

// ---- the view -------------------------------------------------------------------

int DrumRackDevice::selectedPad() const
{
    return juce::jlimit(0, padCount - 1,
                        static_cast<int>(state.getProperty(selectedId, DrumRackEngine::defaultFirstNote)));
}

void DrumRackDevice::setSelectedPad(int note)
{
    state.setProperty(selectedId, juce::jlimit(0, padCount - 1, note), nullptr);
}

int DrumRackDevice::firstShownNote() const
{
    const auto note = static_cast<int>(state.getProperty(firstShownId, DrumRackEngine::defaultFirstNote));
    return juce::jlimit(0, lastFirstNote, note - note % rowSize);
}

void DrumRackDevice::showNotesFrom(int note)
{
    note = juce::jlimit(0, lastFirstNote, note);
    state.setProperty(firstShownId, note - note % rowSize, nullptr);
}

bool DrumRackDevice::autoSelect() const
{
    return static_cast<bool>(state.getProperty(autoSelectId, true));
}

void DrumRackDevice::setAutoSelect(bool on)
{
    state.setProperty(autoSelectId, on, nullptr);
}

// ---- strikes and counts ------------------------------------------------------

void DrumRackDevice::previewPad(int note)
{
    engine.previewPad(note);
}

void DrumRackDevice::previewPart(int note, float start, float end)
{
    engine.previewPart(note, start, end);
}

std::uint32_t DrumRackDevice::padStrikes(int note) const
{
    return engine.strikes(note);
}

std::uint32_t DrumRackDevice::notesReceived(int note) const
{
    return engine.notesReceived(note);
}

float DrumRackDevice::padPlayhead(int note) const
{
    return engine.playhead(note);
}

void DrumRackDevice::collectSamples()
{
    engine.collect();
}

// ---- pictures ---------------------------------------------------------------------

DrumRackDevice::CachedPicture& DrumRackDevice::keptPicture(KeptPictures& kept, int note, int columns)
{
    for (auto& each : kept)
        if (each.note == note && each.columns == columns)
        {
            each.asked = ++picturesAsked;
            return each;
        }
    auto& older = kept[0].asked <= kept[1].asked ? kept[0] : kept[1];
    older.asked = ++picturesAsked;
    return older;
}

const DrumRackDevice::Picture& DrumRackDevice::padPicture(int note, int columns)
{
    static const Picture none;
    if (!validPad(note) || columns <= 0)
        return none;
    const auto controls = controlsOf(note, false);
    const auto& sound = loaded[static_cast<size_t>(note)];
    const auto playback = engine.padPlayback(note);
    // The playback is part of what is drawn, so it is part of the key.
    const auto key = sound + "|" + juce::String(static_cast<int>(playback.mode)) + "|" + juce::String(playback.start)
                   + "|" + juce::String(playback.end) + "|" + juce::String(playback.fadeIn) + "|"
                   + juce::String(playback.fadeOut) + "|" + juce::String(playback.attack) + "|"
                   + juce::String(playback.sustain) + "|" + juce::String(playback.release) + "|"
                   + (playback.loop ? "loop" : "");
    auto& kept = keptPicture(pictures, note, columns);
    if (kept.note == note && kept.columns == columns && kept.sound == key && kept.settings == controls)
        return kept.picture;
    kept.note = note;
    kept.sound = key;
    kept.settings = controls;
    kept.columns = columns;
    auto& drawn = kept.picture;
    drawn = {};

    const auto most = static_cast<int>(pictureRate * pictureSeconds);
    pictureLeft.resize(static_cast<size_t>(most));
    pictureRight.resize(static_cast<size_t>(most));
    const auto frames = DrumRackEngine::renderStrike(engine.padSource(note), engine.padModel(note), engine.padSample(note),
                                                     controls, playback, 1.0f, pictureRate, pictureLeft.data(),
                                                     pictureRight.data(), most);
    drawn.seconds = frames / pictureRate;
    drawInto(drawn, columns, frames, [this] (int i) { return pictureLeft[static_cast<size_t>(i)]; },
             [this] (int i) { return pictureRight[static_cast<size_t>(i)]; });
    return drawn;
}

const DrumRackDevice::Picture& DrumRackDevice::samplePicture(int note, int columns)
{
    static const Picture none;
    const auto* sample = validPad(note) ? engine.padSample(note) : nullptr;
    if (sample == nullptr || columns <= 0)
        return none;
    // The file is the same until the pad takes another, so its picture is
    // kept by what the pad was given and how wide it is drawn.
    const auto& sound = loaded[static_cast<size_t>(note)];
    auto& kept = keptPicture(sampleWaves, note, columns);
    if (kept.note == note && kept.columns == columns && kept.sound == sound)
        return kept.picture;
    kept.note = note;
    kept.sound = sound;
    kept.columns = columns;
    auto& drawn = kept.picture;
    drawn = {};
    drawn.seconds = sample->seconds();
    drawInto(drawn, columns, sample->length(), [sample] (int i) { return sample->left[static_cast<size_t>(i)]; },
             [sample] (int i) { return sample->stereo() ? sample->right[static_cast<size_t>(i)]
                                                        : sample->left[static_cast<size_t>(i)]; });
    return drawn;
}

// ---- slices ------------------------------------------------------------------------

const std::vector<float>& DrumRackDevice::padSlices(int note)
{
    static const std::vector<float> none;
    const auto* sample = validPad(note) ? engine.padSample(note) : nullptr;
    const auto view = pad(note);
    if (sample == nullptr || !view.sound.has_value() || view.sound->source != DrumRackEngine::Source::sample)
        return none;
    // The engine's copy of a pad's playback leaves out how it is cut, so the
    // pad's own is read. Only what decides the cuts is compared -- the file,
    // the part and how it is cut -- so a fade turned does not cut again.
    const auto& cut = view.sound->playback;
    const auto& sound = loaded[static_cast<size_t>(note)];
    const auto& kept = slices.playback;
    if (slices.note == note && slices.sound == sound && kept.start == cut.start && kept.end == cut.end
        && kept.sliceBy == cut.sliceBy && kept.divisions == cut.divisions && kept.sensitivity == cut.sensitivity)
        return slices.starts;
    slices.note = note;
    slices.sound = sound;
    slices.playback = cut;
    slices.starts = DrumSlicer::slices(*sample, cut);
    return slices.starts;
}

int DrumRackDevice::spreadSlices(int note)
{
    const auto view = pad(note);
    if (!view.sound.has_value() || view.sound->source != DrumRackEngine::Source::sample || view.unreadable)
        return 0;
    const auto starts = padSlices(note);
    if (starts.empty())
        return 0;
    const auto original = *view.sound;
    const auto count = std::min(static_cast<int>(starts.size()), padCount - note);
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        for (int index = 0; index < count; ++index)
        {
            auto slice = original;
            slice.name = original.displayName() + " " + juce::String(index + 1);
            slice.playback = {};
            slice.playback.start = starts[static_cast<size_t>(index)];
            slice.playback.end = index + 1 < static_cast<int>(starts.size()) ? starts[static_cast<size_t>(index + 1)]
                                                                             : original.playback.end;
            slice.playback.fadeOut = sliceFadeOut;
            writePad(note + index, slice);
            if (auto tree = padTree(note + index); tree.isValid())
            {
                tree.removeProperty(muteId, getUndoManager());
                tree.removeProperty(soloId, getUndoManager());
            }
        }
    }
    syncPads();
    return count;
}
}
