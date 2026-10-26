#include "DuckSuites.h"
#include "DuckTestSupport.h"

// The engine, measured on rendered audio: the clock in each trigger mode,
// the mix, the band split, the trigger latency and the anti-click fade.
namespace rhino::duck::tests
{
namespace
{
constexpr int cycle = 24000;   // one quarter note at 120 bpm and 48 kHz

struct RenderOptions
{
    int block = 512;
    HostClock host { true, 120.0, 0.0 };
    std::vector<float> side;
    std::vector<int> midi;
};

// Renders `input` on both channels through a fresh engine in blocks, the
// host's ppq advancing from the options' start while it plays, and returns
// the left channel.
std::vector<float> render(Engine& engine, const Settings& settings, const std::vector<float>& input, const RenderOptions& options)
{
    const auto frames = static_cast<int>(input.size());
    std::vector<float> left(input), right(input);
    std::vector<int> frames_;
    for (int start = 0; start < frames; start += options.block)
    {
        const auto count = std::min(options.block, frames - start);
        float* main[2] { left.data() + start, right.data() + start };
        const float* side[1] { options.side.empty() ? nullptr : options.side.data() + start };
        frames_.clear();
        for (const auto at : options.midi)
            if (at >= start && at < start + count) frames_.push_back(at - start);
        auto host = options.host;
        if (host.playing) host.ppq += start / testRate * host.bpm / 60.0;
        engine.process(main, 2, side, options.side.empty() ? 0 : 1, count, settings, host, frames_.data(),
                       static_cast<int>(frames_.size()));
    }
    return left;
}

float largestStep(const std::vector<float>& signal, int from = 1)
{
    auto largest = 0.0f;
    for (size_t i = static_cast<size_t>(std::max(1, from)); i < signal.size(); ++i)
        largest = std::max(largest, std::abs(signal[i] - signal[i - 1]));
    return largest;
}

void syncFollowsTheHost()
{
    Engine engine;
    engine.prepare(testRate, 4096);
    Settings settings;
    const auto out = render(engine, settings, constant(cycle * 2), {});
    auto worst = 0.0f;
    for (int i = 100; i < cycle * 2; ++i)
    {
        const auto phase = static_cast<float>(i % cycle) / cycle;
        if (i % cycle < 100) continue;   // the cut takes a millisecond
        worst = std::max(worst, std::abs(out[static_cast<size_t>(i)] - curveGain(0, phase, {})));
    }
    require(worst < 0.03f, "in Sync the output follows the curve at the host's phase");
    require(out[static_cast<size_t>(cycle + 200)] < 0.02f, "and ducks again on the next beat");
    require(engine.isArmed() && engine.sweepPhase() >= 0.0f && engine.sweepPhase() < 1.0f, "the sweep reports a phase");

    // Starting playback mid-song lands on the beat's own phase.
    Engine late;
    late.prepare(testRate, 4096);
    RenderOptions options;
    options.host.ppq = 0.5;   // half a beat in: Classic is half open there
    const auto fromHalf = render(late, settings, constant(4000), options);
    requireClose(fromHalf[3000], curveGain(0, 0.5f + 3000.0f / cycle, {}), 0.03f, "playback from half a beat reads the curve there");
}

void mixScalesTheDuck()
{
    juce::Random random(11);
    std::vector<float> noise(cycle);
    for (auto& sample : noise) sample = random.nextFloat() * 2.0f - 1.0f;
    Engine engine;
    engine.prepare(testRate, 4096);
    Settings settings;
    settings.mix = 0.0f;
    const auto dry = render(engine, settings, noise, {});
    require(dry == noise, "at Mix 0 the signal passes untouched");

    Engine half;
    half.prepare(testRate, 4096);
    settings.mix = 0.5f;
    const auto halfway = render(half, settings, constant(cycle), {});
    requireClose(halfway[720], 0.5f, 0.02f, "at Mix 50% the duck goes half way down");
}

void blocksOfAnySizeAgree()
{
    juce::Random random(5);
    std::vector<float> noise(cycle);
    for (auto& sample : noise) sample = random.nextFloat() * 2.0f - 1.0f;
    Settings settings;
    settings.curve = 2;
    settings.band = true;
    settings.cutoffHz = 150.0f;
    settings.mix = 0.8f;
    Engine reference;
    reference.prepare(testRate, 4096);
    const auto expected = render(reference, settings, noise, {});
    for (const auto block : { 64, 1000, 4096 })
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        RenderOptions options;
        options.block = block;
        const auto other = render(engine, settings, noise, options);
        auto worst = 0.0f;
        for (size_t i = 0; i < expected.size(); ++i) worst = std::max(worst, std::abs(expected[i] - other[i]));
        require(worst < 1.0e-5f, "blocks of any size render the same audio in Sync");
    }

    // And with the Audio trigger, whose restarts are scheduled by frame.
    settings.trigger = 2;
    settings.band = false;
    std::vector<float> side(cycle, 0.0f);
    addKick(side, 1000);
    addKick(side, 14000);
    RenderOptions withSide;
    withSide.side = side;
    Engine triggered;
    triggered.prepare(testRate, 4096);
    const auto expectedTriggered = render(triggered, settings, noise, withSide);
    for (const auto block : { 64, 1000 })
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        withSide.block = block;
        const auto other = render(engine, settings, noise, withSide);
        auto worst = 0.0f;
        for (size_t i = 0; i < expectedTriggered.size(); ++i) worst = std::max(worst, std::abs(expectedTriggered[i] - other[i]));
        require(worst < 1.0e-5f, "blocks of any size render the same audio with the Audio trigger");
    }
}

void bandSplitSumsFlat()
{
    for (const auto hz : { 60.0, 200.0, 1000.0, 5000.0 })
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        Settings settings;
        settings.mix = 0.0f;
        settings.band = true;
        settings.cutoffHz = 200.0f;
        const auto input = sine(cycle, hz);
        const auto out = render(engine, settings, input, {});
        const auto gainDb = decibels(rmsOf(out, 8000, cycle) / rmsOf(input, 8000, cycle));
        require(std::abs(gainDb) < 0.3f, "the two bands sum back to the signal, within a third of a decibel");
    }
}

// One-shot on Trim: after a third of a cycle the gain is 0 and stays there,
// so what still comes through is what the band split leaves alone.
void bandSplitDucksOnlyTheLows()
{
    Settings settings;
    settings.trigger = 1;
    settings.loop = false;
    settings.curve = 10;
    settings.antiClick = false;
    settings.band = true;
    settings.cutoffHz = 200.0f;
    RenderOptions options;
    options.midi = { 0 };
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        const auto input = sine(cycle * 2, 2000.0);
        const auto out = render(engine, settings, input, options);
        const auto gainDb = decibels(rmsOf(out, cycle + 4000, cycle * 2) / rmsOf(input, cycle + 4000, cycle * 2));
        require(std::abs(gainDb) < 0.5f, "a 2 kHz tone passes a 200 Hz band split unducked");
    }
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        const auto input = sine(cycle * 2, 60.0);
        const auto out = render(engine, settings, input, options);
        const auto gainDb = decibels(rmsOf(out, cycle + 4000, cycle * 2) / rmsOf(input, cycle + 4000, cycle * 2));
        require(gainDb < -30.0f, "a 60 Hz tone under it is ducked away");
    }
    // Solo keeps only the band, whatever the duck is doing.
    settings.mix = 0.0f;
    settings.bandSolo = true;
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        const auto input = sine(cycle, 2000.0);
        const auto out = render(engine, settings, input, options);
        require(decibels(rmsOf(out, 8000, cycle) / rmsOf(input, 8000, cycle)) < -30.0f, "Solo removes what lies above the cutoff");
    }
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        const auto input = sine(cycle, 60.0);
        const auto out = render(engine, settings, input, options);
        require(std::abs(decibels(rmsOf(out, 8000, cycle) / rmsOf(input, 8000, cycle))) < 0.5f, "and keeps what lies below it");
    }
}

void audioTriggerRestartsOnTheKick()
{
    Engine engine;
    engine.prepare(testRate, 4096);
    Settings settings;
    settings.trigger = 2;
    settings.antiClick = false;
    const auto latency = latencySamples(Trigger::audio, false, testRate);
    require(latency == 480, "the Audio trigger looks 10 ms ahead");
    RenderOptions options;
    options.side.assign(cycle * 3, 0.0f);
    addKick(options.side, 10000);
    addKick(options.side, 30000);
    const auto out = render(engine, settings, constant(cycle * 3), options);
    require(meanOf(out, 2000, 9000) > 0.98f, "before any kick the signal passes unducked");
    require(meanOf(out, 10000, 10380) > 0.97f, "and still does until the kick's own sample leaves the delay");
    require(meanOf(out, 10000 + latency + 70, 10000 + latency + 250) < 0.05f, "then the duck lands with the kick");
    // The second kick lands four fifths through the cycle, where the curve
    // is open, and restarts it.
    require(out[static_cast<size_t>(30000)] > 0.9f, "the curve has opened again before the second kick");
    require(meanOf(out, 30000 + latency + 70, 30000 + latency + 250) < 0.05f, "and the second kick restarts it");
}

void midiTriggerRestartsAtItsFrame()
{
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        Settings settings;
        settings.trigger = 1;
        settings.antiClick = false;
        RenderOptions options;
        options.midi = { 5000 };
        const auto out = render(engine, settings, constant(cycle), options);
        require(meanOf(out, 1000, 4999) > 0.999f, "before any note the signal passes unducked");
        require(meanOf(out, 5060, 5300) < 0.02f, "a note cuts the signal on its own frame");
    }
    {
        Engine engine;
        engine.prepare(testRate, 4096);
        Settings settings;
        settings.trigger = 1;
        settings.antiClick = true;
        const auto latency = latencySamples(Trigger::midi, true, testRate);
        require(latency == 288, "anti-click smoothing is 6 ms of latency in MIDI mode");
        RenderOptions options;
        options.midi = { 5000 };
        const auto out = render(engine, settings, constant(cycle), options);
        require(out[4990] > 0.999f, "the fade has not begun before the note");
        requireClose(out[5000 + 144], 0.5f, 0.08f, "half way through the fade the gain is half way down");
        require(meanOf(out, 5000 + latency + 10, 5000 + latency + 200) < 0.02f, "and the cut lands when the note's sample leaves the delay");
    }
}

void oneShotHoldsAtTheEndMarker()
{
    Engine engine;
    engine.prepare(testRate, 4096);
    Settings settings;
    settings.trigger = 1;
    settings.loop = false;
    settings.endMarker = 0.3f;
    settings.antiClick = false;
    RenderOptions options;
    options.midi = { 0 };
    const auto out = render(engine, settings, constant(cycle * 2), options);
    const auto held = curveGain(0, 0.3f, {});
    require(held > 0.3f && held < 0.8f, "the end marker falls on the curve's slope");
    auto worst = 0.0f;
    for (int i = 8000; i < cycle * 2; ++i) worst = std::max(worst, std::abs(out[static_cast<size_t>(i)] - held));
    require(worst < 1.0e-3f, "a one-shot curve freezes at the End marker and stays there");
    requireClose(engine.sweepPhase(), 0.3f, 1.0e-4f, "the sweep rests on the End marker");
}

void antiClickSoftensTheRestart()
{
    Settings settings;
    settings.trigger = 1;
    RenderOptions options;
    options.midi = { 3000, 9000, 15000, 21000 };
    settings.antiClick = true;
    Engine smooth;
    smooth.prepare(testRate, 4096);
    const auto softened = render(smooth, settings, constant(cycle), options);
    // Measured once the delayed signal has arrived: its arrival is the one
    // step that is not a restart.
    require(largestStep(softened, 1000) < 0.004f, "with anti-click on, no restart moves the gain faster than the fade");
    settings.antiClick = false;
    Engine hard;
    hard.prepare(testRate, 4096);
    const auto abrupt = render(hard, settings, constant(cycle), options);
    require(largestStep(abrupt, 1) > 0.015f, "with it off, a restart cuts within a millisecond");
    require(largestStep(abrupt, 1) < 0.025f, "but never in a single sample");
}

void stoppedHostRunsFree()
{
    Engine engine;
    engine.prepare(testRate, 4096);
    Settings settings;
    RenderOptions options;
    options.host.playing = false;
    const auto out = render(engine, settings, constant(cycle * 2), options);
    require(meanOf(out, 100, 1000) < 0.05f && meanOf(out, cycle + 100, cycle + 1000) < 0.05f,
            "with the host stopped the curve runs on at the host's tempo");
    require(out[20000] > 0.95f, "and opens between beats");
}

void scopeRecordsTheCycle()
{
    Engine engine;
    engine.prepare(testRate, 4096);
    Settings settings;
    const auto out = render(engine, settings, constant(cycle * 2), {});
    juce::ignoreUnused(out);
    require(engine.scopeColumn(10).outHigh < 0.05f, "early in the cycle the scope saw the ducked output");
    require(engine.scopeColumn(240).outHigh > 0.95f, "and late in it the open one");
    require(engine.cycleCount() >= 2, "two cycles were counted");
}

void latencyIsReportedPerMode()
{
    require(latencySamples(Trigger::sync, true, testRate) == 0 && latencySamples(Trigger::sync, false, testRate) == 0,
            "Sync has no latency");
    require(latencySamples(Trigger::midi, false, testRate) == 0, "MIDI without smoothing has none");
    require(latencySamples(Trigger::midi, true, testRate) == 288, "MIDI with smoothing has 6 ms");
    require(latencySamples(Trigger::audio, false, testRate) == 480, "Audio without smoothing has 10 ms");
    require(latencySamples(Trigger::audio, true, testRate) == 768, "Audio with smoothing has 16 ms");
    require(latencySamples(Trigger::audio, true, 44100.0) == 706, "and the figure follows the sample rate");
}
}

void engineTests()
{
    syncFollowsTheHost();
    mixScalesTheDuck();
    blocksOfAnySizeAgree();
    bandSplitSumsFlat();
    bandSplitDucksOnlyTheLows();
    audioTriggerRestartsOnTheKick();
    midiTriggerRestartsAtItsFrame();
    oneShotHoldsAtTheEndMarker();
    antiClickSoftensTheRestart();
    stoppedHostRunsFree();
    scopeRecordsTheCycle();
    latencyIsReportedPerMode();
}
}
