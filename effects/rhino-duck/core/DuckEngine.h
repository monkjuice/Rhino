#pragma once

#include "DuckCurves.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// The ducking engine: one gain a sample, from a curve read by a clock, applied
// to the whole signal or to the band under a crossover.
//
// The clock has three sources. Sync reads the host's position, so the curve
// repeats every eighth, quarter, half or whole note of the song and the duck
// lands on the beat wherever playback starts; with the host stopped it runs
// free at the host's tempo. MIDI restarts the curve on a note, Audio on a
// transient found in the sidechain input, and either loops the curve or runs
// it once up to the End marker and holds there.
//
// A trigger needs a little future to be click-free. Audio detection takes
// 10 ms of lookahead, and anti-click smoothing leans the gain towards the new
// cycle's start over the 6 ms before it. Both are latency the host is told
// about, and both are nothing in Sync mode. The engine delays the signal by
// that much itself, so a restart lands on the sample it was heard on.
//
// Nothing here allocates after prepare, takes a lock or touches anything but
// its own state and the buffers it is handed.
namespace rhino::duck
{
enum class Trigger { sync = 0, midi = 1, audio = 2 };

constexpr int lengthCount = 4;
inline constexpr std::array<double, lengthCount> lengthInBeats { 0.5, 1.0, 2.0, 4.0 };
inline constexpr std::array<const char*, lengthCount> lengthNames { "1/8", "1/4", "1/2", "1/1" };

constexpr double detectMs = 10.0;      // the Audio trigger's lookahead
constexpr double fadeMs = 6.0;         // anti-click smoothing before a restart
constexpr double detectLeadMs = 1.0;   // how long the detector takes to hear an onset
constexpr double cutMs = 1.0;          // the gain's fastest swing from 1 to 0
constexpr float cutoffMinHz = 20.0f, cutoffMaxHz = 5120.0f;
constexpr int scopeColumns = 256;

struct Settings
{
    float mix = 1.0f;
    int curve = 0;
    CurveAdjust adjust;
    int length = 1;
    int trigger = 0;
    bool loop = true;
    float endMarker = 1.0f;
    bool band = false;
    float cutoffHz = 200.0f;
    bool bandSolo = false;
    bool antiClick = true;
};

struct HostClock
{
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;   // at the block's first sample, in quarter notes
};

// The scope's picture of one cycle: the ducked output, the same signal before
// the duck, and the sidechain, as the lowest and highest sample each column
// saw. Written a sample at a time by the audio thread and read whole by the
// panel.
struct ScopeColumn
{
    float outLow = 0.0f, outHigh = 0.0f, sideLow = 0.0f, sideHigh = 0.0f, dryLow = 0.0f, dryHigh = 0.0f;
};

// What takeDeepestGain answers when no block has run since it last asked.
constexpr float noGainReading = 2.0f;

inline int latencySamples(Trigger trigger, bool antiClick, double sampleRate) noexcept
{
    if (trigger == Trigger::sync) return 0;
    const auto ms = (trigger == Trigger::audio ? detectMs : 0.0) + (antiClick ? fadeMs : 0.0);
    return static_cast<int>(std::lround(ms * sampleRate / 1000.0));
}

// A second-order section in transposed direct form II.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process(float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Butterworth second-order sections, Q of 1/sqrt(2). Two of each in series
// make the fourth-order Linkwitz-Riley crossover, whose low and high halves
// sum back to a flat response.
inline void setLowPass(Biquad& filter, double hz, double sampleRate) noexcept
{
    const auto w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
    const auto c = std::cos(w), s = std::sin(w);
    const auto alpha = s / (2.0 * 0.70710678118654752);
    const auto a0 = 1.0 + alpha;
    filter.b0 = static_cast<float>((1.0 - c) * 0.5 / a0);
    filter.b1 = static_cast<float>((1.0 - c) / a0);
    filter.b2 = filter.b0;
    filter.a1 = static_cast<float>(-2.0 * c / a0);
    filter.a2 = static_cast<float>((1.0 - alpha) / a0);
}

inline void setHighPass(Biquad& filter, double hz, double sampleRate) noexcept
{
    const auto w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
    const auto c = std::cos(w), s = std::sin(w);
    const auto alpha = s / (2.0 * 0.70710678118654752);
    const auto a0 = 1.0 + alpha;
    filter.b0 = static_cast<float>((1.0 + c) * 0.5 / a0);
    filter.b1 = static_cast<float>(-(1.0 + c) / a0);
    filter.b2 = filter.b0;
    filter.a1 = static_cast<float>(-2.0 * c / a0);
    filter.a2 = static_cast<float>((1.0 - alpha) / a0);
}

class Engine
{
public:
    static constexpr int maxChannels = 2;

    // Message thread, never while processing. Sizes the delay lines for the
    // longest latency any mode needs at this rate.
    void prepare(double sampleRate, int /*maximumBlockSize*/)
    {
        rate = sampleRate > 1.0 ? sampleRate : 48000.0;
        const auto longest = latencySamples(Trigger::audio, true, rate) + 8;
        for (auto& line : lines) line.assign(static_cast<size_t>(longest), 0.0f);
        sideLine.assign(static_cast<size_t>(longest), 0.0f);
        reset();
    }

    // Forgets every sound and every trigger. Audio thread, or before any.
    void reset() noexcept
    {
        for (auto& line : lines) std::fill(line.begin(), line.end(), 0.0f);
        std::fill(sideLine.begin(), sideLine.end(), 0.0f);
        writePos = 0;
        for (auto& channel : filters)
            for (auto& filter : channel) filter.reset();
        cutoffFresh = true;
        phase = 0.0;
        freePhase = 0.0;
        running = false;
        armed = false;
        wasPlaying = false;
        gain = 1.0f;
        bandAmount = 0.0f;
        soloAmount = 0.0f;
        fast = slow = 0.0f;
        refractory = 0;
        above = false;
        pendingCount = 0;
        frame = 0;
        lastColumn = -1;
        for (auto& value : scope) value.store(0.0f, std::memory_order_relaxed);
        sweep.store(0.0f, std::memory_order_relaxed);
        gainNow.store(1.0f, std::memory_order_relaxed);
        deepest.store(noGainReading, std::memory_order_relaxed);
        cycles.store(0, std::memory_order_relaxed);
    }

    // One block. `main` is worked in place; `side` is read for the Audio
    // trigger and the scope, and may be null with sideChannels 0.
    // `midiFrames` are the block-relative frames of the notes that trigger,
    // in order, and count only in MIDI mode.
    void process(float* const* main, int mainChannels, const float* const* side, int sideChannels,
                 int numSamples, const Settings& settings, const HostClock& host,
                 const int* midiFrames, int midiCount) noexcept
    {
        const auto trigger = static_cast<Trigger>(std::clamp(settings.trigger, 0, 2));
        const auto latency = latencySamples(trigger, settings.antiClick, rate);
        const auto fadeFrames = settings.antiClick ? ms(fadeMs) : 0;
        const auto lead = ms(detectLeadMs);
        const auto beats = lengthInBeats[static_cast<size_t>(std::clamp(settings.length, 0, lengthCount - 1))];
        const auto bpm = host.bpm > 1.0 ? host.bpm : 120.0;
        const auto ppqPerSample = bpm / 60.0 / rate;
        const auto phasePerSample = ppqPerSample / beats;
        const auto endAt = static_cast<double>(std::clamp(settings.endMarker, 0.0f, 1.0f));
        const auto curve = clampCurve(settings.curve);
        const auto mix = std::clamp(settings.mix, 0.0f, 1.0f);
        const auto gainStep = static_cast<float>(1.0 / std::max(1.0, cutMs * rate / 1000.0));
        const auto startGain = curveGain(curve, 0.0f, settings.adjust);
        const auto bandTarget = settings.band ? 1.0f : 0.0f;
        const auto soloTarget = settings.band && settings.bandSolo ? 1.0f : 0.0f;
        const auto switchCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.010 * rate)));
        const auto channels = std::min(mainChannels, maxChannels);
        const auto lineLength = static_cast<int>(sideLine.size());
        if (lineLength <= latency) return;

        updateCutoff(settings.cutoffHz, numSamples);

        if (trigger == Trigger::sync)
        {
            // Stopping hands the free clock the phase the host left it at,
            // so the pump carries on from where it was rather than jumping.
            if (!host.playing && wasPlaying) freePhase = phase;
            wasPlaying = host.playing;
        }

        auto midiIndex = 0;
        auto deepestHere = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            // Triggers heard on this frame restart the cycle `latency` frames
            // on, when the sample they were heard with leaves the delay line.
            if (trigger == Trigger::midi)
                while (midiIndex < midiCount && midiFrames[midiIndex] <= i)
                {
                    schedule(frame + latency);
                    ++midiIndex;
                }

            auto sideNow = 0.0f;
            for (int channel = 0; channel < sideChannels && channel < maxChannels; ++channel)
                sideNow += side[channel][i];
            if (sideChannels > 1) sideNow *= 0.5f;
            if (trigger == Trigger::audio && detect(sideNow)) schedule(frame + latency - lead);

            // Where the cycle is on this output frame.
            if (trigger == Trigger::sync)
            {
                if (host.playing)
                {
                    phase = (host.ppq + i * ppqPerSample) / beats;
                    phase -= std::floor(phase);
                }
                else
                {
                    freePhase += phasePerSample;
                    freePhase -= std::floor(freePhase);
                    phase = freePhase;
                }
                if (!running || phase < lastPhase) cycles.fetch_add(1, std::memory_order_relaxed);
                running = armed = true;
            }
            else if (due())
            {
                phase = 0.0;
                running = armed = true;
                lastColumn = -1;
                cycles.fetch_add(1, std::memory_order_relaxed);
            }
            else if (running)
            {
                phase += phasePerSample;
                if (settings.loop)
                {
                    if (phase >= 1.0)
                    {
                        phase -= 1.0;
                        cycles.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else if (phase >= endAt)
                {
                    // Frozen just short of the cycle's end when End is at
                    // 100%: a phase of exactly 1 is the next cycle's start.
                    phase = std::min(endAt, 1.0 - 1.0e-6);
                    running = false;
                }
            }
            lastPhase = phase;

            auto target = armed ? curveGain(curve, static_cast<float>(phase), settings.adjust) : 1.0f;
            // Anti-click: through the fade before a restart, lean towards the
            // gain the new cycle starts at, so the restart lands on it.
            if (fadeFrames > 0 && trigger != Trigger::sync && pendingCount > 0)
            {
                const auto until = nextRestart - frame;
                if (until >= 0 && until < fadeFrames)
                    target += (startGain - target) * (1.0f - static_cast<float>(until) / static_cast<float>(fadeFrames));
            }
            const auto wanted = 1.0f - mix * (1.0f - target);
            gain += std::clamp(wanted - gain, -gainStep, gainStep);
            deepestHere = std::min(deepestHere, gain);

            bandAmount += (bandTarget - bandAmount) * switchCoeff;
            soloAmount += (soloTarget - soloAmount) * switchCoeff;

            const auto readPos = (writePos - latency + lineLength) % lineLength;
            auto outLow = 1.0e9f, outHigh = -1.0e9f, dryLow = 1.0e9f, dryHigh = -1.0e9f;
            for (int channel = 0; channel < channels; ++channel)
            {
                auto& line = lines[static_cast<size_t>(channel)];
                line[static_cast<size_t>(writePos)] = main[channel][i];
                const auto x = line[static_cast<size_t>(readPos)];
                dryLow = std::min(dryLow, x);
                dryHigh = std::max(dryHigh, x);
                // The crossover runs whether or not the band is on, so turning
                // it on meets warm filters rather than a transient.
                auto& stage = filters[static_cast<size_t>(channel)];
                const auto low = stage[1].process(stage[0].process(x));
                const auto high = stage[3].process(stage[2].process(x));
                const auto plain = x * gain;
                auto y = plain + ((low * gain + high) - plain) * bandAmount;
                y += (low * gain - y) * soloAmount;
                main[channel][i] = y;
                outLow = std::min(outLow, y);
                outHigh = std::max(outHigh, y);
            }
            sideLine[static_cast<size_t>(writePos)] = sideNow;
            const auto sideShown = sideLine[static_cast<size_t>(readPos)];
            writePos = (writePos + 1) % lineLength;

            if (running && channels > 0) paintScope({ outLow, outHigh, sideShown, sideShown, dryLow, dryHigh });
            ++frame;
        }
        sweep.store(static_cast<float>(phase), std::memory_order_relaxed);
        gainNow.store(gain, std::memory_order_relaxed);
        // The deepest gain since the meter last took it. A take landing
        // between this load and store loses nothing worse than one block's
        // figure to the next frame.
        deepest.store(std::min(deepest.load(std::memory_order_relaxed), deepestHere), std::memory_order_relaxed);
    }

    // ---- what the panel reads, from any thread -----------------------------
    float sweepPhase() const noexcept { return sweep.load(std::memory_order_relaxed); }
    float currentGain() const noexcept { return gainNow.load(std::memory_order_relaxed); }
    // The deepest gain the duck applied, Mix included, since the last take:
    // what the meter shows, so a cut shorter than a frame is never missed.
    // noGainReading when no block has run since. Only the meter takes it.
    float takeDeepestGain() const noexcept { return deepest.exchange(noGainReading, std::memory_order_relaxed); }
    // How many cycles have started, so a picture can tell a new one.
    int cycleCount() const noexcept { return cycles.load(std::memory_order_relaxed); }
    bool isArmed() const noexcept { return armed; }

    ScopeColumn scopeColumn(int column) const noexcept
    {
        const auto base = static_cast<size_t>(std::clamp(column, 0, scopeColumns - 1)) * scopeFields;
        const auto at = [this, base] (size_t field) { return scope[base + field].load(std::memory_order_relaxed); };
        return { at(0), at(1), at(2), at(3), at(4), at(5) };
    }

private:
    int ms(double milliseconds) const noexcept { return static_cast<int>(std::lround(milliseconds * rate / 1000.0)); }

    // The crossover follows its cutoff over about 20 ms, recomputed per block.
    // The first block after a reset lands on it at once, so a static setting
    // renders the same in blocks of any size.
    void updateCutoff(float targetHz, int numSamples) noexcept
    {
        const auto target = static_cast<double>(std::clamp(targetHz, cutoffMinHz, cutoffMaxHz));
        if (cutoffFresh)
        {
            cutoffNow = target;
            cutoffFresh = false;
        }
        else
        {
            cutoffNow += (target - cutoffNow) * (1.0 - std::exp(-numSamples / (0.020 * rate)));
        }
        if (std::abs(cutoffNow - cutoffSet) > cutoffSet * 1.0e-4)
        {
            cutoffSet = cutoffNow;
            for (auto& stage : filters)
            {
                setLowPass(stage[0], cutoffSet, rate);
                setLowPass(stage[1], cutoffSet, rate);
                setHighPass(stage[2], cutoffSet, rate);
                setHighPass(stage[3], cutoffSet, rate);
            }
        }
    }

    // An onset in the sidechain: a fast follower overtaking a slow one by
    // 6 dB above a floor, once per 50 ms, re-armed once it has fallen back.
    bool detect(float sample) noexcept
    {
        const auto level = std::abs(sample);
        fast += (level - fast) * (level > fast ? fastAttack() : fastRelease());
        slow += (level - slow) * (level > slow ? slowAttack() : slowRelease());
        if (refractory > 0) --refractory;
        const auto loud = fast > 0.01f && fast > slow * 2.0f;
        auto fired = false;
        if (loud && !above && refractory == 0)
        {
            fired = true;
            refractory = ms(50.0);
        }
        if (loud) above = true;
        else if (fast < slow * 1.3f) above = false;
        return fired;
    }

    float fastAttack() const noexcept { return coefficient(0.0002); }
    float fastRelease() const noexcept { return coefficient(0.020); }
    float slowAttack() const noexcept { return coefficient(0.010); }
    float slowRelease() const noexcept { return coefficient(0.150); }
    float coefficient(double seconds) const noexcept { return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * rate))); }

    void schedule(std::int64_t at) noexcept
    {
        if (pendingCount >= static_cast<int>(pending.size())) return;
        pending[static_cast<size_t>(pendingCount++)] = at;
        nextRestart = pendingCount == 1 ? at : std::min(nextRestart, at);
    }

    // Whether a restart falls on this frame; takes it off the list.
    bool due() noexcept
    {
        if (pendingCount == 0 || nextRestart > frame) return false;
        auto kept = 0;
        for (int i = 0; i < pendingCount; ++i)
            if (pending[static_cast<size_t>(i)] > frame) pending[static_cast<size_t>(kept++)] = pending[static_cast<size_t>(i)];
        pendingCount = kept;
        nextRestart = frame;
        for (int i = 0; i < pendingCount; ++i)
            nextRestart = i == 0 ? pending[0] : std::min(nextRestart, pending[static_cast<size_t>(i)]);
        return true;
    }

    // A new column starts from this sample; the same column widens to take
    // it in, each low field taking the lower and each high the higher.
    void paintScope(const ScopeColumn& sample) noexcept
    {
        const auto column = std::clamp(static_cast<int>(phase * scopeColumns), 0, scopeColumns - 1);
        const auto base = static_cast<size_t>(column) * scopeFields;
        const std::array<float, scopeFields> values { sample.outLow, sample.outHigh, sample.sideLow, sample.sideHigh,
                                                      sample.dryLow, sample.dryHigh };
        const auto fresh = column != lastColumn;
        lastColumn = column;
        for (size_t field = 0; field < scopeFields; ++field)
        {
            auto& slot = scope[base + field];
            const auto held = slot.load(std::memory_order_relaxed);
            const auto low = field % 2 == 0;
            slot.store(fresh ? values[field] : (low ? std::min(held, values[field]) : std::max(held, values[field])),
                       std::memory_order_relaxed);
        }
    }

    double rate = 48000.0;
    std::array<std::vector<float>, maxChannels> lines;
    std::vector<float> sideLine;
    int writePos = 0;
    std::array<std::array<Biquad, 4>, maxChannels> filters;   // low 1, low 2, high 1, high 2
    double cutoffNow = 200.0, cutoffSet = 0.0;
    bool cutoffFresh = true;

    double phase = 0.0, lastPhase = 0.0, freePhase = 0.0;
    bool running = false, armed = false, wasPlaying = false;
    float gain = 1.0f, bandAmount = 0.0f, soloAmount = 0.0f;

    float fast = 0.0f, slow = 0.0f;
    int refractory = 0;
    bool above = false;

    std::array<std::int64_t, 8> pending {};
    int pendingCount = 0;
    std::int64_t nextRestart = 0;
    std::int64_t frame = 0;

    static constexpr size_t scopeFields = 6;   // the fields of a ScopeColumn, in order
    int lastColumn = -1;
    std::array<std::atomic<float>, scopeColumns * scopeFields> scope {};
    std::atomic<float> sweep { 0.0f }, gainNow { 1.0f };
    // Taken, and so written, from a const reader: the meter's exchange.
    mutable std::atomic<float> deepest { noGainReading };
    std::atomic<int> cycles { 0 };
};
}
