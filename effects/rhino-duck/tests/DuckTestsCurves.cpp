#include "DuckSuites.h"
#include "DuckTestSupport.h"

#include <set>

// The sixteen curves as pure arithmetic: bounded, shaped as their family
// says, and bent by Slope and Shift the way the panel promises.
namespace rhino::duck::tests
{
namespace
{
float highest(int curve, CurveAdjust adjust = {})
{
    auto top = 0.0f;
    for (int k = 0; k < 1000; ++k) top = std::max(top, curveGain(curve, k / 1000.0f, adjust));
    return top;
}

float lowest(int curve, CurveAdjust adjust = {})
{
    auto bottom = 1.0f;
    for (int k = 0; k < 1000; ++k) bottom = std::min(bottom, curveGain(curve, k / 1000.0f, adjust));
    return bottom;
}

void curvesStayWithinUnity()
{
    juce::Random random(3);
    for (int curve = 0; curve < curveCount; ++curve)
        for (int trial = 0; trial < 40; ++trial)
        {
            CurveAdjust adjust { random.nextFloat() * 2.0f - 1.0f, random.nextFloat() - 0.5f };
            if (trial == 0) adjust = {};
            for (int k = 0; k <= 400; ++k)
            {
                const auto gain = curveGain(curve, k / 400.0f, adjust);
                require(std::isfinite(gain) && gain >= -1.0e-6f && gain <= 1.0f + 1.0e-6f, "a curve's gain stays within 0 and 1");
            }
        }
}

void familiesDoWhatTheySay()
{
    for (int curve = 0; curve < 10; ++curve)
    {
        require(curveGain(curve, 0.0f, {}) <= 0.15f, "a ducking curve starts ducked");
        require(highest(curve) >= 0.98f, "a ducking curve opens fully");
        require(curveFamily(curve) == (curve < 8 ? Family::sidechain : Family::subBass), "the first ten are sidechain and sub bass curves");
    }
    for (int curve = 10; curve < 12; ++curve)
    {
        require(curveGain(curve, 0.0f, {}) >= 0.98f, "a trim curve starts open");
        require(curveGain(curve, 0.99f, {}) <= 0.02f, "a trim curve ends closed");
        require(curveFamily(curve) == Family::kickTrim, "11 and 12 trim kicks");
    }
    for (int curve = 12; curve < 14; ++curve)
    {
        require(curveGain(curve, 0.0f, {}) >= 0.98f && curveGain(curve, 0.999f, {}) >= 0.98f, "a reverse curve is open at both ends");
        require(lowest(curve) <= 0.1f, "and sucks the sound away in between");
        require(curveFamily(curve) == Family::reverse, "13 and 14 are reverse curves");
    }
    for (int curve = 14; curve < 16; ++curve)
    {
        require(curveGain(curve, 0.0f, {}) <= 0.05f && curveGain(curve, 0.999f, {}) <= 0.05f, "a rhythm curve is closed at both ends");
        require(highest(curve) >= 0.98f, "and opens in between");
        require(curveFamily(curve) == Family::rhythm, "15 and 16 are rhythm curves");
    }
}

void shiftMovesTheWholeCurve()
{
    for (int curve = 0; curve < curveCount; ++curve)
        for (int k = 0; k < 50; ++k)
        {
            const auto phase = k / 50.0f;
            requireClose(curveGain(curve, phase, { 0.0f, 0.2f }), curveGain(curve, phase - 0.2f, {}), 1.0e-5f,
                         "Shift moves the curve round the cycle");
            requireClose(curveGain(curve, phase, { 0.0f, -0.3f }), curveGain(curve, phase + 0.3f, {}), 1.0e-5f,
                         "and the other way");
        }
}

void slopeMovesTheSlopedPartWithinItsRoom()
{
    for (int curve = 0; curve < curveCount; ++curve)
    {
        const auto left = mobileSpan(curve, { -1.0f, 0.0f });
        const auto middle = mobileSpan(curve, {});
        const auto right = mobileSpan(curve, { 1.0f, 0.0f });
        require(left.from <= middle.from + 1.0e-6f && middle.from <= right.from + 1.0e-6f, "Slope moves the sloped part left and right");
        require(left.from >= 0.0f && right.to <= 1.0f + 1.0e-5f, "and never past the cycle");
        requireClose(left.to - left.from, right.to - right.from, 1.0e-5f, "the sloped part keeps its length");
        for (const auto slope : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            requireClose(slopeForOffset(curve, mobileOffset(curve, slope)), slope, 1.0e-4f, "a drag's offset maps back to its Slope");
        // Moved all the way left, a ducking curve is open sooner.
        if (curve < 8)
            require(curveGain(curve, 0.3f, { -1.0f, 0.0f }) >= curveGain(curve, 0.3f, {}) - 1.0e-6f
                        && curveGain(curve, 0.3f, { 1.0f, 0.0f }) <= curveGain(curve, 0.3f, {}) + 1.0e-6f,
                    "a ducking curve moved left opens sooner, moved right later");
    }
    // The sloped part never crosses a fixed point: Hook's dip stays before
    // its rise, and the sub curves' fall stays after theirs.
    require(mobileSpan(1, { -1.0f, 0.0f }).from >= 0.07f - 1.0e-5f, "Hook's rise cannot be dragged into its dip");
    require(mobileSpan(8, { 1.0f, 0.0f }).to <= 0.88f + 1.0e-5f, "Sub Slow's rise cannot be dragged into its fall");
}

void namesAreDistinct()
{
    std::set<juce::String> names;
    for (int curve = 0; curve < curveCount; ++curve) names.insert(curveName(curve));
    require(static_cast<int>(names.size()) == curveCount, "every curve has a name of its own");
    require(juce::String(familyName(Family::sidechain)) == "Sidechain", "a family has a name");
    require(clampCurve(-5) == 0 && clampCurve(99) == curveCount - 1, "an index out of range is clamped");
}
}

void curveTests()
{
    curvesStayWithinUnity();
    familiesDoWhatTheySay();
    shiftMovesTheWholeCurve();
    slopeMovesTheSlopedPartWithinItsRoom();
    namesAreDistinct();
}
}
