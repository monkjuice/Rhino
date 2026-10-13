#pragma once
#include "SessionInternal.h"
#include "dj/DjEngine.h"
#include "dj/DjTrack.h"
#include "content/ContentLibrary.h"
#include <atomic>
#include <memory>
#include <vector>

// The DJ booth's private state. Only SessionDj.cpp and SessionDjSources.cpp
// include this.

namespace rhino
{
// Where the document keeps the booth: a child of the edit's state, written
// when a snapshot is taken (SessionDj.cpp).
extern const juce::Identifier djStateID;

// The settings a deck keeps apart from its material: what a document saves
// and what a load applies once the material is on the deck. Seconds, so a
// hot cue survives a file at another rate.
struct DjDeckSettings
{
    float tempoPercent = 0.0f;
    int tempoRange = 6;
    bool synced = false, reversed = false;
    double cueSeconds = 0.0;
    std::array<double, DjDeck::hotCueCount> hotCueSeconds;
    Session::DjLoop loop;
    // A clip loops whole when it first lands, so it plays round.
    bool loopWhole = false;
    DjDeckSettings() { hotCueSeconds.fill(-1.0); }
};

// A read in flight on the worker: a file, or the sound of an audio clip,
// read and analysed. The job is shared with the worker, which writes its
// result and raises `done`.
struct DjLoadJob
{
    // The engine slot of the deck the read is for: it stays the deck's
    // however the console moves around it.
    int slot = -1;
    int generation = 0;
    juce::File file;
    // The stretch of the file to read, in seconds; a length below zero reads
    // to the end.
    double startSeconds = 0.0, lengthSeconds = -1.0;
    bool keepBeat = false;
    std::atomic<bool> done {false};
    std::atomic<bool> cancel {false};
    std::unique_ptr<DjTrack> result;
    juce::String error;
    // A clip's name and the song's tempo and signature, which lay its grid;
    // a file's tempo is found by the analysis instead.
    juce::String name;
    double knownBpm = 0.0;
    int beatsPerBar = 4;
};

struct Session::DjBooth final : juce::AudioIODeviceCallback
{
    DjBooth();
    ~DjBooth() override;

    struct Deck
    {
        DjDeckInfo info;
        DjDeckSettings settings;
        // The track's monitoring before Live switched it on, put back after.
        std::optional<InputMonitoring> monitoringBeforeLive;
        // The track whose hidden ends play this deck's port, or none.
        te::EditItemID boundTrack;
        // The deck's clip changed under it - its notes, its sound, the song's
        // tempo - and its material is made again once nothing is held.
        bool materialDirty = false;
        // What the material on the deck was made from: the clip, its notes or
        // its stretch of file, its length and the tempo. A remake that would
        // come out the same - an edit to another clip of the track, the
        // arrangement's among them - hands nothing over, so the deck neither
        // crossfades nor lets a held note go for it.
        juce::String clipSignature;
        // A source the engine's queue had no room for: the material a load
        // brought, or nothing for an eject. It waits here, for the load it
        // came from, and the poll hands it over once the queue has room
        // (handOverDjSource). A newer load or eject takes its place.
        struct WaitingSource
        {
            std::unique_ptr<DjTrack> material;
            bool keepBeat = false;
            int generation = 0;
        };
        std::optional<WaitingSource> waiting;
    };

    DjEngine engine;
    // Each engine slot's deck, by slot: its source, settings and loads.
    std::array<Deck, maximumDjDecks> decks;
    int count = 0;
    // The console's decks in its order, each the engine slot it plays in,
    // which stays its own while it is on the console: taking a deck off
    // closes the console's gap and moves nothing the engine plays. Past the
    // console the free slots follow, in the order a new deck takes them, so a
    // control the console sets on every channel at once - the filters'
    // resonance, the faders' curve - has reached a slot before a deck does.
    std::array<int, maximumDjDecks> slotOfDeck {0, 1, 2, 3, 4, 5};
    // The slot of a deck on the console, or -1.
    int slotOf(int deck) const noexcept
    {
        return juce::isPositiveAndBelow(deck, count) ? slotOfDeck[static_cast<size_t>(deck)] : -1;
    }
    // The slot behind any of the six channels: a deck's, or past the console
    // the free slot the next deck would take.
    int channelSlot(int channel) const noexcept
    {
        return juce::isPositiveAndBelow(channel, maximumDjDecks) ? slotOfDeck[static_cast<size_t>(channel)] : -1;
    }
    // Which channel a slot stands behind, and which deck of the console holds
    // it, -1 for none.
    int channelOfSlot(int slot) const noexcept
    {
        for (int i = 0; i < maximumDjDecks; ++i)
            if (slotOfDeck[static_cast<size_t>(i)] == slot)
                return i;
        return -1;
    }
    int deckOfSlot(int slot) const noexcept
    {
        const auto channel = channelOfSlot(slot);
        return channel < count ? channel : -1;
    }
    bool attached = false;
    juce::AudioFormatManager formats;
    // One worker, below normal priority, for reading files and clips' sound.
    juce::ThreadPool workers {1, 0, juce::Thread::Priority::low};
    std::vector<std::shared_ptr<DjLoadJob>> jobs;
    // The last few taps on the effect's TAP key, as millisecond ticks.
    std::vector<juce::uint32> taps;

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
};

// Reads an audio file, or the stretch of it from `startSeconds` for
// `lengthSeconds` (to the end for a negative length), into a track, up to the
// longest a deck takes. Null, with the reason in `error`, when it cannot. Any
// thread.
std::unique_ptr<DjTrack> readDjTrack(const juce::File&, juce::AudioFormatManager&, juce::String& error,
                                     std::atomic<bool>* cancel = nullptr, double startSeconds = 0.0,
                                     double lengthSeconds = -1.0);
constexpr double longestDjTrackSeconds = 15.0 * 60.0;
}
