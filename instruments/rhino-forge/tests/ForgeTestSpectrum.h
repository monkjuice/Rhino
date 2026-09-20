#pragma once

#include "ForgeTestSupport.h"

// Measuring what was rendered.
//
// The first question a player asks and the one the panel cannot answer for
// itself: does a note come out at the pitch it names? Measured off the rendered
// signal rather than off the phase accumulator, so nothing here can agree with
// the oscillator by construction — a transposed table, a mis-scaled frame or a
// phase increment that is off by a ratio all show up as cents.
//
// The same window answers what a shape is as well as where it sits, which is
// what the sub's six waveforms are held to: one transform, read once for the
// fundamental and once per partial over it.
namespace rhino::forge::tests
{
// How long a window is measured over. Big enough that a bin is a couple of
// Hertz wide at either rate, so two partials of a low note are never in the
// same bin and the interpolation below has room to work.
inline constexpr int spectrumOrder = 15;
inline constexpr int spectrumSize = 1 << spectrumOrder;

// Where a peak really sits between two bins, and how tall it really is. A
// partial almost never lands on a bin centre, and a Hann window spreads it
// across three — so taking the tallest bin alone misreads the frequency by up
// to half a bin and the amplitude by up to 1.4 dB. Fitting a parabola through
// the logs of the three recovers both, which is what lets one partial be
// compared against another closely enough to name a waveform.
struct SpectrumPeak
{
    double frequency = 0.0;
    double amplitude = 0.0;
};

// The magnitude spectrum of a patch rendered through Core, measured past the
// attack so the window holds steady state and not the envelope's edge.
std::vector<double> renderedSpectrum(const Patch& patch, int note, double sampleRate);

// The peak around one bin, and the tallest partial in a whole spectrum — which
// for every shape measured here is the fundamental.
SpectrumPeak peakAt(const std::vector<double>& magnitude, int bin, double sampleRate);
SpectrumPeak loudestPeak(const std::vector<double>& magnitude, double sampleRate);

// The amplitude of one harmonic of a known fundamental, found by looking for
// the local maximum where that harmonic should be rather than trusting a bin
// index: a fundamental measured to a fraction of a bin puts the tenth harmonic
// several bins from wherever the arithmetic lands.
double partialAmplitude(const std::vector<double>& magnitude, double fundamental, int harmonic,
                        double sampleRate);

// The patch the tuning checks ask their question of: one oscillator on the saw
// frame and nothing else sounding.
Patch sawOnly();

// The fundamental a note actually renders at, for comparison with the frequency
// the note is nominally worth.
double renderedFundamental(int note, double sampleRate);
}
