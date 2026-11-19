#pragma once
#include "DeviceEditorPanel.h"
#include "ui/rack/MultibandDisplayGeometry.h"
#include "audio/MultibandDynamicsDevice.h"

// Shared by Multiband Dynamics' translation units of DeviceEditorPanel, and
// nothing else: DeviceEditorPanelMultiband.cpp (where everything stands, the
// drawing and the meters) and DeviceEditorPanelMultibandGestures.cpp (what a
// point is, presses, drags and tooltips).
namespace rhino::multibandface
{
using Device = MultibandDynamicsDevice;
using Control = MultibandDynamicsDevice::BandControl;
namespace md = multibanddisplay;

constexpr int titleHeight = 12;
constexpr int footerHeight = 16;
constexpr int lineHeight = 15;
constexpr int bandKnobSize = 24;
// How far the pointer travels to take a block from 1:1 to either end, and a
// readout across its whole range; Shift takes a quarter of either, or of an
// edge's decibels.
constexpr float volumePixels = 100.0f;
constexpr float readoutPixels = 150.0f;
constexpr float fineFactor = 0.25f;

constexpr const char* bandNames[3] { "Low", "Mid", "High" };

inline int parameterOf(int band, Control control)
{
    return Device::bandParameter(band, control);
}

inline int thresholdOf(int band, bool above)
{
    return parameterOf(band, above ? Control::aboveThreshold : Control::belowThreshold);
}

inline int ratioOf(int band, bool above)
{
    return parameterOf(band, above ? Control::aboveRatio : Control::belowRatio);
}

inline bool isAboveRatio(int parameter)
{
    for (int b = 0; b < 3; ++b)
        if (parameter == ratioOf(b, true))
            return true;
    return false;
}

inline juce::NormalisableRange<float> rangeOf(const Session::DeviceParameter& parameter)
{
    return { parameter.minimum, parameter.maximum, parameter.interval, static_cast<float>(parameter.skew) };
}
}
