<p align="center">
  <img src="native/assets/rhino_app_icon_256.png" width="128" alt="Rhino logo">
</p>

<h1 align="center">Rhino</h1>

<p align="center"><strong>A focused music studio for turning an idea into a finished track.</strong></p>

<p align="center">
  <a href="#build-rhino-on-windows">Build Rhino</a> ·
  <a href="instruments/rhino-forge/README.md">Meet Forge</a> ·
  <a href="wiki/index.md">Explore the wiki</a>
</p>

> [!IMPORTANT]
> Rhino is in **mid-alpha**. It is ready for experimentation and feedback, not production-critical work. The current build path is Windows only; the first production release, with installers for Windows and macOS, is targeted for **December 2026–January 2027**.

Rhino is a native desktop DAW built for electronic music, sound design, and fast arrangement. Record audio or MIDI, shape parts directly on the timeline, build device chains, and finish the mix without leaving one focused workspace.

It is written in C++20 with [JUCE](https://juce.com/) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine), with immediate pointer response and smooth visual feedback treated as core features—not finishing touches.

## From first beat to final mix

### Write without breaking the flow

Create MIDI clips directly on the arrangement, draw and edit notes in the step grid, play instruments from a MIDI controller or the computer keyboard, and shape ideas with Rhino Arp. Region-based cut, copy, paste, duplicate, split, and undo keep larger arrangements quick to edit.

### Record what happens

Record MIDI and audio into dedicated track types, choose inputs per track, set monitoring to Off, Auto, or In, and use a one-to-four-bar count-in. Takes become ordinary timeline clips, ready to move, trim, split, and process.

### Make audio fit the song

Import samples and recordings, then adjust clip gain, pan, pitch, fades, reverse, and mute. Time warp follows project tempo with modes for beats, tones, textures, complex material, and repitch; warp markers handle performances that drift.

### Build a sound of your own

Rhino includes instruments, drum kits, MIDI effects, and audio effects in a drag-and-drop browser. The independent [Rhino Forge](instruments/rhino-forge/README.md) synth adds wavetable and spectral synthesis, deep modulation, an arpeggiator, flexible routing, and effects as a VST3 or standalone app.

### Mix with the essentials in reach

Every audio and MIDI track has volume, pan, mute, solo, devices, automation, and routing. Group tracks into real bus tracks, process the main output, and use built-in tools including Rhino EQ, Rhino Tune, Rhino Vocoder, Utility, and sidechain routing.

## What to expect from the alpha

The foundations are working: arrangement editing, audio and MIDI recording, project save/open, offline WAV export, track groups, automation, VST3 hosting, built-in devices, and a tested native audio model.

Some production workflows are still being completed. Projects reference imported media in place, plugin discovery is limited, recording has no comping or loop-take workflow yet, and the clip-launching session view is temporarily disabled while its interaction model is finished. macOS remains unvalidated until the release build and installer work begins.

Keep backups of anything important and expect project compatibility to change during alpha development.

## Build Rhino on Windows

There is no installer during alpha. A source build takes four tools and a few commands.

### 1. Install the prerequisites

- [Git](https://git-scm.com/download/win) and [Git LFS](https://git-lfs.com/)
- [Python 3.12 or newer](https://www.python.org/downloads/windows/)
- [CMake 3.24 or newer](https://cmake.org/download/)
- [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) with **Desktop development with C++** and a Windows SDK

### 2. Clone and build

Open PowerShell and run:

```powershell
git lfs install
git clone https://github.com/monkjuice/Rhino.git
Set-Location Rhino
python native/scripts/fetch-dependencies.py
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release --parallel 2
```

The dependency script downloads Rhino's pinned JUCE and Tracktion Engine revisions into the ignored `native/.deps` directory.

### 3. Start Rhino

```powershell
& ".\native\build\RhinoNative_artefacts\Release\RhinoDAW.exe"
```

On first launch, open **Edit → Audio settings** and choose your audio device, sample rate, buffer size, and inputs.

### 4. Add Rhino Forge (optional)

Forge is built separately, using the JUCE checkout fetched above:

```powershell
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2 -- /p:BuildInParallel=false
```

Rhino discovers the development VST3 automatically. You can also run Forge by itself from:

```text
instruments/rhino-forge/build/RhinoForge_artefacts/Release/Standalone/Rhino Forge.exe
```

## Verify a development build

```powershell
ctest --test-dir native/build -C Release --output-on-failure
ctest --test-dir instruments/rhino-forge/build -C Release -j 8 --output-on-failure
```

The detailed build notes, architecture, component guides, decisions, and known gotchas live in the [Rhino wiki](wiki/index.md). Start with the [project overview](wiki/overview.md), [building Rhino](wiki/pages/build-and-test-rhino.md), or [building Forge](wiki/pages/build-and-test-forge.md).

## Platform roadmap

| Today | Production target |
| --- | --- |
| Windows source builds | Windows installer |
| macOS architecture kept portable but unvalidated | macOS installer |
| Mid-alpha project format and workflows | Production-ready release |

Rhino is developed at [github.com/monkjuice/Rhino](https://github.com/monkjuice/Rhino).
