#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{

// Renders a component to a PNG without a desktop peer, so a panel can be
// looked at without bringing the app to the front and taking the screen away
// from whoever is using the machine.
//
// The file is deleted first, and that is not a tidiness measure: JUCE's
// File::createOutputStream opens an existing file for *append*, so writing a
// second PNG to the same path leaves the first one intact at the front and
// every reader sees the original image. That cost an hour of chasing a text
// run that was being drawn correctly the whole time.
inline void writeSnapshot(juce::Component& component, const juce::File& destination)
{
    destination.deleteFile();
    if (auto stream = destination.createOutputStream())
        juce::PNGImageFormat().writeImageToStream(
            component.createComponentSnapshot(component.getLocalBounds()), *stream);
}

}
