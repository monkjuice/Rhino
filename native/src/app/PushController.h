#pragma once
#include "model/Session.h"
#include "PushProtocol.h"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace rhino
{

// The original Ableton Push, driven by Rhino rather than by Live.
//
// Rhino owns the Push's User port, both ways, and keeps the engine off all
// four of its ports (Session::reserveMidiPorts). It writes the display, lights
// the pads and buttons, and turns what comes back into Rhino's own commands:
//
//   pads       played on the MIDI thread, translated into the scale (or onto
//              a Drum Rack's bank) and handed to Session::sendControllerMidi,
//              which is where a keyboard's notes arrive - so a track hears
//              them exactly as it hears the Launchkey, armed or monitoring In;
//   buttons    and encoders, on the message thread: transport, tracks, the
//              selected device's controls and the mixer. None of them ever
//              reaches a track.
//
// Three units: PushController.cpp holds the ports and the MIDI thread,
// PushControllerControls.cpp what the buttons and encoders do, and
// PushControllerSurface.cpp what the Push shows. See
// wiki/pages/ableton-push.md.
class PushController final : private juce::MidiInputCallback,
                             private juce::AsyncUpdater,
                             private juce::Timer,
                             private Session::Listener
{
public:
    explicit PushController(Session&);
    ~PushController() override;

    // Looks for a Push, opens it, and keeps looking. Nothing before this
    // touches the hardware, so the test runners, which never call it, never
    // take the Push from whoever is playing it.
    void start();
    void stop();
    bool isConnected() const { return connected; }

    // What the shell tells it: the arrangement's selected track, and the
    // device the rack shows on it.
    void setSelectedTrack(int track);
    void setSelectedDevice(int track, int pluginIndex);

    // What it asks of the shell. Play and Record go through the shell, which
    // knows what they mean in the view that is showing.
    std::function<void(int track)> selectTrack;
    std::function<void(int track, int pluginIndex)> selectDevice;
    std::function<void()> play, returnToStart, record;
    std::function<void(const juce::String&)> status;
    // False while the shell is busy - opening a project, exporting - when a
    // button should do nothing rather than act on a document in flux.
    std::function<bool()> commandsAllowed;

    // Where translated pad notes go: Session::sendControllerMidi, unless a
    // test listens instead. Called on the MIDI thread.
    std::function<void(const juce::MidiMessage&)> playNote;
    // Where messages to the Push go: the port, unless a test listens instead.
    std::function<void(const juce::MidiMessage&)> sendToPush;
    // A message as if from the Push. The pads are played at once, as they
    // are from the MIDI thread; anything else waits for the message thread,
    // or for flush().
    void receive(const juce::MidiMessage&);
    // Handles what is waiting and redraws now, rather than at the next
    // async update or tick. For the tests.
    void flush();
    // Makes the next redraw send everything, as on connecting.
    void invalidateSurface();

    enum class EncoderMode { device, volume, pan, scales };
    EncoderMode encoderMode() const { return mode; }
    const push::NoteLayout& noteLayout() const { return layout; }

private:
    friend int runArrangementTest();

    // ---- PushController.cpp: ports and the MIDI thread ----
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void editWillChange() override;
    void editDidChange() override;
    void lookForPush();
    bool openPorts(const juce::MidiDeviceInfo& input, const juce::MidiDeviceInfo& output);
    void closePorts(bool sayGoodbye);
    void greet();
    void send(const juce::MidiMessage&);
    void sendBytes(const push::Bytes&);
    void flushOutput();
    void playPad(const juce::MidiMessage&);

    // ---- PushControllerControls.cpp: buttons and encoders ----
    void handleControl(const juce::MidiMessage&);
    void pressButton(int controller);
    void releaseButton(int controller);
    void turnEncoder(int encoder, int delta);
    void turnDeviceEncoder(int encoder, int delta);
    void turnMixerEncoder(int encoder, int delta);
    void turnScalesEncoder(int encoder, int delta);
    void turnTempo(int delta);
    void turnMaster(int delta);
    void tapTempo();
    void touchEncoder(int touch, bool touched);
    void moveOctave(int direction);
    void selectTrackAt(int track);
    void setEncoderMode(EncoderMode);
    // Keeps the selection pointing at something that exists: a track that
    // was deleted, a device taken off the chain. A track with no device the
    // rack has reported falls back to its instrument, or its first device.
    void validateSelection();
    // The first of the eight tracks the lower row shows.
    int bankFirst() const;
    // Every open gesture is closed before anything that could take its
    // target away or make it mean something else.
    void endGestures();
    void endIdleGestures(double nowMs);
    int pageCount() const;
    // The Drum Rack on the selected track, if it has one: the plugin index,
    // or -1.
    int drumRackSlot() const;
    void saveLayout() const;
    void loadLayout();

    // ---- PushControllerSurface.cpp: what the Push shows ----
    struct Surface
    {
        std::array<int, push::padCount> pads {};
        std::array<int, 128> buttons {};
        std::array<std::string, push::displayLineCount> lines;
    };
    Surface wantedSurface();
    void wantPads(Surface&);
    void wantButtons(Surface&);
    void wantDisplay(Surface&);
    void redraw();
    // The pad map the MIDI thread plays from, rebuilt when the layout, the
    // selected track or its Drum Rack's bank changes.
    void updatePadMap();
    int trackColourIndex(int track, int shade) const;

    Session& session;

    // The ports. The input calls back on its own thread; the output sends
    // from a thread of its own, because a sysex can block the caller.
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::MidiBuffer pendingOutput;
    bool started = false, connected = false;
    int attemptsFailed = 0;
    bool reportedBusy = false;
    int ticksToLook = 0;
    juce::String inputIdentifier;

    // ---- shared with the MIDI thread ----
    juce::SpinLock padLock;
    // The note each pad plays, or -1, and the note each held pad is sounding,
    // so letting go releases what was played even if the layout moved under
    // the finger.
    std::array<int, push::padCount> padMap {};
    std::array<int, push::padCount> sounding {};
    // In fourths, a note sits on two pads, one row above the other. The note
    // starts with the first pad down and stops with the last one up.
    std::array<int, 128> padsHolding {};
    std::atomic<bool> accent {false};
    // Everything that is not a pad, waiting for the message thread.
    // Buttons, encoders and touches cross from the MIDI thread to the message
    // thread through a ring the MIDI thread never grows: one writer, one
    // reader, and a message that finds it full is dropped - a lost button
    // press costs a second press, where a MIDI thread blocked on the heap
    // costs every note on the port its timing.
    static constexpr int controlQueueSize = 256;
    std::array<juce::MidiMessage, controlQueueSize> controlQueue;
    std::atomic<int> controlWritten {0}, controlRead {0};

    // ---- message thread ----
    int selectedTrack = 0, selectedPlugin = -1;
    EncoderMode mode = EncoderMode::device, modeBeforeScales = EncoderMode::device;
    int parameterPage = 0;
    push::NoteLayout layout;
    bool shift = false, muteHeld = false, soloHeld = false, rowUsedWhileHeld = false;
    // Detents turned on an encoder whose control moves in whole steps.
    std::array<int, push::rowButtons + 3> detents {};
    std::array<bool, push::touchStrip + 1> touched {};
    push::TapTempo taps;

    // An open gesture: one per encoder at most. Opened by the first turn, not
    // the touch, because opening one takes an automated knob off its lane.
    struct Gesture
    {
        enum class Kind { none, device, volume, pan, master, tempo } kind = Kind::none;
        int track = -1, plugin = -1, parameter = -1;
        double lastMoveMs = 0.0;
        // How long the gesture stays open after its last move while nothing
        // touches it: short for a turn, long enough for the next tap.
        double idleMs = 300.0;
    };
    // The eight track encoders, then master, swing and tempo - indexed as the
    // touch notes are - and the tap button's gesture last.
    static constexpr int tapGesture = push::touchTempoEncoder + 1;
    std::array<Gesture, tapGesture + 1> gestures {};
    void beginGesture(int slot, Gesture);
    void endGesture(int slot);

    // What each pad is in the scale, beside the note it plays, for its light.
    // Message thread only.
    std::array<push::PadRole, push::padCount> padRoles {};

    Surface shown;
    bool shownValid = false;
};

}
