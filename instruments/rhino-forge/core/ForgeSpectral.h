#pragma once

#include "ForgeSample.h"
#include "ForgeVoiceParts.h"
#include "ForgePatch.h"  // sourcePanLeft/Right, the one pan law every source is placed with

#include <array>
#include <cmath>

// The phase vocoder a spectral oscillator is rendered by: what one voice holds
// while it is sounding, and the hop of work that fills it.
//
// The analysed sample is in ForgeSample.h and is immutable and shared. This is
// the per-voice half — the running synthesis phase, the overlap-add buffer and
// where in the spectrogram the voice has got to — and it is the only part that
// is touched from the audio thread.
//
// Read the architecture section of SPECTRAL.md before changing any of it. The
// three decisions it records are load-bearing and none of them is obvious from
// the code alone.
namespace rhino::forge
{
// What plays the sample back at its own pitch. MIDI 60, which Forge's keyboard
// marks C3 (see Editor's setOctaveForMiddleC), so the note under the hand at
// the middle of the keyboard is the sample as it was recorded.
inline constexpr float spectralRootHz = 261.6255653f;

// How many detuned members a spectral stack may hold.
//
// The whole stack is summed in the frequency domain and inverse-transformed
// once, so a member costs a pass over the bins rather than a transform of its
// own — but it does cost a synthesis phase per bin, which is four kilobytes
// each, per voice, per oscillator. Twelve of those across sixteen voices and
// three oscillators is most of this engine's memory, and a spectral stack that
// wide is a wash rather than a chorus in any case.
inline constexpr int spectralUnisonMax = 6;

// How a spectral oscillator's playhead travels through its sample: the loop
// menu (pp. 77-78, 109).
//
// Declared in Serum's order with Serum's whole list, for the reason the
// oscillator's MODE is: a choice parameter's list is part of the plugin's
// published interface, and slotting TAILED in later would renumber MANUAL in
// every automation lane that had written it. TAILED is the one not built — the
// manual's description of it is the one in the menu that does not say what the
// loop markers do — so it is left off the field and plays as FWD LOOP if a
// preset from elsewhere names it.
//
// - ONE-SHOT plays from the start marker to the end marker once, and is then
//   silent for the rest of the note.
// - FWD LOOP plays from the start marker into the loop, and round it.
// - REV LOOP plays into the loop, turns at its far end, and loops backwards.
// - FWD/REV plays into the loop and then bounces between its ends.
// - MANUAL does not run at all: SCAN is the playhead's position between the
//   start and end markers, so it is set, automated or modulated rather than
//   played (p. 109).
//
// A negative SCAN plays the same journey from the other end: a voice starts at
// the end marker, and "into the loop" means arriving at the loop's end first.
enum class SpectralLoop { oneShot = 0, forward = 1, reverse = 2, pingPong = 3, tailed = 4, manual = 5 };
inline constexpr int spectralLoopCount = 6;

inline const char* spectralLoopName(int mode)
{
    switch (mode)
    {
        case 0: return "ONE-SHOT";
        case 2: return "REV LOOP";
        case 3: return "FWD/REV";
        case 4: return "TAILED";
        case 5: return "MANUAL";
        default: break;
    }
    return "FWD LOOP";
}

inline constexpr bool spectralLoopBuilt(int mode)
{
    return mode >= 0 && mode < spectralLoopCount && mode != static_cast<int>(SpectralLoop::tailed);
}

// What a loop value read off a parameter means to the engine. Anything not
// built plays as FWD LOOP, which is what every spectral oscillator did before
// the menu existed.
inline SpectralLoop spectralLoopOf(float value) noexcept
{
    const auto mode = juce::roundToInt(value);
    return spectralLoopBuilt(mode) ? static_cast<SpectralLoop>(mode) : SpectralLoop::forward;
}

// What is stored and what is listed, as for the oscillator's MODE: the field
// steps through the built modes only, and these two are where the orders meet.
inline constexpr int spectralLoopBuiltCount = spectralLoopCount - 1;

inline int spectralLoopAt(int position) noexcept
{
    auto seen = 0;
    for (int mode = 0; mode < spectralLoopCount; ++mode)
        if (spectralLoopBuilt(mode) && seen++ == position) return mode;
    return static_cast<int>(SpectralLoop::forward);
}

inline int spectralLoopPosition(int mode) noexcept
{
    if (!spectralLoopBuilt(mode)) mode = static_cast<int>(SpectralLoop::forward);
    auto seen = 0;
    for (int i = 0; i < mode; ++i)
        if (spectralLoopBuilt(i)) ++seen;
    return seen;
}

// Everything one voice holds for one spectral oscillator.
//
// This is heap-allocated by Core and never by value inside Voice. It is about
// seventy kilobytes, and Core is a header-only type that the test suites
// construct on the stack — see the per-voice state decision in SPECTRAL.md for
// why that is not negotiable.
struct SpectralVoice
{
    // Where in the spectrogram this voice is reading, in frames. Fractional:
    // the magnitudes either side of it are interpolated, which is what lets
    // SCAN move at a rate unrelated to the one the sample was analysed at.
    double frame = 0.0;
    // Whether the playhead has been put at its starting marker yet. A voice is
    // reset at note-on, before it knows which way SCAN is pointing, so the
    // first hop is the one that places it.
    bool running = false;
    // Which way the loop has turned it: +1 the way SCAN points, -1 against it.
    // REV LOOP turns once on reaching the loop and FWD/REV at every end.
    float bounce = 1.0f;
    // Whether the playhead has reached the loop yet. Until it has, it is still
    // playing the run-in from the start marker, which no loop mode repeats.
    bool looping = false;
    // A ONE-SHOT that has played to its end marker. The note goes on — the
    // envelope still decides when it stops — but there is nothing left to read.
    bool ended = false;
    // Which frame the last hop read. A transient resets the running phase, and
    // that has to happen on *arriving* at the frame rather than on every hop
    // that reads it: a playhead held still on an attack — SCAN at zero, which
    // is a frozen spectrum and a perfectly ordinary setting — would otherwise
    // reset to the same phase every hop, which is a periodic signal at the hop
    // rate rather than the tone that is actually there.
    int lastFrame = -1;

    // One running synthesis phase per bin per unison member. A member's phase
    // cannot be shared or offset from another's: they advance at different
    // rates because they are at different pitches, and that difference is the
    // whole of what detune is.
    //
    // What a bin holds is the phase of the *peak* that last owned it, not the
    // bin's own: every bin in a peak's region is locked to that peak, so the
    // peak's phase is the one thing that has to be carried from hop to hop.
    // Keeping it at every bin of the region is what lets a partial that drifts
    // into the next bin carry on from where it was rather than from whatever
    // its new neighbour was locked to — Laroche and Dolson's scaled phase
    // locking, without a list of peaks to match between hops.
    std::array<std::array<float, spectralBins>, spectralUnisonMax> synthPhase {};

    // The overlap-add buffers, circular and exactly one window long. A sample
    // is complete once every window that covers it has been added in, which at
    // a quarter-window hop is four of them.
    std::array<float, spectralFftSize> olaLeft {};
    std::array<float, spectralFftSize> olaRight {};
    int writeIndex = 0;

    // The hop of finished samples waiting to be handed out one at a time.
    std::array<float, spectralHop> outLeft {};
    std::array<float, spectralHop> outRight {};
    int pending = 0;
    int readIndex = 0;

    void reset() noexcept
    {
        frame = 0.0;
        running = false;
        bounce = 1.0f;
        looping = false;
        ended = false;
        lastFrame = -1;
        for (auto& member : synthPhase) member.fill(0.0f);
        olaLeft.fill(0.0f);
        olaRight.fill(0.0f);
        writeIndex = 0;
        pending = 0;
        readIndex = 0;
    }
};

// The scratch one hop of synthesis needs. Shared by every voice rather than
// held per voice: only one voice is ever inside a hop at a time, and this is
// another sixty kilobytes that would otherwise be multiplied by forty-eight.
struct SpectralScratch
{
    std::array<float, 2 * spectralFftSize> spectrumLeft {};
    std::array<float, 2 * spectralFftSize> spectrumRight {};
    std::array<float, spectralFftSize> window {};

    // What the hop reads out of the sample before any member of the stack has
    // shifted it: the magnitude where the playhead is, every bin's true phase
    // advance, and the peaks the spectrum is divided into. None of it depends
    // on the pitch, so it is worked out once per hop rather than once per
    // member.
    std::array<float, spectralBins> magnitude {};
    std::array<float, spectralBins> advance {};
    std::array<int, spectralBins> peak {};
    std::array<int, spectralBins> regionLow {};
    std::array<int, spectralBins> regionHigh {};
    int peaks = 0;
    // One member's peak phases for the next hop, written beside the ones being
    // read so a region shifted onto a bin cannot overwrite the phase a later
    // peak was about to continue from.
    std::array<float, spectralBins> nextPhase {};
    // How loud the region that wrote each bin of nextPhase was there. Shifting
    // down packs regions closer together than they were analysed, so two of
    // them can land on one bin; the bin carries the phase of whichever is
    // louder at it. Without this, a harmonic's region overwrote the bin the
    // harmonic below it had landed on, and that partial carried on next hop
    // from its neighbour's phase — 3.6 dB of a bright tone lost an octave down.
    std::array<float, spectralBins> claim {};
    bool ready = false;

    void prepare()
    {
        if (ready) return;
        for (int i = 0; i < spectralFftSize; ++i)
            window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(
                2.0f * juce::MathConstants<float>::pi * static_cast<float>(i)
                / static_cast<float>(spectralFftSize));
        ready = true;
    }
};

// What a hop of synthesis is told, resolved once per block rather than per bin.
struct SpectralSettings
{
    float pitchRatio = 1.0f;   // the note, against the sample's own pitch
    float scan = 1.0f;         // frames per hop, signed
    float cut = 1.0f;          // the spectral filter's corner, 0..1 of Nyquist
    float mix = 0.0f;          // filtered against unfiltered
    float lo = 0.0f, hi = 1.0f;  // the frequency bounds, 0..1 of Nyquist
    bool smooth = true;        // a Butterworth skirt at the bounds, not a cliff
    bool phaseLock = false;
    bool transients = false;
    int unison = 1;
    float detune = 0.0f;
    float blend = 0.5f;
    float pan = 0.0f;
    SpectralLoop loopMode = SpectralLoop::forward;
    // The markers, each 0..1 of the whole sample: where playback starts and
    // ends, and the loop inside that. The loop is held inside the playback
    // markers here rather than by the parameters, so dragging START past a
    // loop drags the loop's effective edge with it without moving its setting.
    float start = 0.0f, end = 1.0f;
    float loopStart = 0.0f, loopEnd = 1.0f;
    // MANUAL's playhead, 0..1 between the start and end markers.
    float position = 0.0f;
};

// Where the markers fall in frames, worked out once per hop. The loop is at
// least a frame long and the run at least a frame long, so nothing below
// divides by a span of nothing.
struct SpectralSpan
{
    double start = 0.0, end = 0.0, loopStart = 0.0, loopEnd = 0.0;

    SpectralSpan(const SpectralSettings& settings, int frames) noexcept
    {
        const auto last = static_cast<double>(juce::jmax(0, frames - 1));
        const auto at = [last] (float proportion)
        {
            return static_cast<double>(juce::jlimit(0.0f, 1.0f, proportion)) * last;
        };
        start = at(juce::jmin(settings.start, settings.end));
        end = at(juce::jmax(settings.start, settings.end));
        if (end - start < 1.0) end = juce::jmin(last, start + 1.0);
        if (end - start < 1.0) start = juce::jmax(0.0, end - 1.0);
        loopStart = juce::jlimit(start, end, at(juce::jmin(settings.loopStart, settings.loopEnd)));
        loopEnd = juce::jlimit(start, end, at(juce::jmax(settings.loopStart, settings.loopEnd)));
        if (loopEnd - loopStart < 1.0) loopEnd = juce::jmin(end, loopStart + 1.0);
        if (loopEnd - loopStart < 1.0) loopStart = juce::jmax(start, loopEnd - 1.0);
    }
};

// Move the playhead one hop through the sample, as the loop mode says. This is
// the only place SCAN acts on the voice; the pitch never reaches it.
inline void spectralAdvance(SpectralVoice& voice, const SpectralSettings& settings, int frames) noexcept
{
    const SpectralSpan span(settings, frames);
    if (settings.loopMode == SpectralLoop::manual)
    {
        voice.frame = span.start + static_cast<double>(juce::jlimit(0.0f, 1.0f, settings.position))
                                       * (span.end - span.start);
        return;
    }

    const auto step = static_cast<double>(settings.scan * voice.bounce);
    auto frame = voice.frame + step;
    const auto length = span.loopEnd - span.loopStart;

    if (settings.loopMode == SpectralLoop::oneShot)
    {
        if (frame > span.end || frame < span.start) voice.ended = true;
        voice.frame = juce::jlimit(span.start, span.end, frame);
        return;
    }

    // Arriving at the loop is reaching its far end in the direction of travel:
    // the loop's end going forwards, its start going backwards. A run-in that
    // starts inside the loop is already there for FWD LOOP — nothing is
    // different about the first time round — but REV LOOP and FWD/REV both
    // turn at that far end, so for them the arrival is the turn.
    if (!voice.looping)
    {
        const auto reachedFar = (step > 0.0 && frame >= span.loopEnd)
                             || (step < 0.0 && frame <= span.loopStart);
        const auto inside = frame >= span.loopStart && frame <= span.loopEnd;
        if (reachedFar || (settings.loopMode == SpectralLoop::forward && inside))
        {
            voice.looping = true;
            if (reachedFar && settings.loopMode != SpectralLoop::forward)
            {
                // Turned at the far end, by however far the hop overshot it.
                const auto edge = step > 0.0 ? span.loopEnd : span.loopStart;
                frame = 2.0 * edge - frame;
                voice.bounce = -voice.bounce;
            }
        }
        else
        {
            voice.frame = juce::jlimit(span.start, span.end, frame);
            return;
        }
    }

    if (settings.loopMode == SpectralLoop::pingPong)
    {
        // Reflected off whichever end it crossed. A hop is a few frames at
        // most and the loop is at least one, so this settles in a handful of
        // turns; the bound is there so a pathological setting cannot spin.
        for (int turn = 0; turn < 16 && (frame > span.loopEnd || frame < span.loopStart); ++turn)
        {
            frame = frame > span.loopEnd ? 2.0 * span.loopEnd - frame : 2.0 * span.loopStart - frame;
            voice.bounce = -voice.bounce;
        }
        voice.frame = juce::jlimit(span.loopStart, span.loopEnd, frame);
        return;
    }

    // FWD LOOP and REV LOOP both wrap; which way round is already in bounce.
    frame = span.loopStart + std::fmod(frame - span.loopStart, length);
    if (frame < span.loopStart) frame += length;
    voice.frame = frame;
}

// A fourth-order Butterworth magnitude response, which is what the manual's
// Smooth option asks for at each frequency bound (p. 107). Applied to the
// magnitude of a bin rather than as a filter, because in here there is nothing
// to filter — the spectrum is the signal.
inline float spectralSkirt(float ratio) noexcept
{
    if (ratio <= 0.0f) return 1.0f;
    const auto r4 = ratio * ratio * ratio * ratio;
    const auto r8 = r4 * r4;
    return 1.0f / std::sqrt(1.0f + r8);
}

// The gain one bin keeps, from the frequency bounds and the spectral filter.
//
// CUT is a corner rather than a mask curve: the drawable mask is deferred (see
// SPECTRAL.md) and would multiply in here when it arrives, which is why this is
// a separate function rather than three terms inlined into the bin loop.
inline float spectralBinGain(float normalised, const SpectralSettings& s) noexcept
{
    auto gain = 1.0f;
    if (s.smooth)
    {
        if (s.lo > 0.0f) gain *= spectralSkirt(s.lo / juce::jmax(1.0e-4f, normalised));
        if (s.hi < 1.0f) gain *= spectralSkirt(normalised / juce::jmax(1.0e-4f, s.hi));
    }
    else if (normalised < s.lo || normalised > s.hi)
    {
        return 0.0f;
    }
    return gain;
}

// The hop at the write head is complete once every window that covers it has
// been added in, so it is taken out for spectralRead to hand on and its place
// cleared for the window that will land on it four hops from now.
inline void spectralEmitHop(SpectralVoice& voice) noexcept
{
    constexpr auto mask = spectralFftSize - 1;
    for (int i = 0; i < spectralHop; ++i)
    {
        const auto at = static_cast<size_t>((voice.writeIndex + i) & mask);
        voice.outLeft[static_cast<size_t>(i)] = voice.olaLeft[at];
        voice.outRight[static_cast<size_t>(i)] = voice.olaRight[at];
        voice.olaLeft[at] = 0.0f;
        voice.olaRight[at] = 0.0f;
    }
    voice.writeIndex = (voice.writeIndex + spectralHop) & mask;
    voice.pending = spectralHop;
    voice.readIndex = 0;
}

// One hop: read the spectrogram where the voice has got to, build the stack's
// spectrum from it, transform it once, and overlap-add the result.
//
// `fft` is the Core's, built once at prepare. Nothing here allocates.
inline void spectralSynthesise(SpectralVoice& voice, const Sample& sample,
                               const SpectralSettings& settings, SpectralScratch& scratch,
                               const juce::dsp::FFT& fft) noexcept
{
    const auto frames = sample.frameCount();

    // The first hop puts the playhead on its starting marker: the start going
    // forwards, the end going backwards. MANUAL needs no placing — it is put
    // wherever SCAN says on every hop, this one included.
    if (!voice.running)
    {
        voice.running = true;
        const SpectralSpan span(settings, frames);
        if (settings.loopMode == SpectralLoop::manual)
            voice.frame = span.start + static_cast<double>(juce::jlimit(0.0f, 1.0f, settings.position))
                                           * (span.end - span.start);
        else
            voice.frame = settings.scan < 0.0f ? span.end : span.start;
    }

    // A ONE-SHOT that has run out reads nothing more. The windows already in
    // the overlap-add still drain, so the last of the sample fades over the
    // three hops it was always going to take rather than stopping dead.
    if (voice.ended)
    {
        spectralEmitHop(voice);
        return;
    }

    auto& left = scratch.spectrumLeft;
    auto& right = scratch.spectrumRight;
    left.fill(0.0f);
    right.fill(0.0f);

    const auto here = juce::jlimit(0, frames - 1, static_cast<int>(voice.frame));
    const auto next = juce::jlimit(0, frames - 1, here + 1);
    const auto blendFrames = static_cast<float>(voice.frame - std::floor(voice.frame));
    const auto* magsHere = sample.magnitudes(here);
    const auto* magsNext = sample.magnitudes(next);
    // The pair of frames whose phase difference tells each bin its true
    // frequency, rather than the nominal one its bin centre implies.
    //
    // Normally that is this frame and the one before it. At frame zero there is
    // no frame before it, and taking the difference against itself is not the
    // harmless fallback it looks like: a measured advance of zero makes the
    // unwrap below snap every bin to the nearest whole turn per hop, which
    // quantises the pitch to multiples of four bins. A 440 Hz sample sitting at
    // bin 18.77 comes out at bin 20, which is 468.75 Hz — 110 cents sharp, and
    // audible immediately on a frozen playhead. So the first frame measures
    // forwards instead, which is a real reading rather than an absent one.
    const auto back = here >= 1 ? here - 1 : 0;
    const auto front = here >= 1 ? here : juce::jmin(1, frames - 1);
    const auto* phaseHere = sample.phases(front);
    const auto* phaseBack = sample.phases(back);

    // A transient is where the vocoder's running phase is thrown away and the
    // sample's own is taken instead. Smearing is the price of carrying phase
    // across hops, and at an attack it is the whole of what goes wrong.
    //
    // The first hop a voice takes is a reset as well. Its running phases are
    // whatever the last note left in them, and the attack the sample opens on
    // is exactly the thing that should come out as it was recorded.
    const auto reset = voice.lastFrame < 0
                    || (settings.transients && here != voice.lastFrame
                        && sample.transient(here) > 0.35f);
    voice.lastFrame = here;

    // --- What the hop reads, before anything is shifted ----------------------
    //
    // The magnitude where the playhead is, interpolated between the two frames
    // either side of it, and each bin's true phase advance: what the hop would
    // give it if it sat exactly on its own centre frequency, plus however far
    // the sample says it actually moved.
    auto& magnitude = scratch.magnitude;
    auto& advance = scratch.advance;
    auto loudest = 0.0f;
    for (int bin = 0; bin < spectralBins; ++bin)
    {
        const auto index = static_cast<size_t>(bin);
        magnitude[index] = juce::jmap(blendFrames, magsHere[bin], magsNext[bin]);
        loudest = juce::jmax(loudest, magnitude[index]);
        const auto expected = spectralBinAdvance(bin);
        advance[index] = expected + spectralWrap(phaseHere[bin] - phaseBack[bin] - expected);
    }

    // The peaks, and the region of the spectrum each one owns.
    //
    // A partial is not one bin. The analysis window spreads it across four, and
    // the shape of that spread — its magnitudes, and the phases of the bins
    // either side alternating against the centre — is what makes the window
    // come back out as the window. Shift a partial bin by bin, reading each
    // output bin from wherever b / ratio lands, and that shape is stretched an
    // octave up and crushed an octave down: the window that comes back is the
    // wrong width, the overlap-add no longer sums to one, and the level falls
    // with it — 11 dB an octave down, measured on a real loop. So the spectrum
    // is cut into regions, one per peak, each region is moved whole by a whole
    // number of bins, and every bin in it is locked to its peak's phase
    // (Laroche & Dolson, 1999). The shape is never touched; only where it sits
    // and how fast its peak turns.
    //
    // A peak is louder than both neighbours on either side, which is the test
    // that keeps the sidelobes of one partial from each claiming a region of
    // their own. Anything a hundred decibels under the loudest bin is noise
    // floor, and not worth the regions it would cut.
    const auto floor = loudest * 1.0e-5f;
    auto peaks = 0;
    for (int bin = 1; bin < spectralBins - 1; ++bin)
    {
        const auto at = [&magnitude] (int b)
        {
            return b < 0 || b >= spectralBins ? 0.0f : magnitude[static_cast<size_t>(b)];
        };
        const auto m = at(bin);
        if (m <= floor || m <= at(bin - 1) || m < at(bin + 1)
            || m <= at(bin - 2) || m < at(bin + 2))
            continue;
        scratch.peak[static_cast<size_t>(peaks++)] = bin;
    }
    // A region runs from the quietest bin after the previous peak to the
    // quietest bin before the next one, so two partials close together divide
    // the trough between them where it is actually deepest.
    for (int i = 0; i < peaks; ++i)
    {
        const auto index = static_cast<size_t>(i);
        scratch.regionLow[index] = i == 0 ? 1 : scratch.regionHigh[index - 1] + 1;
        if (i == peaks - 1)
        {
            scratch.regionHigh[index] = spectralBins - 2;
            continue;
        }
        auto trough = scratch.peak[index];
        for (int bin = scratch.peak[index] + 1; bin < scratch.peak[index + 1]; ++bin)
            if (magnitude[static_cast<size_t>(bin)] < magnitude[static_cast<size_t>(trough)]) trough = bin;
        scratch.regionHigh[index] = trough;
    }

    // How many bins one hop's phase advance is worth, which turns a peak's
    // measured advance back into the frequency it is really at.
    constexpr auto binsPerRadian = static_cast<float>(spectralFftSize)
        / (2.0f * juce::MathConstants<float>::pi * static_cast<float>(spectralHop));

    const auto members = juce::jlimit(1, spectralUnisonMax, settings.unison);
    auto power = 0.0f;

    for (int member = 0; member < members; ++member)
    {
        const auto spread = members == 1 ? 0.0f
            : static_cast<float>(member) / static_cast<float>(members - 1) - 0.5f;
        const auto offset = unisonOffset(member, members);
        const auto centreWeight = 1.0f - juce::jmin(1.0f, std::abs(spread) * 2.0f);
        const auto gain = juce::jmap(juce::jlimit(0.0f, 1.0f, settings.blend), centreWeight, 1.0f);
        power += gain * gain;

        // This member's pitch, as a ratio against the sample's own. The shift
        // happens here, in the spectrum, rather than by reading the output
        // faster: each region moves to where its peak's frequency times the
        // ratio lands, and the peak's phase advances by its true frequency
        // times the ratio. That is what keeps one transform per hop whatever
        // note is played, and what leaves SCAN free of the pitch entirely.
        const auto ratio = settings.pitchRatio
            * std::pow(2.0f, offset * juce::jlimit(0.0f, 1.0f, settings.detune)
                                    * unisonSpreadSemitones / 12.0f);
        const auto memberPan = juce::jlimit(-1.0f, 1.0f,
            settings.pan + spread * juce::jlimit(0.0f, 1.0f, settings.detune) * 1.6f);
        const auto panL = sourcePanLeft(memberPan) * gain;
        const auto panR = sourcePanRight(memberPan) * gain;
        auto& phases = voice.synthPhase[static_cast<size_t>(member)];
        auto& nextPhase = scratch.nextPhase;
        nextPhase = phases;
        scratch.claim.fill(-1.0f);

        for (int i = 0; i < peaks; ++i)
        {
            const auto index = static_cast<size_t>(i);
            const auto peak = scratch.peak[index];
            // Where the partial really is, in bins, and how far the whole region
            // has to move for it to land on the note. Rounded, because a region
            // only moves whole: the fraction of a bin left over is carried by
            // the phase advance below, which is what sets the frequency that
            // actually comes out — the bins only have to be near it.
            const auto frequency = advance[static_cast<size_t>(peak)] * binsPerRadian;
            const auto shift = juce::roundToInt(frequency * (ratio - 1.0f));
            const auto target = peak + shift;
            // Above the top of the spectrum there is nothing to write, which is
            // what makes a sample played far above its own pitch quietly run out
            // of top end rather than fold back down.
            if (target < 1 || target > spectralBins - 2) continue;

            const auto peakPhase = spectralWrap(reset
                ? phaseHere[peak]
                : phases[static_cast<size_t>(target)] + advance[static_cast<size_t>(peak)] * ratio);

            for (int bin = scratch.regionLow[index]; bin <= scratch.regionHigh[index]; ++bin)
            {
                const auto out = bin + shift;
                if (out < 1 || out > spectralBins - 2) continue;
                // Every bin of the region carries the peak's phase forward, so
                // whichever of them the partial is nearest next hop continues
                // it — unless a louder region has already landed there.
                const auto source = magnitude[static_cast<size_t>(bin)];
                if (source > scratch.claim[static_cast<size_t>(out)])
                {
                    scratch.claim[static_cast<size_t>(out)] = source;
                    nextPhase[static_cast<size_t>(out)] = peakPhase;
                }
                if (source <= 1.0e-7f) continue;
                const auto normalised = static_cast<float>(out) / static_cast<float>(spectralBins - 1);
                const auto bounded = source * spectralBinGain(normalised, settings);
                // CUT and MIX. The filtered spectrum and the unfiltered one are
                // the same spectrum with two different gains, so the wet/dry
                // blend is done here on the magnitude rather than on two
                // transforms.
                const auto corner = juce::jlimit(0.0f, 1.0f, settings.cut);
                const auto filtered = normalised <= corner
                    ? bounded
                    : bounded * spectralSkirt(normalised / juce::jmax(1.0e-4f, corner));
                const auto shaped = juce::jmap(juce::jlimit(0.0f, 1.0f, settings.mix), bounded, filtered);
                if (shaped <= 1.0e-7f) continue;

                // Locked to the peak: the bin keeps the phase it had against the
                // peak in the analysis, which is the window's own shape.
                const auto phase = peakPhase + phaseHere[bin] - phaseHere[peak];
                const auto re = shaped * std::cos(phase);
                const auto im = shaped * std::sin(phase);
                left[static_cast<size_t>(2 * out)] += re * panL;
                left[static_cast<size_t>(2 * out + 1)] += im * panL;
                right[static_cast<size_t>(2 * out)] += re * panR;
                right[static_cast<size_t>(2 * out + 1)] += im * panR;
            }
        }
        phases = nextPhase;
    }

    // The mirrored half, which a real signal's spectrum has and the inverse
    // transform needs filled in.
    for (int bin = 1; bin < spectralBins - 1; ++bin)
    {
        const auto mirror = spectralFftSize - bin;
        left[static_cast<size_t>(2 * mirror)] = left[static_cast<size_t>(2 * bin)];
        left[static_cast<size_t>(2 * mirror + 1)] = -left[static_cast<size_t>(2 * bin + 1)];
        right[static_cast<size_t>(2 * mirror)] = right[static_cast<size_t>(2 * bin)];
        right[static_cast<size_t>(2 * mirror + 1)] = -right[static_cast<size_t>(2 * bin + 1)];
    }

    fft.performRealOnlyInverseTransform(left.data());
    fft.performRealOnlyInverseTransform(right.data());

    // Power normalisation, the same rule the wavetable stack is held to: a
    // wider stack changes the sound without changing how loud it is.
    const auto scale = spectralGain / std::sqrt(juce::jmax(0.0001f, power));
    const auto mask = spectralFftSize - 1;
    for (int i = 0; i < spectralFftSize; ++i)
    {
        const auto at = static_cast<size_t>((voice.writeIndex + i) & mask);
        const auto windowed = scratch.window[static_cast<size_t>(i)] * scale;
        voice.olaLeft[at] += left[static_cast<size_t>(i)] * windowed;
        voice.olaRight[at] += right[static_cast<size_t>(i)] * windowed;
    }

    spectralEmitHop(voice);

    // And move through the spectrogram, which is the only place SCAN acts.
    spectralAdvance(voice, settings, frames);
}

// One sample out of the vocoder, synthesising another hop when the last one
// runs out. This is what renderSample calls, so it is a branch and two array
// reads on all but one sample in five hundred and twelve.
inline void spectralRead(SpectralVoice& voice, const Sample& sample,
                         const SpectralSettings& settings, SpectralScratch& scratch,
                         const juce::dsp::FFT& fft, float& left, float& right) noexcept
{
    if (sample.isEmpty()) return;
    if (voice.pending <= 0) spectralSynthesise(voice, sample, settings, scratch, fft);
    if (voice.pending <= 0) return;
    left += voice.outLeft[static_cast<size_t>(voice.readIndex)];
    right += voice.outRight[static_cast<size_t>(voice.readIndex)];
    ++voice.readIndex;
    --voice.pending;
}
}
