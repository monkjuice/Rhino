#include "ui/arrangement/Arrangement.h"
#include "ui/notes/StepGrid.h"
#include "ui/rack/DeviceRack.h"
#include "ui/dj/DjView.h"
#include "ui/audioclip/AudioClipPanel.h"
#include "ui/shared/Theme.h"
#include "SystemUsage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

// What a big song costs, measured rather than argued. Run with
// `RhinoDAW.exe --profile-stress`. Not a CTest case: it checks nothing and
// prints timings, as --profile-ui does for a medium document.
//
// The document is the kind of session a finished track becomes: 120 tracks,
// half MIDI and half audio, in groups, seven minutes long at 120 BPM, with a
// synth on most MIDI tracks, effects scattered through the chains, automation
// on some, and clips tiling about two thirds of the song. Every panel the
// shell keeps listening is built and laid out, the hidden ones switched off,
// so a change pays for exactly the listeners it pays for in the app.
//
// Sizes can be overridden for a bisection: RHINO_STRESS_TRACKS (even, split
// half MIDI and half audio), RHINO_STRESS_MINUTES, RHINO_STRESS_RENDER_SECONDS
// (0 skips the render).
namespace rhino
{
namespace
{
int envInt(const char* name, int fallback)
{
    const auto value = juce::SystemStats::getEnvironmentVariable(name, {});
    return value.isNotEmpty() ? value.getIntValue() : fallback;
}

double medianMilliseconds(int repetitions, const std::function<void(int)>& body)
{
    std::vector<double> times;
    times.reserve(static_cast<size_t>(repetitions));
    for (int i = 0; i < repetitions; ++i)
    {
        const auto start = juce::Time::getHighResolutionTicks();
        body(i);
        times.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1.0e3);
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

double timeOnce(const std::function<void()>& body)
{
    const auto start = juce::Time::getHighResolutionTicks();
    body();
    return juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1.0e3;
}

void report(const char* name, double milliseconds)
{
    std::printf("%-44s %10.2f ms\n", name, milliseconds);
    std::fflush(stdout);
}

void reportMemory(const char* name)
{
    std::printf("%-44s %10.1f MB\n", name, static_cast<double>(SystemUsage::processMemoryBytes()) / (1024.0 * 1024.0));
    std::fflush(stdout);
}

void paintArea(juce::Component& component, juce::Image& canvas, juce::Rectangle<int> area)
{
    juce::Graphics g(canvas);
    g.reduceClipRegion(area);
    component.paintEntireComponent(g, false);
}

// A software image, as --profile-ui paints on, so the painters' own cost is
// what is measured and not the cost of opening a Direct2D context.
juce::Image canvasFor(const juce::Component& component)
{
    return {juce::Image::ARGB, component.getWidth(), component.getHeight(), true, juce::SoftwareImageType()};
}

// Eight bars of something to look at and hear: a kick and hat loop, a pad, a
// riser or a bass, by `kind`, so the four audio files the song tiles differ.
void writeLoop(const juce::File& file, int kind, double seconds)
{
    constexpr double rate = 48000.0;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        throw std::runtime_error("Could not write the stress profile's loop");
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(rate).withNumChannels(2).withBitsPerSample(16));
    if (writer == nullptr)
        throw std::runtime_error("Could not write the stress profile's loop");
    juce::AudioBuffer<float> audio(2, static_cast<int>(rate * seconds));
    juce::Random noise(kind + 1);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto t = i / rate;
        float sample = 0.0f;
        switch (kind % 4)
        {
            case 0:
            {
                const auto inBeat = std::fmod(t, 0.5);
                const auto offBeat = std::fmod(t + 0.25, 0.5);
                sample = static_cast<float>(0.6 * std::sin(juce::MathConstants<double>::twoPi * 55.0 * inBeat) * std::exp(-inBeat * 18.0)
                                            + 0.25 * (noise.nextFloat() * 2.0f - 1.0f) * std::exp(-offBeat * 60.0));
                break;
            }
            case 1:
                sample = static_cast<float>(0.15 * (std::sin(juce::MathConstants<double>::twoPi * 220.0 * t)
                                                    + std::sin(juce::MathConstants<double>::twoPi * 277.18 * t)
                                                    + std::sin(juce::MathConstants<double>::twoPi * 329.63 * t)));
                break;
            case 2:
                sample = (noise.nextFloat() * 2.0f - 1.0f) * static_cast<float>(0.2 * t / seconds);
                break;
            default:
            {
                const auto inBeat = std::fmod(t, 0.25);
                sample = static_cast<float>(0.4 * std::sin(juce::MathConstants<double>::twoPi * 41.2 * t) * std::exp(-inBeat * 6.0));
                break;
            }
        }
        audio.setSample(0, i, sample);
        audio.setSample(1, i, sample * (kind % 2 == 0 ? 1.0f : 0.8f));
    }
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        throw std::runtime_error("Could not write the stress profile's loop");
}

struct Shape
{
    int tracks = 120;
    double minutes = 7.0;
    int renderSeconds = 20;
    bool clips = true, devices = true;
    int midiTracks() const { return tracks / 2; }
    int bars() const { return static_cast<int>(std::ceil(minutes * 60.0 / 2.0)); }  // 120 BPM, 4/4
};

struct Built
{
    int midiClips = 0, notes = 0, audioClips = 0, devices = 0, groups = 0, lanes = 0;
};

// Which tracks are which kind. The starter document already holds two of
// each, so the stack is read back rather than assumed.
struct Layout
{
    std::vector<int> midi, audio;
};

Layout readLayout(Session& session)
{
    Layout layout;
    for (int track = 0; track < session.trackCount(); ++track)
        if (!session.isGroupBusTrack(track))
            (session.trackType(track) == Session::TrackType::midi ? layout.midi : layout.audio).push_back(track);
    return layout;
}

Layout addTracks(Session& session, const Shape& shape)
{
    while (static_cast<int>(readLayout(session).midi.size()) < shape.midiTracks())
        if (session.addTrack(Session::TrackType::midi).failed())
            throw std::runtime_error("Could not add a MIDI track");
    while (session.trackCount() < shape.tracks)
        if (session.addTrack(Session::TrackType::audio).failed())
            throw std::runtime_error("Could not add an audio track");
    return readLayout(session);
}

// Six roles, cycling down the MIDI tracks: how many notes a bar and how long
// a clip each role plays in. The song is eight sections; a track plays in
// about six of ten, tiling its clips from the section's start.
void addMidiClips(Session& session, const Shape& shape, const Layout& layout, Built& built)
{
    static constexpr int notesPerBar[] = {8, 12, 16, 32, 16, 4};
    static constexpr int clipBars[] = {4, 8, 4, 2, 4, 8};
    static constexpr int lowNote[] = {36, 48, 60, 72, 48, 36};
    juce::Random random(7);
    auto& edit = *session.edit;
    const auto barBeats = session.beatsPerBar();
    const auto sections = 8;
    const auto sectionBars = shape.bars() / sections;
    const auto tracks = te::getAudioTracks(edit);
    for (int index = 0; index < static_cast<int>(layout.midi.size()); ++index)
    {
        const auto role = index % 6;
        auto* audioTrack = tracks[layout.midi[static_cast<size_t>(index)]];
        for (int section = 0; section < sections; ++section)
        {
            if (random.nextFloat() > 0.6f) continue;
            for (int bar = section * sectionBars; bar + clipBars[role] <= (section + 1) * sectionBars; bar += clipBars[role])
            {
                const auto startBeat = bar * barBeats;
                const auto endBeat = startBeat + clipBars[role] * barBeats;
                const auto start = edit.tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat));
                const auto end = edit.tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(endBeat));
                auto midi = audioTrack->insertMIDIClip("Clip " + juce::String(bar + 1), {start, end}, nullptr);
                if (midi == nullptr)
                    throw std::runtime_error("Could not add a MIDI clip");
                auto& sequence = midi->getSequence();
                const auto step = barBeats / notesPerBar[role];
                for (int clipBar = 0; clipBar < clipBars[role]; ++clipBar)
                    for (int note = 0; note < notesPerBar[role]; ++note)
                    {
                        const auto pitch = lowNote[role] + (note * 5 + clipBar * 2) % 19;
                        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(clipBar * barBeats + note * step),
                                         tracktion::core::BeatDuration::fromBeats(step * 0.8), 70 + (note * 13) % 50, 0, nullptr);
                        ++built.notes;
                    }
                ++built.midiClips;
            }
        }
    }
}

// Four loops of eight bars, tiled back to back through about seven sections
// in ten, every audio track taking one of the four files.
void addAudioClips(Session& session, const Shape& shape, const Layout& layout, const std::vector<juce::File>& loops, Built& built)
{
    juce::Random random(11);
    auto& edit = *session.edit;
    const auto loopSeconds = 16.0;
    const auto sections = 8;
    const auto sectionSeconds = shape.bars() / sections * 2.0;
    const auto tracks = te::getAudioTracks(edit);
    for (int index = 0; index < static_cast<int>(layout.audio.size()); ++index)
    {
        const auto& file = loops[static_cast<size_t>(index % static_cast<int>(loops.size()))];
        auto* audioTrack = tracks[layout.audio[static_cast<size_t>(index)]];
        for (int section = 0; section < sections; ++section)
        {
            if (random.nextFloat() > 0.7f) continue;
            for (auto start = section * sectionSeconds; start + loopSeconds <= (section + 1) * sectionSeconds + 0.001; start += loopSeconds)
            {
                te::ClipPosition position {{tracktion::core::TimePosition::fromSeconds(start),
                                            tracktion::core::TimePosition::fromSeconds(start + loopSeconds)}, {}};
                auto clip = audioTrack->insertWaveClip(file.getFileNameWithoutExtension(), file, position, false);
                if (clip == nullptr)
                    throw std::runtime_error("Could not add an audio clip");
                ++built.audioClips;
            }
        }
    }
}

// A synth on most MIDI tracks, two kinds; effects down the stack.
void addDevices(Session& session, const Shape& shape, const Layout& layout, Built& built)
{
    const auto add = [&](const char* id, int track)
    {
        if (const auto added = session.addDevice(id, track); added.failed())
            throw std::runtime_error(("Could not add " + juce::String(id) + " to track " + juce::String(track) + " - "
                                      + added.getErrorMessage()).toStdString());
        ++built.devices;
    };
    const auto before = built.devices;
    for (int index = 0; index < static_cast<int>(layout.midi.size()); ++index)
        if (index % 6 == 1) add("RhinoFM", layout.midi[static_cast<size_t>(index)]);
    std::printf("  %d Rhino FM: ", built.devices - before);
    reportMemory("memory");
    const auto afterFm = built.devices;
    for (int index = 0; index < static_cast<int>(layout.midi.size()); ++index)
        if (index % 6 != 1 && index % 2 == 0) add("FourOsc", layout.midi[static_cast<size_t>(index)]);
    std::printf("  %d 4OSC: ", built.devices - afterFm);
    reportMemory("memory");
    const auto afterOsc = built.devices;
    for (int track = 0; track < shape.tracks; ++track)
        if (track % 3 == 0) add("Equaliser", track);
    std::printf("  %d Rhino EQ: ", built.devices - afterOsc);
    reportMemory("memory");
    const auto afterEq = built.devices;
    for (int track = 0; track < shape.tracks; ++track)
        if (track % 5 == 0) add("Compressor", track);
    std::printf("  %d Compressor: ", built.devices - afterEq);
    reportMemory("memory");
}

// Eight tracks to a group, from the bottom of the stack up so the indices of
// the tracks still to be grouped do not move under the buses made.
void addGroups(Session& session, const Shape& shape, Built& built)
{
    constexpr int perGroup = 8;
    for (int first = (shape.tracks / perGroup - 1) * perGroup; first >= 0; first -= perGroup)
    {
        std::vector<int> members;
        for (int track = first; track < first + perGroup && track < shape.tracks; ++track)
            members.push_back(track);
        if (session.groupTracks(members, "Group " + juce::String(first / perGroup + 1)).failed())
            throw std::runtime_error("Could not group tracks");
        ++built.groups;
    }
}

// A lane with a handful of points on the first knob of every fourth device.
void addAutomation(Session& session, const Shape& shape, Built& built)
{
    const auto songSeconds = shape.minutes * 60.0;
    for (int track = 0; track < session.trackCount(); track += 4)
    {
        const auto slots = session.deviceSlots(track);
        if (slots.empty()) continue;
        const Session::DeviceTarget target {track, slots.front().pluginIndex, 0};
        if (session.showTrackAutomation(target, false).failed()) continue;
        std::vector<Session::AutomationPoint> points;
        for (int point = 0; point < 8; ++point)
            points.push_back({songSeconds * point / 8.0, point % 2 == 0 ? 0.2f : 0.9f});
        if (session.setTrackAutomationPoints(target, points).failed()) continue;
        ++built.lanes;
    }
}

// The song rendered through the engine's own graph, as Export WAV renders it,
// over its first `seconds`: the cost of playing it, with no device to hide it.
double renderSeconds(Session& session, const juce::File& destination, double seconds, double& wallSeconds)
{
    auto& edit = *session.edit;
    te::Edit::ScopedRenderStatus status(edit, true);
    session.mirrorAutomationToEngine();
    te::Renderer::turnOffAllPlugins(edit);
    std::atomic<float> progress {0.0f};
    juce::WavAudioFormat wav;
    te::Renderer::Parameters parameters(edit);
    parameters.destFile = destination;
    parameters.audioFormat = &wav;
    parameters.sampleRateForAudio = 48000.0;
    parameters.bitDepth = 24;
    parameters.blockSizeForAudio = 512;
    parameters.useMasterPlugins = true;
    parameters.time = {{}, tracktion::core::TimePosition::fromSeconds(seconds)};
    te::Renderer::RenderTask task("Stress render", parameters, &progress, nullptr);
    const auto start = juce::Time::getHighResolutionTicks();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 600.0 * 1000.0;
    auto lastProgress = -1.0f;
    auto lastMoved = juce::Time::getMillisecondCounterHiRes();
    while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (progress.load() != lastProgress)
        {
            lastProgress = progress.load();
            lastMoved = now;
        }
        if (now > deadline || now - lastMoved > 60.0 * 1000.0)
        {
            std::printf("render gave up at %.0f%%\n", progress.load() * 100.0);
            break;
        }
    }
    wallSeconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
    te::Renderer::turnOffAllPlugins(edit);
    return progress.load();
}
}

int runStressProfile()
{
    try
    {
        Shape shape;
        shape.tracks = std::max(4, envInt("RHINO_STRESS_TRACKS", shape.tracks) / 2 * 2);
        shape.minutes = std::max(1, envInt("RHINO_STRESS_MINUTES", static_cast<int>(shape.minutes)));
        shape.renderSeconds = std::max(0, envInt("RHINO_STRESS_RENDER_SECONDS", shape.renderSeconds));
        shape.clips = envInt("RHINO_STRESS_CLIPS", 1) != 0;
        shape.devices = envInt("RHINO_STRESS_DEVICES", 1) != 0;
        std::printf("Rhino stress profile: %d tracks (%d MIDI, %d audio), %.0f minutes at 120 BPM, %d bars\n",
                    shape.tracks, shape.midiTracks(), shape.tracks - shape.midiTracks(), shape.minutes, shape.bars());
        reportMemory("memory at start");

        juce::TemporaryFile folderHolder;
        const auto folder = folderHolder.getFile().getSiblingFile("rhino-stress-" + juce::String(juce::Time::currentTimeMillis()));
        if (!folder.createDirectory())
            throw std::runtime_error("Could not make the stress profile's folder");
        std::vector<juce::File> loops;
        report("write four 16 s loops", timeOnce([&]
        {
            for (int kind = 0; kind < 4; ++kind)
            {
                loops.push_back(folder.getChildFile("loop" + juce::String(kind + 1) + ".wav"));
                writeLoop(loops.back(), kind, 16.0);
            }
        }));

        Session session;
        Built built;
        Layout layout;
        reportMemory("memory with an empty session");
        report("add tracks", timeOnce([&] { layout = addTracks(session, shape); }));
        reportMemory("memory with the tracks");
        if (shape.clips)
        {
            report("add MIDI clips and notes", timeOnce([&] { addMidiClips(session, shape, layout, built); }));
            reportMemory("memory with the MIDI clips");
            report("add audio clips", timeOnce([&] { addAudioClips(session, shape, layout, loops, built); }));
            reportMemory("memory with the audio clips");
        }
        if (shape.devices)
        {
            report("add devices", timeOnce([&] { addDevices(session, shape, layout, built); }));
            reportMemory("memory with the devices");
        }
        report("group tracks", timeOnce([&] { addGroups(session, shape, built); }));
        if (shape.devices)
            report("add automation", timeOnce([&] { addAutomation(session, shape, built); }));
        session.markModified();
        session.sendSynchronousChangeMessage();
        std::printf("document: %d tracks, %d MIDI clips, %d notes, %d audio clips, %d devices added, %d groups, %d lanes, %.1f s long\n",
                    session.trackCount(), built.midiClips, built.notes, built.audioClips, built.devices, built.groups, built.lanes,
                    session.edit->getLength().inSeconds());
        reportMemory("memory with the document");

        Theme theme;
        juce::LookAndFeel::setDefaultLookAndFeel(&theme);
        juce::Component shell;
        shell.setBounds(0, 0, 1600, 1000);
        auto arrangement = std::make_unique<Arrangement>(session);
        auto grid = std::make_unique<StepGrid>(session);
        auto rack = std::make_unique<DeviceRack>(session);
        auto djView = std::make_unique<DjView>(session);
        auto clipPanel = std::make_unique<AudioClipPanel>(session);
        const auto panelsBuilt = timeOnce([&]
        {
            shell.addAndMakeVisible(*arrangement);
            shell.addAndMakeVisible(*rack);
            shell.addChildComponent(*grid);
            shell.addChildComponent(*djView);
            shell.addChildComponent(*clipPanel);
            arrangement->setBounds(0, 0, 1600, 700);
            grid->setBounds(0, 700, 1600, 300);
            rack->setBounds(0, 700, 1600, 260);
            djView->setBounds(0, 0, 1600, 700);
            clipPanel->setBounds(0, 700, 1600, 260);
            rack->selectTrack(0);
            session.sendSynchronousChangeMessage();
        });
        report("panels built and first sync", panelsBuilt);
        reportMemory("memory with the panels");

        // What every announced change costs: every listening panel rebuilds.
        report("change announced (all panels)", medianMilliseconds(15, [&] (int) { session.sendSynchronousChangeMessage(); }));
        report("  arrangement sync", medianMilliseconds(15, [&] (int) { arrangement->sync(); }));
        report("  arrangement syncTrackControls", medianMilliseconds(30, [&] (int) { arrangement->syncTrackControls(); }));
        report("  arrangement buildRows", medianMilliseconds(30, [&] (int) { arrangement->buildRows(); }));
        report("  arrangement updateScroll", medianMilliseconds(30, [&] (int) { arrangement->updateScroll(); }));
        report("arrangement resized", medianMilliseconds(30, [&] (int) { arrangement->resized(); }));
        {
            // The two facts a sync asks of every track that cost a walk of the
            // edit each: how many tracks there are, and one track's name.
            report("  session.trackCount x120", medianMilliseconds(30, [&] (int)
            {
                int total = 0;
                for (int i = 0; i < 120; ++i) total += session.trackCount();
                if (total < 0) std::printf("never\n");
            }));
            report("  session.trackName x120", medianMilliseconds(30, [&] (int)
            {
                int total = 0;
                for (int i = 0; i < 120; ++i) total += session.trackName(i).length();
                if (total < 0) std::printf("never\n");
            }));
            report("  session.trackMixer x120", medianMilliseconds(30, [&] (int)
            {
                float total = 0;
                for (int i = 0; i < 120; ++i) total += session.trackMixer(i).volumeDb;
                if (total < -1.0e9f) std::printf("never\n");
            }));
            report("  session.trackAutomations x120", medianMilliseconds(30, [&] (int)
            {
                size_t total = 0;
                for (int i = 0; i < 120; ++i) total += session.trackAutomations(i).size();
                if (total > 1u << 30) std::printf("never\n");
            }));
            const auto getter = [&](const char* name, const std::function<int(int)>& read)
            {
                report(name, medianMilliseconds(20, [&] (int)
                {
                    int total = 0;
                    for (int i = 0; i < 120; ++i) total += read(i);
                    if (total == -12345678) std::printf("never\n");
                }));
            };
            getter("  session.trackType x120", [&](int i) { return static_cast<int>(session.trackType(i)); });
            getter("  session.trackColour x120", [&](int i) { return static_cast<int>(session.trackColour(i).getARGB() & 1); });
            getter("  session.isTrackArmed x120", [&](int i) { return session.isTrackArmed(i) ? 1 : 0; });
            getter("  session.trackHasInstrument x120", [&](int i) { return session.trackHasInstrument(i) ? 1 : 0; });
            getter("  session.isGroupBusTrack x120", [&](int i) { return session.isGroupBusTrack(i) ? 1 : 0; });
            getter("  session.isTrackFolded x120", [&](int i) { return session.isTrackFolded(i) ? 1 : 0; });
            getter("  session.trackRecordInput x120", [&](int i) { return static_cast<int>(session.trackRecordInput(i)); });
            getter("  session.trackMidiInputName x120", [&](int i) { return session.trackMidiInputName(i).length(); });
            getter("  session.trackAudioInputName x120", [&](int i) { return session.trackAudioInputName(i).length(); });
            getter("  session.trackMonitoring x120", [&](int i) { return static_cast<int>(session.trackMonitoring(i)); });
            getter("  session.trackLaneHeight x120", [&](int i) { return static_cast<int>(session.trackLaneHeight(i)); });
            getter("  session.deviceSlots x120", [&](int i) { return static_cast<int>(session.deviceSlots(i).size()); });
            getter("  session.trackGroups x120", [&](int) { return static_cast<int>(session.trackGroups().size()); });
            report("  session.loopRange", medianMilliseconds(30, [&] (int) { session.loopRange(); }));
        }

        auto canvas = canvasFor(*arrangement);
        report("arrangement full paint (default zoom)", medianMilliseconds(10, [&] (int)
        {
            paintArea(*arrangement, canvas, arrangement->getLocalBounds());
        }));
        report("arrangement playhead strip (default zoom)", medianMilliseconds(40, [&] (int i)
        {
            const auto x = 600 + (i % 8) * 3;
            paintArea(*arrangement, canvas, arrangement->getLocalBounds().withX(x).withWidth(6));
        }));
        arrangement->fit();
        report("arrangement full paint (whole song)", medianMilliseconds(10, [&] (int)
        {
            paintArea(*arrangement, canvas, arrangement->getLocalBounds());
        }));
        report("arrangement playhead strip (whole song)", medianMilliseconds(40, [&] (int i)
        {
            const auto x = 600 + (i % 8) * 3;
            paintArea(*arrangement, canvas, arrangement->getLocalBounds().withX(x).withWidth(6));
        }));
        report("meters read (takeOutputLevels)", medianMilliseconds(60, [&] (int) { session.takeOutputLevels(); }));

        auto rackCanvas = canvasFor(*rack);
        report("device rack full paint", medianMilliseconds(10, [&] (int)
        {
            paintArea(*rack, rackCanvas, rack->getLocalBounds());
        }));

        // Commands, each what one click or one pixel of a drag costs.
        // The groups made above put a bus before every eighth track, so the
        // kinds are read again for what follows.
        layout = readLayout(session);
        const auto knobTrack = layout.midi.front();
        const auto slots = session.deviceSlots(knobTrack);
        if (!slots.empty())
        {
            const auto synthSlot = slots.front().pluginIndex;
            rack->selectTrack(knobTrack);
            session.beginDeviceParameterGesture(knobTrack, synthSlot, 0);
            report("knob drag step", medianMilliseconds(40, [&] (int i)
            {
                session.setDeviceParameter(knobTrack, synthSlot, 0, (i % 2) == 0 ? 0.3f : 0.6f);
            }));
            session.endDeviceParameterGesture(knobTrack, synthSlot, 0);
        }
        session.beginTrackVolumeGesture(1);
        report("fader drag step", medianMilliseconds(40, [&] (int i)
        {
            session.setTrackVolumeDb(1, (i % 2) == 0 ? -3.0f : -6.0f);
        }));
        session.endTrackVolumeGesture(1);
        session.beginTempoGesture();
        report("tempo drag step", medianMilliseconds(10, [&] (int i)
        {
            session.setTempo((i % 2) == 0 ? 121.0 : 120.0);
        }));
        session.endTempoGesture();
        report("mute toggle (command and all panels)", medianMilliseconds(10, [&] (int) { session.toggleTrackMute(3); }));
        report("undo (mute)", medianMilliseconds(6, [&] (int) { session.undo(); }));
        report("redo (mute)", medianMilliseconds(6, [&] (int) { session.redo(); }));
        {
            const auto before = session.trackCount();
            report("add a track", timeOnce([&] { session.addTrack(Session::TrackType::midi); }));
            if (session.trackCount() != before + 1)
                throw std::runtime_error("The stress profile could not add a track");
            report("undo (add a track)", timeOnce([&] { session.undo(); }));
            if (session.trackCount() != before)
                throw std::runtime_error("The stress profile could not undo a track");
        }
        report("import one audio file (new clip)", timeOnce([&]
        {
            if (const auto imported = session.importAudioAt(loops.front(), layout.audio.back(), shape.minutes * 60.0 + 4.0); imported.failed())
                throw std::runtime_error(("The stress profile could not import a file - " + imported.getErrorMessage()).toStdString());
        }));
        session.undo();
        report("automation mirror", medianMilliseconds(20, [&] (int)
        {
            session.markModified();
            session.mirrorAutomationToEngine();
        }));

        // A clip moved and a device added, as while stopped, and then with the
        // playback graph allocated as it is while the song plays - the graph
        // the transport builds when play is pressed, and again after every
        // change to a chain.
        const auto moveAClip = [&]
        {
            auto* track = te::getAudioTracks(*session.edit)[layout.midi.front()];
            const auto clips = track->getClips();
            if (clips.isEmpty()) return;
            auto* clip = clips.getLast();
            const auto position = clip->getPosition();
            const ClipGeometry moved {position.time.getStart().inSeconds() + 1.0, position.time.getEnd().inSeconds() + 1.0,
                                      position.offset.inSeconds()};
            if (const auto result = session.editClip(clip->itemID, moved, ClipGesture::move); result.failed())
                throw std::runtime_error(("The stress profile could not move a clip - " + result.getErrorMessage()).toStdString());
        };
        report("move a clip", medianMilliseconds(6, [&] (int) { moveAClip(); }));
        report("undo (move a clip)", medianMilliseconds(6, [&] (int) { session.undo(); }));
        for (int i = 0; i < 6; ++i) session.undo();
        report("add a device (Reverb)", timeOnce([&]
        {
            if (session.addDevice("Reverb", layout.audio.front()).failed())
                throw std::runtime_error("The stress profile could not add a device");
        }));
        report("undo (add a device)", timeOnce([&] { session.undo(); }));
        {
            auto& transport = session.edit->getTransport();
            report("playback graph build (ensureContextAllocated)", timeOnce([&] { transport.ensureContextAllocated(true); }));
            report("with the graph: move a clip", medianMilliseconds(6, [&] (int) { moveAClip(); }));
            report("with the graph: undo (move a clip)", medianMilliseconds(6, [&] (int) { session.undo(); }));
            for (int i = 0; i < 6; ++i) session.undo();
            report("with the graph: add a device (Reverb)", timeOnce([&]
            {
                if (session.addDevice("Reverb", layout.audio.front()).failed())
                    throw std::runtime_error("The stress profile could not add a device");
            }));
            report("with the graph: undo (add a device)", timeOnce([&] { session.undo(); }));
            report("with the graph: mute toggle", medianMilliseconds(6, [&] (int) { session.toggleTrackMute(3); }));
            report("with the graph: change announced", medianMilliseconds(10, [&] (int) { session.sendSynchronousChangeMessage(); }));
            // What the engine does on the message thread after an edit while
            // the graph stands: the restart it posts itself is dispatched here
            // by hand, since the profile runs no message loop, and timed as the
            // stall the window would show.
            const auto pendingUpdates = [&]
            {
                return timeOnce([&]
                {
                    session.edit->dispatchPendingUpdatesSynchronously();
                    session.engine.getDeviceManager().dispatchPendingUpdates();
                });
            };
            report("with the graph: pending updates after nothing", pendingUpdates());
            for (int round = 0; round < 3; ++round)
            {
                moveAClip();
                report("with the graph: pending updates after a MIDI clip move", pendingUpdates());
                session.undo();
                pendingUpdates();
            }
            const auto moveAnAudioClip = [&]
            {
                auto* track = te::getAudioTracks(*session.edit)[layout.audio.front()];
                const auto clips = track->getClips();
                if (clips.isEmpty()) return;
                auto* clip = clips.getLast();
                const auto position = clip->getPosition();
                const ClipGeometry moved {position.time.getStart().inSeconds() + 1.0, position.time.getEnd().inSeconds() + 1.0,
                                          position.offset.inSeconds()};
                if (const auto result = session.editClip(clip->itemID, moved, ClipGesture::move); result.failed())
                    throw std::runtime_error(("The stress profile could not move an audio clip - " + result.getErrorMessage()).toStdString());
            };
            for (int round = 0; round < 3; ++round)
            {
                moveAnAudioClip();
                report("with the graph: pending updates after an audio clip move", pendingUpdates());
                session.undo();
                pendingUpdates();
            }
            for (int round = 0; round < 3; ++round)
            {
                session.edit->restartPlayback();
                report("with the graph: pending updates after restartPlayback", pendingUpdates());
            }
            for (int round = 0; round < 2; ++round)
            {
                if (session.addDevice("Reverb", layout.audio.front()).failed())
                    throw std::runtime_error("The stress profile could not add a device");
                report("with the graph: pending updates after adding a device", pendingUpdates());
                session.undo();
                pendingUpdates();
            }
            session.toggleTrackMute(3);
            report("with the graph: pending updates after a mute", pendingUpdates());
            session.toggleTrackMute(3);
            pendingUpdates();
            report("playback graph free", timeOnce([&] { transport.freePlaybackContext(); }));
        }

        // Save and load, as File > Save and File > Open do them.
        const auto document = folder.getChildFile("stress.rhinoedit");
        juce::ValueTree snapshot;
        report("save: projectSnapshot", timeOnce([&] { snapshot = session.projectSnapshot(document); }));
        std::unique_ptr<juce::XmlElement> xml;
        report("save: createXml", timeOnce([&] { xml = snapshot.createXml(); }));
        report("save: writeTo", timeOnce([&]
        {
            if (xml == nullptr || !xml->writeTo(document))
                throw std::runtime_error("The stress profile could not write its document");
        }));
        std::printf("%-44s %10.1f MB\n", "document file", static_cast<double>(document.getSize()) / (1024.0 * 1024.0));
        xml.reset();
        snapshot = {};
        reportMemory("memory before load");
        juce::ValueTree loaded;
        report("load: parseXML and fromXml", timeOnce([&]
        {
            auto parsed = juce::parseXML(document);
            if (parsed == nullptr)
                throw std::runtime_error("The stress profile could not parse its document");
            loaded = juce::ValueTree::fromXml(*parsed);
        }));
        report("load: restoreProject (and every panel)", timeOnce([&]
        {
            if (session.restoreProject(loaded, document).failed())
                throw std::runtime_error("The stress profile could not reopen its document");
        }));
        loaded = {};
        reportMemory("memory after load");
        report("change announced after load", medianMilliseconds(10, [&] (int) { session.sendSynchronousChangeMessage(); }));
        canvas = canvasFor(*arrangement);
        report("arrangement full paint after load", medianMilliseconds(10, [&] (int)
        {
            paintArea(*arrangement, canvas, arrangement->getLocalBounds());
        }));

        if (shape.renderSeconds > 0)
        {
            double wall = 0.0;
            const auto done = renderSeconds(session, folder.getChildFile("render.wav"), shape.renderSeconds, wall);
            const auto rendered = shape.renderSeconds * done;
            std::printf("%-44s %10.2f s for %.1f s of song (%.2fx real time, %.0f us a 512 block)\n", "offline render",
                        wall, rendered, rendered / std::max(1.0e-6, wall),
                        wall * 1.0e6 / std::max(1.0, rendered * 48000.0 / 512.0));
            std::fflush(stdout);
        }

        // The live graph, a block at a time through the engine's hosted audio
        // device with no hardware, the transport rolling from the top: what a
        // callback costs the audio thread, at the two block sizes that matter.
        for (const int liveBlock : {512, 128})
        {
            constexpr double liveRate = 48000.0;
            session.releaseAudioDevice();
            auto& hosted = session.engine.getDeviceManager().getHostedAudioDeviceInterface();
            te::HostedAudioDeviceInterface::Parameters parameters;
            parameters.sampleRate = liveRate;
            parameters.blockSize = liveBlock;
            parameters.inputChannels = 0;
            parameters.outputChannels = 2;
            hosted.initialise(parameters);
            hosted.prepareToPlay(liveRate, liveBlock);
            session.engine.getDeviceManager().dispatchPendingUpdates();
            auto& transport = session.edit->getTransport();
            const auto build = timeOnce([&]
            {
                transport.ensureContextAllocated();
                session.edit->dispatchPendingUpdatesSynchronously();
                session.engine.getDeviceManager().dispatchPendingUpdates();
            });
            if (session.edit->getCurrentPlaybackContext() == nullptr)
            {
                std::printf("no playback context on the hosted device at %d\n", liveBlock);
                continue;
            }
            std::printf("%-44s %10.2f ms\n", ("hosted graph build at " + juce::String(liveBlock)).toRawUTF8(), build);
            transport.setPosition({});
            transport.play(false);
            juce::AudioBuffer<float> songOut(2, liveBlock);
            juce::MidiBuffer midiIn;
            std::vector<double> blockTimes;
            const auto blocks = static_cast<int>(liveRate * 8.0 / liveBlock);
            blockTimes.reserve(static_cast<size_t>(blocks));
            double peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                songOut.clear();
                midiIn.clear();
                const auto start = juce::Time::getHighResolutionTicks();
                hosted.processBlock(songOut, midiIn);
                blockTimes.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1.0e6);
                peak = std::max(peak, static_cast<double>(songOut.getMagnitude(0, liveBlock)));
            }
            transport.stop(false, false);
            const auto budget = liveBlock * 1.0e6 / liveRate;
            // The first second apart: the graph warms its file readers and its
            // threads there, and a song is started once.
            const auto warm = static_cast<size_t>(liveRate / liveBlock);
            const auto maxAt = static_cast<int>(std::max_element(blockTimes.begin(), blockTimes.end()) - blockTimes.begin());
            std::vector<double> steady(blockTimes.begin() + static_cast<std::ptrdiff_t>(std::min(warm, blockTimes.size() - 1)), blockTimes.end());
            std::sort(blockTimes.begin(), blockTimes.end());
            std::sort(steady.begin(), steady.end());
            std::printf("live graph at %d: median %.0f us, p99 %.0f us, max %.0f us (block %d) of a %.0f us block (%.0f%% median, %.0f%% p99), peak %.3f\n",
                        liveBlock, blockTimes[blockTimes.size() / 2], blockTimes[blockTimes.size() * 99 / 100], blockTimes.back(), maxAt,
                        budget, blockTimes[blockTimes.size() / 2] / budget * 100.0, blockTimes[blockTimes.size() * 99 / 100] / budget * 100.0,
                        peak);
            std::printf("  after the first second: median %.0f us, p99 %.0f us, max %.0f us (%.0f%% p99)\n",
                        steady[steady.size() / 2], steady[steady.size() * 99 / 100], steady.back(),
                        steady[steady.size() * 99 / 100] / budget * 100.0);
            std::fflush(stdout);
            transport.freePlaybackContext();
        }
        session.releaseAudioDevice();

        clipPanel.reset();
        djView.reset();
        rack.reset();
        grid.reset();
        arrangement.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        folder.deleteRecursively();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Stress profile failed: %s\n", error.what());
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        return 1;
    }
}
}
