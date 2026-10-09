#include "SessionDjInternal.h"
#include <algorithm>
#include <cmath>

// What a deck plays, and how it gets there.
//
// A file is read whole and analysed on the booth's one worker thread, so a
// six-minute song does not stall the interface while it is decoded, and the
// poll installs it. A track or a group of the document is bounced to audio
// on the same worker, from a copy of the document loaded from a snapshot of
// its state - the rule the file workers follow: a worker is handed a
// detached snapshot, never the live edit, so the person can go on editing
// while the bounce renders and nothing races. The live edit stays on the
// device throughout, so the decks, and a track being played live, are not
// interrupted. The copy costs its plugins being made again, which is the
// price of the isolation.
//
// A bounced deck goes stale whenever the document changes, and the poll
// bounces it again once the document has been quiet for half a second -
// which is what makes the note editor edit a deck while it plays. The new
// bounce is swapped in by beat, so the deck does not lose its place.

namespace rhino
{
namespace
{
constexpr int readChunk = 1 << 16;

juce::String djSourceLabel(Session::DjSourceKind kind)
{
    switch (kind)
    {
        case Session::DjSourceKind::none: return "nothing";
        case Session::DjSourceKind::file: return "a file";
        case Session::DjSourceKind::track: return "a track";
        case Session::DjSourceKind::group: return "a group";
    }
    return {};
}
}

std::unique_ptr<DjTrack> readDjTrack(const juce::File& file, juce::AudioFormatManager& formats, juce::String& error,
                                     std::atomic<bool>* cancel)
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
    const auto longest = static_cast<juce::int64>(longestDjTrackSeconds * reader->sampleRate);
    const auto frames = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, longest));
    if (reader->lengthInSamples > longest)
        juce::Logger::writeToLog("Rhino: " + file.getFileName() + " is longer than a deck takes; the first "
                                 + juce::String(longestDjTrackSeconds / 60.0, 0) + " minutes are loaded");
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
        if (!reader->read(&buffer, 0, count, start, true, stereo))
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

juce::Result Session::loadDjDeckFile(int index, const juce::File& file)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count))
        return juce::Result::fail("That deck does not exist.");
    if (!file.existsAsFile())
        return juce::Result::fail(file.getFileName() + " could not be found.");
    auto& deck = dj->decks[static_cast<size_t>(index)];
    if (deck.info.live)
        setDjDeckLive(index, false);
    const auto generation = deck.info.generation + 1;
    deck.info = {};
    deck.info.kind = DjSourceKind::file;
    deck.info.file = file;
    deck.info.name = file.getFileNameWithoutExtension();
    deck.info.generation = generation;
    deck.settings = {};
    return startDjFileRead(index);
}

// Hands a file deck's file to the worker. Whatever the deck held, and any
// read still in flight for it, is let go of; the settings are kept, so a
// document's hot cues land on the file once it is read.
juce::Result Session::startDjFileRead(int index)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(index)];
    const auto file = deck.info.file;
    for (auto& job : b.jobs)
        if (job->deck == index)
            job->cancel.store(true);
    if (!file.existsAsFile())
    {
        deck.info.error = file.getFileName() + " could not be found.";
        deck.info.loading = false;
        deck.info.stale = false;
        sendSynchronousChangeMessage();
        return juce::Result::fail(deck.info.error);
    }
    deck.info.loading = true;
    deck.info.stale = false;
    deck.info.error.clear();
    auto job = std::make_shared<DjLoadJob>();
    job->deck = index;
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

juce::Result Session::loadDjDeckTrack(int index, int track)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count))
        return juce::Result::fail("That deck does not exist.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Choose a track of the song for the deck.");
    if (isGroupBusTrack(track))
        return loadDjDeckGroup(index, trackGroupBusId(track));
    auto& deck = dj->decks[static_cast<size_t>(index)];
    if (deck.info.live)
        setDjDeckLive(index, false);
    for (auto& job : dj->jobs)
        if (job->deck == index)
            job->cancel.store(true);
    const auto generation = deck.info.generation + 1;
    deck.info = {};
    deck.info.kind = DjSourceKind::track;
    deck.info.trackId = tracks[track]->itemID;
    deck.info.name = trackName(track);
    deck.info.generation = generation;
    deck.settings = {};
    deck.settings.loopWhole = true;
    // A deck's source is saved with the document.
    markDjModified();
    return bounceDjDeck(index, false);
}

juce::Result Session::loadDjDeckGroup(int index, int groupId)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count))
        return juce::Result::fail("That deck does not exist.");
    const auto group = trackGroup(groupId);
    if (!group)
        return juce::Result::fail("That group does not exist.");
    auto& deck = dj->decks[static_cast<size_t>(index)];
    if (deck.info.live)
        setDjDeckLive(index, false);
    for (auto& job : dj->jobs)
        if (job->deck == index)
            job->cancel.store(true);
    const auto generation = deck.info.generation + 1;
    deck.info = {};
    deck.info.kind = DjSourceKind::group;
    deck.info.groupId = groupId;
    deck.info.name = group->name;
    deck.info.generation = generation;
    deck.settings = {};
    deck.settings.loopWhole = true;
    markDjModified();
    return bounceDjDeck(index, false);
}

juce::Result Session::rebounceDjDeck(int index)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count))
        return juce::Result::fail("That deck does not exist.");
    const auto& info = dj->decks[static_cast<size_t>(index)].info;
    if (info.kind != DjSourceKind::track && info.kind != DjSourceKind::group)
        return juce::Result::fail("Only a track or a group is bounced; a file is read as it is.");
    return bounceDjDeck(index, info.loaded);
}

// The bounce. One track, or a bus with the members that feed it, rendered
// from the top of the song to the end of the last clip among them, rounded
// up to whole bars, through its own devices and fader and nothing else.
// Solo elsewhere in the edit and session-view slot clips are kept out, as a
// merge keeps them out. The render runs on the worker against a copy of the
// document; what this does on the message thread is make the copy and the
// render, which takes a fraction of a second, and hand them over.
juce::Result Session::bounceDjDeck(int index, bool keepBeat)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(index)];
    const auto info = djDeckInfo(index);
    deck.info.stale = false;
    deck.info.error.clear();
    const auto fail = [&deck, this](const juce::String& message)
    {
        deck.info.error = message;
        deck.info.loading = false;
        sendSynchronousChangeMessage();
        return juce::Result::fail(message);
    };
    if (info.track < 0)
        return fail(info.kind == DjSourceKind::group ? "The deck's group is no longer in the song."
                                                     : "The deck's track is no longer in the song.");
    const auto tracks = te::getAudioTracks(*edit);
    std::vector<int> members;
    if (info.kind == DjSourceKind::group)
    {
        const auto group = trackGroup(info.groupId);
        if (!group)
            return fail("The deck's group is no longer in the song.");
        members.push_back(group->busTrack);
        for (int t = group->firstTrack; t <= group->lastTrack(); ++t)
            members.push_back(t);
    }
    else
    {
        members.push_back(info.track);
    }
    // The span: to the end of the last clip, in whole bars, at least one.
    auto end = tracktion::core::TimePosition::fromSeconds(0.0);
    bool anyClip = false;
    for (const auto member : members)
        for (auto* clip : tracks[member]->getClips())
            if (clip != nullptr && shouldShowClipInArrangement(*clip))
            {
                end = std::max(end, clip->getPosition().time.getEnd());
                anyClip = true;
            }
    if (!anyClip)
        return fail((info.kind == DjSourceKind::group ? "The group " : "The track ") + info.name.quoted()
                    + " has no clips to bounce yet.");
    const auto endBeats = edit->tempoSequence.toBeats(end).inBeats();
    const auto bars = std::max(1.0, std::ceil(endBeats / beatsPerBar() - 1.0e-6));
    const auto spanEnd = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(bars * beatsPerBar()));

    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Rhino DJ bounces");
    if (!folder.createDirectory().wasOk())
        return fail("Rhino could not create a folder for the bounce.");
    const auto file = folder.getNonexistentChildFile("Deck " + juce::String(index + 1), ".wav");

    // The copy: the document as it stands, lanes mirrored onto the engine's
    // curves first so the render plays them as playback would.
    mirrorAutomationToEngine();
    edit->flushState();
    auto copy = te::loadEditFromState(engine, edit->state.createCopy());
    if (copy == nullptr)
        return fail("The song could not be copied for the bounce.");
    copy->editFileRetriever = edit->editFileRetriever;
    std::vector<te::EditItemID> memberIds;
    for (const auto member : members)
        memberIds.push_back(tracks[member]->itemID);
    te::Track::Array isolated;
    const auto copyTracks = te::getAudioTracks(*copy);
    for (const auto id : memberIds)
        for (auto* track : copyTracks)
            if (track->itemID == id)
                isolated.add(track);
    if (isolated.size() != static_cast<int>(memberIds.size()))
        return fail("The deck's tracks were not found in the copy of the song.");

    // A read already in flight for this deck is let go of: the newer bounce
    // is the one that counts.
    for (auto& job : b.jobs)
        if (job->deck == index)
            job->cancel.store(true);
    auto job = std::make_shared<DjLoadJob>();
    job->deck = index;
    job->generation = deck.info.generation;
    job->file = file;
    job->keepBeat = keepBeat;
    job->name = info.name;
    job->tempo = tempo();
    job->beatsPerBar = std::max(1, static_cast<int>(std::lround(beatsPerBar())));
    auto work = std::make_unique<DjBounceWork>();
    work->job = job;
    work->copy = std::move(copy);
    work->isolator = std::make_unique<te::FreezePointPlugin::ScopedTrackSoloIsolator>(*work->copy, isolated);
    work->slotDisabler = std::make_unique<te::Renderer::ScopedClipSlotDisabler>(*work->copy, isolated);
    auto& devices = engine.getDeviceManager();
    te::Renderer::Parameters parameters(*work->copy);
    parameters.destFile = file;
    parameters.audioFormat = &work->wav;
    parameters.bitDepth = 24;
    // Rendered at the device's own rate and block size, as a merge is.
    parameters.sampleRateForAudio = devices.getSampleRate() > 7000.0 ? devices.getSampleRate() : 48000.0;
    parameters.blockSizeForAudio = devices.getBlockSize() > 0 ? devices.getBlockSize() : 512;
    parameters.time = {tracktion::core::TimePosition(), spanEnd};
    // The track, or a group's bus with the members that feed it: the engine
    // builds a member into its bus's node, and a bus named alone renders
    // nothing.
    const auto all = te::getAllTracks(*work->copy);
    for (auto* track : isolated)
    {
        const auto position = all.indexOf(static_cast<te::Track*>(track));
        if (position >= 0)
            parameters.tracksToDo.setBit(position);
    }
    // The track as it sounds: its instrument, its effects and its fader.
    // The main chain stays out, as it is the mixer's job here.
    parameters.usePlugins = true;
    parameters.useMasterPlugins = false;
    parameters.canRenderInMono = false;
    work->task = std::make_unique<te::Renderer::RenderTask>("Bounce to deck", parameters, nullptr, nullptr);
    job->started = juce::Time::getMillisecondCounterHiRes();
    deck.info.loading = true;
    juce::Logger::writeToLog("Rhino: bouncing " + info.name.quoted() + " to deck " + juce::String(index + 1) + ": "
                             + juce::String(bars, 0) + " bars, " + juce::String(members.size()) + " track(s)");
    b.jobs.push_back(job);
    auto* task = work->task.get();
    b.bounces.push_back(std::move(work));
    auto* formats = &b.formats;
    const auto render = [job, task, formats]
    {
        juce::String error;
        // A render that returns unfinished for good would hold the worker
        // for good; a budget turns that into a refusal.
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 60000.0;
        while (task->runJob() != juce::ThreadPoolJob::jobHasFinished)
        {
            if (job->cancel.load())
            {
                error = "Cancelled.";
                break;
            }
            if (juce::Time::getMillisecondCounterHiRes() > deadline)
            {
                error = "The bounce did not finish in time.";
                break;
            }
        }
        if (error.isEmpty() && (task->errorMessage.isNotEmpty() || !job->file.existsAsFile()))
            error = task->errorMessage.isNotEmpty() ? task->errorMessage : juce::String("The bounce could not be rendered.");
        std::unique_ptr<DjTrack> track;
        if (error.isEmpty())
        {
            track = readDjTrack(job->file, *formats, error, &job->cancel);
            if (track != nullptr)
            {
                track->name = job->name;
                DjAnalysisOptions options;
                options.knownBpm = job->tempo;
                options.knownFirstBeatSeconds = 0.0;
                options.beatsPerBar = job->beatsPerBar;
                options.cancel = &job->cancel;
                track->analysis = analyseDjTrack(track->left.data(), track->stereo() ? track->right.data() : nullptr,
                                                 track->length(), track->sampleRate, options);
            }
        }
        job->file.deleteFile();
        juce::Logger::writeToLog("Rhino: bounce to deck " + juce::String(job->deck + 1) + (error.isEmpty() ? " rendered in " : " failed after ")
                                 + juce::String(juce::Time::getMillisecondCounterHiRes() - job->started, 0) + " ms"
                                 + (error.isEmpty() ? juce::String() : ": " + error));
        job->error = error;
        job->result = std::move(track);
        job->done.store(true, std::memory_order_release);
    };
    // The render's initialisation takes the message manager's lock, which
    // the worker is granted only while the message thread dispatches. The
    // app's always does, as it does for the WAV export; the test runners
    // have no dispatch loop (modal loops are compiled out), so under the
    // command-line test mode the render runs here, inline, and the poll
    // still installs it.
    if (isCommandLineTestMode())
        render();
    else
        b.workers.addJob(render);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Material arriving on a deck, from a bounce or a read. The settings read
// from a document, or the whole-loop a fresh bounce asks for, are applied
// once the engine has adopted it - which it does at its next block, so the
// commands are queued behind the swap.
void Session::finishDjLoad(int index, int generation, std::unique_ptr<DjTrack> track, const juce::String& error, bool keepBeat)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count)) return;
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(index)];
    if (deck.info.generation != generation)
        return;   // a later load has taken the deck
    deck.info.loading = false;
    if (track == nullptr)
    {
        deck.info.error = error;
        deck.info.loaded = false;
        juce::Logger::writeToLog("Rhino: deck " + juce::String(index + 1) + " could not load: " + error);
        sendSynchronousChangeMessage();
        return;
    }
    const auto first = !deck.info.loaded;
    const auto seconds = track->seconds();
    const auto bpm = track->analysis.bpm;
    b.engine.setTrack(index, std::move(track), keepBeat);
    b.engine.collect();
    deck.info.loaded = true;
    deck.info.error.clear();
    // New material, whether a first load or a bounce made again: a display
    // keyed on the generation redraws.
    ++deck.info.generation;
    if (first || !keepBeat)
        applyDjDeckSettings(index);
    juce::Logger::writeToLog("Rhino: deck " + juce::String(index + 1) + " loaded " + deck.info.name.quoted() + ", "
                             + juce::String(seconds, 1) + " s" + (bpm > 0.0 ? ", " + juce::String(bpm, 2) + " BPM" : ""));
    if (deck.info.live && deck.info.kind == DjSourceKind::track && !deck.monitoringBeforeLive.has_value())
    {
        // Live was saved with the document: switch the track's input on now
        // that the deck is here.
        deck.info.live = false;
        setDjDeckLive(index, true);
    }
    sendSynchronousChangeMessage();
}

void Session::applyDjDeckSettings(int index)
{
    auto& b = *dj;
    auto& deck = b.decks[static_cast<size_t>(index)];
    const auto* track = b.engine.trackOf(index);
    if (track == nullptr) return;
    const auto rate = track->sampleRate;
    auto& settings = deck.settings;
    auto& engineDeck = b.engine.deck(index);
    engineDeck.tempoRange.store(settings.tempoRange, std::memory_order_relaxed);
    engineDeck.tempoPercent.store(settings.tempoPercent, std::memory_order_relaxed);
    engineDeck.synced.store(settings.synced, std::memory_order_relaxed);
    engineDeck.reversed.store(settings.reversed, std::memory_order_relaxed);
    using Command = DjEngine::Command;
    const auto push = [&b](Command c)
    {
        if (!b.engine.push(c))
            juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; a setting was dropped");
    };
    Command cue;
    cue.type = Command::Type::setCue;
    cue.deck = index;
    cue.value = std::max(0.0, settings.cueSeconds) * rate;
    push(cue);
    for (int i = 0; i < DjDeck::hotCueCount; ++i)
    {
        const auto at = settings.hotCueSeconds[static_cast<size_t>(i)];
        if (at < 0.0) continue;
        Command set;
        set.type = Command::Type::setHotCue;
        set.deck = index;
        set.count = i;
        set.value = at * rate;
        push(set);
    }
    Command loop;
    loop.type = Command::Type::setLoop;
    loop.deck = index;
    if (settings.loopWhole)
    {
        loop.value = 0.0;
        loop.value2 = static_cast<double>(track->length());
        loop.flag = true;
        push(loop);
    }
    else if (settings.loop.valid())
    {
        loop.value = settings.loop.startSeconds * rate;
        loop.value2 = settings.loop.endSeconds * rate;
        loop.flag = settings.loop.active;
        push(loop);
    }
    settings.loopWhole = false;
}

void Session::ejectDjDeck(int index)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count)) return;
    auto& deck = dj->decks[static_cast<size_t>(index)];
    if (deck.info.live)
        setDjDeckLive(index, false);
    for (auto& job : dj->jobs)
        if (job->deck == index)
            job->cancel.store(true);
    const auto generation = deck.info.generation + 1;
    deck.info = {};
    deck.info.generation = generation;
    deck.settings = {};
    dj->engine.setTrack(index, nullptr, false);
    dj->engine.collect();
    markDjModified();
    sendSynchronousChangeMessage();
}

void Session::setDjDeckAutoRebounce(int index, bool automatic)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count)) return;
    dj->decks[static_cast<size_t>(index)].info.autoRebounce = automatic;
    if (automatic && dj->decks[static_cast<size_t>(index)].info.stale)
        dj->staleSince = juce::Time::getMillisecondCounter();
    sendSynchronousChangeMessage();
}

// Live: the deck's track takes its input and plays it, so a Drum Rack on it
// is heard over the bounce. It is monitoring switched On, put back when Live
// goes off, and it needs a MIDI track with something to play.
juce::Result Session::setDjDeckLive(int index, bool live)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count))
        return juce::Result::fail("That deck does not exist.");
    auto& deck = dj->decks[static_cast<size_t>(index)];
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
        return juce::Result::fail(info.kind == DjSourceKind::group
            ? "A group has no instrument to play live. Load one of its tracks on a deck instead."
            : "Live plays a track's instrument over its bounce. Load a track of the song on this deck first.");
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

// The first MIDI clip on the deck's track, which is what the note editor
// opens; the empty starter clip counts, because it is what a fresh track
// edits in.
te::EditItemID Session::djDeckEditClip(int index) const
{
    const auto info = djDeckInfo(index);
    if (info.kind != DjSourceKind::track || info.track < 0)
        return {};
    const auto tracks = te::getAudioTracks(*edit);
    te::Clip* first = nullptr;
    for (auto* clip : tracks[info.track]->getClips())
        if (dynamic_cast<te::MidiClip*>(clip) != nullptr
            && (first == nullptr || clip->getPosition().time.getStart() < first->getPosition().time.getStart()))
            first = clip;
    return first != nullptr ? first->itemID : te::EditItemID();
}

// Every change to the document may have moved what a bounced deck plays.
void Session::djDocumentChanged()
{
    if (dj == nullptr) return;
    auto any = false;
    for (int i = 0; i < dj->count; ++i)
    {
        auto& deck = dj->decks[static_cast<size_t>(i)];
        if (deck.info.kind == DjSourceKind::track || deck.info.kind == DjSourceKind::group)
        {
            deck.info.stale = true;
            any = true;
        }
    }
    if (any)
        dj->staleSince = juce::Time::getMillisecondCounter();
}

void Session::djPoll()
{
    if (dj == nullptr) return;
    auto& b = *dj;
    // Files the worker has finished, installed in the order they were asked
    // for. A job whose deck has moved on is dropped with its result.
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
            finishDjLoad(finished->deck, finished->generation, std::move(finished->result), finished->error, finished->keepBeat);
        // A bounce's copy of the document and its render go now, here on the
        // message thread: the worker touched neither after raising done.
        b.bounces.erase(std::remove_if(b.bounces.begin(), b.bounces.end(),
                                       [&finished](const std::unique_ptr<DjBounceWork>& work) { return work->job == finished; }),
                        b.bounces.end());
    }
    b.engine.collect();
    // A stale deck is loaded again once the document has been quiet for a
    // moment - one deck per poll, so several decks do not stall one frame.
    // A file read in a document goes to the worker; a bounce is made here.
    if (b.staleSince == 0) return;
    if (juce::Time::getMillisecondCounter() - b.staleSince < DjBooth::rebounceQuietMs) return;
    for (int i = 0; i < b.count; ++i)
    {
        auto& deck = b.decks[static_cast<size_t>(i)];
        if (!deck.info.stale || !deck.info.autoRebounce) continue;
        if (deck.info.kind == DjSourceKind::file)
        {
            startDjFileRead(i);
            return;
        }
        if (deck.info.kind != DjSourceKind::track && deck.info.kind != DjSourceKind::group) continue;
        bounceDjDeck(i, deck.info.loaded);
        return;
    }
    b.staleSince = 0;
}

bool Session::djBusy() const
{
    if (dj == nullptr) return false;
    for (int i = 0; i < dj->count; ++i)
        if (dj->decks[static_cast<size_t>(i)].info.loading)
            return true;
    return !dj->jobs.empty();
}

// Everything the booth holds of a document goes with it: the decks, their
// material and the loads in flight. The engine stays attached, silent.
void Session::djReset()
{
    if (dj == nullptr) return;
    auto& b = *dj;
    for (auto& job : b.jobs)
        job->cancel.store(true);
    // The bounces' copies of the document die here, on the message thread,
    // so the worker is drained first: a render still running would read a
    // copy being destroyed.
    b.workers.removeAllJobs(true, 10000);
    b.jobs.clear();
    b.bounces.clear();
    for (int i = 0; i < maximumDjDecks; ++i)
    {
        b.decks[static_cast<size_t>(i)] = {};
        b.engine.setTrack(i, nullptr, false);
        auto& deck = b.engine.deck(i);
        deck.tempoPercent.store(0.0f);
        deck.tempoRange.store(6);
        deck.synced.store(false);
        deck.reversed.store(false);
        deck.nudgePercent.store(0.0f);
        deck.brakeSeconds.store(0.0f);
        deck.quantiseSnap.store(true);
        deck.scratching.store(false);
        auto& strip = b.engine.channel(i);
        strip.trimDb.store(0.0f);
        strip.comp.store(0.0f);
        for (auto& band : strip.eqDb) band.store(0.0f);
        strip.filter.store(0.0f);
        strip.resonance.store(0.2f);
        strip.fader.store(1.0f);
        strip.faderCurve.store(1);
        strip.send.store(0.0f);
        strip.cue.store(false);
        strip.fxOn.store(false);
        strip.crossfaderSide.store(1);
    }
    b.count = 0;
    b.engine.setDeckCount(0);
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
    b.staleSince = 0;
    b.engine.collect();
}
}
