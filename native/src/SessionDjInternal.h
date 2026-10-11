#pragma once
#include "SessionInternal.h"
#include "DjEngine.h"
#include "DjTrack.h"
#include "ContentLibrary.h"
#include <atomic>
#include <memory>
#include <vector>

// The DJ booth's private state. Only SessionDj.cpp and SessionDjSources.cpp
// include this.

namespace rhino
{
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
    // A bounce loops whole when it first lands, as a stem would be left to.
    bool loopWhole = false;
    DjDeckSettings() { hotCueSeconds.fill(-1.0); }
};

// A load in flight on the worker: a file being read and analysed, or a
// bounce being rendered. The job is shared with the worker, which writes
// its result and raises `done`; what a bounce renders is not in it.
struct DjLoadJob
{
    int deck = -1;
    int generation = 0;
    juce::File file;
    bool keepBeat = false;
    std::atomic<bool> done {false};
    std::atomic<bool> cancel {false};
    std::unique_ptr<DjTrack> result;
    juce::String error;
    // A bounce's name, tempo and signature, for the analysis.
    juce::String name;
    double tempo = 0.0;
    int beatsPerBar = 4;
    double started = 0.0;
    // A bounce's notes, gathered on the message thread, for the live preview.
    std::vector<DjMidiEvent> midi;
};

// What a bounce renders: a copy of the document loaded from a snapshot of
// its state, so the live edit is never read from another thread while the
// person goes on editing it, and the render that drives it. Owned by the
// message thread alone, made there and destroyed there once its job is
// done; the worker is handed a plain pointer to the render and touches
// nothing here after it raises `done`. The copy is declared first so the
// render and the scopes that refer to it go before it.
struct DjBounceWork
{
    std::shared_ptr<DjLoadJob> job;
    std::unique_ptr<te::Edit> copy;
    std::unique_ptr<te::FreezePointPlugin::ScopedTrackSoloIsolator> isolator;
    std::unique_ptr<te::Renderer::ScopedClipSlotDisabler> slotDisabler;
    juce::WavAudioFormat wav;
    std::unique_ptr<te::Renderer::RenderTask> task;
};

struct Session::DjBooth final : juce::AudioIODeviceCallback,
                                DjLiveSink,
                                juce::AsyncUpdater
{
    DjBooth();
    ~DjBooth() override;

    struct Deck
    {
        DjDeckInfo info;
        DjDeckSettings settings;
        // The track's monitoring before Live switched it on, put back after.
        std::optional<InputMonitoring> monitoringBeforeLive;
        bool rebounceWanted = false;
    };

    // The live preview's notes go straight into the track the deck plays,
    // as an editor's guide notes do, so no input need be monitored and no
    // graph rebuilt for them. The engine raises them on the audio thread,
    // and the track takes them only on the message thread - it asserts as
    // much, and its listeners are made and unmade there with the graph - so
    // they cross in this queue and an async update brings the message
    // thread to them. The session sets a deck's track when its preview
    // begins and clears it when the preview ends, on the message thread.
    struct PreviewNote { int deck = 0, note = 0, velocity = 0; bool on = false; };
    static constexpr int previewQueueSize = 1024;
    juce::AbstractFifo previewFifo {previewQueueSize};
    std::array<PreviewNote, previewQueueSize> previewQueue {};
    std::atomic<int> previewDropped {0};
    std::array<te::AudioTrack*, maximumDjDecks> previewTracks {};
    void noteOn(int deck, int note, int velocity) override;
    void noteOff(int deck, int note) override;
    void queuePreviewNote(PreviewNote);
    void handleAsyncUpdate() override;
    // Puts the queued notes into their tracks. Message thread; the poll
    // calls it too, so a test with no message loop still delivers.
    void deliverPreviewNotes();

    DjEngine engine;
    std::array<Deck, maximumDjDecks> decks;
    int count = 0;
    bool attached = false;
    juce::AudioFormatManager formats;
    // One worker, below normal priority: a render at full tilt beside the
    // audio thread was what made a set stutter while a bounce was made.
    juce::ThreadPool workers {1, 0, juce::Thread::Priority::low};
    std::vector<std::shared_ptr<DjLoadJob>> jobs;
    // The bounces in flight, kept until their jobs are done.
    std::vector<std::unique_ptr<DjBounceWork>> bounces;
    // When the document last changed under a bounced deck, as a millisecond
    // tick, and zero when nothing is stale. The poll bounces again once it
    // has been quiet for `rebounceQuietMs`.
    juce::uint32 staleSince = 0;
    static constexpr juce::uint32 rebounceQuietMs = 500;
    bool phaseLock = true;
    // The last few taps on the effect's TAP key, as millisecond ticks.
    std::vector<juce::uint32> taps;

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
};

// Reads a whole audio file into a track, up to the longest a deck takes.
// Null, with the reason in `error`, when it cannot. Any thread.
std::unique_ptr<DjTrack> readDjTrack(const juce::File&, juce::AudioFormatManager&, juce::String& error,
                                     std::atomic<bool>* cancel = nullptr);
constexpr double longestDjTrackSeconds = 15.0 * 60.0;
}
