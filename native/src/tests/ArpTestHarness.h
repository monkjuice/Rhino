#pragma once
#include "midi/RhinoArpDevice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace rhino::arp_test
{
constexpr double sampleRate = 48000.0;

inline void require(bool valid, const juce::String& message)
{
    if (!valid)
        throw std::runtime_error(message.toStdString());
}

inline int frames(double seconds) { return juce::roundToInt(seconds * sampleRate); }

struct Input
{
    int frame;
    juce::MidiMessage message;
};

inline Input on(double seconds, int pitch = 60, int channel = 1)
{
    return { frames(seconds), juce::MidiMessage::noteOn(channel, pitch, 0.8f) };
}

inline Input off(double seconds, int pitch = 60, int channel = 1)
{
    return { frames(seconds), juce::MidiMessage::noteOff(channel, pitch) };
}

struct Output
{
    double seconds;
    juce::MidiMessage message;
    bool allNotesOff = false;
};

inline void requirePitches(const std::vector<Output>& notes, const std::vector<int>& pitches)
{
    require(notes.size() == pitches.size(), "expected " + juce::String(static_cast<int>(pitches.size()))
        + " onsets, received " + juce::String(static_cast<int>(notes.size())));
    for (size_t i = 0; i < pitches.size(); ++i)
        require(notes[i].message.getNoteNumber() == pitches[i],
                "onset " + juce::String(static_cast<int>(i)) + " has pitch "
                    + juce::String(notes[i].message.getNoteNumber()) + "; expected " + juce::String(pitches[i]));
}

// Drive the actual plugin with a moving edit clock, or a frozen playhead and
// advancing audio blocks. Output times always use elapsed audio time so the
// same assertions cover live playing while the transport is stopped.
struct Harness
{
    explicit Harness(te::Edit& edit)
        : plugin(edit.getPluginCache().createNewPlugin(RhinoArpDevice::xmlTypeName, {})),
          arp(dynamic_cast<RhinoArpDevice*>(plugin.get()))
    {
        require(arp != nullptr, "could not construct Rhino Arp");
        arp->initialise({{}, sampleRate, 24000});
    }

    ~Harness() { arp->deinitialise(); }

    void set(const char* id, float value)
    {
        auto parameter = arp->getAutomatableParameterByID(id);
        require(parameter != nullptr, "missing parameter " + juce::String(id));
        parameter->setParameter(value, juce::dontSendNotification);
    }

    void plain()
    {
        set("style", 0.0f);
        set("rate", 1.0f);
        set("gate", 200.0f);
        set("distance", 0.0f);
        set("steps", 0.0f);
        set("groove", 0.0f);
        set("hold", 0.0f);
        set("retrigger", 0.0f);
        set("repeats", 0.0f);
        set("root", 0.0f);
        set("scale", 0.0f);
    }

    void render(double seconds, std::vector<Input> input = {}, bool playing = true, int blockSize = 256)
    {
        std::stable_sort(input.begin(), input.end(), [] (const Input& a, const Input& b)
        { return a.frame < b.frame; });
        const auto total = frames(seconds);
        size_t next = 0;
        te::MidiMessageArray midi;
        for (int done = 0; done < total;)
        {
            const auto count = std::min(blockSize, total - done);
            midi.clear();
            midi.isAllNotesOff = allNotesOffNextBlock;
            allNotesOffNextBlock = false;
            while (next < input.size() && input[next].frame < done + count)
            {
                midi.addMidiMessage(input[next].message, (input[next].frame - done) / sampleRate, {});
                ++next;
            }
            const auto start = playing ? elapsed / sampleRate + editOffset : 2.0;
            const auto end = playing ? start + count / sampleRate : start;
            const tracktion::core::TimeRange time {
                tracktion::core::TimePosition::fromSeconds(start),
                tracktion::core::TimePosition::fromSeconds(end) };
            te::PluginRenderContext context(nullptr, 0, count, &midi, 0.0, time,
                                             playing, false, playing, false);
            arp->applyToBuffer(context);
            if (midi.isAllNotesOff)
                output.push_back({ elapsed / sampleRate, {}, true });
            for (const auto& message : midi)
            {
                require(message.getTimeStamp() >= -1.0e-8
                            && message.getTimeStamp() <= count / sampleRate + 1.0e-8,
                        "output timestamp is outside its audio block");
                output.push_back({ elapsed / sampleRate + message.getTimeStamp(), message, false });
            }
            elapsed += count;
            done += count;
        }
    }

    std::vector<Output> onsets(double from = 0.0, int channel = 0) const
    {
        std::vector<Output> result;
        for (const auto& event : output)
            if (!event.allNotesOff && event.message.isNoteOn() && event.seconds >= from - 1.0e-8
                && (channel == 0 || event.message.getChannel() == channel))
                result.push_back(event);
        return result;
    }

    void requireBalanced() const
    {
        std::array<int, 16 * 128> sounding {};
        for (const auto& event : output)
        {
            if (event.allNotesOff)
            {
                sounding.fill(0);
                continue;
            }
            const auto& message = event.message;
            const auto channel = message.getChannel();
            if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                for (int pitch = 0; pitch < 128; ++pitch)
                    sounding[static_cast<size_t>((channel - 1) * 128 + pitch)] = 0;
                continue;
            }
            if (!message.isNoteOnOrOff())
                continue;
            auto& count = sounding[static_cast<size_t>((channel - 1) * 128 + message.getNoteNumber())];
            const auto where = " at " + juce::String(event.seconds, 6) + " s, channel "
                + juce::String(channel) + ", pitch " + juce::String(message.getNoteNumber());
            if (message.isNoteOn())
            {
                require(count == 0, "same pitch retriggered before its previous note-off" + where);
                ++count;
            }
            else
            {
                require(count == 1, "note-off has no sounding note" + where);
                --count;
            }
        }
        require(std::all_of(sounding.begin(), sounding.end(), [] (int count) { return count == 0; }),
                "generated notes remain sounding after input release");
    }

    te::Plugin::Ptr plugin;
    RhinoArpDevice* arp;
    std::vector<Output> output;
    std::int64_t elapsed = 0;
    bool allNotesOffNextBlock = false;
    double editOffset = 0.0;
};
}
