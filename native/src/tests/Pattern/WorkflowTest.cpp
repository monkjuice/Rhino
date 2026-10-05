#include "../../Session.h"
// The scenarios drive devices directly, so this runner needs the definitions
// rather than their catalog entries.
#include "audio/UtilityDevice.h"
#include "audio/RhinoBloomDevice.h"
#include "audio/RhinoSpaceDevice.h"
#include "instruments/DrumDevice.h"
#include "midi/RhinoArpDevice.h"
#include "DeviceRackTest.h"
#include "../../StepGrid.h"
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
        const auto scenario = [](const char* name)
        {
            std::fprintf(stderr, "Pattern scenario: %s\n", name);
            std::fflush(stderr);
        };
        scenario("device rack");
        runPatternDeviceRackTest();
        scenario("no track is first");
       #include "scenarios/NoFirstTrack.inc"
        Session session;

        scenario("device parameters");
       #include "scenarios/DeviceParameters.inc"
        scenario("editing and automation");
       #include "scenarios/EditingAndAutomation.inc"
        scenario("rendering");
       #include "scenarios/Rendering.inc"
        scenario("persistence");
       #include "scenarios/Persistence.inc"
        scenario("new project");
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
