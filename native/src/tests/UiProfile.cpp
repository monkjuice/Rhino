#include "../Arrangement.h"
#include "../StepGrid.h"
#include "../DeviceRack.h"
#include "../DjView.h"
#include "../AudioClipPanel.h"
#include "../Playhead.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

// What the interface costs on the message thread, measured rather than argued.
// Run with `RhinoDAW.exe --profile-ui`. Not a CTest case: it checks nothing and
// prints medians, for comparing two builds the way Forge's --profile does. Run
// the two binaries alternately, because the machine drifts by more than some of
// the differences being measured.
//
// The document is deliberately large - sixteen MIDI tracks, each with twenty
// clips of sixty-four notes - and every panel the shell keeps listening is
// built and sized, the hidden DJ view among them, so a model change pays for
// exactly the listeners it pays for in the app.
namespace rhino
{
namespace
{
constexpr int profileTracks = 16;
constexpr int clipsPerTrack = 20;
constexpr int notesPerClip = 64;

// With untimed work before each run: the booth processed so the decks move, or
// the damage the run before left cleared.
double medianMicroseconds(int repetitions, const std::function<void(int)>& setup, const std::function<void(int)>& body)
{
    std::vector<double> times;
    times.reserve(static_cast<size_t>(repetitions));
    for (int i = 0; i < repetitions; ++i)
    {
        if (setup) setup(i);
        const auto start = juce::Time::getHighResolutionTicks();
        body(i);
        times.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1.0e6);
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

double medianMicroseconds(int repetitions, const std::function<void(int)>& body)
{
    return medianMicroseconds(repetitions, nullptr, body);
}

void report(const char* name, double microseconds)
{
    std::printf("%-34s %10.1f us\n", name, microseconds);
    std::fflush(stdout);
}

// Paints a component the way its peer would for a repaint of `area`: only that
// rectangle is in the clip, so a painter that culls by the clip pays less.
void paintArea(juce::Component& component, juce::Image& canvas, juce::Rectangle<int> area)
{
    juce::Graphics g(canvas);
    g.reduceClipRegion(area);
    component.paintEntireComponent(g, false);
}

// A software image, not the platform's default. On Windows the default is a
// Direct2D bitmap, and opening a context on one cost about 3 ms for the
// arrangement's size whatever was drawn - which buried the painters' own cost,
// the thing being compared, under a cost no on-screen repaint pays.
juce::Image canvasFor(const juce::Component& component)
{
    return {juce::Image::ARGB, component.getWidth(), component.getHeight(), true, juce::SoftwareImageType()};
}

// What a component and everything inside it invalidated, collected as its
// peer would collect it, so a line can paint exactly that. A component with a
// cached image is told of every repaint inside it before its parent is; this
// one only remembers them, and the component owns it.
struct DamageRecorder final : juce::CachedComponentImage
{
    explicit DamageRecorder(juce::Component& owner) : component(owner) {}
    void paint(juce::Graphics&) override {}
    bool invalidateAll() override
    {
        damage.add(component.getLocalBounds());
        return false;
    }
    bool invalidate(const juce::Rectangle<int>& area) override
    {
        damage.add(area);
        return false;
    }
    void releaseResources() override {}
    juce::Component& component;
    juce::RectangleList<int> damage;
};

DamageRecorder& recordDamage(juce::Component& component)
{
    auto* recorder = new DamageRecorder(component);
    component.setCachedComponentImage(recorder);
    return *recorder;
}

// Paints what was invalidated, as one region, the way the peer paints it.
void paintDamage(juce::Component& component, juce::Image& canvas, DamageRecorder& recorder)
{
    if (!recorder.damage.isEmpty())
    {
        juce::Graphics g(canvas);
        g.reduceClipRegion(recorder.damage);
        component.paintEntireComponent(g, false);
    }
    recorder.damage.clear();
}

// A minute of a beat for the file decks at 120 BPM: a kick on the beat, a hat
// between and a tone under both, so the screen's three bands each have
// something to draw.
void writeBeat(const juce::File& file)
{
    constexpr double rate = 48000.0;
    constexpr int seconds = 60;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        throw std::runtime_error("Could not write the profile's beat");
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(rate).withNumChannels(2).withBitsPerSample(16));
    if (writer == nullptr)
        throw std::runtime_error("Could not write the profile's beat");
    juce::AudioBuffer<float> audio(2, static_cast<int>(rate) * seconds);
    juce::Random noise(1);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto t = i / rate;
        const auto inBeat = std::fmod(t, 0.5);
        const auto offBeat = std::fmod(t + 0.25, 0.5);
        const auto kick = std::sin(juce::MathConstants<double>::twoPi * 55.0 * inBeat) * std::exp(-inBeat * 18.0);
        const auto hat = (noise.nextFloat() * 2.0f - 1.0f) * std::exp(-offBeat * 60.0);
        const auto tone = 0.1 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * t);
        const auto sample = static_cast<float>(0.6 * kick + 0.25 * hat + tone);
        audio.setSample(0, i, sample);
        audio.setSample(1, i, sample);
    }
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        throw std::runtime_error("Could not write the profile's beat");
}

void fillDocument(Session& session)
{
    while (session.trackCount() < profileTracks)
        if (session.addTrack(Session::TrackType::midi).failed())
            throw std::runtime_error("Could not add a profile track");

    auto& edit = *session.edit;
    const auto barBeats = session.beatsPerBar();
    for (int track = 0; track < profileTracks; ++track)
    {
        if (session.trackType(track) != Session::TrackType::midi)
            continue;
        auto* audioTrack = te::getAudioTracks(edit)[track];
        for (int clip = 0; clip < clipsPerTrack; ++clip)
        {
            const auto startBeat = clip * barBeats;
            const auto start = edit.tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat));
            const auto end = edit.tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat + barBeats));
            auto midi = audioTrack->insertMIDIClip("Profile", {start, end}, nullptr);
            if (midi == nullptr)
                throw std::runtime_error("Could not add a profile clip");
            for (int note = 0; note < notesPerClip; ++note)
                midi->getSequence().addNote(48 + (note * 7) % 24,
                                            tracktion::core::BeatPosition::fromBeats((note % 16) * 0.25),
                                            tracktion::core::BeatDuration::fromBeats(0.2), 100, 0, nullptr);
        }
    }
}
}

int runUiProfile()
{
    try
    {
        Session session;
        fillDocument(session);
        // A 4OSC on the first track: its macro knobs are what a device drag moves.
        if (session.addDevice("FourOsc", 0).failed())
            throw std::runtime_error("Could not add the profile synth");
        const auto slots = session.deviceSlots(0);
        if (slots.empty())
            throw std::runtime_error("The profile synth has no slot");
        const auto synthSlot = slots.front().pluginIndex;
        // Automation on a few tracks, so the mirror has lanes to write and the
        // rack has a knob to follow.
        for (int track = 0; track < 4; ++track)
        {
            if (session.addDevice("FourOsc", track * 2).failed()) continue;
            const auto trackSlots = session.deviceSlots(track * 2);
            if (trackSlots.empty()) continue;
            const Session::DeviceTarget target {track * 2, trackSlots.front().pluginIndex, 0};
            session.showTrackAutomation(target, false);
            session.setTrackAutomationPoints(target, {{0.0, 0.0f}, {8.0, 1.0f}, {16.0, 0.2f}, {32.0, 0.9f}});
        }

        Theme theme;
        juce::LookAndFeel::setDefaultLookAndFeel(&theme);
        // Laid out as the shell lays them out while a device is being edited:
        // the arrangement and the rack showing, the note editor and the clip
        // panel - the lower pane's other two faces - and the DJ view present
        // but switched off.
        juce::Component shell;
        shell.setBounds(0, 0, 1400, 1000);
        auto arrangement = std::make_unique<Arrangement>(session);
        auto grid = std::make_unique<StepGrid>(session);
        auto rack = std::make_unique<DeviceRack>(session);
        auto djView = std::make_unique<DjView>(session);
        auto clipPanel = std::make_unique<AudioClipPanel>(session);
        shell.addAndMakeVisible(*arrangement);
        shell.addAndMakeVisible(*rack);
        shell.addChildComponent(*grid);
        shell.addChildComponent(*djView);
        shell.addChildComponent(*clipPanel);
        arrangement->setBounds(0, 0, 1400, 620);
        grid->setBounds(0, 620, 1400, 300);
        rack->setBounds(0, 620, 1400, 260);
        djView->setBounds(0, 0, 1400, 620);
        clipPanel->setBounds(0, 620, 1400, 260);
        rack->selectTrack(0);
        session.sendSynchronousChangeMessage();

        std::printf("Rhino UI profile: %d tracks x %d clips x %d notes\n", profileTracks, clipsPerTrack, notesPerClip);

        // One step of a knob drag on the rack, a fader drag and a tempo drag:
        // each is what one pixel of pointer movement costs the message thread.
        session.beginDeviceParameterGesture(0, synthSlot, 0);
        report("knob drag step", medianMicroseconds(60, [&] (int i)
        {
            session.setDeviceParameter(0, synthSlot, 0, (i % 2) == 0 ? 0.3f : 0.6f);
        }));
        session.endDeviceParameterGesture(0, synthSlot, 0);

        session.beginTrackVolumeGesture(1);
        report("fader drag step", medianMicroseconds(60, [&] (int i)
        {
            session.setTrackVolumeDb(1, (i % 2) == 0 ? -3.0f : -6.0f);
        }));
        session.endTrackVolumeGesture(1);

        session.beginTempoGesture();
        report("tempo drag step", medianMicroseconds(30, [&] (int i)
        {
            session.setTempo((i % 2) == 0 ? 121.0 : 120.0);
        }));
        session.endTempoGesture();

        // What every announced change costs the arrangement: the rebuild, and
        // the layout pass it ends with.
        report("arrangement sync", medianMicroseconds(30, [&] (int)
        {
            arrangement->sync();
        }));
        report("arrangement resized", medianMicroseconds(60, [&] (int)
        {
            arrangement->resized();
        }));

        // What the lanes cost the message thread now that the engine plays
        // them: one mirror onto the engine's curves for each change to the
        // document, and, every frame while playing, the rack reading back the
        // faces a lane is moving. The knob drag above took the first track's
        // knob from its lane, so it is handed back first.
        session.toggleParameterAutomationOverride(0, synthSlot, 0);
        if (const auto knobs = session.deviceParameters(0, synthSlot);
            knobs.empty() || !knobs.front().automated || knobs.front().automationOverridden)
            throw std::runtime_error("The profile's first knob is not on a lane");
        report("automation mirror", medianMicroseconds(60, [&] (int)
        {
            session.markModified();
            session.mirrorAutomationToEngine();
        }));
        report("rack automation follow", medianMicroseconds(60, [&] (int)
        {
            rack->followAutomation();
        }));

        // A frame of the arrangement: the whole panel, then what a playhead
        // moving three pixels invalidates - the columns at its old and new
        // places, between the ruler and the foot, as updatePlayhead asks.
        auto arrangementCanvas = canvasFor(*arrangement);
        report("arrangement full paint", medianMicroseconds(20, [&] (int)
        {
            paintArea(*arrangement, arrangementCanvas, arrangement->getLocalBounds());
        }));
        const auto arrangementSweep = arrangement->getLocalBounds()
            .withTrimmedTop(static_cast<int>(Arrangement::rulerTop))
            .withTrimmedBottom(18 + static_cast<int>(arrangement->bottomInset));
        arrangement->playheadSweepsLanes = true;
        report("arrangement playhead strip", medianMicroseconds(60, [&] (int i)
        {
            const auto x = 600.0f + static_cast<float>(i % 8) * 3.0f;
            arrangement->playhead = x + 3.0f;
            paintArea(*arrangement, arrangementCanvas, playheadDamage(x, x + 3.0f, arrangementSweep));
        }));

        auto gridCanvas = canvasFor(*grid);
        report("note editor full paint", medianMicroseconds(20, [&] (int)
        {
            paintArea(*grid, gridCanvas, grid->getLocalBounds());
        }));
        const auto gridSweep = grid->getLocalBounds()
            .withTrimmedTop(static_cast<int>(StepGrid::headerHeight))
            .withTrimmedBottom(static_cast<int>(StepGrid::footerHeight)
                               + (grid->horizontalScroll.isVisible() ? static_cast<int>(StepGrid::scrollHeight) : 0));
        report("note editor playhead strip", medianMicroseconds(60, [&] (int i)
        {
            const auto x = 600.0f + static_cast<float>(i % 8) * 3.0f;
            grid->playhead = x + 3.0f;
            paintArea(*grid, gridCanvas, playheadDamage(x, x + 3.0f, gridSweep));
        }));

        auto rackCanvas = canvasFor(*rack);
        report("device rack full paint", medianMicroseconds(20, [&] (int)
        {
            paintArea(*rack, rackCanvas, rack->getLocalBounds());
        }));

        // The DJ view, showing as the shell shows it in the arrangement's
        // place, with six decks playing: three on a file and three on a cell
        // of a track of the song, the track's first clip copied into it. The
        // cells' screens draw notes, the files' screens waveforms. Every line
        // paints what its work invalidated, collected as the peer would
        // collect it.
        juce::TemporaryFile beat(".wav");
        writeBeat(beat.getFile());
        session.setDjQuantise(Session::DjQuantise::off);
        // The pressed console's track carries two effects, so the Device View
        // has faces to read when a press follows the deck.
        constexpr int pressedTrack = 5;
        if (session.addDevice("Equaliser", pressedTrack).failed() || session.addDevice("Reverb", pressedTrack).failed())
            throw std::runtime_error("Could not add the pressed deck's effects");
        for (int deck = 0; deck < Session::maximumDjDecks; ++deck)
        {
            if (session.addDjDeck().failed())
                throw std::runtime_error("Could not add a profile deck");
            const auto playCell = [&session](int onDeck, int lane)
            {
                const auto clips = te::getAudioTracks(*session.edit)[lane]->getClips();
                if (clips.isEmpty()) return juce::Result::fail("The profile track has no clip");
                if (!session.slotClip(lane, 0).hasClip)
                    if (const auto copied = session.copyClipToTrackSlot(clips.getFirst()->itemID, lane, 0); copied.failed())
                        return copied;
                return session.loadDjDeckSlot(onDeck, lane, 0);
            };
            const auto loaded = deck % 2 == 0 ? session.loadDjDeckFile(deck, beat.getFile())
                                              : playCell(deck, pressedTrack + (deck / 2) * 2);
            if (loaded.failed())
                throw std::runtime_error(("Could not load a profile deck: " + loaded.getErrorMessage()).toStdString());
        }
        for (int waited = 0; waited < 800 && session.djBusy(); ++waited)
        {
            juce::Thread::sleep(25);
            session.djPoll();
        }
        for (int deck = 0; deck < Session::maximumDjDecks; ++deck)
            if (!session.djDeckInfo(deck).loaded)
                throw std::runtime_error(("A profile deck did not load: " + session.djDeckInfo(deck).error).toStdString());
        arrangement->setVisible(false);
        djView->setVisible(true);
        djView->setBounds(0, 0, 1400, 900);
        session.sendSynchronousChangeMessage();
        for (int deck = 0; deck < Session::maximumDjDecks; ++deck)
            session.djPlay(deck);
        session.djProcessOffline(4800);
        auto& djDamage = recordDamage(*djView);
        auto& rackDamage = recordDamage(*rack);
        auto djCanvas = canvasFor(*djView);
        // The screens paint their pictures a couple a frame: a few frames
        // first, so every line below finds them painted, as a set would.
        for (int frame = 0; frame < 30; ++frame)
        {
            session.djProcessOffline(800);
            djView->frame();
            paintDamage(*djView, djCanvas, djDamage);
        }
        paintArea(*djView, djCanvas, djView->getLocalBounds());
        djDamage.damage.clear();
        std::printf("DJ view: %d decks playing, %d x %d\n", session.djDeckCount(), djView->getWidth(), djView->getHeight());

        // What every announcement costs the view, and painting what it asked for.
        report("dj view sync", medianMicroseconds(30, [&] (int)
        {
            djView->sync();
        }));
        report("dj view sync paint", medianMicroseconds(30, [&] (int)
        {
            djDamage.damage.clear();
            djView->sync();
        }, [&] (int)
        {
            paintDamage(*djView, djCanvas, djDamage);
        }));
        // A display refresh: every console's screen and platter, with the
        // booth moved on a 60 Hz frame's worth between them.
        std::vector<double> frameTimes;
        report("dj frame", medianMicroseconds(120, [&] (int)
        {
            session.djProcessOffline(800);
        }, [&] (int)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            djView->frame();
            paintDamage(*djView, djCanvas, djDamage);
            frameTimes.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1.0e6);
        }));
        {
            double sum = 0.0;
            for (const auto time : frameTimes) sum += time;
            report("dj frame (mean)", sum / static_cast<double>(std::max<size_t>(1, frameTimes.size())));
        }
        // The keys and the meters at the view's 30 Hz, the booth moved on a
        // tick's worth between them so the meters move.
        report("dj tick", medianMicroseconds(60, [&] (int)
        {
            session.djProcessOffline(1600);
        }, [&] (int)
        {
            djView->timerCallback();
            paintDamage(*djView, djCanvas, djDamage);
        }));
        // A key pressed on the focused console: the press focuses its deck and
        // the shell follows it, which a test reaches as far as the
        // arrangement's selection and the rack; the shell's own pane layout is
        // in Main.cpp, out of reach.
        djView->deckSelected = [&] (int deck)
        {
            const auto info = session.djDeckInfo(deck);
            if (info.kind != Session::DjSourceKind::track || info.track < 0) return;
            arrangement->selectTrack(info.track);
            rack->selectTrack(info.track);
        };
        djView->decks[1]->selected();
        paintDamage(*djView, djCanvas, djDamage);
        paintDamage(*rack, rackCanvas, rackDamage);
        report("dj console press", medianMicroseconds(30, [&] (int)
        {
            djView->decks[1]->selected();
            paintDamage(*djView, djCanvas, djDamage);
            paintDamage(*rack, rackCanvas, rackDamage);
        }));
        // The name bar of the focused console, which asks the shell to follow
        // the deck again.
        report("dj name bar press", medianMicroseconds(30, [&] (int)
        {
            djView->decks[1]->nameBarPressed();
            paintDamage(*djView, djCanvas, djDamage);
            paintDamage(*rack, rackCanvas, rackDamage);
        }));
        // One tile of each picture the screens scroll, handed to the
        // platform's own image type, since that is what the screen pays. An
        // image of that type is held throughout, as the window holds its
        // Direct2D resources: with none alive, every hand-over built them
        // afresh, about 115 ms, which no screen pays.
        const juce::Image holdsTheScreensResources(juce::Image::ARGB, 1, 1, true);
        auto& fileScreen = djView->decks[0]->display;
        auto& trackScreen = djView->decks[1]->display;
        report("dj tile build (waveform)", medianMicroseconds(20, [&] (int)
        {
            juce::NativeImageType().convert(fileScreen.paintTile(fileScreen.tileKey(), 2));
        }));
        report("dj tile build (notes)", medianMicroseconds(20, [&] (int)
        {
            juce::NativeImageType().convert(trackScreen.paintTile(trackScreen.tileKey(), 2));
        }));
        djView->setCachedComponentImage(nullptr);
        rack->setCachedComponentImage(nullptr);
        session.djStopAll();

        clipPanel.reset();
        djView.reset();
        rack.reset();
        grid.reset();
        arrangement.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "UI profile failed: %s\n", error.what());
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        return 1;
    }
}
}
