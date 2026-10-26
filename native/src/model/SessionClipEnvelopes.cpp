#include "SessionInternal.h"
#include "infra/ClipEnvelopeDevice.h"
#include "infra/ClipPitchDevice.h"
#include <algorithm>
#include <cmath>

// An audio clip's own envelopes: Live's clip envelopes, for Gain, Pan and
// Pitch (dsp/ClipEnvelope.h). Serves AudioClipPanel.
//
// They are the clip's, not its track's. Each one is a child of the clip's own
// state, so it rides every path a clip already travels - a move, a split, an
// undo, a saved document - and the region copy carries it by value. Its points
// are in seconds of the clip's content rather than of the song, so trimming a
// clip's start or splitting it leaves every point on the sound it was drawn
// against.
//
// The engine plays them, live and in a render alike, through hidden devices at
// the head of the clip's own plugin list: Clip Envelope for Gain and Pan, and
// Clip Pitch for Pitch (devices/infra/). A clip carries a device only
// while it has an envelope for it to play, so a clip with none costs nothing.
// The devices' parameters follow engine curves written here in edit time from
// the clip's envelopes, the way the track lanes are mirrored
// (SessionAutomation.cpp): outside the undo history, rewritten whenever a
// change may have moved the clip, and left alone when nothing did.

namespace rhino
{
// On the clip's state, one per envelope that has points: its kind, and its
// points as the track lanes keep theirs.
const juce::Identifier clipEnvelopeID {"rhinoClipEnvelope"};
const juce::Identifier clipEnvelopeKindID {"kind"};
// On the state of each hidden device, so it is told apart from an effect
// dropped on the clip: "level" for Clip Envelope, "pitch" for Clip Pitch.
const juce::Identifier clipEnvelopeDeviceID {"rhinoClipEnvelopeDevice"};
// A clip's fades, in seconds, while the clip has a Pitch envelope: then they
// are Rhino's to keep and Clip Envelope plays them, ahead of the shifter, and
// the engine's own are zero. The engine fades a clip after the clip's plugins
// and allows for none of their delay, so with Clip Pitch on the clip its fades
// fell a third of a second early and cut off the clip's last third of a
// second. Present exactly while the fades are held here.
const juce::Identifier heldFadeInID {"rhinoFadeIn"};
const juce::Identifier heldFadeOutID {"rhinoFadeOut"};

namespace
{
const juce::String levelRole {"level"};
const juce::String pitchRole {"pitch"};

bool fadesHeld(const te::AudioClipBase& clip)
{
    return clip.state.hasProperty(heldFadeInID) || clip.state.hasProperty(heldFadeOutID);
}

// The engine's rule for two fades on one clip (AudioClipBase::getFadeIn at
// 4536d8a): neither longer than the clip, and two that would overlap both
// shortened in proportion until they meet.
std::pair<double, double> fitFades(double length, double in, double out)
{
    length = std::max(0.0, length);
    in = juce::jlimit(0.0, length, std::isfinite(in) ? in : 0.0);
    out = juce::jlimit(0.0, length, std::isfinite(out) ? out : 0.0);
    if (in + out > length && in + out > 0.0)
    {
        const auto scale = length / (in + out);
        in *= scale;
        out *= scale;
    }
    return {in, out};
}

std::pair<double, double> heldFades(const te::AudioClipBase& clip)
{
    return fitFades(clip.getPosition().time.getLength().inSeconds(),
                    static_cast<double>(clip.state.getProperty(heldFadeInID, 0.0)),
                    static_cast<double>(clip.state.getProperty(heldFadeOutID, 0.0)));
}

// Hands the clip's fades to Rhino or back to the engine, whichever its Pitch
// envelope calls for, through the caller's transaction.
void placeFades(te::WaveAudioClip& clip, bool hold)
{
    auto* undoManager = &clip.edit.getUndoManager();
    if (hold && !fadesHeld(clip))
    {
        clip.state.setProperty(heldFadeInID, clip.getFadeIn().inSeconds(), undoManager);
        clip.state.setProperty(heldFadeOutID, clip.getFadeOut().inSeconds(), undoManager);
        clip.setFadeIn({});
        clip.setFadeOut({});
    }
    else if (!hold && fadesHeld(clip))
    {
        const auto in = clipFadeInSeconds(clip), out = clipFadeOutSeconds(clip);
        clip.state.removeProperty(heldFadeInID, undoManager);
        clip.state.removeProperty(heldFadeOutID, undoManager);
        clip.setFadeIn(tracktion::core::TimeDuration::fromSeconds(in));
        clip.setFadeOut(tracktion::core::TimeDuration::fromSeconds(out));
    }
}

juce::ValueTree envelopeState(const juce::ValueTree& clip, ClipEnvelopeKind kind)
{
    const juce::String id {clipEnvelopeId(kind)};
    for (int i = 0; i < clip.getNumChildren(); ++i)
    {
        const auto child = clip.getChild(i);
        if (child.hasType(clipEnvelopeID) && child.getProperty(clipEnvelopeKindID).toString() == id)
            return child;
    }
    return {};
}

// Finite, at or after the content's start, inside the envelope's range and in
// time order. Stable, so two points at one moment stay as given: a step.
void tidy(std::vector<Session::AutomationPoint>& points, ClipEnvelopeKind kind)
{
    points.erase(std::remove_if(points.begin(), points.end(), [](const auto& point)
    {
        return !std::isfinite(point.timeSeconds) || !std::isfinite(point.value);
    }), points.end());
    for (auto& point : points)
    {
        point.timeSeconds = std::max(0.0, point.timeSeconds);
        point.value = clampClipEnvelopeValue(kind, point.value);
    }
    std::stable_sort(points.begin(), points.end(),
                     [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
}

std::vector<Session::AutomationPoint> readEnvelope(const juce::ValueTree& clip, ClipEnvelopeKind kind)
{
    std::vector<Session::AutomationPoint> points;
    const auto state = envelopeState(clip, kind);
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto point = state.getChild(i);
        if (point.hasType(automationPointID))
            points.push_back({static_cast<double>(point.getProperty(automationTimeID, 0.0)),
                              static_cast<float>(point.getProperty(automationValueID, 0.0))});
    }
    tidy(points, kind);
    return points;
}

// Through `undoManager`, so the caller's transaction holds the change. An
// envelope left with no points takes its child with it, so a clip whose
// envelopes are all cleared is the clip it was before any were drawn.
void writeEnvelope(juce::ValueTree clip, ClipEnvelopeKind kind, const std::vector<Session::AutomationPoint>& points,
                   juce::UndoManager* undoManager)
{
    auto state = envelopeState(clip, kind);
    if (points.empty())
    {
        if (state.isValid())
            clip.removeChild(state, undoManager);
        return;
    }
    if (!state.isValid())
    {
        state = juce::ValueTree(clipEnvelopeID);
        state.setProperty(clipEnvelopeKindID, juce::String(clipEnvelopeId(kind)), nullptr);
        clip.addChild(state, -1, undoManager);
    }
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild(i).hasType(automationPointID))
            state.removeChild(i, undoManager);
    for (const auto& point : points)
    {
        juce::ValueTree node(automationPointID);
        node.setProperty(automationTimeID, point.timeSeconds, nullptr);
        node.setProperty(automationValueID, point.value, nullptr);
        state.addChild(node, -1, undoManager);
    }
}

te::Plugin* envelopeDevice(te::PluginList& list, const juce::String& role)
{
    for (auto* plugin : list)
        if (plugin != nullptr && plugin->state.getProperty(clipEnvelopeDeviceID).toString() == role)
            return plugin;
    return nullptr;
}

// Leaves a curve that already says this alone: the mirror runs after every
// change to the document, and nearly every one of them moved no clip.
void writeCurve(te::AutomatableParameter& parameter, const std::vector<Session::AutomationPoint>& points)
{
    auto& curve = parameter.getCurve();
    auto same = curve.getNumPoints() == static_cast<int>(points.size());
    for (int i = 0; same && i < curve.getNumPoints(); ++i)
        same = std::abs(curve.getPointTime(i).inSeconds() - points[static_cast<size_t>(i)].timeSeconds) < 1.0e-9
            && curve.getPointValue(i) == points[static_cast<size_t>(i)].value;
    if (same)
        return;
    curve.clear(nullptr);
    for (const auto& point : points)
        curve.addPoint(tracktion::core::TimePosition::fromSeconds(point.timeSeconds), point.value, 0.0f, nullptr);
}
}

ClipEnvelopePoints clipEnvelopesOf(const te::WaveAudioClip& clip)
{
    ClipEnvelopePoints envelopes;
    for (int kind = 0; kind < clipEnvelopeKindCount; ++kind)
        envelopes[static_cast<size_t>(kind)] = readEnvelope(clip.state, static_cast<ClipEnvelopeKind>(kind));
    return envelopes;
}

void writeClipEnvelopes(te::WaveAudioClip& clip, const ClipEnvelopePoints& envelopes, juce::UndoManager* undoManager)
{
    for (int kind = 0; kind < clipEnvelopeKindCount; ++kind)
    {
        auto points = envelopes[static_cast<size_t>(kind)];
        tidy(points, static_cast<ClipEnvelopeKind>(kind));
        writeEnvelope(clip.state, static_cast<ClipEnvelopeKind>(kind), points, undoManager);
    }
    syncClipEnvelopeDevices(clip);
}

bool isClipEnvelopeDevice(const te::Plugin& plugin)
{
    return plugin.state.hasProperty(clipEnvelopeDeviceID);
}

double clipFadeInSeconds(const te::AudioClipBase& clip)
{
    return fadesHeld(clip) ? heldFades(clip).first : clip.getFadeIn().inSeconds();
}

double clipFadeOutSeconds(const te::AudioClipBase& clip)
{
    return fadesHeld(clip) ? heldFades(clip).second : clip.getFadeOut().inSeconds();
}

void setClipFades(te::WaveAudioClip& clip, std::optional<double> in, std::optional<double> out)
{
    if (!fadesHeld(clip))
    {
        if (in.has_value())
            clip.setFadeIn(tracktion::core::TimeDuration::fromSeconds(*in));
        if (out.has_value())
            clip.setFadeOut(tracktion::core::TimeDuration::fromSeconds(*out));
        return;
    }
    // Stored as the engine stores its own, both as set, and fitted to the
    // clip whenever they are read.
    auto* undoManager = &clip.edit.getUndoManager();
    const auto length = clip.getPosition().time.getLength().inSeconds();
    const auto [fittedIn, fittedOut] = fitFades(length, in.value_or(clipFadeInSeconds(clip)),
                                                out.value_or(clipFadeOutSeconds(clip)));
    clip.state.setProperty(heldFadeInID, fittedIn, undoManager);
    clip.state.setProperty(heldFadeOutID, fittedOut, undoManager);
}

void clearHeldFadesAtCut(te::Clip& left, te::Clip* right)
{
    auto* undoManager = &left.edit.getUndoManager();
    if (auto* audio = dynamic_cast<te::AudioClipBase*>(&left); audio != nullptr && fadesHeld(*audio))
        left.state.setProperty(heldFadeOutID, 0.0, undoManager);
    if (auto* audio = dynamic_cast<te::AudioClipBase*>(right); audio != nullptr && fadesHeld(*audio))
        right->state.setProperty(heldFadeInID, 0.0, undoManager);
}

void lendHeldFadesToEngine(te::WaveAudioClip& clip, bool lend)
{
    if (!fadesHeld(clip))
        return;
    // Outside the undo history: a render borrows them and gives them back
    // before anything is recorded.
    const auto [in, out] = heldFades(clip);
    clip.state.setProperty(te::IDs::fadeIn, lend ? in : 0.0, nullptr);
    clip.state.setProperty(te::IDs::fadeOut, lend ? out : 0.0, nullptr);
}

bool syncClipEnvelopeDevices(te::WaveAudioClip& clip)
{
    auto* list = clip.getPluginList();
    if (list == nullptr)
        return false;
    const auto has = [&clip](ClipEnvelopeKind kind) { return envelopeState(clip.state, kind).isValid(); };
    const auto wantPitch = has(ClipEnvelopeKind::pitch);
    // Clip Envelope plays the fades whenever Clip Pitch is on the clip.
    const auto wantLevel = has(ClipEnvelopeKind::gain) || has(ClipEnvelopeKind::pan) || wantPitch;
    placeFades(clip, wantPitch);
    auto changed = false;

    // Gain and Pan first in the list, so they shape the clip's own sound
    // before any effect dropped on it, as the clip's knobs do.
    if (auto* level = envelopeDevice(*list, levelRole); level != nullptr && !wantLevel)
    {
        level->deleteFromParent();
        changed = true;
    }
    else if (level == nullptr && wantLevel)
    {
        if (auto plugin = clip.edit.getPluginCache().createNewPlugin(ClipEnvelopeDevice::xmlTypeName, {}))
        {
            plugin->state.setProperty(clipEnvelopeDeviceID, levelRole, nullptr);
            list->insertPlugin(plugin, 0, nullptr);
            changed = true;
        }
    }

    // Then the pitch, through the stretcher an unwarped clip's own Pitch knob
    // goes through, which lands a pitched hit where it was.
    if (auto* pitch = envelopeDevice(*list, pitchRole); pitch != nullptr && !wantPitch)
    {
        pitch->deleteFromParent();
        changed = true;
    }
    else if (pitch == nullptr && wantPitch)
    {
        if (auto plugin = clip.edit.getPluginCache().createNewPlugin(ClipPitchDevice::xmlTypeName, {}))
        {
            plugin->state.setProperty(clipEnvelopeDeviceID, pitchRole, nullptr);
            list->insertPlugin(plugin, envelopeDevice(*list, levelRole) != nullptr ? 1 : 0, nullptr);
            changed = true;
        }
    }
    return changed;
}

std::vector<Session::AutomationPoint> clipEnvelopeInEditTime(const te::WaveAudioClip& clip, ClipEnvelopeKind kind)
{
    const auto points = readEnvelope(clip.state, kind);
    if (points.empty())
        return {};
    const auto position = clip.getPosition();
    const auto start = position.time.getStart().inSeconds();
    const auto length = position.time.getLength().inSeconds();
    const auto offset = position.offset.inSeconds();
    const auto neutral = clipEnvelopeRange(kind).neutral;
    // Its value at either edge of the span the clip plays, and every point in
    // between: the line inside the clip exactly, and nothing outside it. A
    // step on an edge belongs to the side the clip plays - the start takes
    // what follows it, the end what led up to it - or a clip cut on a step
    // would ramp across its whole length to the far side of it.
    std::vector<Session::AutomationPoint> curve;
    curve.push_back({start, clipEnvelopeValueAt(points, offset, neutral)});
    for (const auto& point : points)
        if (point.timeSeconds > offset && point.timeSeconds < offset + length)
            curve.push_back({start + point.timeSeconds - offset, point.value});
    curve.push_back({start + length, clipEnvelopeValueBefore(points, offset + length, neutral)});
    return curve;
}

void mirrorClipEnvelopes(te::WaveAudioClip& clip)
{
    auto* list = clip.getPluginList();
    if (list == nullptr)
        return;
    if (auto* level = dynamic_cast<ClipEnvelopeDevice*>(envelopeDevice(*list, levelRole)))
    {
        writeCurve(*level->gain, clipEnvelopeInEditTime(clip, ClipEnvelopeKind::gain));
        writeCurve(*level->pan, clipEnvelopeInEditTime(clip, ClipEnvelopeKind::pan));
        // The fades it holds, as the engine draws its own: straight lines
        // from silence at the clip's start and to silence at its end.
        std::vector<Session::AutomationPoint> fades;
        if (fadesHeld(clip))
        {
            const auto time = clip.getPosition().time;
            const auto start = time.getStart().inSeconds(), end = time.getEnd().inSeconds();
            const auto [in, out] = heldFades(clip);
            if (in > 0.0 || out > 0.0)
            {
                fades.push_back({start, in > 0.0 ? 0.0f : 1.0f});
                if (in > 0.0)
                    fades.push_back({start + in, 1.0f});
                fades.push_back({end - out, 1.0f});
                if (out > 0.0)
                    fades.push_back({end, 0.0f});
            }
        }
        writeCurve(*level->fade, fades);
    }
    // The engine reads a delayed plugin's curve as early as the plugin delays
    // its sound, so the Pitch curve is written that much early: the shift
    // then meets the audio it was drawn over (ClipPitchDevice.h). The line
    // before the song's start is what it is at the start.
    if (auto* pitch = dynamic_cast<ClipPitchDevice*>(envelopeDevice(*list, pitchRole)))
    {
        auto points = clipEnvelopeInEditTime(clip, ClipEnvelopeKind::pitch);
        for (auto& point : points)
            point.timeSeconds -= ClipPitchDevice::latencySeconds;
        if (!points.empty() && points.front().timeSeconds < 0.0)
        {
            const auto atZero = clipEnvelopeValueBefore(points, 0.0, points.front().value);
            points.erase(points.begin(), std::find_if(points.begin(), points.end(),
                                                      [](const auto& point) { return point.timeSeconds >= 0.0; }));
            points.insert(points.begin(), {0.0, atZero});
        }
        writeCurve(*pitch->semitones, points);
    }
}

void retimeClipEnvelopes(te::WaveAudioClip& clip, double scale, double shift, juce::UndoManager* undoManager)
{
    if (!std::isfinite(scale) || !std::isfinite(shift) || (std::abs(scale - 1.0) < 1.0e-12 && shift == 0.0))
        return;
    for (int kind = 0; kind < clipEnvelopeKindCount; ++kind)
    {
        auto state = envelopeState(clip.state, static_cast<ClipEnvelopeKind>(kind));
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto point = state.getChild(i);
            if (!point.hasType(automationPointID))
                continue;
            const auto seconds = static_cast<double>(point.getProperty(automationTimeID, 0.0));
            if (std::isfinite(seconds))
                point.setProperty(automationTimeID, std::max(0.0, seconds * scale + shift), undoManager);
        }
    }
}

std::vector<Session::AutomationPoint> Session::clipEnvelope(te::EditItemID id, ClipEnvelope kind) const
{
    auto* clip = findAudioClip(id);
    return clip != nullptr ? readEnvelope(clip->state, kind) : std::vector<AutomationPoint>();
}

std::array<bool, Session::clipEnvelopeCount> Session::clipEnvelopesInUse(te::EditItemID id) const
{
    std::array<bool, clipEnvelopeCount> inUse {};
    if (auto* clip = findAudioClip(id))
        for (int kind = 0; kind < clipEnvelopeCount; ++kind)
            inUse[static_cast<size_t>(kind)] = envelopeState(clip->state, static_cast<ClipEnvelope>(kind)).isValid();
    return inUse;
}

juce::Result Session::setClipEnvelope(te::EditItemID id, ClipEnvelope kind, std::vector<AutomationPoint> points)
{
    tidy(points, kind);
    auto playbackChanged = false;
    const auto result = applyAudioClipEdit(id, juce::String(clipEnvelopeName(kind)) + " envelope",
                                           [this, kind, &points, &playbackChanged](te::WaveAudioClip& clip)
    {
        writeEnvelope(clip.state, kind, points, &edit->getUndoManager());
        playbackChanged = syncClipEnvelopeDevices(clip);
        // Heard now, even in the middle of a drag, which announces nothing
        // until it ends.
        mirrorClipEnvelopes(clip);
    });
    // A device arriving on the clip or leaving it is a change to the graph,
    // and the pitch shifter's delay is one compensation has to hear about.
    if (playbackChanged && edit->getTransport().isPlaying())
        edit->restartPlayback();
    return result;
}

juce::Result Session::clearClipEnvelope(te::EditItemID id, ClipEnvelope kind)
{
    return setClipEnvelope(id, kind, {});
}

void Session::mirrorClipEnvelopesToEngine()
{
    // Only a clip with a plugin can have a device to drive, which spares the
    // thousands of clips with none any further look.
    for (auto* track : audioTracks())
        for (auto* clip : track->getClips())
            if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip);
                audio != nullptr && audio->getPluginList() != nullptr && audio->getPluginList()->size() > 0)
                mirrorClipEnvelopes(*audio);
}

}
