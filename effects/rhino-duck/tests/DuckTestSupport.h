#pragma once

#include "../src/DuckProcessor.h"

#include <iostream>
#include <vector>

// What every Rhino Duck test file shares: the checks, a parameter reached by
// id, a play head the tests drive, and a few signals.
namespace rhino::duck::tests
{
extern int failures;

void require(bool condition, const char* message);
void requireClose(float actual, float expected, float tolerance, const char* message);

void setValue(Processor&, const char* id, float plainValue);
float value(const Processor&, const char* id);

// A host clock the tests set by hand.
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};

constexpr double testRate = 48000.0;

std::vector<float> constant(int frames, float level = 1.0f);
std::vector<float> sine(int frames, double hz, float amplitude = 0.5f);
// A synthetic kick: a 55 Hz sine with a half-millisecond attack and an 80 ms
// decay, at `at`, on top of whatever `into` already holds.
void addKick(std::vector<float>& into, int at, float amplitude = 0.9f);

float rmsOf(const std::vector<float>& signal, int from, int to);
float meanOf(const std::vector<float>& signal, int from, int to);
float decibels(float ratio);
}
