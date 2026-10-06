#include "ArrangementInternal.h"
#include "BrowserIds.h"
#include "DevicePreset.h"
#include <optional>
#include <set>

// File drops from the OS and item drops from the browser.

namespace rhino
{
namespace
{
bool isSupportedAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aiff" || extension == ".aif"
        || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
}

// What kind of track a browser item needs, when the drop lands past the last
// lane and one has to be made for it. The lane is made for what is arriving
// rather than always for audio, because a track cannot change kind afterwards.
// Nothing is returned for an item that does not land on a lane at all.
std::optional<Session::TrackType> trackTypeForDropKind(const juce::String& kind)
{
    if (kind == "preset" || kind == "instrument" || kind == "midi-effect" || kind == "drumkit"
        || kind == "drum-preset")
        return Session::TrackType::midi;
    if (kind == "sample" || kind == "file" || kind == "effect")
        return Session::TrackType::audio;
    return {};
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
    std::vector<juce::File> audio;
    for (const auto& path : files)
        if (const auto file = juce::File(path); isSupportedAudioFile(file))
            audio.push_back(file);
    // A drum track takes each file onto its next empty pad, as it takes a
    // sample from the browser.
    if (session.trackHasDrumRack(targetTrack))
    {
        for (const auto& file : audio)
            if (const auto result = session.addDrumSound(file, targetTrack); result.failed())
            {
                if (status) status(result.getErrorMessage());
                return;
            }
        if (status && !audio.empty())
            status("Added " + (audio.size() == 1 ? audio.front().getFileNameWithoutExtension()
                                                 : juce::String(static_cast<int>(audio.size())) + " sounds")
                   + " to the Drum Rack on " + session.trackName(targetTrack));
        return;
    }
    // The lane under the pointer is the lane they land on, end to end from
    // the drop point. Nudging a drop off track 0 used to hide that a MIDI lane
    // takes no audio by quietly using the next lane down instead.
    const auto result = session.importAudioFilesAt(audio, targetTrack,
                                                   snapped(std::max(0.0, timeAt(static_cast<float>(x))), false));
    if (result.failed() && status) status(result.getErrorMessage());
    fit();
}

bool Arrangement::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString().startsWith("rhino-browser:");
}

void Arrangement::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    auto targetTrack = trackAt(static_cast<float>(details.localPosition.y));
    const auto description = details.description.toString();
    const auto kind = browserDropKind(description);
    if (kind == "effect")
        if (const auto clipIndex = hit(details.localPosition.toFloat()); clipIndex >= 0)
        {
            const auto* effect = deviceFromId(browserDropId(description), DeviceKind::AudioEffect);
            if (effect == nullptr)
            {
                if (status) status("That browser item cannot be inserted here.");
                return;
            }
            // A copy: adding the device is announced, and the clip list is
            // rebuilt before the fallback below reads it.
            const auto clip = clips[static_cast<size_t>(clipIndex)];
            const auto result = session.addClipDevice(effect->id, clip.id);
            if (result.failed())
            {
                const auto trackResult = session.addDevice(effect->id, clip.track);
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
    if (targetTrack < 0
        && session.trackCount() > 0
        && static_cast<float>(details.localPosition.y) > lane(session.trackCount() - 1).getBottom())
    {
        // A preset needs the lane its device would.
        const auto* presetDevice = deviceForPresetDrop(description);
        if (const auto wanted = trackTypeForDropKind(presetDevice != nullptr ? deviceDropKind(*presetDevice) : kind))
        {
            const auto result = session.addTrack(*wanted);
            if (result.failed())
            {
                if (status) status(result.getErrorMessage());
                return;
            }
            targetTrack = session.trackCount() - 1;
        }
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

    // The main track carries effects and nothing else, a preset of one
    // included.
    const auto* presetDevice = deviceForPresetDrop(description);
    const auto carriesEffect = kind == "effect"
        || (presetDevice != nullptr && presetDevice->kind == DeviceKind::AudioEffect);
    if (session.isMasterTrack(track) && !carriesEffect && kind != "info")
        return juce::Result::fail("The main track takes audio effects only.");

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
        const auto* effect = deviceFromId(id, DeviceKind::AudioEffect);
        if (effect == nullptr) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop audio effects on a track or clip.");
        const auto result = session.addDevice(effect->id, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added browser effect to " + session.trackName(track) + " Device View");
        return juce::Result::ok();
    }

    if (kind == "instrument")
    {
        const auto* instrument = deviceFromId(id, DeviceKind::Instrument);
        if (instrument == nullptr) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop instruments on a track or clip.");
        // An instrument drop changes what the track is, never its clips. The
        // track's existing notes stay and play through the new instrument.
        juce::ignoreUnused(startSeconds, insertPreset);
        const auto result = session.addDevice(instrument->id, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Track " + juce::String(track + 1) + " now runs " + session.trackName(track)
                           + ". Double-click the lane to add a clip.");
        return juce::Result::ok();
    }

    if (kind == "drumkit")
    {
        const auto kit = browserDropKitFile(description);
        if (kit == juce::File()) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop drum kits on a track.");
        const auto result = session.addDrumKit(kit, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Track " + juce::String(track + 1) + " now runs " + session.trackName(track)
                           + ". Double-click the lane to add a clip.");
        return juce::Result::ok();
    }

    // A drum sound on a lane fills the first empty pad of the lane's Drum
    // Rack, bringing a blank rack with it when the track has none.
    if (kind == "drum-preset")
    {
        const auto sound = browserDropDrumPresetFile(description);
        if (sound == juce::File()) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop drum sounds on a track.");
        const auto result = session.addDrumSound(sound, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added " + sound.getFileNameWithoutExtension() + " to the Drum Rack on "
                           + session.trackName(track));
        return juce::Result::ok();
    }

    if (kind == "midi-effect")
    {
        const auto* effect = deviceFromId(id, DeviceKind::MidiEffect);
        if (effect == nullptr) return juce::Result::fail("That browser item cannot be inserted here.");
        if (track < 0) return juce::Result::fail("Drop MIDI FX on an instrument track.");
        const auto result = session.addDevice(effect->id, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added MIDI FX to " + session.trackName(track) + " Device View");
        return juce::Result::ok();
    }

    // A preset adds the device it is for, already set; an instrument the
    // track already runs takes the preset instead.
    if (kind == "device-preset")
    {
        const auto file = browserDropPresetFile(description);
        if (track < 0) return juce::Result::fail("Drop presets on a track.");
        const auto result = session.addDeviceFromPreset(file, track);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Loaded " + DevicePreset::nameOf(file) + " on " + session.trackName(track));
        return juce::Result::ok();
    }

    if (kind == "file")
    {
        const auto file = browserDropFile(description);
        if (file == juce::File() || !file.existsAsFile())
            return juce::Result::fail("That library file is missing.");
        // Any track index is offered: importAudioAt is what knows an audio
        // clip cannot share a track with an instrument, and says so.
        if (track < 0) return juce::Result::fail("Drop audio on a track.");
        // A drum track takes a sample onto its next empty pad instead.
        if (session.trackHasDrumRack(track))
        {
            const auto result = session.addDrumSound(file, track);
            if (result.failed()) return result;
            selectTrack(track);
            if (status) status("Added " + file.getFileNameWithoutExtension() + " to the Drum Rack on "
                               + session.trackName(track));
            return juce::Result::ok();
        }
        const auto result = session.importAudioAt(file, track, startSeconds);
        if (result.failed()) return result;
        selectTrack(track);
        if (status) status("Added " + file.getFileNameWithoutExtension() + " to " + session.trackName(track));
        return juce::Result::ok();
    }

    if (kind == "sample")
    {
        const auto sample = builtInSampleFromId(id);
        if (!sample) return juce::Result::fail("That browser sample cannot be inserted here.");
        // Any lane is offered: importAudioAt is what knows which lanes take
        // audio, and says so in the same words wherever the sample came from.
        if (track < 0) return juce::Result::fail("Drop audio samples on an audio track.");
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
