#include "DjDeckDisplay.h"
#include "DevicePixels.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>

// A deck's screen, its pictures: the tiles of the scrolling strip and the
// overview, how each is painted, and how a frame draws the strip and the
// overview from them. See DjDeckDisplay.h.

namespace rhino
{
namespace
{
// A tile set keeps no more than this many; the farthest from the playhead
// go first, since a long track at a fine zoom would otherwise keep a hundred.
constexpr size_t tilesKept = 24;

// The loudest of a run of columns in each band, which is what a pixel shows.
DjWaveformColumn pooled(const std::vector<DjWaveformColumn>& columns, int from, int to)
{
    DjWaveformColumn out;
    const auto count = static_cast<int>(columns.size());
    from = std::max(0, from);
    to = std::min(count, std::max(to, from + 1));
    for (int c = from; c < to; ++c)
    {
        const auto& column = columns[static_cast<size_t>(c)];
        out.low = std::max(out.low, column.low);
        out.mid = std::max(out.mid, column.mid);
        out.high = std::max(out.high, column.high);
    }
    return out;
}

// Three bands mirrored about the middle row, lows behind and highs in front,
// written straight into a software image's pixels a device column at a time:
// a tile was some 768 rectangles filled through a Graphics.
void drawColumns(juce::Image& image, const std::vector<DjWaveformColumn>& columns, double firstColumn, double columnsPerPixel,
                 float reachOfFull)
{
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readWrite);
    jassert(pixels.pixelFormat == juce::Image::RGB);
    const auto height = image.getHeight();
    const auto middle = static_cast<float>(height) * 0.5f;
    const std::array<juce::PixelARGB, 3> colours {palette::djWaveLow.getPixelARGB(), palette::djWaveMid.getPixelARGB(),
                                                  palette::djWaveHigh.getPixelARGB()};
    for (int x = 0; x < image.getWidth(); ++x)
    {
        const auto first = firstColumn + x * columnsPerPixel;
        const auto column = pooled(columns, static_cast<int>(first), static_cast<int>(first + columnsPerPixel));
        const std::array<float, 3> levels {column.low, column.mid, column.high * 0.85f};
        for (size_t band = 0; band < levels.size(); ++band)
        {
            const auto reach = std::max(0.5f, levels[band] * reachOfFull);
            const auto top = std::max(0, juce::roundToInt(middle - reach));
            const auto bottom = std::min(height, std::max(top + 1, juce::roundToInt(middle + reach)));
            for (int y = top; y < bottom; ++y)
                reinterpret_cast<juce::PixelRGB*>(pixels.getPixelPointer(x, y))->set(colours[band]);
        }
    }
}

// The beat grid under a tile's picture, on whole device pixels: a bar line
// full height, a beat a short tick top and bottom.
void drawGrid(juce::Image& image, const DjAnalysis& analysis, double start, double perSecond, float scale)
{
    if (!analysis.hasGrid()) return;
    juce::Graphics g(image);
    const auto beat = analysis.secondsPerBeat();
    const auto perBar = analysis.beatsPerBar;
    const auto origin = analysis.firstBeatSeconds;
    const auto width = image.getWidth(), height = image.getHeight();
    const auto line = std::max(1, juce::roundToInt(scale));
    const auto tick = std::max(1, juce::roundToInt(4.0f * scale));
    const auto firstBeat = static_cast<long long>(std::floor((start / perSecond - origin) / beat)) - 1;
    const auto lastBeat = static_cast<long long>(std::ceil(((start + width) / perSecond - origin) / beat)) + 1;
    if (lastBeat - firstBeat >= 4000) return;
    const auto left = static_cast<long long>(start);
    for (auto b = firstBeat; b <= lastBeat; ++b)
    {
        const auto x = static_cast<int>(std::llround((origin + static_cast<double>(b) * beat) * perSecond) - left);
        if (x < 0 || x >= width) continue;
        const auto bar = ((b % perBar) + perBar) % perBar == 0;
        g.setColour(bar ? palette::displayTextDim : palette::displayTextFaint.withMultipliedAlpha(0.6f));
        if (bar)
            g.fillRect(x, 0, line, height);
        else
        {
            g.fillRect(x, 0, line, tick);
            g.fillRect(x, height - tick, line, tick);
        }
    }
}

// A MIDI track's notes, as the editor would show them: a bar of notes on a
// row per pitch, brighter for a harder velocity.
void drawNotes(juce::Image& image, const std::vector<djwave::NoteSpan>& notes, int lowest, int highest,
               const DjAnalysis& analysis, double start, double perSecond, float scale)
{
    if (lowest > highest) return;
    juce::Graphics g(image);
    const auto width = static_cast<double>(image.getWidth());
    const auto height = static_cast<float>(image.getHeight());
    const auto rows = std::max(1, highest - lowest + 1);
    const auto rowHeight = std::max(2.0f * scale, (height - 4.0f * scale) / static_cast<float>(rows));
    const auto top = (height - rowHeight * static_cast<float>(rows)) * 0.5f;
    const auto secondsPerBeat = analysis.secondsPerBeat();
    const auto origin = analysis.firstBeatSeconds;
    for (const auto& note : notes)
    {
        const auto x0 = (origin + note.startBeat * secondsPerBeat) * perSecond - start;
        if (x0 > width) break;   // the spans run in order of where they start
        const auto x1 = (origin + note.endBeat * secondsPerBeat) * perSecond - start;
        if (x1 < 0.0) continue;
        const auto y = top + rowHeight * static_cast<float>(highest - note.note);
        const auto velocity = juce::jlimit(0.3f, 1.0f, static_cast<float>(note.velocity) / 127.0f);
        g.setColour(palette::djWaveMid.withMultipliedBrightness(0.6f + 0.4f * velocity));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(x0), y + scale,
                                          static_cast<float>(std::max(2.0 * scale, x1 - x0 - scale)),
                                          std::max(scale, rowHeight - 2.0f * scale)));
    }
}
}

DjDeckDisplay::Picture* DjDeckDisplay::PictureSet::find(long long index)
{
    for (auto& picture : pictures)
        if (picture.index == index)
            return &picture;
    return nullptr;
}

// Both pictures are painted on a software image, where a Graphics costs
// nothing to open and the columns can be written as pixels, and handed to
// the screen's own image type the first time they are drawn.
juce::Image DjDeckDisplay::paintTile(const PictureKey& key, long long index) const
{
    juce::Image image(juce::Image::RGB, std::max(1, key.width), std::max(1, key.height), false, juce::SoftwareImageType());
    image.clear(image.getBounds(), palette::displayInset);
    if (track == nullptr || track->analysis.columns.empty() || key.pixelsPerSecond <= 0.0) return image;
    const auto perSecond = key.pixelsPerSecond * key.scale;
    const auto start = static_cast<double>(index) * key.width;
    // A MIDI track's material shows its notes, as the editor would, rather
    // than the waveform they rendered to: what a hand reads on a drum or a
    // melody, and far less to paint than three bands of columns.
    if (drawsNotes)
        drawNotes(image, notes, lowestNote, highestNote, track->analysis, start, perSecond, key.scale);
    else
        drawColumns(image, track->analysis.columns, start * track->sampleRate / DjAnalysis::columnFrames / perSecond,
                    track->sampleRate / DjAnalysis::columnFrames / perSecond, static_cast<float>(image.getHeight()) * 0.46f);
    drawGrid(image, track->analysis, start, perSecond, key.scale);
    return image;
}

juce::Image DjDeckDisplay::paintOverviewPicture(const PictureKey& key) const
{
    juce::Image image(juce::Image::RGB, std::max(1, key.width), std::max(1, key.height), false, juce::SoftwareImageType());
    image.clear(image.getBounds(), palette::displayInset.brighter(0.04f));
    if (track == nullptr || track->analysis.columns.empty()) return image;
    const auto& columns = track->analysis.columns;
    drawColumns(image, columns, 0.0, static_cast<double>(columns.size()) / image.getWidth(),
                static_cast<float>(image.getHeight()) * 0.45f);
    // Drops: a marker at the top of the bar they land on.
    if (track->analysis.hasGrid() && !track->analysis.drops.empty())
    {
        juce::Graphics g(image);
        g.setColour(palette::djDrop);
        const auto barSeconds = track->analysis.secondsPerBeat() * track->analysis.beatsPerBar;
        const auto scale = key.scale;
        for (const auto bar : track->analysis.drops)
        {
            const auto x = static_cast<float>((track->analysis.firstBeatSeconds + bar * barSeconds) / track->seconds() * image.getWidth());
            juce::Path marker;
            marker.addTriangle(x - 4.0f * scale, 0.0f, x + 4.0f * scale, 0.0f, x, 6.0f * scale);
            g.fillPath(marker);
        }
    }
    return image;
}

bool DjDeckDisplay::tilesStandIn() const
{
    return tiles.complete || standIns.pictures.empty();
}

djwave::TileRange DjDeckDisplay::visibleTiles(double scroll) const
{
    const auto area = waveArea();
    const auto centreX = area.getX() + area.getWidth() / 2;
    return djwave::tilesTouching(scroll - (centreX - area.getX()) * paintedScale - 1.0,
                                 scroll + (area.getRight() - centreX) * paintedScale + 1.0, tileWidth);
}

// The visible tiles the strip lacks first, nearest the playhead, then the
// overview, then the tile the deck is heading into; `budget` of them at
// most. A key that changed - a zoom, a resize, another scale, or new
// content - leaves the pictures of the old key standing in until every
// visible picture of the new is painted. A key left before its tiles
// covered the strip keeps the stand-ins they were painted over: a drag
// that changes the key every frame - a splitter, the window's edge, the
// wheel spun - put the two tiles a frame paints in place of the whole set,
// and the strip went bare around them.
void DjDeckDisplay::paintPictures(int budget)
{
    if (track == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
    const auto key = tileKey();
    if (!(tiles.key == key))
    {
        if (tilesStandIn())
            standIns = std::move(tiles);
        tiles = {key, {}};
    }
    const auto area = waveArea();
    auto paintedVisible = false;
    if (!area.isEmpty())
    {
        const auto scroll = static_cast<double>(scrollFor(state.positionSeconds));
        const auto visible = visibleTiles(scroll);
        const auto distance = [scroll](long long index) { return std::abs((static_cast<double>(index) + 0.5) * tileWidth - scroll); };
        std::vector<long long> wanted;
        for (auto index = std::max(0LL, visible.first); index <= visible.last; ++index)
            if (tiles.find(index) == nullptr)
                wanted.push_back(index);
        std::sort(wanted.begin(), wanted.end(), [&distance](long long a, long long b) { return distance(a) < distance(b); });
        for (const auto index : wanted)
        {
            if (budget <= 0) break;
            tiles.pictures.push_back({index, paintTile(key, index), false});
            paintedVisible = true;
            --budget;
        }
        tiles.complete = std::all_of(wanted.begin(), wanted.end(), [this](long long index) { return tiles.find(index) != nullptr; });
        if (tiles.complete)
        {
            standIns = {};
            const auto ahead = state.reversed ? visible.first - 1 : visible.last + 1;
            const auto beyondTheEnd = static_cast<double>(ahead) * tileWidth > track->seconds() * key.pixelsPerSecond * key.scale;
            if (budget > 0 && ahead >= 0 && !beyondTheEnd && tiles.find(ahead) == nullptr)
            {
                tiles.pictures.push_back({ahead, paintTile(key, ahead), false});
                --budget;
            }
        }
        if (tiles.pictures.size() > tilesKept)
        {
            std::sort(tiles.pictures.begin(), tiles.pictures.end(),
                      [&distance](const Picture& a, const Picture& b) { return distance(a.index) < distance(b.index); });
            tiles.pictures.resize(tilesKept);
        }
    }
    const auto overviewNow = overviewKey();
    if (!(overview.key == overviewNow))
    {
        if (!overview.pictures.empty())
            overviewStandIn = std::move(overview);
        overview = {overviewNow, {}};
    }
    if (overview.pictures.empty() && budget > 0 && !overviewArea().isEmpty())
    {
        overview.pictures.push_back({0, paintOverviewPicture(overviewNow), false});
        overviewStandIn = {};
        repaint(overviewArea());
    }
    if (paintedVisible)
        repaint(area);
}

void DjDeckDisplay::handOver(juce::Graphics& g, Picture& picture) const
{
    if (picture.handedOver) return;
    picture.image = g.getInternalContext().getPreferredImageTypeForTemporaryImages()->convert(picture.image);
    picture.handedOver = true;
}

void DjDeckDisplay::paintWave(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (track == nullptr)
    {
        messageLine.set(message, messageFont, area.reduced(8, 0), juce::Justification::centred, true);
        g.setColour(palette::displayTextFaint);
        messageLine.draw(g);
        return;
    }
    const juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(area);
    const auto scale = paintedScale;
    const auto perSecond = pixelsPerSecond() * scale;
    const auto scroll = static_cast<double>(scrollFor(state.positionSeconds));
    const auto centreX = area.getX() + area.getWidth() / 2;
    // The strip is placed in device pixels from the one the screen's origin
    // falls in, its timeline from the device pixel nearest its middle and
    // its top from the one nearest its edge, so the tiles land whole on the
    // window's grid (DevicePixels.h). From the strip's timeline, in device
    // pixels from the top of the material, to device pixels here, and to x.
    const auto phase = devicePhase(*this, scale);
    const auto middle = static_cast<double>(std::round(phase.x + static_cast<float>(centreX) * scale));
    const auto deviceX = [&](double timeline) { return middle + timeline - scroll; };
    const auto xAt = [&](double timeline) { return static_cast<float>((deviceX(timeline) - phase.x) / scale); };
    const auto left = scroll - (centreX - area.getX()) * scale, right = scroll + (area.getRight() - centreX) * scale;
    const auto top = static_cast<float>(area.getY()), height = static_cast<float>(area.getHeight());
    const auto deviceTop = std::round(phase.y + top * scale);
    // Pictures of another key - the last zoom, size or bounce - stand in
    // where a tile of this key is not painted yet, stretched to where their
    // time falls now. Where there is neither, and before the top of the
    // material, the ground paint() laid down shows. Until the refresh comes
    // round, the tiles of a key just left are still `tiles`, and stand in
    // as the refresh will decide.
    const auto key = tileKey();
    auto* current = tiles.key == key ? &tiles : nullptr;
    auto* stand = current == nullptr && tilesStandIn() ? &tiles : &standIns;
    const auto standIn = [&](juce::Rectangle<float> region)
    {
        if (stand->key.pixelsPerSecond <= 0.0 || stand->key.scale <= 0.0f) return;
        const auto toHere = perSecond / (stand->key.pixelsPerSecond * stand->key.scale);
        const juce::Graphics::ScopedSaveState inside(g);
        g.reduceClipRegion(region.getSmallestIntegerContainer());
        for (auto& picture : stand->pictures)
        {
            const auto from = xAt(static_cast<double>(picture.index) * stand->key.width * toHere);
            const auto to = xAt(static_cast<double>(picture.index + 1) * stand->key.width * toHere);
            if (to < region.getX() || from > region.getRight()) continue;
            handOver(g, picture);
            g.drawImage(picture.image, juce::Rectangle<float>(from, top, to - from, height));
        }
    };
    const auto visible = visibleTiles(scroll);
    for (auto index = std::max(0LL, visible.first); index <= visible.last; ++index)
        if (current == nullptr || current->find(index) == nullptr)
            standIn({xAt(static_cast<double>(index) * tileWidth), top, static_cast<float>(tileWidth) / scale, height});
    // This key's tiles, drawn in device pixels: each is copied as it was
    // painted. Drawn from the strip's middle, between two device pixels at
    // 125 or 150 per cent, each was resampled half a pixel soft.
    if (current != nullptr)
    {
        const juce::Graphics::ScopedSaveState devicePixels(g);
        g.addTransform(juce::AffineTransform::scale(1.0f / scale));
        for (auto index = std::max(0LL, visible.first); index <= visible.last; ++index)
            if (auto* picture = current->find(index))
            {
                handOver(g, *picture);
                g.drawImageTransformed(picture->image,
                                       juce::AffineTransform::translation(static_cast<float>(deviceX(static_cast<double>(index) * tileWidth)) - phase.x,
                                                                          deviceTop - phase.y));
            }
    }
    const auto xFor = [&](double seconds) { return xAt(seconds * perSecond); };
    const auto visibleStart = left / perSecond, visibleEnd = right / perSecond;
    // The loop, shaded, with its edges.
    if (state.loop.valid())
    {
        const auto from = std::max(static_cast<float>(area.getX()), xFor(state.loop.startSeconds));
        const auto to = std::min(static_cast<float>(area.getRight()), xFor(state.loop.endSeconds));
        if (to > from)
        {
            g.setColour((state.loop.active ? palette::djCue : palette::displayTextDim).withAlpha(state.loop.active ? 0.18f : 0.1f));
            g.fillRect(from, top, to - from, height);
            g.setColour(state.loop.active ? palette::djCue : palette::displayTextDim);
            g.fillRect(from, top, 1.0f, height);
            g.fillRect(to - 1.0f, top, 1.0f, height);
        }
    }
    // Cue points: a flag at the top in the cue's colour, its letter shaped
    // once.
    const auto flag = [&](double seconds, juce::Colour colour, const ShapedLine& letter)
    {
        if (seconds < visibleStart - 1.0 || seconds > visibleEnd + 1.0) return;
        const auto x = xFor(seconds);
        g.setColour(colour);
        g.fillRect(x - 0.5f, top, 2.0f, height);
        const juce::Rectangle<int> box(static_cast<int>(x) + 1, area.getY(), 12, 11);
        g.fillRect(box);
        g.setColour(colour.contrasting(0.9f));
        letter.draw(g, juce::AffineTransform::translation(static_cast<float>(box.getX()), static_cast<float>(box.getY())));
    };
    flag(state.cueSeconds, palette::djCue, flagLetters[0]);
    for (size_t i = 0; i < state.hotCueSeconds.size(); ++i)
        if (state.hotCueSeconds[i] >= 0.0)
            flag(state.hotCueSeconds[i], palette::djHotCue[i], flagLetters[i + 1]);
    // The playhead, fixed in the middle.
    g.setColour(state.isPlaying() ? palette::djPlay : palette::text);
    g.fillRect(centreX - 1, area.getY(), 2, area.getHeight());
}

void DjDeckDisplay::paintOverview(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(palette::displayInset.brighter(0.04f));
    g.fillRect(area);
    if (track == nullptr)
        return;
    const auto key = overviewKey();
    auto* picture = overview.key == key ? overview.find(0) : nullptr;
    auto* standIn = overview.key == key ? overviewStandIn.find(0) : overview.find(0);
    if (picture != nullptr)
    {
        // In device pixels from the device pixel nearest the overview's
        // corner, as the tiles are drawn, so it is copied rather than
        // resampled; the ground shows in what it leaves of a device pixel at
        // an edge.
        handOver(g, *picture);
        const auto phase = devicePhase(*this, paintedScale);
        const juce::Graphics::ScopedSaveState devicePixels(g);
        g.reduceClipRegion(area);
        g.addTransform(juce::AffineTransform::scale(1.0f / paintedScale));
        g.drawImageTransformed(picture->image,
                               juce::AffineTransform::translation(std::round(phase.x + static_cast<float>(area.getX()) * paintedScale) - phase.x,
                                                                  std::round(phase.y + static_cast<float>(area.getY()) * paintedScale) - phase.y));
    }
    else if (standIn != nullptr)
    {
        handOver(g, *standIn);
        g.drawImage(standIn->image, area.toFloat());
    }
    const auto length = std::max(1.0e-6, state.lengthSeconds);
    const auto xFor = [&](double seconds) { return area.getX() + static_cast<float>(seconds / length * area.getWidth()); };
    // What has been played is dimmed, as a CDJ dims it, up to the marker on
    // the device pixel tick() chose.
    const auto played = static_cast<float>(area.getX())
                       + static_cast<float>(overviewMarker(state.positionSeconds)) / paintedScale;
    g.setColour(palette::displayInset.withAlpha(0.45f));
    g.fillRect(static_cast<float>(area.getX()), static_cast<float>(area.getY()), played - area.getX(), static_cast<float>(area.getHeight()));
    if (state.loop.valid())
    {
        g.setColour((state.loop.active ? palette::djCue : palette::displayTextDim).withAlpha(0.25f));
        g.fillRect(xFor(state.loop.startSeconds), static_cast<float>(area.getY()),
                   xFor(state.loop.endSeconds) - xFor(state.loop.startSeconds), static_cast<float>(area.getHeight()));
    }
    g.setColour(palette::djCue);
    g.fillRect(xFor(state.cueSeconds) - 0.5f, static_cast<float>(area.getBottom() - 5), 1.0f, 5.0f);
    for (size_t i = 0; i < state.hotCueSeconds.size(); ++i)
    {
        const auto at = state.hotCueSeconds[i];
        if (at < 0.0) continue;
        g.setColour(palette::djHotCue[i]);
        g.fillRect(xFor(at) - 1.0f, static_cast<float>(area.getBottom() - 6), 3.0f, 6.0f);
    }
    g.setColour(palette::text);
    g.fillRect(played - 0.5f, static_cast<float>(area.getY()), 1.5f, static_cast<float>(area.getHeight()));
}
}
