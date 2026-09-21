#pragma once
#include <array>
#include <atomic>
#include <vector>

// The live spectrum behind an EQ curve, split across the two threads that
// have to cooperate to draw it.
//
// SpectrumTap is the audio side and does nothing but copy a mono sum into a
// ring: no transform, no allocation, no lock. SpectrumReader is the message
// side and does all of the work -- window, transform, fold into log-spaced
// display bins, decay -- in the panel's timer callback, where it costs
// nothing when no EQ is on screen.
//
// Putting the transform on the drawing thread rather than the audio thread is
// the whole point of the split. A spectrum is a thing a person is looking at;
// it should not be able to cost an audio dropout when nobody is.
namespace rhino
{

class SpectrumTap
{
public:
    static constexpr int ringSize = 1 << 14;   // 341 ms at 48 kHz

    void prepare(double sampleRateIn);
    void reset();

    // Audio thread. Mono sum of however many channels arrive, so one ring
    // covers a stereo device without the reader having to know.
    void write(const float* const* data, int channels, int count) noexcept;

    double rate() const { return sampleRate.load(std::memory_order_relaxed); }

    // True once anything at all has been written. A tap that has never run
    // draws no spectrum rather than a floor of digital silence.
    bool hasRun() const { return written.load(std::memory_order_relaxed) > 0; }

    // Copies the most recent `count` samples, oldest first. Safe to call from
    // any thread: the writer may advance underneath it, but a frame that
    // straddles two blocks is a pixel of jitter on a decaying display, not a
    // defect worth a lock on the audio thread.
    void readTail(float* destination, int count) const noexcept;

private:
    std::array<float, ringSize> ring {};
    std::atomic<int> writePosition {0};
    std::atomic<long long> written {0};
    std::atomic<double> sampleRate {48000.0};
};

class SpectrumReader
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;   // 23.4 Hz resolution at 48 kHz
    static constexpr int binCount = 160;
    static constexpr float lowestFrequency = 20.0f;
    static constexpr float highestFrequency = 20000.0f;
    static constexpr float floorDb = -96.0f;

    SpectrumReader();

    // Reads the tap, transforms, folds and decays. `decayDb` is how far a bin
    // may fall in one call, which is what makes the display readable rather
    // than a flicker. Returns false when the tap has never run.
    bool update(const SpectrumTap&, float decayDb);
    void clear();

    const std::array<float, binCount>& decibels() const { return display; }

    // Centre frequency of a display bin, so the panel places it on the same
    // log axis the grid is drawn on.
    static float binFrequency(int index);

    // Exposed so a test can drive the fold without a tap.
    void analyse(const float* samples, int count, double sampleRateIn, float decayDb);

private:
    void buildBinMap(double sampleRateIn);
    void transform();

    std::array<float, fftSize> window {};
    // The tap is read into `tail`, and `frame` is what the transform chews
    // on. They are separate because analyse() zero-fills its own frame, and
    // handing it the buffer it is about to clear reads back silence.
    std::array<float, fftSize> tail {}, frame {};
    std::array<float, fftSize> real {}, imaginary {};
    std::array<float, fftSize / 2 + 1> magnitude {};
    std::array<int, fftSize> bitReversed {};
    std::array<float, fftSize / 2> cosTable {}, sinTable {};
    // Where each display bin starts and ends in FFT bins. Fractional, because
    // below a few hundred hertz a display bin is narrower than an FFT bin and
    // has to interpolate rather than average nothing.
    std::array<float, binCount> binStart {}, binEnd {};
    std::array<float, binCount> display {};
    double mappedRate = 0.0;
};

}
