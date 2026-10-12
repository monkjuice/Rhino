#include "RhinoArpDevice.h"
#include "ScaleQuantizer.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
int positiveModulo(std::int64_t value, int divisor)
{
    const auto remainder = static_cast<int>(value % divisor);
    return remainder < 0 ? remainder + divisor : remainder;
}

int floorDivide(int value, int divisor)
{
    const auto quotient = value / divisor;
    return value < 0 && value % divisor != 0 ? quotient - 1 : quotient;
}

// Pitch first keeps the pattern in pitch order even across MIDI channels.
int noteKey(int pitch, int channel) { return pitch * 16 + channel - 1; }
}

void RhinoArpDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    firstOutput.reserve(8192);
    steadyOutput.reserve(8192);
    firstRender = true;
    wasPlaying = false;
    resetPending.store(false);
    clearState();
}

void RhinoArpDevice::clearState()
{
    for (auto& note : heldNotes)
        note = {};
    for (auto& pending : pendingOffs)
        pending.active = false;
    clockValid = false;
    nextTickBeat = 0.0;
    lastBlockEndBeat = 0.0;
    sequenceStep = 0;
    nextRetriggerBeat = 0.0;
    lastRetriggerInterval = 0.0;
}

void RhinoArpDevice::reset()
{
    // Stop/panic can arrive from the message thread. Only the audio thread
    // may clear the chord and its outstanding releases.
    resetPending.store(true);
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
        default: return 0.25;    // 1/16
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

double RhinoArpDevice::grooveMultiplier(std::int64_t step) const
{
    const auto selected = static_cast<Groove>(juce::roundToInt(grooveParam->getCurrentValue()));
    if (selected == Groove::Straight)
        return 1.0;

    // 58/42 preserves the total length of each swing pair.
    const auto swingPair = selected == Groove::Swing16 ? 2 : std::max(2, juce::roundToInt(1.0 / rateBeats()));
    const auto place = positiveModulo(step, swingPair);
    if (place == 0) return 1.16;
    if (place == 1) return 0.84;
    return 1.0;
}

double RhinoArpDevice::toSeconds(double beat) const
{
    if (freeRunning)
        return freeAnchorSeconds + (beat - freeAnchorBeat) * freeSecondsPerBeat;
    return edit.tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beat)).inSeconds();
}

double RhinoArpDevice::toBeat(double seconds) const
{
    if (freeRunning)
        return freeAnchorBeat + (seconds - freeAnchorSeconds) / freeSecondsPerBeat;
    return edit.tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
}

double RhinoArpDevice::nextStepBeat(double tickBeat, std::int64_t step) const
{
    const auto multiplier = grooveMultiplier(step);
    if (rateModeParam->getCurrentValue() < 0.5f)
        return tickBeat + rateBeats() * multiplier;
    return toBeat(toSeconds(tickBeat)
        + juce::jlimit(10.0f, 2000.0f, freeRateParam->getCurrentValue()) * 0.001 * multiplier);
}

double RhinoArpDevice::noteOffBeat(double tickBeat, double nextBeat, std::int64_t step) const
{
    const auto gate = juce::jlimit(10.0f, 200.0f, gateParam->getCurrentValue()) / 100.0;
    if (rateModeParam->getCurrentValue() < 0.5f)
        return tickBeat + (nextBeat - tickBeat) * gate;
    return toBeat(toSeconds(tickBeat)
        + juce::jlimit(10.0f, 2000.0f, freeRateParam->getCurrentValue()) * 0.001
            * grooveMultiplier(step) * gate);
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
    for (int key = 0; key < static_cast<int>(heldNotes.size()); ++key)
        if (heldNotes[static_cast<size_t>(key)].active && ordinal-- == 0)
            return key;
    return -1;
}

int RhinoArpDevice::styleLength(int noteCount) const
{
    const auto selected = static_cast<Style>(juce::roundToInt(styleParam->getCurrentValue()));
    if (selected == Style::Chord || noteCount <= 1) return 1;
    if (selected == Style::UpDown || selected == Style::DownUp) return noteCount * 2 - 2;
    return noteCount;
}

int RhinoArpDevice::ordinalForStep(std::int64_t step, int noteCount) const
{
    if (noteCount <= 1) return 0;
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
    const auto selectedScale = static_cast<MusicalScale>(juce::roundToInt(scaleParam->getCurrentValue()));
    if (selectedScale == MusicalScale::Chromatic) return pitch + transposition;

    // Share Rhino Tune's scale definitions, including scales with five or six
    // degrees. The existing Chromatic/Major/Minor saved indices stay intact.
    std::array<int, 12> notes {};
    int size = 0;
    const auto bits = musicalScaleBits(selectedScale);
    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        if ((bits >> pitchClass) & 1u)
            notes[static_cast<size_t>(size++)] = pitchClass;
    const auto scaleRoot = juce::jlimit(0, 11, juce::roundToInt(rootParam->getCurrentValue()));
    const auto relative = pitch - scaleRoot;
    const auto octave = floorDivide(relative, 12);
    const auto pitchClass = positiveModulo(relative, 12);
    auto degree = 0;
    for (int i = 1; i < size; ++i)
        if (notes[static_cast<size_t>(i)] <= pitchClass) degree = i;
    // Keep a passing note's displacement rather than quantising the input.
    const auto remainder = pitchClass - notes[static_cast<size_t>(degree)];
    const auto target = octave * size + degree + transposition;
    return scaleRoot + floorDivide(target, size) * 12
        + notes[static_cast<size_t>(positiveModulo(target, size))] + remainder;
}

void RhinoArpDevice::clearActiveNotes()
{
    for (auto& note : heldNotes) note.active = false;
}

void RhinoArpDevice::releaseLatchedNotes()
{
    for (auto& note : heldNotes)
        if (!note.physicallyHeld) note.active = false;
}

void RhinoArpDevice::resetSequence(double beat)
{
    nextTickBeat = beat;
    sequenceStep = 0;
    clockValid = true;
    nextRetriggerBeat = (std::floor((beat + 1.0e-9) / intervalBeats()) + 1.0) * intervalBeats();
}

void RhinoArpDevice::endOutputNote(int key, double timestamp, te::MidiMessageArray& output)
{
    auto& pending = pendingOffs[static_cast<size_t>(key)];
    if (!pending.active) return;
    output.addMidiMessage(juce::MidiMessage::noteOff(key % 16 + 1, key / 16),
                          std::max(0.0, timestamp), pending.source);
    pending.active = false;
}

void RhinoArpDevice::releaseInactiveNotes(double timestamp, te::MidiMessageArray& output)
{
    for (int key = 0; key < static_cast<int>(pendingOffs.size()); ++key)
    {
        const auto& pending = pendingOffs[static_cast<size_t>(key)];
        if (pending.active && !heldNotes[static_cast<size_t>(pending.inputKey)].active)
            endOutputNote(key, timestamp, output);
    }
    if (activeNoteCount() == 0) clockValid = false;
}

void RhinoArpDevice::flushPendingOffs(double endBeat, double blockStartSeconds, bool inclusive,
                                     te::MidiMessageArray& output)
{
    for (int key = 0; key < static_cast<int>(pendingOffs.size()); ++key)
    {
        const auto& pending = pendingOffs[static_cast<size_t>(key)];
        if (pending.active && (inclusive ? pending.beat <= endBeat + 1.0e-9 : pending.beat < endBeat - 1.0e-9))
            endOutputNote(key, toSeconds(pending.beat) - blockStartSeconds, output);
    }
}

void RhinoArpDevice::emitNote(double tickBeat, double offBeat, int inputKey,
                             double blockStartSeconds, te::MidiMessageArray& output)
{
    if (!juce::isPositiveAndBelow(inputKey, static_cast<int>(heldNotes.size()))) return;
    const auto patternLength = styleLength(activeNoteCount());
    const auto transposeCount = juce::jlimit(0, 8, juce::roundToInt(stepsParam->getCurrentValue()));
    const auto transposeIndex = static_cast<int>((sequenceStep / patternLength) % (transposeCount + 1));
    const auto movement = juce::roundToInt(distanceParam->getCurrentValue()) * transposeIndex;
    const auto pitch = juce::jlimit(0, 127, transposedPitch(inputKey / 16, movement));
    const auto channel = inputKey % 16 + 1;
    const auto key = noteKey(pitch, channel);
    const auto& source = heldNotes[static_cast<size_t>(inputKey)];
    auto& pending = pendingOffs[static_cast<size_t>(key)];
    // Clamping at a MIDI range boundary can collapse several chord tones to
    // the same output. Sound that pitch once at this tick.
    if (pending.active && std::abs(pending.startedBeat - tickBeat) < 1.0e-9) return;
    const auto timestamp = std::max(0.0, toSeconds(tickBeat) - blockStartSeconds);
    // MIDI 1 has no identity for overlapping instances of a channel/pitch.
    // End the previous instance before starting another, so an old gate can
    // never cut a new note and no retrigger can exhaust release storage.
    endOutputNote(key, timestamp, output);
    output.addMidiMessage(juce::MidiMessage::noteOn(channel, pitch, source.velocity), timestamp, source.source);
    pending = {true, offBeat, tickBeat, inputKey, source.source};
}

void RhinoArpDevice::generateUntil(double endBeat, double blockStartSeconds, te::MidiMessageArray& output)
{
    constexpr double epsilon = 1.0e-9;
    while (clockValid && activeNoteCount() > 0)
    {
        const auto beatRetrigger = static_cast<Retrigger>(juce::roundToInt(retriggerParam->getCurrentValue()))
            == Retrigger::Beat;
        // A beat boundary is an event in its own right. Waiting for the next
        // regular tick drifts with swing or a free rate such as 80 ms.
        const auto retriggerNow = beatRetrigger && nextRetriggerBeat <= nextTickBeat + epsilon;
        const auto tickBeat = retriggerNow ? nextRetriggerBeat : nextTickBeat;
        if (tickBeat >= endBeat - epsilon) break;
        if (retriggerNow)
        {
            sequenceStep = 0;
            nextRetriggerBeat += intervalBeats();
        }
        flushPendingOffs(tickBeat, blockStartSeconds, true, output);
        const auto noteCount = activeNoteCount();
        const auto transposeCount = juce::jlimit(0, 8, juce::roundToInt(stepsParam->getCurrentValue()));
        const auto fullPatternLength = styleLength(noteCount) * (transposeCount + 1);
        const auto repeatLimit = juce::jlimit(0, 8, juce::roundToInt(repeatsParam->getCurrentValue()));
        const auto followingTick = nextStepBeat(tickBeat, sequenceStep);
        if (repeatLimit == 0 || sequenceStep < fullPatternLength * repeatLimit)
        {
            const auto offBeat = noteOffBeat(tickBeat, followingTick, sequenceStep);
            if (static_cast<Style>(juce::roundToInt(styleParam->getCurrentValue())) == Style::Chord)
            {
                for (int key = 0; key < static_cast<int>(heldNotes.size()); ++key)
                    if (heldNotes[static_cast<size_t>(key)].active)
                        emitNote(tickBeat, offBeat, key, blockStartSeconds, output);
            }
            else
                emitNote(tickBeat, offBeat, noteAtOrdinal(ordinalForStep(sequenceStep, noteCount)),
                         blockStartSeconds, output);
        }
        nextTickBeat = followingTick;
        ++sequenceStep;
    }
    // Keep releases pending until the scheduler actually reaches them. Writing
    // future offs at note-on time prevents a later input release from cancelling
    // them and makes behavior depend on the host's block size.
    flushPendingOffs(endBeat, blockStartSeconds, false, output);
}

void RhinoArpDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* midi = context.bufferForMidiMessages;
    if (midi == nullptr) return;

    SCOPED_REALTIME_CHECK
    auto& output = firstRender ? firstOutput : steadyOutput;
    firstRender = false;
    output.clear();
    const auto resetRequested = resetPending.exchange(false);
    output.isAllNotesOff = midi->isAllNotesOff || resetRequested || (wasPlaying && !context.isPlaying);
    if (output.isAllNotesOff) clearState();
    wasPlaying = context.isPlaying;

    const auto blockStartSeconds = context.editTime.getStart().inSeconds();
    auto blockEndSeconds = context.editTime.getEnd().inSeconds();
    if (blockEndSeconds <= blockStartSeconds && context.bufferNumSamples > 0)
        blockEndSeconds = blockStartSeconds + context.bufferNumSamples / sampleRate;
    // Fresh live notes still arpeggiate with a stopped, frozen playhead. A
    // playing-to-stopped transition above first clears the previous pattern.
    freeRunning = !context.isPlaying;
    if (freeRunning)
    {
        const auto here = edit.tempoSequence.toBeats(
            tracktion::core::TimePosition::fromSeconds(blockStartSeconds)).inBeats();
        const auto beatLater = edit.tempoSequence.toTime(
            tracktion::core::BeatPosition::fromBeats(here + 1.0)).inSeconds();
        freeSecondsPerBeat = std::max(1.0e-3, beatLater - blockStartSeconds);
        freeAnchorSeconds = blockStartSeconds;
        freeAnchorBeat = clockValid ? lastBlockEndBeat : here;
    }
    const auto blockStartBeat = toBeat(blockStartSeconds);
    const auto blockEndBeat = toBeat(blockEndSeconds);
    if (clockValid && (blockStartBeat < lastBlockEndBeat - 1.0e-6 || blockStartBeat > lastBlockEndBeat + 0.05))
    {
        output.isAllNotesOff = true;
        clearState();
    }
    if (holdParam->getCurrentValue() < 0.5f)
    {
        releaseLatchedNotes();
        releaseInactiveNotes(0.0, output);
    }
    const auto retriggerInterval = static_cast<Retrigger>(juce::roundToInt(retriggerParam->getCurrentValue())) == Retrigger::Beat
        ? intervalBeats() : 0.0;
    if (retriggerInterval != lastRetriggerInterval)
    {
        if (retriggerInterval > 0.0)
            nextRetriggerBeat = std::ceil((blockStartBeat - 1.0e-9) / retriggerInterval) * retriggerInterval;
        lastRetriggerInterval = retriggerInterval;
    }

    for (const auto& message : *midi)
    {
        const auto timestamp = juce::jlimit(0.0, blockEndSeconds - blockStartSeconds, message.getTimeStamp());
        const auto eventBeat = toBeat(blockStartSeconds + timestamp);
        generateUntil(eventBeat, blockStartSeconds, output);
        const auto channel = message.getChannel();
        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (int pitch = 0; pitch < 128; ++pitch)
                heldNotes[static_cast<size_t>(noteKey(pitch, channel))] = {};
            releaseInactiveNotes(timestamp, output);
            output.add(message);
            continue;
        }
        if (message.isNoteOnOrOff())
        {
            const auto key = noteKey(message.getNoteNumber(), channel);
            auto& note = heldNotes[static_cast<size_t>(key)];
            const auto holding = holdParam->getCurrentValue() >= 0.5f;
            if (message.isNoteOn())
            {
                const auto wasEmpty = activeNoteCount() == 0;
                const auto hadPhysicalNotes = physicallyHeldCount() > 0;
                if (holding && !hadPhysicalNotes && !wasEmpty) clearActiveNotes();
                // A fresh chord replaces a latch; re-pressing a latched key
                // while other keys stay down removes it from that chord.
                if (holding && hadPhysicalNotes && note.active && !note.physicallyHeld)
                    note.active = false;
                else
                {
                    note.active = true;
                    note.velocity = message.getFloatVelocity();
                    note.source = message.mpeSourceID;
                }
                note.physicallyHeld = true;
                releaseInactiveNotes(timestamp, output);
                const auto noteRetrigger = static_cast<Retrigger>(juce::roundToInt(retriggerParam->getCurrentValue()))
                    == Retrigger::Note;
                if (wasEmpty || !clockValid || noteRetrigger || (holding && !hadPhysicalNotes)) resetSequence(eventBeat);
            }
            else
            {
                note.physicallyHeld = false;
                if (!holding) note.active = false;
                releaseInactiveNotes(timestamp, output);
            }
            continue;
        }
        output.add(message);
    }

    generateUntil(blockEndBeat, blockStartSeconds, output);
    lastBlockEndBeat = blockEndBeat;
    midi->swapWith(output);
    midi->sortByTimestamp();
}
}
