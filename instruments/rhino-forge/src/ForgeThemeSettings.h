#pragma once

#include "../ui/ForgeTheme.h"

// The theme as a setting of the machine.
//
// It is read from the settings file beside the MIDI map (Processor::settingsFile
// names it, and the favourites share it), written there by the panel's menu,
// and applied once per process before the first panel is built, because every
// control copies the colour tokens as it is constructed. The Editor reads it
// as its first act. It is the panel's own setting - the Processor neither
// draws nor needs to know a theme exists - which is why these are free
// functions rather than Processor methods, and why ForgeProcessor.h includes
// nothing under ui/. Defined in ForgeProcessorSettings.cpp, beside the
// favourites, because the two read and write the same file.
namespace rhino::forge::themeSettings
{
// What the settings file says, or Forge for a file that is missing, unreadable,
// not ours or silent on the matter.
ui::ThemeId read();
// Read, change, write: a setting a later build adds survives this one saving
// the theme.
void save(ui::ThemeId);
// Reads the chosen theme the first time it is called in the process and
// applies it; every later call answers what is applied. A theme something
// already selected on purpose - the snapshot tool, a test - outranks the file,
// which is what lets one binary render either look.
ui::ThemeId loadOnce();
void forgetLoadedForTests();
}
