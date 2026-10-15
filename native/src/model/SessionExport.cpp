#include "SessionInternal.h"

// Export WAV: the song from the top to the end of its last clip, through the
// main chain, at unity, as a 24-bit dithered WAV at the audio device's own
// rate and block size, so the file is what was heard.
//
// It renders the live edit on the message thread a few blocks at a time, as
// Bounce to New Track does (SessionBounce.cpp), so the shell's loading screen
// can draw and cancel between steps while nothing can edit the document. It
// used to render on a worker while the message thread went on reading the
// same edit, with only the window disabled.

namespace rhino
{
namespace
{
// As the bounce's: how long a render that can never finish keeps the loading
// screen up before it is given up. Cancel works meanwhile.
constexpr double exportStallSeconds = 15.0;

// Export WAV's settings, read by the stepped export and by the blocking render
// alike, so the two cannot drift. `wav` must outlive the task.
te::Renderer::Parameters exportParameters(te::Edit& song, const juce::File& destination, double seconds,
                                          double sampleRate, int blockSize, juce::AudioFormat& wav)
{
    te::Renderer::Parameters parameters(song);
    parameters.destFile = destination;
    parameters.audioFormat = &wav;
    parameters.sampleRateForAudio = sampleRate;
    parameters.bitDepth = 24;
    parameters.blockSizeForAudio = blockSize;
    // The main track is the edit's master chain, so its devices and its
    // fader only reach the file when master plugins are rendered. Export
    // at unity otherwise: normalising would re-level the mix against what
    // the main meter showed.
    parameters.useMasterPlugins = true;
    parameters.ditheringEnabled = true;
    parameters.time = {{}, tracktion::core::TimePosition::fromSeconds(seconds)};
    return parameters;
}
}

struct Session::ExportRun
{
    explicit ExportRun(const juce::File& file) : temporary(file) {}
    // Written beside the destination and swapped in only once the render has
    // finished, so a failed or cancelled export leaves what was there.
    juce::TemporaryFile temporary;
    double seconds = 0.0, sampleRate = 48000.0;
    int blockSize = 512;
    // Off the device for the whole export, and the last thing to go.
    std::unique_ptr<te::Edit::ScopedRenderStatus> renderStatus;
    std::atomic<float> progress {0.0f};
    juce::WavAudioFormat wav;
    // Made by the first step, so the screen is drawn before the render is
    // built. Last, so it goes before what it relies on.
    std::unique_ptr<te::Renderer::RenderTask> task;
    std::shared_ptr<bool> stalled;
};

void Session::ExportRunDeleter::operator()(ExportRun* run) const
{
    delete run;
}

// Only the timeline: a render plays the clips in the cells through the
// switching node's own launchers, which it never waits for.
juce::Result Session::checkTimelineFiles()
{
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
            if (const auto readable = checkClipFile(*clip); readable.failed())
                return readable;
    return juce::Result::ok();
}

juce::Result Session::renderTimelineWav(te::Edit& song, const juce::File& destination, double seconds, double sampleRate,
                                        int blockSize, double stallSeconds)
{
    // Where the render has got to, as a fraction of the song. Declared before
    // the task, which updates it until it goes.
    std::atomic<float> progress {0.0f};
    juce::WavAudioFormat wav;
    te::Renderer::RenderTask task("Export WAV", exportParameters(song, destination, seconds, sampleRate, blockSize, wav),
                                  &progress, nullptr);
    return runRenderTask(task, progress, stallSeconds);
}

juce::Result Session::startExport(const juce::File& destination)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (isExporting() || isBouncing())
        return juce::Result::fail("Wait for the render that is running to finish.");
    const auto seconds = edit->getLength().inSeconds();
    if (seconds <= 0.0)
        return juce::Result::fail("There is no arrangement to export.");
    // A render waits for every clip's file to open, so one whose file is gone
    // would hold the export until the stall gave up on it. Said, naming the
    // file, before anything starts.
    if (const auto readable = checkTimelineFiles(); readable.failed())
        return juce::Result::fail("Could not export WAV: " + readable.getErrorMessage());

    std::unique_ptr<ExportRun, ExportRunDeleter> run(new ExportRun(destination));
    run->seconds = seconds;
    // At the device's own rate and block size. Anything else asks every
    // plugin to run at settings it was never auditioned at, and the file stops
    // being the thing that was heard.
    auto& devices = engine.getDeviceManager();
    run->sampleRate = devices.getSampleRate() > 7000.0 ? devices.getSampleRate() : 48000.0;
    run->blockSize = devices.getBlockSize() > 0 ? devices.getBlockSize() : 512;
    stop();
    // The render drives the very plugin instances the device callback is
    // holding. Taking the edit off the device keeps the export off the
    // speakers and stops the live graph re-entering plugins the render has
    // re-prepared; it goes back as the run goes.
    run->renderStatus = std::make_unique<te::Edit::ScopedRenderStatus>(*edit, true);
    // The render reads the lanes from the engine's curves, as playback does,
    // so they have to be current now rather than at the next change.
    mirrorAutomationToEngine();
    te::Renderer::turnOffAllPlugins(*edit);
    exportRun = std::move(run);
    return juce::Result::ok();
}

bool Session::continueExport(double milliseconds)
{
    if (!isExporting())
        return false;
    auto& run = *exportRun;
    if (run.task == nullptr)
    {
        run.task = std::make_unique<te::Renderer::RenderTask>(
            "Export WAV",
            exportParameters(*edit, run.temporary.getFile(), run.seconds, run.sampleRate, run.blockSize, run.wav),
            &run.progress, nullptr);
        run.stalled = stopRenderWhenStalled(*run.task, run.progress, exportStallSeconds);
    }
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + milliseconds;
    do
    {
        if (run.task->runJob() == juce::ThreadPoolJob::jobHasFinished)
        {
            finishExport();
            return false;
        }
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return true;
}

float Session::exportProgress() const
{
    return isExporting() ? exportRun->progress.load() : 0.0f;
}

juce::Result Session::exportOutcome() const
{
    return isExporting() ? juce::Result::fail("The export is still running.") : lastExport;
}

void Session::cancelExport()
{
    if (!isExporting())
        return;
    endExport(juce::Result::fail("The export was cancelled."));
}

void Session::finishExport()
{
    const auto rendered = renderTaskOutcome(*exportRun->task, *exportRun->stalled, exportStallSeconds);
    // The task first, so the file it wrote is closed before it is moved.
    exportRun->task.reset();
    if (rendered.failed())
        endExport(juce::Result::fail("Could not export WAV: " + rendered.getErrorMessage()));
    else if (!exportRun->temporary.overwriteTargetFileWithTemporary())
        endExport(juce::Result::fail("Could not export WAV: Rhino could not write "
                                     + exportRun->temporary.getTargetFile().getFileName() + "."));
    else
        endExport(juce::Result::ok());
}

void Session::endExport(const juce::Result& outcome)
{
    auto run = std::move(exportRun);
    // A render still in flight goes before what it holds in place; whatever
    // it wrote goes with the temporary file.
    run->task.reset();
    // No plugin left prepared for the render's settings when the device
    // comes back, which it does as the run goes.
    te::Renderer::turnOffAllPlugins(*edit);
    run.reset();
    lastExport = outcome;
}

}
