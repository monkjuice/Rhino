#include "DjAnalysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// How a deck reads its material: see DjAnalysis.h.
//
// The tempo comes from the onset envelope - how much louder each band gets
// from one column to the next - by autocorrelation with a mild preference for
// dance tempos, then a comb search that settles the period to a hundredth of
// a beat per minute and finds the phase the beats sit on. The downbeat is the
// beat of the four that the low band hits hardest. Drops are bars that come
// back loud after a quieter passage. The key is a chroma profile against the
// Krumhansl-Schmuckler key profiles.

namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int columnFrames = DjAnalysis::columnFrames;
constexpr int onsetBands = 4;
constexpr int keyFftOrder = 12;
constexpr int keyFftSize = 1 << keyFftOrder;

// A one-pole low-pass applied twice, which is enough of an edge to tell a
// kick from a hat without the cost of a real crossover on every frame of a
// ten-minute file.
struct TwoPoleLowPass
{
    float a = 0.0f, s1 = 0.0f, s2 = 0.0f;
    void set(double cutoff, double rate)
    {
        a = static_cast<float>(1.0 - std::exp(-2.0 * pi * cutoff / rate));
    }
    float process(float x) noexcept
    {
        s1 += a * (x - s1);
        s2 += a * (s1 - s2);
        return s2;
    }
};

// Iterative radix-2 transform, as PitchTracker carries, because RhinoCore
// depends on nothing but juce_core.
struct Fft
{
    int n;
    std::vector<float> cosTable, sinTable;
    std::vector<int> reversed;

    explicit Fft(int size) : n(size), cosTable(static_cast<size_t>(size / 2)), sinTable(static_cast<size_t>(size / 2)),
                             reversed(static_cast<size_t>(size))
    {
        for (int i = 0; i < n / 2; ++i)
        {
            cosTable[static_cast<size_t>(i)] = static_cast<float>(std::cos(2.0 * pi * i / n));
            sinTable[static_cast<size_t>(i)] = static_cast<float>(-std::sin(2.0 * pi * i / n));
        }
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            reversed[static_cast<size_t>(i)] = r;
        }
    }

    void transform(std::vector<float>& re, std::vector<float>& im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const auto j = reversed[static_cast<size_t>(i)];
            if (j > i)
            {
                std::swap(re[static_cast<size_t>(i)], re[static_cast<size_t>(j)]);
                std::swap(im[static_cast<size_t>(i)], im[static_cast<size_t>(j)]);
            }
        }
        for (int span = 2; span <= n; span *= 2)
        {
            const auto step = n / span;
            for (int start = 0; start < n; start += span)
                for (int k = 0; k < span / 2; ++k)
                {
                    const auto twiddle = static_cast<size_t>(k * step);
                    const auto wr = cosTable[twiddle];
                    const auto wi = sinTable[twiddle];
                    const auto a = static_cast<size_t>(start + k);
                    const auto b = static_cast<size_t>(start + k + span / 2);
                    const auto tr = re[b] * wr - im[b] * wi;
                    const auto ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
    }
};

bool cancelled(const DjAnalysisOptions& options)
{
    return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed);
}

// The scan: one pass over the audio filling the waveform columns and, per
// column, the energy of four bands for the onset detector and the energy of
// the whole for the bar levels.
struct Scan
{
    std::vector<DjWaveformColumn> columns;
    std::vector<float> bandEnergy[onsetBands];
    std::vector<float> energy;
};

Scan scan(const float* left, const float* right, int frames, double rate, const DjAnalysisOptions& options)
{
    Scan result;
    const auto columnCount = (frames + columnFrames - 1) / columnFrames;
    result.columns.resize(static_cast<size_t>(columnCount));
    for (auto& band : result.bandEnergy)
        band.assign(static_cast<size_t>(columnCount), 0.0f);
    result.energy.assign(static_cast<size_t>(columnCount), 0.0f);

    TwoPoleLowPass low, lowMid, mid;
    low.set(180.0, rate);
    lowMid.set(600.0, rate);
    mid.set(2500.0, rate);
    for (int column = 0; column < columnCount; ++column)
    {
        if ((column & 1023) == 0 && cancelled(options))
            return result;
        const auto start = column * columnFrames;
        const auto end = std::min(frames, start + columnFrames);
        float peakLow = 0.0f, peakMid = 0.0f, peakHigh = 0.0f;
        double sumBand[onsetBands] {0.0, 0.0, 0.0, 0.0};
        double sumAll = 0.0;
        for (int i = start; i < end; ++i)
        {
            const auto mono = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            const auto l = low.process(mono);
            const auto lm = lowMid.process(mono);
            const auto m = mid.process(mono);
            const auto displayHigh = mono - m;
            const auto displayMid = m - l;
            peakLow = std::max(peakLow, std::abs(l));
            peakMid = std::max(peakMid, std::abs(displayMid));
            peakHigh = std::max(peakHigh, std::abs(displayHigh));
            const float bands[onsetBands] {l, lm - l, m - lm, displayHigh};
            for (int b = 0; b < onsetBands; ++b)
                sumBand[b] += static_cast<double>(bands[b]) * bands[b];
            sumAll += static_cast<double>(mono) * mono;
        }
        const auto count = std::max(1, end - start);
        auto& out = result.columns[static_cast<size_t>(column)];
        out.low = std::min(1.0f, peakLow);
        out.mid = std::min(1.0f, peakMid);
        out.high = std::min(1.0f, peakHigh);
        for (int b = 0; b < onsetBands; ++b)
            result.bandEnergy[b][static_cast<size_t>(column)] = static_cast<float>(sumBand[b] / count);
        result.energy[static_cast<size_t>(column)] = static_cast<float>(sumAll / count);
    }
    return result;
}

// How much louder each band got since the last column, summed: the onset
// strength. Log-compressed so a quiet hat counts beside a loud kick.
//
// Over `smoothColumns` trailing columns, when asked. A column is shorter
// than one cycle of a kick, so a column's own energy rises and falls with
// the phase of the sine, and every rise counted as an onset: the ripple
// correlated with itself every 0.4 beats and buried the beat. Averaged over
// two cycles of the lowest bass the ripple is gone, and the attack still
// first shows in the column it lands in, which is what the phase is read
// from with the unsmoothed flux.
std::vector<float> onsetFlux(const Scan& scanned, int smoothColumns, std::vector<float>* lowBandFlux)
{
    const auto count = static_cast<int>(scanned.energy.size());
    std::vector<float> flux(static_cast<size_t>(count), 0.0f);
    if (lowBandFlux != nullptr)
        lowBandFlux->assign(static_cast<size_t>(count), 0.0f);
    const auto span = std::max(1, smoothColumns);
    float previous[onsetBands] {0.0f, 0.0f, 0.0f, 0.0f};
    double window[onsetBands] {0.0, 0.0, 0.0, 0.0};
    for (int t = 0; t < count; ++t)
    {
        float total = 0.0f;
        for (int b = 0; b < onsetBands; ++b)
        {
            window[b] += scanned.bandEnergy[b][static_cast<size_t>(t)];
            if (t - span >= 0)
                window[b] -= scanned.bandEnergy[b][static_cast<size_t>(t - span)];
            const auto energy = window[b] / std::min(span, t + 1);
            const auto level = std::log1p(1000.0f * static_cast<float>(std::max(0.0, energy)));
            const auto rise = std::max(0.0f, level - previous[b]);
            previous[b] = level;
            total += rise;
            if (b == 0 && lowBandFlux != nullptr)
                (*lowBandFlux)[static_cast<size_t>(t)] = rise;
        }
        flux[static_cast<size_t>(t)] = total;
    }
    return flux;
}

// With its local mean taken out, so a sustained passage reads as nothing
// happening: a one-second moving mean, subtracted.
std::vector<float> removeMean(const std::vector<float>& flux, double rate)
{
    const auto count = static_cast<int>(flux.size());
    const auto half = std::max(1, static_cast<int>(rate / columnFrames / 2.0));
    std::vector<float> onset(static_cast<size_t>(count), 0.0f);
    double window = 0.0;
    int inWindow = 0;
    for (int t = 0; t < std::min(count, half); ++t)
    {
        window += flux[static_cast<size_t>(t)];
        ++inWindow;
    }
    for (int t = 0; t < count; ++t)
    {
        if (t + half < count)
        {
            window += flux[static_cast<size_t>(t + half)];
            ++inWindow;
        }
        if (t - half - 1 >= 0)
        {
            window -= flux[static_cast<size_t>(t - half - 1)];
            --inWindow;
        }
        onset[static_cast<size_t>(t)] = flux[static_cast<size_t>(t)] - static_cast<float>(window / std::max(1, inWindow));
    }
    return onset;
}

double bpmForLag(double lag, double rate)
{
    return 60.0 * rate / (columnFrames * lag);
}

double lagForBpm(double bpm, double rate)
{
    return 60.0 * rate / (columnFrames * bpm);
}

// A mild preference for the tempos dance music is written at: a log-Gaussian
// around 128, wide enough that a 90 or a 174 still wins when it is there.
double tempoPrior(double bpm)
{
    const auto octaves = std::log2(bpm / 128.0) / 0.95;
    return std::exp(-0.5 * octaves * octaves);
}

// How well a comb of period `period` columns lines up with the onsets: the
// share of onset strength falling into the best of a ring of phase bins.
// Also hands back the phase of that bin, as a fraction of the period.
double combScore(const std::vector<float>& positive, double period, int bins, double* phase)
{
    std::vector<double> ring(static_cast<size_t>(bins), 0.0);
    double total = 0.0;
    for (size_t t = 0; t < positive.size(); ++t)
    {
        const auto value = positive[t];
        if (value <= 0.0f) continue;
        const auto fraction = std::fmod(static_cast<double>(t), period) / period;
        auto bin = static_cast<int>(fraction * bins);
        if (bin >= bins) bin = bins - 1;
        ring[static_cast<size_t>(bin)] += value;
        total += value;
    }
    if (total <= 0.0)
        return 0.0;
    double best = -1.0;
    int bestBin = 0;
    for (int i = 0; i < bins; ++i)
    {
        const auto before = ring[static_cast<size_t>((i + bins - 1) % bins)];
        const auto after = ring[static_cast<size_t>((i + 1) % bins)];
        const auto score = ring[static_cast<size_t>(i)] + 0.5 * (before + after);
        if (score > best)
        {
            best = score;
            bestBin = i;
        }
    }
    if (phase != nullptr)
    {
        // The centroid of the peak bin and its neighbours, so the phase is
        // finer than a bin.
        const auto before = ring[static_cast<size_t>((bestBin + bins - 1) % bins)];
        const auto here = ring[static_cast<size_t>(bestBin)];
        const auto after = ring[static_cast<size_t>((bestBin + 1) % bins)];
        const auto offset = (after - before) / std::max(1.0e-9, before + here + after);
        auto centre = (bestBin + 0.5 + offset) / bins;
        if (centre < 0.0) centre += 1.0;
        if (centre >= 1.0) centre -= 1.0;
        *phase = centre;
    }
    return best / total;
}

struct Tempo
{
    double bpm = 0.0;
    double periodColumns = 0.0;
    double phaseColumns = 0.0;   // where the first beat at or after zero sits
    double confidence = 0.0;
};

Tempo detectTempo(const std::vector<float>& onset, const std::vector<float>& sharpFlux, double rate,
                  const DjAnalysisOptions& options)
{
    Tempo tempo;
    const auto count = static_cast<int>(onset.size());
    const auto minLag = static_cast<int>(std::floor(lagForBpm(200.0, rate)));
    const auto maxLag = static_cast<int>(std::ceil(lagForBpm(60.0, rate)));
    // Two bars of audio at the slowest tempo, or there is nothing to count.
    if (count < maxLag * 4 || minLag < 2)
        return tempo;
    const auto longestLag = std::min(count - 1, maxLag * 2);
    std::vector<double> correlation(static_cast<size_t>(longestLag + 1), 0.0);
    for (int lag = 0; lag <= longestLag; ++lag)
    {
        if ((lag & 15) == 0 && cancelled(options))
            return tempo;
        double sum = 0.0;
        const auto n = count - lag;
        for (int t = 0; t < n; ++t)
            sum += static_cast<double>(onset[static_cast<size_t>(t)]) * onset[static_cast<size_t>(t + lag)];
        correlation[static_cast<size_t>(lag)] = sum / n;
    }
    const auto zero = correlation[0];
    if (zero <= 0.0)
        return tempo;
    const auto at = [&correlation, longestLag](double lag)
    {
        if (lag < 0.0 || lag > longestLag) return 0.0;
        const auto below = static_cast<int>(std::floor(lag));
        const auto above = std::min(longestLag, below + 1);
        const auto fraction = lag - below;
        return correlation[static_cast<size_t>(below)] * (1.0 - fraction)
             + correlation[static_cast<size_t>(above)] * fraction;
    };
    // Each lag scored with its octaves, so a half-time or double-time
    // reading has to beat the plain one rather than merely tie it.
    double bestScore = -1.0;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const auto score = (at(lag) + 0.5 * at(2.0 * lag) + 0.25 * at(0.5 * lag)) / zero
                         * tempoPrior(bpmForLag(lag, rate));
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }
    if (bestLag <= 0)
        return tempo;
    // A parabola through the peak and its neighbours puts the lag between
    // columns.
    auto lag = static_cast<double>(bestLag);
    {
        const auto before = at(bestLag - 1.0), here = at(bestLag), after = at(bestLag + 1.0);
        const auto denominator = before - 2.0 * here + after;
        if (std::abs(denominator) > 1.0e-12)
            lag += 0.5 * (before - after) / denominator;
    }
    const auto coarseBpm = bpmForLag(lag, rate);
    // Everything the comb search needs is the onsets that rose, not the
    // ones that fell.
    std::vector<float> positive(onset.size());
    for (size_t t = 0; t < onset.size(); ++t)
        positive[t] = std::max(0.0f, onset[t]);
    // Two passes: three per cent either side in steps of a twentieth of a per
    // cent, then a tenth of a beat per minute either side in thousandths.
    auto best = coarseBpm;
    auto bestComb = -1.0;
    const auto search = [&](double centre, double halfWidth, double step)
    {
        for (auto candidate = centre - halfWidth; candidate <= centre + halfWidth + 1.0e-9; candidate += step)
        {
            if (candidate < 50.0 || candidate > 220.0) continue;
            const auto score = combScore(positive, lagForBpm(candidate, rate), 32, nullptr);
            if (score > bestComb)
            {
                bestComb = score;
                best = candidate;
            }
        }
    };
    search(coarseBpm, coarseBpm * 0.03, coarseBpm * 0.0005);
    if (cancelled(options))
        return tempo;
    search(best, 0.1, 0.005);
    tempo.bpm = best;
    tempo.periodColumns = lagForBpm(best, rate);
    double phase = 0.0;
    combScore(positive, tempo.periodColumns, 256, &phase);
    // The smoothed envelope's rise is spread over its window, so the phase
    // it gives is late by part of that window. The unsmoothed flux spikes
    // in the column each attack lands in: the offset near the estimate
    // that gathers most of it is where the beats are.
    auto phaseColumns = phase * tempo.periodColumns;
    {
        const auto count = static_cast<int>(sharpFlux.size());
        double bestSum = -1.0;
        auto bestOffset = 0.0;
        for (int offset = -12; offset <= 4; ++offset)
        {
            double sum = 0.0;
            for (auto column = phaseColumns + offset; column < count; column += tempo.periodColumns)
            {
                const auto index = static_cast<int>(std::lround(column));
                if (index >= 0 && index < count)
                    sum += sharpFlux[static_cast<size_t>(index)];
            }
            if (sum > bestSum)
            {
                bestSum = sum;
                bestOffset = offset;
            }
        }
        phaseColumns += bestOffset;
        while (phaseColumns < 0.0) phaseColumns += tempo.periodColumns;
        while (phaseColumns >= tempo.periodColumns) phaseColumns -= tempo.periodColumns;
    }
    tempo.phaseColumns = phaseColumns;
    tempo.confidence = std::clamp(bestScore / 1.75, 0.0, 1.0);
    return tempo;
}

// Which of the four beats is the one: the beat the low band hits hardest,
// counted across the whole file.
int downbeatOffset(const std::vector<float>& lowFlux, double firstBeatColumns, double periodColumns, int beatsPerBar)
{
    const auto count = static_cast<int>(lowFlux.size());
    if (count == 0 || periodColumns <= 0.0)
        return 0;
    double best = -1.0;
    int bestOffset = 0;
    for (int offset = 0; offset < beatsPerBar; ++offset)
    {
        double sum = 0.0;
        for (auto column = firstBeatColumns + offset * periodColumns; column < count;
             column += periodColumns * beatsPerBar)
        {
            const auto index = static_cast<int>(std::lround(column));
            for (int near = index - 1; near <= index + 1; ++near)
                if (near >= 0 && near < count)
                    sum += lowFlux[static_cast<size_t>(near)];
        }
        if (sum > best)
        {
            best = sum;
            bestOffset = offset;
        }
    }
    return bestOffset;
}

void measureBars(DjAnalysis& analysis, const Scan& scanned, double rate)
{
    analysis.barEnergy.clear();
    analysis.drops.clear();
    if (!analysis.hasGrid() || scanned.energy.empty())
        return;
    const auto columnsPerBar = analysis.secondsPerBeat() * analysis.beatsPerBar * rate / columnFrames;
    if (columnsPerBar < 1.0)
        return;
    const auto count = static_cast<double>(scanned.energy.size());
    const auto firstColumn = analysis.firstBeatSeconds * rate / columnFrames;
    std::vector<float> rms;
    for (auto start = firstColumn; start + columnsPerBar * 0.5 < count; start += columnsPerBar)
    {
        const auto from = std::max(0, static_cast<int>(std::floor(start)));
        const auto to = std::min(static_cast<int>(count), static_cast<int>(std::floor(start + columnsPerBar)));
        double sum = 0.0;
        int n = 0;
        for (int column = from; column < to; ++column)
        {
            sum += scanned.energy[static_cast<size_t>(column)];
            ++n;
        }
        rms.push_back(n > 0 ? static_cast<float>(std::sqrt(sum / n)) : 0.0f);
    }
    if (rms.empty())
        return;
    const auto loudest = *std::max_element(rms.begin(), rms.end());
    if (loudest <= 0.0f)
    {
        analysis.barEnergy.assign(rms.size(), 0.0f);
        return;
    }
    analysis.barEnergy.resize(rms.size());
    for (size_t bar = 0; bar < rms.size(); ++bar)
        analysis.barEnergy[bar] = rms[bar] / loudest;
    // A drop: a bar that is loud, half again as loud as the four before it,
    // and plainly louder than the one just before. Eight bars apart at least,
    // because a drop is a phrase coming in rather than a fill.
    for (int bar = 4; bar < static_cast<int>(rms.size()); ++bar)
    {
        const auto here = analysis.barEnergy[static_cast<size_t>(bar)];
        float before = 0.0f;
        for (int b = bar - 4; b < bar; ++b)
            before += analysis.barEnergy[static_cast<size_t>(b)];
        before /= 4.0f;
        if (here >= 0.6f && here > 1.5f * before && analysis.barEnergy[static_cast<size_t>(bar - 1)] < 0.75f * here
            && (analysis.drops.empty() || bar - analysis.drops.back() >= 8))
            analysis.drops.push_back(bar);
    }
}

int detectKey(const float* left, const float* right, int frames, double rate, const DjAnalysisOptions& options)
{
    if (frames < keyFftSize * 2)
        return -1;
    const Fft fft(keyFftSize);
    std::vector<float> re(keyFftSize), im(keyFftSize), window(keyFftSize);
    for (int i = 0; i < keyFftSize; ++i)
        window[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * i / keyFftSize));
    double chroma[12] {};
    const auto hop = keyFftSize * 2;
    const auto lowestBin = std::max(1, static_cast<int>(55.0 * keyFftSize / rate));
    const auto highestBin = std::min(keyFftSize / 2 - 1, static_cast<int>(2200.0 * keyFftSize / rate));
    for (int start = 0; start + keyFftSize <= frames; start += hop)
    {
        if (cancelled(options))
            return -1;
        for (int i = 0; i < keyFftSize; ++i)
        {
            const auto mono = right != nullptr ? 0.5f * (left[start + i] + right[start + i]) : left[start + i];
            re[static_cast<size_t>(i)] = mono * window[static_cast<size_t>(i)];
            im[static_cast<size_t>(i)] = 0.0f;
        }
        fft.transform(re, im);
        for (int bin = lowestBin; bin <= highestBin; ++bin)
        {
            const auto magnitude = std::sqrt(re[static_cast<size_t>(bin)] * re[static_cast<size_t>(bin)]
                                             + im[static_cast<size_t>(bin)] * im[static_cast<size_t>(bin)]);
            if (magnitude <= 0.0f) continue;
            const auto frequency = bin * rate / keyFftSize;
            const auto note = 69.0 + 12.0 * std::log2(frequency / 440.0);
            const auto pitchClass = ((static_cast<int>(std::lround(note)) % 12) + 12) % 12;
            // Lower partials weigh more: the fundamental says the key, and
            // the overtones above it mostly say the fifth.
            chroma[pitchClass] += magnitude / std::sqrt(frequency / 55.0);
        }
    }
    double total = 0.0;
    for (const auto value : chroma) total += value;
    if (total <= 0.0)
        return -1;
    static constexpr double major[12] {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr double minor[12] {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    const auto correlate = [&chroma](const double* profile, int root)
    {
        double meanChroma = 0.0, meanProfile = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            meanChroma += chroma[i];
            meanProfile += profile[i];
        }
        meanChroma /= 12.0;
        meanProfile /= 12.0;
        double numerator = 0.0, chromaVariance = 0.0, profileVariance = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const auto a = chroma[i] - meanChroma;
            const auto b = profile[(i - root + 12) % 12] - meanProfile;
            numerator += a * b;
            chromaVariance += a * a;
            profileVariance += b * b;
        }
        const auto denominator = std::sqrt(chromaVariance * profileVariance);
        return denominator > 0.0 ? numerator / denominator : -1.0;
    };
    double best = -2.0;
    int bestKey = -1;
    for (int root = 0; root < 12; ++root)
    {
        const auto majorScore = correlate(major, root);
        if (majorScore > best)
        {
            best = majorScore;
            bestKey = root;
        }
        const auto minorScore = correlate(minor, root);
        if (minorScore > best)
        {
            best = minorScore;
            bestKey = 12 + root;
        }
    }
    return bestKey;
}
}

juce::String DjAnalysis::keyName(int key)
{
    static constexpr const char* names[12] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (key < 0 || key >= 24)
        return "-";
    return juce::String(names[key % 12]) + (key >= 12 ? "m" : "");
}

juce::String DjAnalysis::camelotName(int key)
{
    if (key < 0 || key >= 24)
        return "-";
    // The wheel's numbers for the majors, C to B; a minor shares its relative
    // major's number and takes an A instead of a B.
    static constexpr int majorNumbers[12] {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
    const auto major = key < 12;
    const auto root = major ? key % 12 : (key % 12 + 3) % 12;
    return juce::String(majorNumbers[root]) + (major ? "B" : "A");
}

DjAnalysis analyseDjTrack(const float* left, const float* right, int frames, double sampleRate,
                          const DjAnalysisOptions& options)
{
    DjAnalysis analysis;
    analysis.beatsPerBar = std::max(1, options.beatsPerBar);
    if (left == nullptr || frames <= 0)
        return analysis;
    const auto rate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    auto scanned = scan(left, right, frames, rate, options);
    if (cancelled(options))
        return analysis;
    analysis.columns = std::move(scanned.columns);

    // Two cycles of a 40 Hz bass, in columns, for the tempo's envelope.
    const auto smoothColumns = std::max(4, static_cast<int>(std::ceil(0.05 * rate / columnFrames)));
    std::vector<float> lowFlux;
    const auto sharp = onsetFlux(scanned, 1, &lowFlux);
    const auto onset = removeMean(onsetFlux(scanned, smoothColumns, nullptr), rate);
    if (options.knownBpm > 0.0)
    {
        analysis.bpm = options.knownBpm;
        analysis.firstBeatSeconds = std::max(0.0, options.knownFirstBeatSeconds);
        analysis.confidence = 1.0;
    }
    else
    {
        const auto tempo = detectTempo(onset, sharp, rate, options);
        if (cancelled(options))
            return analysis;
        if (tempo.bpm > 0.0)
        {
            analysis.bpm = tempo.bpm;
            analysis.confidence = tempo.confidence;
            const auto offset = downbeatOffset(lowFlux, tempo.phaseColumns, tempo.periodColumns, analysis.beatsPerBar);
            analysis.firstBeatSeconds = (tempo.phaseColumns + offset * tempo.periodColumns) * columnFrames / rate;
        }
    }
    measureBars(analysis, scanned, rate);
    if (options.detectKey && !cancelled(options))
        analysis.keyIndex = detectKey(left, right, frames, rate, options);
    return analysis;
}
}
