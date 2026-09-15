#include "ProjectFiles.h"

namespace theta
{
void ProjectFiles::save(bool saveAs, std::function<void(bool)> completion)
{
    if (busy)
    {
        report("Please wait for the current file operation.");
        if (completion) completion(false);
        return;
    }
    if (!saveAs && session.projectFile != juce::File{})
    {
        write(session.projectFile, std::move(completion));
        return;
    }
    busy = true;
    const auto suggested = session.projectFile == juce::File{}
        ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.thetaedit")
        : session.projectFile.withFileExtension("thetaedit");
    chooser = std::make_unique<juce::FileChooser>("Save project", suggested, "*.thetaedit");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [weak = juce::WeakReference<ProjectFiles>(this), completion](const juce::FileChooser& selected)
        {
            if (!weak) return;
            weak->busy = false;
            const auto file = selected.getResult();
            if (file == juce::File{}) { if (completion) completion(false); return; }
            // Preserve the chosen filename so the native overwrite confirmation
            // refers to the exact file that will be replaced.
            weak->write(file, completion);
        });
}

void ProjectFiles::write(const juce::File& file, std::function<void(bool)> completion)
{
    busy = true;
    report("Saving " + file.getFileName() + "...");
    auto snapshot = session.projectSnapshot();
    workers.addJob([weak = juce::WeakReference<ProjectFiles>(this), snapshot, file, completion]
    {
        juce::TemporaryFile temporary(file);
        auto xml = snapshot.createXml();
        const bool success = xml && xml->writeTo(temporary.getFile()) && temporary.overwriteTargetFileWithTemporary();
        juce::MessageManager::callAsync([weak, snapshot, file, completion, success]
        {
            if (!weak) return;
            weak->busy = false;
            if (success) weak->session.projectSaved(snapshot, file);
            weak->report(success ? "Saved " + file.getFileName() : "Could not save the project. The previous file was kept.");
            if (completion) completion(success);
        });
    });
}

void ProjectFiles::confirmUnsaved(std::function<void()> action)
{
    if (busy) { report("Please wait for the current file operation."); return; }
    if (!session.hasUnsavedChanges()) { action(); return; }
    busy = true;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
        .withTitle("Save your changes?")
        .withMessage("This project has unsaved changes.")
        .withButton("Save").withButton("Discard").withButton("Cancel"),
        [weak = juce::WeakReference<ProjectFiles>(this), action](int result)
        {
            if (!weak) return;
            weak->busy = false;
            if (result == 1)
                weak->save(false, [weak, action](bool saved)
                {
                    if (weak && saved && !weak->session.hasUnsavedChanges()) action();
                });
            else if (result == 2) action();
        });
}

void ProjectFiles::open()
{
    confirmUnsaved([weak = juce::WeakReference<ProjectFiles>(this)] { if (weak) weak->chooseOpen(); });
}

void ProjectFiles::chooseOpen()
{
    busy = true;
    chooser = std::make_unique<juce::FileChooser>("Open project", session.projectFile, "*.thetaedit");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weak = juce::WeakReference<ProjectFiles>(this)](const juce::FileChooser& selected)
        {
            if (!weak) return;
            const auto file = selected.getResult();
            if (file == juce::File{}) { weak->busy = false; return; }
            weak->busy = false;
            weak->load(file);
        });
}

void ProjectFiles::openFile(const juce::File& file)
{
    if (!file.existsAsFile() || !file.hasFileExtension("thetaedit"))
    {
        report("Could not open the selected Theta project.");
        return;
    }
    confirmUnsaved([weak = juce::WeakReference<ProjectFiles>(this), file] { if (weak) weak->load(file); });
}

void ProjectFiles::exportWav()
{
    if (busy)
    {
        report("Please wait for the current file operation.");
        return;
    }

    const auto folder = session.projectFile == juce::File{}
        ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        : session.projectFile.getParentDirectory();
    const auto name = session.projectFile == juce::File{} ? "Untitled.wav"
                                                           : session.projectFile.getFileNameWithoutExtension() + ".wav";
    busy = true;
    chooser = std::make_unique<juce::FileChooser>("Export WAV", folder.getChildFile(name), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [weak = juce::WeakReference<ProjectFiles>(this)](const juce::FileChooser& selected)
        {
            if (!weak) return;
            weak->busy = false;
            const auto selectedFile = selected.getResult();
            if (selectedFile == juce::File{}) return;
            weak->renderWav(selectedFile.hasFileExtension("wav") ? selectedFile
                                                                  : selectedFile.withFileExtension("wav"));
        });
}

void ProjectFiles::renderWav(const juce::File& file)
{
    if (busy) return;
    const auto length = session.edit->getLength();
    if (length.inSeconds() <= 0.0)
    {
        report("There is no arrangement to export.");
        return;
    }

    // Rendering has its own offline playback context. Freeze edit commands
    // until it completes so that the output is one coherent arrangement.
    busy = true;
    if (loadingChanged) loadingChanged(true);
    session.stop();
    auto* edit = session.edit.get();

    // The render drives the very plugin instances the device callback is still
    // holding. Detaching the edit from the device first is what keeps the export
    // off the speakers, and stops the live graph from re-entering plugins that
    // the render has just re-prepared. Released on the message thread below,
    // which reattaches the device.
    renderStatus = std::make_unique<te::Edit::ScopedRenderStatus>(*edit, true);
    // Lanes only move under the shell's playback timer, so the render needs them
    // as engine curves or it writes every automated parameter frozen.
    session.beginOfflineAutomation();
    te::Renderer::turnOffAllPlugins(*edit);

    // Render at the device's own rate and block size. Anything else asks every
    // plugin to run at settings it was never auditioned at, and the file stops
    // being the thing that was heard.
    auto& devices = session.engine.getDeviceManager();
    const double sampleRate = devices.getSampleRate() > 7000.0 ? devices.getSampleRate() : 48000.0;
    const int blockSize = devices.getBlockSize() > 0 ? devices.getBlockSize() : 512;

    report("Exporting " + file.getFileName() + "...");
    workers.addJob([weak = juce::WeakReference<ProjectFiles>(this), edit, file, length, sampleRate, blockSize]
    {
        juce::TemporaryFile temporary(file);
        juce::WavAudioFormat wav;
        bool success = false;
        juce::String error;
        {
            te::Renderer::Parameters parameters(*edit);
            parameters.destFile = temporary.getFile();
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
            parameters.time = {{}, tracktion::core::TimePosition{} + length};
            te::Renderer::RenderTask task("Export WAV", parameters, nullptr, nullptr);
            while (task.runJob() != juce::ThreadPoolJob::jobHasFinished) {}
            error = task.errorMessage;
            success = error.isEmpty() && temporary.overwriteTargetFileWithTemporary();
        }
        juce::MessageManager::callAsync([weak, file, success, error]
        {
            if (!weak) return;
            weak->busy = false;
            // Leave no plugin prepared for the render's settings before the
            // device comes back; releasing the status reallocates the context.
            te::Renderer::turnOffAllPlugins(*weak->session.edit);
            weak->session.endOfflineAutomation();
            weak->renderStatus.reset();
            if (weak->loadingChanged) weak->loadingChanged(false);
            weak->report(success ? "Exported " + file.getFileName()
                                 : "Could not export WAV" + (error.isEmpty() ? juce::String{} : ": " + error));
        });
    });
}

void ProjectFiles::load(const juce::File& file)
{
    if (busy) return;
    busy = true;
    if (loadingChanged) loadingChanged(true);
    report("Opening " + file.getFileName() + "...");
    workers.addJob([weak = juce::WeakReference<ProjectFiles>(this), file]
    {
        auto xml = juce::parseXML(file);
        auto state = xml ? juce::ValueTree::fromXml(*xml) : juce::ValueTree{};
        juce::MessageManager::callAsync([weak, file, state]
        {
            if (!weak) return;
            weak->busy = false;
            const auto result = weak->session.restoreProject(state, file);
            if (weak->loadingChanged) weak->loadingChanged(false);
            weak->report(result.wasOk() ? "Opened " + file.getFileName() : result.getErrorMessage());
        });
    });
}
}
