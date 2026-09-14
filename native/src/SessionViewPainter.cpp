#include "SessionView.h"
#include <algorithm>

// Session view rendering.

namespace theta
{
namespace
{
constexpr auto backgroundColour = 0xff1d2228;
constexpr auto toolbarColour = 0xff20262c;
constexpr auto gridLineColour = 0xff2b333a;
constexpr auto emptySlotColour = 0xff242b31;
constexpr auto textColour = 0xffc4cbd1;
constexpr auto dimTextColour = 0xff8a969f;
constexpr auto playingColour = 0xffc6d58c;
constexpr auto queuedColour = 0xff5ab9d6;

void drawTriangle(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    juce::Path path;
    path.addTriangle(area.getX(), area.getY(),
                     area.getX(), area.getBottom(),
                     area.getRight(), area.getCentreY());
    g.setColour(colour);
    g.fillPath(path);
}

void drawSquare(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour);
    g.fillRect(area.withSizeKeepingCentre(area.getWidth() * 0.72f, area.getHeight() * 0.72f));
}
}

void SessionView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(backgroundColour));
    const auto trackCount = session.trackCount();
    const auto sceneCount = session.sceneCount();
    const auto rightEdge = sceneColumnX();
    const auto gridTop = toolbarHeight + trackHeaderHeight;

    // Clip slot grid.
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion({0, static_cast<int>(gridTop), static_cast<int>(rightEdge),
                            std::max(0, static_cast<int>(gridBottom() - gridTop))});
        for (int track = 0; track < trackCount; ++track)
        {
            const auto x = columnX(track);
            if (x > rightEdge || x + columnWidth < 0.0f) continue;
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                const auto cell = slotBounds(track, scene);
                if (cell.getBottom() < gridTop || cell.getY() > gridBottom()) continue;
                const auto info = session.slotClip(track, scene);
                auto inner = cell.reduced(2.0f, 2.0f);
                const auto isHovered = hovered.region == Region::slot && hovered.track == track
                                    && hovered.scene == scene;
                const auto isDropTarget = dropTarget.region == Region::slot && dropTarget.track == track
                                       && dropTarget.scene == scene;
                if (info.hasClip)
                {
                    auto fill = info.colour.withMultipliedSaturation(0.85f);
                    if (!info.playing) fill = fill.withMultipliedBrightness(0.62f);
                    if (isHovered) fill = fill.brighter(0.12f);
                    g.setColour(fill);
                    g.fillRect(inner);
                    if (info.playing || info.playQueued || info.stopQueued)
                    {
                        g.setColour(juce::Colour(info.playing ? playingColour : queuedColour));
                        g.drawRect(inner, 1.5f);
                    }
                    const auto glyph = inner.removeFromLeft(16.0f).withSizeKeepingCentre(8.0f, 9.0f);
                    if (info.stopQueued) drawSquare(g, glyph, juce::Colour(0xff11161b));
                    else drawTriangle(g, glyph, juce::Colour(0xff11161b));
                    g.setColour(juce::Colour(0xff11161b));
                    g.setFont(juce::FontOptions(11.0f));
                    g.drawText(info.name, inner.toNearestInt().withTrimmedLeft(2),
                               juce::Justification::centredLeft, false);
                }
                else
                {
                    g.setColour(juce::Colour(emptySlotColour).brighter(isHovered ? 0.18f : 0.0f));
                    g.fillRect(inner);
                    drawSquare(g, inner.withWidth(14.0f).withSizeKeepingCentre(7.0f, 7.0f),
                               juce::Colour(0xff4a555f));
                }
                if (isDropTarget)
                {
                    g.setColour(juce::Colour(playingColour));
                    g.drawRect(cell.reduced(2.0f, 2.0f), 2.0f);
                }
            }
            g.setColour(juce::Colour(gridLineColour));
            g.drawVerticalLine(static_cast<int>(x + columnWidth), gridTop, gridBottom());
        }
        if (trackCount == 0 || sceneCount == 0)
        {
            g.setColour(juce::Colour(dimTextColour));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText("No scenes yet", 12, static_cast<int>(gridTop) + 8, 240, 20,
                       juce::Justification::centredLeft);
        }
    }

    // Track headers.
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion({0, static_cast<int>(toolbarHeight), static_cast<int>(rightEdge),
                            static_cast<int>(trackHeaderHeight)});
        for (int track = 0; track < trackCount; ++track)
        {
            const auto header = trackHeaderBounds(track);
            if (header.getX() > rightEdge || header.getRight() < 0.0f) continue;
            g.setColour(juce::Colour(track == selectedTrack ? 0xff343f47 : 0xff262d34));
            g.fillRect(header.reduced(1.0f, 0.0f));
            if (track == selectedTrack)
            {
                g.setColour(juce::Colour(playingColour));
                g.fillRect(header.withHeight(2.0f).reduced(1.0f, 0.0f));
            }
            g.setColour(juce::Colour(textColour));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track),
                       header.toNearestInt().reduced(6, 4), juce::Justification::centredLeft, false);
        }
    }

    // Track stop row.
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion({0, static_cast<int>(gridBottom()), static_cast<int>(rightEdge),
                            static_cast<int>(stopRowHeight)});
        g.setColour(juce::Colour(toolbarColour));
        g.fillRect(0.0f, gridBottom(), rightEdge, stopRowHeight);
        for (int track = 0; track < trackCount; ++track)
        {
            const auto stop = trackStopBounds(track);
            if (stop.getX() > rightEdge || stop.getRight() < 0.0f) continue;
            const auto isHovered = hovered.region == Region::trackStop && hovered.track == track;
            const auto active = session.trackHasActiveSlot(track);
            g.setColour(juce::Colour(isHovered ? 0xff333c44 : 0xff272e35));
            g.fillRect(stop.reduced(2.0f, 3.0f));
            drawSquare(g, stop.reduced(2.0f, 3.0f).withWidth(16.0f).withSizeKeepingCentre(8.0f, 8.0f),
                       juce::Colour(active ? playingColour : 0xff5d6871));
        }
    }

    // Scene column: launch buttons on the right, as in Live.
    {
        const auto column = sceneColumnBounds();
        g.setColour(juce::Colour(0xff222930));
        g.fillRect(column);
        g.setColour(juce::Colour(gridLineColour));
        g.drawVerticalLine(static_cast<int>(column.getX()), toolbarHeight, static_cast<float>(getHeight()));
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion({static_cast<int>(column.getX()), static_cast<int>(gridTop),
                            static_cast<int>(sceneColumnWidth),
                            std::max(0, static_cast<int>(gridBottom() - gridTop))});
        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const auto row = sceneLaunchBounds(scene);
            if (row.getBottom() < gridTop || row.getY() > gridBottom()) continue;
            const auto isHovered = hovered.region == Region::sceneLaunch && hovered.scene == scene;
            g.setColour(juce::Colour(isHovered ? 0xff333c44 : 0xff272e35));
            g.fillRect(row.reduced(2.0f));
            drawTriangle(g, row.reduced(2.0f).withWidth(16.0f).withSizeKeepingCentre(8.0f, 9.0f),
                         juce::Colour(isHovered ? playingColour : 0xff9aa5ae));
            g.setColour(juce::Colour(textColour));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(session.sceneName(scene), row.reduced(2.0f).toNearestInt().withTrimmedLeft(20),
                       juce::Justification::centredLeft, false);
        }
    }

    // Toolbar last so it covers any row scrolled up underneath it.
    g.setColour(juce::Colour(toolbarColour));
    g.fillRect(0.0f, 0.0f, static_cast<float>(getWidth()), toolbarHeight);
    g.setColour(juce::Colour(dimTextColour));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("QUANTISE", 106, 4, 80, 22, juce::Justification::centredLeft);
    g.drawText("Click a clip to launch / right-click a slot to fill it",
               290, 4, std::max(0, static_cast<int>(sceneColumnX()) - 300), 22,
               juce::Justification::centredLeft);
    g.setColour(juce::Colour(gridLineColour));
    g.drawHorizontalLine(static_cast<int>(toolbarHeight), 0.0f, static_cast<float>(getWidth()));
    g.drawHorizontalLine(static_cast<int>(toolbarHeight + trackHeaderHeight), 0.0f, sceneColumnX());
    g.drawHorizontalLine(static_cast<int>(gridBottom()), 0.0f, static_cast<float>(getWidth()));
}

}
