#include "MainComponent.h"
#include "InfoHints.h"
#include "SystemUsage.h"
#include "ui/shared/WallClock.h"
#include <cmath>

namespace rhino
{

static juce::String formatMemory(juce::uint64 bytes)
{
    const auto megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes >= 1024.0) return juce::String(megabytes / 1024.0, 2) + " GB";
    return juce::String(juce::roundToInt(megabytes)) + " MB";
}

// What the Info View says while the pointer is resting on a control.
//
// The app floated a tooltip for this. A tooltip covers the thing it is
// describing, is gone the moment the pointer moves, and holds about two
// words because that is all a popup over a timeline can carry - so the
// interesting half of what a control does was never written down anywhere
// the user could read it. The Info View is already on screen, has room for
// a sentence, and is where Live puts the same thing.
//
// A control still says what it is through setTooltip, which keeps the
// words beside the control they describe and next to the code that knows
// when they change. Only where they are shown has moved.
void MainComponent::updateHint()
{
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    auto* under = source.isTouch() ? nullptr : source.getComponentUnderMouse();
    // The control directly under the pointer and no ancestor of it: a
    // control that says nothing says nothing deliberately, and must not
    // inherit the explanation of the panel it happens to sit on.
    auto* client = dynamic_cast<juce::TooltipClient*>(under);
    if (client == nullptr) under = nullptr;
    const auto text = client != nullptr ? client->getTooltip() : juce::String();
    // Which of this and the status line wins the panel is the whole of the
    // behaviour, so the rule is stated once in InfoHints.h rather than
    // here: this only remembers what the panel was last told about.
    const auto replaces = infoHintReplaces(hintSource.getComponent(), hintText, under, text);
    hintSource = under;
    hintText = text;
    if (replaces) infoView.setText(text, false);
}

void MainComponent::timerCallback()
{
    if (paneLayoutPending && !juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown())
        applyPaneLayout();
    updateHint();
    // The engine both starts and finishes a recording on the audio thread
    // and broadcasts neither, so the clips it wrote are collected here,
    // polled rather than listened for.
    session.recordingStopped();
    // The booth's reads finish on a worker and a changed cell is made
    // again once nothing is held; neither is announced, so both are polled.
    session.setPointerHeld(juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown());
    session.djPoll();
    updateRecordButton();
    const auto seconds = session.playheadSeconds();
    updateReadings(seconds);
    // The loop is where the transport will turn, so it heads the column of
    // readings that describe the session rather than follow it.
    position.setLoop(displayLoop ? loopText() : juce::String());
    const auto signature = session.timeSignature();
    position.setTempoAndSignature(
        displayTempo ? juce::String(session.tempo(), 0) : juce::String(),
        displayTimeSignature ? juce::String(signature.numerator) + "/" + juce::String(signature.denominator)
                             : juce::String());
    // A count-in owns the position while it runs, but the clock, the load
    // meters and the loop keep reading, which is why all of them are
    // updated above this.
    if (session.isCountingIn())
    {
        position.setPosition("COUNT-IN  " + juce::String(session.countInBarsRemaining()));
        return;
    }
    position.setPosition(displayPosition ? placeText(seconds) : juce::String());
}

// The second line of the display. The clock follows the playhead at the
// timer's own rate, but both load meters are sampled a few times a second:
// each one walks kernel structures, and a figure that changed thirty times
// a second could not be read anyway.
void MainComponent::updateReadings(double seconds)
{
    if (++readingSample >= 15)
    {
        readingSample = 0;
        cpuLoad = SystemUsage::processCpuLoad();
        memoryBytes = SystemUsage::processMemoryBytes();
        audioDescription = audioDeviceText();
    }
    position.setClock(displayTime ? formatClock(seconds, true) + "  /  "
                                        + formatClock(session.songLengthSeconds(), true)
                                  : juce::String());
    juce::StringArray load;
    if (displayCpu) load.add("CPU " + juce::String(juce::roundToInt(cpuLoad * 100.0)) + "%");
    if (displayMemory) load.add("RAM " + formatMemory(memoryBytes));
    position.setStatistics(load.joinIntoString("   "));
    // The device reading is the lowest priority thing in the box: the
    // display drops it first when the window is too narrow to hold
    // everything, before it gives up any of the readings above.
    position.setDeviceInfo(displayAudio ? audioDescription : juce::String());
}

// The loop, written exactly as the position above it is - bar, beat and
// sixteenth - because a loop counted differently from the playhead is a
// loop nobody can read against it. Looping is always on: with no span
// dragged on the ruler it is the whole arrangement, so until someone
// narrows it this reads as the length of the project. The word LOOP is the
// display's, not this function's; it drops it when the box is tight.
juce::String MainComponent::loopText() const
{
    const auto range = session.loopRange();
    return placeText(range.getStart().inSeconds())
         + " - " + placeText(range.getEnd().inSeconds());
}

// Bar, count and sixteenth, which is how the position and the loop beside
// it are both written. One formatter for both, because a loop that counts
// differently from the playhead is a loop nobody can read against it. The
// sixteenth is the field that moves while the transport rolls: bars and
// counts change too slowly to tell a stalled readout from a stopped one.
juce::String MainComponent::placeText(double seconds) const
{
    const auto beats = session.secondsToBeats(seconds);
    const auto signature = session.timeSignature();
    const auto place = musicalPosition(beats, signature.numerator, signature.denominator);
    return juce::String(place.bar).paddedLeft('0', 3)
         + "." + juce::String(place.count)
         + "." + juce::String(place.sixteenth);
}

// What the engine is actually running on. Sampled with the load meters
// rather than read every frame, because it changes only when the audio
// settings do. The milliseconds are the block's own latency: the figure
// that moves when the buffer size is changed to chase a crackle.
juce::String MainComponent::audioDeviceText() const
{
    const auto device = session.audioDeviceSummary();
    if (!device.open) return "NO AUDIO DEVICE";
    const auto rate = device.sampleRate;
    const auto block = device.blockSize;
    if (rate <= 0.0 || block <= 0) return device.name.toUpperCase();
    return juce::String(rate / 1000.0, 1) + " kHz   " + juce::String(block)
         + "   " + juce::String(block * 1000.0 / rate, 1) + " ms";
}

}
