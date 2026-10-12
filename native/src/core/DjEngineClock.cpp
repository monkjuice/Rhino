#include "DjEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

// The booth's clock: which deck is master, the tempo every deck plays at and
// sync's correction onto the master's beat, and when a quantised start or
// jump lands. See DjEngine.h.

namespace rhino
{
namespace
{
// The ratio of a master's tempo to a deck's own, folded by an octave either
// way to whichever lies nearest one: 87 BPM against 174 plays at its own
// tempo with a beat on every other one of the master's, rather than at twice
// its speed. Returns the fold, a power of two.
double octaveFold(double ratio)
{
    auto fold = 1.0;
    for (const auto candidate : {0.5, 2.0})
        if (std::abs(std::log(ratio * candidate)) < std::abs(std::log(ratio * fold)))
            fold = candidate;
    return fold;
}

double positiveModulo(double value, double span)
{
    return value - std::floor(value / span) * span;
}
}

// The master is the deck the others follow: the one asked for while it
// plays, else the one already master while it plays, else the first that
// does. Nothing playing means no master, and the next deck to start takes
// the job.
void DjEngine::chooseMaster()
{
    const auto playing = [this](int i)
    {
        return juce::isPositiveAndBelow(i, maximumDecks) && rendering[static_cast<size_t>(i)]
            && deckList[static_cast<size_t>(i)].isPlaying() && deckList[static_cast<size_t>(i)].ownBpm() > 0.0;
    };
    const auto requested = requestedMaster.load(std::memory_order_relaxed);
    auto current = masterDeck.load(std::memory_order_relaxed);
    if (playing(requested))
        current = requested;
    else if (!playing(current))
    {
        current = -1;
        for (int i = 0; i < maximumDecks; ++i)
            if (playing(i))
            {
                current = i;
                break;
            }
    }
    masterDeck.store(current, std::memory_order_relaxed);
}

// Each deck's tempo and its platter's rate for the chunk, the master first so
// the others follow this chunk's tempo. The master's tempo is its fader's -
// or the tempo it holds - and never its platter's speed, so a master braked,
// held under a hand or played backwards drags no synced deck with it.
void DjEngine::setRates()
{
    const auto m = masterDeck.load(std::memory_order_relaxed);
    const auto* master = juce::isPositiveAndBelow(m, maximumDecks) && rendering[static_cast<size_t>(m)]
                       ? &deckList[static_cast<size_t>(m)] : nullptr;
    if (master != nullptr)
        setRate(deckList[static_cast<size_t>(m)], master, 0.0);
    const auto masterTempo = master != nullptr ? master->ownBpm() * master->tempo : 0.0;
    masterBpm.store(masterTempo, std::memory_order_relaxed);
    for (int i = 0; i < maximumDecks; ++i)
        if (rendering[static_cast<size_t>(i)] && i != m)
            setRate(deckList[static_cast<size_t>(i)], master, masterTempo);
}

// A deck's tempo: its fader with the jog's push; for a synced deck the
// master's tempo, folded to the octave nearest its own; and for a synced deck
// that has just taken over as master, the tempo it was following, held until
// its fader moves. Its platter: the hand while it is there, else the tempo
// with a gentle correction back onto the master's beat while both decks move
// steadily - a reversed, scratched, braking or spinning deck's beat runs
// against the correction, which only warbled.
void DjEngine::setRate(DjDeck& d, const DjDeck* master, double masterTempo)
{
    const auto own = d.ownBpm();
    const auto follows = master != nullptr && master != &d && d.controls.synced.load(std::memory_order_relaxed) && own > 0.0
                      && masterTempo > 0.0;
    auto fold = 1.0;
    double tempo;
    if (follows)
    {
        fold = octaveFold(masterTempo / own);
        tempo = masterTempo / own * fold;
        d.heldTempo = 0.0;
    }
    else
    {
        if (d.following && master == &d)
            d.heldTempo = d.tempo;
        const auto nudge = d.controls.nudgePercent.load(std::memory_order_relaxed) / 100.0;
        tempo = d.heldTempo > 0.0 ? std::max(0.0, d.heldTempo + nudge) : d.baseRate();
    }
    d.following = follows;
    d.tempo = tempo;
    d.tempoRate.store(tempo, std::memory_order_relaxed);
    d.heldTempoRate.store(d.heldTempo, std::memory_order_relaxed);
    if (d.controls.scratching.load(std::memory_order_relaxed))
    {
        d.targetRate = std::clamp(d.controls.scratchRate.load(std::memory_order_relaxed), -10.0, 10.0);
        return;
    }
    auto target = tempo;
    if (follows && phaseLock.load(std::memory_order_relaxed) && d.isPlaying() && d.steady() && master->steady())
    {
        // The error in this deck's beats, the master's folded to them,
        // wrapped to the nearest and closed over about half a second, never
        // by more than two per cent.
        auto error = master->grid.beatAt(master->pos) * fold - d.grid.beatAt(d.pos);
        error -= std::round(error);
        const auto errorSeconds = error * 60.0 / (masterTempo * fold);
        if (std::abs(errorSeconds) > 0.001)
            target += std::clamp(errorSeconds / 0.5, -0.02, 0.02);
    }
    d.targetRate = std::max(0.0, target);
}

// Sync pressed: the deck jumps onto the master's phase within the master's
// bar, the master's beat folded to this deck's as its tempo is.
void DjEngine::alignToMaster(int slot)
{
    const auto m = masterDeck.load(std::memory_order_relaxed);
    if (!juce::isPositiveAndBelow(m, maximumDecks) || m == slot || !rendering[static_cast<size_t>(m)]) return;
    const auto& master = deckList[static_cast<size_t>(m)];
    auto& d = deckList[static_cast<size_t>(slot)];
    if (master.current == nullptr || !master.grid.valid() || d.ownBpm() <= 0.0) return;
    const auto span = std::max(1, boundaryBeats(Quantise::bar, master.current->analysis.beatsPerBar));
    const auto fold = octaveFold(master.ownBpm() * master.tempo / d.ownBpm());
    const auto beat = master.grid.beatAt(master.pos) * fold;
    d.alignPhase(positiveModulo(beat, span), span);
}

// A deck waiting for its start, or a playing deck waiting to jump, keeps its
// target exactly and lands when the master's phase within the boundary
// reaches the target's phase on the deck's own grid - a cue on the bar's
// second beat lands as the master reaches a bar's second beat - so bars line
// up with bars and no cue is moved. A deck that follows the master counts the
// master's beats folded to its own, as its tempo and phase lock do, so an 87
// BPM deck synced to 174 lands on the beat it is locked to. The master times
// its own jumps against its own grid, so its phase, and every synced deck's,
// runs on through them. With quantise off, nothing to follow, or a master
// going nowhere forwards, it lands at once. The landing falls at a frame
// inside the block.
void DjEngine::landPendings(int frames)
{
    const auto q = static_cast<Quantise>(quantise.load(std::memory_order_relaxed));
    const auto m = masterDeck.load(std::memory_order_relaxed);
    const auto* master = juce::isPositiveAndBelow(m, maximumDecks) && rendering[static_cast<size_t>(m)]
                       ? &deckList[static_cast<size_t>(m)] : nullptr;
    for (int i = 0; i < maximumDecks; ++i)
    {
        auto& d = deckList[static_cast<size_t>(i)];
        if (!rendering[static_cast<size_t>(i)] || d.currentPending() == DjDeck::Pending::none) continue;
        if (q == Quantise::off || master == nullptr || master->current == nullptr || !master->grid.valid())
        {
            d.landPending(0);
            continue;
        }
        const auto span = boundaryBeats(q, master->current->analysis.beatsPerBar);
        const auto forwards = !master->controls.reversed.load(std::memory_order_relaxed);
        if (span <= 0 || !forwards || master->rate * master->grid.sampleRate / rate / master->grid.framesPerBeat <= 1.0e-9)
        {
            d.landPending(0);
            continue;
        }
        const auto fold = &d != master && d.following && d.ownBpm() > 0.0 ? octaveFold(master->ownBpm() * master->tempo / d.ownBpm())
                                                                           : 1.0;
        const auto target = d.grid.valid() ? d.grid.beatAt(d.pendingFrame()) : 0.0;
        if (const auto at = landingFrame(*master, fold, target, span, frames); at >= 0)
            d.landPending(at);
    }
}

// The master read on through this chunk, stretch by stretch: from where it
// stands to its loop's wrap, then from the loop's start to the next, so a
// master looping a beat while a cue waits for a bar line is timed by where
// its loop takes it rather than by a straight line it never travels. A loop
// that never passes the target's phase within the span - a beat looped off
// the bar line, the cue on it - is timed by the beat's phase instead, and a
// loop that never passes even that by its own wrap: the boundaries the master
// does reach, rather than waiting for ever.
int DjEngine::landingFrame(const DjDeck& master, double fold, double targetBeat, int span, int frames) const
{
    const auto step = master.rate * master.grid.sampleRate / rate;
    const auto beatsPerFrame = step / master.grid.framesPerBeat * fold;
    const auto lStart = master.loopStart.load(std::memory_order_relaxed), lEnd = master.loopEnd.load(std::memory_order_relaxed);
    const auto looping = master.loopActive.load(std::memory_order_relaxed) && lStart >= 0.0 && lEnd > lStart + 1.0;
    const auto phaseAt = [&master, fold](double frame, double within)
    {
        return positiveModulo(master.grid.beatAt(frame) * fold, within);
    };
    auto within = static_cast<double>(span);
    auto atWrap = false;
    if (looping)
    {
        const auto loopBeats = (lEnd - lStart) / master.grid.framesPerBeat * fold;
        const auto passes = [&](double length)
        {
            return loopBeats >= length || positiveModulo(targetBeat - phaseAt(lStart, length), length) < loopBeats;
        };
        if (!passes(within))
        {
            within = 1.0;
            atWrap = !passes(within);
        }
    }
    const auto targetPhase = positiveModulo(targetBeat, within);
    auto position = master.pos;
    for (auto frame = 0.0; frame < frames;)
    {
        // How many frames on the plan reads the master wrapped, if it loops.
        const auto wrapIn = looping ? std::max(1.0, std::ceil((lEnd - position) / step)) : std::numeric_limits<double>::infinity();
        if (atWrap)
            return frame + wrapIn < frames ? static_cast<int>(frame + wrapIn) : -1;
        auto ahead = positiveModulo(targetPhase - phaseAt(position, within), within);
        // A hair past the target's phase counts as on it.
        if (within - ahead < 0.002)
            ahead = 0.0;
        const auto crossing = ahead / beatsPerFrame;
        // Where the plan reads the master on the first frame after its wrap.
        const auto wrapped = looping ? lStart + positiveModulo(position + wrapIn * step - lStart, lEnd - lStart) : position;
        if (crossing < wrapIn)
        {
            auto nearest = std::round(crossing);
            if (nearest >= wrapIn)
            {
                // The crossing falls in the last half frame before the wrap.
                // The wrapped frame lands when the wrap keeps the phase - a
                // loop of whole spans - and the frame before it otherwise.
                const auto off = positiveModulo(phaseAt(wrapped, within) - targetPhase + within * 0.5, within) - within * 0.5;
                nearest = std::abs(off) < beatsPerFrame ? wrapIn : wrapIn - 1.0;
            }
            const auto at = frame + nearest;
            return at < frames ? static_cast<int>(at) : -1;
        }
        frame += wrapIn;
        position = wrapped;
    }
    return -1;
}
}
