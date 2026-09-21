#include "AudioClipPanel.h"
#include "Theme.h"
#include "WaveformLanes.h"
#include <cmath>

namespace rhino
{
namespace
{
constexpr auto accentColour = 0xffc6d58c;
constexpr auto waveColour = 0xff8cc5d2;

juce::String secondsText(double seconds)
{
    if (seconds < 1.0)
        return juce::String(seconds * 1000.0, 0) + " ms";
    return juce::String(seconds, 2) + " s";
}

juce::String panText(float pan)
{
    if (std::abs(pan) < 0.005f)
        return "C";
    return (pan < 0.0f ? "L" : "R") + juce::String(std::abs(pan) * 100.0f, 0);
}

void styleSwitch(juce::TextButton& button, bool on, juce::Colour onColour)
{
    button.setToggleState(on, juce::dontSendNotification);
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a30));
    button.setColour(juce::TextButton::buttonOnColourId, onColour);
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff9aa6af));
    button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff10161a));
}
}

AudioClipPanel::AudioClipPanel(Session& s) : session(s)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    formats.registerBasicFormats();
    configureControls();
    session.addChangeListener(this);
    session.listeners.add(this);
}

AudioClipPanel::~AudioClipPanel()
{
    session.listeners.remove(this);
    session.removeChangeListener(this);
}

void AudioClipPanel::configureControls()
{
    title.setFont(uiFontBold(11.0f));
    title.setColour(juce::Label::textColourId, juce::Colour(0xffdce5ea));
    title.setInterceptsMouseClicks(false, false);
    subtitle.setFont(uiFont(10.5f));
    subtitle.setColour(juce::Label::textColourId, juce::Colour(0xff8d98a3));
    subtitle.setJustificationType(juce::Justification::centredRight);
    subtitle.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(title);
    addAndMakeVisible(subtitle);

    static constexpr const char* labels[controlCount] {"Gain", "Pan", "Pitch", "Fade In", "Fade Out"};
    static constexpr const char* tooltips[controlCount] {
        "Gain of this clip only, in decibels",
        "Pan of this clip only",
        "Transpose this clip, in semitones",
        "Fade in length for this clip",
        "Fade out length for this clip"};
    for (int i = 0; i < controlCount; ++i)
    {
        const auto index = static_cast<size_t>(i);
        names[index].setText(labels[i], juce::dontSendNotification);
        names[index].setFont(uiFont(11.0f));
        names[index].setJustificationType(juce::Justification::centred);
        names[index].setColour(juce::Label::textColourId, juce::Colour(0xffdfe6ea));
        names[index].setInterceptsMouseClicks(false, false);
        values[index].setFont(uiFont(10.5f));
        values[index].setJustificationType(juce::Justification::centred);
        values[index].setColour(juce::Label::textColourId, juce::Colour(0xffaebbc3));
        values[index].setInterceptsMouseClicks(false, false);
        auto& slider = sliders[index];
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setTooltip(tooltips[i]);
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(accentColour));
        slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2d3940));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(accentColour).brighter(0.28f));
        slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff20282e));
        slider.setDoubleClickReturnValue(true, 0.0);
        slider.onDragStart = [this, i] { session.beginAudioClipGesture(gestureName(i)); };
        slider.onValueChange = [this, i] { if (!syncing) apply(i); };
        slider.onDragEnd = [this] { session.endAudioClipGesture(); };
        addAndMakeVisible(names[index]);
        addAndMakeVisible(values[index]);
        addAndMakeVisible(slider);
    }
    sliders[gainControl].setRange(Session::minimumClipGainDb, Session::maximumClipGainDb, 0.1);
    sliders[panControl].setRange(-1.0, 1.0, 0.01);
    sliders[pitchControl].setRange(-Session::maximumClipPitchSemitones, Session::maximumClipPitchSemitones, 0.01);
    sliders[fadeInControl].setRange(0.0, 1.0, 0.001);
    sliders[fadeOutControl].setRange(0.0, 1.0, 0.001);

    reverse.setButtonText("Reverse");
    reverse.setTooltip("Play this clip's source material backwards");
    reverse.onClick = [this]
    {
        const auto result = session.setAudioClipReversed(clip, !mix.reversed);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    mute.setButtonText("Mute");
    mute.setTooltip("Silence this clip without muting its track");
    mute.onClick = [this]
    {
        const auto result = session.setAudioClipMuted(clip, !mix.muted);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    addAndMakeVisible(reverse);
    addAndMakeVisible(mute);
    sync();
}

void AudioClipPanel::setClip(te::EditItemID id)
{
    clip = id;
    sync();
}

void AudioClipPanel::sync()
{
    mix = session.audioClipMix(clip);
    const auto enabled = mix.valid;
    for (auto& slider : sliders)
        slider.setEnabled(enabled);
    reverse.setEnabled(enabled);
    mute.setEnabled(enabled);
    // A fade can never be longer than the clip it is on, so the two fade
    // controls are scaled to whatever clip the panel is showing.
    const auto length = std::max(0.01, mix.lengthSeconds());
    sliders[fadeInControl].setRange(0.0, length, std::min(0.001, length / 500.0));
    sliders[fadeOutControl].setRange(0.0, length, std::min(0.001, length / 500.0));
    title.setText(mix.valid ? mix.name : "No audio clip open", juce::dontSendNotification);
    subtitle.setText(mix.valid ? mix.sourceFile.getFileName() + "   \xe2\x80\xa2   " + secondsText(mix.lengthSeconds())
                               : juce::String("Double-click an audio clip in the arrangement"),
                     juce::dontSendNotification);
    styleSwitch(reverse, mix.valid && mix.reversed, juce::Colour(accentColour));
    styleSwitch(mute, mix.valid && mix.muted, juce::Colour(0xff97634c));
    pushValues();
    updateReadouts();
    refreshThumbnail();
    repaint();
}

void AudioClipPanel::pushValues()
{
    const juce::ScopedValueSetter<bool> scope(syncing, true);
    sliders[gainControl].setValue(mix.gainDb, juce::dontSendNotification);
    sliders[panControl].setValue(mix.pan, juce::dontSendNotification);
    sliders[pitchControl].setValue(mix.pitchSemitones, juce::dontSendNotification);
    sliders[fadeInControl].setValue(mix.fadeInSeconds, juce::dontSendNotification);
    sliders[fadeOutControl].setValue(mix.fadeOutSeconds, juce::dontSendNotification);
}

juce::String AudioClipPanel::readout(int control) const
{
    if (!mix.valid) return "-";
    switch (control)
    {
        case gainControl:    return juce::String(mix.gainDb, 1) + " dB";
        case panControl:     return panText(mix.pan);
        case pitchControl:   return juce::String(mix.pitchSemitones, 2) + " st";
        case fadeInControl:  return secondsText(mix.fadeInSeconds);
        case fadeOutControl: return secondsText(mix.fadeOutSeconds);
        default: break;
    }
    return {};
}

juce::String AudioClipPanel::gestureName(int control) const
{
    switch (control)
    {
        case gainControl:    return "Clip gain";
        case panControl:     return "Clip pan";
        case pitchControl:   return "Clip pitch";
        case fadeInControl:  return "Clip fade in";
        case fadeOutControl: return "Clip fade out";
        default: break;
    }
    return "Clip mix";
}

void AudioClipPanel::updateReadouts()
{
    for (int i = 0; i < controlCount; ++i)
        values[static_cast<size_t>(i)].setText(readout(i), juce::dontSendNotification);
}

void AudioClipPanel::apply(int control)
{
    const auto value = sliders[static_cast<size_t>(control)].getValue();
    const auto result = control == gainControl ? session.setAudioClipGainDb(clip, static_cast<float>(value))
        : control == panControl ? session.setAudioClipPan(clip, static_cast<float>(value))
        : control == pitchControl ? session.setAudioClipPitch(clip, static_cast<float>(value))
        : control == fadeInControl ? session.setAudioClipFadeIn(clip, value)
        : session.setAudioClipFadeOut(clip, value);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    // Read back rather than trusting the value that went in: the engine clamps
    // gain and pitch, and a fade long enough to meet the other one shortens it.
    mix = session.audioClipMix(clip);
    if (control == fadeInControl || control == fadeOutControl)
    {
        const juce::ScopedValueSetter<bool> scope(syncing, true);
        sliders[control == fadeInControl ? fadeOutControl : fadeInControl]
            .setValue(control == fadeInControl ? mix.fadeOutSeconds : mix.fadeInSeconds, juce::dontSendNotification);
    }
    updateReadouts();
    repaint(waveArea);
}

void AudioClipPanel::refreshThumbnail()
{
    const auto source = mix.valid ? mix.sourceFile.getFullPathName() : juce::String();
    if (source == thumbnailSource)
        return;
    thumbnailSource = source;
    if (thumbnail != nullptr)
        thumbnail->removeChangeListener(this);
    thumbnail.reset();
    thumbnailReadable = false;
    if (source.isEmpty())
        return;
    thumbnail = std::make_unique<juce::AudioThumbnail>(512, formats, thumbnailCache);
    thumbnail->addChangeListener(this);
    thumbnailReadable = thumbnail->setSource(new juce::FileInputSource(mix.sourceFile));
}

void AudioClipPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Samples arriving on the thumbnail worker only need the waveform redrawn;
    // anything from the session may have changed the clip itself.
    if (thumbnail != nullptr && source == thumbnail.get())
    {
        repaint(waveArea);
        return;
    }
    sync();
}

void AudioClipPanel::editWillChange()
{
    clip = {};
    mix = {};
}

void AudioClipPanel::editDidChange()
{
    sync();
}

void AudioClipPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    auto header = area.removeFromTop(headerHeight);
    subtitle.setBounds(header.removeFromRight(std::min(header.getWidth() / 2, 320)));
    title.setBounds(header);
    area.removeFromTop(4);

    // The controls keep their size and the waveform takes what is left, down to
    // a width worth drawing; below that the controls have the panel to
    // themselves rather than being squeezed beside a stripe.
    const auto wanted = controlCount * controlWidth + switchWidth + 10;
    if (area.getWidth() >= wanted + minimumWaveWidth)
    {
        controlsArea = area.removeFromLeft(wanted);
        area.removeFromLeft(8);
        waveArea = area;
    }
    else
    {
        controlsArea = area;
        waveArea = {};
    }

    auto controls = controlsArea;
    auto switches = controls.removeFromRight(switchWidth).reduced(6, 0);
    const auto switchHeight = std::min(26, std::max(18, (switches.getHeight() - 8) / 2));
    switches = switches.withSizeKeepingCentre(switches.getWidth(), switchHeight * 2 + 6);
    reverse.setBounds(switches.removeFromTop(switchHeight));
    switches.removeFromTop(6);
    mute.setBounds(switches.removeFromTop(switchHeight));

    const auto cellWidth = std::max(1, controls.getWidth() / controlCount);
    for (int i = 0; i < controlCount; ++i)
    {
        const juce::Rectangle<int> cell(controls.getX() + i * cellWidth, controls.getY(), cellWidth, controls.getHeight());
        const auto knobSize = std::min({58, std::max(26, cell.getWidth() - 12), std::max(26, cell.getHeight() - 34)});
        names[static_cast<size_t>(i)].setBounds(cell.getX() + 2, cell.getY(), cell.getWidth() - 4, 16);
        sliders[static_cast<size_t>(i)].setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 2));
        values[static_cast<size_t>(i)].setBounds(cell.getX() + 2, cell.getBottom() - 17, cell.getWidth() - 4, 16);
    }
}

void AudioClipPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1f24));
    g.setColour(juce::Colour(0xff222a30));
    g.fillRect(getLocalBounds().withHeight(headerHeight + 8));
    g.setColour(juce::Colour(0xff2b343b));
    g.drawRect(getLocalBounds(), 1);
    if (!controlsArea.isEmpty() && !waveArea.isEmpty())
    {
        g.setColour(juce::Colour(0xff2b343b));
        g.fillRect(waveArea.getX() - 5, controlsArea.getY(), 1, controlsArea.getHeight());
    }
    if (waveArea.isEmpty())
        return;
    g.setColour(juce::Colour(0xff13181c));
    g.fillRect(waveArea);
    if (!mix.valid)
    {
        paintEmpty(g, waveArea);
        return;
    }
    paintWaveform(g, waveArea);
}

void AudioClipPanel::paintEmpty(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff5c6771));
    g.setFont(uiFont(11.5f));
    drawSnappedText(g, "Double-click an audio clip to edit it here", area, juce::Justification::centred);
}

// The span of the source file this clip plays, drawn end to end, with the two
// fades shaded over it. Reversed clips are drawn mirrored, because the first
// thing heard is what the file ends with.
void AudioClipPanel::paintWaveform(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto inner = area.reduced(4, 6);
    if (thumbnail == nullptr || thumbnail->getTotalLength() <= 0.0)
    {
        g.setColour(juce::Colour(0xff6d7a83));
        g.setFont(uiFont(11.0f));
        drawSnappedText(g, thumbnailReadable ? "Reading waveform..." : "Missing or unreadable audio",
                        inner, juce::Justification::centred);
        return;
    }
    const auto speed = std::max(0.0001, mix.speedRatio);
    const auto sourceStart = mix.offsetSeconds * speed;
    const auto sourceEnd = sourceStart + mix.lengthSeconds() * speed;
    {
        juce::Graphics::ScopedSaveState scope(g);
        if (mix.reversed)
            g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f)
                               .translated(static_cast<float>(inner.getRight() + inner.getX()), 0.0f));
        paintWaveformLanes(g, *thumbnail, inner, sourceStart, sourceEnd, 1.45f,
                           juce::Colour(waveColour).withAlpha(mix.muted ? 0.32f : 1.0f));
    }

    const auto length = mix.lengthSeconds();
    if (length > 0.0)
    {
        const auto left = static_cast<float>(inner.getX());
        const auto right = static_cast<float>(inner.getRight());
        const auto top = static_cast<float>(inner.getY());
        const auto bottom = static_cast<float>(inner.getBottom());
        const auto shade = juce::Colour(0xff13181c).withAlpha(0.72f);
        if (mix.fadeInSeconds > 0.0)
        {
            const auto x = left + static_cast<float>(mix.fadeInSeconds / length) * inner.getWidth();
            juce::Path wedge;
            wedge.startNewSubPath(left, top);
            wedge.lineTo(x, top);
            wedge.lineTo(left, bottom);
            wedge.closeSubPath();
            g.setColour(shade);
            g.fillPath(wedge);
            g.setColour(juce::Colour(accentColour).withAlpha(0.85f));
            g.drawLine(left, bottom, x, top, 1.2f);
        }
        if (mix.fadeOutSeconds > 0.0)
        {
            const auto x = right - static_cast<float>(mix.fadeOutSeconds / length) * inner.getWidth();
            juce::Path wedge;
            wedge.startNewSubPath(right, top);
            wedge.lineTo(x, top);
            wedge.lineTo(right, bottom);
            wedge.closeSubPath();
            g.setColour(shade);
            g.fillPath(wedge);
            g.setColour(juce::Colour(accentColour).withAlpha(0.85f));
            g.drawLine(x, top, right, bottom, 1.2f);
        }
    }

    g.setColour(juce::Colour(0xff2b343b));
    g.drawRect(area, 1);
    if (mix.muted)
    {
        g.setColour(juce::Colour(0xffd9a48c));
        g.setFont(uiFontBold(10.0f));
        drawSnappedText(g, "CLIP MUTED", area.withHeight(18).withTrimmedLeft(8), juce::Justification::centredLeft);
    }
}
}
