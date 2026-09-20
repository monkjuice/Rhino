#pragma once

#include "ForgeControls.h"
#include "ForgeEnvelopeVisuals.h"
#include "ForgeFilterVisuals.h"
#include "ForgeFxVisuals.h"
#include "ForgeLookAndFeel.h"

// The panel's look, in one include for whatever needs all of it — which is the
// editor, and almost nothing else. Each piece is its own header and says at the
// top what it is for:
//
//   ForgeStyle.h             colours, and the geometry a control's look needs
//   ForgeLookAndFeel.h       the knob, slider and menu, as JUCE asks for them
//   ForgeDisplays.h          the picture tube and the traces drawn on it
//   ForgeControls.h          the components a module is built out of
//   ForgeFxVisuals.h         an effect's colour, mark, plate, shelf and row
//   ForgeEnvelopeVisuals.h   the envelope and LFO displays
//   ForgeFilterVisuals.h     the filter response
//
// Layout lives next door in ForgeLayout.h; nothing here decides where anything
// goes. The material everything is drawn out of -- plates, bevels, grain,
// fasteners -- lives in ForgeChrome.h, so a module and a decal are made of the
// same metal.
