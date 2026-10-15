#include "SessionInternal.h"
#include <algorithm>

// The meters on the track cards read here.
//
// Every track carries one of the engine's level meters just behind its fader
// (ensureTrackMixers), and the playback context measures the main output
// after its own fader. A meter measures for whoever has a place on it - a
// LevelMeasurer::Client, which the audio thread writes the loudest sample
// into - so the session keeps one place on each and reads it once a frame.
//
// The engine mutes a track after its chain, meter included
// (createNodeForAudioTrack at 4536d8a puts the TrackMutingNode behind the
// plugins), so a muted track's meter goes on measuring what it would play.
// What the song can hear is the engine's own track flag, shouldBePlayed.

namespace rhino
{

// One place on a measurer. The measurer keeps a pointer to the client and
// writes into it from the audio thread, so the client comes off before it
// goes. A track's meter is held so it cannot go first; the main output's
// measurer belongs to the playback context, which is rebuilt as the device
// comes and goes, so that one is only watched.
struct Session::MeterTap
{
    MeterTap(te::LevelMeasurer& source, te::Plugin::Ptr owner) : measurer(&source), plugin(std::move(owner))
    {
        source.addClient(client);
    }
    ~MeterTap()
    {
        if (auto* source = measurer.get())
            source->removeClient(client);
    }
    OutputLevel take()
    {
        const auto left = client.getAndClearAudioLevel(0).dB;
        return {left, client.getNumChannelsUsed() > 1 ? client.getAndClearAudioLevel(1).dB : left};
    }

    juce::WeakReference<te::LevelMeasurer> measurer;
    te::Plugin::Ptr plugin;
    te::LevelMeasurer::Client client;
    juce::uint32 pass = 0;
    JUCE_DECLARE_NON_COPYABLE(MeterTap)
};

void Session::MeterTapDeleter::operator()(MeterTap* tap) const
{
    delete tap;
}

void Session::releaseMeterTaps()
{
    meterTaps.clear();
    mainMeterTap.reset();
}

std::vector<Session::OutputLevel> Session::takeOutputLevels()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    std::vector<OutputLevel> levels(static_cast<size_t>(tracks.size()) + 1);
    // A render drives the same meters behind the loading screen, and what it
    // measures is not what the song is playing. It is read all the same, so
    // that it is gone once the render is.
    const auto rendering = isBouncing() || isExporting();
    ++meterPass;
    for (int index = 0; index < tracks.size(); ++index)
    {
        auto* track = tracks[index];
        auto* meter = track->getLevelMeterPlugin();
        if (meter == nullptr)
            continue;
        // By id, so a track moved in the stack keeps its place; and checked
        // against the plugin, because an undo brings a track back as new
        // plugins under the old ids.
        auto& tap = meterTaps[meter->itemID];
        if (tap == nullptr || tap->plugin.get() != meter)
            tap.reset(new MeterTap(meter->measurer, meter));
        tap->pass = meterPass;
        const auto level = tap->take();
        if (!rendering && track->shouldBePlayed())
            levels[static_cast<size_t>(index)] = level;
    }
    // The places on meters whose tracks have gone.
    std::erase_if(meterTaps, [this](const auto& entry) { return entry.second->pass != meterPass; });
    if (auto* context = edit->getCurrentPlaybackContext())
    {
        if (mainMeterTap == nullptr || mainMeterTap->measurer.get() != &context->masterLevels)
            mainMeterTap.reset(new MeterTap(context->masterLevels, nullptr));
        const auto level = mainMeterTap->take();
        if (!rendering)
            levels.back() = level;
    }
    else
    {
        mainMeterTap.reset();
    }
    return levels;
}

}
