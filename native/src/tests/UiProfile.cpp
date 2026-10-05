#include "../Arrangement.h"
#include "../StepGrid.h"
#include "../DeviceRack.h"
#include "../SessionView.h"
#include "../AudioClipPanel.h"
#include "../Theme.h"
#include <algorithm>
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
// built and sized, the hidden session view among them, so a model change pays
// for exactly the listeners it pays for in the app.
namespace rhino
{
namespace
{
constexpr int profileTracks = 16;
constexpr int clipsPerTrack = 20;
constexpr int notesPerClip = 64;

double medianMicroseconds(int repetitions, const std::function<void(int)>& body)
{
    std::vector<double> times;
    times.reserve(static_cast<size_t>(repetitions));
    for (int i = 0; i < repetitions; ++i)
    {
        const auto start = juce::Time::getHighResolutionTicks();
        body(i);
        times.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1.0e6);
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
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
        // Automation on a few tracks, so the automation sweep has lanes to read.
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
        // panel - the lower pane's other two faces - and the paused session
        // view present but switched off.
        juce::Component shell;
        shell.setBounds(0, 0, 1400, 1000);
        auto arrangement = std::make_unique<Arrangement>(session);
        auto grid = std::make_unique<StepGrid>(session);
        auto rack = std::make_unique<DeviceRack>(session);
        auto sessionView = std::make_unique<SessionView>(session);
        auto clipPanel = std::make_unique<AudioClipPanel>(session);
        shell.addAndMakeVisible(*arrangement);
        shell.addAndMakeVisible(*rack);
        shell.addChildComponent(*grid);
        shell.addChildComponent(*sessionView);
        shell.addChildComponent(*clipPanel);
        arrangement->setBounds(0, 0, 1400, 620);
        grid->setBounds(0, 620, 1400, 300);
        rack->setBounds(0, 620, 1400, 260);
        sessionView->setBounds(0, 0, 1400, 620);
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

        // The automation sweep the shell runs every frame while playing.
        report("automation sweep", medianMicroseconds(60, [&] (int i)
        {
            session.applyTrackAutomationAt(0.05 * i);
        }));

        // A frame of the arrangement: the whole panel, then the narrow strip a
        // moving playhead invalidates.
        juce::Image arrangementCanvas(juce::Image::ARGB, arrangement->getWidth(), arrangement->getHeight(), true);
        report("arrangement full paint", medianMicroseconds(20, [&] (int)
        {
            paintArea(*arrangement, arrangementCanvas, arrangement->getLocalBounds());
        }));
        report("arrangement playhead strip", medianMicroseconds(60, [&] (int i)
        {
            paintArea(*arrangement, arrangementCanvas, {600 + (i % 8) * 3, 0, 4, arrangement->getHeight()});
        }));

        juce::Image gridCanvas(juce::Image::ARGB, grid->getWidth(), grid->getHeight(), true);
        report("note editor full paint", medianMicroseconds(20, [&] (int)
        {
            paintArea(*grid, gridCanvas, grid->getLocalBounds());
        }));
        report("note editor playhead strip", medianMicroseconds(60, [&] (int i)
        {
            paintArea(*grid, gridCanvas, {600 + (i % 8) * 3, 0, 4, grid->getHeight()});
        }));

        juce::Image rackCanvas(juce::Image::ARGB, rack->getWidth(), rack->getHeight(), true);
        report("device rack full paint", medianMicroseconds(20, [&] (int)
        {
            paintArea(*rack, rackCanvas, rack->getLocalBounds());
        }));

        clipPanel.reset();
        sessionView.reset();
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
