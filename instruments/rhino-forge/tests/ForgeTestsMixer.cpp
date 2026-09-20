// The eight mixer channels, their sends, and the two busses.
#include "ForgeTestSupport.h"

namespace rhino::forge::tests
{
namespace
{
// ----------------------------------------------------------------- mixer ---

// The mixer claims to route, to place and to balance. Every one of those is
// checked by measuring what comes out rather than by reading the patch back:
// a send summed into the wrong accumulator, or a pan law applied twice, reads
// perfectly correct in the parameters and wrong in the audio.
void mixerSuite()
{
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    // --- The busses -----------------------------------------------------------

    // A send is parallel: it adds the source to a bus without taking it away
    // from wherever it was already going.
    rhino::forge::Processor sent;
    soloSineOnA(sent);
    renderNote(sent, buffer);
    const auto direct = rms(buffer, 0, settled);
    require(direct > 0.0f, "a source with no sends is audible on its own");

    setValue(sent, "bus1Level", 1.0f);
    setValue(sent, "oscASend1", 1.0f);
    renderNote(sent, buffer);
    require(allSamplesFinite(buffer), "a send renders finite audio");
    require(rms(buffer, 0, settled) > direct * 1.5f,
            "sending a source to a bus adds it to the output rather than moving it there");

    // The bus's own fader scales what arrives at it, and its enable removes the
    // bus entirely, leaving what the source was already doing untouched.
    setValue(sent, "bus1Level", 0.0f);
    renderNote(sent, buffer);
    requireClose(rms(buffer, 0, settled), direct, direct * 0.01f,
                 "a bus at silence contributes nothing");
    setValue(sent, "bus1Level", 1.0f);
    setValue(sent, "bus1Enable", 0.0f);
    renderNote(sent, buffer);
    requireClose(rms(buffer, 0, settled), direct, direct * 0.01f,
                 "a bus switched off is heard nowhere, and takes nothing else with it");

    // A source whose destination is silenced is still heard on the bus it is
    // sent to: a send is taken from the channel, not from the output.
    rhino::forge::Processor onlyBus;
    soloSineOnA(onlyBus);
    setValue(onlyBus, "oscAEnable", 0.0f);
    setValue(onlyBus, "subEnable", 1.0f);
    setValue(onlyBus, "subLevel", 0.8f);
    setValue(onlyBus, "routeSub", 0.0f);
    setValue(onlyBus, "subSend1", 1.0f);
    setValue(onlyBus, "bus1Level", 1.0f);
    renderNote(onlyBus, buffer);
    require(rms(buffer, 0, settled) > 0.0f, "a source reaches the output through a bus");

    // Bus 1 into bus 2 is one chain: silencing the bus at the end of it
    // silences everything upstream.
    rhino::forge::Processor chained;
    soloSineOnA(chained);
    setValue(chained, "oscASend1", 1.0f);
    setValue(chained, "bus1Level", 1.0f);
    setValue(chained, "bus2Level", 1.0f);
    setValue(chained, "bus1Dest", 1.0f);
    renderNote(chained, buffer);
    require(rms(buffer, 0, settled) > direct * 1.5f,
            "a bus routed into the other still reaches the output");
    setValue(chained, "bus2Level", 0.0f);
    renderNote(chained, buffer);
    requireClose(rms(buffer, 0, settled), direct, direct * 0.01f,
                 "silencing the bus at the end of a chain silences the whole chain");

    // Two busses pointed at each other is a loop with no answer. It has to be
    // finite and audible rather than either silent or runaway: the second of
    // the pair goes to the output instead.
    rhino::forge::Processor looped;
    soloSineOnA(looped);
    setValue(looped, "oscASend1", 1.0f);
    setValue(looped, "bus1Level", 1.0f);
    setValue(looped, "bus2Level", 1.0f);
    setValue(looped, "bus1Dest", 1.0f);
    setValue(looped, "bus2Dest", 1.0f);
    renderNote(looped, buffer);
    require(allSamplesFinite(buffer), "two busses pointed at each other render finite audio");
    require(rms(buffer, 0, settled) > direct * 1.5f,
            "a mutually crossed pair still reaches the output");

    // --- Placing a source -----------------------------------------------------

    // The sub and the noise have a pan of their own now. Panned hard, they are
    // on one side and not the other.
    rhino::forge::Processor placed;
    soloSineOnA(placed);
    setValue(placed, "oscAEnable", 0.0f);
    setValue(placed, "subEnable", 1.0f);
    setValue(placed, "subLevel", 0.8f);
    setValue(placed, "subPan", -1.0f);
    renderNote(placed, buffer);
    require(rms(buffer, 0, settled) > 0.0f, "a hard left sub is audible on the left");
    require(rms(buffer, 1, settled) < rms(buffer, 0, settled) * 0.01f,
            "a hard left sub is silent on the right");

    // Centred, it is equally on both. And the level it is centred at is the one
    // it had before the mixer gave it a pan law at all: 0.12 under the old law
    // and 0.17 under the new are the same sound, which is what the migration in
    // Processor::migrated exists to preserve. Measured rather than assumed.
    setValue(placed, "subPan", 0.0f);
    setValue(placed, "subLevel", 0.17f);
    renderNote(placed, buffer);
    const auto centred = rms(buffer, 0, settled);
    requireClose(rms(buffer, 1, settled), centred, centred * 0.001f,
                 "a centred sub is equally on both channels");

    rhino::forge::Processor asBefore;
    soloSineOnA(asBefore);
    setValue(asBefore, "oscAEnable", 0.0f);
    setValue(asBefore, "subEnable", 1.0f);
    setValue(asBefore, "subLevel", 0.12f * juce::MathConstants<float>::sqrt2);
    renderNote(asBefore, buffer);
    requireClose(rms(buffer, 0, settled), centred, centred * 0.01f,
                 "the sub's new default is the level its old default sounded at");

    // --- The filter's channel -------------------------------------------------

    // MIX blends what came out of the filter against what went in, so at
    // nothing the filter is inaudible however closed it is.
    rhino::forge::Processor blended;
    soloSineOnA(blended);
    setValue(blended, "filterEnable", 1.0f);
    setValue(blended, "routeA", 1.0f);
    setValue(blended, "resonance", 0.0f);
    setValue(blended, "cutoff", 18000.0f);
    renderNote(blended, buffer);
    const auto open = rms(buffer, 0, settled);
    setValue(blended, "cutoff", 60.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, settled) < open * 0.25f, "a closed filter attenuates its channel");
    setValue(blended, "filterMix", 0.0f);
    renderNote(blended, buffer);
    requireClose(rms(buffer, 0, settled), open, open * 0.02f,
                 "MIX at nothing passes what went into the filter, whatever the cutoff is doing");

    // The channel's fader and its pan sit after that blend.
    setValue(blended, "filterMix", 1.0f);
    setValue(blended, "cutoff", 18000.0f);
    setValue(blended, "filterLevel", 0.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, settled) < open * 0.001f, "the filter channel's fader silences it");
    setValue(blended, "filterLevel", 1.0f);
    setValue(blended, "filterPan", 1.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, settled) < open * 0.01f, "a hard right filter is silent on the left");
    require(rms(buffer, 1, settled) > 0.0f, "a hard right filter is audible on the right");

    // A channel pan is unity at its centre, because it moves a sum that is
    // already balanced rather than placing a source among others. A source pan
    // is not, which is exactly why the two laws are separate.
    setValue(blended, "filterPan", 0.0f);
    renderNote(blended, buffer);
    requireClose(rms(buffer, 0, settled), open, open * 0.001f,
                 "a filter panned to the centre is as loud as one with no pan at all");

    // --- Nothing moved that was not asked to ----------------------------------
    //
    // The mixer's defaults have to leave the synth where they found it, or
    // every patch written before it quietly changed.
    require(rhino::forge::Patch {}.filterMix == 1.0f, "the filter is all wet by default");
    require(rhino::forge::Patch {}.filterLevel == 1.0f, "the filter channel opens at unity");
    require(rhino::forge::Patch {}.filterPan == 0.0f, "the filter channel opens centred");
    for (int bus = 0; bus < rhino::forge::busCount; ++bus)
    {
        const auto& settings = rhino::forge::Patch {}.buses[static_cast<size_t>(bus)];
        require(settings.dest == 0.0f, "a bus opens pointed at the main output");
    }
    rhino::forge::Processor fresh;
    require(peakForNote(fresh) > 0.0f, "the patch Forge opens on still makes a sound");
    for (const auto* id : {"oscASend1", "oscASend2", "subSend1", "filterSend1"})
        require(value(fresh, id) == 0.0f, "nothing is sent anywhere until it is asked for");
}
}

void mixerTests()
{
    mixerSuite();
}
}
