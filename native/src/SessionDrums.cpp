#include "SessionInternal.h"
#include "ContentLibrary.h"
#include "DrumKitFile.h"

// The Drum Rack through the session: kits and drum presets to and from disk,
// sounds dropped on pads and on tracks, auditioning a drum preset, and the kit
// a drum pattern brings to a blank rack. Each edit is one undo step and is
// announced like any other. What a pad keeps when its sound changes is the
// device's rule (DrumRackDevice), so every path that fills a pad inherits it.

namespace rhino
{
namespace
{
// The kit the drum patterns were written for: the kick on C2, the toms on D2,
// E2 and F#2, the snare on F2, the clap on G#2 and the hats on A#2 and B2.
constexpr const char* defaultKitPath = "Drums/Kits/808 Kit.rdk";
// A drum preset is auditioned by rendering one strike into memory.
constexpr double previewRate = 48000.0;
constexpr double previewSeconds = 4.0;

DrumRackDevice* rackIn(const Session& session, int track, int slot)
{
    return dynamic_cast<DrumRackDevice*>(session.devicePlugin(track, slot));
}

juce::String padNumber(int pad)
{
    return "pad " + juce::String(pad + 1);
}

// Whether a file is a sound a pad can play, asked before anything changes so
// that a file that will not play leaves the pad as it was. Opening a reader is
// enough to know; the pad decodes the file when it takes it.
juce::Result checkSampleFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return juce::Result::fail(file.getFileName() + " is missing.");
    if (!file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3"))
        return juce::Result::fail(file.getFileName() + " is not a sound a pad can play.");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return juce::Result::fail("Rhino cannot read " + file.getFileName() + ".");
    return juce::Result::ok();
}
}

void loadDefaultDrumKit(DrumRackDevice& drums)
{
    DrumKit kit;
    if (const auto read = DrumFiles::read(ContentLibrary::file(defaultKitPath), kit); read.failed())
    {
        juce::Logger::writeToLog("Rhino: the drum patterns' kit could not be loaded: " + read.getErrorMessage());
        return;
    }
    drums.setKit(kit);
}

std::optional<DrumKit> drumKitOf(te::AudioTrack& track)
{
    if (const auto* drums = findDrumRack(track))
        return drums->kit();
    return std::nullopt;
}

void fillBlankDrumRack(te::AudioTrack& track, const DrumKit& kit)
{
    if (auto* drums = findDrumRack(track); drums != nullptr && drums->isBlank())
        drums->setKit(kit);
}

bool Session::trackHasDrumRack(int trackIndex) const
{
    const auto tracks = te::getAudioTracks(*edit);
    return juce::isPositiveAndBelow(trackIndex, tracks.size()) && findDrumRack(*tracks[trackIndex]) != nullptr;
}

juce::Result Session::addDrumKit(const juce::File& file, int trackIndex)
{
    DrumKit kit;
    if (const auto read = DrumFiles::read(file, kit); read.failed())
        return read;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop drum kits on a track.");
    if (isGroupBusTrack(trackIndex))
        return juce::Result::fail("A group track takes audio effects only.");
    // A kit is an instrument, so it goes where an instrument goes.
    if (trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Drop drum kits on a MIDI track instead.");
    const auto* rack = DeviceCatalog::byId("Drums");
    if (rack == nullptr)
        return juce::Result::fail("The Drum Rack is not in this build.");

    auto* track = tracks[trackIndex];
    const auto hadRack = findDrumRack(*track) != nullptr;
    const auto name = DrumFiles::nameOf(file);
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto changed = false;
    if (const auto switched = switchTrackInstrument(*edit, *track, *rack, changed); switched.failed())
        return switched;
    auto* drums = findDrumRack(*track);
    if (drums == nullptr)
        return juce::Result::fail("The Drum Rack could not be created.");
    drums->setKit(kit);
    // The track is named after its instrument, and the kit is what the
    // instrument now is.
    track->setName(name);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    // A new rack is a new node in the graph. A kit loaded into the rack
    // already there is not, so playback goes on undisturbed.
    if (!hadRack && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::loadDrumKit(int track, int slot, const juce::File& file)
{
    DrumKit kit;
    if (const auto read = DrumFiles::read(file, kit); read.failed())
        return read;
    auto* drums = rackIn(*this, track, slot);
    if (drums == nullptr)
        return juce::Result::fail("There is no Drum Rack there to load " + DrumFiles::nameOf(file) + " into.");
    edit->getUndoManager().beginNewTransaction("Load " + DrumFiles::nameOf(file));
    drums->setKit(kit);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::saveDrumKit(int track, int slot, const juce::File& file)
{
    const auto* drums = rackIn(*this, track, slot);
    if (drums == nullptr)
        return juce::Result::fail("There is no Drum Rack there to save.");
    return DrumFiles::write(drums->kit(), file);
}

juce::Result Session::loadDrumPadSample(int track, int slot, int pad, const juce::File& sample)
{
    auto* drums = rackIn(*this, track, slot);
    if (drums == nullptr)
        return juce::Result::fail("Drop samples on a pad of a Drum Rack.");
    if (!juce::isPositiveAndBelow(pad, DrumRackDevice::padCount))
        return juce::Result::fail("A Drum Rack has pads 1 to " + juce::String(DrumRackDevice::padCount) + ".");
    if (const auto playable = checkSampleFile(sample); playable.failed())
        return playable;
    edit->getUndoManager().beginNewTransaction("Load " + sample.getFileNameWithoutExtension() + " on " + padNumber(pad));
    drums->setPadSample(pad, sample);
    // The pad just filled is the one worth looking at.
    drums->setSelectedPad(pad);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::loadDrumPadPreset(int track, int slot, int pad, const juce::File& file)
{
    DrumSound sound;
    if (const auto read = DrumFiles::read(file, sound); read.failed())
        return read;
    auto* drums = rackIn(*this, track, slot);
    if (drums == nullptr)
        return juce::Result::fail("Drop drum presets on a pad of a Drum Rack.");
    if (!juce::isPositiveAndBelow(pad, DrumRackDevice::padCount))
        return juce::Result::fail("A Drum Rack has pads 1 to " + juce::String(DrumRackDevice::padCount) + ".");
    // A preset that names nothing takes its own file's name, which is what
    // the browser called it.
    if (sound.name.isEmpty())
        sound.name = DrumFiles::nameOf(file);
    edit->getUndoManager().beginNewTransaction("Load " + sound.name + " on " + padNumber(pad));
    drums->setPadSound(pad, sound);
    drums->setSelectedPad(pad);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::saveDrumPadPreset(int track, int slot, int pad, const juce::File& file)
{
    const auto* drums = rackIn(*this, track, slot);
    if (drums == nullptr)
        return juce::Result::fail("There is no Drum Rack there to save a pad from.");
    if (!juce::isPositiveAndBelow(pad, DrumRackDevice::padCount))
        return juce::Result::fail("A Drum Rack has pads 1 to " + juce::String(DrumRackDevice::padCount) + ".");
    const auto view = drums->pad(pad);
    if (!view.sound.has_value())
        return juce::Result::fail("Pad " + juce::String(pad + 1) + " is empty, so there is no sound on it to save.");
    return DrumFiles::write(*view.sound, file);
}

juce::Result Session::addDrumSound(const juce::File& file, int trackIndex)
{
    std::optional<DrumSound> preset;
    if (file.hasFileExtension(DrumFiles::soundExtension))
    {
        DrumSound sound;
        if (const auto read = DrumFiles::read(file, sound); read.failed())
            return read;
        if (sound.name.isEmpty())
            sound.name = DrumFiles::nameOf(file);
        preset = std::move(sound);
    }
    else if (const auto playable = checkSampleFile(file); playable.failed())
    {
        return playable;
    }

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop drum sounds on a track.");
    if (isGroupBusTrack(trackIndex))
        return juce::Result::fail("A group track takes audio effects only.");
    if (trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Drop drum sounds on a MIDI track instead.");
    auto* track = tracks[trackIndex];
    auto* drums = findDrumRack(*track);
    // A drum preset is an instrument's worth of sound, and brings the rack it
    // plays in. A bare sample does not replace what a track plays.
    if (drums == nullptr && !preset.has_value())
        return juce::Result::fail("Drop samples on a pad of a Drum Rack, or on an audio track.");
    if (drums != nullptr && drums->firstEmptyPad() < 0)
        return juce::Result::fail("Every pad of this Drum Rack holds a sound. Drop it on the pad it should replace.");
    const auto* rack = DeviceCatalog::byId("Drums");
    if (rack == nullptr)
        return juce::Result::fail("The Drum Rack is not in this build.");

    const auto name = preset.has_value() ? preset->displayName() : file.getFileNameWithoutExtension();
    edit->getUndoManager().beginNewTransaction("Add " + name);
    const auto added = drums == nullptr;
    if (added)
    {
        auto changed = false;
        if (const auto switched = switchTrackInstrument(*edit, *track, *rack, changed); switched.failed())
            return switched;
        drums = findDrumRack(*track);
        if (drums == nullptr)
            return juce::Result::fail("The Drum Rack could not be created.");
    }
    const auto pad = drums->firstEmptyPad();
    if (preset.has_value())
        drums->setPadSound(pad, *preset);
    else
        drums->setPadSample(pad, file);
    drums->setSelectedPad(pad);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (added && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// A drum preset is heard as a pad would play it, Tune, Decay and Tone and all,
// rather than as the bare file underneath: one strike, rendered into memory
// and played through the preview like any library sound.
juce::Result Session::previewDrumSound(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!browserPreview)
        return juce::Result::ok();
    DrumSound sound;
    if (const auto read = DrumFiles::read(file, sound); read.failed())
        return read;
    ensurePreviewAttached();
    std::unique_ptr<DrumSample> sample;
    if (sound.source == DrumRackEngine::Source::sample)
    {
        sample = DrumRackDevice::readSample(ContentLibrary::resolveStoredPath(sound.sample), previewFormats);
        if (sample == nullptr)
            return juce::Result::fail(DrumFiles::nameOf(file) + " plays a sample that is missing or unreadable.");
    }
    stopPreview();
    const auto most = static_cast<int>(previewRate * previewSeconds);
    juce::AudioBuffer<float> strike(2, most);
    const auto frames = DrumRackEngine::renderStrike(sound.source, sound.model, sample.get(), sound.settings, 1.0f,
                                                     previewRate, strike.getWritePointer(0), strike.getWritePointer(1),
                                                     most);
    strike.setSize(2, std::max(1, frames), true);
    startPreview(std::make_unique<juce::MemoryAudioSource>(strike, true, false), previewRate);
    return juce::Result::ok();
}
}
