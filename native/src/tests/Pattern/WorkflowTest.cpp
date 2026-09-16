#include "../../Session.h"
#include "DeviceRackTest.h"
#include <stdexcept>

namespace theta
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
