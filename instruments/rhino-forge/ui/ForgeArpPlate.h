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
// it and spent once it has gone through the floor. Everything about its fall
// is settled when it is made except the moment its note is let go, so a frame
// only has to ask how old it is and whether it is still held: the panel
// repaints whole at 24 Hz, and a spark that had to be stepped would have to be
// stepped by every one of those repaints.
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

// Where a held note stops falling: past where a spark is fully lit and short
// of where it starts to go out, so a held note hangs about half way down the
// well at full strength instead of fading under a finger that is still down.
inline constexpr float sparkRestProgress = 0.6f;

// How long a held note, once it has stopped, takes to brighten all the way to
// ultra. Long enough that an ordinary sustained chord only glows, short enough
// that a pad held through a bar gets there.
inline constexpr double sparkChargeMs = 1800.0;

// How quickly ultra comes in once it is reached, so it arrives rather than pops.
inline constexpr double sparkUltraEaseMs = 250.0;

// The burst of light at the moment ultra is reached.
inline constexpr double sparkUltraFlashMs = 350.0;

// How long a released light takes to let go of the column it hung from and
// dim back to an ordinary spark on its way to the floor.
inline constexpr double sparkLetGoMs = 320.0;

// In ultra, how many pixels the light's width swells by at the crest of its
// ripple, how long one ripple takes, and how far apart two crests are on the
// column.
inline constexpr float sparkUltraSwellPx = 3.0f;
inline constexpr double sparkUltraRippleMs = 450.0;
inline constexpr float sparkUltraRipplePx = 14.0f;

// How many motes of stardust sprinkle round a light in ultra.
inline constexpr std::uint32_t sparkUltraMotes = 10;

// How old a spark is when it reaches its rest, if its note is still down.
inline double sparkRestAgeMs(const NoteSpark& spark)
{
    return static_cast<double>(sparkRestProgress) * sparkFallMs / static_cast<double>(spark.pace);
}

// How long a spark has hung at its rest with its note held: the time its fall
// has been stopped for. Stops counting when the note is let go.
inline double sparkParkedMs(const NoteSpark& spark, double nowMs)
{
    const auto heldUntil = spark.held() ? nowMs : juce::jmin(nowMs, spark.releasedMs);
    return juce::jmax(0.0, heldUntil - (spark.bornMs + sparkRestAgeMs(spark)));
}

// Where a spark has got to: 0 at the top of the well, 1 at the floor, and past
// 1 once it is spent. Its clock stops while it hangs at its rest, so a note let
// go after a long hold falls the rest of the way exactly as it would have.
inline float sparkProgress(const NoteSpark& spark, double nowMs)
{
    const auto fallingMs = nowMs - spark.bornMs - sparkParkedMs(spark, nowMs);
    return static_cast<float>(fallingMs * static_cast<double>(spark.pace) / sparkFallMs);
}

// How long a held note's trail, once it has stopped, takes to stretch up to
// the top of the well.
inline constexpr double sparkColumnMs = 500.0;

// What holding the note has done to its light. All zero for a note let go
// before it reached its rest, which is drawn exactly as every spark was before
// holding meant anything.
struct SparkHold
{
    // 1 while the note is held, easing to 0 once it is let go: how much of
    // everything below is still showing.
    float attached = 0.0f;
    // How far its trail has stretched towards the top of the well, 0 to 1:
    // the note still pouring in.
    float column = 0.0f;
    // How far it has brightened, 0 to 1.
    float charge = 0.0f;
    // How far into ultra, 0 to 1.
    float ultra = 0.0f;
    // How long since ultra was reached, or below zero if it never was.
    double ultraMs = -1.0;
};

inline SparkHold sparkHold(const NoteSpark& spark, double nowMs)
{
    SparkHold hold;
    hold.attached = spark.held() ? 1.0f
                                 : static_cast<float>(juce::jlimit(0.0, 1.0, 1.0 - (nowMs - spark.releasedMs) / sparkLetGoMs));
    const auto parked = sparkParkedMs(spark, nowMs);
    // Eased out, so the column shoots up and then settles against the top.
    const auto stretch = 1.0 - juce::jlimit(0.0, 1.0, parked / sparkColumnMs);
    hold.column = hold.attached * static_cast<float>(1.0 - stretch * stretch);
    hold.charge = hold.attached * static_cast<float>(juce::jlimit(0.0, 1.0, parked / sparkChargeMs));
    if (parked >= sparkChargeMs)
    {
        // Timed from the clock rather than from the hold, so the dust keeps
        // drifting while a released light lets go instead of freezing.
        hold.ultraMs = nowMs - (spark.bornMs + sparkRestAgeMs(spark) + sparkChargeMs);
        hold.ultra = hold.attached * static_cast<float>(juce::jlimit(0.0, 1.0, (parked - sparkChargeMs) / sparkUltraEaseMs));
    }
    return hold;
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
// the oldest light already let go is the one dropped, because a held note is
// the one a player is still watching.
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

// Let go of every light whose note the voices have stopped holding.
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

    for (const auto& spark : sparks)
    {
        const auto t = sparkProgress(spark, nowMs);
        if (t < 0.0f || t >= 1.0f) continue;
        const auto hold = sparkHold(spark, nowMs);
        // A little faster as it goes, the way anything falling does, and only a
        // little, so the fall reads as drifting rather than dropping.
        const auto drop = t * (0.7f + 0.3f * t);
        // Lit over its first tenth and put out over its last third, so it is
        // gone by the time it reaches the floor rather than cut off by it. A
        // held note rests short of that last third, so it stays lit.
        const auto glow = juce::jlimit(0.0f, 1.0f, juce::jmin(t / 0.1f, (1.0f - t) / 0.3f));
        const auto width = 2.5f + 4.0f * spark.weight;
        const auto height = width * 1.9f;
        const auto x = well.getX() + 4.0f + spark.across * (well.getWidth() - 8.0f);
        const auto y = well.getY() - height * 0.5f + drop * (well.getHeight() + height);
        // In ultra the light's width swells and narrows by a few pixels, in a
        // ripple that runs down the column into it.
        const auto swell = [&hold, y] (float atY)
        {
            if (hold.ultra <= 0.0f) return 0.0f;
            const auto ripple = static_cast<float>(hold.ultraMs / sparkUltraRippleMs) - (atY - y) / sparkUltraRipplePx;
            return hold.ultra * sparkUltraSwellPx
                 * (0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * ripple));
        };
        const auto body = juce::Rectangle<float>(width + swell(y), height).withCentre({x, y});
        // Held, it brightens towards white as it charges, and stops short of
        // it: a light gone all the way to white reads as grey against the
        // violet sky rather than as brighter.
        const auto colour = signalViolet.interpolatedWith(
            juce::Colours::white, juce::jmin(0.85f, 0.25f + 0.45f * spark.warmth + 0.35f * hold.charge));

        // The trail above the body, fading as it goes up: steps of one column
        // rather than a gradient, which is all a trail this narrow can show
        // and a good deal cheaper to fill. A held note's trail stretches up to
        // the top of the well and stays lit along it, the note still pouring
        // in; it is cut finer then, on whole pixels, so the ripple has rows
        // to move and the steps do not overlap into lines. Its colour moves
        // only half way to the light's, so it stays violet.
        const auto trail = 8.0f + 18.0f * spark.weight;
        const auto reach = juce::jmax(trail, trail + (body.getY() - well.getY() - trail) * hold.column);
        const auto steps = hold.column > 0.0f ? juce::jlimit(4, 64, static_cast<int>(std::ceil(reach / 2.0f))) : 4;
        const auto slice = reach / static_cast<float>(steps);
        const auto fade = 1.0f - 0.6f * hold.column;
        const auto trailColour = signalViolet.interpolatedWith(colour, 0.5f * hold.charge);
        for (int step = 0; step < steps; ++step)
        {
            const auto share = static_cast<float>(step) / static_cast<float>(steps);
            auto lower = body.getY() - slice * static_cast<float>(step);
            auto upper = lower - slice;
            auto overlap = 0.5f;
            if (hold.column > 0.0f)
            {
                lower = std::round(lower);
                upper = std::round(upper);
                overlap = 0.0f;
            }
            const auto columnWidth = width * 0.6f + swell((upper + lower) * 0.5f);
            g.setColour(trailColour.withAlpha(glow * (0.2f + 0.3f * hold.charge) * (1.0f - share * fade)));
            g.fillRect(juce::Rectangle<float>(x - columnWidth * 0.5f, upper, columnWidth, lower - upper + overlap));
        }
        // The halo, the body, and a white heart: the three together are what
        // reads as a light rather than as a coloured mark. Charging, the halo
        // spreads a little, a second, tighter one gathers round the body, and
        // the heart burns whiter: two halos falling off read as a glow where
        // one large one reads as a flat disc.
        g.setColour(colour.withAlpha(glow * (0.16f + 0.1f * hold.charge)));
        g.fillEllipse(body.expanded(width * (0.9f + 0.4f * hold.charge), width * (0.6f + 0.4f * hold.charge)));
        if (hold.charge > 0.0f)
        {
            g.setColour(colour.withAlpha(glow * 0.3f * hold.charge));
            g.fillEllipse(body.expanded(width * 0.45f, width * 0.3f));
        }
        if (hold.ultraMs >= 0.0 && hold.ultraMs < sparkUltraFlashMs)
        {
            const auto flash = 1.0f - static_cast<float>(hold.ultraMs / sparkUltraFlashMs);
            g.setColour(juce::Colours::white.withAlpha(0.3f * flash * hold.attached));
            g.fillEllipse(body.expanded(width * (1.0f + 3.0f * (1.0f - flash))));
        }
        g.setColour(colour.withAlpha(glow * 0.92f));
        g.fillRoundedRectangle(body, body.getWidth() * 0.5f);
        g.setColour(juce::Colours::white.withAlpha(glow * (0.55f + 0.4f * hold.charge)));
        g.fillRoundedRectangle(body.reduced(body.getWidth() * 0.3f, height * 0.3f), body.getWidth() * 0.25f);

        // Ultra's stardust: motes smaller than any spark, each appearing
        // somewhere along the column or beside the light, drifting out and
        // down as it twinkles, and turning up somewhere new on its next turn.
        if (hold.ultra <= 0.0f) continue;
        const auto columnTop = body.getY() - reach;
        for (std::uint32_t mote = 0; mote < sparkUltraMotes; ++mote)
        {
            const auto period = 650.0 + 600.0 * static_cast<double>(sparkNoise(spark.seed, mote, 0));
            const auto clock = hold.ultraMs + period * static_cast<double>(sparkNoise(spark.seed, mote, 1));
            const auto turn = static_cast<std::uint32_t>(clock / period);
            const auto life = static_cast<float>(clock / period - static_cast<double>(turn));
            const auto roll = [&spark, mote, turn] (std::uint32_t k)
            { return sparkNoise(spark.seed, mote, 2 + turn * 8 + k); };
            const auto side = roll(0) < 0.5f ? -1.0f : 1.0f;
            const auto moteX = x + side * (body.getWidth() * 0.5f + 1.0f + 5.0f * roll(1) + life * (2.0f + 4.0f * roll(3)));
            const auto moteY = columnTop + (body.getBottom() + 3.0f - columnTop) * roll(2) + life * (3.0f + 6.0f * roll(4));
            const auto size = 0.8f + 0.8f * roll(5);
            const auto twinkle = std::sin(juce::MathConstants<float>::pi * life);
            g.setColour(juce::Colours::white.interpolatedWith(colour, 0.5f * roll(6))
                            .withAlpha(hold.ultra * twinkle * (0.45f + 0.5f * roll(7))));
            g.fillRect(juce::Rectangle<float>(size, size).withCentre({moteX, moteY}));
        }
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
