// The three effects racks: what each type does to the signal, what a slot draws
// of itself, and that the two are computed from the same functions.
#include "ForgeTestSupport.h"
#include "../core/ForgeFxDsp.h"
#include "../ui/ForgeFxGestures.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeFxVisuals.h"

namespace rhino::forge::tests
{
namespace
{
// The rack is measured rather than read back, for the same reason the mixer is:
// a slot wired to the wrong accumulator, a delay line read at the wrong offset
// or a type that silently does nothing all look perfectly correct in the
// parameters.
//
// These lean on what each type *provably* does rather than on how it sounds — a
// delay puts energy where there was none, a filter takes brightness away, a
// distortion adds harmonics — because those are the claims that would be wrong
// if the wiring were wrong.
void fxSuite()
{
    using rhino::forge::FxType;
    constexpr int samples = 16384;
    juce::AudioBuffer<float> buffer(2, samples);

    // One slot of one rack, set up in a line.
    const auto place = [] (rhino::forge::Processor& processor, int rack, int slot, FxType type)
    {
        setValue(processor, rhino::forge::fxParameterId(rack, slot, "Type").toRawUTF8(),
                 static_cast<float>(type));
    };
    const auto knob = [] (rhino::forge::Processor& processor, int rack, int slot, int index, float value)
    {
        setValue(processor, (rhino::forge::fxParameterId(rack, slot, "Knob")
                             + juce::String(index + 1)).toRawUTF8(), value);
    };

    // Everything below plays one short note and then listens to what is left
    // after it, which is where a delay and a reverb live and where a dry synth
    // is silent.
    const auto tailAfterNote = [&buffer] (rhino::forge::Processor& processor)
    {
        processor.prepareToPlay(48000.0, buffer.getNumSamples());
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 57), 2000);
        processor.processBlock(buffer, midi);
        // Well past the note and its release, so anything here arrived by way
        // of a delay line rather than from the voice.
        return rms(buffer, 0, 9000);
    };

    // --- A rack does something, and only where it is put ----------------------

    auto dryOwner = std::make_unique<rhino::forge::Processor>();
    auto& dry = *dryOwner;
    soloSineOnA(dry);
    setValue(dry, "env1Release", 0.02f);
    const auto silence = tailAfterNote(dry);

    auto delayedOwner = std::make_unique<rhino::forge::Processor>();
    auto& delayed = *delayedOwner;
    soloSineOnA(delayed);
    setValue(delayed, "env1Release", 0.02f);
    place(delayed, 0, 0, FxType::delay);
    knob(delayed, 0, 0, 0, 0.35f);   // a time long enough to outlast the note
    knob(delayed, 0, 0, 2, 0.8f);    // feedback, so there is a tail to find
    const auto withDelay = tailAfterNote(delayed);
    require(allSamplesFinite(buffer), "a delay renders finite audio");
    require(withDelay > silence * 4.0f + 0.0001f,
            "a delay on the main rack leaves sound behind after the note has gone");

    // Capacity past the original four is signal-path capacity, not only extra
    // parameters in the panel. Prove the last slot is rendered as an insert.
    auto extendedOwner = std::make_unique<rhino::forge::Processor>();
    auto& extended = *extendedOwner;
    soloSineOnA(extended);
    setValue(extended, "env1Release", 0.02f);
    place(extended, 0, rhino::forge::fxSlotCount - 1, FxType::delay);
    knob(extended, 0, rhino::forge::fxSlotCount - 1, 0, 0.35f);
    knob(extended, 0, rhino::forge::fxSlotCount - 1, 2, 0.8f);
    require(tailAfterNote(extended) > silence * 4.0f + 0.0001f,
            "the eighth FX slot is part of the rendered chain");

    // The same delay on a bus nothing is sent to must change nothing at all.
    auto unsentOwner = std::make_unique<rhino::forge::Processor>();
    auto& unsent = *unsentOwner;
    soloSineOnA(unsent);
    setValue(unsent, "env1Release", 0.02f);
    place(unsent, 1, 0, FxType::delay);
    knob(unsent, 1, 0, 0, 0.35f);
    knob(unsent, 1, 0, 2, 0.8f);
    requireClose(tailAfterNote(unsent), silence, silence + 0.0001f,
                 "a rack on a bus with nothing sent to it is heard nowhere");

    // Sent to that bus, it arrives. This is the whole point of the busses.
    auto sentToBusOwner = std::make_unique<rhino::forge::Processor>();
    auto& sentToBus = *sentToBusOwner;
    soloSineOnA(sentToBus);
    setValue(sentToBus, "env1Release", 0.02f);
    setValue(sentToBus, "oscASend1", 1.0f);
    setValue(sentToBus, "bus1Level", 1.0f);
    place(sentToBus, 1, 0, FxType::delay);
    knob(sentToBus, 1, 0, 0, 0.35f);
    knob(sentToBus, 1, 0, 2, 0.8f);
    require(tailAfterNote(sentToBus) > silence * 4.0f + 0.0001f,
            "a source sent to a bus is heard through that bus's rack");

    // A reverb is the type most easily broken into silence — a comb read past
    // the end of its own line answers nothing and the bank stays quiet — so it
    // is held to the same claim the delay is: sound after the note is gone.
    auto reverbedOwner = std::make_unique<rhino::forge::Processor>();
    auto& reverbed = *reverbedOwner;
    soloSineOnA(reverbed);
    setValue(reverbed, "env1Release", 0.02f);
    place(reverbed, 0, 0, FxType::reverb);
    knob(reverbed, 0, 0, 0, 0.8f);   // size
    knob(reverbed, 0, 0, 1, 0.0f);   // no pre-delay
    knob(reverbed, 0, 0, 2, 0.1f);   // very little damping, so the tail carries
    knob(reverbed, 0, 0, 5, 1.0f);   // high cut open
    require(tailAfterNote(reverbed) > silence * 4.0f + 0.0001f,
            "a reverb leaves a tail behind the note");
    require(allSamplesFinite(buffer), "a reverb renders finite audio");

    // --- Bypass, at both levels ------------------------------------------------

    auto bypassedOwner = std::make_unique<rhino::forge::Processor>();
    auto& bypassed = *bypassedOwner;
    soloSineOnA(bypassed);
    setValue(bypassed, "env1Release", 0.02f);
    place(bypassed, 0, 0, FxType::delay);
    knob(bypassed, 0, 0, 0, 0.35f);
    knob(bypassed, 0, 0, 2, 0.8f);
    setValue(bypassed, rhino::forge::fxParameterId(0, 0, "Bypass").toRawUTF8(), 1.0f);
    requireClose(tailAfterNote(bypassed), silence, silence + 0.0001f,
                 "a bypassed slot is out of the signal");
    setValue(bypassed, rhino::forge::fxParameterId(0, 0, "Bypass").toRawUTF8(), 0.0f);
    setValue(bypassed, rhino::forge::fxRackParameterId(0, "Bypass").toRawUTF8(), 1.0f);
    requireClose(tailAfterNote(bypassed), silence, silence + 0.0001f,
                 "bypassing the whole rack takes every slot in it out at once");

    // A slot left at OFF is not a slot that does nothing quietly — it must be
    // exactly the same signal as no slot at all.
    auto emptySlotOwner = std::make_unique<rhino::forge::Processor>();
    auto& emptySlot = *emptySlotOwner;
    soloSineOnA(emptySlot);
    renderNote(emptySlot, buffer);
    const auto plain = rms(buffer, 0, 1024);
    place(emptySlot, 0, 2, FxType::off);
    renderNote(emptySlot, buffer);
    requireClose(rms(buffer, 0, 1024), plain, plain * 0.0001f, "a slot set to OFF changes nothing");

    // --- MIX and LEVEL mean one thing across every type ------------------------

    auto blendedOwner = std::make_unique<rhino::forge::Processor>();
    auto& blended = *blendedOwner;
    soloSineOnA(blended);
    place(blended, 0, 0, FxType::filter);
    knob(blended, 0, 0, 0, 0.05f);   // a low cutoff, so the effect is obvious
    knob(blended, 0, 0, 1, 0.0f);
    knob(blended, 0, 0, 3, 0.0f);
    renderNote(blended, buffer);
    const auto filtered = rms(buffer, 0, 1024);
    require(filtered < plain * 0.7f, "a filter in the rack takes the level down");
    setValue(blended, rhino::forge::fxParameterId(0, 0, "Mix").toRawUTF8(), 0.0f);
    renderNote(blended, buffer);
    requireClose(rms(buffer, 0, 1024), plain, plain * 0.02f,
                 "MIX at nothing passes what went into the slot, whatever the slot is doing");
    setValue(blended, rhino::forge::fxParameterId(0, 0, "Mix").toRawUTF8(), 1.0f);
    setValue(blended, rhino::forge::fxParameterId(0, 0, "Level").toRawUTF8(), 0.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, 1024) < plain * 0.001f, "a slot's LEVEL at nothing silences it");

    // --- The slots run in order ------------------------------------------------
    //
    // A filter opened wide after a filter closed down is still dark; the other
    // way round it is still dark too, but a rack that ran its slots in the
    // wrong order would let the second one undo the first.
    auto orderedOwner = std::make_unique<rhino::forge::Processor>();
    auto& ordered = *orderedOwner;
    soloSineOnA(ordered);
    setValue(ordered, "oscAPosition", 6.0f / 9.0f);   // a saw, so there is something to take away
    renderNote(ordered, buffer);
    const auto open = brightness(buffer, 0, 1024);
    place(ordered, 0, 0, FxType::filter);
    knob(ordered, 0, 0, 0, 0.1f);
    knob(ordered, 0, 0, 1, 0.0f);
    knob(ordered, 0, 0, 3, 0.0f);
    renderNote(ordered, buffer);
    const auto afterFirst = brightness(buffer, 0, 1024);
    require(afterFirst < open * 0.8f, "a low pass in the rack takes the top off");
    place(ordered, 0, 1, FxType::filter);
    knob(ordered, 0, 1, 0, 1.0f);
    knob(ordered, 0, 1, 1, 0.0f);
    knob(ordered, 0, 1, 3, 0.0f);
    renderNote(ordered, buffer);
    require(brightness(buffer, 0, 1024) < open * 0.8f,
            "a filter wide open after a closed one cannot put back what the first took out");

    // --- Every type renders, and none of them is silent or infinite ------------
    //
    // The cheapest check there is, and the one that would have caught every
    // mistake made writing these: a type whose state is read before it is
    // prepared, or whose feedback path runs away.
    for (int type = 1; type < rhino::forge::fxTypeCount; ++type)
    {
        auto eachOwner = std::make_unique<rhino::forge::Processor>();
        auto& each = *eachOwner;
        soloSineOnA(each);
        place(each, 0, 0, static_cast<FxType>(type));
        // Driven hard on purpose: a feedback path that is going to run away
        // does it here rather than in somebody's project.
        for (int index = 0; index < rhino::forge::fxKnobCount; ++index)
            knob(each, 0, 0, index, 0.95f);
        renderNote(each, buffer);
        if (!allSamplesFinite(buffer))
        {
            require(false, "every effect type renders finite audio at its extremes");
            std::cerr << "       type: " << rhino::forge::fxTypeName(type) << '\n';
        }
        if (buffer.getMagnitude(0, buffer.getNumSamples()) > 1.0f)
        {
            require(false, "no effect type leaves full scale");
            std::cerr << "       type: " << rhino::forge::fxTypeName(type) << '\n';
        }
        // And again with everything at nothing, which is the other end a
        // divide-by-zero hides at.
        for (int index = 0; index < rhino::forge::fxKnobCount; ++index)
            knob(each, 0, 0, index, 0.0f);
        renderNote(each, buffer);
        if (!allSamplesFinite(buffer))
        {
            require(false, "every effect type renders finite audio at the bottom of its range");
            std::cerr << "       type: " << rhino::forge::fxTypeName(type) << '\n';
        }
    }

    // --- A knob's reading is the one the DSP uses ------------------------------
    //
    // The whole point of the type table: the panel, the readout and the render
    // all take their arithmetic from the same helpers. If a delay's TIME said
    // 250 ms while the line was read at some other offset, this is what would
    // notice.
    auto readingOwner = std::make_unique<rhino::forge::Processor>();
    auto& reading = *readingOwner;
    place(reading, 0, 0, FxType::delay);
    const auto quarter = rhino::forge::fxScaled(0.5f, 0.01f, rhino::forge::fxMaxDelayTime, 2.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f),
                quarter < 1.0f ? juce::String(juce::roundToInt(quarter * 1000.0f)) + " ms"
                               : juce::String(quarter, 2) + " s",
                "a delay's TIME reads the time the engine will use");
    // Switched to beats, the same knob reads a division instead.
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeB").toRawUTF8(), 1.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 1.0f),
                "2/1", "a synced delay's TIME reads a division of the beat");
    // A knob the type does not use says so rather than showing a number that
    // means nothing.
    place(reading, 0, 0, FxType::filter);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob6").toRawUTF8(), 0.5f),
                "-", "a knob the type does not use reads as nothing");
    place(reading, 0, 0, FxType::compressor);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob2").toRawUTF8(), 0.0f),
                "1.0 : 1", "a compressor's ratio reads the ratio its curve and detector use");
    place(reading, 0, 0, FxType::phaser);
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeA").toRawUTF8(), 1.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 1.0f),
                "2/1", "a synced phaser's RATE reads the division its LFO uses");
    // A distortion's DRIVE is a gain into six of its shapes, a number of
    // steps for CRUSH and a hold for DOWNSMP, and reads as whichever it is.
    place(reading, 0, 0, FxType::distortion);
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeA").toRawUTF8(), 0.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.3f),
                "+" + juce::String(20.0f * std::log10(1.0f + 24.0f * 0.3f), 1) + " dB",
                "a distortion's DRIVE reads the gain its shape is driven by");
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeA").toRawUTF8(), 6.0f / 7.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f),
                "8 STEPS", "a crushing DRIVE reads the steps it quantises to");
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeA").toRawUTF8(), 1.0f);
    require(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f).endsWith("Hz"),
            "a downsampling DRIVE reads the rate it holds at");
    // A reverb's SIZE reads the tail it makes, which a HALL lengthens.
    place(reading, 0, 0, FxType::reverb);
    {
        rhino::forge::FxSlot room;
        room.type = static_cast<float>(FxType::reverb);
        room.knobs[0] = 0.5f;
        setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeA").toRawUTF8(), 0.0f);
        const auto plate = textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f);
        requireText(plate, juce::String(rhino::forge::fxReverbRt60(room, 0.0f, 48000.0), 2) + " s",
                    "a reverb's SIZE reads the time its tail takes to fall sixty decibels");
        setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeA").toRawUTF8(), 1.0f);
        const auto hall = textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f);
        require(hall.getFloatValue() > plate.getFloatValue(), "the same SIZE rings longer in a HALL");
    }
    // A flanger's DEPTH and DELAY are milliseconds and its FEEDBACK is signed.
    place(reading, 0, 0, FxType::flanger);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob2").toRawUTF8(), 0.5f),
                "2.50 ms", "a flanger's DEPTH reads how far the tap travels");
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob3").toRawUTF8(), 0.0f),
                "-95 %", "a flanger's FEEDBACK reads its sign");
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob4").toRawUTF8(), 1.0f),
                "10.00 ms", "a flanger's DELAY reads where the sweep starts");
    // A delay's feedback reads no higher than the loop lets it go.
    place(reading, 0, 0, FxType::delay);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob3").toRawUTF8(), 1.0f),
                "95 %", "a delay's FEEDBACK at the top reads the most the loop feeds back");
    // AUTO says what it is applying.
    place(reading, 0, 0, FxType::compressor);
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeB").toRawUTF8(), 1.0f);
    setValue(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f);
    setValue(reading, rhino::forge::fxParameterId(0, 0, "Knob2").toRawUTF8(), 0.5f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob5").toRawUTF8(), 0.0f),
                "AUTO +" + juce::String(rhino::forge::fxCompressorAutoMakeup(rhino::forge::fxCompressorThreshold(0.5f),
                                                                            rhino::forge::fxCompressorRatio(0.5f)), 1) + " dB",
                "automatic makeup reads the gain it applies");

    // --- The distortion's filter, drawn from its own arithmetic -----------------
    //
    // The TONE well draws distortionFilterMagnitude. That is only honest if it
    // is what the slot does, so a sine is run through the slot at frequencies
    // either side of the corner -- HARD at no drive passes what it is given --
    // and the level that comes out is held to the curve within half a decibel.
    for (const auto highPass : {false, true})
    {
        rhino::forge::FxRack rack;
        const auto sampleRate = 48000.0;
        rack.prepare(sampleRate);
        rhino::forge::Rack settings;
        auto& slot = settings.slots[0];
        slot.type = static_cast<float>(FxType::distortion);
        slot.modeA = 2.0f / 7.0f;                    // HARD: the identity at no drive
        slot.modeB = highPass ? 0.25f : 0.5f;        // PRE HP or PRE LP
        slot.knobs = {0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f};
        const auto cutoff = rhino::forge::fxHertz(slot.knobs[1], 40.0f, 16000.0f);
        const auto q = rhino::forge::fxScaled(slot.knobs[2], 0.4f, 8.0f);
        for (const auto ratio : {0.25f, 0.6f, 1.0f, 1.7f, 4.0f})
        {
            const auto hz = cutoff * ratio;
            rack.reset();
            double phase = 0.0;
            auto in = 0.0, out = 0.0;
            const auto sweepLength = 48000;
            for (int i = 0; i < sweepLength; ++i)
            {
                const auto x = 0.2f * static_cast<float>(std::sin(phase));
                phase += juce::MathConstants<double>::twoPi * hz / sampleRate;
                auto left = x, right = x;
                rack.process(settings, 120.0, left, right);
                if (i >= sweepLength / 2)
                {
                    in += static_cast<double>(x) * x;
                    out += static_cast<double>(left) * left;
                }
            }
            const auto measured = 10.0 * std::log10(out / in);
            const auto drawn = 20.0 * std::log10(rhino::forge::distortionFilterMagnitude(hz, cutoff, q, highPass,
                                                                                         sampleRate));
            if (std::abs(measured - drawn) > 0.5)
                std::cerr << "       " << (highPass ? "HP" : "LP") << " at " << hz << " Hz: heard " << measured
                          << " dB, drawn " << drawn << " dB" << '\n';
            require(std::abs(measured - drawn) <= 0.5,
                    "the distortion's filter curve is the gain the slot actually applies");
        }
    }

    // --- The rack filter, drawn from its own arithmetic --------------------------
    //
    // The FILTER well draws fxFilterMagnitude, so each of the eight models is
    // held to what the slot does with a sine either side of its cutoff. Where
    // the curve falls out of the window, a notch or a steep skirt, it is enough
    // that the slot is just as deep.
    for (int model = 0; model < 8; ++model)
    {
        rhino::forge::FxRack rack;
        const auto sampleRate = 48000.0;
        rack.prepare(sampleRate);
        rhino::forge::Rack settings;
        auto& slot = settings.slots[0];
        slot.type = static_cast<float>(FxType::filter);
        slot.modeA = static_cast<float>(model) / 7.0f;
        slot.knobs = {0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
        const auto cutoff = rhino::forge::fxHertz(slot.knobs[0], 30.0f, 18000.0f);
        for (const auto ratio : {0.25f, 0.6f, 1.0f, 1.7f, 4.0f})
        {
            const auto hz = cutoff * ratio;
            rack.reset();
            double phase = 0.0;
            auto in = 0.0, out = 0.0;
            const auto sweepLength = 48000;
            for (int i = 0; i < sweepLength; ++i)
            {
                const auto x = 0.2f * static_cast<float>(std::sin(phase));
                phase += juce::MathConstants<double>::twoPi * hz / sampleRate;
                auto left = x, right = x;
                rack.process(settings, 120.0, left, right);
                if (i >= sweepLength / 2)
                {
                    in += static_cast<double>(x) * x;
                    out += static_cast<double>(left) * left;
                }
            }
            const auto measured = 10.0 * std::log10(out / in);
            const auto drawn = 20.0 * std::log10(juce::jmax(1.0e-9f,
                rhino::forge::fxFilterMagnitude(slot, hz, sampleRate)));
            const auto agrees = drawn < -30.0 ? measured < -24.0 : std::abs(measured - drawn) <= 0.5;
            if (!agrees)
                std::cerr << "       model " << model << " at " << hz << " Hz: heard " << measured
                          << " dB, drawn " << drawn << " dB" << '\n';
            require(agrees, "the filter's curve is the gain the slot actually applies");
        }
    }

    // --- The flanger's comb, drawn from its own arithmetic -----------------------
    //
    // The JET well draws fxFlangerMagnitude. With no DEPTH the tap stands
    // still, so the slot is a fixed comb, and a sine at a peak, a notch and
    // between them is held to the curve. DELAY is not a whole number of
    // samples here, so the line's interpolation is in what is compared.
    {
        rhino::forge::FxRack rack;
        const auto sampleRate = 48000.0;
        rack.prepare(sampleRate);
        rhino::forge::Rack settings;
        auto& slot = settings.slots[0];
        slot.type = static_cast<float>(FxType::flanger);
        slot.knobs = {0.2f, 0.0f, 0.72f, 0.3f, 0.5f, 0.5f};
        const auto delayMs = rhino::forge::fxFlangerDelayMs(slot.knobs[3]);
        for (const auto ratio : {0.2f, 0.5f, 0.75f, 1.0f, 2.6f})
        {
            const auto hz = ratio * 1000.0f / delayMs;
            rack.reset();
            double phase = 0.0;
            auto in = 0.0, out = 0.0;
            const auto sweepLength = 48000;
            for (int i = 0; i < sweepLength; ++i)
            {
                const auto x = 0.2f * static_cast<float>(std::sin(phase));
                phase += juce::MathConstants<double>::twoPi * hz / sampleRate;
                auto left = x, right = x;
                rack.process(settings, 120.0, left, right);
                if (i >= sweepLength / 2)
                {
                    in += static_cast<double>(x) * x;
                    out += static_cast<double>(left) * left;
                }
            }
            const auto measured = 10.0 * std::log10(out / in);
            const auto drawn = 20.0 * std::log10(juce::jmax(1.0e-9f,
                rhino::forge::fxFlangerMagnitude(slot, hz, sampleRate, delayMs)));
            const auto agrees = drawn < -30.0 ? measured < -24.0 : std::abs(measured - drawn) <= 0.5;
            if (!agrees)
                std::cerr << "       flanger at " << hz << " Hz: heard " << measured << " dB, drawn " << drawn
                          << " dB" << '\n';
            require(agrees, "the flanger's comb is the gain the slot actually applies");
        }
    }

    // --- What a slot says it is doing -------------------------------------------
    //
    // The lamps, the gain-reduction needle and the sweeps read a meter the rack
    // keeps as it renders. A compressor driven hard says how much it took off
    // and the peaks either side of it; a bypassed slot says it did nothing
    // rather than reading as silence; a swept type says where its LFO is; and
    // taking a meter starts the next one, so a peak is reported once.
    {
        rhino::forge::FxRack rack;
        rack.prepare(48000.0);
        rhino::forge::Rack settings;
        settings.slots[0].type = static_cast<float>(FxType::compressor);
        settings.slots[0].knobs = {0.5f, 0.6f, 0.1f, 0.3f, 0.0f, 0.0f};   // -30 dB, a steep ratio
        settings.slots[1].type = static_cast<float>(FxType::delay);
        settings.slots[1].bypass = 1.0f;
        settings.slots[2].type = static_cast<float>(FxType::phaser);
        double phase = 0.0;
        for (int i = 0; i < 9600; ++i)
        {
            const auto x = 0.8f * static_cast<float>(std::sin(phase));
            phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
            auto left = x, right = x;
            rack.process(settings, 120.0, left, right);
        }
        const auto compressor = rack.takeMeter(0);
        require(compressor.rendered, "a slot that ran says so");
        require(std::abs(compressor.inputPeak - 0.8f) < 0.01f, "a slot's input peak is the loudest sample it was given");
        require(compressor.reductionDb < -10.0f, "a compressor driven twenty-eight decibels past its threshold says it is taking gain off");
        require(compressor.outputPeak > 0.0f && compressor.outputPeak < compressor.inputPeak,
                "a compressor's output peak is what came out of it");
        require(!rack.takeMeter(1).rendered, "a bypassed slot says it did nothing");
        const auto phaser = rack.takeMeter(2);
        require(phaser.rendered && phaser.phase > 0.0f && phaser.phase < 1.0f, "a phaser says where its sweep is");
        const auto again = rack.takeMeter(0);
        require(!again.rendered && again.inputPeak == 0.0f && again.reductionDb == 0.0f,
                "a taken meter starts again, so a peak is reported once");
    }

    // The processor holds a slot's peaks across blocks until the panel takes
    // them, and forgets them once it has.
    {
        auto heldOwner = std::make_unique<rhino::forge::Processor>();
        auto& held = *heldOwner;
        soloSineOnA(held);
        held.prepareToPlay(48000.0, 512);
        place(held, 0, 0, FxType::distortion);
        knob(held, 0, 0, 0, 1.0f);
        juce::AudioBuffer<float> block(2, 512);
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
        for (int i = 0; i < 8; ++i)
        {
            block.clear();
            held.processBlock(block, notes);
            notes.clear();
        }
        const auto first = held.takeFxMeter(0, 0);
        require(first.rendered && first.outputPeak > 0.5f, "a driven distortion's peak reaches the panel");
        const auto second = held.takeFxMeter(0, 0);
        require(second.outputPeak == 0.0f, "a peak the panel has taken is not taken twice");
        held.releaseResources();
    }

    // --- What a type opens on --------------------------------------------------
    //
    // Every type is put into a slot by the panel, which then sets it up from
    // the table beside the type. These hold that table to the two things that
    // would actually be wrong: an equaliser that colours a signal the moment it
    // is dropped in, and a reverb that drowns the main output.
    for (int type = 1; type < rhino::forge::fxTypeCount; ++type)
    {
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(type)];
        require(info.initMix > 0.0f && info.initMix <= 1.0f,
                "a type opens at a wet amount you can hear and cannot exceed");
        for (int index = 0; index < rhino::forge::fxKnobCount; ++index)
            require(info.init[static_cast<size_t>(index)] >= 0.0f
                        && info.init[static_cast<size_t>(index)] <= 1.0f,
                    "a type opens on knob values inside the range the knobs have");
    }
    // An equaliser has to open flat, or dropping one in colours the sound
    // before it is asked to. Gain sits at the centre of a signed range, so
    // "flat" is a number this can check rather than a claim.
    {
        const auto& eq = rhino::forge::fxTypes()[static_cast<size_t>(FxType::equaliser)];
        requireClose(rhino::forge::fxScaled(eq.init[2], -18.0f, 18.0f), 0.0f, 0.01f,
                     "an equaliser's low band opens at no gain at all");
        requireClose(rhino::forge::fxScaled(eq.init[5], -18.0f, 18.0f), 0.0f, 0.01f,
                     "an equaliser's high band opens at no gain at all");
    }
    // A reverb and a delay are the two that usually sit on the main output, so
    // they are the two that must not arrive fully wet.
    for (const auto type : {FxType::reverb, FxType::delay})
        require(rhino::forge::fxTypes()[static_cast<size_t>(type)].initMix < 0.5f,
                "a reverb and a delay open mostly dry, because they are usually placed on MAIN");

    // The two new families have real behaviour rather than only menu entries.
    // Compression bends levels above threshold, while unity ratio leaves them.
    requireClose(rhino::forge::fxCompressorOutputDb(-6.0f, -18.0f, 1.0f, 0.0f), -6.0f, 0.001f,
                 "a compressor at unity ratio leaves a level alone");
    require(rhino::forge::fxCompressorOutputDb(-6.0f, -18.0f, 8.0f, 0.0f) < -15.0f,
            "a high compressor ratio bends a level above threshold down");

    {
        rhino::forge::Rack compressed;
        auto& slot = compressed.slots[0];
        slot.type = static_cast<float>(FxType::compressor);
        slot.knobs = rhino::forge::fxTypes()[static_cast<size_t>(FxType::compressor)].init;
        slot.knobs[0] = 0.5f;  // -30 dB
        slot.knobs[1] = 1.0f;  // 20:1
        slot.knobs[2] = 0.0f;  // fastest attack
        slot.knobs[4] = 0.0f;  // no makeup
        slot.knobs[5] = 0.0f;  // hard knee
        rhino::forge::FxRack rack;
        rack.prepare(48000.0);
        auto tailMagnitude = 0.0f;
        for (int sample = 0; sample < 4096; ++sample)
        {
            auto left = 0.6f, right = 0.6f;
            rack.process(compressed, 120.0, left, right);
            if (sample >= 3072) tailMagnitude += std::abs(left);
        }
        require(tailMagnitude / 1024.0f < 0.12f,
                "the compressor's detector and gain stage reduce sustained audio above threshold");
    }

    rhino::forge::FxSlot staged;
    staged.type = static_cast<float>(FxType::phaser);
    for (int choice = 0; choice < 4; ++choice)
    {
        staged.modeB = static_cast<float>(choice) / 3.0f;
        require(rhino::forge::fxPhaserStages(staged) == std::array<int, 4>{4, 6, 8, 12}[static_cast<size_t>(choice)],
                "a phaser stage choice reaches the cascade it names");
    }
    {
        rhino::forge::Rack phased;
        auto& slot = phased.slots[0];
        slot.type = static_cast<float>(FxType::phaser);
        slot.knobs = rhino::forge::fxTypes()[static_cast<size_t>(FxType::phaser)].init;
        rhino::forge::FxRack rack;
        rack.prepare(48000.0);
        auto difference = 0.0f;
        for (int sample = 0; sample < 2048; ++sample)
        {
            const auto drySample = std::sin(juce::MathConstants<float>::twoPi * 440.0f
                                            * static_cast<float>(sample) / 48000.0f) * 0.4f;
            auto left = drySample, right = drySample;
            rack.process(phased, 120.0, left, right);
            difference += std::abs(left - drySample) + std::abs(right - drySample);
        }
        require(difference > 1.0f,
                "the phaser's allpass cascade changes the signal rather than only filling a menu row");
    }

    // A NaN that reaches a rack is let go of, not fed back for ever: nothing
    // else clears a rack's lines, all-notes-off included.
    {
        rhino::forge::Rack echoing;
        auto& slot = echoing.slots[0];
        slot.type = static_cast<float>(FxType::delay);
        slot.knobs = rhino::forge::fxTypes()[static_cast<size_t>(FxType::delay)].init;
        rhino::forge::FxRack rack;
        rack.prepare(48000.0);
        auto left = std::numeric_limits<float>::quiet_NaN(), right = left;
        rack.process(echoing, 120.0, left, right);
        auto finite = std::isfinite(left) && std::isfinite(right);
        for (int sample = 0; sample < 96000; ++sample)
        {
            auto quietLeft = 0.0f, quietRight = 0.0f;
            rack.process(echoing, 120.0, quietLeft, quietRight);
            finite = finite && std::isfinite(quietLeft) && std::isfinite(quietRight);
        }
        require(finite, "a NaN reaching a rack does not ring on in its feedback");
    }

    // --- The matrix reaches the rack ------------------------------------------
    //
    // FX run on the summed voices, so a per-voice source has to resolve to one
    // voice's value rather than to none. What is checked here is that it
    // arrives at all: a macro is a steady source, so the same patch at two
    // macro settings has to render differently.
    auto modulatedOwner = std::make_unique<rhino::forge::Processor>();
    auto& modulated = *modulatedOwner;
    soloSineOnA(modulated);
    place(modulated, 0, 0, FxType::filter);
    knob(modulated, 0, 0, 0, 0.05f);
    knob(modulated, 0, 0, 1, 0.0f);
    knob(modulated, 0, 0, 3, 0.0f);   // no parallel FAT path hiding the cutoff sweep
    const auto cutoffSlot = rhino::forge::fxDestinationOf(0, 0, 0);
    require(cutoffSlot > 0 && cutoffSlot < rhino::forge::destinationCount,
            "a rack knob has a destination index inside the list");
    requireText(juce::String(rhino::forge::destinations()[static_cast<size_t>(cutoffSlot)].id),
                rhino::forge::fxParameterId(0, 0, "Knob1"),
                "the destination list names the parameter it claims to");
    // Slots are numbered from one here, as the parameter ids are.
    setSlot(modulated, 1, static_cast<float>(rhino::forge::ModSource::macro1),
            static_cast<float>(cutoffSlot), 1.0f);
    setValue(modulated, "macro1", 0.0f);
    renderNote(modulated, buffer);
    const auto closed = brightness(buffer, 0, 1024);
    setValue(modulated, "macro1", 1.0f);
    renderNote(modulated, buffer);
    require(brightness(buffer, 0, 1024) > closed * 1.2f,
            "a macro pointed at a rack knob opens it");

    auto levelOwner = std::make_unique<rhino::forge::Processor>();
    auto& level = *levelOwner;
    soloSineOnA(level);
    place(level, 0, 0, FxType::filter);
    setValue(level, rhino::forge::fxParameterId(0, 0, "Level").toRawUTF8(), 0.0f);
    setSlot(level, 1, static_cast<float>(rhino::forge::ModSource::macro1),
            static_cast<float>(rhino::forge::fxLevelDestinationOf(0, 0)), 1.0f);
    setValue(level, "macro1", 0.0f);
    renderNote(level, buffer);
    const auto levelDown = rms(buffer, 0, 1024);
    setValue(level, "macro1", 1.0f);
    renderNote(level, buffer);
    require(rms(buffer, 0, 1024) > levelDown + 0.01f,
            "a macro pointed at an FX LEVEL raises the slot output");
}

void fxDisplaySuite()
{
    namespace ui = rhino::forge::ui;
    using rhino::forge::FxType;

    // --- Dragging a picture by its node ---------------------------------------
    //
    // A press lands on the value drawn under it, and a drag sets the knobs that
    // put the node under the pointer. Each gesture is held to its picture's own
    // axes: ask for a value, find where the picture draws it, drag there, and
    // read back what the knob now means.
    {
        const auto sampleRate = 48000.0, bpm = 120.0;
        ui::FxWell well;
        well.area = {0, 0, 320, 64};
        well.plot = well.area.reduced(2);
        const auto slotOf = [] (FxType type)
        {
            rhino::forge::FxSlot slot;
            slot.type = static_cast<float>(type);
            slot.knobs = rhino::forge::fxTypes()[static_cast<size_t>(type)].init;
            return slot;
        };
        const auto boxOf = [&well] (ui::FxWellKind kind) { return ui::fxPictureBox(kind, well.plot.toFloat()); };
        const auto drag = [&] (ui::FxWellKind kind, const rhino::forge::FxSlot& slot, juce::Point<float> press,
                               juce::Point<float> to, int part)
        {
            well.kind = kind;
            ui::FxWellGrab grab;
            require(ui::fxWellGrabAt(well, slot, press, grab), "a picture with a node takes a press");
            require(grab.part == part, "a press takes the part of the picture nearest it");
            auto moved = slot;
            const auto edit = ui::fxWellDragTo(well, grab, to, sampleRate, bpm);
            for (int i = 0; i < edit.count; ++i)
                moved.knobs[static_cast<size_t>(edit.knobs[static_cast<size_t>(i)])] = edit.values[static_cast<size_t>(i)];
            return moved;
        };
        const auto within = [] (float value, float wanted, float tolerance) { return std::abs(value - wanted) <= tolerance; };

        // The filter: CUTOFF across, the resonant peak up.
        {
            const auto box = boxOf(ui::FxWellKind::filterResponse);
            const auto to = juce::Point<float>(ui::filterHzToX(box, 1000.0f),
                                               box.getBottom() - (6.0f + 36.0f) / 54.0f * box.getHeight());
            const auto moved = drag(ui::FxWellKind::filterResponse, slotOf(FxType::filter), to, to, 0);
            require(within(rhino::forge::fxHertz(moved.knobs[0], 30.0f, 18000.0f), 1000.0f, 15.0f),
                    "dragging the filter's node across sets the cutoff under the pointer");
            require(within(moved.knobs[1], (2.0f - 0.5f) / 1.96f, 0.02f),
                    "and up, the resonance that peaks it six decibels there");
        }

        // The distortion's tone: FREQ across, Q as the node's height. Nothing
        // is taken while its filter is out of the path.
        {
            auto slot = slotOf(FxType::distortion);
            slot.modeB = 0.5f;   // PRE LP
            const auto box = boxOf(ui::FxWellKind::distortionTone);
            const auto to = juce::Point<float>(ui::filterHzToX(box, 1000.0f), ui::filterDbToY(box, 20.0f * std::log10(2.0f)));
            const auto moved = drag(ui::FxWellKind::distortionTone, slot, to, to, 0);
            require(within(rhino::forge::fxHertz(moved.knobs[1], 40.0f, 16000.0f), 1000.0f, 15.0f),
                    "dragging the distortion's node across sets its filter's FREQ");
            require(within(rhino::forge::fxScaled(moved.knobs[2], 0.4f, 8.0f), 2.0f, 0.03f), "and up, its Q");
            slot.modeB = 0.0f;   // OFF
            ui::FxWellGrab grab;
            well.kind = ui::FxWellKind::distortionTone;
            require(!ui::fxWellGrabAt(well, slot, to, grab), "a filter switched out of the path has no node to take");
        }

        // The distortion's shape is dragged by how far the pointer moves.
        {
            const auto slot = slotOf(FxType::distortion);
            const auto at = boxOf(ui::FxWellKind::distortionShape).getCentre();
            const auto moved = drag(ui::FxWellKind::distortionShape, slot, at, at.translated(0.0f, -30.0f), 0);
            require(within(moved.knobs[0], juce::jmin(1.0f, slot.knobs[0] + 0.25f), 0.001f),
                    "dragging the shape up drives it harder by how far it moved");
        }

        // The delay: TIME is the left side's first repeat, snapped to the
        // nearest division when synced; OFFSET the right side's.
        {
            auto slot = slotOf(FxType::delay);
            slot.modeB = 1.0f;          // BPM
            slot.knobs[0] = 5.0f / 8.0f; // 1/4, half a second at 120
            slot.knobs[2] = 0.5f;
            const auto box = boxOf(ui::FxWellKind::delayTape);
            const auto span = ui::fxDelayTapeSpan(slot, bpm);
            const auto xOf = [&] (float seconds) { return box.getX() + seconds / span * box.getWidth(); };
            const auto top = box.getY() + 2.0f;
            auto moved = drag(ui::FxWellKind::delayTape, slot, {xOf(0.5f), top}, {xOf(0.5f), top}, ui::fxPart::time);
            require(within(moved.knobs[0], 5.0f / 8.0f, 0.001f), "pressing a synced delay's repeat leaves it where it is");
            moved = drag(ui::FxWellKind::delayTape, slot, {xOf(0.5f), top}, {xOf(0.97f), top}, ui::fxPart::time);
            require(within(moved.knobs[0], 6.0f / 8.0f, 0.001f), "and dragging it out snaps to the division nearest the pointer");
            const auto bottom = box.getBottom() - 2.0f;
            moved = drag(ui::FxWellKind::delayTape, slot, {xOf(0.5f), bottom}, {xOf(0.75f), bottom}, ui::fxPart::offset);
            require(within(rhino::forge::fxOffsetRatio(moved.knobs[1]), 1.5f, 0.01f),
                    "the right side's repeat sets OFFSET as its share of TIME");
        }

        // The delay's loop: FREQ across, and Q as what the loop keeps there.
        {
            const auto box = boxOf(ui::FxWellKind::delayLoop);
            const auto to = juce::Point<float>(ui::filterHzToX(box, 3000.0f), box.getY() + 2.0f / 24.0f * box.getHeight());
            const auto moved = drag(ui::FxWellKind::delayLoop, slotOf(FxType::delay), to, to, 0);
            require(within(rhino::forge::fxHertz(moved.knobs[3], 200.0f, 16000.0f), 3000.0f, 45.0f),
                    "dragging the loop's node across sets its corner");
            require(within(20.0f * std::log10(rhino::forge::fxDelayLoopGain(moved, 3000.0f, sampleRate)), -2.0f, 0.1f),
                    "and up, the Q that keeps that much at the corner");
        }

        // The reverb: the tail's start is PRE-DLY, where it has fallen sixty
        // decibels is SIZE.
        {
            const auto slot = slotOf(FxType::reverb);
            const auto box = boxOf(ui::FxWellKind::reverbTail);
            const auto tX = [&box] (float seconds) { return box.getX() + std::sqrt(seconds / ui::fxReverbAxisSeconds) * box.getWidth(); };
            const auto pre = rhino::forge::fxScaled(slot.knobs[1], 0.0f, 0.2f);
            const auto bottom = box.getBottom() - 2.0f;
            const auto moved = drag(ui::FxWellKind::reverbTail, slot, {tX(pre + 1.0f), bottom}, {tX(pre + 2.0f), bottom},
                                    ui::fxPart::size);
            require(within(rhino::forge::fxReverbRt60(moved, 0.0f, sampleRate), 2.0f, 0.02f),
                    "dragging the tail's end sets the SIZE that rings that long");
            const auto top = box.getY() + 2.0f;
            const auto early = drag(ui::FxWellKind::reverbTail, slot, {tX(0.1f), top}, {tX(0.1f), top}, ui::fxPart::preDelay);
            require(within(rhino::forge::fxScaled(early.knobs[1], 0.0f, 0.2f), 0.1f, 0.002f),
                    "and its start sets the pre-delay");
        }

        // The reverb's tone: whichever cut is nearer.
        {
            auto slot = slotOf(FxType::reverb);
            slot.knobs[4] = 0.3f;
            slot.knobs[5] = 0.7f;
            const auto box = boxOf(ui::FxWellKind::reverbDecay);
            const auto low = ui::filterHzToX(box, rhino::forge::fxHertz(slot.knobs[4], 20.0f, 1200.0f));
            const auto moved = drag(ui::FxWellKind::reverbDecay, slot, {low + 3.0f, box.getCentreY()},
                                    {ui::filterHzToX(box, 200.0f), box.getCentreY()}, ui::fxPart::lowCut);
            require(within(rhino::forge::fxHertz(moved.knobs[4], 20.0f, 1200.0f), 200.0f, 3.0f),
                    "dragging near LO CUT's line moves LO CUT");
            require(moved.knobs[5] == slot.knobs[5], "and leaves HI CUT alone");
        }

        // The equaliser: the nearer band's frequency across and gain up.
        {
            const auto slot = slotOf(FxType::equaliser);
            const auto box = boxOf(ui::FxWellKind::eqResponse);
            const auto nodes = ui::fxEqNodes(box, slot);
            const auto to = juce::Point<float>(ui::filterHzToX(box, 5000.0f), box.getCentreY() - 6.0f / 24.0f * box.getHeight() * 0.5f);
            const auto moved = drag(ui::FxWellKind::eqResponse, slot, nodes[1].translated(2.0f, 1.0f), to, ui::fxPart::highBand);
            require(within(rhino::forge::fxHertz(moved.knobs[3], 500.0f, 18000.0f), 5000.0f, 75.0f),
                    "dragging the high band's node sets its frequency");
            require(within(rhino::forge::fxScaled(moved.knobs[5], -18.0f, 18.0f), 6.0f, 0.2f), "and its gain");
            require(moved.knobs[0] == slot.knobs[0] && moved.knobs[2] == slot.knobs[2], "and leaves the low band where it was");
        }

        // The compressor: THRESH where the curve bends, RATIO by its far end.
        {
            auto slot = slotOf(FxType::compressor);
            slot.modeB = 0.0f;   // MANUAL
            slot.knobs[4] = 0.0f;
            slot.knobs[0] = ui::fxKnobForScaled(-24.0f, -60.0f, 0.0f);
            const auto box = boxOf(ui::FxWellKind::compressorCurve);
            const auto xOf = [&box] (float db) { return box.getX() + (db + 60.0f) / 60.0f * box.getWidth(); };
            const auto yOf = [&box] (float db) { return box.getBottom() - (db + 60.0f) / 66.0f * box.getHeight(); };
            auto moved = drag(ui::FxWellKind::compressorCurve, slot, {xOf(-30.0f), box.getCentreY()},
                              {xOf(-24.0f), box.getCentreY()}, ui::fxPart::threshold);
            require(within(rhino::forge::fxCompressorThreshold(moved.knobs[0]), -24.0f, 0.3f),
                    "dragging the knot across sets the threshold");
            moved = drag(ui::FxWellKind::compressorCurve, slot, {box.getRight() - 3.0f, yOf(-10.0f)},
                         {box.getRight() - 3.0f, yOf(-18.0f)}, ui::fxPart::ratio);
            require(within(rhino::forge::fxCompressorRatio(moved.knobs[1]), 4.0f, 0.1f),
                    "and the far end, the ratio that brings full scale out there");
        }

        // The chorus: the nearer tap's delay, taken at the right.
        {
            auto slot = slotOf(FxType::chorus);
            slot.knobs[1] = 0.1f;
            slot.knobs[2] = 0.6f;
            const auto box = boxOf(ui::FxWellKind::chorusTaps);
            const auto msY = [&box] (float ms) { return box.getBottom() - ms / 42.0f * box.getHeight(); };
            const auto second = msY(rhino::forge::fxScaled(slot.knobs[2], 0.5f, 30.0f));
            const auto moved = drag(ui::FxWellKind::chorusTaps, slot, {box.getRight() - 4.0f, second + 1.0f},
                                    {box.getRight() - 4.0f, msY(20.0f)}, ui::fxPart::secondTap);
            require(within(rhino::forge::fxScaled(moved.knobs[2], 0.5f, 30.0f), 20.0f, 0.3f),
                    "dragging the second tap sets DELAY 2");
            ui::FxWellGrab grab;
            well.kind = ui::FxWellKind::chorusTaps;
            require(!ui::fxWellGrabAt(well, slot, {box.getX() + 4.0f, second}, grab),
                    "the chorus's history, at the left, holds nothing to take");
        }

        // The phaser's centre follows the pointer across.
        {
            const auto box = boxOf(ui::FxWellKind::phaserComb);
            const auto to = juce::Point<float>(ui::filterHzToX(box, 1000.0f), box.getCentreY());
            const auto moved = drag(ui::FxWellKind::phaserComb, slotOf(FxType::phaser), to, to, 0);
            require(within(rhino::forge::fxHertz(moved.knobs[2], 80.0f, 8000.0f), 1000.0f, 15.0f),
                    "dragging the phaser's marker sets its FREQ");
        }

        // The gain-reduction meter and the flanger's comb show what is
        // happening and take nothing.
        for (const auto kind : {ui::FxWellKind::compressorMeter, ui::FxWellKind::flangerComb})
        {
            ui::FxWellGrab grab;
            well.kind = kind;
            require(!ui::fxWellGrabAt(well, slotOf(FxType::flanger), well.area.getCentre().toFloat(), grab),
                    "a picture with nothing to set takes no press");
        }
    }

    // Every active strip keeps its common controls. A type with only three
    // named knobs still needs MIX, LEVEL and its card, which is its bypass;
    // hiding unused general knobs must not make the frame disappear with them.
    {
        rhino::forge::Processor processor;
        const std::array<FxType, 6> types {FxType::chorus, FxType::distortion, FxType::reverb,
                                           FxType::equaliser, FxType::delay, FxType::filter};
        for (int slot = 0; slot < static_cast<int>(types.size()); ++slot)
            setValue(processor, rhino::forge::fxParameterId(0, slot, "Type").toRawUTF8(),
                     static_cast<float>(types[static_cast<size_t>(slot)]));
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1440, 1200);
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<rhino::forge::ui::PageTab*>(child))
                if (tab->getButtonText() == "FX" && tab->onClick) tab->onClick();

        auto mixes = 0, cards = 0, chips = 0;
        ui::FxPlate* chorusCard = nullptr;
        const auto countControls = [&] (auto&& self, juce::Component& parent) -> void
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* label = dynamic_cast<juce::Label*>(child))
                    if (label->isVisible() && label->getText() == "MIX") ++mixes;
                if (auto* plate = dynamic_cast<ui::FxPlate*>(child))
                    if (plate->isVisible())
                    {
                        ++cards;
                        if (plate->type == static_cast<int>(FxType::chorus)) chorusCard = plate;
                    }
                if (auto* chip = dynamic_cast<ui::ToggleChip*>(child))
                    if (chip->isVisible() && chip->getButtonText() == "BYP") ++chips;
                self(self, *child);
            }
        };
        countControls(countControls, *editor);
        const auto* fxModule = [&]() -> const ui::Module*
        {
            for (const auto& module : ui::modules())
                if (juce::String(module.id) == "fx") return &module;
            return nullptr;
        }();
        require(fxModule != nullptr, "the FX display test finds the rack module");
        const auto moduleArea = fxModule == nullptr ? juce::Rectangle<int>()
            : ui::fxModuleBounds(editor->getLocalBounds(), *fxModule, false);
        const auto visible = juce::jmin(static_cast<int>(types.size()),
                                        ui::fxIntersectingSlotCount(moduleArea));
        require(ui::fxIntersectingSlotCount(moduleArea) > ui::fxVisibleSlotCount(moduleArea),
                "the rack control test includes a row crossing the viewport edge");
        require(mixes == visible,
                "every fully or partly visible effect strip keeps its common MIX control");
        require(cards == visible,
                "every fully or partly visible effect strip keeps its identity card, which is its bypass");
        require(chips == 0, "a rack slot's bypass is its card, not a chip beside it");

        // The card is the switch: a click bypasses the effect and the card
        // goes dark; a second click brings it back and lights it again.
        require(chorusCard != nullptr && chorusCard->lit, "an effect that is in has a lit card");
        if (chorusCard != nullptr && chorusCard->onPlateClick)
        {
            const auto bypassId = rhino::forge::fxParameterId(0, 0, "Bypass");
            chorusCard->onPlateClick();
            require(value(processor, bypassId.toRawUTF8()) >= 0.5f, "clicking an effect's card bypasses it");
            require(!chorusCard->lit, "a bypassed effect's card goes dark");
            chorusCard->onPlateClick();
            require(value(processor, bypassId.toRawUTF8()) < 0.5f, "clicking it again brings the effect back in");
            require(chorusCard->lit, "and lights its card again");
        }
    }

    // The list and the rack scroll separately. A full chain is eight rows of
    // list beside four strips of rack at the size the panel opens at, so this
    // is where the two would be seen moving together if they still did.
    {
        rhino::forge::Processor processor;
        const std::array<FxType, 8> types {FxType::distortion, FxType::delay, FxType::distortion,
                                           FxType::delay, FxType::reverb, FxType::equaliser,
                                           FxType::compressor, FxType::chorus};
        for (int slot = 0; slot < static_cast<int>(types.size()); ++slot)
            setValue(processor, rhino::forge::fxParameterId(0, slot, "Type").toRawUTF8(),
                     static_cast<float>(types[static_cast<size_t>(slot)]));
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<ui::PageTab*>(child))
                if (tab->getButtonText() == "FX" && tab->onClick) tab->onClick();

        const auto plateShown = [&] (const juce::String& name)
        {
            auto found = false;
            const auto search = [&] (auto&& self, juce::Component& parent) -> void
            {
                for (auto* child : parent.getChildren())
                {
                    if (auto* plate = dynamic_cast<ui::FxPlate*>(child))
                        if (plate->isVisible() && plate->getName() == name) found = true;
                    self(self, *child);
                }
            };
            search(search, *editor);
            return found;
        };
        const ui::Module* fxModule = nullptr;
        for (const auto& module : ui::modules())
            if (juce::String(module.id) == "fx") fxModule = &module;
        require(fxModule != nullptr, "the scrolling test finds the rack module");
        const auto area = fxModule == nullptr ? juce::Rectangle<int>()
            : ui::fxModuleBounds(editor->getLocalBounds(), *fxModule, false);
        require(ui::fxListVisibleRowCount(area) >= fxSlotCount,
                "a full chain fits the list at the size the panel opens at");
        require(ui::fxVisibleSlotCount(area) < fxSlotCount,
                "and does not fit the rack, so the rack has somewhere to scroll");

        const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
        const auto when = juce::Time::getCurrentTime();
        const auto eventAt = [&] (juce::Point<int> at, juce::Component* on)
        {
            return juce::MouseEvent(mouse, at.toFloat(), juce::ModifierKeys(),
                                    1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    on, on, when, at.toFloat(), when, 1, false);
        };
        juce::MouseWheelDetails down;
        down.deltaY = -0.6f;
        juce::MouseWheelDetails up = down;
        up.deltaY = 1.2f;

        require(plateShown("DIST") && !plateShown("CHORUS"), "the rack opens on the top of the chain");
        editor->mouseWheelMove(eventAt(ui::fxListViewportBounds(area, true).getCentre(), editor.get()), down);
        require(plateShown("DIST") && !plateShown("CHORUS"),
                "the wheel over the list leaves the rack where it was");

        const auto last = ui::fxListItemBounds(area, fxSlotCount - 1, true).getCentre();
        editor->mouseDown(eventAt(last, editor.get()));
        editor->mouseUp(eventAt(last, editor.get()));
        require(plateShown("CHORUS") && !plateShown("DIST"),
                "picking the last row brings its strip into view");

        // A knob keeps the wheel: the panel hears of it only as a listener, and
        // the event then names the knob.
        const auto rackPoint = ui::fxSlotViewportBounds(area, true).getCentre();
        juce::Slider knob;
        editor->mouseWheelMove(eventAt(rackPoint, &knob), up);
        require(plateShown("CHORUS") && !plateShown("DIST"),
                "the wheel turning a rack knob does not also scroll the rack");
        editor->mouseWheelMove(eventAt(rackPoint, editor.get()), up);
        require(plateShown("DIST") && !plateShown("CHORUS"),
                "the wheel over the rack itself scrolls the rack");
    }

    // A rack picture is dragged through the panel itself. The pointer over a
    // node's picture turns to a crosshair, a press there puts the knob the node
    // stands for under the pointer, and a drag carries it on. Found by moving
    // the pointer, so the test knows nothing of where the strip puts the well.
    {
        auto owner = std::make_unique<rhino::forge::Processor>();
        auto& processor = *owner;
        setValue(processor, rhino::forge::fxParameterId(0, 0, "Type").toRawUTF8(), static_cast<float>(FxType::phaser));
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<ui::PageTab*>(child))
                if (tab->getButtonText() == "FX" && tab->onClick) tab->onClick();
        const ui::Module* fxModule = nullptr;
        for (const auto& module : ui::modules())
            if (juce::String(module.id) == "fx") fxModule = &module;
        require(fxModule != nullptr, "the drag test finds the rack module");
        const auto area = fxModule == nullptr ? juce::Rectangle<int>()
            : ui::fxModuleBounds(editor->getLocalBounds(), *fxModule, false);
        const auto viewport = ui::fxSlotViewportBounds(area, true);

        const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
        const auto when = juce::Time::getCurrentTime();
        const auto eventAt = [&] (juce::Point<int> at)
        {
            return juce::MouseEvent(mouse, at.toFloat(), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    editor.get(), editor.get(), when, at.toFloat(), when, 1, false);
        };
        const auto takes = [&] (juce::Point<int> at)
        {
            editor->mouseMove(eventAt(at));
            return editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::CrosshairCursor);
        };
        auto node = juce::Point<int>(-1, -1);
        for (int y = viewport.getY() + 8; y < viewport.getY() + 70 && node.x < 0; y += 4)
            for (int x = viewport.getX(); x < viewport.getRight() && node.x < 0; x += 4)
                if (takes({x, y})) node = {x, y + 6};
        require(node.x >= 0, "the pointer over the phaser's picture says it can be taken");
        // Pressed in the middle of the picture, clear of both ends of FREQ's
        // range, which is narrower than the picture's axis.
        auto right = node.x;
        while (right < viewport.getRight() && takes({right + 2, node.y})) right += 2;
        node.x = (node.x + right) / 2;

        const auto freq = rhino::forge::fxParameterId(0, 0, "Knob3");
        editor->mouseDown(eventAt(node));
        const auto pressed = value(processor, freq.toRawUTF8());
        editor->mouseDrag(eventAt(node.translated(30, 0)));
        const auto dragged = value(processor, freq.toRawUTF8());
        editor->mouseUp(eventAt(node.translated(30, 0)));
        if (!(dragged > pressed + 0.02f))
            std::cerr << "       pressed at " << node.x << "," << node.y << " in " << viewport.toString()
                      << ": FREQ " << pressed << " then " << dragged << std::endl;
        require(dragged > pressed + 0.02f, "dragging the phaser's marker to the right raises its FREQ");
        editor->mouseDrag(eventAt(node.translated(-30, 0)));
        require(value(processor, freq.toRawUTF8()) == dragged, "and once let go, the pointer moves nothing");
    }

    // --- The equaliser ---------------------------------------------------------
    //
    // A band's magnitude is read off the coefficients setBand built, so a band
    // asked for no gain at all has to measure as no gain at all — at every
    // frequency, not only at its corner. This is what would catch a shelf built
    // from the wrong cookbook formula: it would still look like a shelf.
    {
        constexpr auto rate = 48000.0;
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(FxType::equaliser)];
        rhino::forge::FxSlot flat;
        flat.type = static_cast<float>(FxType::equaliser);
        flat.knobs = info.init;
        requireClose(rhino::forge::fxScaled(flat.knobs[2], -18.0f, 18.0f), 0.0f, 0.01f,
                     "the equaliser this checks really is asking for no gain");

        rhino::forge::Biquad low, high;
        rhino::forge::setBand(low, rhino::forge::BandShape::lowShelf,
                              rhino::forge::fxHertz(flat.knobs[0], 20.0f, 2000.0f),
                              rhino::forge::fxScaled(flat.knobs[1], 0.2f, 6.0f),
                              rhino::forge::fxScaled(flat.knobs[2], -18.0f, 18.0f), rate);
        rhino::forge::setBand(high, rhino::forge::BandShape::highShelf,
                              rhino::forge::fxHertz(flat.knobs[3], 500.0f, 18000.0f),
                              rhino::forge::fxScaled(flat.knobs[4], 0.2f, 6.0f),
                              rhino::forge::fxScaled(flat.knobs[5], -18.0f, 18.0f), rate);
        for (const auto hz : {30.0f, 120.0f, 440.0f, 2000.0f, 9000.0f, 17000.0f})
        {
            const auto gain = ui::biquadMagnitude(low, hz, rate) * ui::biquadMagnitude(high, hz, rate);
            requireClose(gain, 1.0f, 0.01f, "an equaliser opening flat measures flat at every frequency");
        }

        // A shelf asked for a boost has to measure as one below its corner and
        // as nothing well above it, or the display is drawing the wrong band.
        rhino::forge::Biquad boosted;
        rhino::forge::setBand(boosted, rhino::forge::BandShape::lowShelf, 200.0f, 0.7f, 12.0f, rate);
        require(ui::biquadMagnitude(boosted, 30.0f, rate) > 3.0f,
                "a low shelf asked for +12 dB lifts what is under it");
        requireClose(ui::biquadMagnitude(boosted, 12000.0f, rate), 1.0f, 0.05f,
                     "a low shelf leaves what is well above it alone");
    }

    // --- The distortion --------------------------------------------------------
    //
    // The transfer curve is fxShape called per pixel, so the two cannot disagree
    // by construction — what is worth checking is that the shapes behave the way
    // a curve drawn from them would be read: passing through the origin, odd
    // about it where they claim to be, and never leaving the box.
    {
        for (int shape = 0; shape < 8; ++shape)
        {
            if (shape == 7) continue;   // downsampling is a rate, not a curve
            for (const auto drive : {0.0f, 0.4f, 1.0f})
            {
                requireClose(rhino::forge::fxShape(shape, 0.0f, drive), 0.0f, 0.001f,
                             "a distortion shape leaves silence silent");
                for (const auto in : {-1.0f, -0.6f, -0.2f, 0.2f, 0.6f, 1.0f})
                {
                    const auto out = rhino::forge::fxShape(shape, in, drive);
                    if (!std::isfinite(out) || std::abs(out) > 1.001f)
                    {
                        require(false, "a distortion shape stays inside the box its curve is drawn in");
                        std::cerr << "       shape " << shape << " drive " << drive
                                  << " in " << in << " out " << out << '\n';
                    }
                }
            }
        }
        // Hard clipping at no drive is the one shape that is exactly the
        // diagonal the display draws behind every curve, which makes it the
        // check that the diagonal means what it claims.
        for (const auto in : {-0.9f, -0.3f, 0.3f, 0.9f})
            requireClose(rhino::forge::fxShape(2, in, 0.0f), in, 0.001f,
                         "hard clipping at no drive is the identity the faint diagonal stands for");

        // OFF / PRE / POST used to be the whole selector at 0 / .5 / 1.
        // The expanded LP/HP choices keep those saved values on the old
        // low-pass placements, while the choices between them select HP.
        rhino::forge::FxSlot filtered;
        filtered.type = static_cast<float>(FxType::distortion);
        filtered.modeB = 0.5f;
        require(rhino::forge::fxDistortionFilterPlacement(filtered) == 1
                    && !rhino::forge::fxDistortionFilterHighPass(filtered),
                "an old PRE distortion-filter value remains PRE LP");
        filtered.modeB = 1.0f;
        require(rhino::forge::fxDistortionFilterPlacement(filtered) == 2
                    && !rhino::forge::fxDistortionFilterHighPass(filtered),
                "an old POST distortion-filter value remains POST LP");
        filtered.modeB = 0.25f;
        require(rhino::forge::fxDistortionFilterPlacement(filtered) == 1
                    && rhino::forge::fxDistortionFilterHighPass(filtered),
                "the distortion filter offers a pre-shaper high-pass tap");

        const auto draw = [&] (float mode)
        {
            filtered.modeB = mode;
            filtered.knobs = rhino::forge::fxTypes()[static_cast<size_t>(FxType::distortion)].init;
            juce::Image image(juce::Image::ARGB, 220, 60, true);
            juce::Graphics graphics(image);
            ui::drawFxDistortion(graphics, image.getBounds().toFloat(), filtered,
                                 ui::fxTypeColour(static_cast<int>(FxType::distortion)), 1.0f);
            return image;
        };
        const auto lp = draw(0.5f), hp = draw(0.25f);
        auto differentFilterPixels = 0;
        for (int y = 0; y < lp.getHeight(); ++y)
            for (int x = 0; x < lp.getWidth() / 2; ++x)
                if (lp.getPixelAt(x, y) != hp.getPixelAt(x, y)) ++differentFilterPixels;
        require(differentFilterPixels > 20,
                "the distortion display visibly distinguishes its LP and HP responses");
    }

    // --- The delay -------------------------------------------------------------
    //
    // The repeats are placed by the same fxDelaySeconds the line is read at, so
    // what is checked is that a synced delay lands on the beat it names.
    {
        rhino::forge::FxSlot synced;
        synced.type = static_cast<float>(FxType::delay);
        synced.modeB = 1.0f;   // BPM rather than milliseconds
        // The division a knob lands on, and the time that division is at 120.
        for (int step = 0; step < rhino::forge::fxDivisionCount; ++step)
        {
            const auto at = static_cast<float>(step) / (rhino::forge::fxDivisionCount - 1);
            synced.knobs[0] = at;
            const auto& division = rhino::forge::fxDivisionAt(at);
            const auto expected = juce::jlimit(0.001f, rhino::forge::fxMaxDelayTime,
                                               0.5f * division.beats);
            requireClose(rhino::forge::fxDelaySeconds(synced, 120.0), expected, 0.0005f,
                         "a synced delay lands on the division its readout names");
        }
    }

    // --- The reverb ------------------------------------------------------------
    //
    // The envelope is decay raised to the number of comb round trips. SIZE sets
    // the room and its natural decay, and a hall holds longer than a plate.
    {
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(FxType::reverb)];
        rhino::forge::FxSlot room;
        room.type = static_cast<float>(FxType::reverb);
        room.knobs = info.init;
        room.modeA = 0.0f;
        const auto plate = rhino::forge::fxReverbDecay(room);
        room.modeA = 1.0f;
        const auto hall = rhino::forge::fxReverbDecay(room);
        require(hall > plate, "a hall holds its energy longer than a plate at the same setting");
        room.modeA = 0.0f;
        room.knobs[0] = 0.0f;
        const auto small = rhino::forge::fxReverbDecay(room);
        room.knobs[0] = 1.0f;
        require(rhino::forge::fxReverbDecay(room) > small,
                "turning reverb size up lengthens the room's natural tail");
        require(info.knobs[4] != nullptr && juce::String(info.knobs[4]) == "LO CUT"
                    && info.knobs[5] != nullptr && juce::String(info.knobs[5]) == "HI CUT",
                "the reverb exposes both ends of its tail filter");
    }

    // Expanding the filter list must not reinterpret an old LP / HP / BP
    // automation value. Those were stored at 0 / .5 / 1, so their richer
    // equivalents deliberately still occupy those normalised positions.
    {
        const auto& models = rhino::forge::fxTypes()[static_cast<size_t>(FxType::filter)].modeA;
        require(juce::String(rhino::forge::fxModeName(models, 0.0f)).contains("LOW"),
                "an old low-pass value still chooses a low-pass model");
        require(juce::String(rhino::forge::fxModeName(models, 0.5f)).contains("HIGH"),
                "an old high-pass value still chooses a high-pass model");
        require(juce::String(rhino::forge::fxModeName(models, 1.0f)).contains("BAND"),
                "an old band-pass value still chooses a band-pass model");
    }

    // --- Reading a parameter back while it is still being announced ------------
    //
    // The panel used to read parameter values from the cached atomic beside
    // them, and a mode field appeared to wait for an unrelated click before it
    // caught up. This is why: that atomic is kept up to date by one of the
    // parameter's own listeners, and JUCE calls listeners in the reverse of the
    // order they registered. A panel attachment registers after the state does,
    // so it is called first — and reads the value from before the change it is
    // being told about.
    //
    // What the panel reads now is the parameter itself, which stores its value
    // before it tells anybody. This pins that property rather than the panel
    // that depends on it, because the property is the whole of the fix.
    {
        rhino::forge::Processor processor;
        auto* parameter = processor.state.getParameter("fx1s1ModeA");
        require(parameter != nullptr, "the parameter this checks exists");
        if (parameter != nullptr)
        {
            struct Watcher final : juce::AudioProcessorParameter::Listener
            {
                Watcher(rhino::forge::Processor& p, juce::RangedAudioParameter& r)
                    : processor(p), ranged(r) { ranged.addListener(this); }
                ~Watcher() override { ranged.removeListener(this); }
                void parameterValueChanged(int, float) override
                {
                    ++calls;
                    fromParameter = ranged.convertFrom0to1(ranged.getValue());
                    const auto* atomic = processor.state.getRawParameterValue("fx1s1ModeA");
                    fromAtomic = atomic == nullptr ? -1.0f : atomic->load();
                }
                void parameterGestureChanged(int, bool) override {}
                rhino::forge::Processor& processor;
                juce::RangedAudioParameter& ranged;
                int calls = 0;
                float fromParameter = -1.0f, fromAtomic = -1.0f;
            };

            Watcher watcher(processor, *parameter);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(1.0f));
            require(watcher.calls > 0, "setting a parameter tells its listeners");
            requireClose(watcher.fromParameter, 1.0f, 0.001f,
                         "a parameter read inside its own announcement is already the new value");
            // The atomic is allowed to be either, and saying which it was makes
            // the reason for the fix visible when this is read later.
            if (std::abs(watcher.fromAtomic - 1.0f) > 0.001f)
                std::cerr << "       (the cached atomic was still "
                          << watcher.fromAtomic << " at that moment, which is the race)" << '\n';
        }
    }

    // --- A mode field, read back the way it is written -------------------------
    //
    // The parameter behind a mode is a plain 0..1, because what it steps
    // through changes with the type. The panel spreads a choice across that
    // range and fxModeOf reads it back; if the two ever disagreed, a field
    // would show one state and the engine would run another. So every choice of
    // every mode of every type is written and read here.
    for (int type = 0; type < rhino::forge::fxTypeCount; ++type)
    {
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(type)];
        for (const auto* mode : {&info.modeA, &info.modeB})
        {
            if (mode->count <= 1) continue;
            for (int choice = 0; choice < mode->count; ++choice)
            {
                const auto at = static_cast<float>(choice) / static_cast<float>(mode->count - 1);
                if (rhino::forge::fxModeOf(*mode, at) != choice)
                {
                    require(false, "a mode choice reads back as the one that was set");
                    std::cerr << "       " << info.name << " choice " << choice
                              << " of " << mode->count << '\n';
                }
            }
            // Every choice a field offers has to be named, or the selector
            // draws an empty segment.
            for (int choice = 0; choice < mode->count; ++choice)
                require(mode->choices[static_cast<size_t>(choice)] != nullptr
                            && juce::String(mode->choices[static_cast<size_t>(choice)]).isNotEmpty(),
                        "every choice a mode offers has a name to draw");
            require(mode->label != nullptr, "a mode field that has choices has a label");
        }
    }

    // --- A stack of choices, at the size the panel gives it ---------------------
    //
    // The choices are stacked, so the field has to divide into as many rows as
    // the most any mode offers and every one of them still be readable. Checked
    // as geometry rather than by eye: the three-choice case is the tight one,
    // and it is tight at the smallest window rather than at the default.
    {
        const auto tightest = juce::Rectangle<int>(0, 0, rhino::forge::ui::minPanelWidth,
                                                   rhino::forge::ui::minPanelHeight);
        const auto shared = rhino::forge::ui::uniformKnobDiameter(tightest);
        rhino::forge::Processor processor;
        for (const auto& module : rhino::forge::ui::modules())
        {
            // The rack's fields are laid out per type and are checked in its
            // strip sweep (tests/ForgeTestsLayout.cpp).
            if (juce::String(module.id) == "fx") continue;
            const auto area = rhino::forge::ui::moduleBounds(tightest, module);
            for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            {
                const auto& controls = module.rows[static_cast<size_t>(r)].controls;
                for (int c = 0; c < static_cast<int>(controls.size()); ++c)
                {
                    if (controls[static_cast<size_t>(c)].style != rhino::forge::ui::Style::selector)
                        continue;
                    const auto block = rhino::forge::ui::controlBlock(area, module, r, c, shared);
                    // A field seated inside a display has no label strip over
                    // it — it is inside the thing it names — so the strip it
                    // was given is the whole field.
                    const auto seated = rhino::forge::ui::seatedOutsideBody(module, r);
                    const auto field = seated
                        ? block
                        : block.withTrimmedTop(rhino::forge::ui::stepperLabelHeight);

                    rhino::forge::ui::FxSelector selector;
                    selector.setBounds(field);
                    // A field's geometry follows how many choices it is holding,
                    // so it is given some. Nothing here draws them.
                    const auto holding = [&selector] (int howMany)
                    {
                        selector.choices.assign(static_cast<size_t>(howMany), "X");
                    };

                    // A seated field is held to a different standard, and a
                    // lower one: the strip it sits in is declared at the height
                    // of a single line, so it is asked to be readable as a line
                    // rather than to divide into three. What makes that safe is
                    // that it cannot be given a stack in the first place — a
                    // field is only ever seated where the parameter behind it
                    // has more choices than fit inline, which is what this
                    // pins. Seat a two-state parameter in a display and the
                    // component would stack it into slivers, and this is what
                    // would say so.
                    if (seated)
                    {
                        const auto* choices = dynamic_cast<const juce::AudioParameterChoice*>(
                            processor.state.getParameter(controls[static_cast<size_t>(c)].id));
                        require(choices != nullptr
                                    && choices->choices.size()
                                           > rhino::forge::ui::FxSelector::inlineLimit,
                                "a field seated in a display has too many choices to stack");
                        if (choices != nullptr)
                            holding(choices->choices.size());
                        require(field.getHeight() >= 18,
                                "a seated field stays tall enough to read a line in");
                        require(field.withZeroOrigin().contains(selector.listBounds()),
                                "a seated field draws its line inside the strip it was given");
                        continue;
                    }
                    // A fixed AudioParameterChoice with more than the inline
                    // limit can only ever draw the single-line arrow form.
                    // The two- and three-state stack belongs to the rack's
                    // type-dependent mode controls, whose parameter is a plain
                    // normalised float because its choices change with type.
                    const auto* fixedChoices = dynamic_cast<const juce::AudioParameterChoice*>(
                        processor.state.getParameter(controls[static_cast<size_t>(c)].id));
                    if (fixedChoices != nullptr
                        && fixedChoices->choices.size() > rhino::forge::ui::FxSelector::inlineLimit)
                    {
                        holding(fixedChoices->choices.size());
                        require(field.withZeroOrigin().contains(selector.listBounds()),
                                "a fixed long selector draws its line inside the box it was given");
                        continue;
                    }
                    // Every mode field that stacks its choices, at its widest.
                    for (int count = 2; count <= rhino::forge::ui::FxSelector::inlineLimit; ++count)
                    {
                        holding(count);
                        auto covered = 0;
                        for (int i = 0; i < count; ++i)
                        {
                            const auto segment = selector.segmentBounds(i);
                            require(segment.getHeight() >= 14,
                                    "a stacked choice stays tall enough to read");
                            // Which field, because the answer is always a row
                            // weight somewhere and the message alone does not
                            // say whose.
                            if (segment.getHeight() < 14 && i == 0)
                                std::cerr << "       " << module.id << " row " << r << " cell " << c
                                          << ": " << count << " choices in a field "
                                          << field.getHeight() << " tall (block "
                                          << block.getHeight() << ", cell "
                                          << rhino::forge::ui::cellBounds(area, module, r, c).getHeight()
                                          << ")" << '\n';
                            require(segment.getWidth() == field.getWidth(),
                                    "a stacked choice takes the whole width of its field");
                            covered += segment.getHeight();
                            for (int j = i + 1; j < count; ++j)
                                require(!segment.intersects(selector.segmentBounds(j)),
                                        "no two stacked choices overlap");
                        }
                        require(covered == field.getHeight(),
                                "the stack fills its field exactly, with no gap and no overhang");
                    }
                    // A field with more choices than fit draws one line instead,
                    // and it has to sit inside the same box.
                    holding(rhino::forge::warpModeCount);
                    require(field.withZeroOrigin().contains(selector.listBounds()),
                            "a field too long to stack draws a line inside the box it was given");
                }
            }
        }
    }

    // --- What a mode makes meaningless -----------------------------------------
    //
    // Three rules, each a fact about the effect rather than about the panel.
    // Checked both ways round, because a rule that greys a knob and never
    // ungreys it looks exactly like one that works.
    {
        rhino::forge::FxSlot distortion;
        distortion.type = static_cast<float>(FxType::distortion);
        const auto& dist = rhino::forge::fxTypes()[static_cast<size_t>(FxType::distortion)];
        distortion.modeB = 0.0f;   // FILTER OFF
        require(rhino::forge::fxKnobLive(distortion, 0), "DRIVE is live whatever the filter is doing");
        require(!rhino::forge::fxKnobLive(distortion, 1),
                "a distortion's FREQ is dead while its filter is switched off");
        require(!rhino::forge::fxKnobLive(distortion, 2),
                "a distortion's Q is dead while its filter is switched off");
        distortion.modeB = 1.0f / static_cast<float>(dist.modeB.count - 1);   // PRE
        require(rhino::forge::fxKnobLive(distortion, 1),
                "a distortion's FREQ comes back once the filter is in the path");

        rhino::forge::FxSlot eq;
        eq.type = static_cast<float>(FxType::equaliser);
        const auto& bands = rhino::forge::fxTypes()[static_cast<size_t>(FxType::equaliser)];
        eq.modeA = 0.0f;   // SHELF
        require(rhino::forge::fxKnobLive(eq, 2), "a shelf has a gain to set");
        eq.modeA = 1.0f;   // the last choice, HI PASS
        require(!rhino::forge::fxKnobLive(eq, 2), "a high pass has no gain to set");
        require(rhino::forge::fxKnobLive(eq, 0), "a high pass still has a frequency to set");
        eq.modeB = 1.0f;   // LO PASS
        require(!rhino::forge::fxKnobLive(eq, 5), "a low pass has no gain to set");
        juce::ignoreUnused(bands);

        rhino::forge::FxSlot compressor;
        compressor.type = static_cast<float>(FxType::compressor);
        compressor.modeB = 0.0f;   // MANUAL
        require(rhino::forge::fxKnobLive(compressor, 4),
                "manual compressor gain leaves MAKEUP live");
        compressor.modeB = 1.0f;   // AUTO
        require(!rhino::forge::fxKnobLive(compressor, 4),
                "automatic compressor gain greys its unused MAKEUP knob");
        require(rhino::forge::fxKnobLive(compressor, 0),
                "automatic gain does not disable the compressor threshold");

        // The filter's six-decibel models are one pole each and have no
        // resonance; every other model has, and nothing else is taken away.
        // On PEAK 12 FAT sets the peak's height, and its caption says so.
        rhino::forge::FxSlot filter;
        filter.type = static_cast<float>(FxType::filter);
        for (int model = 0; model < 8; ++model)
        {
            filter.modeA = static_cast<float>(model) / 7.0f;
            const auto onePole = model == 1 || model == 5;
            require(rhino::forge::fxKnobLive(filter, 1) == !onePole,
                    onePole ? "a six-decibel filter has no resonance to set"
                            : "a two-pole filter's RES is live");
            for (const auto knob : {0, 2, 3, 4, 5})
                require(rhino::forge::fxKnobLive(filter, knob), "RES is the only knob a filter model takes away");
            requireText(rhino::forge::fxKnobLabel(filter, 3), model == 6 ? "PEAK" : "FAT",
                        "the filter's fourth knob is captioned for what it does in this model");
        }

        // Every other type leaves every knob alone, so a rule added by accident
        // to one of them is caught rather than merely unnoticed.
        for (const auto type : {FxType::reverb, FxType::delay, FxType::chorus, FxType::phaser, FxType::flanger})
        {
            rhino::forge::FxSlot other;
            other.type = static_cast<float>(type);
            for (const auto mode : {0.0f, 0.5f, 1.0f})
            {
                other.modeA = mode;
                other.modeB = mode;
                for (int knob = 0; knob < rhino::forge::fxKnobCount; ++knob)
                    require(rhino::forge::fxKnobLive(other, knob),
                            "a type with no such rule leaves all of its knobs live");
            }
        }
    }

    // --- The pictures they are drawn in -----------------------------------------
    //
    // Every type's strip has a picture, at the size the panel opens at; where
    // they stand is the type's own and is swept in the layout tests.
    {
        const auto bounds = juce::Rectangle<int>(0, 0, rhino::forge::ui::defaultPanelWidth,
                                                 rhino::forge::ui::defaultPanelHeight);
        const auto strip = juce::Rectangle<int>(0, 0, 1174, rhino::forge::ui::fxSlotHeight);
        for (int type = 1; type < rhino::forge::fxTypeCount; ++type)
        {
            const auto layout = rhino::forge::ui::fxStripLayout(type, strip,
                                                                rhino::forge::ui::fxKnobSizesFor(bounds));
            require(layout.wellCount >= 1, "every type's strip draws a picture");
            for (int well = 0; well < layout.wellCount; ++well)
                require(layout.wells[static_cast<size_t>(well)].area.getWidth() > 40
                            && layout.wells[static_cast<size_t>(well)].area.getHeight() > 20,
                        "a picture is big enough to draw a curve in");
        }
    }
}
}

void fxTests()
{
    fxSuite();
    fxDisplaySuite();
}
}
