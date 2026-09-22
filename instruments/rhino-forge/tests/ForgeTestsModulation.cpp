// The matrix: a slot reaching a destination, depths summing, bipolar sources,
// and the macros.
#include "ForgeTestSupport.h"

namespace rhino::forge::tests
{
namespace
{
void modulationSuite()
{
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> plain(2, samples), modulated(2, samples);

    // A slot at zero depth must cost nothing at all, not merely almost nothing:
    // an idle matrix may not colour the sound.
    auto bareOwner = std::make_unique<rhino::forge::Processor>();
    auto& bare = *bareOwner;
    closedFilterOnA(bare);
    renderNote(bare, plain);

    auto wiredOwner = std::make_unique<rhino::forge::Processor>();
    auto& wired = *wiredOwner;
    closedFilterOnA(wired);
    setSlot(wired, 1, srcLfo1, destCutoff, 0.0f);
    renderNote(wired, modulated);
    require(identical(plain, modulated), "a slot at zero depth renders bit-identically to no slot");

    // Pointed at nothing, a slot with depth is equally inert.
    auto unpointedOwner = std::make_unique<rhino::forge::Processor>();
    auto& unpointed = *unpointedOwner;
    closedFilterOnA(unpointed);
    setSlot(unpointed, 1, srcLfo1, destOff, 1.0f);
    renderNote(unpointed, modulated);
    require(identical(plain, modulated), "a slot with no destination renders bit-identically");

    // With depth, the envelope opens the filter and more gets through.
    auto sweptOwner = std::make_unique<rhino::forge::Processor>();
    auto& swept = *sweptOwner;
    closedFilterOnA(swept);
    setSlot(swept, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(swept, modulated);
    const auto closed = rms(plain, 0, settled);
    const auto opened = rms(modulated, 0, settled);
    require(opened > closed * 1.2f, "an envelope pointed at the cutoff opens the filter");

    // Two slots on one destination sum, rather than one winning.
    auto halvesOwner = std::make_unique<rhino::forge::Processor>();
    auto& halves = *halvesOwner;
    closedFilterOnA(halves);
    setSlot(halves, 1, srcEnv1, destCutoff, 0.5f);
    setSlot(halves, 2, srcEnv1, destCutoff, 0.5f);
    juce::AudioBuffer<float> summed(2, samples);
    renderNote(halves, summed);
    require(identical(modulated, summed), "two half-depth slots sum to one full-depth slot");

    // Switching a slot's source off restores the unmodulated render exactly.
    setSlot(swept, 1, srcOff, destCutoff, 1.0f);
    renderNote(swept, modulated);
    require(identical(plain, modulated), "switching a slot's source off restores the plain render");

    // What the knobs draw is published from the same reading the voice renders
    // with, so a ring that moves is proof the engine moved the value, not a
    // second guess at it from the UI.
    auto watchedOwner = std::make_unique<rhino::forge::Processor>();
    auto& watched = *watchedOwner;
    closedFilterOnA(watched);
    require(watched.modulationOffset(destCutoff) == 0.0f,
            "an idle matrix publishes no offset for a knob to draw");

    setSlot(watched, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(watched, modulated);
    const auto published = watched.modulationOffset(destCutoff);
    require(published > 0.0f, "an envelope pointed at the cutoff publishes an offset to draw");
    require(published <= 1.0f, "a unipolar source at full depth cannot publish past full travel");
    // At full depth from ENV 1 the offset is the envelope itself, so the ring on
    // the cutoff knob and the curve on ENV 1's display cannot disagree.
    require(published == watched.envelopeLevel(),
            "the offset drawn on a knob is the same reading ENV 1's own display draws");
    require(watched.modulationOffset(destSub) == 0.0f,
            "a destination nothing points at publishes nothing");

    // Pointing the same slot somewhere else has to release the knob it left.
    setSlot(watched, 1, srcEnv1, destSub, 1.0f);
    renderNote(watched, modulated);
    require(watched.modulationOffset(destCutoff) == 0.0f,
            "a destination a slot has left goes back to publishing nothing");

    // A source only reaches a destination through a voice, so with nothing
    // sounding there is no modulated value and the knobs have nothing to
    // animate. This holds for a macro too, which is the case that looks most
    // like it ought to be an exception: the hand is on the macro, but until a
    // note is played the macro is moving nothing.
    auto idleOwner = std::make_unique<rhino::forge::Processor>();
    auto& idle = *idleOwner;
    closedFilterOnA(idle);
    setSlot(idle, 1, static_cast<float>(rhino::forge::ModSource::macro1), destCutoff, 1.0f);
    setValue(idle, "macro1", 0.5f);
    idle.prepareToPlay(48000.0, samples);
    juce::AudioBuffer<float> silence(2, samples);
    juce::MidiBuffer noNotes;
    silence.clear();
    idle.processBlock(silence, noNotes);
    require(idle.modulationOffset(destCutoff) == 0.0f,
            "nothing sounding publishes no offset, so the rings stay still");

    // And the same patch under a note does publish, so the check above is
    // measuring silence rather than a routing that was never live.
    renderNote(idle, modulated);
    requireClose(idle.modulationOffset(destCutoff), 0.5f, 0.001f,
                 "the same macro publishes its offset once a note is sounding");


    // --- Polarity -------------------------------------------------------------
    //
    // BI centres a source that only rises, so the destination's own setting
    // becomes the middle of the reach rather than the bottom of it. A macro at
    // rest then pulls the destination as far negative as the depth goes, where
    // UNI leaves it alone.
    //
    // This is checked on the published offset rather than on a render, because
    // the claim is about the arithmetic and not about what a filter does with
    // it. A Serum patch copied knob for knob came out an octave wrong on its
    // pitch destination for exactly the want of this switch, which is why the
    // whole travel is pinned here rather than only that it does something.
    {
        const auto offsetFor = [samples] (float source, float macro, bool bipolar)
        {
            auto probeOwner = std::make_unique<rhino::forge::Processor>();
            auto& probe = *probeOwner;
            closedFilterOnA(probe);
            setSlot(probe, 1, source, static_cast<float>(destCutoff), 1.0f);
            setValue(probe, "mod1Bipolar", bipolar ? 1.0f : 0.0f);
            setValue(probe, "macro1", macro);
            juce::AudioBuffer<float> rendered(2, samples);
            renderNote(probe, rendered);
            return probe.modulationOffset(destCutoff);
        };
        constexpr auto macro1 = static_cast<float>(rhino::forge::ModSource::macro1);

        requireClose(offsetFor(macro1, 0.0f, false), 0.0f, 0.001f,
                     "UNI leaves a source at rest moving nothing");
        requireClose(offsetFor(macro1, 0.5f, false), 0.5f, 0.001f,
                     "UNI reaches half the depth from half the source");
        requireClose(offsetFor(macro1, 1.0f, false), 1.0f, 0.001f,
                     "UNI reaches the whole depth from the top of the source");

        requireClose(offsetFor(macro1, 0.0f, true), -0.5f, 0.001f,
                     "BI at rest pulls as far negative as the depth reaches");
        requireClose(offsetFor(macro1, 0.5f, true), 0.0f, 0.001f,
                     "BI at half leaves the destination where it was set");
        requireClose(offsetFor(macro1, 1.0f, true), 0.5f, 0.001f,
                     "BI at the top reaches as far positive, the reach recentred not resized");
    }

    // An LFO already swings both ways, so the switch has nothing to centre and
    // must not quietly double its reach instead.
    {
        auto uniOwner = std::make_unique<rhino::forge::Processor>();
        auto biOwner = std::make_unique<rhino::forge::Processor>();
        auto& uni = *uniOwner;
        auto& bi = *biOwner;
        closedFilterOnA(uni);
        closedFilterOnA(bi);
        setSlot(uni, 1, srcLfo1, destCutoff, 1.0f);
        setSlot(bi, 1, srcLfo1, destCutoff, 1.0f);
        setValue(bi, "mod1Bipolar", 1.0f);
        juce::AudioBuffer<float> unipolar(2, samples), bipolar(2, samples);
        renderNote(uni, unipolar);
        renderNote(bi, bipolar);
        require(identical(unipolar, bipolar),
                "BI leaves an LFO alone, which already swings both ways");
    }

    // Off to begin with, so every preset written before the switch existed
    // still modulates exactly as it did.
    require(value(bare, "mod1Bipolar") == 0.0f, "a slot starts unipolar");

    // Depth clamps at the destination's own limits instead of running past them.
    auto slammedOwner = std::make_unique<rhino::forge::Processor>();
    auto& slammed = *slammedOwner;
    closedFilterOnA(slammed);
    setValue(slammed, "cutoff", 18000.0f);
    setSlot(slammed, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(slammed, modulated);
    require(allSamplesFinite(modulated), "modulation past a parameter's top stays finite");
    auto atTopOwner = std::make_unique<rhino::forge::Processor>();
    auto& atTop = *atTopOwner;
    closedFilterOnA(atTop);
    setValue(atTop, "cutoff", 18000.0f);
    renderNote(atTop, plain);
    require(identical(plain, modulated),
            "modulating a parameter already at its maximum changes nothing");

    // Velocity is a source like any other, and a softer note modulates less.
    const auto atVelocity = [&] (float velocity)
    {
        auto processorOwner = std::make_unique<rhino::forge::Processor>();
        auto& processor = *processorOwner;
        closedFilterOnA(processor);
        setSlot(processor, 1, srcVelocity, destCutoff, 1.0f);
        processor.prepareToPlay(48000.0, samples);
        juce::AudioBuffer<float> buffer(2, samples);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, velocity), 0);
        processor.processBlock(buffer, midi);
        // Measured as brightness rather than level, so this cannot be
        // satisfied merely by a hard note being louder than a soft one.
        return brightness(buffer, 0, settled);
    };
    require(atVelocity(1.0f) > atVelocity(0.25f) * 1.1f,
            "a harder note opens a velocity-driven filter further");

    // Pitch is reachable now that Semitone is continuous, which is what
    // replaced the old hardwired LFO-to-pitch knob.
    auto bentOwner = std::make_unique<rhino::forge::Processor>();
    auto& bent = *bentOwner;
    soloSineOnA(bent);
    for (int slot = 1; slot <= rhino::forge::modSlotCount; ++slot)
        setSlot(bent, slot, srcOff, destOff, 0.0f);
    renderNote(bent, plain);
    const auto atPitch = zeroCrossings(plain, 0, settled);
    setSlot(bent, 1, srcEnv1, destAPitch, 1.0f);
    renderNote(bent, modulated);
    require(zeroCrossings(modulated, 0, settled) > atPitch,
            "an envelope pointed at pitch raises the note");

    // Macros are sources like any other, and reach their target only through
    // the matrix.
    const auto firstMacro = static_cast<float>(rhino::forge::ModSource::macro1);
    require(rhino::forge::modSourceCount == static_cast<int>(rhino::forge::ModSource::macro1)
                + rhino::forge::macroCount + 1,
            "every macro and the appended modulation wheel are offered as sources");

    auto byMacroOwner = std::make_unique<rhino::forge::Processor>();
    auto& byMacro = *byMacroOwner;
    closedFilterOnA(byMacro);
    setValue(byMacro, "macro1", 1.0f);
    renderNote(byMacro, plain);
    require(rms(plain, 0, settled) > 0.0f, "a macro alone changes nothing until it is routed");

    setSlot(byMacro, 1, firstMacro, destCutoff, 1.0f);
    renderNote(byMacro, modulated);
    require(brightness(modulated, 0, settled) > brightness(plain, 0, settled) * 1.1f,
            "a macro turned up opens the filter it is pointed at");

    setValue(byMacro, "macro1", 0.0f);
    renderNote(byMacro, modulated);
    require(identical(plain, modulated), "a macro at zero leaves its target exactly where it was");

    // And a source that never moves still behaves: NOTE is constant per voice.
    auto byNoteOwner = std::make_unique<rhino::forge::Processor>();
    auto& byNote = *byNoteOwner;
    closedFilterOnA(byNote);
    setValue(byNote, "subEnable", 1.0f);
    setValue(byNote, "subLevel", 0.0f);
    renderNote(byNote, plain, 36);
    setSlot(byNote, 1, srcNote, destSub, 1.0f);
    renderNote(byNote, modulated, 36);
    require(!identical(plain, modulated), "the note source reaches its destination");
    require(rms(modulated, 0, settled) > rms(plain, 0, settled),
            "note-driven modulation adds the sub it was pointed at");

    // A panel gesture and a host CC1 message must drive the same matrix source.
    auto modWheelOwner = std::make_unique<rhino::forge::Processor>();
    auto& modWheel = *modWheelOwner;
    closedFilterOnA(modWheel);
    setSlot(modWheel, 1, static_cast<float>(ModSource::modWheel), destCutoff, 1.0f);
    renderNote(modWheel, plain);
    const auto atRest = rms(plain, 0, settled);
    modWheel.setModWheel(127);
    renderNote(modWheel, modulated);
    require(rms(modulated, 0, settled) > atRest * 1.2f,
            "the modulation wheel opens its matrix destination");
    requireClose(modWheel.modulationOffset(destCutoff), 1.0f, 0.001f,
                 "the matrix meter follows the modulation wheel");
    modWheel.prepareToPlay(48000.0, samples);
    juce::MidiBuffer cc;
    cc.addEvent(juce::MidiMessage::controllerEvent(1, 1, 37), 0);
    cc.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    modWheel.processBlock(modulated, cc);
    require(modWheel.modWheelValue() == 37, "host CC1 updates the panel wheel");

    // Bend is heard on an already specified note, across the whole voice.
    auto pitchOwner = std::make_unique<rhino::forge::Processor>();
    auto& pitch = *pitchOwner;
    soloSineOnA(pitch);
    renderNote(pitch, plain);
    const auto straightCrossings = zeroCrossings(plain, 0, settled);
    pitch.setPitchWheel(16383);
    renderNote(pitch, modulated);
    requireClose(static_cast<float>(zeroCrossings(modulated, 0, settled)),
                 straightCrossings * std::pow(2.0f, 2.0f / 12.0f),
                 straightCrossings * 0.04f, "the pitch wheel bends by two semitones");
    pitch.prepareToPlay(48000.0, samples);
    juce::MidiBuffer bend;
    bend.addEvent(juce::MidiMessage::pitchWheel(1, 0), 0);
    bend.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    pitch.processBlock(modulated, bend);
    require(pitch.pitchWheelValue() == 0, "host pitch bend updates the panel wheel");
    require(zeroCrossings(modulated, 0, settled) < straightCrossings,
            "host pitch bend lowers the rendered note");
}
}

void modulationTests()
{
    modulationSuite();
}
}
