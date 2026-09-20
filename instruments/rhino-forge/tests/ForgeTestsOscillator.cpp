// The oscillators, measured: tuning, the unison stack, the ten shapes, the sub,
// and how much of the table survives being band-limited for a high note.
#include "ForgeTestSupport.h"
#include "ForgeTestSpectrum.h"

namespace rhino::forge::tests
{
namespace
{
void oscillatorSuite()
{
    // Tuning is one multiplier built from three controls. Check the arithmetic
    // directly before checking that the voice honours it.
    rhino::forge::Oscillator osc;
    requireClose(rhino::forge::tuningRatio(osc), 1.0f, 0.0001f, "an untuned oscillator plays at pitch");
    osc.octave = 1.0f;
    requireClose(rhino::forge::tuningRatio(osc), 2.0f, 0.0001f, "one octave doubles the frequency");
    osc.octave = 0.0f; osc.semitone = 12.0f;
    requireClose(rhino::forge::tuningRatio(osc), 2.0f, 0.0001f, "twelve semitones doubles the frequency");
    osc.semitone = 0.0f; osc.fine = 100.0f;
    requireClose(rhino::forge::tuningRatio(osc), std::pow(2.0f, 1.0f / 12.0f), 0.0001f,
                 "one hundred cents is one semitone");
    osc.octave = -1.0f; osc.semitone = 12.0f; osc.fine = 0.0f;
    requireClose(rhino::forge::tuningRatio(osc), 1.0f, 0.0001f, "octave and semitone cancel");

    // And the voice honours it: an octave up doubles the zero-crossing rate.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    rhino::forge::Processor atPitch;
    soloSineOnA(atPitch);
    renderNote(atPitch, buffer);
    const auto baseCrossings = zeroCrossings(buffer, 0, settled);
    require(baseCrossings > 0, "a solo sine crosses zero");

    rhino::forge::Processor anOctaveUp;
    soloSineOnA(anOctaveUp);
    setValue(anOctaveUp, "oscAOctave", 1.0f);
    renderNote(anOctaveUp, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 static_cast<float>(baseCrossings * 2), static_cast<float>(baseCrossings) * 0.05f,
                 "an octave up doubles the oscillator's frequency");

    rhino::forge::Processor sevenSemis;
    soloSineOnA(sevenSemis);
    setValue(sevenSemis, "oscASemitone", 7.0f);
    renderNote(sevenSemis, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 baseCrossings * std::pow(2.0f, 7.0f / 12.0f), static_cast<float>(baseCrossings) * 0.05f,
                 "seven semitones is a fifth");

    // Pan law: hard left puts nothing in the right channel.
    rhino::forge::Processor panned;
    soloSineOnA(panned);
    setValue(panned, "oscAPan", -1.0f);
    renderNote(panned, buffer);
    const auto left = rms(buffer, 0, settled);
    const auto right = rms(buffer, 1, settled);
    require(left > 0.0f, "a hard-left oscillator still feeds the left channel");
    require(right < left * 0.01f, "a hard-left oscillator is absent from the right channel");

    // Centred, an equal-power pan puts the same energy in both channels.
    rhino::forge::Processor centred;
    soloSineOnA(centred);
    renderNote(centred, buffer);
    requireClose(rms(buffer, 0, settled), rms(buffer, 1, settled), 0.0001f,
                 "a centred oscillator is equal in both channels");

    // Level scales the oscillator directly, now that it owns one.
    const auto fullLevel = rms(buffer, 0, settled);
    rhino::forge::Processor halfLevel;
    soloSineOnA(halfLevel);
    setValue(halfLevel, "oscALevel", 0.5f);
    renderNote(halfLevel, buffer);
    requireClose(rms(buffer, 0, settled), fullLevel * 0.5f, fullLevel * 0.02f,
                 "halving an oscillator's level halves its output");

    // Widening the stack changes the sound without changing the level. This has
    // to be measured over a window longer than the slowest beat in the stack -
    // at A3 with twelve voices that is nearly five seconds - because a shorter
    // one reports wherever the stack happens to sit rather than its level.
    constexpr int longSamples = 48000 * 13 / 2;
    constexpr int longSettled = 24000;
    juce::AudioBuffer<float> longBuffer(2, longSamples);

    rhino::forge::Processor single;
    soloSineOnA(single);
    setValue(single, "oscADetune", 0.3f);
    setValue(single, "oscAUnison", 1.0f);
    renderNote(single, longBuffer);
    const auto oneVoice = rms(longBuffer, 0, longSettled);
    require(oneVoice > 0.0f, "a single voice makes sound");

    // Every stack size, not just one, so the widest the parameter allows cannot
    // quietly be the loudest.
    for (int count = 2; count <= rhino::forge::unisonMax; ++count)
    {
        rhino::forge::Processor stacked;
        soloSineOnA(stacked);
        setValue(stacked, "oscADetune", 0.3f);
        setValue(stacked, "oscAUnison", static_cast<float>(count));
        renderNote(stacked, longBuffer);
        const auto stackedVoices = rms(longBuffer, 0, longSettled);
        require(stackedVoices > 0.0f, "every stack size makes sound");
        const auto decibels = juce::Decibels::gainToDecibels(stackedVoices / oneVoice);
        require(std::abs(decibels) < 4.0f, "stacking voices does not change the oscillator's level");
        if (std::abs(decibels) >= 4.0f)
            std::cerr << "       unison " << count << " level shift: " << decibels
                      << " dB\n";
    }

    // The parameter stops exactly where the phase arrays do, so the widest
    // stack the panel can ask for is one the voice can actually render.
    requireText(textFor(single, "oscAUnison", 99.0f),
                juce::String(rhino::forge::unisonMax),
                "unison stops at the width the voice can render");

    // A full stack has to chorus, not throb. Evenly spaced members give every
    // neighbouring pair the same beat rate, which makes the whole stack swing
    // together on one slow period; unisonOffset exists to prevent exactly that,
    // and this is the check that it still does.
    rhino::forge::Processor wide;
    soloSineOnA(wide);
    setValue(wide, "oscAPosition", 6.0f / 9.0f);
    setValue(wide, "oscADetune", 0.5f);
    setValue(wide, "oscAUnison", static_cast<float>(rhino::forge::unisonMax));
    renderNote(wide, longBuffer);
    const auto depth = envelopeDepthDb(longBuffer, 0, longSettled);
    require(depth < 12.0f, "a full unison stack choruses rather than throbs");
    if (depth >= 12.0f)
        std::cerr << "       full stack envelope depth: " << depth << " dB\n";

    // The members must be unevenly spaced for that to hold. Checked directly so
    // a failure says which of the two things broke.
    auto smallest = 1.0e9f, largest = 0.0f;
    for (int i = 1; i < rhino::forge::unisonMax; ++i)
    {
        const auto gap = rhino::forge::unisonOffset(i, rhino::forge::unisonMax)
                       - rhino::forge::unisonOffset(i - 1, rhino::forge::unisonMax);
        require(gap > 0.0f, "the stack stays in order");
        smallest = juce::jmin(smallest, gap);
        largest = juce::jmax(largest, gap);
    }
    require(largest > smallest * 2.0f, "the stack is not evenly spaced");
    requireClose(rhino::forge::unisonOffset(rhino::forge::unisonMax - 1, rhino::forge::unisonMax)
                 - rhino::forge::unisonOffset(0, rhino::forge::unisonMax),
                 1.0f, 0.0001f, "the stack still spans exactly what detune asks for");
}

void waveTableSuite()
{
    using rhino::forge::waveShape;
    using rhino::forge::waveShapeCount;

    // Every frame has to be finite, stay inside plus or minus one, and reach
    // full scale. The last of those is what keeps morphing level: if one frame
    // were quiet, sweeping POSITION across it would dip the oscillator.
    for (int shape = 0; shape < waveShapeCount; ++shape)
    {
        auto extreme = 0.0f;
        for (int i = 0; i < 2048; ++i)
        {
            const auto at = waveShape(shape, static_cast<float>(i) / 2048.0f);
            require(std::isfinite(at), "a wavetable frame is finite everywhere");
            extreme = std::max(extreme, std::abs(at));
        }
        if (extreme > 1.0f || extreme < 0.9f)
        {
            require(false, "a wavetable frame fills the range without leaving it");
            std::cerr << "       " << rhino::forge::waveShapeName(shape)
                      << " peaks at " << extreme << '\n';
        }
    }

    // Ten shapes, not one shape ten times. Checked against every other frame
    // rather than only its neighbour, so a duplicate anywhere in the table is
    // caught.
    for (int a = 0; a < waveShapeCount; ++a)
        for (int b = a + 1; b < waveShapeCount; ++b)
        {
            auto apart = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                const auto phase = static_cast<float>(i) / 512.0f;
                apart = std::max(apart, std::abs(waveShape(a, phase) - waveShape(b, phase)));
            }
            if (apart <= 0.05f)
            {
                require(false, "no two wavetable frames are the same shape");
                std::cerr << "       " << rhino::forge::waveShapeName(a) << " and "
                          << rhino::forge::waveShapeName(b) << '\n';
            }
        }

    // Landing on a frame's own position has to give that frame exactly, or the
    // names the knob reads out would be pointing at the wrong thing.
    for (int shape = 0; shape < waveShapeCount; ++shape)
    {
        const auto position = static_cast<float>(shape) / static_cast<float>(waveShapeCount - 1);
        for (int i = 0; i < 128; ++i)
        {
            const auto phase = static_cast<float>(i) / 128.0f;
            requireClose(rhino::forge::waveAt(position, phase), waveShape(shape, phase), 0.0005f,
                         "a position on a frame reads that frame exactly");
        }
        requireText(rhino::forge::waveLabel(position), rhino::forge::waveShapeName(shape),
                    "a position on a frame is named after it");
    }

    // The saw's jump belongs in the middle of its frame, not at the edge of it
    // where nothing can see it. Both halves climb, it crosses zero where the
    // frame begins and ends, and the whole swing happens in one step across the
    // centre. That is the difference between a display that reads as a saw and
    // one that reads as a single diagonal.
    {
        constexpr auto saw = 6;
        constexpr auto step = 1.0f / 2048.0f;
        requireClose(waveShape(saw, 0.0f), 0.0f, 0.001f, "the saw starts its frame at zero");
        requireClose(waveShape(saw, 0.5f - step), 1.0f, 0.005f, "the saw climbs to the top by the centre");
        requireClose(waveShape(saw, 0.5f), -1.0f, 0.001f, "the saw drops to the bottom at the centre");
        requireClose(waveShape(saw, 1.0f - step), 0.0f, 0.005f, "the saw returns to zero by the end");
        require(std::abs(waveShape(saw, 1.0f - step) - waveShape(saw, 0.0f)) < 0.01f,
                "the saw joins up across the cycle boundary");
    }

    // And between two frames it names both, so a blend never masquerades as a
    // shape you could have chosen.
    requireText(rhino::forge::waveLabel(0.5f / 9.0f), "SINE>TRI",
                "a position between two frames names both");

    // Morphing has to be continuous in position: a small move must not jump the
    // output, or modulating POSITION would click.
    for (int i = 1; i < 900; ++i)
    {
        const auto before = static_cast<float>(i) / 900.0f;
        const auto after = before + 0.001f;
        for (int k = 0; k < 32; ++k)
        {
            const auto phase = static_cast<float>(k) / 32.0f;
            const auto step = std::abs(rhino::forge::waveAt(after, phase)
                                       - rhino::forge::waveAt(before, phase));
            if (step > 0.05f)
            {
                require(false, "morphing POSITION moves the wave smoothly");
                std::cerr << "       " << before << " -> " << after
                          << " at phase " << phase << " jumped " << step << '\n';
            }
        }
    }
}

// How much energy a signal carries at a given harmonic of its fundamental. A
// plain correlation rather than a transform: the checks below ask about a
// handful of named harmonics, not a whole spectrum.
float harmonicEnergy(const std::vector<float>& cycle, int harmonic)
{
    auto real = 0.0, imaginary = 0.0;
    const auto points = static_cast<double>(cycle.size());
    for (size_t i = 0; i < cycle.size(); ++i)
    {
        const auto angle = 2.0 * juce::MathConstants<double>::pi * harmonic * static_cast<double>(i) / points;
        real += cycle[i] * std::cos(angle);
        imaginary += cycle[i] * std::sin(angle);
    }
    return static_cast<float>(2.0 * std::sqrt(real * real + imaginary * imaginary) / points);
}

// One cycle of a table's frame at a level, read the way the voice reads it.
std::vector<float> readFrame(const rhino::forge::Wavetable& table, int level, int frame, int points)
{
    std::vector<float> cycle(static_cast<size_t>(points));
    for (int i = 0; i < points; ++i)
        cycle[static_cast<size_t>(i)] = table.frameSample(level, frame,
                                                          static_cast<float>(i) / static_cast<float>(points));
    return cycle;
}

// Band-limiting is the whole reason a frame is stored more than once. These
// check the extra copies really are the same wave with harmonics taken off the
// top, rather than something the transform has scaled, shifted or mangled.
void bandLimitSuite()
{
    using rhino::forge::wavetableFrameSize;
    const auto& table = rhino::forge::builtInWavetable();

    require(table.frameCount() == rhino::forge::waveShapeCount,
            "the built-in table holds every declared frame");
    require(table.levelCount() > 1, "a table carries band-limited copies of its frames");

    // The one that catches a scaling mistake in the transform. A sine is a
    // single harmonic, so every level that keeps any harmonic at all has to
    // hand back that same sine at that same amplitude: not half of it, not
    // 2048 times it, not inverted.
    for (int level = 0; level < table.levelCount(); ++level)
        for (int i = 0; i < 64; ++i)
        {
            const auto phase = static_cast<float>(i) / 64.0f;
            requireClose(table.frameSample(level, 0, phase),
                         std::sin(phase * juce::MathConstants<float>::twoPi), 0.02f,
                         "a band-limited sine is the same sine at every level");
        }

    // A saw carries every harmonic, so it is what shows whether the levels are
    // cut where they say they are: each must keep what is below its limit and
    // have thrown away what is above it.
    constexpr auto saw = 6;
    for (int level = 1; level < table.levelCount(); ++level)
    {
        const auto limit = table.harmonicsAt(level);
        if (limit < 8 || limit >= wavetableFrameSize / 2) continue;
        const auto cycle = readFrame(table, level, saw, 8192);
        const auto kept = limit / 2;
        require(harmonicEnergy(cycle, kept) > 0.3f / static_cast<float>(kept),
                "a band-limited frame keeps the harmonics below its limit");
        // Everything above the cut, swept up to the stored Nyquist rather than
        // sampled at one harmonic. Past the stored Nyquist a probe measures the
        // interpolator's images and not the band-limiting, so it stops there —
        // and a single probe at twice the limit, which is what this used to be,
        // sat on the stored frame's DC image and so read near zero whatever the
        // transform had done.
        const auto top = table.sizeAt(level) / 2;
        const auto stride = std::max(1, (top - limit) / 32);
        auto leaked = 0.0f;
        for (int harmonic = limit + 1; harmonic <= top; harmonic += stride)
            leaked = std::max(leaked, harmonicEnergy(cycle, harmonic));
        require(leaked < 0.02f / static_cast<float>(limit),
                "a band-limited frame has thrown away the harmonics above its limit");
    }

    // Level 0 is the frame exactly as authored, because it is what the panel
    // draws. If the transform touched it, the display and the table would
    // disagree and M9a's guarantee would be gone.
    for (int shape = 0; shape < rhino::forge::waveShapeCount; ++shape)
        for (int i = 0; i < wavetableFrameSize; i += 37)
        {
            const auto phase = static_cast<float>(i) / static_cast<float>(wavetableFrameSize);
            requireClose(table.frameSample(0, shape, phase), rhino::forge::waveShape(shape, phase), 0.0005f,
                         "level 0 is the frame exactly as it was authored");
        }

    // The level a note is given has to be one whose harmonics all fit under
    // Nyquist, at every note Forge can be asked to play.
    constexpr double sampleRate = 48000.0;
    for (int note = 0; note <= 127; ++note)
    {
        const auto hz = static_cast<float>(440.0 * std::pow(2.0, (note - 69) / 12.0));
        const auto level = table.levelFor(hz, sampleRate);
        const auto harmonics = table.harmonicsAt(level);
        require(level == 0 || static_cast<double>(harmonics) * hz <= sampleRate * 0.5 + 1.0,
                "the level a note reads keeps its harmonics under Nyquist");
        // And the point of the finer spacing: it keeps most of what it could
        // have had, rather than as little as half of it. Only asked where a
        // note is entitled to enough harmonics for the spacing to be the thing
        // deciding; at the very top of the keyboard the counts are so small
        // that rounding them to whole harmonics is.
        const auto allowed = sampleRate * 0.5 / hz;
        require(level == 0 || allowed < 8.0 || static_cast<double>(harmonics) >= allowed * 0.7,
                "the level a note reads keeps most of the harmonics it was entitled to");
    }

    // And the point of the whole exercise: a high note aliases far less than
    // the raw frame does. Read straight from the table rather than through the
    // synth, so nothing but the band-limiting is being measured. Aliasing lands
    // between the note's harmonics, never on them, so the bins halfway between
    // are where a clean saw has nothing and a folded one does not.
    const auto hz = 4186.0f;
    constexpr int steps = 8192;
    std::vector<float> raw(steps), limited(steps);
    const auto level = table.levelFor(hz, sampleRate);
    for (int i = 0; i < steps; ++i)
    {
        const auto phase = static_cast<float>(std::fmod(static_cast<double>(i) * hz / sampleRate, 1.0));
        raw[static_cast<size_t>(i)] = table.frameSample(0, saw, phase);
        limited[static_cast<size_t>(i)] = table.frameSample(level, saw, phase);
    }
    auto rawFold = 0.0f, limitedFold = 0.0f;
    for (int bin = 40; bin < 3900; ++bin)
    {
        const auto binHz = static_cast<float>(bin) * static_cast<float>(sampleRate) / static_cast<float>(steps);
        const auto ofFundamental = binHz / hz;
        if (std::abs(ofFundamental - std::round(ofFundamental)) < 0.35f) continue;
        rawFold = std::max(rawFold, harmonicEnergy(raw, bin));
        limitedFold = std::max(limitedFold, harmonicEnergy(limited, bin));
    }
    if (limitedFold >= rawFold * 0.25f)
    {
        require(false, "band-limiting takes the aliasing off a high note");
        std::cerr << "       raw folded " << rawFold << ", band-limited folded " << limitedFold << '\n';
    }
}

void tuningSuite()
{
    for (const double sampleRate : {44100.0, 48000.0})
        for (const int note : {27, 33, 45, 52, 54, 56, 57, 69, 81})
        {
            const auto expected = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const auto measured = renderedFundamental(note, sampleRate);
            const auto cents = 1200.0 * std::log2(measured / expected);
            // Two cents is under what anyone hears and well over the peak
            // interpolation's own error, which measures at about half of one.
            require(std::abs(cents) < 2.0, "a note sounds at the pitch it names");
        }
}

// --- The sub oscillator -------------------------------------------------------
//
// The sub has a shape and a pitch of its own, and both claims are settled the
// way every other claim about what Forge sounds like is: by measuring the
// rendered signal. Nothing here reads the phase accumulator or the table, so a
// wave wired to the wrong frame, a frame generated wrongly, or an octave
// applied to the phase increment twice all show up as a number that is out.
//
// The harmonic ratios below are the textbook ones and were confirmed against
// the authored frames before they were written down: a saw's second partial is
// half its first, a square and a triangle have no second at all and differ by
// their third, a 25% pulse has a null at its fourth, and the rounded rectangle
// sits between a sine and a square rather than being either.
void subSuite()
{
    // The sub alone: no oscillator, no noise, no filter, and the amp envelope
    // out of the way, so the whole of what is measured is the sub.
    const auto subOnly = [] (int wave, float octave)
    {
        rhino::forge::Patch patch;
        patch.a.enable = 0.0f;
        patch.b.enable = 0.0f;
        patch.noiseEnable = 0.0f;
        patch.filterEnable = 0.0f;
        patch.subEnable = 1.0f;
        patch.subLevel = 1.0f;
        patch.subWave = static_cast<float>(wave);
        patch.subOctave = octave;
        patch.envs[rhino::forge::ampEnv].attack = 0.001f;
        patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
        return patch;
    };

    constexpr auto sampleRate = 48000.0;
    constexpr auto note = 57;   // A3, so the sub at OCT 0 lands on A2
    const auto nominal = 440.0 * std::pow(2.0, (note - 69) / 12.0);

    // Where the sub sits. OCT 0 is an octave below the note, which is what it
    // has always been and what a patch written before the control existed
    // expects; every step either way is a doubling or a halving of that.
    for (const auto octave : {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f})
    {
        const auto expected = nominal * 0.5 * std::pow(2.0, octave);
        const auto spectrum = renderedSpectrum(subOnly(0, octave), note, sampleRate);
        const auto cents = 1200.0 * std::log2(loudestPeak(spectrum, sampleRate).frequency / expected);
        require(std::abs(cents) < 2.0, "the sub sounds an octave below the note, moved by its own octave");
    }

    // What each shape is, by its harmonics. Measured against the fundamental
    // rather than in absolute terms, because the shapes are normalised to the
    // same peak and not to the same fundamental -- a square's is 2 dB over a
    // sine's at the same setting, exactly as it is in the table the
    // oscillators read.
    const auto partials = [&] (int wave)
    {
        const auto spectrum = renderedSpectrum(subOnly(wave, 0.0f), note, sampleRate);
        const auto fundamental = loudestPeak(spectrum, sampleRate);
        std::array<double, 7> ratio {};
        ratio[1] = 1.0;
        for (int harmonic = 2; harmonic < static_cast<int>(ratio.size()); ++harmonic)
            ratio[static_cast<size_t>(harmonic)] =
                partialAmplitude(spectrum, fundamental.frequency, harmonic, sampleRate)
                / std::max(1.0e-12, fundamental.amplitude);
        return ratio;
    };

    // A sine is the fundamental and nothing else. The tolerance is what the
    // window leaks into the neighbouring partials, not what the oscillator
    // produces.
    const auto sine = partials(0);
    require(sine[2] < 0.002 && sine[3] < 0.002, "the sub's sine carries no harmonics");

    // A rounded rectangle: odd harmonics like a square, but less of each. The
    // third is what separates it from both of its neighbours -- nothing at all
    // on a sine, a third on a square, and 0.27 here.
    const auto rect = partials(1);
    require(rect[2] < 0.01, "the sub's rounded rectangle is an odd-harmonic wave");
    requireClose(static_cast<float>(rect[3]), 0.269f, 0.02f,
                 "the sub's rounded rectangle sits between a sine and a square");
    require(rect[5] < 0.2 && rect[5] > 0.05,
            "the sub's rounded rectangle is softer than a square further up");

    const auto triangle = partials(2);
    require(triangle[2] < 0.01, "the sub's triangle is an odd-harmonic wave");
    requireClose(static_cast<float>(triangle[3]), 1.0f / 9.0f, 0.015f,
                 "the sub's triangle has a ninth of its fundamental at the third harmonic");

    const auto saw = partials(3);
    requireClose(static_cast<float>(saw[2]), 0.5f, 0.02f, "the sub's saw halves at the second harmonic");
    requireClose(static_cast<float>(saw[3]), 1.0f / 3.0f, 0.02f, "the sub's saw thirds at the third");
    requireClose(static_cast<float>(saw[4]), 0.25f, 0.02f, "the sub's saw quarters at the fourth");

    const auto square = partials(4);
    require(square[2] < 0.01 && square[4] < 0.01, "the sub's square has no even harmonics");
    requireClose(static_cast<float>(square[3]), 1.0f / 3.0f, 0.02f,
                 "the sub's square thirds at the third harmonic");

    // A quarter-cycle pulse, which is the one shape here that is neither odd
    // only nor a plain 1/h series: its fourth harmonic falls in the null the
    // duty cycle puts there, and that null is what tells it from a square.
    const auto pulse = partials(5);
    requireClose(static_cast<float>(pulse[2]), 0.7071f, 0.03f,
                 "the sub's pulse is loud at the second harmonic");
    require(pulse[4] < 0.02, "the sub's pulse has a null where its duty cycle puts one");

    // Every shape is read through the same band-limited copies the oscillators
    // are, which is the reason the sub is a table at all now that it is not
    // only a sine. The saw is the one with something at every harmonic, so it
    // is the one that would alias first: pushed two octaves up and played at
    // the top of the keyboard, everything it renders must still be a harmonic
    // of the note.
    const auto& table = rhino::forge::subWavetable();
    require(table.frameCount() == rhino::forge::subShapeCount,
            "the sub's table holds every declared shape");
    require(table.levelCount() > 1, "the sub's table carries band-limited copies");

    const auto high = renderedSpectrum(subOnly(3, 2.0f), 96, sampleRate);
    const auto top = loudestPeak(high, sampleRate);
    auto alias = 0.0;
    for (int bin = 2; bin < static_cast<int>(high.size()) - 1; ++bin)
    {
        const auto hz = static_cast<double>(bin) * sampleRate / static_cast<double>(spectrumSize);
        const auto of = hz / top.frequency;
        // Anything within a fifth of a harmonic's place is that harmonic or the
        // window's skirt around it; what is left is either aliasing or nothing.
        if (std::abs(of - std::round(of)) < 0.2) continue;
        alias = std::max(alias, high[static_cast<size_t>(bin)]);
    }
    require(alias < 0.02 * top.amplitude,
            "a saw sub two octaves up folds nothing back down the spectrum");
}
}

void oscillatorTests()
{
    oscillatorSuite();
    waveTableSuite();
    bandLimitSuite();
    tuningSuite();
    subSuite();
}
}
