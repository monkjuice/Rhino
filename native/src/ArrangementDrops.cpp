#include "ArrangementInternal.h"
#include "BrowserIds.h"
#include <optional>
#include <set>

// File drops from the OS and item drops from the browser.

namespace theta
{
namespace
{
bool isSupportedAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aiff" || extension == ".aif"
        || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
}
}

bool Arrangement::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void Arrangement::filesDropped(const juce::StringArray& files, int x, int y)
{
    auto targetTrack = trackAt(static_cast<float>(y));
    if (targetTrack < 0 && session.trackCount() > 0 && static_cast<float>(y) > lane(session.trackCount() - 1).getBottom())
    {
        const auto result = session.addAudioTrack();
        if (result.failed())
        {
            if (status) status(result.getErrorMessage());
            return;
        }
        targetTrack = session.trackCount() - 1;
        selectTrack(targetTrack);
    }
    if (targetTrack < 0)
    {
        if (status) status("Drop audio on the arrangement lanes.");
        return;
    }
    for (const auto& path : files)
    {
        const auto file = juce::File(path);
        if (!isSupportedAudioFile(file)) continue;
        const auto result = session.importAudioAt(file, std::max(1, targetTrack), snapped(std::max(0.0, timeAt(static_cast<float>(x))), false));
        if (result.failed() && status) status(result.getErrorMessage());
    }
    fit();
}

bool Arrangement::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString().startsWith("theta-browser:");
}

void Arrangement::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    auto targetTrack = trackAt(static_cast<float>(details.localPosition.y));
    const auto description = details.description.toString();
    const auto kind = browserDropKind(description);
    if (kind == "effect")
        if (const auto clipIndex = hit(details.localPosition.toFloat()); clipIndex >= 0)
        {
            const auto effect = audioEffectFromId(browserDropId(description));
            if (!effect)
            {
                if (status) status("That browser item cannot be inserted here.");
                return;
            }
            const auto& clip = clips[static_cast<size_t>(clipIndex)];
            const auto result = session.addClipAudioEffect(*effect, clip.id);
            if (result.failed())
            {
                const auto trackResult = session.addAudioEffect(*effect, clip.track);
                if (trackResult.failed())
                {
                    if (status) status(result.getErrorMessage() + " " + trackResult.getErrorMessage());
                    return;
                }
                selected = clip.id;
                selectTrack(clip.track);
                if (status) status("Added browser effect to " + session.trackName(clip.track)
                    + " Device View for selected MIDI clip");
                return;
            }
            selected = clip.id;
            selectTrack(clip.track);
            if (status) status("Added browser effect to clip; clip FX count "
                + juce::String(session.clipPluginCount(clip.id)));
            return;
        }
    if ((kind == "preset" || kind == "effect" || kind == "instrument" || kind == "midi-effect" || kind == "sample")
        && targetTrack < 0
        && session.trackCount() > 0
        && static_cast<float>(details.localPosition.y) > lane(session.trackCount() - 1).getBottom())
    {
        const auto result = session.addAudioTrack();
        if (result.failed())
        {
            if (status) status(result.getErrorMessage());
            return;
        }
        targetTrack = session.trackCount() - 1;
    }

    const auto result = applyBrowserDrop(description, targetTrack,
                                         snapped(std::max(0.0, timeAt(static_cast<float>(details.localPosition.x))), false),
                                         true);
    if (result.failed() && status) status(result.getErrorMessage());
}

juce::Result Arrangement::applyBrowserDrop(const juce::String& description, int track, double startSeconds, bool insertPreset)
{
    const auto kind = browserDropKind(description);
    const auto id = browserDropId(description);

    if (kind == "preset")
    {
        const auto preset = patternPresetFromId(id);
        if (!preset) return juce::Result::fail("That browser item cannot be loaded here.");
        const auto result = insertPreset ? session.insertPatternPreset(*preset, std::max(0, track), startSeconds)
                                         : juce::Result::ok();
        if (result.failed()) return result;
        if (!insertPreset) session.applyPatternPreset(*preset);
        selectTrack(insertPreset ? std::max(0, track) : 0);
        if (status) status(insertPreset ? "Added pattern clip from browser" : "Loaded browser preset on Pattern 1");
        return juce::Result::ok();
    }

    if (kind == "effect")
    {
        const auto effect = audioEffectFromId(id);
        if (!effect) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop audio effects on a track or clip.");
        const auto result = session.addAudioEffect(*effect, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added browser effect to " + session.trackName(track) + " Device View");
        return juce::Result::ok();
    }

    if (kind == "instrument")
    {
        const auto instrument = instrumentFromId(id);
        if (!instrument) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop instruments on a track or clip.");
        const auto result = insertPreset ? session.insertInstrumentClip(*instrument, track, startSeconds)
                                         : session.addInstrument(*instrument, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status(insertPreset ? "Added instrument clip to " + session.trackName(track)
                                        : "Activated instrument on " + session.trackName(track));
        return juce::Result::ok();
    }

    if (kind == "midi-effect")
    {
        const auto effect = midiEffectFromId(id);
        if (!effect) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop MIDI FX on an instrument track.");
        const auto result = session.addMidiEffect(*effect, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added MIDI FX to " + session.trackName(track) + " Device View");
        return juce::Result::ok();
    }

    if (kind == "sample")
    {
        const auto sample = builtInSampleFromId(id);
        if (!sample) return juce::Result::fail("That browser sample cannot be inserted here.");
        if (track < 1) return juce::Result::fail("Drop audio samples on an audio track.");
        const auto result = session.importBuiltInSample(*sample, track, startSeconds);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added " + id + " audio sample to " + session.trackName(track));
        return juce::Result::ok();
    }

    if (kind == "info")
    {
        if (status) status(id + " is already available in this starter session.");
        return juce::Result::ok();
    }

    return juce::Result::fail("Drop sounds, drums, instruments, MIDI FX, or audio effects on the arrangement.");
}

}
