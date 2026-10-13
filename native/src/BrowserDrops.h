#pragma once
#include "BrowserIds.h"
#include <optional>

namespace rhino
{
// A browser item that joins a track's chain rather than making a clip - an
// instrument, an audio or MIDI effect, a kit, a drum sound, a device preset,
// or a sound file onto a track running a Drum Rack, which takes it onto a
// pad - handed to the model, each device held to the kind its drag says it
// is. Nothing when the item makes a clip, or is not a browser item: the
// target places those itself.
//
// The console's clips grid is the one target that lands its drops here. The
// arrangement still routes the same kinds itself, in
// Arrangement::applyBrowserDrop (ArrangementDrops.cpp), with refusals and
// words of its own; a rule changed here does not reach a lane until that
// routing moves onto this.
std::optional<juce::Result> applyChainDrop(Session&, const juce::String& description, int track);
}
