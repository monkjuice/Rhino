#include "SessionDjInternal.h"
#include <algorithm>
#include <cmath>

// What a deck plays, and how it is heard.
//
// A file is read whole and analysed on the booth's one worker thread, and the
// poll installs it. A track of the song is heard live: the deck plays one of
// the track's own clips - its clip slots, the console's cells, which are the
// DJ view's and never the arrangement's - and the track's chain plays it. A
// MIDI clip's notes go into the track's instrument and an audio clip's sound
// into its effects, through the hidden input at the head of the chain, and
// what the chain makes comes back through the hidden output at its end into
// the deck's channel strip (DjTrackPort, devices/infra/DjTrackPortDevices.h).
// So every knob on the track's devices is heard as it turns, an external
// instrument's own window included, and nothing is rendered between an edit
// and the sound. What a deck still makes again is its clip, when the clip's
// notes or sound change: a MIDI clip at once, an audio clip on the worker.

namespace rhino
{
namespace
{
constexpr int readChunk = 1 << 16;

// The track's two hidden ends, or null where one is missing.
struct PortDevices
{
    DjTrackInjectorDevice* input = nullptr;
    DjTrackTapDevice* output = nullptr;
};

PortDevices portDevicesOf(te::AudioTrack& track)
{
    PortDevices devices;
    for (auto* plugin : track.pluginList)
    {
        if (auto* input = dynamic_cast<DjTrackInjectorDevice*>(plugin); input != nullptr && devices.input == nullptr)
            devices.input = input;
        if (auto* output = dynamic_cast<DjTrackTapDevice*>(plugin); output != nullptr && devices.output == nullptr)
            devices.output = output;
    }
    return devices;
}

// A clip's length on the deck: its own, rounded up to whole bars and at least
// one, so a cell loops on the grid and lands quantised where a bar does.
double wholeBarsBeats(double beats, double beatsPerBar)
{
    return std::max(1.0, std::ceil(beats / beatsPerBar - 1.0e-6)) * beatsPerBar;
}
}

std::unique_ptr<DjTrack> readDjTrack(const juce::File& file, juce::AudioFormatManager& formats, juce::String& error,
                                     std::atomic<bool>* cancel, double startSeconds, double lengthSeconds)
{
    if (!file.existsAsFile())
    {
        error = file.getFileName() + " could not be found.";
        return nullptr;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = "Rhino cannot read " + file.getFileName() + " as audio.";
        return nullptr;
    }
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = reader->sampleRate;
    track->name = file.getFileNameWithoutExtension();
    const auto first = juce::jlimit<juce::int64>(0, reader->lengthInSamples,
                                                 static_cast<juce::int64>(std::max(0.0, startSeconds) * reader->sampleRate));
    const auto available = reader->lengthInSamples - first;
    const auto wanted = lengthSeconds < 0.0 ? available
                                            : std::min(available, static_cast<juce::int64>(lengthSeconds * reader->sampleRate));
    const auto longest = static_cast<juce::int64>(longestDjTrackSeconds * reader->sampleRate);
    const auto frames = static_cast<int>(std::min(wanted, longest));
    if (wanted > longest)
        juce::Logger::writeToLog("Rhino: " + file.getFileName() + " is longer than a deck takes; the first "
                                 + juce::String(longestDjTrackSeconds / 60.0, 0) + " minutes are loaded");
    if (frames <= 0)
    {
        error = file.getFileName() + " has no audio there.";
        return nullptr;
    }
    const auto stereo = reader->numChannels > 1;
    track->left.resize(static_cast<size_t>(frames));
    if (stereo) track->right.resize(static_cast<size_t>(frames));
    juce::AudioBuffer<float> buffer(stereo ? 2 : 1, readChunk);
    for (int start = 0; start < frames; start += readChunk)
    {
        if (cancel != nullptr && cancel->load())
        {
            error = "Cancelled.";
            return nullptr;
        }
        const auto count = std::min(readChunk, frames - start);
        if (!reader->read(&buffer, 0, count, first + start, true, stereo))
        {
            error = "Rhino could not read all of " + file.getFileName() + ".";
            return nullptr;
        }
        std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + count, track->left.begin() + start);
        if (stereo)
            std::copy(buffer.getReadPointer(1), buffer.getReadPointer(1) + count, track->right.begin() + start);
    }
    return track;
}

// ---- binding a deck to a track ---------------------------------------------

void Session::bindDjTrack(int slot, int track)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(slot)];
    auto& port = b.engine.port(slot);
    const auto tracks = audioTracks();
    auto* next = juce::isPositiveAndBelow(track, tracks.size()) ? tracks[track] : nullptr;
    const auto changing = next == nullptr || deck.boundTrack != next->itemID;
    // The track it played before, when that is another, plays the song again:
    // the port says so first, so its hidden input lets go of the notes it
    // played before it is pointed away.
    if (changing)
        port.bound.store(false, std::memory_order_release);
    // Every hidden end pointing at this port but the new track's lets go of
    // it: the track the deck left, and one an undo brought back with the
    // devices it had when it went. Found by the port, not by the track the
    // deck remembers, so no end is ever left reading a port it does not own.
    for (auto* t : tracks)
    {
        if (t == next) continue;
        const auto devices = portDevicesOf(*t);
        if (devices.input != nullptr && devices.input->currentPort() == &port) devices.input->setPort(nullptr);
        if (devices.output != nullptr && devices.output->currentPort() == &port) devices.output->setPort(nullptr);
    }
    if (next == nullptr)
    {
        deck.boundTrack = {};
        return;
    }
    const auto devices = portDevicesOf(*next);
    if (devices.input == nullptr || devices.output == nullptr)
    {
        // Every track carries both from the start (ensureDjPortDevices); one
        // without them cannot carry a deck.
        port.bound.store(false, std::memory_order_release);
        deck.boundTrack = {};
        juce::Logger::writeToLog("Rhino: " + next->getName().quoted() + " has no way in for a DJ deck");
        return;
    }
    // The ends point at the port before the port says it is bound, so the
    // audio thread never finds it bound with nowhere to go.
    devices.input->setPort(&port);
    devices.output->setPort(&port);
    port.bound.store(true, std::memory_order_release);
    if (changing)
    {
        deck.boundTrack = next->itemID;
        // The track's graph has to run to play the deck; it does whenever the
        // song has a playback context, stopped or not. The test runners have
        // no device to run one on, and play a track's chain themselves; a
        // context left allocated there is one a later offline render must
        // not find.
        if (!isCommandLineTestMode())
            edit->getTransport().ensureContextAllocated();
    }
}

void Session::djUnbindAll()
{
    if (dj == nullptr) return;
    for (int slot = 0; slot < maximumDjDecks; ++slot)
    {
        dj->engine.port(slot).bound.store(false, std::memory_order_release);
        dj->decks[static_cast<size_t>(slot)].boundTrack = {};
    }
    // Every hidden end of every track, whatever port it points at: nothing
    // may be left reading the booth once it goes.
    if (edit == nullptr) return;
    for (auto* t : audioTracks())
    {
        const auto devices = portDevicesOf(*t);
        if (devices.input != nullptr) devices.input->setPort(nullptr);
        if (devices.output != nullptr) devices.output->setPort(nullptr);
    }
}

// ---- loads -----------------------------------------------------------------

juce::Result Session::loadDjDeckFile(int index, const juce::File& file)
{
    const auto slot = dj != nullptr ? dj->slotOf(index) : -1;
    if (slot < 0)
        return juce::Result::fail("That deck does not exist.");
    if (!file.existsAsFile())
        return juce::Result::fail(file.getFileName() + " could not be found.");
    auto& deck = dj->decks[static_cast<size_t>(slot)];
    if (deck.info.live)
        setDjDeckLive(index, false);
    // A file plays itself, through no track.
    bindDjTrack(slot, -1);
    const auto generation = deck.info.generation + 1;
    deck.info = {};
    deck.info.kind = DjSourceKind::file;
    deck.info.file = file;
    deck.info.name = file.getFileNameWithoutExtension();
    deck.info.generation = generation;
    deck.settings = {};
    deck.materialDirty = false;
    return startDjFileRead(slot);
}

// Hands a file deck's file to the worker. Whatever the deck held, and any
// read still in flight for it, is let go of; the settings are kept, so a
// document's hot cues land on the file once it is read.
juce::Result Session::startDjFileRead(int slot)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(slot)];
    const auto file = deck.info.file;
    for (auto& job : b.jobs)
        if (job->slot == slot)
            job->cancel.store(true);
    if (!file.existsAsFile())
    {
        deck.info.error = file.getFileName() + " could not be found.";
        deck.info.loading = false;
        sendSynchronousChangeMessage();
        return juce::Result::fail(deck.info.error);
    }
    deck.info.loading = true;
    deck.info.error.clear();
    auto job = std::make_shared<DjLoadJob>();
    job->slot = slot;
    job->generation = deck.info.generation;
    job->file = file;
    b.jobs.push_back(job);
    auto* formats = &b.formats;
    b.workers.addJob([job, formats]
    {
        juce::String error;
        auto track = readDjTrack(job->file, *formats, error, &job->cancel);
        if (track != nullptr && !job->cancel.load())
        {
            DjAnalysisOptions options;
            options.cancel = &job->cancel;
            track->analysis = analyseDjTrack(track->left.data(), track->stereo() ? track->right.data() : nullptr,
                                             track->length(), track->sampleRate, options);
        }
        job->error = error;
        job->result = std::move(track);
        job->done.store(true, std::memory_order_release);
    });
    markDjModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// The deck becomes the track's: heard through the track's chain from now on,
// and playing nothing until one of the track's cells is chosen. The
// arrangement's clips are never a deck's - the two views share the track, not
// its clips.
juce::Result Session::loadDjDeckTrack(int index, int track)
{
    const auto slot = dj != nullptr ? dj->slotOf(index) : -1;
    if (slot < 0)
        return juce::Result::fail("That deck does not exist.");
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size()) || carriesOtherTracks(track))
        return juce::Result::fail("Choose a track of the song for the deck.");
    for (int other = 0; other < dj->count; ++other)
        if (other != index && dj->decks[static_cast<size_t>(dj->slotOf(other))].boundTrack == tracks[track]->itemID)
            return juce::Result::fail(trackName(track).quoted() + " plays on deck " + juce::String(other + 1)
                                      + " already: a track plays one deck at a time.");
    auto& deck = dj->decks[static_cast<size_t>(slot)];
    if (deck.info.live && deck.info.trackId != tracks[track]->itemID)
        setDjDeckLive(index, false);
    for (auto& job : dj->jobs)
        if (job->slot == slot)
            job->cancel.store(true);
    const auto generation = deck.info.generation + 1;
    const auto live = deck.info.live;
    deck.info = {};
    deck.info.kind = DjSourceKind::track;
    deck.info.trackId = tracks[track]->itemID;
    deck.info.name = trackName(track);
    deck.info.generation = generation;
    deck.info.live = live;
    deck.settings = {};
    deck.materialDirty = false;
    bindDjTrack(slot, track);
    // Whatever the deck played before goes: it waits for a cell now.
    deck.waiting = DjBooth::Deck::WaitingSource {nullptr, false, generation};
    handOverDjSource(slot);
    markDjModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// One of the track's cells on the deck. A deck already playing that track
// keeps its beat across the switch, so a cell chosen while the deck runs
// comes in where the deck is.
juce::Result Session::loadDjDeckSlot(int index, int track, int scene)
{
    const auto deckSlot = dj != nullptr ? dj->slotOf(index) : -1;
    if (deckSlot < 0)
        return juce::Result::fail("That deck does not exist.");
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size()) || carriesOtherTracks(track))
        return juce::Result::fail("Choose a track of the song for the deck.");
    const auto clip = slotClip(track, scene);
    if (!clip.hasClip)
        return juce::Result::fail("That cell is empty: double-click it for a new clip, or drop one on it.");
    for (int other = 0; other < dj->count; ++other)
        if (other != index && dj->decks[static_cast<size_t>(dj->slotOf(other))].boundTrack == tracks[track]->itemID)
            return juce::Result::fail(trackName(track).quoted() + " plays on deck " + juce::String(other + 1)
                                      + " already: a track plays one deck at a time.");
    auto& deck = dj->decks[static_cast<size_t>(deckSlot)];
    const auto sameTrack = deck.info.kind == DjSourceKind::track && deck.info.trackId == tracks[track]->itemID;
    // The cell it plays, or is reading, chosen again: nothing to do. Made
    // over, a read in flight would be dropped as stale and not asked again.
    if (sameTrack && deck.info.slot == scene && (deck.info.loaded || deck.info.loading))
        return juce::Result::ok();
    const auto keepBeat = sameTrack && deck.info.loaded;
    if (deck.info.live && !sameTrack)
        setDjDeckLive(index, false);
    const auto generation = deck.info.generation + 1;
    const auto live = deck.info.live;
    const auto wasLoaded = deck.info.loaded;
    deck.info = {};
    deck.info.kind = DjSourceKind::track;
    deck.info.trackId = tracks[track]->itemID;
    deck.info.slot = scene;
    deck.info.name = trackName(track) + " / " + clip.name;
    deck.info.generation = generation;
    deck.info.live = live;
    // Material it already plays stays on it until the cell's lands.
    deck.info.loaded = sameTrack && wasLoaded;
    deck.settings = {};
    deck.settings.loopWhole = true;
    deck.materialDirty = false;
    // A deck moving to another track keeps playing what it had until the new
    // cell lands - at once for a MIDI cell; for an audio cell, read on the
    // worker, the old material is heard through the new track's chain for
    // that moment, which is the price of the deck not stopping.
    bindDjTrack(deckSlot, track);
    markDjModified();
    return loadDjClip(deckSlot, keepBeat);
}

// The deck's cell made into what the deck plays. A MIDI clip is its notes,
// over its length in whole bars, read at the song's tempo: the deck sequences
// them into the track, whose instrument sounds them, so the material carries
// no audio of its own. An audio clip is its sound, read from its file on the
// worker, which the deck plays into the track's effects. A cell found empty -
// its clip deleted under the deck - leaves the deck the track's, playing
// nothing.
juce::Result Session::loadDjClip(int slot, bool keepBeat)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(slot)];
    const auto info = djSlotInfo(slot);
    const auto fail = [&deck, this](const juce::String& message)
    {
        deck.info.error = message;
        deck.info.loading = false;
        sendSynchronousChangeMessage();
        return juce::Result::fail(message);
    };
    if (info.track < 0)
        return fail("The deck's track is no longer in the song.");
    auto* cell = info.slot >= 0 ? clipSlotAt(info.track, info.slot) : nullptr;
    auto* clip = cell != nullptr ? cell->getClip() : nullptr;
    const auto cancelReads = [&b, slot]
    {
        for (auto& job : b.jobs)
            if (job->slot == slot)
                job->cancel.store(true);
    };
    if (clip == nullptr)
    {
        cancelReads();
        const auto generation = deck.info.generation + 1;
        deck.clipSignature.clear();
        deck.info.slot = -1;
        deck.info.loaded = false;
        deck.info.loading = false;
        deck.info.name = trackName(info.track);
        deck.info.generation = generation;
        deck.waiting = DjBooth::Deck::WaitingSource {nullptr, false, generation};
        handOverDjSource(slot);
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }
    deck.info.error.clear();
    const auto barBeats = std::max(1.0, beatsPerBar());
    const auto clipSeconds = clip->getPosition().time.getLength().inSeconds();
    const auto clipBeats = edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(clipSeconds)).inBeats();
    const auto spanBeats = wholeBarsBeats(clipBeats, barBeats);
    const auto spanSeconds = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(spanBeats)).inSeconds();
    const auto bpm = tempo();
    const auto perBar = std::max(1, static_cast<int>(std::lround(barBeats)));

    if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
    {
        auto material = std::make_unique<DjTrack>();
        const auto rate = b.engine.sampleRate() > 7000.0 ? b.engine.sampleRate() : 48000.0;
        material->sampleRate = rate;
        material->name = info.name;
        material->silentFrames = std::max(1, static_cast<int>(std::lround(spanSeconds * rate)));
        // The notes the engine plays: a cell's loop laid end to end, whose
        // zero is the loop's start and which its offset counts into.
        const auto offset = clipOffsetBeats(*clip);
        for (auto* note : midi->getSequenceLooped().getNotes())
        {
            const auto start = note->getStartBeat().inBeats() - offset;
            const auto end = start + note->getLengthBeats().inBeats();
            if (end <= 0.0 || start >= clipBeats) continue;
            material->midi.push_back({std::max(0.0, start), note->getNoteNumber(), note->getVelocity(), true});
            material->midi.push_back({std::min(clipBeats, end), note->getNoteNumber(), 0, false});
        }
        std::sort(material->midi.begin(), material->midi.end(), [](const DjMidiEvent& a, const DjMidiEvent& c)
        {
            return a.beat < c.beat || (a.beat == c.beat && !a.on && c.on);
        });
        juce::StringArray fields {clip->itemID.toString(), juce::String(material->silentFrames), juce::String(bpm, 6),
                                  juce::String(perBar)};
        for (const auto& event : material->midi)
            fields.add(juce::String(event.beat, 6) + " " + juce::String(event.note) + " " + juce::String(event.velocity)
                       + (event.on ? " on" : " off"));
        const auto signature = fields.joinIntoString(";");
        if (deck.info.loaded && keepBeat && signature == deck.clipSignature)
            return juce::Result::ok();   // the same notes: nothing to hand over
        cancelReads();
        deck.clipSignature = signature;
        // The grid is the song's, and the picture a strip of silence the
        // length of the clip: the screen draws the notes over it.
        auto& analysis = material->analysis;
        analysis.bpm = bpm;
        analysis.firstBeatSeconds = 0.0;
        analysis.beatsPerBar = perBar;
        analysis.confidence = 1.0;
        analysis.columns.assign(static_cast<size_t>(material->silentFrames / DjAnalysis::columnFrames + 1), {});
        finishDjLoad(slot, deck.info.generation, std::move(material), {}, keepBeat);
        return juce::Result::ok();
    }

    auto* wave = dynamic_cast<te::WaveAudioClip*>(clip);
    if (wave == nullptr)
        return fail("That clip cannot be played on a deck.");
    if (const auto readable = checkClipFile(*clip); readable.failed())
        return fail(readable.getErrorMessage());
    const auto sourceFile = wave->getAudioFile().getFile();
    const auto startSeconds = clip->getPosition().getOffset().inSeconds();
    const auto signature = juce::StringArray {clip->itemID.toString(), sourceFile.getFullPathName(), juce::String(startSeconds, 6),
                                              juce::String(clipSeconds, 6), juce::String(spanSeconds, 6), juce::String(bpm, 6),
                                              juce::String(perBar)}
                               .joinIntoString(";");
    if (deck.info.loaded && keepBeat && signature == deck.clipSignature)
        return juce::Result::ok();   // the same stretch of the same file: nothing to read again
    cancelReads();
    deck.clipSignature = signature;
    auto job = std::make_shared<DjLoadJob>();
    job->slot = slot;
    job->generation = deck.info.generation;
    job->file = sourceFile;
    job->startSeconds = startSeconds;
    job->lengthSeconds = clipSeconds;
    job->keepBeat = keepBeat;
    job->name = info.name;
    job->knownBpm = bpm;
    job->beatsPerBar = perBar;
    b.jobs.push_back(job);
    deck.info.loading = true;
    auto* formats = &b.formats;
    const auto padTo = spanSeconds;
    b.workers.addJob([job, formats, padTo]
    {
        juce::String error;
        auto track = readDjTrack(job->file, *formats, error, &job->cancel, job->startSeconds, job->lengthSeconds);
        if (track != nullptr && !job->cancel.load())
        {
            // Whole bars, the rest silence, so the cell loops on the grid.
            const auto frames = static_cast<size_t>(std::max<double>(static_cast<double>(track->length()),
                                                                     std::round(padTo * track->sampleRate)));
            track->left.resize(frames, 0.0f);
            if (track->stereo()) track->right.resize(frames, 0.0f);
            track->name = job->name;
            DjAnalysisOptions options;
            options.knownBpm = job->knownBpm;
            options.knownFirstBeatSeconds = 0.0;
            options.beatsPerBar = job->beatsPerBar;
            options.cancel = &job->cancel;
            track->analysis = analyseDjTrack(track->left.data(), track->stereo() ? track->right.data() : nullptr,
                                             track->length(), track->sampleRate, options);
        }
        job->error = error;
        job->result = std::move(track);
        job->done.store(true, std::memory_order_release);
    });
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Material arriving on a deck, from a clip or a read. It reaches the engine
// as a command, and the settings read from a document, or the whole-loop a
// fresh clip asks for, are queued behind it, so the block that takes the
// material up applies them to it.
void Session::finishDjLoad(int slot, int generation, std::unique_ptr<DjTrack> track, const juce::String& error, bool keepBeat)
{
    if (dj == nullptr || dj->deckOfSlot(slot) < 0) return;
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(slot)];
    const auto deckNumber = juce::String(b.deckOfSlot(slot) + 1);
    if (deck.info.generation != generation)
        return;   // a later load has taken the deck
    deck.info.loading = false;
    if (track == nullptr)
    {
        deck.info.error = error;
        deck.info.loaded = false;
        juce::Logger::writeToLog("Rhino: deck " + deckNumber + " could not load: " + error);
        sendSynchronousChangeMessage();
        return;
    }
    deck.waiting = DjBooth::Deck::WaitingSource {std::move(track), keepBeat, generation};
    if (!handOverDjSource(slot))
    {
        // The engine's queue drains only while blocks run, so it is full when
        // the device has stopped: the material waits here, and the poll
        // hands it over once there is room.
        juce::Logger::writeToLog("Rhino: deck " + deckNumber + " waits for room in the booth's command queue to take its material");
        sendSynchronousChangeMessage();
    }
}

bool Session::handOverDjSource(int slot)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(slot)];
    if (!deck.waiting.has_value())
        return true;
    if (deck.waiting->generation != deck.info.generation)
    {
        // A newer load or eject has taken the deck since: this one is moot.
        deck.waiting.reset();
        return true;
    }
    auto& source = *deck.waiting;
    const auto ejecting = source.material == nullptr;
    const auto keepBeat = source.keepBeat;
    const auto first = !deck.info.loaded;
    const auto seconds = ejecting ? 0.0 : source.material->seconds();
    const auto bpm = ejecting ? 0.0 : source.material->analysis.bpm;
    // The material and the settings queued behind it go together: it waits
    // until the queue has room for the cue, every hot cue and the loop too.
    const auto settings = !ejecting && (first || !keepBeat) ? 2 + DjDeck::hotCueCount : 0;
    if (DjEngine::queueSize - 1 - b.engine.pendingCommands() < 1 + settings
        || !b.engine.setDeckSource(slot, std::move(source.material), keepBeat))
    {
        // The deck goes on with what it had meanwhile; a load is still
        // loading.
        deck.info.loading = !ejecting;
        return false;
    }
    deck.waiting.reset();
    b.engine.collect();
    if (ejecting)
    {
        deck.info.loaded = false;
        return true;
    }
    deck.info.loading = false;
    deck.info.loaded = true;
    deck.info.error.clear();
    // New material, whether a first load or a clip made again: a display
    // keyed on the generation redraws.
    ++deck.info.generation;
    // A remake already on its way was asked for after this material, so it
    // lands on the new generation rather than being dropped as stale.
    for (auto& job : b.jobs)
        if (job->slot == slot && !job->cancel.load())
            job->generation = deck.info.generation;
    if (first || !keepBeat)
        applyDjDeckSettings(slot);
    juce::Logger::writeToLog("Rhino: deck " + juce::String(b.deckOfSlot(slot) + 1) + " loaded " + deck.info.name.quoted() + ", "
                             + juce::String(seconds, 1) + " s" + (bpm > 0.0 ? ", " + juce::String(bpm, 2) + " BPM" : ""));
    if (deck.info.live && deck.info.kind == DjSourceKind::track && !deck.monitoringBeforeLive.has_value())
    {
        // Live was saved with the document: switch the track's input on now
        // that the deck is here.
        deck.info.live = false;
        setDjDeckLive(b.deckOfSlot(slot), true);
    }
    sendSynchronousChangeMessage();
    return true;
}

void Session::applyDjDeckSettings(int slot)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(slot)];
    const auto* track = b.engine.trackOf(slot);
    if (track == nullptr) return;
    const auto rate = track->sampleRate;
    auto& settings = deck.settings;
    auto& controls = b.engine.deck(slot).controls;
    controls.tempoRange.store(settings.tempoRange, std::memory_order_relaxed);
    controls.tempoPercent.store(settings.tempoPercent, std::memory_order_relaxed);
    controls.synced.store(settings.synced, std::memory_order_relaxed);
    controls.reversed.store(settings.reversed, std::memory_order_relaxed);
    using Command = DjEngine::Command;
    const auto push = [&b](const Command& c)
    {
        if (!b.engine.push(c))
            juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; a setting was dropped");
    };
    push(Command::setCue(slot, std::max(0.0, settings.cueSeconds) * rate));
    for (int i = 0; i < DjDeck::hotCueCount; ++i)
        if (const auto at = settings.hotCueSeconds[static_cast<size_t>(i)]; at >= 0.0)
            push(Command::setHotCue(slot, i, at * rate));
    if (settings.loopWhole)
        push(Command::setLoop(slot, 0.0, static_cast<double>(track->length()), true));
    else if (settings.loop.valid())
        push(Command::setLoop(slot, settings.loop.startSeconds * rate, settings.loop.endSeconds * rate, settings.loop.active));
    settings.loopWhole = false;
}

void Session::ejectDjDeck(int index)
{
    const auto slot = dj != nullptr ? dj->slotOf(index) : -1;
    if (slot < 0) return;
    auto& deck = dj->decks[static_cast<size_t>(slot)];
    if (deck.info.live)
        setDjDeckLive(index, false);
    bindDjTrack(slot, -1);
    for (auto& job : dj->jobs)
        if (job->slot == slot)
            job->cancel.store(true);
    const auto generation = deck.info.generation + 1;
    deck.info = {};
    deck.info.generation = generation;
    deck.settings = {};
    deck.materialDirty = false;
    // An eject the queue has no room for waits, as a load does, and empties
    // the deck once it has.
    deck.waiting = DjBooth::Deck::WaitingSource {nullptr, false, generation};
    if (!handOverDjSource(slot))
        juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; deck " + juce::String(index + 1)
                                 + " empties once it has room");
    markDjModified();
    sendSynchronousChangeMessage();
}

// Live: the deck's track takes its input too and plays it, so a Drum Rack on
// it is heard from the keys through the deck's channel. It is monitoring
// switched On, put back when Live goes off, and it needs a MIDI track with
// something to play.
juce::Result Session::setDjDeckLive(int index, bool live)
{
    const auto slot = dj != nullptr ? dj->slotOf(index) : -1;
    if (slot < 0)
        return juce::Result::fail("That deck does not exist.");
    auto& deck = dj->decks[static_cast<size_t>(slot)];
    const auto info = djDeckInfo(index);
    if (live == deck.info.live && (!live || deck.monitoringBeforeLive.has_value()))
        return juce::Result::ok();
    if (!live)
    {
        deck.info.live = false;
        if (deck.monitoringBeforeLive.has_value() && info.track >= 0)
            setTrackMonitoring(info.track, *deck.monitoringBeforeLive);
        deck.monitoringBeforeLive.reset();
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }
    if (info.kind != DjSourceKind::track)
        return juce::Result::fail("Live plays a track's instrument from the keys. Load a track of the song on this deck first.");
    if (info.track < 0)
        return juce::Result::fail("The deck's track is no longer in the song.");
    if (trackType(info.track) != TrackType::midi)
        return juce::Result::fail("Live plays an instrument from the keys, and " + info.name.quoted() + " is an audio track.");
    if (!trackHasInstrument(info.track))
        return juce::Result::fail(info.name.quoted() + " runs no instrument to play live. Drop one on it first.");
    deck.monitoringBeforeLive = trackMonitoring(info.track);
    // The track takes the setting whether or not an input could be routed
    // to it; a refusal says why nothing will be heard yet - no audio device,
    // or a MIDI input the machine lacks - and the deck is live all the same,
    // so the input is heard as soon as there is one.
    const auto result = setTrackMonitoring(info.track, InputMonitoring::on);
    if (trackMonitoring(info.track) != InputMonitoring::on)
    {
        deck.monitoringBeforeLive.reset();
        return result.failed() ? result : juce::Result::fail("The track would not take Live.");
    }
    deck.info.live = true;
    sendSynchronousChangeMessage();
    return result;
}

bool Session::djDeckHasDrumRack(int index) const
{
    const auto info = djDeckInfo(index);
    return info.kind == DjSourceKind::track && info.track >= 0 && trackHasDrumRack(info.track);
}

// The cell the deck plays, for the note editor or the audio editor; nothing
// while it plays none. The arrangement's clips are the arrangement's.
te::EditItemID Session::djDeckEditClip(int index) const
{
    const auto info = djDeckInfo(index);
    if (info.kind != DjSourceKind::track || info.track < 0 || info.slot < 0)
        return {};
    return slotClip(info.track, info.slot).clipID;
}

// What a change leaves a deck to do. A device, a knob, a curve or the track's
// fader is heard through the live chain and asks for nothing. A change to what
// a clip plays - its notes, its sound - makes the clip of every deck playing
// that track again, and a tempo every deck's, since a clip's grid is the
// song's. The poll makes them once nothing is held.
void Session::djDocumentChanged(int track, ModelChange changes)
{
    if (dj == nullptr) return;
    const auto bits = static_cast<unsigned>(changes);
    constexpr auto clipBits = static_cast<unsigned>(ModelChange::notes) | static_cast<unsigned>(ModelChange::audioClips);
    const auto tempoChanged = (bits & static_cast<unsigned>(ModelChange::tempo)) != 0;
    if ((bits & clipBits) == 0 && !tempoChanged) return;
    const auto tracks = audioTracks();
    const auto changedId = juce::isPositiveAndBelow(track, tracks.size()) ? tracks[track]->itemID : te::EditItemID();
    for (int i = 0; i < dj->count; ++i)
    {
        auto& deck = dj->decks[static_cast<size_t>(dj->slotOf(i))];
        if (deck.info.kind != DjSourceKind::track || deck.info.slot < 0) continue;
        if (!tempoChanged && track >= 0 && deck.info.trackId != changedId) continue;
        deck.materialDirty = true;
    }
}

// A knob, a fader, the tempo field, an audio clip's sliders and a note's
// velocity say when they are held - V held down presses no mouse button; a
// jog wheel, a note drag or the booth's own faders do not, but each holds a
// mouse button down.
bool Session::gestureHeld() const
{
    return parameterGestureDepth > 0 || mixerGestureDepth > 0 || tempoGestureDepth > 0 || audioClipGestureDepth > 0
        || noteVelocityGestureDepth > 0 || pointerHeld;
}

void Session::setPointerHeld(bool held)
{
    pointerHeld = held;
}

void Session::djPoll()
{
    if (dj == nullptr) return;
    auto& b = *dj;
    // What the worker has read, installed in the order it was asked for. A
    // job whose deck has moved on is dropped with its result.
    for (size_t i = 0; i < b.jobs.size();)
    {
        auto& job = b.jobs[i];
        if (!job->done.load(std::memory_order_acquire))
        {
            ++i;
            continue;
        }
        auto finished = job;
        b.jobs.erase(b.jobs.begin() + static_cast<long>(i));
        if (!finished->cancel.load())
            finishDjLoad(finished->slot, finished->generation, std::move(finished->result), finished->error, finished->keepBeat);
    }
    for (int i = 0; i < b.count; ++i)
    {
        const auto slot = b.slotOf(i);
        auto& deck = b.decks[static_cast<size_t>(slot)];
        // A source a full queue kept waiting goes over once there is room.
        if (deck.waiting.has_value())
            handOverDjSource(slot);
        // The track a deck plays, asserted again. Gone, the deck lets go of it
        // and of what it played through it, which would otherwise play on
        // raw through the booth. Back - an undo - it is bound again and its
        // cell made again; its devices made anew are pointed at the port.
        if (deck.info.kind == DjSourceKind::track)
        {
            const auto info = djSlotInfo(slot);
            if (info.track < 0)
            {
                if (deck.boundTrack.isValid() || deck.info.loaded || deck.info.loading)
                {
                    bindDjTrack(slot, -1);
                    for (auto& job : b.jobs)
                        if (job->slot == slot)
                            job->cancel.store(true);
                    const auto generation = deck.info.generation + 1;
                    deck.info.loaded = false;
                    deck.info.loading = false;
                    deck.info.generation = generation;
                    deck.info.error = "The deck's track is no longer in the song.";
                    deck.clipSignature.clear();
                    deck.waiting = DjBooth::Deck::WaitingSource {nullptr, false, generation};
                    handOverDjSource(slot);
                    sendSynchronousChangeMessage();
                }
            }
            else
            {
                const auto rebinding = !deck.boundTrack.isValid();
                bindDjTrack(slot, info.track);
                if (rebinding && deck.info.slot >= 0 && !deck.info.loaded && !deck.info.loading)
                    deck.materialDirty = true;
            }
        }
    }
    // A slot no deck holds that the engine still renders - letting it go met
    // a full queue (djReset) - is let go of again, so no deck of a document
    // gone plays on.
    for (int slot = 0; slot < maximumDjDecks; ++slot)
        if (b.engine.slotActive(slot) && b.deckOfSlot(slot) < 0)
            b.engine.setSlotActive(slot, false);
    // A deck's material made again: a reopened document's, read for the first
    // time, or a clip changed under the deck. Not while the person is in the
    // middle of something, so a note being dragged is not cut on every step.
    if (!gestureHeld())
        for (int i = 0; i < b.count; ++i)
        {
            const auto slot = b.slotOf(i);
            auto& deck = b.decks[static_cast<size_t>(slot)];
            if (!deck.materialDirty) continue;
            deck.materialDirty = false;
            if (deck.info.kind == DjSourceKind::file)
                startDjFileRead(slot);
            else if (deck.info.kind == DjSourceKind::track && deck.info.slot >= 0)
                loadDjClip(slot, deck.info.loaded);
        }
    b.engine.collect();
}

bool Session::djBusy() const
{
    if (dj == nullptr) return false;
    for (int i = 0; i < dj->count; ++i)
        if (dj->decks[static_cast<size_t>(dj->slotOf(i))].info.loading)
            return true;
    return !dj->jobs.empty();
}

// Everything the booth holds of a document goes with it: the decks, their
// material, the tracks they played and the reads in flight. The engine stays
// attached, silent.
void Session::djReset()
{
    if (dj == nullptr) return;
    auto& b = *dj;
    for (auto& job : b.jobs)
        job->cancel.store(true);
    b.workers.removeAllJobs(true, 10000);
    b.jobs.clear();
    djUnbindAll();
    // Every slot is emptied on the audio thread at its next block, and its
    // controls and strip go back to a new deck's.
    for (int slot = 0; slot < maximumDjDecks; ++slot)
    {
        b.decks[static_cast<size_t>(slot)] = {};
        b.engine.port(slot).bound.store(false, std::memory_order_release);
        if (!b.engine.setSlotActive(slot, false))
            juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; a deck was not emptied");
        b.engine.deck(slot).controls.reset();
        b.engine.resetChannelControls(slot);
        b.engine.channel(slot).resonance.store(0.2f);
        b.engine.channel(slot).faderCurve.store(1);
    }
    b.slotOfDeck = {0, 1, 2, 3, 4, 5};
    b.count = 0;
    b.engine.quantise.store(static_cast<int>(DjQuantise::bar));
    b.engine.phaseLock.store(true);
    b.engine.crossfader().position.store(0.0f);
    b.engine.crossfader().curve.store(0.0f);
    b.engine.master().levelDb.store(0.0f);
    b.engine.master().lowDb.store(0.0f);
    b.engine.master().midDb.store(0.0f);
    b.engine.master().highDb.store(0.0f);
    b.engine.master().cueMix.store(0.0f);
    b.engine.master().cueLevelDb.store(0.0f);
    b.engine.master().monoSplit.store(false);
    b.engine.tapBpm.store(128.0);
    b.engine.fx().on.store(false);
    b.engine.fx().target.store(-1);
    b.engine.fx().type.store(1);
    b.engine.fx().beats.store(0.5f);
    b.engine.fx().depth.store(0.5f);
    b.engine.fx().bandLow.store(true);
    b.engine.fx().bandMid.store(true);
    b.engine.fx().bandHigh.store(true);
    b.engine.fx().autoTime.store(true);
    b.engine.fx().manualSeconds.store(0.25f);
    b.engine.mic().levelDb.store(0.0f);
    b.engine.mic().lowDb.store(0.0f);
    b.engine.mic().highDb.store(0.0f);
    b.engine.mic().mode.store(0);
    b.engine.sendFx().type.store(2);
    b.engine.sendFx().size.store(0.5f);
    b.engine.sendFx().time.store(0.5f);
    b.engine.sendFx().tone.store(0.5f);
    b.engine.sendFx().mix.store(0.5f);
    b.taps.clear();
    b.engine.collect();
}
}
