#include "SessionInternal.h"
#include "dsp/DrumSlicer.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Slice to New MIDI Track, as Live has it (Live 12 manual, 13.1, pp. 335-337):
// an audio clip is cut into slices, each slice goes on a pad of a Drum Rack on
// a new MIDI track under the clip's own, and a MIDI clip strikes the pads in
// order, a note a slice, so the new track plays what the clip did and its notes
// can be moved, repeated and resequenced.
//
// What is sliced is the clip as it plays, rendered the way a merge renders it
// (SessionMerge.cpp): its own gain, pan, pitch, warp, fades and reverse are in
// the slices, and its track's devices and fader are not. Nor are its clip
// effects or its envelopes, which the engine plays on the clip's plugin list
// and gates with the track's. Slicing the source file instead would be right
// only for a clip that plays its file untouched, and the notes would be placed
// against audio the clip never plays.
//
// Each slice is a file of its own, so a pad reads only its slice. A pad reads
// the whole of its file, up to DrumRackEngine::longestSampleSeconds, so pads
// sharing one render would each hold all of it, and a clip longer than the
// ceiling would lose its end. A slice longer than the ceiling is cut into equal
// parts under it, so nothing the clip plays is lost. The slices carry no fades:
// struck in order they put the render back sample for sample, and a fade at
// each cut would dip the sound there; the sample editor fades a pad that is
// played out of order.
//
// The slices land on C1 up, Live's first slice note, which leaves 92 pads; a
// cut that would make more is refused. The render runs on the message thread
// to its end, as a merge's does, and the files outlive an undo. The source
// clip is muted in the same undo step, as a bounce mutes its source, so the
// song does not play it twice.

namespace rhino
{
namespace
{
// A render that has not moved for this long is given up, as a bounce's is.
constexpr double sliceStallSeconds = 15.0;
constexpr int sliceNoteCount = DrumRackDevice::padCount - Session::firstSliceNote;

// The clip alone, before its track's devices, at the device's rate.
juce::Result renderClip(te::Edit& edit, te::AudioTrack& track, te::WaveAudioClip& clip, const juce::File& file,
                        double sampleRate, int blockSize)
{
    // The render drives the edit the device callback is holding, so the edit
    // comes off the device first and is reattached when this goes.
    const te::Edit::ScopedRenderStatus renderStatus(edit, true);
    te::TransportControl::stopAllTransports(edit.engine, false, true);
    te::Renderer::turnOffAllPlugins(edit);
    // A clip with a Pitch envelope keeps its fades with Clip Envelope, which
    // this render leaves out, so the engine plays them for it.
    lendHeldFadesToEngine(clip, true);
    auto outcome = juce::Result::ok();
    {
        // One track and one clip: solo elsewhere would silence the track, and
        // a clip in one of its cells would play into the render.
        te::Track::Array isolated;
        isolated.add(&track);
        const te::FreezePointPlugin::ScopedTrackSoloIsolator isolator(edit, isolated);
        const te::Renderer::ScopedClipSlotDisabler slotDisabler(edit, isolated);
        // A track in a group feeds its bus, and rendered on its own like that
        // it renders nothing at all.
        const ScopedStraightOut straightOut(track);
        juce::WavAudioFormat wav;
        te::Renderer::Parameters parameters(edit);
        parameters.destFile = file;
        parameters.audioFormat = &wav;
        // Float, so a clip driven past full scale reaches the pads as it is.
        parameters.bitDepth = 32;
        parameters.sampleRateForAudio = sampleRate;
        parameters.blockSizeForAudio = blockSize;
        parameters.time = clip.getPosition().time;
        parameters.tracksToDo.setBit(te::getAllTracks(edit).indexOf(static_cast<te::Track*>(&track)));
        juce::Array<te::Clip*> allowed;
        allowed.add(&clip);
        parameters.allowedClips = allowed;
        parameters.usePlugins = false;
        parameters.useMasterPlugins = false;
        parameters.canRenderInMono = false;
        // Declared before the task, which updates it until it goes.
        std::atomic<float> progress {0.0f};
        te::Renderer::RenderTask task("Slice to new MIDI track", parameters, &progress, nullptr);
        outcome = runRenderTask(task, progress, sliceStallSeconds);
    }
    lendHeldFadesToEngine(clip, false);
    te::Renderer::turnOffAllPlugins(edit);
    if (outcome.wasOk() && !file.existsAsFile())
        return juce::Result::fail("The clip could not be rendered.");
    return outcome;
}

juce::Result readRender(const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return juce::Result::fail("The rendered clip could not be read back.");
    if (reader->lengthInSamples > std::numeric_limits<int>::max())
        return juce::Result::fail("The clip is too long to slice.");
    const auto frames = static_cast<int>(reader->lengthInSamples);
    audio.setSize(static_cast<int>(juce::jlimit(1u, 2u, reader->numChannels)), frames);
    if (!reader->read(&audio, 0, frames, 0, true, true))
        return juce::Result::fail("The rendered clip could not be read back.");
    sampleRate = reader->sampleRate;
    return juce::Result::ok();
}

// Where the transients are, by the cut the Drum Rack's own Slice makes.
std::vector<int> transientCuts(const juce::AudioBuffer<float>& audio, double sampleRate)
{
    DrumSample sample;
    sample.sampleRate = sampleRate;
    sample.left.assign(audio.getReadPointer(0), audio.getReadPointer(0) + audio.getNumSamples());
    if (audio.getNumChannels() > 1)
        sample.right.assign(audio.getReadPointer(1), audio.getReadPointer(1) + audio.getNumSamples());
    std::vector<int> cuts;
    for (const auto start : DrumSlicer::slices(sample, DrumRackEngine::Playback {}))
        cuts.push_back(static_cast<int>(std::lround(static_cast<double>(start) * sample.length())));
    return cuts;
}

// In order from the first frame, each inside the render, none twice, and any
// slice longer than a pad holds cut into equal parts that fit.
std::vector<int> fittedCuts(std::vector<int> cuts, int length, int longest)
{
    cuts.push_back(0);
    std::erase_if(cuts, [length](int cut) { return cut < 0 || cut >= length; });
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::vector<int> fitted;
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const auto from = cuts[i];
        const auto to = i + 1 < cuts.size() ? cuts[i + 1] : length;
        const juce::int64 span = to - from;
        const auto parts = std::max<juce::int64>(1, (span + longest - 1) / longest);
        for (juce::int64 part = 0; part < parts; ++part)
            fitted.push_back(from + static_cast<int>(span * part / parts));
    }
    return fitted;
}

juce::Result writeSlice(const juce::File& file, const juce::AudioBuffer<float>& audio, int from, int frames,
                        double sampleRate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail("Rhino could not write " + file.getFileName() + ".");
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(sampleRate).withNumChannels(audio.getNumChannels()).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(audio, from, frames))
        return juce::Result::fail("Rhino could not write " + file.getFileName() + ".");
    return juce::Result::ok();
}
}

juce::Result Session::sliceClipToNewMidiTrack(te::EditItemID id, double everyBeats, SliceResult* summary)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (summary != nullptr)
        *summary = {};
    if (isBouncing())
        return juce::Result::fail("Wait for the bounce to finish.");
    if (isExporting())
        return juce::Result::fail("Wait for the WAV export to finish.");
    if (!std::isfinite(everyBeats) || everyBeats < 0.0)
        return juce::Result::fail("Choose how to slice the clip.");
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Only an audio clip can be sliced to a new MIDI track.");
    const auto tracks = audioTracks();
    auto* track = dynamic_cast<te::AudioTrack*>(clip->getClipTrack());
    const auto source = tracks.indexOf(track);
    // A clip in one of the track's cells belongs to the DJ view.
    if (source < 0 || !track->getClips().contains(clip) || !shouldShowClipInArrangement(*clip))
        return juce::Result::fail("Only a clip on the timeline can be sliced to a new track.");
    // Either would render silence, and the slices with it.
    if (clip->isMuted())
        return juce::Result::fail(clip->getName().quoted() + " is muted, so it would slice to silence. Unmute it first.");
    if (track->isMuted(false))
        return juce::Result::fail("Unmute " + trackName(source) + " before slicing its clips: a muted track renders silence.");
    // The render waits for the clip's file to open, so a missing one is said
    // before anything is written.
    if (const auto readable = checkClipFile(*clip); readable.failed())
        return readable;
    const auto* rack = DeviceCatalog::byId("Drums");
    if (rack == nullptr)
        return juce::Result::fail("The Drum Rack is not in this build.");
    const auto folder = mergedAudioDirectory();
    if (!folder.createDirectory().wasOk())
        return juce::Result::fail("Rhino could not create the folder for the slices.");

    const auto label = clip->getName().trim().isNotEmpty() ? clip->getName().trim() : trackName(source);
    const auto clipStart = clip->getPosition().time.getStart().inSeconds();
    const auto clipEnd = clip->getPosition().time.getEnd().inSeconds();
    auto& devices = engine.getDeviceManager();
    const double deviceRate = devices.getSampleRate() > 7000.0 ? devices.getSampleRate() : 48000.0;
    const int blockSize = devices.getBlockSize() > 0 ? devices.getBlockSize() : 512;
    juce::AudioBuffer<float> audio;
    auto sampleRate = deviceRate;
    {
        const juce::TemporaryFile rendered(".wav");
        if (const auto made = renderClip(*edit, *track, *clip, rendered.getFile(), deviceRate, blockSize); made.failed())
            return made;
        if (const auto read = readRender(rendered.getFile(), audio, sampleRate); read.failed())
            return read;
    }
    const auto length = audio.getNumSamples();

    std::vector<int> cuts;
    if (everyBeats == sliceAtTransients)
    {
        cuts = transientCuts(audio, sampleRate);
    }
    else
    {
        // Every so many beats of the song from the clip's start, so the first
        // slice starts with the clip. One that would leave a sliver at the end
        // is the end. Bounded, so a division too fine for any rack ends here
        // rather than in a loop that runs for minutes.
        const auto startBeat = secondsToBeats(clipStart);
        for (int step = 1; static_cast<int>(cuts.size()) <= 100000; ++step)
        {
            const auto seconds = beatsToSeconds(startBeat + everyBeats * step) - clipStart;
            if (seconds >= clipEnd - clipStart - minimumSplitSeconds)
                break;
            cuts.push_back(static_cast<int>(std::lround(seconds * sampleRate)));
        }
    }
    cuts = fittedCuts(std::move(cuts), length,
                      static_cast<int>(std::floor(sampleRate * DrumRackEngine::longestSampleSeconds)));
    const auto count = static_cast<int>(cuts.size());
    if (count > sliceNoteCount)
        return juce::Result::fail("Slicing " + label.quoted() + " that way makes " + juce::String(count)
                                  + " slices, and a Drum Rack has " + juce::String(sliceNoteCount)
                                  + " pads from C1 up. Choose a longer division, or split the clip and slice part of it.");

    // Named one at a time, each written before the next is named, so no two
    // are handed the same file.
    std::vector<juce::File> files;
    const auto removeFiles = [&files]
    {
        for (const auto& file : files)
            file.deleteFile();
    };
    for (int i = 0; i < count; ++i)
    {
        const auto from = cuts[static_cast<size_t>(i)];
        const auto to = i + 1 < count ? cuts[static_cast<size_t>(i + 1)] : length;
        const auto file = nextRenderFile(folder, label, "Slice");
        if (file == juce::File{})
        {
            removeFiles();
            return juce::Result::fail("Rhino could not name a file for the slices.");
        }
        if (const auto written = writeSlice(file, audio, from, to - from, sampleRate); written.failed())
        {
            file.deleteFile();
            removeFiles();
            return written;
        }
        files.push_back(file);
    }
    // A one-shot of its whole file, at unity: struck at full velocity, a pad
    // plays its slice as it was rendered.
    DrumKit kit;
    for (int i = 0; i < count; ++i)
    {
        auto sound = DrumSound::forSample(files[static_cast<size_t>(i)]);
        sound.name = label + " " + juce::String(i + 1);
        kit.pads[static_cast<size_t>(firstSliceNote + i)] = std::move(sound);
    }

    auto& undo = edit->getUndoManager();
    undo.beginNewTransaction("Slice " + label + " to new MIDI track");
    // Whatever part of the edit had happened is taken back, so a refusal
    // halfway through leaves the document as it was.
    const auto abandon = [&undo, &removeFiles](const juce::String& why)
    {
        undo.undoCurrentTransactionOnly();
        removeFiles();
        return juce::Result::fail(why);
    };
    const auto colour = clipColour(*clip);
    clip->setMuted(true);
    auto* made = appendTrack(TrackType::midi, source);
    if (made == nullptr)
        return abandon("Could not create the track for the slices.");
    // Beside its source in a group, so its sound goes where the clip's did.
    if (const auto group = trackGroupId(source); group != 0)
        made->state.setProperty(trackGroupMemberID, group, &undo);
    reconcileTrackGroups();
    ensureSceneSlots();
    ensureTrackMixers();
    InstrumentSwitch switched;
    if (const auto result = switchTrackInstrument(*edit, *made, *rack, switched); result.failed())
        return abandon(result.getErrorMessage());
    auto* drums = findDrumRack(*made);
    if (drums == nullptr)
        return abandon("The Drum Rack could not be created.");
    drums->setKit(kit);
    drums->showNotesFrom(firstSliceNote);
    drums->setSelectedPad(firstSliceNote);
    // After the instrument, which names a track after itself.
    made->setName(label + " Slices");
    made->setColour(colour);
    auto midi = made->insertMIDIClip(label + " Slices", clip->getPosition().time, nullptr);
    if (midi == nullptr)
        return abandon("The slices' clip could not be added.");
    // A new clip's sequence starts with the clip, so a note's beat is its
    // slice's place in the song less the clip's start.
    const auto startBeat = secondsToBeats(clipStart);
    const auto beatAt = [this, clipStart, startBeat, sampleRate](int frame)
    {
        return secondsToBeats(clipStart + frame / sampleRate) - startBeat;
    };
    auto& sequence = midi->getSequence();
    for (int i = 0; i < count; ++i)
    {
        const auto from = beatAt(cuts[static_cast<size_t>(i)]);
        const auto to = i + 1 < count ? beatAt(cuts[static_cast<size_t>(i + 1)]) : secondsToBeats(clipEnd) - startBeat;
        sequence.addNote(firstSliceNote + i, tracktion::core::BeatPosition::fromBeats(from),
                         tracktion::core::BeatDuration::fromBeats(std::max(to - from, 1.0e-3)), 127, 0, &undo);
    }
    makeRoomForClip(*midi);
    refreshLoop();
    closeUndoStep();

    if (summary != nullptr)
        *summary = {audioTracks().indexOf(made), midi->itemID, count};
    // A new track among the others moves every lane under it, and may have
    // joined a group, which is what moving a track counts as.
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
