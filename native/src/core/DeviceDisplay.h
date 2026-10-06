#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace rhino
{
// What a device shows on its generated face beside its controls
// (NativeDevice::describe). The device fills it in from its controls as they
// stand and the face only draws it, so a picture cannot disagree with the
// sound: whatever a trace shows, the device's own DSP worked out.
//
// Plain data with no engine in it, so a device, Session and the face can all
// hold one without depending on each other.
struct DeviceDisplay
{
    // A stretch of what the device puts out, drawn as a line and named in a
    // corner. The first trace is drawn in the device's colour and the rest
    // fainter behind it. The face scales every trace by the same amount, so
    // the loudest fills the display and the others keep their size against it.
    struct Trace
    {
        juce::String name;
        std::vector<float> points;
    };

    // A diagram of blocks wired together. A link runs from the block that
    // feeds to the block it feeds, and a link from a block to itself is drawn
    // as a loop. An output block is heard directly and is wired to the bottom
    // of the diagram. Clicking a block opens the section it names.
    struct Block
    {
        juce::String label;     // a character or two
        juce::String section;
        juce::String role;      // what it does, in a few words: "carrier"
        float level = 1.0f;     // 0 to 1, drawn as a bar along the block
        bool output = false;
    };
    struct Link
    {
        int from = 0;
        int to = 0;
    };

    std::vector<Trace> traces;
    std::vector<Block> blocks;
    std::vector<Link> links;

    bool empty() const { return traces.empty() && blocks.empty(); }
};
}
