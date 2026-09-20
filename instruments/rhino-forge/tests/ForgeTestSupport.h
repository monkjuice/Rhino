#pragma once

#include "../src/ForgeProcessor.h"

#include <iostream>
#include <vector>

// What every Forge test file has in common: the checks themselves, reaching a
// parameter by id, rendering a note, and measuring what came back. The bodies
// are in ForgeTestSupport.cpp, so a file that only uses them recompiles when a
// check changes rather than when a measurement does.
//
// A suite lives in its own translation unit and is named in ForgeSuites.h. The
// runner in ForgeTestMain.cpp maps each one to the argument that selects it, so
// a milestone runs the lot and an afternoon on one module runs one file.
namespace rhino::forge::tests
{
// Raised by every failed check. The runner reports it and sets the exit code.
extern int failures;

void require(bool condition, const char* message);
void requireClose(float actual, float expected, float tolerance, const char* message);
void requireText(const juce::String& actual, const juce::String& expected, const char* message);

// Reaching a parameter by the id the layout names it with, in plain units.
void setValue(Processor& processor, const char* id, float plainValue);
float value(const Processor& processor, const char* id);
juce::String textFor(const Processor& processor, const char* id, float plainValue);

// Render a held note into a caller-owned buffer, for checks that need to look
// at the waveform rather than only its peak.
// Source indices, taken from the enum rather than written out: they are what a
// slot's Source parameter stores, and they moved when LFO 2-6 were inserted
// into the middle of the list. Reading them from the one declaration is what
// stops these checks quietly testing the wrong source after the next such move.
constexpr int srcOff = static_cast<int>(rhino::forge::ModSource::off);
constexpr int srcEnv1 = static_cast<int>(rhino::forge::ModSource::env1);
constexpr int srcLfo1 = static_cast<int>(rhino::forge::ModSource::lfo1);
constexpr int srcVelocity = static_cast<int>(rhino::forge::ModSource::velocity);
constexpr int srcNote = static_cast<int>(rhino::forge::ModSource::note);

// Render a held note and report the loudest sample. Used to prove a source is
// silent rather than merely quiet.
float peakForNote(Processor& processor, int samples = 4096);

// Render a held note into a caller-owned buffer, for checks that need to look
// at the waveform rather than only its peak.
void renderNote(Processor& processor, juce::AudioBuffer<float>& buffer, int note = 57);

// Counted on the settled part of the render, past the envelope attack.
int zeroCrossings(const juce::AudioBuffer<float>& buffer, int channel, int from);

// How far the level swings over the course of a render, in dB, measured on the
// short-term RMS rather than on single samples. A detuned stack is supposed to
// move - that movement is the chorus - but it is supposed to move continuously.
// A stack whose members are evenly spaced instead swings in and out of phase
// all together on one slow period, and that reads as a throb rather than as
// chorus. The depth of the swing is what tells the two apart.
float envelopeDepthDb(const juce::AudioBuffer<float>& buffer, int channel, int from,
                      int window = 2048);

float rms(const juce::AudioBuffer<float>& buffer, int channel, int from);

// High-frequency content relative to overall level. Amplitude-independent, so
// it measures how open a filter is without being fooled by a quieter note.
float brightness(const juce::AudioBuffer<float>& buffer, int channel, int from);

bool allSamplesFinite(const juce::AudioBuffer<float>& buffer);

// Two renders, sample for sample. A check that something changed nothing at all
// is stronger than a check that it changed almost nothing.
bool identical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b);

// A single sine on oscillator A and nothing else: the patch most of the engine
// checks measure against, because everything it leaves out is one fewer thing
// that could account for a difference.
void soloSineOnA(Processor& processor);

// Oscillator A through a filter closed far enough that opening it is obvious.
void closedFilterOnA(Processor& processor);

// Destination indices, matching rhino::forge::destinations().
enum Destination { destOff = 0, destAPitch = 5, destSub = 11, destCutoff = 13 };

// Point one modulation slot at one destination.
void setSlot(Processor& processor, int slot, float source, float destination, float depth);
}
