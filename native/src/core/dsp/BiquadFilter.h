#pragma once
#include "EqFilter.h"

// One channel of a Biquad from EqFilter.h, run in transposed direct form II
// in double precision. The drive devices run some of their filters at four
// times the sample rate, where a 20 Hz corner sits so close to the unit circle
// that float coefficients and state lose it.
namespace rhino
{
struct BiquadFilter
{
    Biquad coefficients;
    double s1 = 0.0, s2 = 0.0;

    double process(double x)
    {
        const auto y = coefficients.b0 * x + s1;
        s1 = coefficients.b1 * x - coefficients.a1 * y + s2;
        s2 = coefficients.b2 * x - coefficients.a2 * y;
        return y;
    }

    void reset() { s1 = s2 = 0.0; }
};

// The filter that undoes `filter`, numerator and denominator swapped. Exact
// for a minimum-phase filter, which the shelves and bells of EqFilter.h are:
// their zeros sit inside the unit circle, so as poles they are stable.
inline Biquad inverseOf(const Biquad& filter)
{
    return eqdetail::normalise(1.0, filter.a1, filter.a2, filter.b0, filter.b1, filter.b2);
}
}
