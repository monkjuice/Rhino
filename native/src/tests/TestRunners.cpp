#include "tests/TestRunners.h"
#include "model/Session.h"
#include "app/AppPlatform.h"
#include "tests/Pattern/DeviceRackTest.h"

namespace rhino
{

int runStressProfile();

static void prepareCommandLineAudio()
{
   #if JUCE_WINDOWS
    te::Engine testEngine {"Rhino Native Tests"};
    avoidLegacyDirectSound(testEngine);
    testEngine.getDeviceManager().deviceManager.closeAudioDevice();
   #endif
}

std::optional<int> runTestFlag(const juce::String& args)
{
    // A component snapshot has no audio path, so it stays available while
    // another Rhino instance owns the Windows audio backend. The workflow
    // runs below still prepare audio for their render scenarios.
    if (args == "--arp-snapshot")
    {
        Session::setCommandLineTestMode(true);
        return runArpSnapshotTest();
    }
    // Renders every device offline, so it opens no audio device either.
    if (args == "--device-test")
    {
        Session::setCommandLineTestMode(true);
        return runDeviceConformance();
    }
    // Timings, not checks, and no audio device: it measures the message
    // thread, and leaves the audio backend to any Rhino already running.
    if (args == "--profile-ui")
    {
        Session::setCommandLineTestMode(true);
        return runUiProfile();
    }
    // The same kind of timing on a 120-track, seven-minute song
    // (tests/StressProfile.cpp): no audio device, no checks.
    if (args == "--profile-stress")
    {
        Session::setCommandLineTestMode(true);
        return runStressProfile();
    }
    if (args == "--self-test" || args == "--pattern-test" || args == "--arrangement-test"
        || args == "--arrangement-geometry-test")
    {
        Session::setCommandLineTestMode(true);
        if (args != "--arrangement-geometry-test") prepareCommandLineAudio();
        return args == "--self-test" ? runSelfTest()
             : args == "--pattern-test" ? runPatternTest()
             : args == "--arrangement-test" ? runArrangementTest() : runArrangementGeometryTest();
    }
    return std::nullopt;
}

}
