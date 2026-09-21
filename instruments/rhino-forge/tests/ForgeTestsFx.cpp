// The three effects racks: what each type does to the signal, what a slot draws
// of itself, and that the two are computed from the same functions.
#include "ForgeTestSupport.h"
#include "../core/ForgeFxDsp.h"
#include "../ui/ForgeFxDisplay.h"
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
}

void fxDisplaySuite()
{
    namespace ui = rhino::forge::ui;
    using rhino::forge::FxType;

    // Every active strip keeps its common controls. A type with only three
    // named knobs still needs MIX, LEVEL and BYP; hiding unused general knobs
    // must not make the common tail of the row disappear with them.
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

        auto mixes = 0, bypasses = 0;
        const auto countControls = [&] (auto&& self, juce::Component& parent) -> void
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* label = dynamic_cast<juce::Label*>(child))
                    if (label->isVisible() && label->getText() == "MIX") ++mixes;
                if (auto* chip = dynamic_cast<rhino::forge::ui::ToggleChip*>(child))
                    if (chip->isVisible() && chip->getButtonText() == "BYP") ++bypasses;
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
        require(bypasses == visible,
                "every fully or partly visible effect strip keeps its common bypass control");
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
        for (const auto& module : rhino::forge::ui::modules())
        {
            const auto area = rhino::forge::ui::moduleBounds(tightest, module);
            for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            {
                const auto& controls = module.rows[static_cast<size_t>(r)].controls;
                for (int c = 0; c < static_cast<int>(controls.size()); ++c)
                {
                    if (controls[static_cast<size_t>(c)].style != rhino::forge::ui::Style::selector)
                        continue;
                    const auto block = rhino::forge::ui::controlBlock(area, module, r, c, shared);
                    const auto field = block.withTrimmedTop(rhino::forge::ui::stepperLabelHeight);

                    rhino::forge::ui::FxSelector selector;
                    selector.setBounds(field);
                    // A field's geometry follows how many choices it is holding,
                    // so it is given some. Nothing here draws them.
                    const auto holding = [&selector] (int howMany)
                    {
                        selector.choices.assign(static_cast<size_t>(howMany), "X");
                    };
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

        // Every other type leaves every knob alone, so a rule added by accident
        // to one of them is caught rather than merely unnoticed.
        for (const auto type : {FxType::reverb, FxType::delay, FxType::chorus, FxType::filter,
                                FxType::phaser})
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

    // --- The strip they are drawn in -------------------------------------------
    //
    // Every rack row reserves one, and nothing else on the panel does. A module
    // that grew a display strip without meaning to would be caught here.
    {
        const auto bounds = juce::Rectangle<int>(0, 0, rhino::forge::ui::defaultPanelWidth,
                                                 rhino::forge::ui::defaultPanelHeight);
        auto strips = 0;
        for (const auto& module : rhino::forge::ui::modules())
        {
            const auto area = rhino::forge::ui::moduleBounds(bounds, module);
            for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            {
                const auto strip = rhino::forge::ui::rowDisplayBounds(area, module, r);
                if (strip.isEmpty()) continue;
                ++strips;
                require(strip.getWidth() > 40 && strip.getHeight() > 20,
                        "a display strip is big enough to draw a curve in");
                require(juce::String(module.id) == "fx",
                        "only the rack reserves a strip of a row for a display");
            }
        }
        require(strips == rhino::forge::fxSlotCount,
                "every slot of the rack has a display strip and no row has two");
    }
}
}

void fxTests()
{
    fxSuite();
    fxDisplaySuite();
}
}
