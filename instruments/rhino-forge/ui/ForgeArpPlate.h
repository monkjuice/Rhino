#pragma once

#include "ForgeStyle.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// What moves on the ARP plate beside the keys: the lamp, the lit face while
// the six panes are showing, the four slot buttons, and the notes falling
// through the well. The metal under all of it never changes and is drawn with
// the rest of the chassis in ForgePanels.h, where it is cached; this is the
// part on the frame path, and it is kept to fills of small shapes for that
// reason — nothing here strokes a path but the open state's outline.
namespace rhino::forge::ui
{
// One note on its way down the well. Born when the voices were told to play
// it and spent once it, and the trace a held note leaves behind it, have gone
// through the floor. Everything about its fall is settled when it is made
// except the moment its note is let go, so a frame only has to ask how old it
// is and whether it is still held: the panel repaints whole at 24 Hz, and a
// spark that had to be stepped would have to be stepped by every one of those
// repaints.
struct NoteSpark
{
    // The note it is the light of, so the voices can be asked whether it is
    // still down.
    int note = 60;
    // Where across the well it falls, 0 to 1: the note's place on the keyboard.
    float across = 0.0f;
    // How hard the note was struck, 0 to 1, which is how big and how bright
    // the spark is.
    float weight = 0.5f;
    // How fast it falls against the standard drop. Varied a little per spark
    // so a chord drifts down as several lights rather than as one bar.
    float pace = 1.0f;
    // How far its colour leans from the panel's violet towards white.
    float warmth = 0.0f;
    // Rolled once at birth: where its stardust appears and where its ripple
    // starts, so two held notes never shimmer in step.
    std::uint32_t seed = 0;
    double bornMs = 0.0;
    // When its note was let go, or below zero while it is still down. Every
    // spark is born held, because a note is down at the moment it is struck;
    // the panel lets go of it once the voices say the note has stopped.
    double releasedMs = -1.0;

    bool held() const { return releasedMs < 0.0; }
};

// How long a spark takes to reach the floor at a pace of one. Slow: the point
// of the well is to be watched rather than read, and a drop that is over
// before the eye has found it is a flicker, not a fall.
inline constexpr double sparkFallMs = 1500.0;

// As many as are ever kept. A chord under a fast arp with a long fall can
// have this many in the air, and past it the oldest is the one let go.
inline constexpr int maxSparks = 48;

// How far a held note's light has fallen when its trace starts to brighten:
// about where the light starts to go out, so the trace takes over as the
// light leaves.
inline constexpr float sparkChargeStart = 0.6f;

// How long a held note's trace, once it starts, takes to brighten all the way
// to ultra. Long enough that an ordinary sustained chord only glows, short
// enough that a pad held through a bar gets there.
inline constexpr double sparkChargeMs = 1800.0;

// How quickly ultra comes in once it is reached, so it arrives rather than pops.
inline constexpr double sparkUltraEaseMs = 250.0;

// The burst of light along the trace at the moment ultra is reached.
inline constexpr double sparkUltraFlashMs = 350.0;

// In ultra, how many pixels the trace's width swells by at the crest of its
// ripple, how long one ripple takes, and how far apart two crests are.
inline constexpr float sparkUltraSwellPx = 3.0f;
inline constexpr double sparkUltraRippleMs = 450.0;
inline constexpr float sparkUltraRipplePx = 14.0f;

// How many motes of stardust sprinkle round a trace in ultra.
inline constexpr std::uint32_t sparkUltraMotes = 10;

// How much longer than an ordinary trail a trace has to be before it is lit as
// a held note's. Below it the trail is drawn exactly as every spark's always
// was, so a note let go early looks as it did before holding meant anything.
inline constexpr float sparkTraceBlendPx = 12.0f;

// Where a spark's light has got to: 0 at the top of the well, 1 at the floor,
// and past 1 once it has gone through it. The light falls the same whether or
// not its note is held; holding is carried by its trace.
inline float sparkProgress(const NoteSpark& spark, double nowMs)
{
    return static_cast<float>((nowMs - spark.bornMs) * static_cast<double>(spark.pace) / sparkFallMs);
}

// Where the top of a spark's trace has got to, on the same scale. While the
// note is held the trace hangs from the top of the well, the note still
// pouring in; let go, its top falls at the spark's own pace and the trace
// pours out through the floor.
inline float sparkTailProgress(const NoteSpark& spark, double nowMs)
{
    if (spark.held()) return 0.0f;
    return static_cast<float>((nowMs - spark.releasedMs) * static_cast<double>(spark.pace) / sparkFallMs);
}

// Gone for good: let go, with its light and the top of its trace both
// through the floor. A held note never is.
inline bool sparkSpent(const NoteSpark& spark, double nowMs)
{
    return !spark.held() && sparkProgress(spark, nowMs) >= 1.0f && sparkTailProgress(spark, nowMs) >= 1.0f;
}

// How old a spark is when its trace starts to brighten, if its note is down.
inline double sparkChargeStartMs(const NoteSpark& spark)
{
    return static_cast<double>(sparkChargeStart) * sparkFallMs / static_cast<double>(spark.pace);
}

// What holding the note has done to its trace.
struct SparkTrace
{
    // How far the top of the trace has fallen: sparkTailProgress.
    float tail = 0.0f;
    // How lit what is left of the trace is: 1 while the note is held, put out
    // over the last third of its top's fall once it is let go.
    float light = 1.0f;
    // How far it has brightened, 0 to 1. Kept once the note is let go, so a
    // trace leaves as bright as it was.
    float charge = 0.0f;
    // How far into ultra, 0 to 1. Kept the same way.
    float ultra = 0.0f;
    // How long since ultra was reached, or below zero if it never was. Timed
    // from the clock rather than the hold, so a leaving trace keeps rippling
    // and its dust keeps drifting rather than freezing.
    double ultraMs = -1.0;
};

inline SparkTrace sparkTrace(const NoteSpark& spark, double nowMs)
{
    SparkTrace trace;
    trace.tail = sparkTailProgress(spark, nowMs);
    trace.light = juce::jlimit(0.0f, 1.0f, (1.0f - trace.tail) / 0.3f);
    const auto heldUntil = spark.held() ? nowMs : juce::jmin(nowMs, spark.releasedMs);
    const auto charging = juce::jmax(0.0, heldUntil - (spark.bornMs + sparkChargeStartMs(spark)));
    trace.charge = static_cast<float>(juce::jlimit(0.0, 1.0, charging / sparkChargeMs));
    if (charging >= sparkChargeMs)
    {
        trace.ultraMs = nowMs - (spark.bornMs + sparkChargeStartMs(spark) + sparkChargeMs);
        trace.ultra = static_cast<float>(juce::jlimit(0.0, 1.0, (charging - sparkChargeMs) / sparkUltraEaseMs));
    }
    return trace;
}

// A number from 0 to 1 fixed by its three arguments. The stardust is placed
// from these rather than from a random generator, so the same spark, mote and
// turn always land in the same place and a frame can draw every mote from the
// clock alone with nothing to step.
inline float sparkNoise(std::uint32_t seed, std::uint32_t a, std::uint32_t b)
{
    const auto mix = [] (std::uint32_t h)
    {
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    };
    const auto h = mix(mix(seed + a * 0x9e3779b9u) + b * 0x85ebca6bu);
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// A note just struck, into the well. The same note struck again restarts its
// voice, so only the newest strike of a note can still be held. Past the cap
// the oldest spark already let go is the one dropped, because a held note's
// trace is the one a player is still watching.
inline void addNoteSpark(std::vector<NoteSpark>& sparks, const NoteSpark& spark)
{
    for (auto& older : sparks)
        if (older.note == spark.note && older.held()) older.releasedMs = spark.bornMs;
    if (static_cast<int>(sparks.size()) >= maxSparks)
    {
        const auto letGo = std::find_if(sparks.begin(), sparks.end(),
                                        [] (const NoteSpark& s) { return !s.held(); });
        sparks.erase(letGo != sparks.end() ? letGo : sparks.begin());
    }
    sparks.push_back(spark);
}

// Let go of every spark whose note the voices have stopped holding.
template <typename NoteHeld>
void releaseNoteSparks(std::vector<NoteSpark>& sparks, double nowMs, NoteHeld&& noteHeld)
{
    for (auto& spark : sparks)
        if (spark.held() && !noteHeld(spark.note)) spark.releasedMs = nowMs;
}

inline void drawArpWaterfall(juce::Graphics& g, juce::Rectangle<int> wellBounds,
                             const std::vector<NoteSpark>& sparks, double nowMs)
{
    if (sparks.empty() || wellBounds.getWidth() < 8 || wellBounds.getHeight() < 8) return;
    const auto well = wellBounds.toFloat();
    juce::Graphics::ScopedSaveState state(g);
    // A spark enters from above the top edge and leaves through the floor, so
    // the well's edges are what cut it rather than its own fade.
    g.reduceClipRegion(wellBounds);
    // A little faster as it goes, the way anything falling does, and only a
    // little, so the fall reads as drifting rather than dropping.
    const auto fallen = [] (float progress) { return progress * (0.7f + 0.3f * progress); };

    for (const auto& spark : sparks)
    {
        const auto t = sparkProgress(spark, nowMs);
        if (t < 0.0f || sparkSpent(spark, nowMs)) continue;
        // Lit over its first tenth and put out over its last third, so it is
        // gone by the time it reaches the floor rather than cut off by it.
        const auto glow = juce::jlimit(0.0f, 1.0f, juce::jmin(t / 0.1f, (1.0f - t) / 0.3f));
        const auto width = 2.5f + 4.0f * spark.weight;
        const auto height = width * 1.9f;
        const auto x = well.getX() + 4.0f + spark.across * (well.getWidth() - 8.0f);
        const auto y = well.getY() - height * 0.5f + fallen(t) * (well.getHeight() + height);
        const auto body = juce::Rectangle<float>(width, height).withCentre({x, y});
        const auto colour = signalViolet.interpolatedWith(juce::Colours::white, 0.25f + 0.45f * spark.warmth);

        // The trace runs from its top down to the light, or to the floor once
        // the light has gone through it. Held, its top stays at the top of the
        // well, so it stretches as the light falls and stays when the light
        // has left. An ordinary trail is the same trace too short to be lit as
        // a held note's, and is drawn exactly as it always was.
        const auto trail = 8.0f + 18.0f * spark.weight;
        const auto trace = sparkTrace(spark, nowMs);
        const auto traceTop = well.getY() + fallen(trace.tail) * well.getHeight();
        const auto traceEnd = juce::jmin(body.getY(), well.getBottom());
        const auto length = traceEnd - traceTop;
        const auto stretch = juce::jlimit(0.0f, 1.0f, (length - trail) / sparkTraceBlendPx);
        if (stretch <= 0.0f)
        {
            // The trail above the body, fading as it goes up: four steps of
            // one column rather than a gradient, which is all a trail this
            // narrow can show and a good deal cheaper to fill.
            if (t < 1.0f)
                for (int step = 0; step < 4; ++step)
                {
                    const auto share = static_cast<float>(step) * 0.25f;
                    g.setColour(signalViolet.withAlpha(glow * 0.2f * (1.0f - share)));
                    g.fillRect(juce::Rectangle<float>(x - width * 0.3f, body.getY() - trail * (share + 0.25f),
                                                      width * 0.6f, trail * 0.25f + 0.5f));
                }
        }
        else
        {
            // A held note's trace. Lit by its own hold rather than by the
            // light, so it stays when the light has gone; brighter and whiter
            // the longer the note is held, though only half way to the
            // light's colour, so it stays violet; and cut finer than a trail,
            // on whole pixels, so the ripple has rows to move and the steps do
            // not overlap into lines.
            const auto lit = juce::jmap(stretch, glow, trace.light);
            const auto ultra = trace.ultra * trace.light;
            const auto flash = trace.ultraMs >= 0.0 && trace.ultraMs < sparkUltraFlashMs
                                   ? 1.0f - static_cast<float>(trace.ultraMs / sparkUltraFlashMs) : 0.0f;
            // In ultra its width swells and narrows by a few pixels, in a
            // ripple running down it.
            const auto swell = [&trace, ultra, &well] (float atY)
            {
                if (ultra <= 0.0f) return 0.0f;
                const auto ripple = static_cast<float>(trace.ultraMs / sparkUltraRippleMs)
                                  - (atY - well.getY()) / sparkUltraRipplePx;
                return ultra * sparkUltraSwellPx
                     * (0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * ripple));
            };
            const auto charged = signalViolet.interpolatedWith(
                juce::Colours::white, juce::jmin(0.85f, 0.25f + 0.45f * spark.warmth + 0.35f * trace.charge));
            const auto traceColour = signalViolet.interpolatedWith(charged, 0.5f * trace.charge);
            const auto steps = juce::jlimit(4, 64, static_cast<int>(std::ceil(length / 2.0f)));
            const auto slice = length / static_cast<float>(steps);
            const auto fade = 1.0f - 0.6f * stretch;
            for (int step = 0; step < steps; ++step)
            {
                const auto share = static_cast<float>(step) / static_cast<float>(steps);
                const auto lower = std::round(traceEnd - slice * static_cast<float>(step));
                const auto upper = std::round(traceEnd - slice * static_cast<float>(step + 1));
                const auto columnWidth = width * 0.6f + swell((upper + lower) * 0.5f) + 2.0f * flash;
                g.setColour(traceColour.withAlpha(juce::jmin(1.0f, lit * (0.2f + 0.3f * trace.charge + 0.35f * flash)
                                                                       * (1.0f - share * fade))));
                g.fillRect(juce::Rectangle<float>(x - columnWidth * 0.5f, upper, columnWidth, lower - upper));
            }

            // Ultra's stardust: motes smaller than any spark, each appearing
            // somewhere beside the trace, drifting out and down as it
            // twinkles, and turning up somewhere new on its next turn.
            if (ultra > 0.0f)
                for (std::uint32_t mote = 0; mote < sparkUltraMotes; ++mote)
                {
                    const auto period = 650.0 + 600.0 * static_cast<double>(sparkNoise(spark.seed, mote, 0));
                    const auto clock = trace.ultraMs + period * static_cast<double>(sparkNoise(spark.seed, mote, 1));
                    const auto turn = static_cast<std::uint32_t>(clock / period);
                    const auto life = static_cast<float>(clock / period - static_cast<double>(turn));
                    const auto roll = [&spark, mote, turn] (std::uint32_t k)
                    { return sparkNoise(spark.seed, mote, 2 + turn * 8 + k); };
                    const auto side = roll(0) < 0.5f ? -1.0f : 1.0f;
                    const auto moteX = x + side * (width * 0.3f + 1.5f + 5.0f * roll(1) + life * (2.0f + 4.0f * roll(3)));
                    const auto moteY = traceTop + length * roll(2) + life * (3.0f + 6.0f * roll(4));
                    const auto size = 0.8f + 0.8f * roll(5);
                    const auto twinkle = std::sin(juce::MathConstants<float>::pi * life);
                    g.setColour(juce::Colours::white.interpolatedWith(charged, 0.5f * roll(6))
                                    .withAlpha(ultra * twinkle * (0.45f + 0.5f * roll(7))));
                    g.fillRect(juce::Rectangle<float>(size, size).withCentre({moteX, moteY}));
                }
        }

        // The light itself falls the same whether or not its note is held,
        // and leaves through the floor. The halo, the body, and a white
        // heart: the three together are what reads as a light rather than as
        // a coloured mark.
        if (t >= 1.0f) continue;
        g.setColour(colour.withAlpha(glow * 0.16f));
        g.fillEllipse(body.expanded(width * 0.9f, width * 0.6f));
        g.setColour(colour.withAlpha(glow * 0.92f));
        g.fillRoundedRectangle(body, width * 0.5f);
        g.setColour(juce::Colours::white.withAlpha(glow * 0.55f));
        g.fillRoundedRectangle(body.reduced(width * 0.3f, height * 0.3f), width * 0.25f);
    }
}

// The four slot buttons, two by two. The lit one is faint on purpose: it says
// which slot is playing, and the lamp beside it is the only thing on this
// plate that should be bright.
inline void drawArpSlots(juce::Graphics& g, juce::Rectangle<int> componentBounds, int chosen)
{
    for (int slot = 0; slot < arpSlotCount; ++slot)
    {
        const auto box = arpSlotButtonBounds(componentBounds, slot).toFloat().reduced(0.5f);
        const auto on = slot == chosen;
        g.setColour(on ? signalViolet.withAlpha(0.14f) : juce::Colour(0xff0b0e18));
        g.fillRoundedRectangle(box, 3.5f);
        g.setColour(on ? signalViolet.withAlpha(0.6f) : line.withAlpha(0.8f));
        g.drawRoundedRectangle(box, 3.5f, 1.0f);
        g.setColour(on ? text : mutedText);
        g.setFont(panelFont(Face::emphasis, 12.0f));
        g.drawText(juce::String(slot + 1), box, juce::Justification::centred);
    }
}

// Everything on the plate that follows the arp's state. The lamp follows
// arpEnable, the face follows whether the panes are showing, the buttons
// follow the slot, and the well follows the notes. Lamp and face are drawn as
// different things — a lit lamp against a lit outline — because an arp
// running with its settings put away and an arp on screen that is switched
// off are both ordinary, and one highlight for both would leave you unable to
// tell which you were in.
inline void drawArpPlateState(juce::Graphics& g, juce::Rectangle<int> componentBounds,
                              bool on, bool open, int slot,
                              const std::vector<NoteSpark>& sparks, double nowMs)
{
    const auto face = arpPlateFace(componentBounds).toFloat();
    // Lit while the panes are showing, to the face's own chamfered outline
    // rather than a rounded box laid over it: the light is the well's, so it
    // follows the well's corners. And faint — the lamp says whether the arp is
    // running, and this only has to say the door is open.
    if (open)
    {
        g.setColour(signalViolet.withAlpha(0.05f));
        g.fillPath(chamferedPath(face, arpPlateFaceCut));
        g.setColour(signalViolet.withAlpha(0.32f));
        g.strokePath(chamferedPath(face.reduced(0.5f), arpPlateFaceCut - 0.5f), juce::PathStrokeType(1.0f));
    }

    // The same lamp the module headers wear, drawn here rather than borrowed,
    // because this one is on the chassis instead of on a module and has no
    // component of its own to carry it.
    const auto ledBounds = arpPlateLedBounds(componentBounds).toFloat();
    const auto led = ledBounds.reduced(5.0f);
    if (on)
    {
        g.setColour(signalViolet.withAlpha(0.16f));
        g.fillEllipse(led.expanded(3.5f));
    }
    g.setColour(on ? signalViolet : juce::Colour(0xff232838));
    g.fillEllipse(led);
    g.setColour(on ? juce::Colours::white.withAlpha(0.5f) : mutedText.withAlpha(0.45f));
    g.drawEllipse(led, 1.0f);

    auto name = arpPlateNameBounds(componentBounds).toFloat();
    name = name.withTrimmedLeft(ledBounds.getRight() - name.getX() + 6.0f);
    g.setFont(panelFont(Face::header, 14.0f));
    g.setColour(on ? text : mutedText);
    drawTrackedText(g, "ARP", name.removeFromTop(name.getHeight() * 0.62f), 1.8f,
                    juce::Justification::centredLeft);
    stamp(g, open ? "CLOSE" : "SETTINGS", name, 8.0f, 0.7f);

    drawArpSlots(g, componentBounds, slot);
    drawArpWaterfall(g, arpWaterfallBounds(componentBounds), sparks, nowMs);
}
}
