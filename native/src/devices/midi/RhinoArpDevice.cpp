#include "RhinoArpDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr std::array<const char*, 5> styleNames {"Up", "Down", "UpDown", "DownUp", "Chord"};
constexpr std::array<const char*, 5> rateNames {"1/8", "1/16", "1/32", "1/8T", "1/16T"};
constexpr std::array<const char*, 3> grooveNames {"Straight", "Swing 8", "Swing 16"};
constexpr std::array<const char*, 3> retriggerNames {"Off", "Note", "Beat"};
constexpr std::array<const char*, 4> intervalNames {"1/4", "1/2", "1 bar", "2 bars"};
constexpr std::array<const char*, 12> rootNames {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<const char*, 3> scaleNames {"Chromatic", "Major", "Minor"};
constexpr std::array<int, 7> majorScale {0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> minorScale {0, 2, 3, 5, 7, 8, 10};

int positiveModulo(int value, int divisor)
{
    const auto remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

int floorDivide(int value, int divisor)
{
    const auto quotient = value / divisor;
    return value < 0 && value % divisor != 0 ? quotient - 1 : quotient;
}
}

int RhinoArpDevice::parameterChoiceCount(int parameterIndex)
{
    switch (parameterIndex)
    {
        case styleParameter: return static_cast<int>(styleNames.size());
        case rateParameter: return static_cast<int>(rateNames.size());
        case stepsParameter: return 9;
        case offsetParameter: return 16;
        case grooveParameter: return static_cast<int>(grooveNames.size());
        case retriggerParameter: return static_cast<int>(retriggerNames.size());
        case intervalParameter: return static_cast<int>(intervalNames.size());
        case repeatsParameter: return 9;
        case rootParameter: return static_cast<int>(rootNames.size());
        case scaleParameter: return static_cast<int>(scaleNames.size());
        default: return 0;
    }
}

juce::String RhinoArpDevice::parameterChoiceName(int parameterIndex, int choice)
{
    const auto count = parameterChoiceCount(parameterIndex);
    const auto selected = juce::jlimit(0, std::max(0, count - 1), choice);
    switch (parameterIndex)
    {
        case styleParameter: return styleNames[static_cast<size_t>(selected)];
        case rateParameter: return rateNames[static_cast<size_t>(selected)];
        case stepsParameter:
        case offsetParameter: return juce::String(selected);
        case grooveParameter: return grooveNames[static_cast<size_t>(selected)];
        case retriggerParameter: return retriggerNames[static_cast<size_t>(selected)];
        case intervalParameter: return intervalNames[static_cast<size_t>(selected)];
        case repeatsParameter: return selected == 0 ? juce::String::fromUTF8("\xe2\x88\x9e")
                                                    : juce::String(selected);
        case rootParameter: return rootNames[static_cast<size_t>(selected)];
        case scaleParameter: return scaleNames[static_cast<size_t>(selected)];
        default: return {};
    }
}

RhinoArpDevice::RhinoArpDevice(te::PluginCreationInfo info) : Plugin(info)
{
    // These defaults are the device's identity: a held chord bounces up and
    // down, then walks down two octaves in scale degrees before starting over.
    style.referTo(state, "style", getUndoManager(), 2.0f);
    rateIndex.referTo(state, "rate", getUndoManager(), 1.0f);
    gatePercent.referTo(state, "gate", getUndoManager(), 84.0f);
    distance.referTo(state, "distance", getUndoManager(), -7.0f);
    steps.referTo(state, "steps", getUndoManager(), 2.0f);
    offset.referTo(state, "offset", getUndoManager(), 0.0f);
    groove.referTo(state, "groove", getUndoManager(), 2.0f);
    hold.referTo(state, "hold", getUndoManager(), 1.0f);
    retrigger.referTo(state, "retrigger", getUndoManager(), 2.0f);
    interval.referTo(state, "interval", getUndoManager(), 1.0f);
    repeats.referTo(state, "repeats", getUndoManager(), 0.0f);
    root.referTo(state, "root", getUndoManager(), 8.0f);
    scale.referTo(state, "scale", getUndoManager(), 2.0f);

    styleParam = addParam("style", "Style", {0.0f, 4.0f, 1.0f});
    rateParam = addParam("rate", "Rate", {0.0f, 4.0f, 1.0f});
    gateParam = addParam("gate", "Gate", {10.0f, 200.0f});
    distanceParam = addParam("distance", "Distance", {-12.0f, 12.0f, 1.0f});
    stepsParam = addParam("steps", "Steps", {0.0f, 8.0f, 1.0f});
    offsetParam = addParam("offset", "Offset", {0.0f, 15.0f, 1.0f});
    grooveParam = addParam("groove", "Groove", {0.0f, 2.0f, 1.0f});
    holdParam = addParam("hold", "Hold", {0.0f, 1.0f, 1.0f});
    retriggerParam = addParam("retrigger", "Retrigger", {0.0f, 2.0f, 1.0f});
    intervalParam = addParam("interval", "Interval", {0.0f, 3.0f, 1.0f});
    repeatsParam = addParam("repeats", "Repeats", {0.0f, 8.0f, 1.0f});
    rootParam = addParam("root", "Root", {0.0f, 11.0f, 1.0f});
    scaleParam = addParam("scale", "Scale", {0.0f, 2.0f, 1.0f});

    styleParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(styleParameter, juce::roundToInt(value)); };
    rateParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(rateParameter, juce::roundToInt(value)); };
    gateParam->valueToStringFunction = [] (float value) { return juce::String(juce::roundToInt(value)) + "%"; };
    distanceParam->valueToStringFunction = [this] (float value)
    {
        const auto unit = juce::roundToInt(scaleParam->getCurrentValue()) == 0 ? " st" : " sd";
        return juce::String(juce::roundToInt(value)) + unit;
    };
    stepsParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(stepsParameter, juce::roundToInt(value)); };
    offsetParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(offsetParameter, juce::roundToInt(value)); };
    grooveParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(grooveParameter, juce::roundToInt(value)); };
    holdParam->valueToStringFunction = [] (float value) { return value >= 0.5f ? "On" : "Off"; };
    retriggerParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(retriggerParameter, juce::roundToInt(value)); };
    intervalParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(intervalParameter, juce::roundToInt(value)); };
    repeatsParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(repeatsParameter, juce::roundToInt(value)); };
    rootParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(rootParameter, juce::roundToInt(value)); };
    scaleParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(scaleParameter, juce::roundToInt(value)); };

    styleParam->attachToCurrentValue(style);
    rateParam->attachToCurrentValue(rateIndex);
    gateParam->attachToCurrentValue(gatePercent);
    distanceParam->attachToCurrentValue(distance);
    stepsParam->attachToCurrentValue(steps);
    offsetParam->attachToCurrentValue(offset);
    grooveParam->attachToCurrentValue(groove);
    holdParam->attachToCurrentValue(hold);
    retriggerParam->attachToCurrentValue(retrigger);
    intervalParam->attachToCurrentValue(interval);
    repeatsParam->attachToCurrentValue(repeats);
    rootParam->attachToCurrentValue(root);
    scaleParam->attachToCurrentValue(scale);
}

RhinoArpDevice::~RhinoArpDevice()
{
    notifyListenersOfDeletion();
    styleParam->detachFromCurrentValue();
    rateParam->detachFromCurrentValue();
    gateParam->detachFromCurrentValue();
    distanceParam->detachFromCurrentValue();
    stepsParam->detachFromCurrentValue();
    offsetParam->detachFromCurrentValue();
    grooveParam->detachFromCurrentValue();
    holdParam->detachFromCurrentValue();
    retriggerParam->detachFromCurrentValue();
    intervalParam->detachFromCurrentValue();
    repeatsParam->detachFromCurrentValue();
    rootParam->detachFromCurrentValue();
    scaleParam->detachFromCurrentValue();
}

void RhinoArpDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    firstOutput.reserve(2048);
    steadyOutput.reserve(2048);
    firstRender = true;
    reset();
}

void RhinoArpDevice::reset()
{
    for (auto& note : heldNotes)
        note = {};
    for (auto& pending : pendingOffs)
        pending.active = false;
    clockValid = false;
    nextTickBeat = 0.0;
    lastBlockEndBeat = 0.0;
    sequenceStep = 0;
    lastBeatRetriggerCycle = std::numeric_limits<std::int64_t>::min();
}

void RhinoArpDevice::midiPanic()
{
    reset();
}

double RhinoArpDevice::rateBeats() const
{
    switch (juce::roundToInt(rateParam->getCurrentValue()))
    {
        case 0: return 0.5;       // 1/8
        case 2: return 0.125;     // 1/32
        case 3: return 1.0 / 3.0; // 1/8 triplet
        case 4: return 1.0 / 6.0; // 1/16 triplet
        default: return 0.25;     // 1/16
    }
}

double RhinoArpDevice::intervalBeats() const
{
    switch (juce::roundToInt(intervalParam->getCurrentValue()))
    {
        case 0: return 1.0;
        case 2: return 4.0;
        case 3: return 8.0;
        default: return 2.0;
    }
}

double RhinoArpDevice::stepLengthBeats(int step) const
{
    const auto straight = rateBeats();
    const auto selected = static_cast<Groove>(juce::roundToInt(grooveParam->getCurrentValue()));
    if (selected == Groove::Straight)
        return straight;

    // 58/42 is deliberately a groove rather than a second rate. The pair
    // keeps the same total length, so bars and beat retriggers remain aligned.
    const auto swingPair = selected == Groove::Swing16 ? 2 : std::max(2, juce::roundToInt(1.0 / straight));
    const auto place = positiveModulo(step, swingPair);
    if (place == 0)
        return straight * 1.16;
    if (place == 1)
        return straight * 0.84;
    return straight;
}

int RhinoArpDevice::activeNoteCount() const
{
    return static_cast<int>(std::count_if(heldNotes.begin(), heldNotes.end(),
                                          [] (const auto& note) { return note.active; }));
}

int RhinoArpDevice::physicallyHeldCount() const
{
    return static_cast<int>(std::count_if(heldNotes.begin(), heldNotes.end(),
                                          [] (const auto& note) { return note.physicallyHeld; }));
}

int RhinoArpDevice::noteAtOrdinal(int ordinal) const
{
    for (int pitch = 0; pitch < static_cast<int>(heldNotes.size()); ++pitch)
        if (heldNotes[static_cast<size_t>(pitch)].active && ordinal-- == 0)
            return pitch;
    return -1;
}

int RhinoArpDevice::styleLength(int noteCount) const
{
    const auto selected = static_cast<Style>(juce::roundToInt(styleParam->getCurrentValue()));
    if (selected == Style::Chord || noteCount <= 1)
        return 1;
    if (selected == Style::UpDown || selected == Style::DownUp)
        return noteCount * 2 - 2;
    return noteCount;
}

int RhinoArpDevice::ordinalForStep(int step, int noteCount) const
{
    if (noteCount <= 1)
        return 0;
    const auto selected = static_cast<Style>(juce::roundToInt(styleParam->getCurrentValue()));
    const auto length = styleLength(noteCount);
    auto position = positiveModulo(step + juce::roundToInt(offsetParam->getCurrentValue()), length);
    if (selected == Style::UpDown || selected == Style::DownUp)
        position = position < noteCount ? position : length - position;
    if (selected == Style::Down || selected == Style::DownUp)
        position = noteCount - 1 - position;
    return juce::jlimit(0, noteCount - 1, position);
}

int RhinoArpDevice::transposedPitch(int pitch, int transposition) const
{
    const auto selectedScale = static_cast<Scale>(juce::roundToInt(scaleParam->getCurrentValue()));
    if (selectedScale == Scale::Chromatic)
        return pitch + transposition;

    const auto& notes = selectedScale == Scale::Major ? majorScale : minorScale;
    const auto scaleRoot = juce::jlimit(0, 11, juce::roundToInt(rootParam->getCurrentValue()));
    const auto relative = pitch - scaleRoot;
    const auto octave = floorDivide(relative, 12);
    const auto pitchClass = positiveModulo(relative, 12);
    auto degree = 0;
    for (int i = 1; i < static_cast<int>(notes.size()); ++i)
        if (notes[static_cast<size_t>(i)] <= pitchClass)
            degree = i;
    // A chromatic passing note keeps its displacement from the scale note
    // below it rather than being silently quantised by the arp.
    const auto chromaticRemainder = pitchClass - notes[static_cast<size_t>(degree)];
    const auto targetDegree = octave * static_cast<int>(notes.size()) + degree + transposition;
    const auto targetOctave = floorDivide(targetDegree, static_cast<int>(notes.size()));
    const auto targetIndex = positiveModulo(targetDegree, static_cast<int>(notes.size()));
    return scaleRoot + targetOctave * 12 + notes[static_cast<size_t>(targetIndex)] + chromaticRemainder;
}

void RhinoArpDevice::clearActiveNotes()
{
    for (auto& note : heldNotes)
        note.active = false;
}

void RhinoArpDevice::releaseLatchedNotes()
{
    for (auto& note : heldNotes)
        if (!note.physicallyHeld)
            note.active = false;
}

void RhinoArpDevice::resetSequence(double beat)
{
    nextTickBeat = beat;
    sequenceStep = 0;
    clockValid = true;
    lastBeatRetriggerCycle = static_cast<std::int64_t>(std::floor((beat + 1.0e-9) / intervalBeats()));
}

void RhinoArpDevice::addPendingOff(double beat, int pitch, int channel, te::MPESourceID source)
{
    for (auto& pending : pendingOffs)
        if (!pending.active)
        {
            pending = {true, beat, pitch, channel, source};
            return;
        }
}

void RhinoArpDevice::flushPendingOffs(double blockStartSeconds, double blockEndSeconds,
                                      te::MidiMessageArray& output)
{
    for (auto& pending : pendingOffs)
    {
        if (!pending.active)
            continue;
        const auto offSeconds = edit.tempoSequence.toTime(
            tracktion::core::BeatPosition::fromBeats(pending.beat)).inSeconds();
        if (offSeconds >= blockEndSeconds)
            continue;
        output.addMidiMessage(juce::MidiMessage::noteOff(pending.channel, pending.pitch),
                              std::max(0.0, offSeconds - blockStartSeconds), pending.source);
        pending.active = false;
    }
}

void RhinoArpDevice::emitNote(double tickBeat, double offBeat, int sourcePitch,
                              double blockStartSeconds, double blockEndSeconds,
                              te::MidiMessageArray& output)
{
    if (!juce::isPositiveAndBelow(sourcePitch, static_cast<int>(heldNotes.size())))
        return;
    const auto noteCount = activeNoteCount();
    const auto patternLength = styleLength(noteCount);
    const auto transpositionStep = sequenceStep / std::max(1, patternLength);
    const auto transposeCount = juce::jlimit(0, 8, juce::roundToInt(stepsParam->getCurrentValue()));
    const auto transposeIndex = transpositionStep % (transposeCount + 1);
    const auto movement = juce::roundToInt(distanceParam->getCurrentValue()) * transposeIndex;
    const auto pitch = juce::jlimit(0, 127, transposedPitch(sourcePitch, movement));
    const auto& source = heldNotes[static_cast<size_t>(sourcePitch)];
    const auto tickSeconds = edit.tempoSequence.toTime(
        tracktion::core::BeatPosition::fromBeats(tickBeat)).inSeconds();
    output.addMidiMessage(juce::MidiMessage::noteOn(source.channel, pitch, source.velocity),
                          std::max(0.0, tickSeconds - blockStartSeconds), source.source);

    const auto offSeconds = edit.tempoSequence.toTime(
        tracktion::core::BeatPosition::fromBeats(offBeat)).inSeconds();
    if (offSeconds < blockEndSeconds)
        output.addMidiMessage(juce::MidiMessage::noteOff(source.channel, pitch),
                              std::max(0.0, offSeconds - blockStartSeconds), source.source);
    else
        addPendingOff(offBeat, pitch, source.channel, source.source);
}

void RhinoArpDevice::generateUntil(double endBeat, double blockStartSeconds, double blockEndSeconds,
                                   te::MidiMessageArray& output)
{
    constexpr double epsilon = 1.0e-9;
    while (clockValid && activeNoteCount() > 0 && nextTickBeat < endBeat - epsilon)
    {
        if (static_cast<Retrigger>(juce::roundToInt(retriggerParam->getCurrentValue())) == Retrigger::Beat)
        {
            const auto cycle = static_cast<std::int64_t>(std::floor((nextTickBeat + epsilon) / intervalBeats()));
            if (cycle != lastBeatRetriggerCycle)
            {
                sequenceStep = 0;
                lastBeatRetriggerCycle = cycle;
            }
        }

        const auto noteCount = activeNoteCount();
        const auto patternLength = styleLength(noteCount);
        const auto transposeCount = juce::jlimit(0, 8, juce::roundToInt(stepsParam->getCurrentValue()));
        const auto fullPatternLength = std::max(1, patternLength * (transposeCount + 1));
        const auto repeatLimit = juce::jlimit(0, 8, juce::roundToInt(repeatsParam->getCurrentValue()));
        const auto shouldPlay = repeatLimit == 0 || sequenceStep < fullPatternLength * repeatLimit;
        const auto stepLength = stepLengthBeats(sequenceStep);
        const auto offBeat = nextTickBeat + stepLength
            * juce::jlimit(10.0f, 200.0f, gateParam->getCurrentValue()) / 100.0;

        if (shouldPlay)
        {
            if (static_cast<Style>(juce::roundToInt(styleParam->getCurrentValue())) == Style::Chord)
            {
                for (int ordinal = 0; ordinal < noteCount; ++ordinal)
                    emitNote(nextTickBeat, offBeat, noteAtOrdinal(ordinal),
                             blockStartSeconds, blockEndSeconds, output);
            }
            else
            {
                emitNote(nextTickBeat, offBeat, noteAtOrdinal(ordinalForStep(sequenceStep, noteCount)),
                         blockStartSeconds, blockEndSeconds, output);
            }
        }
        nextTickBeat += stepLength;
        ++sequenceStep;
    }
}

void RhinoArpDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* midi = context.bufferForMidiMessages;
    if (midi == nullptr)
        return;

    SCOPED_REALTIME_CHECK
    auto blockStartSeconds = context.editTime.getStart().inSeconds();
    auto blockEndSeconds = context.editTime.getEnd().inSeconds();
    if (blockEndSeconds <= blockStartSeconds && context.bufferNumSamples > 0)
        blockEndSeconds = blockStartSeconds + context.bufferNumSamples / sampleRate;
    const auto blockStartBeat = edit.tempoSequence.toBeats(
        tracktion::core::TimePosition::fromSeconds(blockStartSeconds)).inBeats();
    const auto blockEndBeat = edit.tempoSequence.toBeats(
        tracktion::core::TimePosition::fromSeconds(blockEndSeconds)).inBeats();

    auto& output = firstRender ? firstOutput : steadyOutput;
    firstRender = false;
    output.clear();

    const auto discontinuity = clockValid
        && (blockStartBeat < lastBlockEndBeat - 1.0e-6 || blockStartBeat > lastBlockEndBeat + 0.05);
    output.isAllNotesOff = midi->isAllNotesOff || discontinuity;
    if (midi->isAllNotesOff)
        reset();
    else if (discontinuity)
    {
        for (auto& pending : pendingOffs)
            pending.active = false;
        resetSequence(blockStartBeat);
    }

    if (holdParam->getCurrentValue() < 0.5f)
        releaseLatchedNotes();
    flushPendingOffs(blockStartSeconds, blockEndSeconds, output);

    for (const auto& message : *midi)
    {
        const auto relativeSeconds = juce::jlimit(0.0, blockEndSeconds - blockStartSeconds,
                                                   message.getTimeStamp());
        const auto eventSeconds = blockStartSeconds + relativeSeconds;
        const auto eventBeat = edit.tempoSequence.toBeats(
            tracktion::core::TimePosition::fromSeconds(eventSeconds)).inBeats();
        // Everything already held is allowed to play up to, but not across,
        // the instant at which this input changes the chord.
        generateUntil(eventBeat, blockStartSeconds, blockEndSeconds, output);

        const auto pitch = message.getNoteNumber();
        if (message.isNoteOn() && juce::isPositiveAndBelow(pitch, static_cast<int>(heldNotes.size())))
        {
            const auto wasEmpty = activeNoteCount() == 0;
            const auto holding = holdParam->getCurrentValue() >= 0.5f;
            const auto hadPhysicalNotes = physicallyHeldCount() > 0;
            auto& note = heldNotes[static_cast<size_t>(pitch)];

            if (holding && !hadPhysicalNotes && !wasEmpty)
                clearActiveNotes();

            // While part of the latched chord is still under the hand, a
            // second press removes a latched note. With every key released,
            // the first new press starts a new chord instead.
            if (holding && hadPhysicalNotes && note.active && !note.physicallyHeld)
                note.active = false;
            else
            {
                note.active = true;
                note.velocity = message.getFloatVelocity();
                note.channel = message.getChannel();
                note.source = message.mpeSourceID;
            }
            note.physicallyHeld = true;

            const auto noteRetrigger = static_cast<Retrigger>(
                juce::roundToInt(retriggerParam->getCurrentValue())) == Retrigger::Note;
            if (wasEmpty || !clockValid || noteRetrigger || (holding && !hadPhysicalNotes))
                resetSequence(eventBeat);
            continue;
        }
        if (message.isNoteOff() && juce::isPositiveAndBelow(pitch, static_cast<int>(heldNotes.size())))
        {
            auto& note = heldNotes[static_cast<size_t>(pitch)];
            note.physicallyHeld = false;
            if (holdParam->getCurrentValue() < 0.5f)
                note.active = false;
            if (activeNoteCount() == 0)
                clockValid = false;
            continue;
        }
        output.add(message);
    }

    generateUntil(blockEndBeat, blockStartSeconds, blockEndSeconds, output);
    lastBlockEndBeat = blockEndBeat;
    midi->swapWith(output);
    midi->sortByTimestamp();
}

void RhinoArpDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, style, rateIndex, gatePercent, distance, steps,
                                     offset, groove, hold, retrigger, interval, repeats, root, scale);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
}
}
