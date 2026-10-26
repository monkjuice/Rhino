#include "DuckSuites.h"
#include "DuckTestSupport.h"

#include <set>

// The sixteen curves as pure arithmetic: bounded, shaped as their family
// says, and bent by Hold, Release, Attack, Depth and Shift the way the panel
// promises.
namespace rhino::duck::tests
{
namespace
{
CurveAdjust randomAdjust(juce::Random& random)
{
    CurveAdjust adjust;
    adjust.hold = random.nextFloat() - 0.5f;
    adjust.shift = random.nextFloat() - 0.5f;
    adjust.release = random.nextFloat() * 3.0f;
    adjust.attack = random.nextFloat() * 0.3f;
    adjust.depth = random.nextFloat();
    return adjust;
}

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
            auto adjust = randomAdjust(random);
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

// The design is what plays until someone turns a knob: the defaults bend
// nothing, and the point the rhythm curves gained at phase 0 changes no gain.
void untouchedCurvesPlayAsDesigned()
{
    for (int curve = 0; curve < curveCount; ++curve)
    {
        const auto room = mobileRoom(curveDef(curve));
        require(room.any && room.last - room.first > 0.1f, "every curve has a sloped part to hold and release");
        const auto part = slopedPart(curve, {});
        requireClose(part.start, room.first, 1.0e-6f, "unadjusted, the sloped part starts where it was designed to");
        requireClose(part.length, room.last - room.first, 1.0e-6f, "and runs as long");
    }
    requireClose(curveGain(0, 0.03f, {}), 0.0f, 1.0e-6f, "Classic holds its cut");
    requireClose(curveGain(0, 0.29f, {}), 0.5f, 1.0e-4f, "and is half open half way up its rise");
    requireClose(curveGain(14, 0.34f, {}), 0.5f, 1.0e-4f, "Swell is half way up its rise at 0.34, as before");
    requireClose(curveGain(15, 0.12f, {}), 0.5f, 1.0e-4f, "and Double half way up its first pulse at 0.12");
}

void holdMovesTheSlopedPartWithinItsRoom()
{
    for (int curve = 0; curve < curveCount; ++curve)
    {
        const auto design = slopedPart(curve, {});
        const auto room = holdRoom(curve, {});
        const auto left = withHold(curve, {}, -1.0f);
        const auto right = withHold(curve, {}, 2.0f);
        requireClose(slopedPart(curve, left).start, room.from, 1.0e-6f, "Hold goes as far left as the room allows");
        requireClose(slopedPart(curve, right).start, room.to, 1.0e-6f, "and as far right");
        require(room.from <= design.start + 1.0e-6f && design.start <= room.to + 1.0e-6f, "the design lies within the room");
        requireClose(slopedPart(curve, left).length, design.length, 1.0e-5f, "Hold keeps the sloped part's length");
        requireClose(slopedPart(curve, right).length, design.length, 1.0e-5f, "at either end");
        require(mobileSpan(curve, left).from >= 0.0f && mobileSpan(curve, right).to <= 1.0f + 1.0e-5f, "and never past the cycle");
        requireClose(slopedPart(curve, withHold(curve, {}, design.start + 0.01f)).start, std::min(design.start + 0.01f, room.to), 1.0e-6f,
                     "a Hold within the room is taken as it is");
        if (curve < 8)
            require(curveGain(curve, 0.3f, left) >= curveGain(curve, 0.3f, {}) - 1.0e-6f
                        && curveGain(curve, 0.3f, right) <= curveGain(curve, 0.3f, {}) + 1.0e-6f,
                    "a ducking curve held shorter opens sooner, held longer later");
    }
    // The sloped part never crosses a fixed point: Hook's dip stays before
    // its rise, and the sub curves' fall stays after theirs.
    require(mobileSpan(1, withHold(1, {}, -1.0f)).from >= 0.07f - 1.0e-5f, "Hook's rise cannot be held into its dip");
    require(mobileSpan(8, withHold(8, {}, 2.0f)).to <= 0.88f + 1.0e-5f, "Sub Slow's rise cannot be held into its fall");
    // A rhythm curve rising from the beat can be held back from it.
    require(curveGain(14, 0.05f, withHold(14, {}, 0.1f)) == 0.0f, "Hold delays Swell's rise");
}

void releaseStretchesTheSlopedPartFromItsStart()
{
    for (int curve = 0; curve < curveCount; ++curve)
    {
        const auto design = slopedPart(curve, {});
        const auto shorter = withRelease(curve, {}, design.length * 0.5f);
        const auto longest = withRelease(curve, {}, 5.0f);
        requireClose(slopedPart(curve, shorter).start, design.start, 1.0e-6f, "Release keeps the sloped part's start");
        requireClose(slopedPart(curve, shorter).length, std::max(design.length * 0.5f, shortestRelease), 1.0e-5f, "and sets its length");
        requireClose(slopedPart(curve, longest).length, releaseRoom(curve, {}).to, 1.0e-5f, "as long as the room allows");
        require(mobileSpan(curve, longest).to <= 1.0f + 1.0e-5f, "and never past the cycle");
    }
    // Classic's rise made half as long is fully open where it used to be half.
    const auto quick = withRelease(0, {}, 0.23f);
    require(curveGain(0, 0.3f, quick) > 0.99f && curveGain(0, 0.3f, {}) < 0.55f, "a shorter Release opens Classic sooner");
    // Each keeps to the room the other leaves: the longest Release leaves
    // Hold nowhere to go, and Hold moving does not shorten the Release.
    const auto stretched = withRelease(0, {}, 5.0f);
    const auto thenHeld = withHold(0, stretched, 2.0f);
    requireClose(slopedPart(0, thenHeld).length, slopedPart(0, stretched).length, 1.0e-6f, "Hold never shortens the Release");
    require(slopedPart(0, thenHeld).start + slopedPart(0, thenHeld).length <= mobileRoom(curveDef(0)).highest + 1.0e-5f,
            "and the two together stay inside the room");
}

void attackEasesTheCutOnTheBeat()
{
    CurveAdjust soft;
    soft.attack = 0.04f;
    requireClose(curveGain(0, 0.0f, soft), 1.0f, 1.0e-6f, "with Attack a cycle starts where the last one ended");
    requireClose(curveGain(0, 0.02f, soft), 0.5f, 1.0e-4f, "half way through the Attack the cut is half made");
    requireClose(curveGain(0, 0.05f, soft), curveGain(0, 0.05f, {}), 1.0e-6f, "and after it the curve is untouched");
    for (const auto phase : { 0.0f, 0.01f, 0.03f })
        requireClose(curveGain(12, phase, soft), curveGain(12, phase, {}), 1.0e-6f, "a curve that ends where it starts has nothing to ease");
}

void depthKeepsPartOfTheDip()
{
    CurveAdjust half;
    half.depth = 0.5f;
    CurveAdjust none;
    none.depth = 0.0f;
    for (int curve = 0; curve < curveCount; ++curve)
        for (int k = 0; k < 50; ++k)
        {
            const auto phase = k / 50.0f;
            requireClose(curveGain(curve, phase, half), 1.0f - 0.5f * (1.0f - curveGain(curve, phase, {})), 1.0e-6f, "Depth 50% keeps half the dip");
            requireClose(curveGain(curve, phase, none), 1.0f, 1.0e-6f, "and Depth 0 none of it");
        }
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
    untouchedCurvesPlayAsDesigned();
    shiftMovesTheWholeCurve();
    holdMovesTheSlopedPartWithinItsRoom();
    releaseStretchesTheSlopedPartFromItsStart();
    attackEasesTheCutOnTheBeat();
    depthKeepsPartOfTheDip();
    namesAreDistinct();
}
}
