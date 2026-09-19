#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Private wavetable bank for Rhino Wave.  Each octave band has a separately
// band-limited rendering of the five morph frames, so oscillator lookup stays
// allocation-free and inexpensive in the audio callback.
namespace rhino::rhino_wave
{
inline constexpr int tableSize = 2048;
inline constexpr int tableMask = tableSize - 1;
inline constexpr int frameCount = 5;
inline constexpr int bandCount = 11;

struct TableBank final
{
    using Table = std::array<float, tableSize>;
    std::array<std::array<Table, frameCount>, bandCount> tables {};

    TableBank()
    {
        for (int band = 0; band < bandCount; ++band)
        {
            // The upper bands deliberately contain fewer harmonics. The last
            // band is used for all frequencies above the final threshold.
            const auto maxHarmonic = std::max(1, 512 >> band);
            for (int i = 0; i < tableSize; ++i)
            {
                const auto phase = static_cast<float>(i) / static_cast<float>(tableSize);
                const auto radians = phase * juce::MathConstants<float>::twoPi;
                auto saw = 0.0f, square = 0.0f, triangle = 0.0f;
                for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic)
                {
                    const auto sine = std::sin(radians * static_cast<float>(harmonic));
                    saw += sine / static_cast<float>(harmonic);
                    if ((harmonic & 1) != 0)
                    {
                        square += sine / static_cast<float>(harmonic);
                        triangle += (harmonic & 2) == 0 ? sine / static_cast<float>(harmonic * harmonic)
                                                       : -sine / static_cast<float>(harmonic * harmonic);
                    }
                }
                tables[band][0][i] = std::sin(radians);
                tables[band][1][i] = triangle * (8.0f / (juce::MathConstants<float>::pi * juce::MathConstants<float>::pi));
                tables[band][2][i] = saw * (-2.0f / juce::MathConstants<float>::pi);
                tables[band][3][i] = square * (4.0f / juce::MathConstants<float>::pi);
                tables[band][4][i] = std::sin(radians + 0.35f * juce::MathConstants<float>::twoPi) * 0.58f
                                   + std::sin(radians * 5.0f) * 0.42f;
            }
        }
    }
};

inline const TableBank& tables()
{
    static const TableBank bank;
    return bank;
}

inline int bandForFrequency(float frequency, double sampleRate)
{
    const auto normalized = std::max(1.0f, frequency) / static_cast<float>(sampleRate);
    return juce::jlimit(0, bandCount - 1, static_cast<int>(std::floor(std::log2(normalized * 1024.0f))));
}
}
