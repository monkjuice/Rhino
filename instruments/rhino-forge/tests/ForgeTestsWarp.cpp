// The two warp stages under each oscillator: that every mode is neutral where it
// says it is, that each one changes the wave in the way its family means, and
// that the cross-modulation sources reach the carrier.
#include "ForgeTestSupport.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge::tests
{
namespace
{
// ------------------------------------------------------------------ warp ---
//
// Warp is a family of twenty-six modes and the measurements below are mostly
// about the family rather than about one of them: that every mode is stable,
// that every mode leaves the note where it was, and that the knob beside a mode
// means nothing when it is at nothing. The handful of per-mode checks are the
// ones where the claim in the tooltip is worth holding the code to.

// A patch with one oscillator on a saw, nothing else sounding and no filter, so
// what is measured is the oscillator and not the voice around it.
rhino::forge::Patch warpTestPatch()
{
    rhino::forge::Patch patch;
    patch.a.enable = 1.0f;
    patch.a.position = 6.0f / 9.0f;   // the SAW frame, landed on exactly
    patch.a.unison = 1.0f;
    patch.a.level = 0.75f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.envs[rhino::forge::ampEnv].attack = 0.001f;
    patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
    return patch;
}

// One held note rendered through a Core, past the attack, as a mono sum.
std::vector<float> warpRender(const rhino::forge::Patch& patch, int samples, int note = 57,
                              double sampleRate = 48000.0)
{
    rhino::forge::Core core;
    core.initialise(sampleRate);
    core.noteOn(note, 1.0f, patch);
    for (int i = 0; i < 4096; ++i) { auto l = 0.0f, r = 0.0f; core.renderSample(patch, l, r); }

    std::vector<float> out(static_cast<size_t>(samples), 0.0f);
    for (int i = 0; i < samples; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
        out[static_cast<size_t>(i)] = 0.5f * (l + r);
    }
    return out;
}

float warpMean(const std::vector<float>& signal)
{
    auto sum = 0.0;
    for (const auto sample : signal) sum += sample;
    return signal.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(signal.size()));
}

float warpPeak(const std::vector<float>& signal)
{
    auto peak = 0.0f;
    for (const auto sample : signal) peak = juce::jmax(peak, std::abs(sample));
    return peak;
}

// How much of the signal sits in the steps between one sample and the next,
// against how much sits in the signal itself. The same amplitude-independent
// reading brightness() takes off a buffer, on a plain vector.
float warpBrightness(const std::vector<float>& signal)
{
    auto edges = 0.0, total = 0.0;
    for (size_t i = 1; i < signal.size(); ++i)
    {
        const auto step = static_cast<double>(signal[i]) - signal[i - 1];
        edges += step * step;
        total += static_cast<double>(signal[i]) * signal[i];
    }
    return total > 0.0 ? static_cast<float>(std::sqrt(edges / total)) : 0.0f;
}

// Whether a render repeats over a given lag, from -1 for the exact opposite to
// 1 for the same thing again. The lag is in samples and need not be a whole
// number of them, because a note's period almost never is.
//
// This is what the pitch of a warped oscillator is measured with, in place of
// the tallest-partial method tuningSuite uses. That method is right for a plain
// saw and wrong here: SYNC deliberately makes its own harmonic the loudest
// thing in the signal, so the tallest bin would report the mode working as the
// note moving. What a warp does leave alone is the period -- it bends the read
// inside the cycle, and the cycle still comes round at the note -- so the
// period is what is asked about. Measured off the rendered samples, with no
// reference to the phase accumulator that produced them.
double warpRepeat(const std::vector<float>& signal, double lag)
{
    const auto window = static_cast<int>(signal.size()) - static_cast<int>(std::ceil(lag)) - 1;
    if (window <= 0) return 0.0;
    auto together = 0.0, here = 0.0, there = 0.0;
    for (int i = 0; i < window; ++i)
    {
        const auto at = static_cast<double>(i) + lag;
        const auto index = static_cast<size_t>(at);
        const auto fraction = at - std::floor(at);
        const auto shifted = signal[index] * (1.0 - fraction) + signal[index + 1] * fraction;
        const auto sample = static_cast<double>(signal[static_cast<size_t>(i)]);
        together += sample * shifted;
        here += sample * sample;
        there += shifted * shifted;
    }
    return here > 0.0 && there > 0.0 ? together / std::sqrt(here * there) : 0.0;
}

void warpSuite()
{
    using rhino::forge::WarpMode;
    using rhino::forge::WarpStage;
    using rhino::forge::WarpState;
    constexpr auto modeCount = rhino::forge::warpModeCount;

    // --- The list itself ------------------------------------------------------
    require(static_cast<int>(rhino::forge::warpModes().size()) == modeCount,
            "the warp table holds exactly as many modes as it says it does");
    for (int mode = 0; mode < modeCount; ++mode)
    {
        const auto name = juce::String(rhino::forge::warpModeName(mode));
        require(name.isNotEmpty(), "every warp mode has a name");
        for (int other = mode + 1; other < modeCount; ++other)
            require(name != rhino::forge::warpModeName(other), "no two warp modes share a name");
    }
    // Every category the menu offers has something in it, or the menu draws an
    // empty submenu.
    for (int category = 0; category < rhino::forge::warpCategoryCount; ++category)
    {
        auto held = 0;
        for (int mode = 0; mode < modeCount; ++mode)
            if (rhino::forge::warpCategoryOf(static_cast<WarpMode>(mode))
                == static_cast<rhino::forge::WarpCategory>(category)) ++held;
        require(held > 0, "every warp category has at least one mode in it");
    }

    // --- Neutrality -----------------------------------------------------------
    //
    // A mode is chosen first and opened up afterwards, so a mode sitting at zero
    // depth has to be inaudible. MIRROR is the one deliberate exception: it
    // folds the cycle whatever the knob says, which is exactly what the manual
    // says of it.
    const auto ramp = [] (float phase) { return phase * 2.0f - 1.0f; };
    for (int mode = 0; mode < modeCount; ++mode)
    {
        if (static_cast<WarpMode>(mode) == WarpMode::mirror) continue;
        // Where the mode itself says it does nothing: at the bottom of the knob
        // for most of them, in the middle for the four that go both ways.
        const auto neutral = rhino::forge::warpNeutralDepth(static_cast<WarpMode>(mode));
        // QUANTIZE holds the read at steps rather than sweeping it, so at
        // nothing it is a staircase finer than the table itself rather than a
        // straight line. Measured against that resolution instead of against
        // nothing at all.
        const auto stepped = static_cast<WarpMode>(mode) == WarpMode::quantize;
        auto stage = rhino::forge::warpStageFor(static_cast<float>(mode), neutral, 220.0, 48000.0);
        WarpState state;
        auto worst = 0.0f;
        for (int i = 0; i <= 64; ++i)
        {
            const auto phase = static_cast<float>(i) / 64.0f * 0.999f;
            worst = juce::jmax(worst, std::abs(rhino::forge::warpRead(stage, phase, state, ramp)
                                               - ramp(phase)));
        }
        if (worst > (stepped ? 0.01f : 1.0e-5f))
        {
            require(false, "a warp mode leaves the wave as it was at the depth it calls neutral");
            std::cerr << "       " << rhino::forge::warpModeName(mode) << " moved it by " << worst << '\n';
        }
    }

    // MIRROR really does mirror: the second half of the cycle is the first half
    // backwards, whatever the depth.
    {
        auto stage = rhino::forge::warpStageFor(static_cast<float>(WarpMode::mirror), 0.5f, 220.0f, 48000.0);
        WarpState state;
        for (int i = 1; i < 32; ++i)
        {
            const auto phase = static_cast<float>(i) / 64.0f;
            requireClose(rhino::forge::warpRead(stage, phase, state, ramp),
                         rhino::forge::warpRead(stage, 1.0f - phase, state, ramp), 1.0e-5f,
                         "MIRROR reads the second half of the cycle as the first half backwards");
        }
    }

    // A bend keeps both ends of the cycle where they were, or the wave would no
    // longer join up with itself.
    for (const float k : {-0.9f, -0.4f, 0.0f, 0.4f, 0.9f})
    {
        requireClose(rhino::forge::warpBend(0.0f, k), 0.0f, 1.0e-6f, "a bend starts where the cycle does");
        requireClose(rhino::forge::warpBend(1.0f, k), 1.0f, 1.0e-6f, "a bend ends where the cycle does");
        auto previous = -1.0f;
        for (int i = 0; i <= 32; ++i)
        {
            const auto bent = rhino::forge::warpBend(static_cast<float>(i) / 32.0f, k);
            require(bent > previous, "a bend never doubles back on itself");
            previous = bent;
        }
    }

    // --- Every mode, rendered -------------------------------------------------
    //
    // Both stages set to the same mode at full depth, on top of a stack wide
    // enough that every member of it is carrying its own state. Nothing here is
    // allowed to produce a value that is not a number, to fall silent, or to
    // leave full scale.
    for (int mode = 1; mode < modeCount; ++mode)
    {
        auto patch = warpTestPatch();
        patch.a.unison = 4.0f;
        patch.a.detune = 0.3f;
        // The modes that read another source need it switched on, or the stage
        // takes itself out and the render below proves nothing about it.
        patch.b.enable = 1.0f;
        patch.b.level = 0.5f;
        patch.subEnable = 1.0f;
        patch.subLevel = 0.0f;
        for (int slot = 0; slot < rhino::forge::warpSlots; ++slot)
        {
            patch.a.warpMode[static_cast<size_t>(slot)] = static_cast<float>(mode);
            patch.a.warpAmount[static_cast<size_t>(slot)] = 1.0f;
        }
        for (const int note : {21, 57, 96})
        {
            const auto rendered = warpRender(patch, 4096, note);
            auto finite = true;
            for (const auto sample : rendered) finite = finite && std::isfinite(sample);
            if (!finite || warpPeak(rendered) <= 0.0f || warpPeak(rendered) > 1.0f)
            {
                require(false, "every warp mode renders finite, audible signal inside full scale");
                std::cerr << "       " << rhino::forge::warpModeName(mode) << " at note " << note
                          << " peaked at " << warpPeak(rendered) << '\n';
            }
        }
    }

    // --- The note survives the warp -------------------------------------------
    //
    // A warp bends where inside the cycle the table is read, and the cycle
    // still comes round at the note, so none of these may move the pitch. This
    // is the check that would catch a warp applied to the phase increment
    // rather than to the phase -- which is the easy way to write one and the
    // wrong way, because it would make the depth knob a tuning control.
    //
    // ODD/EVEN is deliberately absent: at the top of its travel it really does
    // leave only the even harmonics, and a wave with no fundamental in it is an
    // octave up. The manual says as much.
    {
        const auto note = 57;
        const auto expected = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const auto period = 48000.0 / expected;
        // `cycles` is how many of the note's own cycles the wave takes to come
        // round. One for everything that warps a cycle in place; two for FM
        // SUB, because the sub is an octave below the note and a modulator at
        // half the carrier's rate puts the sidebands half a note apart. That is
        // FM working, not the pitch slipping, and it is worth saying out loud
        // rather than leaving the mode out of the check.
        //
        // `frame` is which shape the table is read at. Every mode is checked on
        // the saw except FM SELF, which is checked on the sine: a loop reading
        // its own output back into its own phase is a discontinuous map when
        // the wave it reads has an edge in it, and a discontinuous map has no
        // period to measure however shallow it is set. On a sine -- which is
        // the shape feedback is classically reached for, and the one it turns
        // into a saw -- it is perfectly periodic, and that is the claim worth
        // holding: what the loop does to the pitch, not what a saw does to the
        // loop.
        struct Checked { WarpMode mode; float depth; double cycles; float frame; };
        constexpr auto saw = 6.0f / 9.0f, sine = 0.0f;
        const Checked checks[] = {{WarpMode::sync, 0.7f, 1.0, saw}, {WarpMode::bendUp, 0.7f, 1.0, saw},
                                  {WarpMode::bendDown, 0.7f, 1.0, saw}, {WarpMode::pwm, 0.7f, 1.0, saw},
                                  {WarpMode::asym, 0.8f, 1.0, saw}, {WarpMode::flip, 0.6f, 1.0, saw},
                                  {WarpMode::mirror, 0.7f, 1.0, saw}, {WarpMode::quantize, 0.7f, 1.0, saw},
                                  {WarpMode::hardClip, 0.7f, 1.0, saw}, {WarpMode::linearFold, 0.7f, 1.0, saw},
                                  {WarpMode::lowPass, 0.7f, 1.0, saw}, {WarpMode::pdSub, 0.5f, 2.0, saw},
                                  {WarpMode::pdSelf, 0.7f, 1.0, sine}};
        for (const auto& checked : checks)
        {
            auto patch = warpTestPatch();
            patch.subEnable = 1.0f;   // FM SUB reads it, and the rest never hear it
            patch.subLevel = 0.0f;
            patch.a.position = checked.frame;
            patch.a.warpMode[0] = static_cast<float>(checked.mode);
            patch.a.warpAmount[0] = checked.depth;
            const auto rendered = warpRender(patch, 16384, note);

            const auto atNote = warpRepeat(rendered, period * checked.cycles);
            if (atNote < 0.95)
            {
                require(false, "a warped oscillator still repeats at the note's own period");
                std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(checked.mode))
                          << " repeated at only " << atNote << '\n';
            }
            // And nothing shorter repeats, or the warp has put the note up
            // rather than left it alone. Stopped short of the period itself,
            // because a lag a hair under it correlates nearly as well by
            // arithmetic rather than by the wave saying anything.
            auto shortest = 0.0;
            auto worst = 0.0;
            for (auto lag = 24.0; lag < period * checked.cycles * 0.96; lag += 0.5)
                if (warpRepeat(rendered, lag) > worst) { worst = warpRepeat(rendered, lag); shortest = lag; }
            if (worst > 0.95)
            {
                require(false, "nothing shorter than the note's period repeats in a warped oscillator");
                std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(checked.mode))
                          << " repeated at " << shortest << " samples, " << worst << '\n';
            }
        }
    }

    // --- What the modes are for ------------------------------------------------
    auto plain = warpTestPatch();
    const auto plainRender = warpRender(plain, 8192);
    const auto plainBrightness = warpBrightness(plainRender);

    const auto warped = [] (WarpMode mode, float depth)
    {
        auto patch = warpTestPatch();
        patch.a.warpMode[0] = static_cast<float>(mode);
        patch.a.warpAmount[0] = depth;
        return warpRender(patch, 8192);
    };

    // LPF takes the top off and HPF takes the bottom off, which is a fall and a
    // rise in the same reading.
    require(warpBrightness(warped(WarpMode::lowPass, 1.0f)) < plainBrightness * 0.6f,
            "the LPF warp darkens the waveform");
    require(warpBrightness(warped(WarpMode::highPass, 1.0f)) > plainBrightness * 1.2f,
            "the HPF warp brightens the waveform");

    // A wavefolder adds harmonics the table never held.
    require(warpBrightness(warped(WarpMode::linearFold, 1.0f)) > plainBrightness * 1.2f,
            "folding the wave adds harmonics to it");

    // RECTIFY is asymmetric by construction and would leave a constant offset
    // behind if nothing took it off. This is the check on the blocker: the
    // offset is what a note would thump with, and what the filter would
    // otherwise have to carry.
    const auto rectified = warped(WarpMode::rectify, 1.0f);
    require(warpPeak(rectified) > 0.05f, "RECTIFY leaves something to measure");
    require(std::abs(warpMean(rectified)) < warpPeak(rectified) * 0.02f,
            "a rectified oscillator is left with no constant offset");

    // Both stages run, in the order they are declared. Measured on two modes
    // that shape the sample rather than move the read, because those are the
    // ones the order can be seen in: a mode that bends the phase and a mode
    // that shapes the sample commute by construction here, since a stage hands
    // the one in front of it a way to read rather than something already read.
    {
        auto first = warpTestPatch();
        first.a.warpMode[0] = static_cast<float>(WarpMode::hardClip);
        first.a.warpAmount[0] = 0.8f;
        first.a.warpMode[1] = static_cast<float>(WarpMode::rectify);
        first.a.warpAmount[1] = 0.8f;
        auto second = warpTestPatch();
        second.a.warpMode[0] = static_cast<float>(WarpMode::rectify);
        second.a.warpAmount[0] = 0.8f;
        second.a.warpMode[1] = static_cast<float>(WarpMode::hardClip);
        second.a.warpAmount[1] = 0.8f;

        const auto one = warpRender(first, 2048);
        const auto two = warpRender(second, 2048);
        auto difference = 0.0f;
        for (size_t i = 0; i < one.size(); ++i)
            difference = juce::jmax(difference, std::abs(one[i] - two[i]));
        require(difference > 0.01f, "the two warp stages run in the order they are declared");
    }

    // Every mode driven by the other oscillator needs it switched on, exactly as
    // the manual says of all four families. With it off there is nothing to
    // modulate with and the stage takes itself out rather than going quietly
    // wrong; with it on, its own level makes no difference to the modulation.
    for (const auto mode : {WarpMode::pdOsc, WarpMode::fmOsc, WarpMode::fmExpOsc,
                            WarpMode::amOsc, WarpMode::rmOsc})
    {
        auto alone = warpTestPatch();
        alone.a.warpMode[0] = static_cast<float>(mode);
        alone.a.warpAmount[0] = 1.0f;
        const auto withoutB = warpRender(alone, 2048);
        auto difference = 0.0f;
        for (size_t i = 0; i < withoutB.size(); ++i)
            difference = juce::jmax(difference, std::abs(withoutB[i] - plainRender[i]));
        if (difference > 1.0e-5f)
        {
            require(false, "a stage driven by the other oscillator does nothing while it is off");
            std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(mode)) << '\n';
        }

        alone.b.enable = 1.0f;
        alone.b.level = 0.0f;   // heard only as a modulator, as the manual suggests
        const auto withB = warpRender(alone, 2048);
        difference = 0.0f;
        for (size_t i = 0; i < withB.size(); ++i)
            difference = juce::jmax(difference, std::abs(withB[i] - plainRender[i]));
        if (difference < 0.01f)
        {
            require(false, "a stage driven by the other oscillator works with its level down");
            std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(mode)) << '\n';
        }
    }

    // --- FM against PD --------------------------------------------------------
    //
    // The manual separates them and so does this. PD moves where in the cycle
    // the table is read and never touches the rate the cycle runs at; FM moves
    // that rate and nothing else. The multiplier is the whole of the
    // difference, so it is checked directly rather than inferred from a render.
    {
        const auto factor = [] (WarpMode mode, float depth, float modulator)
        {
            return rhino::forge::warpPitchFactor(mode, depth, modulator);
        };
        for (const auto mode : {WarpMode::pdOsc, WarpMode::pdSelf, WarpMode::bendUp,
                                WarpMode::amOsc, WarpMode::rmOsc, WarpMode::sync})
            requireClose(factor(mode, 1.0f, 1.0f), 1.0f, 0.0001f,
                         "only the FM modes reach the rate the cycle runs at");
        for (const auto mode : {WarpMode::fmOsc, WarpMode::fmExpOsc})
        {
            requireClose(factor(mode, 0.0f, 1.0f), 1.0f, 0.0001f,
                         "FM at no depth leaves the note where it was");
            requireClose(factor(mode, 1.0f, 0.0f), 1.0f, 0.0001f,
                         "FM with nothing arriving leaves the note where it was");
        }
        // Linear is proportional and clamps at zero rather than running the
        // frequency backwards, which is the traditional FM the manual describes.
        requireClose(factor(WarpMode::fmOsc, 1.0f, 1.0f), 1.0f + rhino::forge::warpFmDepth, 0.0001f,
                     "linear FM is proportional to the modulator");
        requireClose(factor(WarpMode::fmOsc, 1.0f, -1.0f), 0.0f, 0.0001f,
                     "linear FM clamps at zero rather than running backwards");
        // The clamp bites at a quarter of the way down at full depth, which is
        // what "traditional FM" means: a good part of the modulator's trough is
        // spent at a standstill. Short of that it is still proportional.
        requireClose(factor(WarpMode::fmOsc, 1.0f, -0.2f),
                     1.0f - 0.2f * rhino::forge::warpFmDepth, 0.0001f,
                     "linear FM short of the clamp is still proportional");
        // Exponential is symmetric in octaves, which is why it sweeps so much
        // further for the same depth and why it does not hold the note.
        requireClose(factor(WarpMode::fmExpOsc, 1.0f, 1.0f),
                     std::pow(2.0f, rhino::forge::warpFmOctaves), 0.01f,
                     "exponential FM sweeps in octaves");
        requireClose(factor(WarpMode::fmExpOsc, 1.0f, -1.0f),
                     1.0f / std::pow(2.0f, rhino::forge::warpFmOctaves), 0.001f,
                     "exponential FM sweeps the same distance downwards");

        // And the two are audibly different things from the same source at the
        // same depth, which is the claim the separation is worth making for.
        auto pd = warpTestPatch();
        pd.b.enable = 1.0f;
        pd.b.level = 0.0f;
        pd.a.warpMode[0] = static_cast<float>(WarpMode::pdOsc);
        pd.a.warpAmount[0] = 0.6f;
        auto fm = pd;
        fm.a.warpMode[0] = static_cast<float>(WarpMode::fmOsc);
        const auto pdRender = warpRender(pd, 4096);
        const auto fmRender = warpRender(fm, 4096);
        auto difference = 0.0f;
        for (size_t i = 0; i < pdRender.size(); ++i)
            difference = juce::jmax(difference, std::abs(pdRender[i] - fmRender[i]));
        require(difference > 0.05f, "FM and PD from one source at one depth are not one sound");
    }

    // --- AM against RM --------------------------------------------------------
    //
    // AM rides the carrier and leaves it in the sound; RM replaces it, so the
    // carrier's own pitch goes and the two sidebands are what is left. Driven
    // by the sub, which runs an octave below the note, that difference is
    // exact rather than approximate: a cycle of the note later, the sub has
    // turned over, so a ring-modulated wave comes back inverted while an
    // amplitude-modulated one does not.
    {
        const auto note = 57;
        const auto period = 48000.0 / (440.0 * std::pow(2.0, (note - 69) / 12.0));
        const auto driven = [note] (WarpMode mode)
        {
            auto patch = warpTestPatch();
            patch.subEnable = 1.0f;
            patch.subLevel = 0.0f;
            patch.a.warpMode[0] = static_cast<float>(mode);
            patch.a.warpAmount[0] = 1.0f;
            return warpRender(patch, 16384, note);
        };
        require(warpRepeat(driven(WarpMode::amSub), period) > 0.2,
                "an amplitude-modulated oscillator still has its own note in it");
        require(warpRepeat(driven(WarpMode::rmSub), period) < -0.5,
                "a ring-modulated oscillator comes back inverted, its own note gone");
    }

    // --- The matrix reaches the depths ----------------------------------------
    //
    // The four warp depths were appended past the racks rather than put beside
    // the oscillator controls they belong with, because a destination is stored
    // as an index and moving one would move it inside every preset already
    // saved. These are the two halves of that: the new entries land where they
    // are expected, and nothing that was already there has shifted.
    {
        rhino::forge::Patch patch;
        const auto base = rhino::forge::warpDestinationBase;
        require(rhino::forge::destinationField(patch, base) == &patch.a.warpAmount[0]
                    && rhino::forge::destinationField(patch, base + 1) == &patch.a.warpAmount[1]
                    && rhino::forge::destinationField(patch, base + 2) == &patch.b.warpAmount[0]
                    && rhino::forge::destinationField(patch, base + 3) == &patch.b.warpAmount[1],
                "each warp depth is the destination its index names");
        require(rhino::forge::destinationField(patch, rhino::forge::destinationCount) == nullptr,
                "an index past the end of the list points at nothing");
        require(rhino::forge::fxDestinationOf(0, 0, 0) == rhino::forge::fxDestinationBase
                    && rhino::forge::destinationField(patch, rhino::forge::fxDestinationBase)
                           == &patch.racks[0].slots[0].knobs[0],
                "appending the warp depths left the racks where they were");
        require(base == 105
                    && rhino::forge::fxDestinationOf(1, 0, 0) == 49
                    && rhino::forge::fxDestinationOf(2, 3, 6) == 104,
                "the original four rack slots keep their saved destination indices");
        require(rhino::forge::fxDestinationOf(0, 4, 0) == rhino::forge::extendedFxDestinationBase
                    && rhino::forge::destinationField(
                           patch, rhino::forge::fxDestinationOf(2, 7, 6))
                           == &patch.racks[2].slots[7].mix,
                "the extended rack slots append after the existing warp destinations");
        for (int i = 0; i < rhino::forge::warpDestinationCount; ++i)
            require(juce::String(rhino::forge::destinations()[static_cast<size_t>(base + i)].id)
                        == rhino::forge::warpDestinations()[static_cast<size_t>(i)].id,
                    "the built list carries the warp depths at the end");
    }

    // --- The parameters and the panel -----------------------------------------
    rhino::forge::Processor processor;
    for (const auto* prefix : {"oscA", "oscB"})
        for (int slot = 1; slot <= rhino::forge::warpSlots; ++slot)
        {
            const auto id = juce::String(prefix) + "Warp" + juce::String(slot);
            require(processor.state.getParameter(id + "Mode") != nullptr
                        && processor.state.getParameter(id) != nullptr,
                    "both halves of every warp stage are real parameters");
            // A fresh patch has no warp on it, so every preset written before
            // warp existed still sounds as it did.
            requireClose(value(processor, (id + "Mode").toRawUTF8()), 0.0f, 0.0001f,
                         "a warp stage opens on OFF");
            requireClose(value(processor, id.toRawUTF8()), 0.0f, 0.0001f,
                         "a warp depth opens at nothing");
            // A host's automation lane names the very mode the engine will run.
            for (int mode = 0; mode < modeCount; ++mode)
                requireText(textFor(processor, (id + "Mode").toRawUTF8(), static_cast<float>(mode)),
                            rhino::forge::warpModeName(mode),
                            "a warp mode reads out as the mode the engine runs");
        }

    // The four bipolar modes are the ones whose neutral is in the middle, and
    // nothing else claims to be. Written out rather than read back from the
    // same function, so this is a second opinion and not a tautology.
    for (int mode = 0; mode < modeCount; ++mode)
    {
        const auto bipolar = static_cast<WarpMode>(mode) == WarpMode::bendBoth
                          || static_cast<WarpMode>(mode) == WarpMode::asym
                          || static_cast<WarpMode>(mode) == WarpMode::mirror
                          || static_cast<WarpMode>(mode) == WarpMode::oddEven;
        requireClose(rhino::forge::warpNeutralDepth(static_cast<WarpMode>(mode)),
                     bipolar ? 0.5f : 0.0f, 0.0001f,
                     "a warp mode is neutral where the panel returns its knob to");
    }

    // Every mode has something to say for itself, which is what the field's
    // tooltip shows once a mode is chosen.
    for (int mode = 0; mode < modeCount; ++mode)
        require(rhino::forge::ui::warpTooltipFor(mode).isNotEmpty(),
                "every warp mode explains itself");

    // The depth knob greys out under OFF and comes back under anything else.
    // Declared as the panel's own rule rather than checked through the editor,
    // which needs a window.
    for (const auto& module : rhino::forge::ui::modules())
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
            {
                const auto id = juce::String(control.id);
                if (!id.startsWith("osc") || !id.contains("Warp") || id.endsWith("Mode")) continue;
                require(control.style == rhino::forge::ui::Style::knob,
                        "a warp depth is drawn as a knob");
                require(control.enabledBy != nullptr && juce::String(control.enabledBy) == id + "Mode",
                        "a warp depth is greyed out by its own mode field");
            }
}
}

void warpTests()
{
    warpSuite();
}
}
