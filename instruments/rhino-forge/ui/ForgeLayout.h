#pragma once

#include "ForgeControlBlock.h"

// What modules exist, what each contains, and where it sits. Pure geometry and
// declaration: nothing here is a component and nothing here draws. Split into
// four, of which this includes the last and each includes the one before it:
//
//   ForgeModule.h        what a Control, a Row and a Module are, and their sizes
//   ForgeModules.h       every module Forge has, declared once
//   ForgePlacement.h     what size the window may be, and where a module lands
//   ForgeControlBlock.h  where one control lands inside its row
//
// Include the narrowest one that answers the question. The editor needs all of
// it and includes this.
