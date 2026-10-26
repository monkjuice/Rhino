#pragma once

#include "DuckArt.h"

#include <array>
#include <cmath>
#include <functional>

// The gain-reduction meter: a rubber duck in a tank of water that sinks as the
// duck goes deeper and rises as it lets go. A real meter, not decoration: its
// depth is the deepest gain the engine applied since the last frame, Mix and
// the band split included, placed on the scale drawn beside it, with the
// figure underneath. The waterline never moves; only the duck does.
namespace rhino::duck::ui
{
// The scale's marks, evenly spaced down the tank as drawn, and -inf at the foot.
inline constexpr std::array<float, 5> meterTicksDb { 0.0f, -3.0f, -6.0f, -12.0f, -18.0f };
constexpr float meterFloorDb = -48.0f;   // the end of the last fifth; anything deeper reads as -inf

// Where a reduction sits on the meter, 0 at 0 dB to 1 at -inf, along the
// scale's own spacing.
inline float meterDepthForDb(float db) noexcept
{
    if (!(db < 0.0f)) return 0.0f;
    constexpr auto fifth = 1.0f / static_cast<float>(meterTicksDb.size());
    for (size_t i = 1; i < meterTicksDb.size(); ++i)
        if (db >= meterTicksDb[i])
            return (static_cast<float>(i - 1) + (meterTicksDb[i - 1] - db) / (meterTicksDb[i - 1] - meterTicksDb[i])) * fifth;
    const auto last = meterTicksDb.back();
    return std::min(1.0f, 1.0f - fifth + fifth * (last - db) / (last - meterFloorDb));
}

inline float gainToDb(float gain) noexcept { return gain > 1.0e-5f ? 20.0f * std::log10(gain) : -100.0f; }

// "-7.8 dB", "0.0 dB", or minus infinity below the scale's floor.
inline juce::String reductionText(float db)
{
    if (db <= meterFloorDb) return juce::String::fromUTF8("-\xe2\x88\x9e dB");
    const auto tenths = juce::roundToInt(db * 10.0f);
    return tenths == 0 ? juce::String("0.0 dB") : juce::String(static_cast<float>(tenths) / 10.0f, 1) + " dB";
}

class DuckMeter final : public juce::Component,
                        private juce::Timer
{
public:
    // A frame's news from the engine: the deepest gain since the last frame,
    // above 1 when no block has run since, and how many cycles have started.
    struct Reading
    {
        float gain = 2.0f;
        int cycles = 0;
    };

    DuckMeter() { setOpaque(true); }
    ~DuckMeter() override { stopTimer(); }

    // Where readings come from. With one set the meter runs itself at 60 Hz
    // and repaints only while something moves.
    void setSource(std::function<Reading()> source)
    {
        read = std::move(source);
        if (read) startTimerHz(60);
        else stopTimer();
    }

    // Moves the animation `seconds` on toward `reading`. True when the
    // picture changed enough to be worth a repaint.
    bool step(Reading reading, float seconds)
    {
        seconds = std::clamp(seconds, 0.0f, 0.1f);
        if (reading.gain <= 1.0f)
        {
            gain = std::max(0.0f, reading.gain);
            quietFor = 0.0f;
        }
        else if ((quietFor += seconds) > 0.25f)
        {
            gain = 1.0f;   // nothing is being processed: the duck surfaces
        }
        const auto db = gainToDb(gain);
        goal = meterDepthForDb(db);

        // A light spring: it sinks briskly and settles with a small bob.
        const auto before = position;
        for (auto left = seconds; left > 0.0f; left -= substep)
        {
            const auto dt = std::min(left, substep);
            velocity += ((goal - position) * stiffness - velocity * damping) * dt;
            position += velocity * dt;
        }

        // A ripple when a cycle starts that ducks.
        if (reading.cycles != lastCycles)
        {
            if (lastCycles >= 0 && db < -0.5f) ripple = 1.0f;
            lastCycles = reading.cycles;
        }
        ripple *= std::exp(-seconds / 0.45f);
        if (ripple < 0.01f) ripple = 0.0f;
        else wave += seconds * 7.0f;

        // The figure holds its deepest reading half a second, then follows.
        if (db < heldDb)
        {
            heldDb = db;
            holdLeft = 0.5f;
        }
        else if ((holdLeft -= seconds) <= 0.0f)
        {
            heldDb += (db - heldDb) * std::min(1.0f, seconds * 6.0f);
        }
        const auto text = reductionText(heldDb);

        const auto moved = std::abs(position - before) > 1.0e-4f || std::abs(velocity) > 1.0e-3f || ripple > 0.0f || text != shownText;
        shownText = text;
        return moved;
    }

    // ---- what the tests ask -------------------------------------------------
    float depth() const noexcept { return position; }   // how far the duck has sunk, 0 to about 1
    float targetDepth() const noexcept { return goal; }
    const juce::String& figure() const noexcept { return shownText; }

    juce::Rectangle<float> glass() const { return getLocalBounds().toFloat().reduced(7.0f); }
    float waterlineY() const { const auto inside = glass(); return inside.getY() + inside.getHeight() * 0.4f; }
    juce::Rectangle<float> scaleArea() const
    {
        const auto inside = glass();
        return { inside.getRight() - 40.0f, inside.getY() + 18.0f, 30.0f, inside.getHeight() - 76.0f };
    }
    // Where the duck is drawn now: lower in the water the deeper it sinks.
    juce::Rectangle<float> duckArea() const
    {
        const auto inside = glass();
        const auto width = (scaleArea().getX() - inside.getX() - 14.0f) * 0.9f;
        const auto height = width * (art.duckBounds().getHeight() / art.duckBounds().getWidth());
        const auto draft = 0.3f + 0.42f * position;   // the share of the duck below the water
        const auto left = inside.getX() + 8.0f + ((scaleArea().getX() - inside.getX() - 14.0f) - width) * 0.5f;
        return { left, waterlineY() - (1.0f - draft) * height, width, height };
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colour::plate);
        const auto bounds = getLocalBounds().toFloat();
        const auto inside = glass();
        const auto sunk = std::clamp(position, 0.0f, 1.0f);

        // The frame round the glass.
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3e45), bounds.getX(), bounds.getY(),
                                               juce::Colour(0xff121417), bounds.getRight(), bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 11.0f);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 11.0f, 1.0f);

        // The tank, warmer under its lamps the deeper the duck goes.
        juce::Path tank;
        tank.addRoundedRectangle(inside, 7.0f);
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(tank);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff201710), inside.getX(), inside.getY(),
                                               juce::Colour(0xff0a0b0d), inside.getX(), inside.getBottom(), false));
        g.fillRect(inside);
        const auto glow = 0.18f + 0.42f * sunk;
        g.setGradientFill(juce::ColourGradient(colour::accent.withAlpha(glow), inside.getCentreX(), inside.getY(),
                                               colour::accent.withAlpha(0.0f), inside.getCentreX(), inside.getY() + inside.getHeight() * 0.75f, true));
        g.fillRect(inside);
        for (const auto x : { 0.3f, 0.7f })
        {
            const auto lamp = juce::Rectangle<float>(inside.getWidth() * 0.16f, 3.0f)
                                  .withCentre({ inside.getX() + inside.getWidth() * x, inside.getY() + 6.0f });
            g.setColour(colour::accentHot.withAlpha(0.45f + 0.5f * sunk));
            g.fillRoundedRectangle(lamp, 1.5f);
        }

        // The scale and the mark that reads it.
        const auto scale = scaleArea();
        g.setColour(colour::accent.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(1.0f, scale.getHeight()).withPosition(scale.getX(), scale.getY()));
        g.setFont(font(Face::reading, 10.5f));
        const auto tickY = [&scale] (float at) { return scale.getY() + at * scale.getHeight(); };
        for (size_t i = 0; i <= meterTicksDb.size(); ++i)
        {
            const auto at = static_cast<float>(i) / static_cast<float>(meterTicksDb.size());
            const auto y = tickY(at);
            g.setColour(colour::accent.withAlpha(0.55f));
            g.fillRect(juce::Rectangle<float>(5.0f, 1.0f).withPosition(scale.getX(), y - 0.5f));
            const auto label = i < meterTicksDb.size() ? juce::String(juce::roundToInt(meterTicksDb[i])) : juce::String::fromUTF8("-\xe2\x88\x9e");
            g.setColour(colour::accent.withAlpha(0.8f));
            g.drawText(label, juce::Rectangle<float>(26.0f, 12.0f).withPosition(scale.getX() + 8.0f, y - 6.0f), juce::Justification::centredLeft, false);
        }
        juce::Path pointer;
        const auto pointerY = tickY(sunk);
        pointer.addTriangle(scale.getX() - 7.0f, pointerY - 4.0f, scale.getX() - 7.0f, pointerY + 4.0f, scale.getX() - 1.0f, pointerY);
        g.setColour(colour::accentHot);
        g.fillPath(pointer);

        // Bubbles that rest in the water, the duck, then the water over its
        // lower half so what is under it reads as under it.
        const auto waterY = waterlineY();
        g.setColour(colour::water.withAlpha(0.16f));
        for (const auto& [x, y, r] : bubbles)
            g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre({ inside.getX() + x * inside.getWidth(),
                                                                                   waterY + y * (inside.getBottom() - waterY) }), 0.8f);
        const auto tilt = std::clamp(velocity * 0.03f, -0.026f, 0.026f);
        const auto squash = std::clamp(velocity * 0.012f, 0.0f, 0.02f);
        drawDuck(g, art, duckArea(), { tilt, squash });

        juce::Path surface;
        const auto steps = 48;
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = inside.getX() + inside.getWidth() * static_cast<float>(i) / static_cast<float>(steps);
            const auto y = waterY + ripple * 2.4f * std::sin(x * 0.12f - wave);
            if (i == 0) surface.startNewSubPath(x, y);
            else surface.lineTo(x, y);
        }
        auto water = surface;
        water.lineTo(inside.getRight(), inside.getBottom());
        water.lineTo(inside.getX(), inside.getBottom());
        water.closeSubPath();
        g.setGradientFill(juce::ColourGradient(colour::water.withAlpha(0.12f), inside.getX(), waterY,
                                               colour::water.withAlpha(0.22f), inside.getX(), inside.getBottom(), false));
        g.fillPath(water);
        g.setColour(colour::water.withAlpha(0.6f));
        g.strokePath(surface, juce::PathStrokeType(1.2f));

        // The figure, over the water at the foot of the tank.
        auto readout = inside.withTop(inside.getBottom() - 54.0f);
        g.setColour(colour::accent);
        g.setFont(font(Face::reading, 24.0f));
        g.drawText(shownText, readout.removeFromTop(32.0f), juce::Justification::centredBottom, false);
        g.setColour(colour::textDim);
        g.setFont(font(Face::label, 12.0f).withExtraKerningFactor(0.12f));
        g.drawText("GAIN REDUCTION", readout.removeFromTop(16.0f), juce::Justification::centred, false);

        // The glass: a faint reflection and a warm edge.
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), inside.getX(), inside.getY(),
                                               juce::Colours::transparentWhite, inside.getX() + inside.getWidth() * 0.5f, inside.getY() + inside.getHeight() * 0.35f, false));
        g.fillRect(inside);
        g.setColour(colour::accent.withAlpha(0.22f));
        g.drawRoundedRectangle(inside.reduced(0.5f), 7.0f, 1.2f);
    }

private:
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto seconds = lastTick > 0.0 ? static_cast<float>((now - lastTick) / 1000.0) : 1.0f / 60.0f;
        lastTick = now;
        if (read && step(read(), seconds)) repaint();
    }

    // Quick enough to follow a duck on every eighth note, about 50 ms to
    // arrive, and a little under critical damping: one small bob.
    static constexpr float stiffness = 900.0f;
    static constexpr float damping = 38.0f;
    static constexpr float substep = 1.0f / 240.0f;
    // Where the resting bubbles sit: across the tank, down the water, radius.
    static constexpr std::array<std::array<float, 3>, 7> bubbles { { { 0.12f, 0.35f, 2.0f }, { 0.2f, 0.7f, 1.5f }, { 0.34f, 0.5f, 1.2f },
                                                                     { 0.58f, 0.62f, 1.8f }, { 0.66f, 0.3f, 1.2f }, { 0.08f, 0.82f, 1.4f },
                                                                     { 0.46f, 0.86f, 1.0f } } };

    DuckArt art;
    std::function<Reading()> read;
    double lastTick = 0.0;
    float gain = 1.0f, goal = 0.0f, position = 0.0f, velocity = 0.0f, quietFor = 0.0f;
    float ripple = 0.0f, wave = 0.0f;
    float heldDb = 0.0f, holdLeft = 0.0f;
    int lastCycles = -1;
    juce::String shownText { "0.0 dB" };
};
}
