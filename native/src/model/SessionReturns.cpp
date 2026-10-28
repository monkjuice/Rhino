#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Return tracks and the sends that feed them.
//
// A return is Live's: a track whose input is the sum of what the other tracks
// send it, so one delay or one reverb serves the whole song and every track
// chooses how much of itself to give it. The engine has both halves already -
// an aux send plugin that copies a chain's audio onto a numbered bus, and an
// aux return plugin that adds a bus into a chain - so a return is an ordinary
// te::AudioTrack carrying an aux return at the head of its chain, and every
// other track carries one send per return at the very end of its own, after
// the fader, where Live's post-fader sends sit. Being a track is the point,
// as it is for a group's bus: the mixer, the device rack, automation, renaming,
// colour, Delete and the card all reach a return with no second code path.
//
// What a return refuses is what makes it one. It takes audio effects only, no
// clips, no input and no DJ deck, and it sends to no other return, so no bus
// can ever feed itself. Returns sit together at the foot of the stack, under
// every other track and over the main row, as Live keeps them, and a lane made
// for a clip arriving past the last track is made above them.
//
// The structure is kept the way the channel strip is: ensureReturnRouting,
// run with ensureTrackMixers by every path that makes a track and as a
// document opens, gives each return its way in and each other track exactly
// one send to each return, a new send at nothing, and takes away a send whose
// return has gone. A send is the track's own state, so it travels with the
// track through a copy and undo brings it back with the track.

namespace rhino
{
namespace
{
// Live's limit, and the letters a return is called by: A to L.
constexpr int maximumReturnTracks = 12;

bool isReturnState(const te::AudioTrack& track)
{
    return track.state.hasProperty(trackReturnBusID);
}

int busOf(const te::AudioTrack& track)
{
    return static_cast<int>(track.state.getProperty(trackReturnBusID, -1));
}

std::vector<te::AuxSendPlugin*> sendsOn(te::AudioTrack& track)
{
    std::vector<te::AuxSendPlugin*> sends;
    for (auto* plugin : track.pluginList)
        if (auto* send = dynamic_cast<te::AuxSendPlugin*>(plugin))
            sends.push_back(send);
    return sends;
}

te::AuxReturnPlugin* wayInOf(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* wayIn = dynamic_cast<te::AuxReturnPlugin*>(plugin))
            return wayIn;
    return nullptr;
}

bool anyReturnSoloed(te::Edit& edit)
{
    for (auto* track : te::getAudioTracks(edit))
        if (isReturnState(*track) && track->isSolo(false))
            return true;
    return false;
}
}

// A send feeds its return while its track can be heard - a muted track sends
// nothing, nor does one another track's solo leaves out, which is what makes
// a soloed track sound with its delay and reverb and nothing else's. A solo on
// a return is the exception: it is asked for to hear what the tracks send it,
// so every send whose track is not muted goes on feeding the returns. The
// engine reads this as it builds the graph; followReturnSolo rebuilds it when
// the answer would change.
bool RhinoEngineBehaviour::shouldProcessAuxSendWhenTrackIsMuted(te::AuxSendPlugin& send)
{
    if (!anyReturnSoloed(send.edit))
        return false;
    auto* owner = send.getOwnerTrack();
    return owner != nullptr && !owner->isMuted(true);
}

bool Session::isReturnTrack(int track) const
{
    auto* audio = audioTrack(track);
    return audio != nullptr && isReturnState(*audio);
}

// The block at the foot of the stack. Every path that makes or moves a track
// keeps the returns there, so the trailing run is all of them.
int Session::firstReturnTrack() const
{
    const auto& tracks = cachedTracks();
    auto first = tracks.size();
    while (first > 0 && isReturnState(*tracks.getUnchecked(first - 1)))
        --first;
    return first;
}

std::vector<Session::ReturnTrack> Session::returnTracks() const
{
    std::vector<ReturnTrack> returns;
    const auto& tracks = cachedTracks();
    for (int track = firstReturnTrack(); track < tracks.size(); ++track)
    {
        const auto* audio = tracks.getUnchecked(track);
        ReturnTrack info;
        info.track = track;
        info.letter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + static_cast<int>(returns.size())));
        info.name = audio->getName();
        info.colour = audio->getColour();
        returns.push_back(info);
    }
    return returns;
}

bool Session::carriesOtherTracks(int track) const
{
    return isGroupBusTrack(track) || isReturnTrack(track);
}

juce::String Session::noClipsReason(int track) const
{
    return isReturnTrack(track) ? "A return track plays what the sends feed it, so it takes no clips."
                                : "A group track carries its members' audio, so it takes no clips.";
}

juce::String Session::audioEffectsOnlyReason(int track) const
{
    return isReturnTrack(track) ? "A return track takes audio effects only."
                                : "A group track takes audio effects only.";
}

// Every return its way in, at the head of its chain and on its own bus, and
// every other track one send to each return, at the end of its chain, after
// the fader and the meter. A send made here starts at nothing: a track sends
// nothing until someone turns it up. A send whose return has gone, a second
// send to one bus, and a send on a return - which could feed itself - are
// taken away. Inside the caller's transaction, as the channel strip is.
void Session::ensureReturnRouting()
{
    const auto tracks = audioTracks();
    std::set<int> buses;
    for (auto* track : tracks)
    {
        if (!isReturnState(*track))
            continue;
        const auto bus = busOf(*track);
        buses.insert(bus);
        if (auto* wayIn = wayInOf(*track))
        {
            if (wayIn->busNumber.get() != bus)
                wayIn->busNumber = bus;
        }
        else
        {
            juce::ValueTree state(te::IDs::PLUGIN);
            state.setProperty(te::IDs::type, juce::String(te::AuxReturnPlugin::xmlTypeName), nullptr);
            state.setProperty(te::IDs::busNum, bus, nullptr);
            if (auto plugin = edit->getPluginCache().createNewPlugin(state))
                track->pluginList.insertPlugin(plugin, djInputEnd(*track), nullptr);
        }
        for (auto* send : sendsOn(*track))
            send->removeFromParent();
        // Heard while another track is soloed, as Live's returns are; a solo
        // on a return itself is followReturnSolo's business.
        if (!track->isSoloIsolate(false) && !anyReturnSoloed(*edit))
            track->setSoloIsolate(true);
    }
    for (auto* track : tracks)
    {
        if (isReturnState(*track))
            continue;
        std::set<int> sent;
        for (auto* send : sendsOn(*track))
            if (!buses.contains(send->busNumber.get()) || !sent.insert(send->busNumber.get()).second)
                send->removeFromParent();
        for (const auto bus : buses)
        {
            if (sent.contains(bus))
                continue;
            auto state = te::AuxSendPlugin::create();
            state.setProperty(te::IDs::busNum, bus, nullptr);
            // The fader's position, and zero is silence: the engine's own
            // default for a new send is 0 dB, which would put every track
            // into every return the moment the return was made.
            state.setProperty(te::IDs::auxSendSliderPos, 0.0f, nullptr);
            if (auto plugin = edit->getPluginCache().createNewPlugin(state))
                track->pluginList.insertPlugin(plugin, track->pluginList.size(), nullptr);
        }
    }
}

// A return lands at the foot of the returns, on the lowest bus none of them
// is using, and brings a send to it from every other track. It gets the
// strip every track made here has - a Utility, the DJ ends, a fader and a
// meter - with its way in at the head. No transaction of its own.
te::AudioTrack* Session::makeReturnTrack(const juce::String& name)
{
    std::set<int> used;
    for (auto* track : audioTracks())
        if (isReturnState(*track))
            used.insert(busOf(*track));
    auto bus = 0;
    while (used.contains(bus))
        ++bus;
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (track == nullptr)
        return nullptr;
    track->setName(name);
    track->setColour(pickTrackColour());
    track->state.setProperty(trackReturnBusID, bus, &edit->getUndoManager());
    track->pluginList.insertPlugin(edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {}), 0, nullptr);
    ensureSceneSlots();
    ensureTrackMixers();
    return track.get();
}

juce::Result Session::addReturnTrack(int* added)
{
    if (static_cast<int>(returnTracks().size()) >= maximumReturnTracks)
        return juce::Result::fail("A song has twelve return tracks at most, A to L.");
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Add return track");
    if (makeReturnTrack("Return") == nullptr)
    {
        undoManager.undoCurrentTransactionOnly();
        return juce::Result::fail("Could not create a return track.");
    }
    closeUndoStep();
    if (added != nullptr)
        *added = trackCount() - 1;
    // Every track gained a send, which is a device of its chain.
    markModified(-1, ModelChange::devices);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// The two a new document starts with, Live's pair the other way round: a
// delay on A and a reverb on B, each wholly wet, since what comes back from a
// return is added to the dry track rather than replacing it. The delay's time
// is not a control the rack shows, so it starts on a dotted eighth at the
// starter tempo rather than the engine's 150 ms slapback. Both open folded to
// their one line, leaving the room to the tracks; the arrow opens either.
void Session::addStarterReturns()
{
    if (auto* delayReturn = makeReturnTrack("Delay"))
    {
        delayReturn->state.setProperty(trackFoldedID, true, nullptr);
        if (auto plugin = edit->getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, {}))
        {
            delayReturn->pluginList.insertPlugin(plugin, chainHeadEnd(*delayReturn), nullptr);
            if (auto* delay = dynamic_cast<te::DelayPlugin*>(plugin.get()))
            {
                setPluginParameter(delay->mixProportion, 1.0f);
                delay->lengthMs = juce::roundToInt(60000.0 / tempo() * 0.75);
            }
        }
    }
    if (auto* reverbReturn = makeReturnTrack("Reverb"))
    {
        reverbReturn->state.setProperty(trackFoldedID, true, nullptr);
        if (auto plugin = edit->getPluginCache().createNewPlugin(te::ReverbPlugin::xmlTypeName, {}))
        {
            reverbReturn->pluginList.insertPlugin(plugin, chainHeadEnd(*reverbReturn), nullptr);
            if (auto* reverb = dynamic_cast<te::ReverbPlugin*>(plugin.get()))
            {
                setPluginParameter(reverb->dryParam, 0.0f);
                setPluginParameter(reverb->roomSizeParam, 0.6f);
            }
        }
    }
}

te::AuxSendPlugin* Session::sendPlugin(int track, int returnTrack) const
{
    auto* source = audioTrack(track);
    auto* destination = audioTrack(returnTrack);
    if (source == nullptr || destination == nullptr || isReturnState(*source) || !isReturnState(*destination))
        return nullptr;
    const auto bus = busOf(*destination);
    for (auto* send : sendsOn(*source))
        if (send->busNumber.get() == bus)
            return send;
    return nullptr;
}

bool Session::hasSend(int track, int returnTrack) const
{
    return sendPlugin(track, returnTrack) != nullptr;
}

float Session::trackSendDb(int track, int returnTrack) const
{
    auto* send = sendPlugin(track, returnTrack);
    if (send == nullptr)
        return minimumSendDb;
    return juce::jlimit(minimumSendDb, maximumSendDb, send->getGainDb());
}

juce::Result Session::setTrackSendDb(int track, int returnTrack, float decibels)
{
    auto* send = sendPlugin(track, returnTrack);
    if (send == nullptr)
        return juce::Result::fail(isReturnTrack(track) ? "A return track sends to no other return."
                                                       : "That track has no send to that return.");
    if (mixerGestureDepth == 0)
        openLooseControlStep("send " + send->itemID.toString(), "Send to " + trackName(returnTrack));
    send->setGainDb(juce::jlimit(minimumSendDb, maximumSendDb, decibels));
    // A send is a parameter of the track's own chain, as its fader is.
    announceMixerChange(track, ModelChange::parameters);
    return juce::Result::ok();
}

void Session::beginTrackSendGesture(int track, int returnTrack)
{
    if (auto* send = sendPlugin(track, returnTrack))
    {
        edit->getUndoManager().beginNewTransaction("Send to " + trackName(returnTrack));
        send->gain->parameterChangeGestureBegin();
        ++mixerGestureDepth;
        updateUndoTimerHold();
    }
}

void Session::endTrackSendGesture(int track, int returnTrack)
{
    if (auto* send = sendPlugin(track, returnTrack))
        send->gain->parameterChangeGestureEnd();
    // Closed even when the track went under a held knob, as a fader's is.
    endMixerGesture();
}

// A return is heard while another track is soloed - Live's solo in place -
// by being solo-isolated, except while a return is itself soloed, when the
// others drop out as any unsoloed track does. Whether a return is soloed also
// decides how every send was built (shouldProcessAuxSendWhenTrackIsMuted), so
// the graph is built again when that changes, and when a mute changes while
// it holds, since a send reads its track's mute only as it is built then.
// The isolation is derived, so it is written outside the undo history.
void Session::followReturnSolo(bool mutesChanged)
{
    const auto soloed = anyReturnSoloed(*edit);
    for (auto* track : audioTracks())
        if (isReturnState(*track) && track->isSoloIsolate(false) == soloed)
            track->setSoloIsolate(!soloed);
    if (soloed != returnSoloHeard || (soloed && mutesChanged))
        edit->restartPlayback();
    returnSoloHeard = soloed;
}

}
