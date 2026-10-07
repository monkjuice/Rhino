#include "DrumRackEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// The natural log of a thousand: an envelope multiplied by e to the minus this
// over T seconds has fallen 60 dB by the end of them.
constexpr float sixtyDecibels = 6.907755279f;
// Where a sample voice's Decay envelope or release stops it: 60 dB down.
constexpr float envelopeFloor = 0.001f;
// How long a voice takes to fade when it is cut off, and how fast a voice's
// gains follow a change to its pad's level, pan, mute or solo.
constexpr double cutSeconds = 0.004;
constexpr double glideSeconds = 0.004;
// Tone's range on a sample pad: a low-pass from this corner, at the bottom of
// the knob, to the top of hearing just below the top. At the top it is gone.
constexpr float darkestTone = 120.0f;
constexpr float brightestTone = 20000.0f;
// A face's play button strikes a pad this hard, and holds a classic pad this
// long before letting go of it.
constexpr float previewVelocity = 0.8f;
constexpr double previewHoldSeconds = 1.0;
// A loop blends the last of its part into its start over this long, or over a
// quarter of a part shorter than four times it.
constexpr double loopCrossfadeSeconds = 0.01;
// A classic voice that runs off the end of its part, rather than being let
// go, is faded over a millisecond instead of cut.
constexpr double declickSeconds = 0.001;
// The shortest part of a file a pad can play, as a fraction of it.
constexpr float shortestPart = 0.001f;

bool validPad(int pad)
{
    return juce::isPositiveAndBelow(pad, DrumRackEngine::padCount);
}

float tuneRatio(float semitones)
{
    return std::pow(2.0f, semitones / 12.0f);
}

// A pad's level as a gain; the bottom of the control is silence.
float levelGain(float decibels)
{
    return decibels <= DrumRackEngine::silentLevel ? 0.0f : std::pow(10.0f, decibels / 20.0f);
}

// A balance rather than a pan law: in the middle both sides are at full level,
// so a pad plays exactly as loud as its sample, and turning it one way only
// turns the other side down.
float panLeft(float pan)
{
    return pan > 0.0f ? 1.0f - std::min(pan, 1.0f) : 1.0f;
}

float panRight(float pan)
{
    return pan < 0.0f ? 1.0f + std::max(pan, -1.0f) : 1.0f;
}

// How loud a strike is as its pad hears it. At full sensitivity a note at half
// velocity is half as loud; with none, every note is as loud as the loudest.
float strengthFor(float velocity, float sensitivity)
{
    const auto clamped = std::clamp(velocity, 0.0f, 1.0f);
    return 1.0f - std::clamp(sensitivity, 0.0f, 1.0f) * (1.0f - clamped);
}

float within(float value, float low, float high, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

// Four-point Hermite interpolation. At a whole position it returns the sample
// itself, so a pad at its file's own rate and untuned plays the file exactly.
float hermite(const std::vector<float>& data, double position) noexcept
{
    const auto length = static_cast<int>(data.size());
    const auto index = static_cast<int>(position);
    const auto at = [&data, length] (int i) { return i >= 0 && i < length ? data[static_cast<size_t>(i)] : 0.0f; };
    const auto x0 = at(index);
    const auto t = static_cast<float>(position - index);
    if (t == 0.0f)
        return x0;
    const auto xm1 = at(index - 1), x1 = at(index + 1), x2 = at(index + 2);
    const auto c = (x1 - xm1) * 0.5f;
    const auto v = x0 - x1;
    const auto w = c + v;
    const auto a = w + v + (x2 - x0) * 0.5f;
    const auto b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}
}

DrumRackEngine::Playback DrumRackEngine::Playback::clamped() const
{
    auto out = *this;
    const auto modeValue = static_cast<int>(mode);
    out.mode = modeValue >= 0 && modeValue <= static_cast<int>(PlayMode::slice) ? mode : PlayMode::oneShot;
    out.start = within(start, 0.0f, 1.0f - shortestPart, 0.0f);
    out.end = within(end, out.start + shortestPart, 1.0f, 1.0f);
    out.fadeIn = within(fadeIn, 0.0f, longestFade, 0.0f);
    out.fadeOut = within(fadeOut, 0.0f, longestFade, 0.0f);
    out.attack = within(attack, 0.0f, longestFade, 0.0f);
    out.sustain = within(sustain, 0.0f, 1.0f, 1.0f);
    out.release = within(release, 0.0f, longestRelease, 0.05f);
    const auto by = static_cast<int>(sliceBy);
    out.sliceBy = by == static_cast<int>(SliceBy::divisions) ? SliceBy::divisions : SliceBy::transients;
    out.divisions = std::clamp(divisions, 2, 64);
    out.sensitivity = within(sensitivity, 0.0f, 1.0f, 0.5f);
    return out;
}

// ---- a voice ----------------------------------------------------------------

void DrumRackEngine::Voice::startSample(const DrumSample& played, const PadSettings& settings,
                                        const Playback& playback, double outputRate, int holdFrames)
{
    source = Source::sample;
    sample = &played;
    const auto length = static_cast<double>(played.length());
    const auto last = std::max(1.0, length);
    partStart = std::clamp(std::floor(static_cast<double>(playback.start) * length), 0.0, last - 1.0);
    partEnd = std::clamp(std::ceil(static_cast<double>(playback.end) * length), partStart + 1.0, last);
    position = partStart;
    increment = played.sampleRate / outputRate * tuneRatio(settings.tune);
    classic = playback.mode == PlayMode::classic;
    // A loop blends the last moments of its part into its start, so the seam
    // is a crossfade rather than a click.
    looping = classic && playback.loop;
    crossfade = looping ? std::min(played.sampleRate * loopCrossfadeSeconds, (partEnd - partStart) * 0.25) : 0.0;
    decaying = settings.decay < fullDecay;
    envelope = 1.0f;
    envelopeFactor = static_cast<float>(std::exp(-sixtyDecibels / (std::max(0.001, double(settings.decay)) * outputRate)));
    // A one-shot falls away to nothing; a classic voice, while its key is
    // held, to its Sustain.
    sustain = classic ? playback.sustain : 0.0f;
    const auto riseSeconds = classic ? playback.attack : playback.fadeIn;
    rise = riseSeconds > 0.0f ? 0.0f : 1.0f;
    riseStep = riseSeconds > 0.0f ? static_cast<float>(1.0 / (riseSeconds * outputRate)) : 0.0f;
    fadeOutFrames = !classic ? outputRate * playback.fadeOut : looping ? 0.0 : outputRate * declickSeconds;
    held = classic;
    releasing = false;
    holdRemaining = classic ? holdFrames : -1;
    releaseGain = 1.0f;
    releaseFactor = static_cast<float>(std::exp(-sixtyDecibels / (std::max(0.001, double(playback.release)) * outputRate)));
    // A two-pole low-pass in Zavalishin's form, latched for the strike.
    filtered = settings.tone < 0.999f;
    ic1 = {};
    ic2 = {};
    if (filtered)
    {
        const auto tone = std::clamp(settings.tone, 0.0f, 1.0f);
        const auto cutoff = std::min(darkestTone * std::pow(brightestTone / darkestTone, tone),
                                     static_cast<float>(outputRate * 0.45));
        const auto g = static_cast<float>(std::tan(juce::MathConstants<double>::pi * cutoff / outputRate));
        k = 1.41421356f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
}

void DrumRackEngine::Voice::startSynth(DrumModel model, const PadSettings& settings, double outputRate,
                                       std::uint32_t seed)
{
    source = Source::synth;
    sample = nullptr;
    decaying = false;
    filtered = false;
    classic = false;
    looping = false;
    held = releasing = false;
    holdRemaining = -1;
    riseStep = 0.0f;
    rise = 1.0f;
    fadeOutFrames = 0.0;
    synth.start(model, outputRate, tuneRatio(settings.tune), settings.decay, settings.tone, seed);
}

void DrumRackEngine::Voice::letGo()
{
    if (!classic || !held)
        return;
    held = false;
    releasing = true;
}

int DrumRackEngine::Voice::render(float* left, float* right, int numSamples, float targetLeft, float targetRight,
                                  float glide) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Whatever ends a voice is asked before the sample it would make, so
        // the count returned is exactly what was played.
        if (fadeStep > 0.0f && fade <= 0.0f)
            return i;
        float l = 0.0f, r = 0.0f;
        if (source == Source::synth)
        {
            if (!synth.active())
                return i;
            l = r = synth.next();
        }
        else
        {
            if (sample == nullptr)
                return i;
            if (position >= partEnd)
            {
                if (!looping)
                    return i;
                // Past the seam the voice carries on from where the crossfade
                // has already taken it.
                position = partStart + crossfade + (position - partEnd);
                if (position >= partEnd)
                    position = partStart;
            }
            if ((decaying && sustain <= 0.0f && envelope < envelopeFloor) || (releasing && releaseGain < envelopeFloor))
                return i;
            l = hermite(sample->left, position);
            r = sample->stereo() ? hermite(sample->right, position) : l;
            if (crossfade > 0.0 && position > partEnd - crossfade)
            {
                const auto into = position - (partEnd - crossfade);
                const auto t = static_cast<float>(into / crossfade);
                const auto echoLeft = hermite(sample->left, partStart + into);
                const auto echoRight = sample->stereo() ? hermite(sample->right, partStart + into) : echoLeft;
                l += (echoLeft - l) * t;
                r += (echoRight - r) * t;
            }
            // How much of the part is left to play, in output frames, for a
            // fade at its end.
            const auto remaining = (partEnd - position) / increment;
            position += increment;
            if (filtered)
                for (size_t channel = 0; channel < 2; ++channel)
                {
                    auto& value = channel == 0 ? l : r;
                    const auto v3 = value - ic2[channel];
                    const auto v1 = a1 * ic1[channel] + a2 * v3;
                    const auto v2 = ic2[channel] + a2 * ic1[channel] + a3 * v3;
                    ic1[channel] = 2.0f * v1 - ic1[channel];
                    ic2[channel] = 2.0f * v2 - ic2[channel];
                    // A file's silent tail would otherwise walk the state
                    // down into the denormal range, where x86 crawls.
                    if (std::abs(ic1[channel]) < 1.0e-20f) ic1[channel] = 0.0f;
                    if (std::abs(ic2[channel]) < 1.0e-20f) ic2[channel] = 0.0f;
                    value = v2;
                }

            // The shape: rising in, decaying towards Sustain, fading at the
            // part's end and releasing once let go. Each is a multiply by one
            // when it does nothing, so an unshaped pad plays its file exactly.
            auto amount = 1.0f;
            if (riseStep > 0.0f)
            {
                amount *= rise;
                rise += riseStep;
                if (rise >= 1.0f)
                {
                    rise = 1.0f;
                    riseStep = 0.0f;
                }
            }
            if (decaying)
            {
                amount *= sustain + (1.0f - sustain) * envelope;
                envelope *= envelopeFactor;
                // Under a sustain the envelope never ends the voice, and would
                // fall on into the denormal range.
                if (envelope < 1.0e-9f)
                    envelope = 0.0f;
            }
            if (fadeOutFrames > 0.0 && remaining < fadeOutFrames)
                amount *= static_cast<float>(std::max(0.0, remaining) / fadeOutFrames);
            if (classic)
            {
                if (holdRemaining == 0)
                    letGo();
                else if (holdRemaining > 0)
                    --holdRemaining;
                if (releasing)
                {
                    amount *= releaseGain;
                    releaseGain *= releaseFactor;
                }
            }
            l *= amount;
            r *= amount;
        }
        gainLeft += glide * (targetLeft - gainLeft);
        gainRight += glide * (targetRight - gainRight);
        const auto level = strength * fade;
        if (right != nullptr)
        {
            left[i] += l * gainLeft * level;
            right[i] += r * gainRight * level;
        }
        else
        {
            left[i] += (l * gainLeft + r * gainRight) * 0.5f * level;
        }
        if (fadeStep > 0.0f)
            fade -= fadeStep;
    }
    return numSamples;
}

// ---- the engine ---------------------------------------------------------------

DrumRackEngine::DrumRackEngine()
{
    lastStruck.fill(never);
}

DrumRackEngine::~DrumRackEngine()
{
    // Nothing renders while an engine is destroyed, so every voice can let go
    // and every sample goes with the engine.
    for (auto& voice : voices)
        if (voice.active)
            finish(voice);
}

void DrumRackEngine::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (glideSeconds * rate)));
    // One pad cannot be struck twice in a millisecond; see strike().
    guardFrames = std::max<juce::int64>(1, static_cast<juce::int64>(rate / 1000.0));
    clear();
    // A pad played from the face while nothing was rendering is not played
    // late, whenever rendering starts.
    for (auto& word : previews)
        word.store(0);
    partPreviewPad.store(-1);
    // Nothing is rendering, so whatever the pads let go of can go now.
    for (const auto& gone : retired)
        owned.erase(std::remove_if(owned.begin(), owned.end(),
                                   [&gone] (const auto& sample) { return sample.get() == gone.sample; }),
                    owned.end());
    retired.clear();
}

void DrumRackEngine::retire(const DrumSample* sample)
{
    if (sample != nullptr)
        retired.push_back({ sample, stretchesBegun.load() });
}

void DrumRackEngine::setPadSample(int pad, std::unique_ptr<DrumSample> sample)
{
    if (!validPad(pad))
        return;
    const DrumSample* incoming = sample.get();
    if (sample != nullptr)
        owned.push_back(std::move(sample));
    // The sample before the source: a strike that reads the source as a
    // sample in between finds either sample, never a pad with none.
    retire(pads[static_cast<size_t>(pad)].sample.exchange(incoming));
    pads[static_cast<size_t>(pad)].source.store(static_cast<int>(incoming != nullptr ? Source::sample : Source::empty));
    collect();
}

void DrumRackEngine::setPadSynth(int pad, DrumModel model)
{
    if (!validPad(pad))
        return;
    auto& target = pads[static_cast<size_t>(pad)];
    target.model.store(static_cast<int>(model));
    target.source.store(static_cast<int>(Source::synth));
    retire(target.sample.exchange(nullptr));
    collect();
}

void DrumRackEngine::clearPad(int pad)
{
    if (!validPad(pad))
        return;
    auto& target = pads[static_cast<size_t>(pad)];
    target.source.store(static_cast<int>(Source::empty));
    retire(target.sample.exchange(nullptr));
    collect();
}

void DrumRackEngine::setPadPlayback(int pad, const Playback& wanted)
{
    if (!validPad(pad))
        return;
    const auto playback = wanted.clamped();
    auto& target = pads[static_cast<size_t>(pad)];
    target.mode.store(static_cast<int>(playback.mode));
    target.start.store(playback.start);
    target.end.store(playback.end);
    target.fadeIn.store(playback.fadeIn);
    target.fadeOut.store(playback.fadeOut);
    target.attack.store(playback.attack);
    target.sustain.store(playback.sustain);
    target.release.store(playback.release);
    target.loop.store(playback.loop);
}

DrumRackEngine::Playback DrumRackEngine::playbackOf(const Pad& pad) const
{
    Playback playback;
    playback.mode = static_cast<PlayMode>(pad.mode.load());
    playback.start = pad.start.load();
    playback.end = pad.end.load();
    playback.fadeIn = pad.fadeIn.load();
    playback.fadeOut = pad.fadeOut.load();
    playback.attack = pad.attack.load();
    playback.sustain = pad.sustain.load();
    playback.release = pad.release.load();
    playback.loop = pad.loop.load();
    return playback;
}

DrumRackEngine::Playback DrumRackEngine::padPlayback(int pad) const
{
    return validPad(pad) ? playbackOf(pads[static_cast<size_t>(pad)]) : Playback {};
}

void DrumRackEngine::setPadChoke(int pad, int group)
{
    if (validPad(pad))
        pads[static_cast<size_t>(pad)].choke.store(juce::jlimit(0, chokeGroupCount, group));
}

void DrumRackEngine::setPadMuted(int pad, bool muted)
{
    if (validPad(pad))
        pads[static_cast<size_t>(pad)].muted.store(muted);
}

void DrumRackEngine::setPadSoloed(int pad, bool soloed)
{
    if (validPad(pad) && pads[static_cast<size_t>(pad)].soloed.exchange(soloed) != soloed)
        soloCount.fetch_add(soloed ? 1 : -1);
}

DrumRackEngine::Source DrumRackEngine::padSource(int pad) const
{
    return validPad(pad) ? static_cast<Source>(pads[static_cast<size_t>(pad)].source.load()) : Source::empty;
}

DrumModel DrumRackEngine::padModel(int pad) const
{
    return validPad(pad) ? static_cast<DrumModel>(pads[static_cast<size_t>(pad)].model.load()) : DrumModel::Kick;
}

const DrumSample* DrumRackEngine::padSample(int pad) const
{
    return validPad(pad) ? pads[static_cast<size_t>(pad)].sample.load() : nullptr;
}

int DrumRackEngine::collect()
{
    const auto ended = stretchesEnded.load();
    retired.erase(std::remove_if(retired.begin(), retired.end(), [this, ended] (const Retired& gone)
    {
        if (ended < gone.after || gone.sample->playing.load() != 0)
            return false;
        owned.erase(std::remove_if(owned.begin(), owned.end(),
                                   [&gone] (const auto& sample) { return sample.get() == gone.sample; }),
                    owned.end());
        return true;
    }), retired.end());
    return static_cast<int>(retired.size());
}

void DrumRackEngine::previewPad(int pad)
{
    if (validPad(pad))
        previews[static_cast<size_t>(pad / 64)].fetch_or(std::uint64_t { 1 } << static_cast<unsigned>(pad % 64));
}

void DrumRackEngine::previewPart(int pad, float start, float end)
{
    if (!validPad(pad))
        return;
    partPreviewStart.store(start);
    partPreviewEnd.store(end);
    partPreviewPad.store(pad);
}

std::uint32_t DrumRackEngine::strikes(int pad) const
{
    return validPad(pad) ? pads[static_cast<size_t>(pad)].struck.load(std::memory_order_relaxed) : 0u;
}

std::uint32_t DrumRackEngine::notesReceived(int note) const
{
    return validPad(note) ? pads[static_cast<size_t>(note)].received.load(std::memory_order_relaxed) : 0u;
}

float DrumRackEngine::playhead(int pad) const
{
    return validPad(pad) ? pads[static_cast<size_t>(pad)].playhead.load(std::memory_order_relaxed) : -1.0f;
}

void DrumRackEngine::finish(Voice& voice)
{
    if (voice.order == newestVoice[static_cast<size_t>(voice.pad)])
        pads[static_cast<size_t>(voice.pad)].playhead.store(-1.0f, std::memory_order_relaxed);
    if (voice.sample != nullptr)
        voice.sample->playing.fetch_sub(1);
    voice.sample = nullptr;
    voice.active = false;
}

float DrumRackEngine::fadeStepFor() const
{
    return static_cast<float>(1.0 / std::max(1.0, cutSeconds * rate));
}

void DrumRackEngine::clear()
{
    for (auto& voice : voices)
        if (voice.active)
            finish(voice);
    lastStruck.fill(never);
}

void DrumRackEngine::releaseAll()
{
    for (auto& voice : voices)
        if (voice.active && voice.fadeStep == 0.0f)
            voice.fadeStep = fadeStepFor();
}

void DrumRackEngine::noteOn(int note, float velocity, const Settings& settings)
{
    const Stretch stretch(*this);
    if (validPad(note))
        pads[static_cast<size_t>(note)].received.fetch_add(1, std::memory_order_relaxed);
    strike(note, velocity, settings, false);
}

void DrumRackEngine::noteOff(int note)
{
    // A voice struck from the face lets go of itself; only one a note struck
    // waits for the note to end.
    for (auto& voice : voices)
        if (voice.active && voice.pad == note && voice.holdRemaining < 0)
            voice.letGo();
}

void DrumRackEngine::strike(int pad, float velocity, const Settings& settings, bool fromFace, const Part* part)
{
    if (!validPad(pad))
        return;
    auto& struckPad = pads[static_cast<size_t>(pad)];
    const auto source = static_cast<Source>(struckPad.source.load());
    if (source == Source::empty)
        return;
    const auto* sample = source == Source::sample ? struckPad.sample.load() : nullptr;
    if (source == Source::sample && sample == nullptr)
        return;

    // One pad cannot be struck twice in a millisecond, so a second strike that
    // close is the same written note arriving twice rather than two hits. The
    // engine delivers one that way whenever a clip starts a hair before a block
    // boundary: once at the end of that block and once a few samples into the
    // next. Summed, the two read as a single hit at twice the level. Nothing a
    // pattern or a player writes comes near the window: a flam is tens of
    // milliseconds, and a sixty-fourth at 200 bpm is nineteen.
    auto& previous = lastStruck[static_cast<size_t>(pad)];
    if (framesRendered - previous < guardFrames)
        return;
    previous = framesRendered;

    // A choke group cuts off every other pad in it, the way a closed hat stops
    // an open one ringing.
    if (const auto group = struckPad.choke.load(); group > 0)
        for (auto& voice : voices)
            if (voice.active && voice.pad != pad && voice.fadeStep == 0.0f
                && pads[static_cast<size_t>(voice.pad)].choke.load() == group)
                voice.fadeStep = fadeStepFor();

    // A free voice, or else the one struck longest ago.
    auto* voice = &voices.front();
    for (auto& candidate : voices)
    {
        if (!candidate.active)
        {
            voice = &candidate;
            break;
        }
        if (candidate.order < voice->order)
            voice = &candidate;
    }
    if (voice->active)
        finish(*voice);

    const auto& padSettings = settings[static_cast<size_t>(pad)];
    if (source == Source::sample)
    {
        auto playback = playbackOf(struckPad);
        if (part != nullptr)
        {
            // A part auditioned is heard as the one-shot it would be on a pad
            // of its own.
            playback.mode = PlayMode::oneShot;
            playback.start = part->start;
            playback.end = part->end;
            playback.fadeIn = 0.0f;
            playback = playback.clamped();
        }
        // A classic pad struck from the face has no key to be let go of, so it
        // lets go of itself.
        const auto hold = fromFace ? static_cast<int>(rate * previewHoldSeconds) : -1;
        sample->playing.fetch_add(1);
        voice->startSample(*sample, padSettings, playback, rate, hold);
    }
    else
    {
        ++strikeCount;
        voice->startSynth(static_cast<DrumModel>(struckPad.model.load()), padSettings, rate,
                          0x9e3779b9u * strikeCount + static_cast<std::uint32_t>(pad) * 7919u + 1u);
    }
    voice->active = true;
    voice->pad = pad;
    voice->order = ++voiceOrder;
    voice->strength = strengthFor(velocity, padSettings.velocity);
    voice->fade = 1.0f;
    voice->fadeStep = 0.0f;
    newestVoice[static_cast<size_t>(pad)] = voice->order;
    // The gains start where the pad's level, mute and solo put them, so the
    // attack is not smoothed away and a muted pad makes no sound at all;
    // they glide from there.
    const auto gain = audible(pad) ? levelGain(padSettings.level) : 0.0f;
    voice->gainLeft = gain * panLeft(padSettings.pan);
    voice->gainRight = gain * panRight(padSettings.pan);
    struckPad.struck.fetch_add(1, std::memory_order_relaxed);
}

bool DrumRackEngine::audible(int pad) const
{
    const auto& asked = pads[static_cast<size_t>(pad)];
    return !asked.muted.load() && (soloCount.load() == 0 || asked.soloed.load());
}

void DrumRackEngine::render(float* left, float* right, int numSamples, const Settings& settings)
{
    if (left == nullptr || numSamples <= 0)
        return;
    const Stretch stretch(*this);
    for (size_t word = 0; word < previews.size(); ++word)
        if (const auto wanted = previews[word].exchange(0); wanted != 0)
            for (unsigned bit = 0; bit < 64; ++bit)
                if ((wanted >> bit) & 1u)
                    strike(static_cast<int>(word * 64 + bit), previewVelocity, settings, true);
    if (const auto pad = partPreviewPad.exchange(-1); pad >= 0)
    {
        const Part part { partPreviewStart.load(), partPreviewEnd.load() };
        strike(pad, previewVelocity, settings, true, &part);
    }

    for (auto& voice : voices)
    {
        if (!voice.active)
            continue;
        // Where the voice's gains are heading: its pad's level and pan, or
        // silence when the pad is muted, or when some other pad is soloed and
        // it is not.
        const auto& padSettings = settings[static_cast<size_t>(voice.pad)];
        const auto gain = audible(voice.pad) ? levelGain(padSettings.level) : 0.0f;
        const auto played = voice.render(left, right, numSamples, gain * panLeft(padSettings.pan),
                                         gain * panRight(padSettings.pan), smoothing);
        if (played < numSamples)
        {
            finish(voice);
            continue;
        }
        if (voice.order == newestVoice[static_cast<size_t>(voice.pad)] && voice.sample != nullptr)
            pads[static_cast<size_t>(voice.pad)].playhead.store(
                static_cast<float>(voice.position / std::max(1, voice.sample->length())), std::memory_order_relaxed);
    }
    framesRendered += numSamples;
}

int DrumRackEngine::activeVoices() const
{
    return static_cast<int>(std::count_if(voices.begin(), voices.end(), [] (const Voice& voice) { return voice.active; }));
}

int DrumRackEngine::renderStrike(Source source, DrumModel model, const DrumSample* sample,
                                 const PadSettings& settings, const Playback& playback, float velocity,
                                 double sampleRate, float* left, float* right, int maxFrames, int holdFrames)
{
    for (auto* channel : { left, right })
        if (channel != nullptr)
            std::fill(channel, channel + std::max(0, maxFrames), 0.0f);
    if (maxFrames <= 0 || source == Source::empty || (source == Source::sample && sample == nullptr))
        return 0;
    const auto outputRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    // A voice of its own, and so a seed of its own: the picture is the same
    // every time it is drawn.
    Voice voice;
    if (source == Source::sample)
        voice.startSample(*sample, settings, playback.clamped(), outputRate, holdFrames);
    else
        voice.startSynth(model, settings, outputRate, 0x9e3779b9u);
    voice.strength = strengthFor(velocity, settings.velocity);
    const auto gain = levelGain(settings.level);
    voice.gainLeft = gain * panLeft(settings.pan);
    voice.gainRight = gain * panRight(settings.pan);

    // In slices, so a strike that ends early stops being rendered.
    constexpr int slice = 256;
    std::array<float, slice> scratchLeft {}, scratchRight {};
    int frames = 0;
    while (frames < maxFrames)
    {
        const auto count = std::min(slice, maxFrames - frames);
        std::fill(scratchLeft.begin(), scratchLeft.end(), 0.0f);
        std::fill(scratchRight.begin(), scratchRight.end(), 0.0f);
        const auto played = voice.render(scratchLeft.data(), scratchRight.data(), count, voice.gainLeft,
                                         voice.gainRight, 1.0f);
        if (left != nullptr)
            std::copy(scratchLeft.begin(), scratchLeft.begin() + played, left + frames);
        if (right != nullptr)
            std::copy(scratchRight.begin(), scratchRight.begin() + played, right + frames);
        frames += played;
        if (played < count)
            break;
    }
    return frames;
}
}
