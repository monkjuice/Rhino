#include "AutoTuneDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
const juce::Identifier rootId ("root"), scaleId ("scale"), notesId ("notes");
const juce::Identifier degreesId ("degrees"), rangeId ("range");
const juce::Identifier liveId ("live"), naturalId ("natural");

constexpr int allTwelveNotes = 0b111111111111;

juce::String percent(float value)
{
    return juce::String(juce::roundToInt(value * 100.0f)) + "%";
}
}

AutoTuneDevice::AutoTuneDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager();
    strength.referTo(state, "strength", undo, 1.0f);
    retune.referTo(state, "retune", undo, 40.0f);
    flex.referTo(state, "flex", undo, 0.0f);
    human.referTo(state, "human", undo, 0.0f);
    pitch.referTo(state, "pitch", undo, 0.0f);
    fine.referTo(state, "fine", undo, 0.0f);
    formant.referTo(state, "formant", undo, 0.0f);
    follow.referTo(state, "follow", undo, 0.0f);
    vibrato.referTo(state, "vibrato", undo, 0.0f);
    vibratoRate.referTo(state, "vibRate", undo, 6.0f);
    vibratoFade.referTo(state, "vibFade", undo, 0.0f);
    mix.referTo(state, "mix", undo, 1.0f);
    inputGain.referTo(state, "inGain", undo, 0.0f);

    root.referTo(state, rootId, undo, 0);
    scaleIndex.referTo(state, scaleId, undo, 0);
    notes.referTo(state, notesId, undo, allTwelveNotes);
    degrees.referTo(state, degreesId, undo, 0);
    range.referTo(state, rangeId, undo, 1);       // Mid: most voices, most of the time
    live.referTo(state, liveId, undo, false);
    natural.referTo(state, naturalId, undo, false);

    strengthParam     = addParam("strength", "Strength", {0.0f, 1.0f});
    retuneParam       = addParam("retune", "Retune", {0.0f, 200.0f});
    flexParam         = addParam("flex", "Flex", {0.0f, 1.0f});
    humanParam        = addParam("human", "Human", {0.0f, 1.0f});
    pitchParam        = addParam("pitch", "Pitch", {-24.0f, 24.0f});
    fineParam         = addParam("fine", "Fine", {-100.0f, 100.0f});
    formantParam      = addParam("formant", "Formant", {-100.0f, 100.0f});
    followParam       = addParam("follow", "F. Follow", {0.0f, 1.0f});
    vibratoParam      = addParam("vibrato", "Vibrato", {0.0f, 200.0f});
    vibratoRateParam  = addParam("vibRate", "Vib Rate", {2.0f, 15.0f});
    vibratoFadeParam  = addParam("vibFade", "Fade In", {0.0f, 5000.0f});
    mixParam          = addParam("mix", "Dry/Wet", {0.0f, 1.0f});
    inputGainParam    = addParam("inGain", "In Gain", {-24.0f, 24.0f});

    strengthParam->attachToCurrentValue(strength);
    retuneParam->attachToCurrentValue(retune);
    flexParam->attachToCurrentValue(flex);
    humanParam->attachToCurrentValue(human);
    pitchParam->attachToCurrentValue(pitch);
    fineParam->attachToCurrentValue(fine);
    formantParam->attachToCurrentValue(formant);
    followParam->attachToCurrentValue(follow);
    vibratoParam->attachToCurrentValue(vibrato);
    vibratoRateParam->attachToCurrentValue(vibratoRate);
    vibratoFadeParam->attachToCurrentValue(vibratoFade);
    mixParam->attachToCurrentValue(mix);
    inputGainParam->attachToCurrentValue(inputGain);

    strengthParam->valueToStringFunction = [] (float v) { return percent(v); };
    flexParam->valueToStringFunction = [] (float v) { return percent(v); };
    humanParam->valueToStringFunction = [] (float v) { return percent(v); };
    followParam->valueToStringFunction = [] (float v) { return percent(v); };
    mixParam->valueToStringFunction = [] (float v) { return percent(v); };
    retuneParam->valueToStringFunction = [] (float v)
    {
        return v < 0.5f ? juce::String("Hard") : juce::String(juce::roundToInt(v)) + " ms";
    };
    pitchParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + " st"; };
    fineParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + " ct"; };
    formantParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + "%"; };
    vibratoParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + " ct"; };
    vibratoRateParam->valueToStringFunction = [] (float v) { return juce::String(v, 2) + " Hz"; };
    vibratoFadeParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + " ms"; };
    inputGainParam->valueToStringFunction = [] (float v) { return juce::String(v, 1) + " dB"; };
}

AutoTuneDevice::~AutoTuneDevice()
{
    notifyListenersOfDeletion();
    strengthParam->detachFromCurrentValue();
    retuneParam->detachFromCurrentValue();
    flexParam->detachFromCurrentValue();
    humanParam->detachFromCurrentValue();
    pitchParam->detachFromCurrentValue();
    fineParam->detachFromCurrentValue();
    formantParam->detachFromCurrentValue();
    followParam->detachFromCurrentValue();
    vibratoParam->detachFromCurrentValue();
    vibratoRateParam->detachFromCurrentValue();
    vibratoFadeParam->detachFromCurrentValue();
    mixParam->detachFromCurrentValue();
    inputGainParam->detachFromCurrentValue();
}

void AutoTuneDevice::initialise(const te::PluginInitialisationInfo& info)
{
    engine.prepare(info.sampleRate > 0.0 ? info.sampleRate : 48000.0, 2,
                   std::max(64, info.blockSizeSamples));
}

void AutoTuneDevice::reset()
{
    engine.reset();
}

PitchClassMask AutoTuneDevice::scaleMask() const
{
    const auto bits = notes.get();
    PitchClassMask mask {};
    for (int i = 0; i < 12; ++i)
        mask[static_cast<size_t>(i)] = (bits >> i) & 1;
    return mask;
}

void AutoTuneDevice::setScaleMask(const PitchClassMask& mask)
{
    auto bits = 0;
    for (int i = 0; i < 12; ++i)
        if (mask[static_cast<size_t>(i)]) bits |= 1 << i;
    writeProperty(notesId, bits);
}

MusicalScale AutoTuneDevice::scale() const
{
    return static_cast<MusicalScale>(juce::jlimit(0, musicalScaleCount - 1, scaleIndex.get()));
}

void AutoTuneDevice::applyScale(int newRoot, MusicalScale newScale)
{
    const auto clamped = ((newRoot % 12) + 12) % 12;
    writeProperty(rootId, clamped);
    writeProperty(scaleId, static_cast<int>(newScale));
    setScaleMask(maskForScale(newScale, clamped));
}

bool AutoTuneDevice::maskMatchesNamedScale() const
{
    return scaleMask() == maskForScale(scale(), scaleRoot());
}

PitchTracker::Range AutoTuneDevice::trackingRange() const
{
    const auto index = juce::jlimit(0, 2, range.get());
    return index == 0 ? PitchTracker::Range::High
         : index == 1 ? PitchTracker::Range::Mid
                      : PitchTracker::Range::Bass;
}

void AutoTuneDevice::setTrackingRange(PitchTracker::Range next)
{
    writeProperty(rangeId, next == PitchTracker::Range::High ? 0
                         : next == PitchTracker::Range::Mid ? 1 : 2);
}

void AutoTuneDevice::setLiveMode(bool on) { writeProperty(liveId, on); }
void AutoTuneDevice::setNaturalVibrato(bool on) { writeProperty(naturalId, on); }
void AutoTuneDevice::setScaleDegreeShift(int steps) { writeProperty(degreesId, juce::jlimit(-12, 12, steps)); }

void AutoTuneDevice::writeProperty(const juce::Identifier& id, const juce::var& value)
{
    state.setProperty(id, value, getUndoManager());
    // Changing the range or Live Mode moves the reported latency, and the
    // graph only reads that when it is rebuilt.
    if (id == rangeId || id == liveId)
        changed();
}

void AutoTuneDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    if (channels == 0) return;

    AutoTuneEngine::Settings settings;
    settings.inputGainDb = inputGainParam->getCurrentValue();
    settings.range = trackingRange();
    settings.liveMode = live.get();
    settings.mask = scaleMask();
    settings.scaleDegreeShift = scaleDegreeShift();
    settings.strength = strengthParam->getCurrentValue();
    settings.retuneMs = retuneParam->getCurrentValue();
    settings.flex = flexParam->getCurrentValue();
    settings.humanize = humanParam->getCurrentValue();
    // Semitones on the coarse control, cents on the fine one, the way Auto
    // Shift splits them: a transposer that lands between two notes is a
    // detune, and there is a separate knob for that.
    settings.pitchSemitones = std::round(pitchParam->getCurrentValue());
    settings.fineCents = fineParam->getCurrentValue();
    settings.formantPercent = formantParam->getCurrentValue();
    settings.formantFollow = followParam->getCurrentValue();
    settings.vibratoCents = vibratoParam->getCurrentValue();
    settings.vibratoRate = vibratoRateParam->getCurrentValue();
    settings.vibratoFadeMs = vibratoFadeParam->getCurrentValue();
    settings.naturalVibrato = natural.get();
    settings.dryWet = mixParam->getCurrentValue();
    engine.setSettings(settings);

    float* channelData[2] {};
    const auto used = std::min(channels, 2);
    for (int channel = 0; channel < used; ++channel)
        channelData[channel] = buffer.getWritePointer(channel, context.bufferStartSample);
    engine.process(channelData, used, context.bufferNumSamples);

    // A voice arrives mono on a stereo track more often than not; anything
    // past the second channel follows the first rather than staying dry.
    for (int channel = used; channel < channels; ++channel)
        buffer.copyFrom(channel, context.bufferStartSample, buffer,
                        0, context.bufferStartSample, context.bufferNumSamples);
}

void AutoTuneDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, strength, retune, flex, human, pitch, fine, formant,
                                     follow, vibrato, vibratoRate, vibratoFade, mix, inputGain,
                                     root, scaleIndex, notes, degrees, range, live, natural);
    strengthParam->updateFromAttachedValue();
    retuneParam->updateFromAttachedValue();
    flexParam->updateFromAttachedValue();
    humanParam->updateFromAttachedValue();
    pitchParam->updateFromAttachedValue();
    fineParam->updateFromAttachedValue();
    formantParam->updateFromAttachedValue();
    followParam->updateFromAttachedValue();
    vibratoParam->updateFromAttachedValue();
    vibratoRateParam->updateFromAttachedValue();
    vibratoFadeParam->updateFromAttachedValue();
    mixParam->updateFromAttachedValue();
    inputGainParam->updateFromAttachedValue();
}
}
