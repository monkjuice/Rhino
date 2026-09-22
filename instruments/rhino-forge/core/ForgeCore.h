#pragma once

#include "ForgeArp.h"
#include "ForgePatch.h"

#include <algorithm>
#include <cstdint>

// The reusable sound engine. It deliberately owns no AudioProcessor, UI,
// Tracktion, state tree, filesystem, or allocation in renderSample().
//
// This file is the voice: allocation and stealing, the envelopes and LFOs
// inside it, the oscillators, the filter, and the metering the panel reads back
// out. What a voice is made of is declared next door, and this includes the
// chain of it:
//
//   ForgeVoiceParts.h  the counts, and the settings one voice holds
//   ForgeShapes.h      the ten waves, the six sub shapes, and their tables
//   ForgeMatrix.h      what can modulate what, and the eight slots
//   ForgePatch.h       the whole patch, and the arithmetic that reads it
//   ForgeArp.h         the arpeggiator, which is upstream of every voice
//
// The arp is the one of those this class never calls. It stands in front of
// Core rather than inside it — it turns the keys being held into the notes
// Core is asked to play — so the Processor owns it and drives it, and nothing
// below this line knows it exists.
//
// Headers rather than translation units, all of it inline, because
// renderSample() is compiled into the processor and there is no link-time code
// generation here to put back what a call across a .cpp boundary would cost.
//
// The effects are not this file's either: they live in ForgeFxDsp.h and run on
// the summed voices rather than inside them, the way an insert after the synth
// does. Core owns the three racks because they hold delay lines and filter
// state, and hands each of them the patch that describes it.
namespace rhino::forge
{
class Core final
{
public:
    void initialise(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        tailStep = static_cast<float>(1.0 / (sampleRate * voiceTailSeconds));
        dcBlock = warpDcCoefficient(sampleRate);
        // Touched here so the tables are built on whichever thread prepares
        // the synth, never lazily on the first note from the audio thread.
        builtInWavetable();
        subWavetable();
        // Every rack's delay lines are sized here, which is the one place they
        // may be: a slot's type changes while audio is running, so each slot
        // carries every type's state and none of it can be built on demand.
        for (auto& rack : racks) rack.prepare(sampleRate);
        // And the filter's, for the same reason and in the same place: the
        // type in the module changes while audio is running, so every voice
        // carries the comb's line whether or not a comb is what is selected.
        // They live here rather than in the voice because a fresh note resets
        // a voice by assigning over it — see FilterDelays in ForgeFilter.h.
        for (auto& voice : filterDelays)
            for (auto& channel : voice)
                channel.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        voices = {};
        for (auto& voice : filterDelays)
            for (auto& channel : voice)
                channel.reset();
        nextVoice = 0;
        freePhase = {};
        freeHeld = {};
        meterLfoPhase = {};
        meterLfoHeld = {};
        meterLfoValue = {};
        noiseState = 0x9e3779b9u;
        heldCount = 0;
        monoMode = false;
        meterEnvelope = {};
        meterStage = {};
        meterOffsets = {};
        for (auto& rack : racks) rack.reset();
    }

    void noteOn(int note, float velocity)
    {
        noteOn(note, velocity, Patch {});
    }

    void noteOn(int note, float velocity, const Patch& patch)
    {
        monoMode = patch.mono >= 0.5f;
        if (monoMode)
        {
            hold(note);
            auto& voice = voices[0];
            const auto continueEnvelope = voice.active && patch.legato >= 0.5f;
            if (!continueEnvelope)
            {
                startVoice(voice, note, velocity);
                retriggerLfo(voice, patch);
            }
            else
            {
                voice.note = note;
                voice.targetHz = noteFrequency(note);
                voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
            }
            if (patch.glide <= 0.0001f) voice.currentHz = voice.targetHz;
            return;
        }

        const auto voiceCount = static_cast<size_t>(juce::jlimit(1, static_cast<int>(voices.size()), juce::roundToInt(patch.polyphony)));
        auto& voice = voices[allocate(voiceCount)];
        startVoice(voice, note, velocity);
        retriggerLfo(voice, patch);
    }

    void noteOff(int note)
    {
        if (monoMode)
        {
            releaseHeld(note);
            auto& voice = voices[0];
            if (voice.active && voice.note == note && heldCount > 0)
            {
                voice.note = heldNotes[static_cast<size_t>(heldCount - 1)];
                voice.targetHz = noteFrequency(voice.note);
                return;
            }
        }
        // Every envelope is released by the key that started it, the auxiliary
        // ones included: an envelope that only fell when the amp did would be a
        // second shape with no release of its own.
        for (auto& voice : voices)
            if (voice.active && voice.note == note && voice.envStage[ampEnv] != EnvelopeStage::release)
                for (int env = 0; env < envCount; ++env)
                {
                    const auto i = static_cast<size_t>(env);
                    voice.envStage[i] = EnvelopeStage::release;
                    voice.envReleaseStart[i] = voice.envelope[i];
                }
    }

    void allNotesOff() { reset(); }

    // What an envelope is doing, for the display to draw. Taken from the
    // loudest sounding voice, which is the one a player is listening to — the
    // loudest by ENV 1, so all four readings come from one voice rather than
    // each from whichever voice happens to have that envelope highest. Plain
    // members rather than atomics: Core stays a pure DSP class and the
    // processor owns the hand-off to the message thread.
    float envelopeLevel(int env = ampEnv) const
    {
        return env >= 0 && env < envCount ? meterEnvelope[static_cast<size_t>(env)] : 0.0f;
    }

    int envelopeStage(int env = ampEnv) const
    {
        return env >= 0 && env < envCount
            ? static_cast<int>(meterStage[static_cast<size_t>(env)]) : 0;
    }

    // Where LFO 1 is in its cycle and what it last put out. The phase draws the
    // running indicator; the value is published as well because a
    // sample-and-hold's step cannot be worked back out of the phase.
    float lfoPosition(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount ? meterLfoPhase[static_cast<size_t>(lfo)] : 0.0f;
    }

    float lfoOutput(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount ? meterLfoValue[static_cast<size_t>(lfo)] : 0.0f;
    }

    // How far the matrix is moving each destination right now, in that
    // destination's normalised space, so a knob can draw where its value
    // actually is while a source plays it. Same reading the loudest voice is
    // rendering with, for the same reason ENV 1's display follows that voice.
    //
    // Zero with nothing sounding, because with no voice there is no modulated
    // value: a source only reaches a destination through a voice. That is the
    // same rule ENV 1's display follows, and it is what stops the panel
    // animating a patch that is making no sound.
    float modulationOffset(int destination) const
    {
        return destination > 0 && destination < destinationCount
            ? meterOffsets[static_cast<size_t>(destination)] : 0.0f;
    }

    // A destination is modulated in the same normalised space its knob moves
    // in, so a depth of 1.0 means "from here to the top of the knob's travel"
    // whatever the underlying units or skew are. The Processor hands these
    // ranges over at prepare time, so nothing here duplicates a parameter range.
    void setDestinationRange(int destination, juce::NormalisableRange<float> range)
    {
        if (destination > 0 && destination < destinationCount)
            destinationRanges[static_cast<size_t>(destination)] = range;
    }

    void renderSample(const Patch& patch, float& left, float& right)
    {
        renderSample(patch, Modulation {}, left, right);
    }

    // The tempo a synced delay or chorus divides. Set by the Processor once per
    // block, the same reading a synced LFO is resolved against — except that an
    // LFO is resolved before the patch is built and a rack is read from it, so
    // the rack needs the tempo itself rather than a rate worked out from it.
    void setTempo(double bpm) { tempo = bpm; }

    // Performance wheels are global MIDI controls. Pitch bends every source in
    // a voice by the conventional two semitones; modulation is a matrix source.
    void setPitchWheel(float position)
    {
        pitchRatio = std::pow(2.0f, juce::jlimit(-1.0f, 1.0f, position) * (2.0f / 12.0f));
    }
    void setModWheel(float position) { modWheel = juce::jlimit(0.0f, 1.0f, position); }

    void renderSample(const Patch& patch, const Modulation& modulation, float& left, float& right)
    {
        left = right = 0.0f;

        // Which LFOs anything actually reads. Six of them stepping through a
        // sine for every voice would be five sixths of that work thrown away
        // when one is routed, so the values are only worked out where they are
        // wanted. The phases still move either way — an LFO nothing is pointed
        // at yet is still one the panel draws running.
        std::array<bool, lfoCount> used {};
        for (const auto& slot : modulation.slots)
        {
            if (slot.destination < 0.5f || slot.depth == 0.0f) continue;
            const auto lfo = lfoIndexOf(juce::roundToInt(slot.source));
            if (lfo >= 0) used[static_cast<size_t>(lfo)] = true;
        }

        // The free-running cycles. An LFO in OFF reads these, and they run
        // whether or not a note is playing, which is the whole of what OFF
        // means. One cycle shared by every voice, so a rate set in beats stays
        // in step with the host across a phrase.
        std::array<float, lfoCount> freeValue {};
        for (int i = 0; i < lfoCount; ++i)
        {
            const auto index = static_cast<size_t>(i);
            const auto& setting = patch.lfos[index];
            const auto free = lfoModeOf(setting) == LfoMode::free;
            if (free && used[index])
                freeValue[index] = lfoValue(setting, freePhase[index], freeHeld[index]);

            // What the panel is shown, before any voice has had its say: the
            // free-running phase for an LFO in OFF, and the start of the shape
            // for one that answers the keyboard — which is exactly where it is
            // with nothing playing, and where the next key press will begin it.
            meterLfoPhase[index] = free ? freePhase[index] : 0.0f;
            meterLfoHeld[index] = free ? freeHeld[index] : 0.0f;

            const auto advanced = freePhase[index]
                + juce::jlimit(0.01f, 40.0f, setting.rate) / static_cast<float>(sampleRate);
            if (advanced >= 1.0f) freeHeld[index] = noise();
            freePhase[index] = wrap(advanced);
        }

        const auto modulated = modulation.anyActive();
        // Whether anything is pointed at a rack at all. A rack runs once on the
        // summed voices, so a per-voice source reaching one has to be resolved
        // to a single voice's value — and carrying a copy of every rack out of
        // the loop to do that is only worth it when something actually is.
        auto fxModulated = false;
        if (modulated)
            for (const auto& slot : modulation.slots)
                if (slot.depth != 0.0f && slot.source >= 0.5f
                    && juce::roundToInt(slot.destination) >= fxDestinationBase)
                    fxModulated = true;
        const Patch* fxPatch = &patch;

        meterEnvelope = {};
        meterStage = {};
        meterOffsets = {};

        // What every voice has sent to each bus. Filled inside the loop and
        // resolved once after it.
        std::array<float, busCount> busLeft {}, busRight {};

        // By index rather than by reference, because a voice's delay lines sit
        // beside it in Core rather than inside it and are reached by the same
        // index. Nothing else in the loop cares which voice it is on.
        for (size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
        {
            auto& voice = voices[voiceIndex];
            if (!voice.active) continue;
            // All four, whether or not anything reads them. An envelope's value
            // is its state rather than something worked out from a phase, so
            // one skipped while nothing points at it would come back wrong the
            // moment something did.
            for (int env = 0; env < envCount; ++env)
            {
                const auto i = static_cast<size_t>(env);
                const auto& shape = patch.envs[i];
                updateEnvelope(voice.envelope[i], voice.envStage[i], voice.envReleaseStart[i],
                               shape.attack, shape.decay, shape.sustain, shape.release);
            }
            // A voice whose envelope has run out is not finished: the filter
            // it fed is still ringing, and at a low cutoff that ring is loud
            // enough to hear. Dropping the voice here truncates it, and a
            // truncated ring is the click at the end of a note — loudest with
            // the sub on, because a sine an octave down is exactly what a low
            // cutoff passes. So it is faded out instead, and the ring decays
            // into the fade.
            if (voice.envStage[ampEnv] == EnvelopeStage::idle)
            {
                voice.tail -= tailStep;
                if (voice.tail <= 0.0f)
                {
                    voice.active = false;
                    continue;
                }
            }
            const auto loudest = voice.envelope[ampEnv] >= meterEnvelope[ampEnv];
            if (loudest)
            {
                meterEnvelope = voice.envelope;
                meterStage = voice.envStage;
            }

            // Every LFO that answers the keyboard runs inside the voice, so a
            // new note restarts its own shape and leaves the notes already
            // sounding where they were. An LFO in OFF is the one cycle
            // everything shares, and the voice simply reads it.
            std::array<float, lfoCount> lfoValues {};
            for (int i = 0; i < lfoCount; ++i)
            {
                const auto index = static_cast<size_t>(i);
                const auto& setting = patch.lfos[index];
                const auto mode = lfoModeOf(setting);
                if (mode == LfoMode::free)
                {
                    lfoValues[index] = freeValue[index];
                    continue;
                }
                if (used[index])
                    lfoValues[index] = lfoValue(setting, voice.lfoPhase[index], voice.lfoHeld[index]);
                // The panel follows the loudest voice, exactly as ENV 1's
                // display does and for the same reason: that is the note a
                // player is listening to.
                if (loudest)
                {
                    meterLfoPhase[index] = voice.lfoPhase[index];
                    meterLfoHeld[index] = voice.lfoHeld[index];
                }
                advanceVoiceLfo(voice, i, setting, mode);
            }

            // Modulation is per voice and per sample: every source is per voice
            // now that the LFOs are, and every destination is read inside the
            // voice. With no live slot the patch is used as it stands and
            // nothing is copied.
            const Patch* voicePatch = &patch;
            if (modulated)
            {
                scratch = patch;
                applyModulation(scratch, modulation, voice, lfoValues, loudest);
                voicePatch = &scratch;
            }
            const auto& active = *voicePatch;

            // The loudest voice is the one every display already follows, and
            // it is the one a rack follows too: one process fed by every note
            // cannot have a value per note.
            if (fxModulated && loudest) { fxScratch.racks = active.racks; fxPatch = &fxScratch; }

            Buses buses;
            renderOscillators(voice, active, buses);

            // Drive belongs to the filter, so only what is routed into it is
            // driven, and switching the module off bypasses the drive with it.
            // The filter state keeps running either way, so switching the
            // module or a route back on does not click.
            const auto inputLeft = buses.wetLeft, inputRight = buses.wetRight;
            auto routedLeft = inputLeft, routedRight = inputRight;
            // Worked out once for the voice and read by both its channels. The
            // corners are per voice because the matrix is — a modulated cutoff
            // is a different frequency in every note — so this cannot move up
            // out of the loop, but it can and does stop being computed twice.
            const auto coefficients = filterCoefficientsFor(filterShapeOf(active, sampleRate));
            auto& delays = filterDelays[voiceIndex];
            if (on(active.filterEnable))
            {
                routedLeft = filterSample(voice.filters[0], delays[0],
                                          saturate(routedLeft, active.drive), coefficients);
                routedRight = filterSample(voice.filters[1], delays[1],
                                           saturate(routedRight, active.drive), coefficients);
                // MIX blends what came out against what went in. With the
                // module switched off there is nothing to blend — the two are
                // the same signal — so the knob is skipped rather than applied
                // to a pair of identical values.
                const auto mix = juce::jlimit(0.0f, 1.0f, active.filterMix);
                routedLeft = routedLeft * mix + inputLeft * (1.0f - mix);
                routedRight = routedRight * mix + inputRight * (1.0f - mix);
            }
            else
            {
                filterSample(voice.filters[0], delays[0], routedLeft, coefficients);
                filterSample(voice.filters[1], delays[1], routedRight, coefficients);
            }

            // The rest of the filter's channel: its place in the image and its
            // own fader, then its sends, which are taken after that fader
            // exactly as every other channel's are.
            const auto filterGain = juce::jlimit(0.0f, 1.0f, active.filterLevel);
            routedLeft *= filterGain * channelPanLeft(active.filterPan);
            routedRight *= filterGain * channelPanRight(active.filterPan);
            send(routedLeft, routedRight, active.sendFilter, buses);

            left += (routedLeft + buses.dryLeft) * voice.tail;
            right += (routedRight + buses.dryRight) * voice.tail;
            // The busses are one sum across every voice rather than one per
            // voice. It makes no difference to a gain, and it is what an
            // effects rack will need when one arrives: a reverb on a bus is a
            // single tail fed by every note, not a copy per note.
            for (int bus = 0; bus < busCount; ++bus)
            {
                const auto index = static_cast<size_t>(bus);
                busLeft[index] += buses.sendLeft[index] * voice.tail;
                busRight[index] += buses.sendRight[index] * voice.tail;
            }
        }

        resolveBuses(*fxPatch, busLeft, busRight, left, right, racks, tempo);

        // Everything that reached the main output, through the main rack, and
        // only then through the master level — which is the order Serum states:
        // audio routed to MAIN passes the modules, and then the master volume.
        racks[0].process(fxPatch->racks[0], tempo, left, right);

        // The values the panel draws, worked out once from the phases the loop
        // settled on rather than per voice: a sample and hold's step cannot be
        // read back out of its phase, so it has to be carried this far.
        for (int i = 0; i < lfoCount; ++i)
            meterLfoValue[static_cast<size_t>(i)] =
                lfoValue(patch.lfos[static_cast<size_t>(i)],
                         meterLfoPhase[static_cast<size_t>(i)], meterLfoHeld[static_cast<size_t>(i)]);

        const auto gain = juce::jlimit(0.0f, 1.25f, patch.output) * 0.28f;
        left = softClip(left * gain);
        right = softClip(right * gain);
    }

private:
    // Each bus given its level and its place, then handed to wherever it goes.
    //
    // A bus pointed at the other is folded in first, so the one being fed is
    // resolved last and arrives at the output carrying both. Two busses pointed
    // at each other is a loop with no answer; the second of the pair goes to the
    // main output instead, which is a setting the panel then never has to
    // refuse. Written for two busses, because "the other bus" is only a thing
    // there are two of.
    static void resolveBuses(const Patch& patch, std::array<float, busCount>& busLeft,
                             std::array<float, busCount>& busRight, float& left, float& right,
                             std::array<FxRack, rackCount>& racks, double tempo)
    {
        static_assert(busCount == 2, "resolveBuses routes a bus to 'the other one'");
        const auto crossed = [&patch] (int bus)
        {
            const auto& settings = patch.buses[static_cast<size_t>(bus)];
            return on(settings.enable) && on(settings.dest);
        };

        // A bus feeding the other is resolved first, whichever of the two it is.
        std::array<int, busCount> order {0, 1};
        if (crossed(1) && !crossed(0)) order = {1, 0};

        for (int i = 0; i < busCount; ++i)
        {
            const auto bus = order[static_cast<size_t>(i)];
            const auto index = static_cast<size_t>(bus);
            const auto& settings = patch.buses[index];
            if (!on(settings.enable)) continue;

            // A bus's rack sits between what arrived and the bus's own fader,
            // so the fader sets how much of the processed signal is heard
            // rather than how hard the rack is driven. Rack 0 is the main
            // output's, so bus n uses rack n + 1.
            racks[static_cast<size_t>(bus + 1)].process(patch.racks[static_cast<size_t>(bus + 1)],
                                                        tempo, busLeft[index], busRight[index]);

            const auto gain = juce::jlimit(0.0f, 1.0f, settings.level);
            const auto outLeft = busLeft[index] * gain * channelPanLeft(settings.pan);
            const auto outRight = busRight[index] * gain * channelPanRight(settings.pan);

            const auto other = static_cast<size_t>(1 - bus);
            // The second of a mutually crossed pair: its destination would be
            // the bus that has already been folded into it.
            const auto loop = i == busCount - 1 && crossed(1 - bus);
            if (on(settings.dest) && !loop)
            {
                busLeft[other] += outLeft;
                busRight[other] += outRight;
            }
            else
            {
                left += outLeft;
                right += outRight;
            }
        }
    }

    enum class EnvelopeStage { idle, attack, decay, sustain, release };

    // What a voice accumulates into: the sources routed through the filter, the
    // sources that bypass it, and what every channel has sent to each bus.
    //
    // A send is parallel to wherever the channel is already going, so a source
    // appears in one of the first two pairs and in as many of the send pairs as
    // it is sent to.
    struct Buses
    {
        float wetLeft = 0.0f, wetRight = 0.0f, dryLeft = 0.0f, dryRight = 0.0f;
        std::array<float, busCount> sendLeft {}, sendRight {};
    };

    // One channel's signal handed to the destination it names and to whichever
    // busses it is sent to. The routing decision is made here, once, rather
    // than being threaded through everything downstream.
    static void distribute(float left, float right, bool throughFilter,
                           const Sends& sends, Buses& buses)
    {
        (throughFilter ? buses.wetLeft : buses.dryLeft) += left;
        (throughFilter ? buses.wetRight : buses.dryRight) += right;
        send(left, right, sends, buses);
    }

    static void send(float left, float right, const Sends& sends, Buses& buses)
    {
        for (int bus = 0; bus < busCount; ++bus)
        {
            const auto index = static_cast<size_t>(bus);
            const auto amount = juce::jlimit(0.0f, 1.0f, sends.amount[index]);
            if (amount <= 0.0f) continue;
            buses.sendLeft[index] += left * amount;
            buses.sendRight[index] += right * amount;
        }
    }

    struct Voice
    {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        std::array<float, unisonMax> phaseA {}, phaseB {};
        // What each oscillator's warp stages are holding on to, one set per
        // member of the stack: the filter modes' state and FM SELF's last
        // output. Per member rather than per oscillator because every member is
        // reading the table at a phase of its own — one filter shared by twelve
        // detuned copies would be a filter fed twelve different signals.
        std::array<std::array<WarpState, warpSlots>, unisonMax> warpA {}, warpB {};
        // What an asymmetric warp leaves behind, taken off each oscillator's
        // output rather than off every member of its stack: the offset is the
        // same in all of them, so blocking it once after the sum is the same
        // answer for a twelfth of the work.
        std::array<WarpDcBlocker, 2> dcA {}, dcB {};
        float phaseSub = 0.0f;
        float currentHz = 0.0f, targetHz = 0.0f;
        // ENV 1 at index zero is the amplitude this voice is rendered at and
        // the one that says when it is finished; ENV 2-4 are carried for the
        // matrix to read and affect nothing on their own.
        std::array<float, envCount> envelope {}, envReleaseStart {};
        // Full until the envelope has finished, then run down to nothing so
        // whatever the filter is still ringing with is let go of rather than
        // cut off. It is also what guarantees the voice comes back: a filter
        // pushed hard would otherwise ring for a long time.
        float tail = 1.0f;
        // One filter per channel. Both run whatever the type is, so switching
        // the module or a route back on never starts a filter from silence —
        // and a comb keeps its line, which is the one type where starting from
        // silence would be audible for several milliseconds rather than one
        // sample. See ForgeFilter.h for what is in each of them.
        std::array<FilterState, 2> filters {};
        std::array<EnvelopeStage, envCount> envStage {};
        // Every LFO that answers the keyboard runs a copy of itself inside each
        // voice, which is what makes TRIG and ENV mean anything: a new note
        // restarts its own shape and leaves the notes already sounding alone.
        // An LFO in OFF ignores these and reads the free-running phase instead.
        std::array<float, lfoCount> lfoPhase {};
        std::array<float, lfoCount> lfoHeld {};
        std::array<bool, lfoCount> lfoStopped {};
    };

    // Each live slot nudges its destination in normalised space and the result
    // is converted back to the destination's own units, so one depth control
    // behaves the same whether it points at a percentage, a frequency with a
    // skewed range, or a pan position.
    //
    // `publish` marks the one voice whose reading the knobs draw, so the panel
    // shows what is happening to the voice a player is listening to rather than
    // to whichever voice happened to be rendered last. The offsets are handed
    // over as the voice renders with them, so the ring on a knob and the sound
    // cannot come from two different readings.
    void applyModulation(Patch& target, const Modulation& modulation, const Voice& voice,
                         const std::array<float, lfoCount>& lfos, bool publish = false)
    {
        // Offsets are accumulated per destination first and applied once.
        // Applying each slot in turn would round-trip through the destination's
        // range between slots, so two half-depth slots would not add up to one
        // at full depth, and an early slot hitting a limit would swallow a
        // later one pulling the other way.
        std::array<float, destinationCount> offsets {};
        auto touched = false;

        for (const auto& slot : modulation.slots)
        {
            const auto source = juce::roundToInt(slot.source);
            const auto destination = juce::roundToInt(slot.destination);
            if (source <= 0 || destination <= 0 || destination >= destinationCount || slot.depth == 0.0f)
                continue;

            auto amount = 0.0f;
            switch (static_cast<ModSource>(source))
            {
                case ModSource::velocity: amount = voice.velocity; break;
                case ModSource::note:     amount = static_cast<float>(voice.note) / 127.0f; break;
                case ModSource::modWheel: amount = modWheel; break;
                case ModSource::off:      continue;
                default:
                {
                    if (const auto env = envIndexOf(source); env >= 0)
                    {
                        amount = voice.envelope[static_cast<size_t>(env)];
                        break;
                    }
                    if (const auto lfo = lfoIndexOf(source); lfo >= 0)
                    {
                        amount = lfos[static_cast<size_t>(lfo)];
                        break;
                    }
                    const auto macro = macroIndexOf(source);
                    if (macro < 0) continue;
                    amount = target.macros[static_cast<size_t>(macro)];
                    break;
                }
            }

            // BI centres the source, so the destination's own setting becomes
            // the middle of what the slot can reach rather than one end of it:
            // a macro at rest pulls it as far negative as the depth goes, a
            // macro at half leaves it alone, and a macro at the top pushes it
            // as far positive. Serum calls this POL, and it is the difference
            // between a source at zero meaning "no modulation" and meaning "as
            // far negative as this reaches" — a whole octave on a pitch
            // destination at full depth, and easy to mistake for the oscillator
            // being mistuned.
            //
            // Half, not double: this recentres the reach without resizing it,
            // so one depth still spans the destination exactly once and the top
            // of the control is still the top of the control. Scaling to plus
            // and minus the depth instead would put everything past halfway
            // into the clamp, where turning the knob further did nothing.
            if (slot.bipolar >= 0.5f && !sourceIsBipolar(source)) amount -= 0.5f;

            offsets[static_cast<size_t>(destination)] += slot.depth * amount;
            touched = true;
        }
        if (publish) meterOffsets = offsets;
        if (!touched) return;

        for (int destination = 1; destination < destinationCount; ++destination)
        {
            const auto offset = offsets[static_cast<size_t>(destination)];
            if (offset == 0.0f) continue;
            auto* field = destinationField(target, destination);
            if (field == nullptr) continue;
            const auto& range = destinationRanges[static_cast<size_t>(destination)];
            *field = range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, range.convertTo0to1(*field) + offset));
        }
    }

    static float wrap(float phase) { return phase - std::floor(phase); }
    static float noteFrequency(int note) { return static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note)); }

    // Which voice a new note takes. A silent one if there is one, and the
    // rotation keeps moving through them so that successive notes do not all
    // land on the same slot and start from the same phases.
    //
    // Past that, the cheapest voice to interrupt. A voice already in its
    // release is the one to take: the key is up and the player has moved on, so
    // the quietest of those is the one least likely still to be heard. Only
    // when every voice is still held does a note that is still being played have
    // to give way, and then the quietest of those. Strict rotation took
    // whichever voice was next whether or not it was busy, so a run of notes
    // longer than the polyphony cut off whatever it landed on.
    size_t allocate(size_t voiceCount)
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto index = (nextVoice + i) % voiceCount;
            if (!voices[index].active) return took(index);
        }

        const auto quietest = [this, voiceCount] (bool releasing)
        {
            auto best = voiceCount;
            for (size_t i = 0; i < voiceCount; ++i)
            {
                if ((voices[i].envStage[ampEnv] == EnvelopeStage::release) != releasing) continue;
                if (best == voiceCount
                    || voices[i].envelope[ampEnv] < voices[best].envelope[ampEnv]) best = i;
            }
            return best;
        };
        const auto releasing = quietest(true);
        const auto chosen = releasing < voiceCount ? releasing : quietest(false);
        // Every voice was active to have reached this far, so one of the two
        // searches found one; the fallback is there to keep the index in range
        // rather than because it can be taken.
        return took(chosen < voiceCount ? chosen : 0);
    }

    size_t took(size_t index)
    {
        nextVoice = index + 1;
        return index;
    }

    // A new note starts every keyboard LFO again from the top — this voice's
    // copies of them, so the notes already sounding are untouched. OFF
    // deliberately does not restart, which is the whole point of it: a
    // free-running LFO keeps its place across a phrase.
    //
    // A legato note in mono is not a new note here, for exactly the reason it
    // does not restart the amp envelope — no key was lifted — so an LFO
    // retriggers precisely when that envelope does.
    void retriggerLfo(Voice& voice, const Patch& patch)
    {
        for (int i = 0; i < lfoCount; ++i)
        {
            if (lfoModeOf(patch.lfos[static_cast<size_t>(i)]) == LfoMode::free) continue;
            voice.lfoPhase[static_cast<size_t>(i)] = 0.0f;
            // A fresh step with it: phase zero is where sample and hold takes one.
            voice.lfoHeld[static_cast<size_t>(i)] = noise();
            voice.lfoStopped[static_cast<size_t>(i)] = false;
        }
    }

    // One LFO's cycle moved on by a sample, inside one voice.
    void advanceVoiceLfo(Voice& voice, int index, const LfoSetting& setting, LfoMode mode)
    {
        const auto i = static_cast<size_t>(index);
        // Parked at the end of a one-shot until a note restarts it — or until
        // the mode is taken off ENV, which lets the shape run on from where it
        // stopped rather than leaving the panel showing a dead indicator.
        if (voice.lfoStopped[i] && mode != LfoMode::envelope) voice.lfoStopped[i] = false;
        if (voice.lfoStopped[i]) return;

        const auto advanced = voice.lfoPhase[i]
            + juce::jlimit(0.01f, 40.0f, setting.rate) / static_cast<float>(sampleRate);
        if (advanced >= 1.0f && mode == LfoMode::envelope)
        {
            // A one-shot stops on the last point of the shape and holds it,
            // rather than wrapping round to the first. That hold is what makes
            // it an envelope instead of a cycle that ran once: a saw finishes at
            // the top, a triangle at the bottom, and whatever it is driving
            // stays there until the next note.
            voice.lfoPhase[i] = 1.0f;
            voice.lfoStopped[i] = true;
            return;
        }
        // One new step per cycle, taken as the cycle turns over, so a
        // sample-and-hold changes exactly where the other shapes restart.
        if (advanced >= 1.0f) voice.lfoHeld[i] = noise();
        voice.lfoPhase[i] = wrap(advanced);
    }

    // Taking a voice that is still sounding must not be audible as anything but
    // the new note arriving. Wiping it — phases, filter state and envelope all
    // back to zero — steps the output straight down to silence in one sample,
    // and that step is the click a player hears when a run of notes is longer
    // than the polyphony.
    //
    // So a voice that is still audible is retuned rather than rebuilt. It keeps
    // its oscillator phases, its filter state and the level its envelope has
    // reached, and the attack simply starts again from that level. Amplitude,
    // waveform and filter are all continuous across the steal; the pitch jumps,
    // and a pitch jump is a new note rather than a click.
    void startVoice(Voice& voice, int note, float velocity)
    {
        const auto sounding = voice.active;
        if (!sounding)
        {
            voice = {};
            // A silent voice starts its unison stack at offsets of its own, so
            // two notes struck together do not begin life as one louder note.
            // The offsets are hashed rather than stepped along by a constant:
            // a constant step is a comb, and a harmonic high enough to see the
            // teeth line up on it, which leaves the stack correlated exactly
            // where it should sound widest. A hash has no such structure, and
            // being a hash rather than a random number it is still the same on
            // every run, which is what lets the tests measure it.
            for (juce::uint32 i = 0; i < voice.phaseA.size(); ++i)
            {
                const auto seed = i * 2654435761u + static_cast<juce::uint32>(note) * 40503u;
                voice.phaseA[i] = unitFromHash(seed);
                voice.phaseB[i] = unitFromHash(seed + 2654435741u);
            }
        }
        voice.active = true;
        voice.tail = 1.0f;
        voice.note = note;
        voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
        voice.currentHz = voice.targetHz = noteFrequency(note);
        // Every envelope starts again, from wherever it had reached: a stolen
        // voice keeps the levels it was at and climbs from them, so all four
        // are continuous across the steal exactly as ENV 1 is.
        for (auto& stage : voice.envStage) stage = EnvelopeStage::attack;
    }

    void hold(int note)
    {
        releaseHeld(note);
        if (heldCount < static_cast<int>(heldNotes.size())) heldNotes[static_cast<size_t>(heldCount++)] = note;
    }

    void releaseHeld(int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (heldNotes[static_cast<size_t>(i)] == note)
            {
                for (int j = i; j + 1 < heldCount; ++j) heldNotes[static_cast<size_t>(j)] = heldNotes[static_cast<size_t>(j + 1)];
                --heldCount;
                return;
            }
    }

    void updateEnvelope(float& value, EnvelopeStage& stage, float releaseStart,
                        float attack, float decay, float sustain, float release) const
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        switch (stage)
        {
            case EnvelopeStage::attack:
                value += dt / std::max(0.001f, attack);
                if (value >= 1.0f) { value = 1.0f; stage = EnvelopeStage::decay; }
                break;
            case EnvelopeStage::decay:
                value -= (1.0f - juce::jlimit(0.0f, 1.0f, sustain)) * dt / std::max(0.001f, decay);
                if (value <= sustain) { value = sustain; stage = EnvelopeStage::sustain; }
                break;
            case EnvelopeStage::sustain: value = sustain; break;
            case EnvelopeStage::release:
                value -= releaseStart * dt / std::max(0.001f, release);
                if (value <= 0.0001f) { value = 0.0f; stage = EnvelopeStage::idle; }
                break;
            case EnvelopeStage::idle: value = 0.0f; break;
        }
    }

    float noise()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<float>(noiseState & 0xffffu) / 32767.5f - 1.0f;
    }

    // One oscillator's whole contribution: its own tuning, its own unison
    // stack, its own pan and its own level, summed into the voice.
    void renderOscillator(std::array<float, unisonMax>& phases,
                          std::array<std::array<WarpState, warpSlots>, unisonMax>& warpStates,
                          std::array<WarpDcBlocker, 2>& dc, const Oscillator& osc, float baseHz,
                          float dt, const std::array<WarpStage, warpSlots>& warp,
                          float& left, float& right) const
    {
        if (!on(osc.enable)) return;
        const auto count = juce::jlimit(1, static_cast<int>(phases.size()), juce::roundToInt(osc.unison));
        const auto position = juce::jlimit(0.0f, 1.0f, osc.position);
        const auto hz = baseHz * tuningRatio(osc);
        const auto detune = juce::jlimit(0.0f, 1.0f, osc.detune);
        const auto blend = juce::jlimit(0.0f, 1.0f, osc.blend);

        // Which band-limited copy of the table this note may read. Chosen from
        // the top of the unison stack rather than its centre, so the sharpest
        // voice in the stack decides and no member of it aliases: detune lifts
        // the top of the stack by at most half of unisonSpreadSemitones, which
        // is a ratio of 1.0413, and 5% of headroom covers that with room to
        // spare. Widening the spread without widening this would let the
        // sharpest voice read a copy that is not band-limited far enough.
        //
        // A warp reads the table somewhere other than where the phase says, or
        // shapes what it finds there, and either makes harmonics the table did
        // not hold. So the copy is chosen for a note that much higher than the
        // one being played: every mode declares how much extra bandwidth it is
        // about to ask for, and the two stages multiply. See ForgeWarp.h.
        const auto& table = osc.table != nullptr ? *osc.table : builtInWavetable();
        const auto warped = warp[0].mode != WarpMode::off || warp[1].mode != WarpMode::off;
        const auto headroom = warped
            ? juce::jlimit(1.0f, warpHeadroomCeiling, warpHeadroom(warp[0]) * warpHeadroom(warp[1]))
            : 1.0f;
        const auto level = table.levelFor(hz * 1.05f * headroom, sampleRate);
        // What FM is doing to the rate the cycle runs at, this sample. One
        // unless a stage is actually modulating the frequency, so an
        // oscillator that is not being frequency-modulated advances exactly as
        // it always did. It is worked out once for the whole stack: every
        // member of it is reading one modulator, and a stack that bent by
        // different amounts would no longer be one oscillator.
        const auto pitch = warped
            ? warpPitchFactor(warp[0].mode, warp[0].amount, warp[0].modulator)
            * warpPitchFactor(warp[1].mode, warp[1].amount, warp[1].modulator)
            : 1.0f;

        auto stackLeft = 0.0f, stackRight = 0.0f, power = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto spread = count == 1 ? 0.0f
                : static_cast<float>(i) / static_cast<float>(count - 1) - 0.5f;
            // Blend and pan read the even position, so the shape of the stack
            // across the gain curve and across the image stays smooth. Only the
            // tuning reads the uneven one, because that is what beats.
            const auto offset = unisonOffset(i, count);
            // Blend balances the centre of the stack against its edges: at 0
            // only the centre voices are heard, at 1 the whole stack is level.
            const auto centreWeight = 1.0f - juce::jmin(1.0f, std::abs(spread) * 2.0f);
            const auto gain = juce::jmap(blend, centreWeight, 1.0f);
            power += gain * gain;

            // The table read, and the two warp stages standing between it and
            // the voice. They chain by one calling the other rather than
            // through a buffer, so a stage that moves the phase moves what the
            // stage in front of it is reading rather than what it already read.
            const auto readTable = [&table, level, position] (float p)
            { return table.sample(level, position, p); };
            auto& states = warpStates[static_cast<size_t>(i)];
            const auto phase = phases[static_cast<size_t>(i)];
            const auto sample = (warped
                ? warpRead(warp[1], phase, states[1], [&] (float p)
                           { return warpRead(warp[0], p, states[0], readTable); })
                : readTable(phase)) * gain;
            const auto pan = juce::jlimit(-1.0f, 1.0f, osc.pan + spread * detune * 1.6f);
            stackLeft += sample * sourcePanLeft(pan);
            stackRight += sample * sourcePanRight(pan);

            const auto ratio = std::pow(2.0f, offset * detune
                                              * unisonSpreadSemitones / 12.0f);
            phases[static_cast<size_t>(i)] =
                wrap(phases[static_cast<size_t>(i)] + hz * ratio * pitch * dt);
        }

        // Power normalisation, so widening the stack changes the sound without
        // changing how loud the oscillator is.
        const auto scale = juce::jlimit(0.0f, 1.0f, osc.level) / std::sqrt(std::max(0.0001f, power));
        auto outLeft = stackLeft * scale, outRight = stackRight * scale;
        // An asymmetric warp puts a constant offset into the signal, which is a
        // thump on every note and a bias the filter would then have to carry.
        // Only while something is warping: an oscillator reading its table
        // straight has no offset to take off.
        if (warped)
        {
            outLeft = dc[0].process(outLeft, dcBlock);
            outRight = dc[1].process(outRight, dcBlock);
        }
        left += outLeft;
        right += outRight;
    }

    void renderOscillators(Voice& voice, const Patch& patch, Buses& buses)
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        const auto glide = juce::jlimit(0.0f, 2.0f, patch.glide);
        if (glide <= 0.0001f) voice.currentHz = voice.targetHz;
        else voice.currentHz += (voice.targetHz - voice.currentHz)
            * (1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * glide)));
        const auto hz = voice.currentHz * pitchRatio;

        // Every source is rendered on its own before it is handed anywhere,
        // because a channel's sends are taken from that channel rather than
        // from the sum it lands in: an oscillator can reach the filter and both
        // busses at once, and it cannot do that while it is being written
        // straight into somebody else's accumulator.
        // Each oscillator's two warp stages, resolved for this sample before
        // either stack is touched: which mode, how deep, the coefficients a
        // filter mode needs, and whatever an FM mode is reading. Worked out
        // once here rather than once per member of a stack, which is what keeps
        // the exponentials out of the inner loop.
        const auto hzA = hz * tuningRatio(patch.a), hzB = hz * tuningRatio(patch.b);
        std::array<WarpStage, warpSlots> warpA {}, warpB {};
        for (int i = 0; i < warpSlots; ++i)
        {
            const auto slot = static_cast<size_t>(i);
            warpA[slot] = warpStageFor(patch.a.warpMode[slot], patch.a.warpAmount[slot], hzA, sampleRate);
            warpB[slot] = warpStageFor(patch.b.warpMode[slot], patch.b.warpAmount[slot], hzB, sampleRate);
        }

        // What FM reads, worked out only where something is actually asking for
        // it. The two oscillators read each other at the phases they both stand
        // at now, before either has advanced, so neither is a sample ahead of
        // the other and swapping which one is rendered first changes nothing.
        const auto asks = [] (const std::array<WarpStage, warpSlots>& warp, bool (*test)(WarpMode))
        {
            for (const auto& stage : warp) if (test(stage.mode)) return true;
            return false;
        };
        const auto wantsOther = asks(warpA, warpReadsOtherOscillator) || asks(warpB, warpReadsOtherOscillator);
        const auto wantsSub = asks(warpA, warpReadsSub) || asks(warpB, warpReadsSub);
        const auto wantsNoise = asks(warpA, warpReadsNoise) || asks(warpB, warpReadsNoise);
        // The sub, read once for whoever needs it: the source itself below, and
        // any warp stage pointed at it. Both read the same shape at the same
        // band limit, because a stage reading FM SUB is reading the sub rather
        // than a sine that happens to stand where it does.
        const auto hzSub = hz * subRatio(patch.subOctave);
        const auto& subTable = subWavetable();
        const auto subBand = subTable.levelFor(hzSub, sampleRate);
        const auto subShapeIndex = subShapeOf(patch.subWave);
        const auto subSample = [&] { return subTable.frameSample(subBand, subShapeIndex, voice.phaseSub); };
        const auto fromSub = wantsSub ? subSample() : 0.0f;
        const auto fromNoise = wantsNoise ? noise() : 0.0f;
        // The centre of the other oscillator's stack, not the whole of it: a
        // modulator is one signal, and twelve detuned copies of one would cost
        // twelve table reads to say the same thing.
        const auto centre = [this] (const Oscillator& osc, const std::array<float, unisonMax>& phases,
                                    float oscHz)
        {
            if (!on(osc.enable)) return 0.0f;
            const auto& table = osc.table != nullptr ? *osc.table : builtInWavetable();
            return table.sample(table.levelFor(oscHz, sampleRate),
                                juce::jlimit(0.0f, 1.0f, osc.position), phases[0]);
        };
        const auto fromB = wantsOther ? centre(patch.b, voice.phaseB, hzB) : 0.0f;
        const auto fromA = wantsOther ? centre(patch.a, voice.phaseA, hzA) : 0.0f;
        // A stage pointed at a source that is switched off is not a stage. The
        // manual says as much -- the other oscillator has to be enabled for FM
        // to work, though its level may be all the way down -- and saying it
        // here rather than letting the modulator come out at zero matters,
        // because a live stage also asks the table for bandwidth it is not
        // going to use, and the carrier would quietly go dull for nothing.
        const auto pointAt = [&] (std::array<WarpStage, warpSlots>& warp, float other, bool otherOn)
        {
            for (auto& stage : warp)
                switch (warpSourceOf(stage.mode))
                {
                    case WarpSource::otherOscillator:
                        if (otherOn) stage.modulator = other; else stage = {};
                        break;
                    case WarpSource::sub:
                        if (on(patch.subEnable)) stage.modulator = fromSub; else stage = {};
                        break;
                    case WarpSource::noise: stage.modulator = fromNoise; break;
                    // A stage reading itself needs nothing from out here; it
                    // keeps its own last output beside its filter state.
                    case WarpSource::self:
                    case WarpSource::none:  break;
                }
        };
        pointAt(warpA, fromB, on(patch.b.enable));
        pointAt(warpB, fromA, on(patch.a.enable));

        auto left = 0.0f, right = 0.0f;
        renderOscillator(voice.phaseA, voice.warpA, voice.dcA, patch.a, hz, dt, warpA, left, right);
        distribute(left, right, on(patch.routeA), patch.sendA, buses);

        left = right = 0.0f;
        renderOscillator(voice.phaseB, voice.warpB, voice.dcB, patch.b, hz, dt, warpB, left, right);
        distribute(left, right, on(patch.routeB), patch.sendB, buses);

        // The sub and the noise generator are their own sources: each is silent
        // unless its own module is on, whatever its level knob reads. Each is
        // placed with the source pan law, the same one the oscillators spread
        // their stacks across.
        if (on(patch.subEnable))
        {
            const auto sub = subSample() * patch.subLevel;
            distribute(sub * sourcePanLeft(patch.subPan), sub * sourcePanRight(patch.subPan),
                       on(patch.routeSub), patch.sendSub, buses);
        }
        if (on(patch.noiseEnable))
        {
            const auto hiss = noise() * patch.noiseLevel;
            distribute(hiss * sourcePanLeft(patch.noisePan), hiss * sourcePanRight(patch.noisePan),
                       on(patch.routeNoise), patch.sendNoise, buses);
        }

        // The amp envelope reaches the sends as well as the two destinations.
        // A send is taken after the channel's own fader, and ENV 1 is part of
        // what that fader amounts to, so a note that has finished must be
        // sending nothing.
        const auto level = voice.envelope[ampEnv] * voice.velocity;
        buses.wetLeft *= level;
        buses.wetRight *= level;
        buses.dryLeft *= level;
        buses.dryRight *= level;
        for (int bus = 0; bus < busCount; ++bus)
        {
            buses.sendLeft[static_cast<size_t>(bus)] *= level;
            buses.sendRight[static_cast<size_t>(bus)] *= level;
        }
        voice.phaseSub = wrap(voice.phaseSub + hzSub * dt);
    }

    std::array<Voice, 16> voices {};
    // One set of filter delay lines per voice, per channel. Sized in
    // initialise() and never afterwards; held here rather than in the voice so
    // a fresh note cannot reallocate one from the audio thread.
    //
    // Inline rather than behind a vector of its own, so every entry exists
    // whether or not initialise() has run: an unprepared one holds empty lines
    // and reports itself not ready, which the four types that read a line
    // check. A vector would instead have to be size-checked per sample.
    std::array<std::array<FilterDelays, 2>, 16> filterDelays {};
    double sampleRate = 48000.0;
    float pitchRatio = 1.0f;
    float modWheel = 0.0f;
    size_t nextVoice = 0;
    // One over the length of the tail fade in samples, worked out when the
    // sample rate is known rather than per sample.
    float tailStep = 1.0f / (44100.0f * voiceTailSeconds);
    // The pole of the offset blocker a warped oscillator runs, worked out when
    // the sample rate is known rather than per sample.
    float dcBlock = warpDcCoefficient(48000.0);
    // The free-running cycles, one per LFO. An LFO in OFF reads these: the same
    // cycle for every voice and for the panel, running whether or not anything
    // is playing, which is the whole of what OFF means. The keyboard modes
    // ignore them and read the copy inside the voice instead.
    std::array<float, lfoCount> freePhase {};
    std::array<float, lfoCount> freeHeld {};
    // What the panel is shown for each LFO. The held step is carried as well as
    // the phase because a sample and hold's step cannot be worked back out of
    // its phase.
    std::array<float, lfoCount> meterLfoPhase {};
    std::array<float, lfoCount> meterLfoHeld {};
    std::array<float, lfoCount> meterLfoValue {};
    std::uint32_t noiseState = 0x9e3779b9u;
    std::array<juce::NormalisableRange<float>, destinationCount> destinationRanges {};
    // Reused every voice and every sample so a modulated render allocates
    // nothing; only touched when at least one slot is live.
    Patch scratch {};
    // The three racks, rendered. They hold delay lines and filter state, so
    // they belong to the Core rather than to the patch that describes them.
    std::array<FxRack, rackCount> racks;
    double tempo = 0.0;
    // Where the loudest voice's modulated rack settings are kept, so the racks
    // can be run from them once the loop is over. Only touched when something
    // is actually pointed at a rack.
    Patch fxScratch {};

    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    bool monoMode = false;
    std::array<float, envCount> meterEnvelope {};
    std::array<EnvelopeStage, envCount> meterStage {};
    std::array<float, destinationCount> meterOffsets {};
};
}
