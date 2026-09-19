#include "../../Session.h"
// The scenarios drive devices directly, so this runner needs the definitions
// rather than their catalog entries.
#include "audio/UtilityDevice.h"
#include "audio/RhinoBloomDevice.h"
#include "audio/RhinoSpaceDevice.h"
#include "instruments/DrumDevice.h"
#include "instruments/RhinoWaveDevice.h"
#include "midi/RhinoArpDevice.h"
#include "DeviceRackTest.h"
#include <stdexcept>

namespace rhino
{
int runPatternTest()
{
    try
    {
        const auto require = [](bool valid, const char* message)
        {
            if (!valid) throw std::runtime_error(message);
        };
        runPatternDeviceRackTest();
        Session session;

       #include "scenarios/DeviceParameters.inc"
       #include "scenarios/EditingAndAutomation.inc"
       #include "scenarios/Rendering.inc"
       #include "scenarios/Persistence.inc"
       #include "scenarios/NewProject.inc"
        return 0;
    }
    catch (const std::exception& error)
    {
        // A GUI executable's stderr is still captured when launched by CTest.
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
