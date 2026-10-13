#include "MainWindow.h"
#include "MainComponent.h"
#include "AppPlatform.h"
#include "StartupScreen.h"
#include "ui/shared/ComponentSnapshot.h"
#include "tests/TestRunners.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace rhino
{

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Rhino"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override
    {
        const auto logFile = rhinoLogFile();
        logFile.getParentDirectory().createDirectory();
        logger = std::make_unique<juce::FileLogger>(logFile, "Rhino debug log", 512 * 1024);
        juce::Logger::setCurrentLogger(logger.get());
        juce::Logger::writeToLog("Rhino: log started at " + logFile.getFullPathName());
        // The test runners and the profiler are this binary with a flag; each
        // decides for itself whether it opens an audio device.
        if (const auto code = runTestFlag(args))
        {
            setApplicationReturnValue(*code);
            quit();
            return;
        }
        startupTest = args == "--startup-test";
        if (!startupTest)
            registerRhinoProjectFileAssociation();
        const auto requestedProject = juce::File(args.trim().unquoted());
        if (requestedProject.existsAsFile() && requestedProject.hasFileExtension("rhinoedit"))
            projectToOpen = requestedProject;
        juce::LookAndFeel::setDefaultLookAndFeel(&theme);
        window = std::make_unique<MainWindow>();
        loading = new StartupScreen();
        loading->setSize(560, 320);
        window->setContentOwned(loading.getComponent(), true);
        window->centreWithSize(560, 320);
        window->setVisible(!startupTest);

        // RHINO_RENDERER=software selects JUCE's rasteriser. It keeps one
        // persistent surface, so a region nobody repainted still holds the last
        // correct frame; the Direct2D backend presents from rotating buffers
        // where that same region holds an older frame instead.
        if (auto* peer = window->getPeer())
        {
            const auto engines = peer->getAvailableRenderingEngines();
            const auto requested = juce::SystemStats::getEnvironmentVariable("RHINO_RENDERER", {}).trim();
            if (requested.isNotEmpty())
            {
                auto match = -1;
                for (int i = 0; i < engines.size(); ++i)
                    if (engines[i].containsIgnoreCase(requested)) { match = i; break; }
                if (match >= 0) peer->setCurrentRenderingEngine(match);
            }
            juce::Logger::writeToLog("Rhino: rendering engine " + engines[peer->getCurrentRenderingEngine()]);
        }
        const auto screenshot = juce::SystemStats::getEnvironmentVariable("RHINO_STARTUP_SNAPSHOT", {});
        if (startupTest && screenshot.isNotEmpty())
            writeSnapshot(*loading, juce::File(screenshot));
        // Let the window become visible before constructing the engine. Engine
        // initialization requires the message thread; yield between its phases.
        startTimer(40);
    }
    void shutdown() override
    {
        stopTimer();
        window.reset();
        session.reset();
        juce::Logger::setCurrentLogger(nullptr);
        logger.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    void systemRequestedQuit() override
    {
        if (window)
            if (auto* controls = dynamic_cast<MainComponent*>(window->getContentComponent()))
            {
                controls->requestClose();
                return;
            }
        stopTimer();
        quit();
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (!window || !loading) return;
        try
        {
            if (window->getContentComponent() != loading.getComponent())
                throw std::runtime_error("Startup screen was replaced before initialization finished.");
            if (auto* peer = window->getPeer()) peer->performAnyPendingRepaintsNow();
            if (startupStage == 0)
                session = std::make_unique<Session>();
            else if (startupStage == 1)
            {
                avoidLegacyDirectSound(session->engine);
                // Two inputs as well as two outputs: a device opened with no
                // input channels leaves the engine with no wave input device,
                // and recording has nothing to arm.
                session->engine.getDeviceManager().initialise(2, 2);
                avoidLegacyDirectSound(session->engine);
            }
            else
            {
                window->setContentOwned(new MainComponent(*session), true);
                window->setResizeLimits(960, 680, 2400, 1600);
                window->centreWithSize(1120, 760);
                if (auto* controls = dynamic_cast<MainComponent*>(window->getContentComponent()))
                {
                    const juce::Component::SafePointer<MainComponent> safeControls(controls);
                    const juce::Component::SafePointer<MainWindow> safeWindow(window.get());
                    window->fileRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showFileMenuFrom(target);
                    };
                    window->editRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showEditMenuFrom(target);
                    };
                    window->viewRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showViewMenuFrom(target);
                    };
                    window->helpRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showHelpMenuFrom(target);
                    };
                    controls->fullScreenToggleRequested = [safeWindow]
                    {
                        if (safeWindow != nullptr) safeWindow->setAppFullScreen(!safeWindow->isAppFullScreen());
                    };
                    controls->fullScreenActive = [safeWindow]
                    {
                        return safeWindow != nullptr && safeWindow->isAppFullScreen();
                    };
                    controls->projectTitleChanged = [safeWindow](const juce::String& title)
                    {
                        if (safeWindow != nullptr) safeWindow->setProjectTitle(title);
                    };
                    window->setProjectTitle("Untitled");
                    controls->listenForKeysOn(*window);
                    // Give the content the focus it never takes for itself, so
                    // the shortcuts answer from the first frame.
                    controls->grabKeyboardFocus();
                }
                if (projectToOpen != juce::File{})
                    if (auto* controls = dynamic_cast<MainComponent*>(window->getContentComponent()))
                        controls->openProjectFile(projectToOpen);
                // The shell is the one panel no test runner builds, so this is the
                // way to look at the control bar without taking the display away
                // from whoever is using the machine - RHINO_SHELL_SNAPSHOT under
                // --startup-test writes the window to a PNG and quits.
                // RHINO_SHELL_SNAPSHOT_SIZE=960x680 renders it at a size of
                // your choosing first, which is how the smallest supported
                // window gets looked at without resizing the real one.
                if (const auto shellShot = juce::SystemStats::getEnvironmentVariable("RHINO_SHELL_SNAPSHOT", {});
                    startupTest && shellShot.isNotEmpty())
                    if (auto* content = window->getContentComponent())
                    {
                        const auto size = juce::SystemStats::getEnvironmentVariable("RHINO_SHELL_SNAPSHOT_SIZE", {});
                        if (const auto cross = size.indexOfChar('x'); cross > 0)
                            content->setSize(std::max(200, size.substring(0, cross).getIntValue()),
                                             std::max(200, size.substring(cross + 1).getIntValue()));
                        writeSnapshot(*content, juce::File(shellShot));
                    }
                if (startupTest) quit();
                return;
            }
            loading->setStage(++startupStage);
            startTimer(1);
        }
        catch (const std::exception& error)
        {
            setApplicationReturnValue(1);
            if (loading) loading->showError(error.what());
            std::fprintf(stderr, "Rhino startup failed: %s\n", error.what());
            if (startupTest) quit();
        }
    }

    Theme theme;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
    juce::Component::SafePointer<StartupScreen> loading;
    int startupStage = 0;
    bool startupTest = false;
    juce::File projectToOpen;
};

}
START_JUCE_APPLICATION(rhino::Application)
