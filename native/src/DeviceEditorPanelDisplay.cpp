#include "DeviceEditorPanel.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

// What a device describes beside its controls (DeviceDisplay), drawn on its
// generated face: a diagram of the blocks it names and the traces it plays.
//
// The diagram is laid out from the links alone. An output block sits on the
// bottom row and is wired to a bus beneath it; any other block sits one row
// above the highest block it feeds, over the blocks it feeds, so every link
// runs downwards. That is how an FM algorithm is conventionally drawn, and it
// is worked out here rather than drawn per algorithm, so a device can say what
// is wired to what and nothing about where.
//
// Everything is placed, and every trace stroked, when the face is laid out,
// which is when the device or the size changes. Painting only fills what is
// already built, and skips the display when a repaint does not reach it, as
// a knob turning beside it does.
namespace rhino
{
namespace
{
constexpr float blockWidth = 26.0f;
constexpr float blockHeight = 19.0f;
constexpr float blockGap = 8.0f;    // between blocks in a row
constexpr float rowGap = 12.0f;     // between rows, where links run
constexpr float busDrop = 9.0f;     // from the bottom row to the bus
constexpr int diagramMargin = 8;
constexpr int traceWidth = 172;
constexpr int partGap = 8;
constexpr float traceThickness = 1.4f;

// Each block's row, counted up from the bottom.
std::vector<int> rowsOf(const DeviceDisplay& display)
{
    const auto count = static_cast<int>(display.blocks.size());
    std::vector<int> rows(display.blocks.size(), 0);
    // Each pass settles at least one more block of a chain, so as many passes
    // as there are blocks settle every chain. A loop between two blocks is
    // not a chain and would climb for ever; the cap keeps it on the diagram.
    for (int pass = 0; pass < count; ++pass)
        for (const auto& link : display.links)
            if (link.from != link.to && juce::isPositiveAndBelow(link.from, count) && juce::isPositiveAndBelow(link.to, count))
                rows[static_cast<size_t>(link.from)] = std::min(count - 1, std::max(rows[static_cast<size_t>(link.from)],
                                                                                   rows[static_cast<size_t>(link.to)] + 1));
    return rows;
}
}

int DeviceEditorPanel::displayWidth() const
{
    auto width = 0;
    if (!display.blocks.empty())
    {
        // Room for every block side by side, whatever the links say now, so
        // the face keeps its width when the device rewires itself.
        const auto blocks = static_cast<float>(display.blocks.size());
        width = diagramMargin * 2 + juce::roundToInt(blocks * blockWidth + (blocks - 1.0f) * blockGap);
    }
    if (!display.traces.empty())
        width += (width > 0 ? partGap : 0) + traceWidth;
    return width;
}

void DeviceEditorPanel::layoutDisplay(juce::Rectangle<int> area, int titleHeight)
{
    displayArea = area;
    displayBlocks.clear();
    displayTraces.clear();
    displaySelected = -1;
    // The title row says which block is in hand and names the traces, where
    // every other part of the face keeps its title.
    auto body = area;
    captionArea = body.removeFromTop(titleHeight);
    diagramArea = {};
    traceArea = {};
    if (!display.blocks.empty())
    {
        const auto blocks = static_cast<float>(display.blocks.size());
        diagramArea = body.removeFromLeft(diagramMargin * 2 + juce::roundToInt(blocks * blockWidth + (blocks - 1.0f) * blockGap));
        if (!display.traces.empty())
            body.removeFromLeft(partGap);
    }
    if (!display.traces.empty())
        traceArea = body.reduced(0, 4);

    if (!display.blocks.empty())
    {
        const auto rows = rowsOf(display);
        const auto topRow = *std::max_element(rows.begin(), rows.end());
        const auto pitch = blockWidth + blockGap;
        const auto rowPitch = blockHeight + rowGap;
        const auto height = static_cast<float>(topRow + 1) * blockHeight + static_cast<float>(topRow) * rowGap + busDrop;
        const auto top = static_cast<float>(diagramArea.getY()) + (static_cast<float>(diagramArea.getHeight()) - height) * 0.5f;
        displayBusY = top + height;
        const auto centre = static_cast<float>(diagramArea.getCentreX());
        const auto leftmost = static_cast<float>(diagramArea.getX() + diagramMargin) + blockWidth * 0.5f;
        const auto rightmost = static_cast<float>(diagramArea.getRight() - diagramMargin) - blockWidth * 0.5f;
        std::vector<float> centres(display.blocks.size(), centre);
        for (int row = 0; row <= topRow; ++row)
        {
            std::vector<int> members;
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i] == row)
                    members.push_back(static_cast<int>(i));
            if (members.empty())
                continue;
            // Where each block would like to be: the bottom row spread evenly
            // in order, and every block above over the middle of what it feeds.
            std::vector<float> wanted(members.size(), centre);
            for (size_t k = 0; k < members.size(); ++k)
            {
                if (row == 0)
                {
                    wanted[k] = centre + (static_cast<float>(k) - static_cast<float>(members.size() - 1) * 0.5f) * pitch;
                    continue;
                }
                auto sum = 0.0f;
                auto fed = 0;
                for (const auto& link : display.links)
                    if (link.from == members[k] && link.to != link.from
                        && juce::isPositiveAndBelow(link.to, static_cast<int>(centres.size())))
                    {
                        sum += centres[static_cast<size_t>(link.to)];
                        ++fed;
                    }
                if (fed > 0)
                    wanted[k] = sum / static_cast<float>(fed);
            }
            // Then a block apart, in that order, and moved back as one so the
            // row sits where it wanted to on average.
            std::vector<size_t> order(members.size());
            std::iota(order.begin(), order.end(), size_t { 0 });
            std::stable_sort(order.begin(), order.end(), [&wanted] (size_t a, size_t b) { return wanted[a] < wanted[b]; });
            std::vector<float> placed(members.size());
            for (size_t j = 0; j < order.size(); ++j)
                placed[j] = j == 0 ? wanted[order[0]] : std::max(wanted[order[j]], placed[j - 1] + pitch);
            const auto shift = (std::accumulate(wanted.begin(), wanted.end(), 0.0f)
                                - std::accumulate(placed.begin(), placed.end(), 0.0f)) / static_cast<float>(placed.size());
            for (size_t j = 0; j < order.size(); ++j)
                centres[static_cast<size_t>(members[order[j]])] = juce::jlimit(leftmost, rightmost, placed[j] + shift);
        }
        for (size_t i = 0; i < display.blocks.size(); ++i)
        {
            const auto y = top + static_cast<float>(topRow - rows[i]) * rowPitch;
            displayBlocks.push_back({ centres[i] - blockWidth * 0.5f, y, blockWidth, blockHeight });
        }
        for (size_t i = 0; i < display.blocks.size(); ++i)
        {
            const auto& section = display.blocks[i].section;
            for (const auto& parameter : parameters)
                if (parameter.section == section && parameter.tabGroup.isNotEmpty())
                {
                    if (selectedTab(parameter.tabGroup) == section)
                        displaySelected = static_cast<int>(i);
                    break;
                }
        }
    }

    if (!traceArea.isEmpty())
    {
        // One scale for every trace, so a quieter one looks quieter.
        auto loudest = 0.0f;
        for (const auto& trace : display.traces)
            for (const auto point : trace.points)
                loudest = std::max(loudest, std::abs(point));
        const auto plot = traceArea.reduced(4, 6).toFloat();
        const auto scale = loudest > 1.0e-6f ? plot.getHeight() * 0.45f / loudest : 0.0f;
        for (const auto& trace : display.traces)
        {
            juce::Path path;
            const auto points = trace.points.size();
            if (points > 1)
            {
                path.preallocateSpace(static_cast<int>(points) * 3 + 3);
                const auto step = plot.getWidth() / static_cast<float>(points - 1);
                for (size_t k = 0; k < points; ++k)
                {
                    const juce::Point<float> at { plot.getX() + static_cast<float>(k) * step,
                                                  plot.getCentreY() - trace.points[k] * scale };
                    if (k == 0)
                        path.startNewSubPath(at);
                    else
                        path.lineTo(at);
                }
            }
            juce::Path outline;
            juce::PathStrokeType(traceThickness, juce::PathStrokeType::beveled).createStrokedPath(outline, path);
            displayTraces.push_back(std::move(outline));
        }
    }
}

void DeviceEditorPanel::paintDisplay(juce::Graphics& g)
{
    if (!g.clipRegionIntersects(displayArea))
        return;
    const auto accent = faceAccent();

    if (!displayBlocks.empty())
    {
        // Links under the blocks, and the outputs down to the bus.
        g.setColour(palette::textDim);
        for (const auto& link : display.links)
        {
            if (!juce::isPositiveAndBelow(link.from, static_cast<int>(displayBlocks.size()))
                || !juce::isPositiveAndBelow(link.to, static_cast<int>(displayBlocks.size())))
                continue;
            const auto& from = displayBlocks[static_cast<size_t>(link.from)];
            const auto& to = displayBlocks[static_cast<size_t>(link.to)];
            if (link.from == link.to)
            {
                juce::Path loop;
                loop.startNewSubPath(from.getRight(), from.getY() + 4.0f);
                loop.cubicTo(from.getRight() + 8.0f, from.getY() + 1.0f, from.getRight() + 8.0f, from.getBottom() - 1.0f,
                             from.getRight(), from.getBottom() - 4.0f);
                g.setColour(accent);
                g.strokePath(loop, juce::PathStrokeType(1.2f));
                g.setColour(palette::textDim);
                continue;
            }
            g.drawLine(from.getCentreX(), from.getBottom(), to.getCentreX(), to.getY(), 1.2f);
        }
        auto busLeft = std::numeric_limits<float>::max(), busRight = std::numeric_limits<float>::lowest();
        for (size_t i = 0; i < displayBlocks.size(); ++i)
            if (display.blocks[i].output)
            {
                const auto& box = displayBlocks[i];
                g.setColour(accent);
                g.drawLine(box.getCentreX(), box.getBottom(), box.getCentreX(), displayBusY, 1.2f);
                busLeft = std::min(busLeft, box.getCentreX());
                busRight = std::max(busRight, box.getCentreX());
            }
        if (busLeft <= busRight)
        {
            g.setColour(accent);
            g.fillRect(juce::Rectangle<float>(busLeft - 4.0f, displayBusY - 0.6f, busRight - busLeft + 8.0f, 1.6f));
        }

        g.setFont(uiFontBold(9.0f));
        for (size_t i = 0; i < displayBlocks.size(); ++i)
        {
            const auto& block = display.blocks[i];
            const auto& box = displayBlocks[i];
            // A block heard directly is solid; one that only feeds others is
            // drawn in outline. Its level is the bar along its foot.
            g.setColour(block.output ? accent : palette::control);
            g.fillRoundedRectangle(box, 2.5f);
            g.setColour(static_cast<int>(i) == displaySelected ? palette::text
                        : block.output ? accent : accent.withAlpha(0.65f));
            g.drawRoundedRectangle(box.reduced(0.5f), 2.5f, static_cast<int>(i) == displaySelected ? 1.5f : 1.0f);
            const auto level = juce::jlimit(0.0f, 1.0f, block.level);
            g.setColour(block.output ? palette::displayInset.withAlpha(0.7f) : accent);
            g.fillRect(juce::Rectangle<float>(box.getX() + 3.0f, box.getBottom() - 3.5f, (box.getWidth() - 6.0f) * level, 1.5f));
            g.setColour(block.output ? palette::displayInset : palette::text);
            drawSnappedText(g, block.label, box.toNearestInt().withTrimmedBottom(2), juce::Justification::centred);
        }

        // The block that is showing, and what it does, over the diagram.
        if (juce::isPositiveAndBelow(displaySelected, static_cast<int>(display.blocks.size())))
        {
            const auto& block = display.blocks[static_cast<size_t>(displaySelected)];
            auto caption = captionArea.withWidth(traceArea.isEmpty() ? captionArea.getWidth()
                                                                     : traceArea.getX() - captionArea.getX() - partGap);
            g.setFont(uiFontBold(7.5f));
            g.setColour(palette::textDim);
            const auto name = block.section.toUpperCase();
            const auto nameWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name);
            drawSnappedText(g, name, caption.removeFromLeft(nameWidth + 5));
            g.setColour(block.output ? accent : palette::text);
            drawSnappedText(g, block.role.toUpperCase(), caption, juce::Justification::centredLeft, true);
        }
    }

    if (!traceArea.isEmpty())
    {
        const auto plot = traceArea.toFloat();
        g.setColour(palette::displayInset);
        g.fillRoundedRectangle(plot, 3.0f);
        g.setColour(palette::border);
        g.fillRect(juce::Rectangle<float>(plot.getX() + 4.0f, plot.reduced(0.0f, 6.0f).getCentreY() - 0.5f,
                                          plot.getWidth() - 8.0f, 1.0f));
        // Drawn back to front: the first trace is the one that matters.
        for (auto t = displayTraces.size(); t-- > 0;)
        {
            g.setColour(t == 0 ? accent : palette::textDim.withAlpha(0.55f));
            g.fillPath(displayTraces[t]);
        }
        // Named in the title row, each in its own colour.
        g.setFont(uiFontBold(7.5f));
        auto names = captionArea.withLeft(traceArea.getX());
        for (size_t t = 0; t < display.traces.size(); ++t)
        {
            const auto name = display.traces[t].name.toUpperCase();
            g.setColour(t == 0 ? accent : palette::textDim);
            const auto width = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name);
            drawSnappedText(g, name, names.removeFromLeft(width + 8));
        }
    }
}

int DeviceEditorPanel::displayBlockAt(juce::Point<float> position) const
{
    for (size_t i = 0; i < displayBlocks.size(); ++i)
        if (displayBlocks[i].expanded(2.0f).contains(position))
            return static_cast<int>(i);
    return -1;
}
}
