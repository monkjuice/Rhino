#pragma once

#include "ForgeStyle.h"

// The picture tube: the curved face, its bezel and brackets, the phosphor a
// trace is drawn in, and the wave paths that go on it. What each module
// actually plots is its own file's — this is the glass.
namespace rhino::forge::ui
{
// --- Drawing a wave ----------------------------------------------------------
//
// One shape, used by the small tube on the oscillator and by the big canvas in
// the table editor, so the two cannot drift apart.

// The trace of a wave across a box: phase runs left to right, +1 sits at the
// top. The curve is sampled rather than handed in, because the small tube reads
// a morph between two frames and the canvas reads one frame exactly.
inline juce::Path wavePath(juce::Rectangle<float> box, const std::function<float(float)>& at, int points)
{
    juce::Path path;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = box.getX() + phase * box.getWidth();
        const auto y = box.getCentreY() - juce::jlimit(-1.0f, 1.0f, at(phase)) * box.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    return path;
}

// The area a wave encloses against its zero line, which is how Serum and Vital
// both draw one and what gives a wave its weight on screen instead of leaving
// it a hairline.
//
// It is one path, not two. The trace already ends on the right-hand edge, so
// running it back along the zero line and closing it makes a figure that
// crosses itself wherever the wave crosses zero: the humps above the line and
// the humps below it wind in opposite directions. Under the non-zero rule both
// are filled and the space outside them is not, which is exactly the region
// between the curve and the line, however many times it changes sides. Filling
// each half separately would need the crossings found first, and this needs
// none of them.
inline void fillWaveArea(juce::Graphics& g, juce::Rectangle<float> box, const juce::Path& trace,
                         juce::Colour colour, float alpha)
{
    auto area = trace;
    area.lineTo(box.getRight(), box.getCentreY());
    area.lineTo(box.getX(), box.getCentreY());
    area.closeSubPath();

    // Brightest against the curve and falling away toward the line, so a tall
    // excursion reads as further from zero rather than merely as more ink.
    //
    // Painted twice, once from each edge toward the line, because one gradient
    // cannot be bright at the top and at the bottom of the same box. The two
    // overlap only where the fill is faintest.
    juce::ColourGradient above(colour.withAlpha(0.34f * alpha), box.getCentreX(), box.getY(),
                               colour.withAlpha(0.06f * alpha), box.getCentreX(), box.getCentreY(), false);
    g.setGradientFill(above);
    g.fillPath(area);
    juce::ColourGradient below(colour.withAlpha(0.34f * alpha), box.getCentreX(), box.getBottom(),
                               colour.withAlpha(0.06f * alpha), box.getCentreX(), box.getCentreY(), false);
    g.setGradientFill(below);
    g.fillPath(area);
}

// Rounded joins rather than the default mitre: an envelope whose decay is a
// millisecond turns two of its corners into near-spikes, and a mitre there
// shoots a splinter of the fat halo stroke well clear of the shape.
inline void strokeGlow(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float alpha)
{
    const auto joined = [] (float width)
    {
        return juce::PathStrokeType(width, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded);
    };
    g.setColour(colour.withAlpha(0.16f * alpha));
    g.strokePath(path, joined(7.0f));
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, joined(1.8f));
}

// --- The picture tube -------------------------------------------------------
//
// An oscillator draws its wave on a small CRT: a black surround, a bowed glass
// face lit from the middle, scanlines, and a bezel that glows where the tube's
// edge catches its own light.

inline constexpr float crtBezel = 6.0f;

// The glass face, inset from the well so the bezel glow has somewhere to sit.
inline juce::Rectangle<float> crtFace(juce::Rectangle<int> well)
{
    return well.toFloat().reduced(crtBezel);
}

// The silhouette of a picture tube: corners pulled well in, edges bowed gently
// outward. It is a path rather than a rounded rectangle because the bow is what
// makes it read as glass instead of a box.
inline juce::Path crtPath(juce::Rectangle<float> face)
{
    const auto corner = juce::jmin(face.getHeight() * 0.34f, face.getWidth() * 0.06f, 18.0f);
    // A quadratic's control point pulls the curve half way toward it, so the
    // glass bulges by half of this.
    const auto bow = 4.0f;
    const auto l = face.getX(), t = face.getY(), r = face.getRight(), b = face.getBottom();
    const auto cx = face.getCentreX(), cy = face.getCentreY();

    juce::Path tube;
    tube.startNewSubPath(l + corner, t);
    tube.quadraticTo(cx, t - bow, r - corner, t);
    tube.quadraticTo(r, t, r, t + corner);
    tube.quadraticTo(r + bow, cy, r, b - corner);
    tube.quadraticTo(r, b, r - corner, b);
    tube.quadraticTo(cx, b + bow, l + corner, b);
    tube.quadraticTo(l, b, l, b - corner);
    tube.quadraticTo(l - bow, cy, l, t + corner);
    tube.quadraticTo(l, t, l + corner, t);
    tube.closeSubPath();
    return tube;
}

// The L-shaped marks in a screen's corners: the registration a scope face
// carries. Drawn inside the glass rather than on the frame, so they read as
// part of what is being shown rather than as decoration around it.
inline void drawDisplayBrackets(juce::Graphics& g, juce::Rectangle<float> face, juce::Colour colour,
                                float alpha)
{
    const auto arm = juce::jmin(9.0f, face.getWidth() * 0.08f, face.getHeight() * 0.14f);
    if (arm < 3.0f) return;
    const auto inset = 5.0f;
    const auto box = face.reduced(inset);

    juce::Path marks;
    const auto bracket = [&marks, arm] (float x, float y, float dx, float dy)
    {
        marks.startNewSubPath(x + dx * arm, y);
        marks.lineTo(x, y);
        marks.lineTo(x, y + dy * arm);
    };
    bracket(box.getX(),     box.getY(),       1.0f,  1.0f);
    bracket(box.getRight(), box.getY(),      -1.0f,  1.0f);
    bracket(box.getX(),     box.getBottom(),  1.0f, -1.0f);
    bracket(box.getRight(), box.getBottom(), -1.0f, -1.0f);

    g.setColour(colour.withAlpha(0.55f * alpha));
    g.strokePath(marks, juce::PathStrokeType(1.3f));
}

inline void drawCrtScreen(juce::Graphics& g, juce::Rectangle<int> well,
                          juce::Colour phosphor, float alpha)
{
    const auto face = crtFace(well);
    if (face.getWidth() <= 1.0f || face.getHeight() <= 1.0f) return;
    const auto tube = crtPath(face);
    const auto cx = face.getCentreX(), cy = face.getCentreY();

    // Nothing is painted around the tube. The corners the bowed glass leaves
    // behind would otherwise be a darker patch sitting on the panel, so the
    // tube is left to float on whatever is already there and they disappear.

    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(tube);

        g.setColour(juce::Colour(0xff01030c));
        g.fillPath(tube);

        // The tube's own light, brightest at the middle of the face and falling
        // away toward the corners the way a phosphor screen does.
        const auto radius = juce::jmax(face.getWidth(), face.getHeight()) * 0.60f;
        juce::ColourGradient bloom(phosphor.withAlpha(0.34f * alpha), cx, cy,
                                   phosphor.withAlpha(0.0f), cx + radius, cy, true);
        bloom.addColour(0.45, phosphor.withAlpha(0.14f * alpha));
        g.setGradientFill(bloom);
        g.fillPath(tube);

        // The face is much wider than it is tall, so the bloom alone leaves the
        // top and bottom lit. Darken them back down for a vignette on all sides.
        const auto shade = juce::Colour(0xdd000208);
        juce::ColourGradient top(shade, cx, face.getY(), juce::Colours::transparentBlack, cx, cy, false);
        g.setGradientFill(top);
        g.fillPath(tube);
        juce::ColourGradient bottom(shade, cx, face.getBottom(), juce::Colours::transparentBlack, cx, cy, false);
        g.setGradientFill(bottom);
        g.fillPath(tube);

        // Scanlines. Three pixels apart is close enough to read as a raster
        // without turning into a moire against the trace.
        g.setColour(juce::Colour(0xff000000).withAlpha(0.22f));
        for (auto y = face.getY(); y < face.getBottom(); y += 3.0f)
            g.fillRect(face.getX(), y, face.getWidth(), 1.0f);

        // The graticule, as faint as one etched on the glass: six divisions
        // across and quarters down. It is what makes the trace read as
        // measured rather than drawn, and it is what the corner marks imply.
        g.setColour(phosphor.withAlpha(0.06f * alpha));
        for (int division = 1; division < 6; ++division)
            g.fillRect(face.getX() + face.getWidth() * (float) division / 6.0f, face.getY(),
                       1.0f, face.getHeight());
        for (const auto share : {0.25f, 0.75f})
            g.fillRect(face.getX(), face.getY() + face.getHeight() * share, face.getWidth(), 1.0f);

        // The zero axis, brighter than the rest of the graticule.
        g.setColour(phosphor.withAlpha(0.16f * alpha));
        g.fillRect(face.getX() + 4.0f, cy, face.getWidth() - 8.0f, 1.0f);

        // Registration marks in the corners, on the glass with the graticule
        // rather than on the bezel around it.
        drawDisplayBrackets(g, face, phosphor, alpha);
    }

    // The bezel: widest and faintest first, so the edge blooms outward.
    g.setColour(phosphor.withAlpha(0.10f * alpha));
    g.strokePath(tube, juce::PathStrokeType(7.0f));
    g.setColour(phosphor.withAlpha(0.22f * alpha));
    g.strokePath(tube, juce::PathStrokeType(3.5f));
    g.setColour(phosphor.withAlpha(0.80f * alpha));
    g.strokePath(tube, juce::PathStrokeType(1.4f));
}

// A phosphor trace: a wide halo, a soft body, and a core hot enough to have
// burnt toward white, which is what makes a CRT line look lit rather than drawn.
inline void strokePhosphor(juce::Graphics& g, const juce::Path& path,
                           juce::Colour phosphor, float alpha)
{
    g.setColour(phosphor.withAlpha(0.16f * alpha));
    g.strokePath(path, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(phosphor.withAlpha(0.55f * alpha));
    g.strokePath(path, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(phosphor.interpolatedWith(juce::Colours::white, 0.7f).withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

inline constexpr float displayCorner = 3.0f;

// The well's own outline, for a trace to be clipped against. A curve is drawn
// the full width of its display and cut off here, so it reaches both edges and
// runs behind the frame rather than stopping short of it with a margin of dead
// space at each end.
inline juce::Path displayClip(juce::Rectangle<int> area)
{
    juce::Path clip;
    clip.addRoundedRectangle(area.toFloat(), displayCorner);
    return clip;
}

inline void drawDisplayWell(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto box = area.toFloat();
    drawWell(g, box, juce::Colour(0xff0a0d16), 1.0f, displayCorner);
    g.setColour(line.withAlpha(0.45f));
    g.drawHorizontalLine(area.getCentreY(), box.getX(), box.getRight());
}

// The oscillator's own table on its tube. The table is handed in rather than
// looked up, because each oscillator now has one of its own and the panel has
// to draw the one that oscillator is actually reading.
// The warp stages are handed in as well, because what an oscillator sounds like
// and what it draws are the same thing: the trace is the table read through the
// very warps the voice is reading it through.
//
// Not all of them, though. A filter mode runs against time and an FM mode reads
// another source in the voice, so neither is a picture of a table and both take
// themselves off the trace rather than drawing something untrue. That is the
// same line the Serum manual draws when it says the 2D view shows what Sync,
// Alt Warp and Distortion are doing and says nothing about the rest.
inline void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area, const rhino::forge::WavetableEdit& table,
                         float position,
                         const std::array<rhino::forge::WarpStage, rhino::forge::warpSlots>& warp,
                         juce::Colour colour, float alpha)
{
    const auto face = crtFace(area);
    // Full width of the glass: the trace runs off both edges and is cut by the
    // tube, the way a scope's is, rather than stopping short inside it.
    const auto box = face.reduced(0.0f, 5.0f).withSizeKeepingCentre(face.getWidth(),
                                                                    face.getHeight() * 0.88f);
    std::array<rhino::forge::WarpStage, rhino::forge::warpSlots> drawn {};
    for (int slot = 0; slot < rhino::forge::warpSlots; ++slot)
    {
        const auto& stage = warp[static_cast<size_t>(slot)];
        if (rhino::forge::warpShapesWaveform(stage.mode)) drawn[static_cast<size_t>(slot)] = stage;
    }
    // None of the modes that reach the trace keep any, so a fresh pair of these
    // per point would do; they are hoisted out so the reader beside them is the
    // same expression the voice uses.
    rhino::forge::WarpState first, second;
    const auto path = wavePath(box, [&] (float phase)
    {
        const auto read = [&table, position] (float p) { return table.sample(position, p); };
        return rhino::forge::warpRead(drawn[1], phase, second, [&] (float p)
                                      { return rhino::forge::warpRead(drawn[0], p, first, read); });
    }, 220);
    // Clipped to the glass, so the halo stops at the bezel rather than spilling
    // over the tube's edge.
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(crtPath(face));
    fillWaveArea(g, box, path, colour, alpha);
    strokePhosphor(g, path, colour, alpha);
}
}
