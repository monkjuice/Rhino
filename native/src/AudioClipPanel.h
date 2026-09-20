#pragma once
#include "Session.h"
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
    std::function<void(juce::String)> status;

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
    void refreshThumbnail();
    void paintWaveform(juce::Graphics&, juce::Rectangle<int> area);
    void paintEmpty(juce::Graphics&, juce::Rectangle<int> area);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void editWillChange() override;
    void editDidChange() override;

    Session& session;
    te::EditItemID clip;
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
    juce::TextButton reverse, mute;
    juce::Rectangle<int> controlsArea, waveArea;
    static constexpr int headerHeight = 24, controlWidth = 66, switchWidth = 78;
    static constexpr int minimumWaveWidth = 140;
};
}
