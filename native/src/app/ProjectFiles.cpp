#include "ProjectFiles.h"

namespace rhino
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
        ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.rhinoedit")
        : session.projectFile.withFileExtension("rhinoedit");
    chooser = std::make_unique<juce::FileChooser>("Save project", suggested, "*.rhinoedit");
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
    // Named for the file it is written to, which a Save As moves.
    auto snapshot = session.projectSnapshot(file);
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

void ProjectFiles::newProject()
{
    confirmUnsaved([weak = juce::WeakReference<ProjectFiles>(this)]
    {
        if (!weak) return;
        weak->session.newProject();
        weak->report("New project");
    });
}

void ProjectFiles::open()
{
    confirmUnsaved([weak = juce::WeakReference<ProjectFiles>(this)] { if (weak) weak->chooseOpen(); });
}

void ProjectFiles::chooseOpen()
{
    busy = true;
    chooser = std::make_unique<juce::FileChooser>("Open project", session.projectFile, "*.rhinoedit");
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
    if (!file.existsAsFile() || !file.hasFileExtension("rhinoedit"))
    {
        report("Could not open the selected Rhino project.");
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

// An export renders the live edit, so it runs on the message thread rather
// than on a worker: in steps behind the shell's loading screen, which keeps
// every edit out until it is over (Session::startExport).
void ProjectFiles::renderWav(const juce::File& file)
{
    if (busy) return;
    if (const auto started = session.startExport(file); started.failed())
    {
        report(started.getErrorMessage());
        return;
    }
    exporting = file;
    report("Exporting " + file.getFileName() + "...");
    if (exportStarted)
    {
        exportStarted(file);
        return;
    }
    while (session.continueExport(1000.0)) {}
    exportFinished();
}

void ProjectFiles::exportFinished()
{
    const auto outcome = session.exportOutcome();
    report(outcome.wasOk() ? "Exported " + exporting.getFileName() : outcome.getErrorMessage());
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
