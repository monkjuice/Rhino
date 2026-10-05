#pragma once
#include "Session.h"
#include "ControlBarFields.h"
#include <array>
#include <memory>

namespace rhino
{
// The audio clip editor: one clip's own mix beside its waveform.
//
// Everything on this panel writes to the clip the arrangement has open and to
// nothing else. That is the whole point of it: the track cards in the
// arrangement carry the *track* fader, and a gain change there moves every
// clip in the lane, which is not what is wanted when one sample is too loud.
class AudioClipPanel final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer,
                             private Session::Listener
{
public:
    explicit AudioClipPanel(Session&);
    ~AudioClipPanel() override;
    // An id that is not an audio clip empties the panel rather than being
    // refused, so a caller can hand over whatever the arrangement selected.
    void setClip(te::EditItemID);
    te::EditItemID clipID() const { return clip; }
    bool hasClip() const { return mix.valid; }
    juce::String clipName() const { return mix.name; }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void(juce::String)> status;
    // Where the timeline's line is. Unset, the panel falls back to the
    // transport, which is the same position whenever it is stopped.
    std::function<double()> splitPosition;

private:
    friend int runArrangementTest();
    // The order the controls are laid out in, and the index every one of the
    // parallel arrays below is addressed by.
    enum Control { gainControl, panControl, pitchControl, fadeInControl, fadeOutControl, controlCount };
    void configureControls();
    void sync();
    void apply(int control);
    void pushValues();
    void updateReadouts();
    juce::String readout(int control) const;
    juce::String gestureName(int control) const;
    // Cuts the open clip in two at the line. The panel acts on the clip it is
    // showing rather than on whatever the arrangement has selected, because
    // those are the same clip and this one is the one being looked at.
    void splitAtLine();
    void refreshThumbnail();
    void paintWaveform(juce::Graphics&, juce::Rectangle<int> area);
    void paintEmpty(juce::Graphics&, juce::Rectangle<int> area);
    // AudioClipWarp.cpp - the warp strip along the foot of the panel, and the
    // markers over the waveform. Split out because this panel was already at
    // the size where a second responsibility belongs in a file of its own, and
    // warping is emphatically a second responsibility: everything above is one
    // clip's mix, and everything here is how that clip reads the clock.
    void configureWarpControls();
    void syncWarpControls();
    void layoutWarpControls(juce::Rectangle<int> strip);
    void paintWarpStrip(juce::Graphics&);
    // The waveform as the clip plays it rather than as it sits in the file:
    // one span per pair of markers, each drawn into the width its segment has
    // been stretched to. The engine's own mapping is piecewise linear between
    // markers, so this is exact rather than an approximation of it.
    void paintWarpedWaveform(juce::Graphics&, juce::Rectangle<int> inner);
    void paintWarpMarkers(juce::Graphics&);
    bool warpMarkersVisible() const;
    // The whole warped extent of the file, which is what the waveform spans
    // while markers are being worked on.
    double warpedLength() const;
    // The band the warp is drawn across, inset from the waveform box so a
    // handle at either end of the file is drawn whole.
    juce::Rectangle<int> warpSpan() const;
    float xForWarpTime(double warpSeconds) const;
    double warpTimeAtX(float x) const;
    // Walks the marker list the other way, so a double-click on the waveform
    // knows which moment in the file it landed on.
    double sourceTimeForWarpTime(double warpSeconds) const;
    double warpTimeForSourceTime(double sourceSeconds) const;
    int warpMarkerAt(juce::Point<float>) const;
    void showWarpModeMenu();
    void report(const juce::Result&);
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void visibilityChanged() override;
    void editWillChange() override;
    void editDidChange() override;

    Session& session;
    te::EditItemID clip;
    // A change arrived while the shell had this pane switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
    Session::AudioClipMix mix;
    // One clip at a time, so one thumbnail: the cache is sized for the handful
    // a user cycles through rather than for a whole arrangement.
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache {8};
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    juce::String thumbnailSource;
    bool thumbnailReadable = false;
    // Set while the panel is writing its own controls, so the value changes it
    // causes are not read back as the user moving them.
    bool syncing = false;
    juce::Label title, subtitle;
    std::array<juce::Slider, controlCount> sliders;
    std::array<juce::Label, controlCount> names, values;
    juce::TextButton reverse, mute, split;
    juce::Rectangle<int> controlsArea, waveArea;
    // Warping. warp is the whole of what the model says about this clip, read
    // back after every edit rather than tracked here.
    Session::ClipWarp warp;
    juce::TextButton warpToggle, markerToggle, warpModeButton, halveBpm, doubleBpm, detectBpm, straighten;
    ValueDragBox clipBpm;
    juce::Rectangle<int> warpStrip, markerBar;
    // Which marker is being dragged, and whether the gesture bracket that
    // folds the drag into one undo step is open.
    int draggingMarker = -1;
    bool warpGestureOpen = false;
    static constexpr int headerHeight = 24, controlWidth = 66, switchWidth = 78;
    static constexpr int minimumWaveWidth = 140;
    // The strip along the foot of the panel, and the band of marker handles
    // over the top of the waveform.
    static constexpr int warpStripHeight = 30, markerBarHeight = 13;
    // How near a handle a press has to land to take it.
    static constexpr float markerGrabPixels = 5.0f;
};
}
