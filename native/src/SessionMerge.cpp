#include "SessionInternal.h"
#include <algorithm>

// Merging audio clips: several clips on one track become one clip reading one
// new audio file. Ctrl+J in the arrangement, and the same command Live spells
// Consolidate and Logic spells Join.
//
// It is a render rather than a splice because the clips being merged need not
// share a source file, need not touch, and need not agree about gain, pitch or
// direction. Rendering is the only way to get a file that is what the passage
// actually sounded like, and the span is rendered whole - the silence in a gap
// between two clips is part of what was merged.
//
// What the render includes is the clips and nothing else. `usePlugins` is off,
// so the track's devices and its fader stay out of the file: the merged clip
// goes on playing through them, and baking them in would process the passage
// twice. A clip's own gain, pan, pitch, fades, mute and reverse are not
// plugins - the engine applies them while reading the clip - so they do land
// in the file, which is what makes the merged clip sound like what it
// replaced.
//
// The one compromise is clip-local effects, which the engine gates with the
// track's plugins rather than separately, so a clip carrying its own effects
// is merged without them. MergeResult says when that happened rather than
// letting it pass quietly.
//
// The rendered file outlives an undo, as a recorded take does: undoing a merge
// brings the original clips back and leaves the file on disk unreferenced.

namespace rhino
{
namespace
{
constexpr double mergeTolerance = 1.0e-7;

// Touching an edge is not overlapping it, exactly as in SessionRegion.cpp: a
// clip that starts where the merged span ends is outside it.
bool overlapsSpan(tracktion::core::TimeRange clip, tracktion::core::TimePosition start,
                  tracktion::core::TimePosition end)
{
    return clip.getStart().inSeconds() < end.inSeconds() - mergeTolerance
        && clip.getEnd().inSeconds() > start.inSeconds() + mergeTolerance;
}

// Numbered rather than overwritten: merging, undoing and merging again must
// not hand the second clip a file the first one is still reading.
juce::File nextMergeFile(const juce::File& folder, const juce::String& trackName)
{
    auto name = juce::File::createLegalFileName(trackName.trim());
    if (name.isEmpty())
        name = "Track";
    for (int take = 1; take < 10000; ++take)
    {
        const auto file = folder.getChildFile(name + " Merge " + juce::String(take) + ".wav");
        if (!file.exists())
            return file;
    }
    return {};
}
}

// Beside the project once it has been saved, so a project folder carries the
// audio its clips read, and in the application's folder until then - the same
// rule recordingDirectory follows, in a folder of its own.
juce::File Session::mergedAudioDirectory() const
{
    if (projectFile != juce::File{})
        return projectFile.getParentDirectory().getChildFile(projectFile.getFileNameWithoutExtension() + " Audio");
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino").getChildFile("Merged audio");
}

juce::Result Session::mergeClips(const std::vector<te::EditItemID>& ids, MergeResult* summary)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (summary != nullptr)
        *summary = {};

    // Grouped by track, because merging is per lane: a selection swept across
    // three tracks leaves one clip on each rather than folding three lanes
    // into one. MIDI clips are passed over the way reverse passes over them -
    // a mixed selection is the normal case, and refusing the whole thing
    // because one lane holds notes would make the command useless exactly
    // where it is quickest to reach.
    struct Group
    {
        int track = -1;
        std::vector<te::Clip*> clips;
        tracktion::core::TimePosition start, end;
    };
    std::vector<Group> groups;
    const auto tracks = te::getAudioTracks(*edit);
    for (const auto id : ids)
    {
        auto* clip = findAudioClip(id);
        if (clip == nullptr)
            continue;
        const auto track = tracks.indexOf(dynamic_cast<te::AudioTrack*>(clip->getClipTrack()));
        if (track < 0)
            continue;
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [track](const Group& candidate) { return candidate.track == track; });
        if (group == groups.end())
        {
            const auto time = clip->getPosition().time;
            groups.push_back({track, {}, time.getStart(), time.getEnd()});
            group = std::prev(groups.end());
        }
        group->clips.push_back(clip);
        group->start = std::min(group->start, clip->getPosition().time.getStart());
        group->end = std::max(group->end, clip->getPosition().time.getEnd());
    }
    // A lane holding only one of the selected clips has nothing to merge and
    // is left exactly as it was.
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [](const Group& group) { return group.clips.size() < 2; }),
                 groups.end());
    if (groups.empty())
        return juce::Result::fail("Select two or more audio clips on the same track to merge.");

    // Everything the span touches goes in, selected or not. A clip sitting in
    // the gap between two merged clips would otherwise be rendered out of the
    // file and then deleted by the merged clip landing on it; a clip crossing
    // an edge would be cut in half. Taking them in instead grows the span,
    // which can reach further clips, so this runs to a fixed point.
    for (auto& group : groups)
    {
        for (auto growing = true; growing;)
        {
            growing = false;
            for (auto* clip : tracks[group.track]->getClips())
            {
                if (clip == nullptr
                    || std::find(group.clips.begin(), group.clips.end(), clip) != group.clips.end()
                    || !overlapsSpan(clip->getPosition().time, group.start, group.end))
                    continue;
                // An empty starter clip is invisible and carries nothing, so
                // it is simply swept up with the rest of the span.
                if (!shouldShowClipInArrangement(*clip))
                    continue;
                if (dynamic_cast<te::WaveAudioClip*>(clip) == nullptr)
                    return juce::Result::fail("A MIDI clip sits inside the span being merged on "
                                              + trackName(group.track) + ".");
                group.clips.push_back(clip);
                group.start = std::min(group.start, clip->getPosition().time.getStart());
                group.end = std::max(group.end, clip->getPosition().time.getEnd());
                growing = true;
            }
        }
    }
    // A muted track renders as silence, so merging one would replace its clips
    // with a silent file. Said rather than done.
    for (const auto& group : groups)
        if (tracks[group.track]->isMuted(false))
            return juce::Result::fail("Unmute " + trackName(group.track) + " before merging its clips.");

    const auto folder = mergedAudioDirectory();
    if (!folder.createDirectory().wasOk())
        return juce::Result::fail("Rhino could not create the folder for merged audio.");

    struct Merged
    {
        int track = -1;
        double startSeconds = 0.0, endSeconds = 0.0;
        juce::File file;
        juce::String name;
        juce::Colour colour;
        std::vector<te::EditItemID> sources;
    };
    std::vector<Merged> merged;
    MergeResult result;

    // Rendered at the device's own rate and block size, as the WAV export is,
    // so a merged clip is made of the same material the mix was auditioned at.
    auto& devices = engine.getDeviceManager();
    const double sampleRate = devices.getSampleRate() > 7000.0 ? devices.getSampleRate() : 48000.0;
    const int blockSize = devices.getBlockSize() > 0 ? devices.getBlockSize() : 512;
    juce::WavAudioFormat wav;
    juce::String failure;
    {
        // The render drives the edit the device callback is holding, so the
        // edit comes off the device first and is reattached when this goes.
        const te::Edit::ScopedRenderStatus renderStatus(*edit, true);
        te::TransportControl::stopAllTransports(engine, false, true);
        te::Renderer::turnOffAllPlugins(*edit);
        for (auto& group : groups)
        {
            std::sort(group.clips.begin(), group.clips.end(), [](const te::Clip* a, const te::Clip* b)
            {
                return a->getPosition().time.getStart() < b->getPosition().time.getStart();
            });
            auto* track = tracks[group.track];
            const auto trackIndex = te::getAllTracks(*edit).indexOf(static_cast<te::Track*>(track));
            if (trackIndex < 0)
            {
                failure = "That track could not be rendered.";
                break;
            }

            juce::Array<te::Clip*> allowed;
            Merged rendered;
            for (auto* clip : group.clips)
            {
                allowed.add(clip);
                rendered.sources.push_back(clip->itemID);
                if (clip->getPluginList() != nullptr && clip->getPluginList()->size() > 0)
                    result.lostClipEffects = true;
            }
            rendered.track = group.track;
            rendered.startSeconds = group.start.inSeconds();
            rendered.endSeconds = group.end.inSeconds();
            // Named for the track rather than for one of the clips: the merged
            // clip is all of them, and the first one's name would read as a
            // claim about what is in the file.
            rendered.name = trackName(group.track);
            rendered.colour = clipColour(*group.clips.front());
            rendered.file = nextMergeFile(folder, rendered.name);
            if (rendered.file == juce::File{})
            {
                failure = "Rhino could not name a file for the merged audio.";
                break;
            }

            // One track, and only the clips being merged. Solo anywhere else
            // in the edit would otherwise silence this track's render, and a
            // session-view slot clip would play into it.
            te::Track::Array isolated;
            isolated.add(track);
            const te::FreezePointPlugin::ScopedTrackSoloIsolator isolator(*edit, isolated);
            const te::Renderer::ScopedClipSlotDisabler slotDisabler(*edit, isolated);

            te::Renderer::Parameters parameters(*edit);
            parameters.destFile = rendered.file;
            parameters.audioFormat = &wav;
            parameters.bitDepth = 24;
            parameters.sampleRateForAudio = sampleRate;
            parameters.blockSizeForAudio = blockSize;
            parameters.time = {group.start, group.end};
            parameters.tracksToDo.setBit(trackIndex);
            parameters.allowedClips = allowed;
            // The clips, not the track: see the note at the top of this file.
            parameters.usePlugins = false;
            parameters.useMasterPlugins = false;
            // A merged clip has to be as wide as the track it plays on, or a
            // lane of mono clips would come back one-sided.
            parameters.canRenderInMono = false;
            te::Renderer::RenderTask task("Merge clips", parameters, nullptr, nullptr);
            while (task.runJob() != juce::ThreadPoolJob::jobHasFinished) {}
            if (task.errorMessage.isNotEmpty() || !rendered.file.existsAsFile())
            {
                rendered.file.deleteFile();
                failure = task.errorMessage.isNotEmpty()
                    ? task.errorMessage : juce::String("The merged audio could not be rendered.");
                break;
            }
            result.sourceCount += static_cast<int>(rendered.sources.size());
            merged.push_back(std::move(rendered));
        }
        te::Renderer::turnOffAllPlugins(*edit);
    }
    // Nothing has been edited yet, so a render that failed halfway leaves the
    // arrangement exactly as it was. Only the files already written have to go.
    if (failure.isNotEmpty())
    {
        for (const auto& rendered : merged)
            rendered.file.deleteFile();
        return juce::Result::fail(failure);
    }

    edit->getUndoManager().beginNewTransaction("Merge clips");
    for (const auto& rendered : merged)
    {
        for (const auto id : rendered.sources)
            if (auto* clip = findClip(id))
                clip->removeFromParent();
        auto* track = te::getAudioTracks(*edit)[rendered.track];
        const auto start = tracktion::core::TimePosition::fromSeconds(rendered.startSeconds);
        const auto end = tracktion::core::TimePosition::fromSeconds(rendered.endSeconds);
        auto clip = track->insertWaveClip(rendered.name, rendered.file, {{start, end}, {}}, false);
        if (clip == nullptr)
            return juce::Result::fail("The merged clip could not be added.");
        setClipColour(*clip, rendered.colour, &edit->getUndoManager());
        // Only the invisible starter clips can be left in the span by now, but
        // the rule that a clip owns the ground it lands on lives in one place
        // and this is not the file to make an exception in.
        makeRoomForClip(*clip);
        result.clips.push_back(clip->itemID);
    }
    repairPatternClip();
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    if (summary != nullptr)
        *summary = std::move(result);
    return juce::Result::ok();
}

}
