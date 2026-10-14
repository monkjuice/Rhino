#include "SessionInternal.h"
#include <algorithm>
#include <optional>

// Bounce to New Track: what some clips, a track or a group play is rendered to
// an audio file and laid on a new audio track under its source, and the source
// is muted, so the song sounds as it did and the material can be worked as
// audio from then on. It is Live's command, with Live's rules (Live 12 manual,
// chapter 20):
//
// - A track is bounced after its devices and before its mixer. Its fader and
//   pan stay out of the file and are copied to the new track instead, so the
//   bounce sits in the mix where the source did and can still be moved.
// - A group is bounced after its bus, fader and pan included, because what a
//   group sounds like is its bus's output. Its new track sits under the whole
//   group, at unity, playing straight to the main output.
// - The main row's devices are never in a bounce.
//
// A track in a group feeds its bus, and the engine builds such a track only as
// an input of the bus it feeds (createNodeForEdit at 4536d8a skips any track
// whose output names another track), so rendered on its own it renders
// nothing. For the render it is therefore pointed straight at the main output
// and its fader switched off, both written outside the undo history and put
// back as soon as the render is over: the render is not an edit.
//
// The render runs on the message thread, a few blocks at a time (one call to
// RenderTask::runJob renders one block), so the shell's loading screen can
// draw between steps. Rendering the edit from a worker while this thread
// edits it would be a race; here nothing else can touch the document while a
// bounce runs, because the loading screen takes every input and undo, redo
// and opening a document refuse or cancel it. As with a merge, the files
// outlive an undo.

namespace rhino
{

struct Session::BounceJob
{
    int track = -1;   // the source: a track, or a group's bus
    bool group = false;
    bool fromClips = false;  // the clips are what is muted, not the track
    std::vector<int> members;
    std::vector<te::Clip*> clips;  // everything the render plays
    // One new clip each, a window onto the render over its own span.
    struct Piece
    {
        te::EditItemID source;  // the clip it stands for, or nothing for a whole track
        double start = 0.0, end = 0.0;
        juce::String name;
        juce::Colour colour;
    };
    std::vector<Piece> pieces;
    double start = 0.0, end = 0.0;
    int after = -1;     // the track the new one lands under
    int joinGroup = 0;  // the group it joins, so its audio goes where its source's did
    juce::String name;
    juce::Colour colour;
    float volumeDb = 0.0f, pan = 0.0f;
    juce::File file;
    te::AudioTrack* made = nullptr;
    std::vector<te::EditItemID> madeClips;
};

namespace
{
// A render is given up only when it has not moved for this long. It runs on
// the message thread, behind the loading screen, so this is how long a render
// that can never finish keeps the window waiting.
constexpr double bounceStallSeconds = 15.0;

// A property written for the length of a render and put back after it,
// outside the undo history.
class ScopedProperty
{
public:
    ScopedProperty(juce::ValueTree tree, const juce::Identifier& id, const juce::var& value)
        : state(std::move(tree)), property(id), had(state.hasProperty(id)), previous(state.getProperty(id))
    {
        state.setProperty(property, value, nullptr);
    }
    ~ScopedProperty()
    {
        if (had)
            state.setProperty(property, previous, nullptr);
        else
            state.removeProperty(property, nullptr);
    }

private:
    juce::ValueTree state;
    juce::Identifier property;
    bool had = false;
    juce::var previous;
    JUCE_DECLARE_NON_COPYABLE(ScopedProperty)
};

tracktion::core::TimePosition at(double seconds)
{
    return tracktion::core::TimePosition::fromSeconds(seconds);
}

// One job's render, and everything it holds in place while it runs. The task
// is the last member, so it goes before anything it relies on is put back.
//
// `alone` names a track rendered before its mixer: its fader is switched off
// and, if it feeds a bus, it is sent straight out instead. A group is rendered
// with no track alone, its members feeding its bus as they do in the song.
struct RenderInFlight
{
    RenderInFlight(te::Edit& edit, te::Track::Array rendered, te::AudioTrack* alone,
                   const juce::Array<te::Clip*>& clips, tracktion::core::TimeRange span,
                   const juce::File& file, double sampleRate, int blockSize)
        : tracks(std::move(rendered)), isolator(edit, tracks), slotDisabler(edit, tracks), destination(file)
    {
        if (alone != nullptr)
        {
            if (auto* volume = alone->getVolumePlugin())
                fader.emplace(volume->state, te::IDs::enabled, false);
            // Where TrackOutput keeps the name of what the track plays into.
            const auto output = alone->state.getChildWithName(te::IDs::OUTPUTDEVICES).getChildWithName(te::IDs::DEVICE);
            if (!alone->getOutput().usesDefaultAudioOut() && output.isValid())
                route.emplace(output, te::IDs::name, te::DeviceManager::getDefaultAudioOutDeviceName(false));
        }
        te::Renderer::Parameters parameters(edit);
        parameters.destFile = file;
        parameters.audioFormat = &wav;
        // Float, so a source driven past full scale reaches the new track as
        // it is; the new track's fader can bring it down, and 24 bits would
        // have stopped every sample at full scale.
        parameters.bitDepth = 32;
        parameters.sampleRateForAudio = sampleRate;
        parameters.blockSizeForAudio = blockSize;
        parameters.time = span;
        const auto all = te::getAllTracks(edit);
        for (auto* track : tracks)
            parameters.tracksToDo.setBit(all.indexOf(track));
        parameters.allowedClips = clips;
        parameters.usePlugins = true;
        parameters.useMasterPlugins = false;
        // As wide as the track it plays on, or a lane of mono clips through a
        // stereo effect would come back one-sided.
        parameters.canRenderInMono = false;
        task = std::make_unique<te::Renderer::RenderTask>("Bounce to new track", parameters, &progress, nullptr);
        stalled = stopRenderWhenStalled(*task, progress, bounceStallSeconds);
    }

    // Only these tracks and only these clips. Solo anywhere else would
    // silence them, and a clip in one of their cells would play into them.
    te::Track::Array tracks;
    const te::FreezePointPlugin::ScopedTrackSoloIsolator isolator;
    const te::Renderer::ScopedClipSlotDisabler slotDisabler;
    std::optional<ScopedProperty> fader, route;
    juce::File destination;
    std::atomic<float> progress {0.0f};
    juce::WavAudioFormat wav;
    std::unique_ptr<te::Renderer::RenderTask> task;
    std::shared_ptr<bool> stalled;
};
}

struct Session::BounceRun
{
    std::vector<BounceJob> jobs;
    size_t next = 0;  // the job rendering now, or the count once all have
    juce::File folder;
    double sampleRate = 48000.0;
    int blockSize = 512;
    double totalSeconds = 0.0, doneSeconds = 0.0;
    // Off the device for the whole bounce, and the last thing to go.
    std::unique_ptr<te::Edit::ScopedRenderStatus> renderStatus;
    std::unique_ptr<RenderInFlight> render;
};

void Session::BounceRunDeleter::operator()(BounceRun* run) const
{
    delete run;
}

juce::Result Session::startBounceClips(const std::vector<te::EditItemID>& ids)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    // One job per track the clips are on, so a selection swept across three
    // lanes bounces to three new tracks, each under the lane it came from.
    std::vector<BounceJob> jobs;
    const auto tracks = te::getAudioTracks(*edit);
    for (const auto id : ids)
    {
        auto* clip = findClip(id);
        if (clip == nullptr || !shouldShowClipInArrangement(*clip))
            continue;
        auto* track = dynamic_cast<te::AudioTrack*>(clip->getClipTrack());
        const auto index = tracks.indexOf(track);
        // A clip in one of the track's cells belongs to the DJ view.
        if (index < 0 || !track->getClips().contains(clip))
            continue;
        const auto time = clip->getPosition().time;
        auto job = std::find_if(jobs.begin(), jobs.end(), [index](const BounceJob& each) { return each.track == index; });
        if (job == jobs.end())
        {
            BounceJob fresh;
            fresh.track = index;
            fresh.fromClips = true;
            fresh.start = time.getStart().inSeconds();
            fresh.end = time.getEnd().inSeconds();
            fresh.after = index;
            fresh.joinGroup = trackGroupId(index);
            fresh.name = trackName(index);
            fresh.colour = trackColour(index);
            const auto mixer = trackMixer(index);
            fresh.volumeDb = mixer.volumeDb;
            fresh.pan = mixer.pan;
            jobs.push_back(std::move(fresh));
            job = std::prev(jobs.end());
        }
        if (std::find(job->clips.begin(), job->clips.end(), clip) != job->clips.end())
            continue;
        if (clip->isMuted())
            return juce::Result::fail(clip->getName().quoted() + " is muted, so it would bounce to silence. "
                                      "Unmute it first.");
        job->clips.push_back(clip);
        const auto name = clip->getName().trim();
        job->pieces.push_back({clip->itemID, time.getStart().inSeconds(), time.getEnd().inSeconds(),
                               name.isEmpty() ? juce::String("(Bounce)") : name + " (Bounce)", clipColour(*clip)});
        job->start = std::min(job->start, time.getStart().inSeconds());
        job->end = std::max(job->end, time.getEnd().inSeconds());
    }
    if (jobs.empty())
        return juce::Result::fail("Select a clip to bounce.");
    for (auto& job : jobs)
        std::sort(job.pieces.begin(), job.pieces.end(),
                  [](const BounceJob::Piece& a, const BounceJob::Piece& b) { return a.start < b.start; });
    return startBounce(jobs);
}

juce::Result Session::startBounceTracks(const std::vector<int>& requested)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    std::vector<int> chosen;
    for (const auto track : requested)
        if (juce::isPositiveAndBelow(track, trackCount()))
            chosen.push_back(track);
    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
    if (chosen.empty())
        return juce::Result::fail("Select a track or a group to bounce. The main row cannot be bounced.");
    // A member chosen with its own bus is already in the group's bounce.
    const auto asked = chosen;
    std::erase_if(chosen, [this, &asked](int track)
    {
        const auto group = trackGroup(trackGroupId(track));
        return group.has_value() && std::find(asked.begin(), asked.end(), group->busTrack) != asked.end();
    });

    std::vector<BounceJob> jobs;
    const auto tracks = te::getAudioTracks(*edit);
    for (const auto track : chosen)
    {
        BounceJob job;
        job.track = track;
        job.name = trackName(track);
        job.colour = trackColour(track);
        std::vector<int> playing {track};
        if (const auto group = trackGroup(trackGroupBusId(track)))
        {
            // After the bus, at unity: the bus's own fader is in the file.
            job.group = true;
            for (auto member = group->firstTrack; member <= group->lastTrack(); ++member)
                job.members.push_back(member);
            playing = job.members;
            job.after = group->trackCount > 0 ? group->lastTrack() : group->busTrack;
        }
        else
        {
            job.after = track;
            job.joinGroup = trackGroupId(track);
            const auto mixer = trackMixer(track);
            job.volumeDb = mixer.volumeDb;
            job.pan = mixer.pan;
        }
        for (const auto source : playing)
            for (auto* clip : tracks[source]->getClips())
            {
                if (clip == nullptr || !shouldShowClipInArrangement(*clip))
                    continue;
                const auto time = clip->getPosition().time;
                job.start = job.clips.empty() ? time.getStart().inSeconds() : std::min(job.start, time.getStart().inSeconds());
                job.end = job.clips.empty() ? time.getEnd().inSeconds() : std::max(job.end, time.getEnd().inSeconds());
                job.clips.push_back(clip);
            }
        // A track with nothing on it is passed over rather than refusing the
        // rest, the way a merge passes over MIDI clips.
        if (job.clips.empty())
            continue;
        job.pieces.push_back({{}, job.start, job.end, job.name + " (Bounce)", {}});
        jobs.push_back(std::move(job));
    }
    if (jobs.empty())
        return juce::Result::fail(chosen.size() == 1 ? trackName(chosen.front()) + " has no clips to bounce."
                                                     : juce::String("None of those tracks has clips to bounce."));
    return startBounce(jobs);
}

juce::Result Session::startBounce(std::vector<BounceJob>& jobs)
{
    if (isBouncing())
        return juce::Result::fail("A bounce is already running.");
    const auto tracks = te::getAudioTracks(*edit);
    // Every refusal before anything is written.
    for (const auto& job : jobs)
    {
        auto* track = tracks[job.track];
        if (track->isMuted(false))
            return juce::Result::fail("Unmute " + job.name + " before bouncing it: a muted track renders silence.");
        if (!job.group && trackType(job.track) == TrackType::midi && trackInstrument(*track) == nullptr)
            return juce::Result::fail(job.name + " plays no instrument, so there is nothing to bounce.");
        // The render waits for every clip's file to open: one whose file is
        // gone would hold the bounce until the stall gives up on it.
        for (auto* clip : job.clips)
            if (const auto readable = checkClipFile(*clip); readable.failed())
                return readable;
    }
    const auto folder = mergedAudioDirectory();
    if (!folder.createDirectory().wasOk())
        return juce::Result::fail("Rhino could not create the folder for bounced audio.");

    std::unique_ptr<BounceRun, BounceRunDeleter> run(new BounceRun);
    run->jobs = std::move(jobs);
    run->folder = folder;
    // At the device's own rate and block size, as the WAV export is, so a
    // bounce is made of the same material the song was auditioned at.
    auto& devices = engine.getDeviceManager();
    run->sampleRate = devices.getSampleRate() > 7000.0 ? devices.getSampleRate() : 48000.0;
    run->blockSize = devices.getBlockSize() > 0 ? devices.getBlockSize() : 512;
    for (const auto& job : run->jobs)
        run->totalSeconds += job.end - job.start;
    // The render drives the edit the device callback is holding, so the edit
    // comes off the device for the whole bounce and goes back when it ends.
    run->renderStatus = std::make_unique<te::Edit::ScopedRenderStatus>(*edit, true);
    te::TransportControl::stopAllTransports(engine, false, true);
    // The render plays the lanes from the engine's curves, as playback does,
    // so they have to be current now rather than at the next change.
    mirrorAutomationToEngine();
    te::Renderer::turnOffAllPlugins(*edit);
    bounceRun = std::move(run);
    return juce::Result::ok();
}

bool Session::continueBounce(double milliseconds)
{
    if (!isBouncing())
        return false;
    auto& run = *bounceRun;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + milliseconds;
    do
    {
        if (run.render == nullptr)
        {
            if (run.next == run.jobs.size())
            {
                finishBounce();
                return false;
            }
            auto& job = run.jobs[run.next];
            // Named only now: two sources of one name must not be handed the
            // same file before either has been written.
            job.file = nextRenderFile(run.folder, job.name, "Bounce");
            if (job.file == juce::File{})
            {
                abandonBounce("Rhino could not name a file for the bounced audio.");
                return false;
            }
            const auto tracks = te::getAudioTracks(*edit);
            te::Track::Array rendered;
            rendered.add(tracks[job.track]);
            for (const auto member : job.members)
                rendered.add(tracks[member]);
            juce::Array<te::Clip*> clips;
            for (auto* clip : job.clips)
                clips.add(clip);
            run.render = std::make_unique<RenderInFlight>(*edit, rendered, job.group ? nullptr : tracks[job.track], clips,
                                                          tracktion::core::TimeRange(at(job.start), at(job.end)),
                                                          job.file, run.sampleRate, run.blockSize);
        }
        if (run.render->task->runJob() != juce::ThreadPoolJob::jobHasFinished)
            continue;
        auto rendered = renderTaskOutcome(*run.render->task, *run.render->stalled, bounceStallSeconds);
        run.render.reset();
        auto& job = run.jobs[run.next];
        if (rendered.wasOk() && !job.file.existsAsFile())
            rendered = juce::Result::fail("The bounce could not be rendered.");
        if (rendered.failed())
        {
            abandonBounce(rendered.getErrorMessage());
            return false;
        }
        run.doneSeconds += job.end - job.start;
        ++run.next;
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return true;
}

Session::BounceProgress Session::bounceProgress() const
{
    BounceProgress progress;
    if (!isBouncing())
        return progress;
    const auto& run = *bounceRun;
    const auto current = std::min(run.next, run.jobs.size() - 1);
    progress.steps = static_cast<int>(run.jobs.size());
    progress.step = static_cast<int>(current) + 1;
    progress.source = run.jobs[current].name;
    auto done = run.doneSeconds;
    if (run.render != nullptr)
        done += run.render->progress.load() * (run.jobs[current].end - run.jobs[current].start);
    progress.fraction = run.totalSeconds > 0.0 ? static_cast<float>(juce::jlimit(0.0, 1.0, done / run.totalSeconds))
                                               : 0.0f;
    return progress;
}

juce::Result Session::bounceOutcome(BounceResult* summary) const
{
    if (summary != nullptr)
        *summary = isBouncing() ? BounceResult {} : lastBounceResult;
    return isBouncing() ? juce::Result::fail("The bounce is still running.") : lastBounce;
}

void Session::cancelBounce()
{
    if (isBouncing())
        abandonBounce("The bounce was cancelled.");
}

// Nothing has been edited yet, so a bounce that fails or is cancelled leaves
// the arrangement as it was. Only the files already written have to go.
void Session::abandonBounce(const juce::String& why)
{
    auto run = std::move(bounceRun);
    if (run == nullptr)
        return;
    // The task first, so what it holds in place is put back after it has gone.
    run->render.reset();
    for (const auto& job : run->jobs)
        if (job.file.existsAsFile())
            job.file.deleteFile();
    // No plugin left prepared for the render's settings when the device
    // comes back, which it does as the run goes.
    te::Renderer::turnOffAllPlugins(*edit);
    run.reset();
    lastBounce = juce::Result::fail(why);
    lastBounceResult = {};
}

void Session::finishBounce()
{
    auto run = std::move(bounceRun);
    te::Renderer::turnOffAllPlugins(*edit);
    run->renderStatus.reset();
    auto& jobs = run->jobs;
    const auto tracks = te::getAudioTracks(*edit);
    lastBounceResult = {};

    auto& undo = edit->getUndoManager();
    undo.beginNewTransaction("Bounce to new track");
    // Muted while every index still names its source.
    for (const auto& job : jobs)
    {
        if (job.fromClips)
        {
            for (const auto& piece : job.pieces)
                if (auto* clip = findClip(piece.source))
                    clip->setMuted(true);
        }
        else
        {
            tracks[job.track]->state.setProperty(te::IDs::mute, true, &undo);
        }
    }
    // From the bottom up, so every track above the one being placed is still
    // where its job says it is.
    std::vector<BounceJob*> placing;
    for (auto& job : jobs)
        placing.push_back(&job);
    std::sort(placing.begin(), placing.end(), [](const BounceJob* a, const BounceJob* b) { return a->after > b->after; });
    for (auto* job : placing)
    {
        job->made = appendTrack(TrackType::audio, job->after);
        if (job->made == nullptr)
        {
            lastBounce = juce::Result::fail("Could not create the track for " + job->name + "'s bounce.");
            return;
        }
        job->made->setName(job->name + " (Bounce)");
        if (!job->colour.isTransparent())
            job->made->setColour(job->colour);
        // Under the last member is outside the group unless it says it joins.
        if (job->joinGroup != 0)
            job->made->state.setProperty(trackGroupMemberID, job->joinGroup, &undo);
    }
    // Routes a track that joined a group into its bus, and gives every new
    // track its fader, its cells and the DJ booth's two ends.
    reconcileTrackGroups();
    ensureSceneSlots();
    ensureTrackMixers();
    for (auto& job : jobs)
    {
        // A track's mix comes across with it; a group's is in the file.
        if (auto* volume = job.made->getVolumePlugin(); volume != nullptr && !job.group)
        {
            volume->setVolumeDb(job.volumeDb);
            volume->setPan(job.pan);
        }
        for (const auto& piece : job.pieces)
        {
            const auto offset = tracktion::core::TimeDuration::fromSeconds(piece.start - job.start);
            auto clip = job.made->insertWaveClip(piece.name, job.file, {{at(piece.start), at(piece.end)}, offset}, false);
            if (clip == nullptr)
            {
                lastBounce = juce::Result::fail("The bounced clip could not be added.");
                return;
            }
            setClipColour(*clip, piece.colour, &undo);
            makeRoomForClip(*clip);
            job.madeClips.push_back(clip->itemID);
        }
    }
    refreshLoop();
    undo.beginNewTransaction();

    BounceResult result;
    const auto now = te::getAudioTracks(*edit);
    std::sort(placing.begin(), placing.end(), [&now](const BounceJob* a, const BounceJob* b)
    {
        return now.indexOf(a->made) < now.indexOf(b->made);
    });
    for (const auto* job : placing)
    {
        result.tracks.push_back(now.indexOf(job->made));
        result.clips.insert(result.clips.end(), job->madeClips.begin(), job->madeClips.end());
        result.sourceCount += job->fromClips ? static_cast<int>(job->pieces.size()) : 1;
        result.group = result.group || job->group;
    }
    lastBounce = juce::Result::ok();
    lastBounceResult = std::move(result);
    // New tracks among the others move every lane under them, and may have
    // joined a group, which is what moving a track counts as.
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
}

juce::Result Session::bounceClipsToNewTracks(const std::vector<te::EditItemID>& ids, BounceResult* summary)
{
    if (summary != nullptr)
        *summary = {};
    if (const auto started = startBounceClips(ids); started.failed())
        return started;
    while (continueBounce(1000.0)) {}
    return bounceOutcome(summary);
}

juce::Result Session::bounceTracksToNewTracks(const std::vector<int>& tracks, BounceResult* summary)
{
    if (summary != nullptr)
        *summary = {};
    if (const auto started = startBounceTracks(tracks); started.failed())
        return started;
    while (continueBounce(1000.0)) {}
    return bounceOutcome(summary);
}

}
