---
title: Reproducing a live timing bug offline
type: guide
summary: Render the real project in a test at the audio device's block size, not the offline default, to catch bugs that live on block boundaries.
tags: [rhino, debugging, audio, testing]
sources: []
updated: 2026-10-06
---

# Reproducing a live timing bug offline

Some bugs depend on where block boundaries fall: they are audible while the device runs and absent from a render made
in a test. A scenario's `te::Renderer::Parameters` renders in 512-sample blocks unless it sets `blockSizeForAudio`, and
none of the existing scenarios do. The File menu's WAV export is not affected: since commit d3ead2a (2026-09-15)
`ProjectFiles::renderWav` asks the audio device for its own rate and block size (falling back to 48 kHz and 512), so
an export follows the device's block size; the trap is in renders made by tests.

## Why the block size matters

The development machine's device has run 10 ms blocks: 480 samples at 48 kHz, and 441 at 44.1 kHz. Both appear in
`%APPDATA%\Rhino\rhino.log` as `Audio block size: <n>  Rate: <r>`, so read the current one there rather than
assuming it. At 10 ms a beat at 120 bpm is exactly 50 blocks, so every note of a 120 bpm song sits on a block
boundary; at 512 almost none do. Anything sensitive to which side of a boundary a note is delivered on flips between
the two.

Automation no longer differs. Since commit `fe163c8` the engine plays the lanes from parameter curves, read every
block live and in any render alike, and the session mirrors the lanes onto those curves after every announced change
([Track automation](automation.md)). Before, the shell moved automation from its 30 Hz timer while a render read curves
only the export had written, so live and rendered sweeps moved at different rates.

## Steps

1. Write a throwaway scenario and include it in a runner before `GesturesAndPersistence.inc`, after which renders never
   finish ([Writing Rhino tests](writing-rhino-tests.md)). Load the real project into the shared session:
   ```cpp
   session.restoreProject(juce::ValueTree::fromXml(*juce::parseXML(file)), file);
   ```
2. Render it twice with a `te::Renderer::RenderTask`, as `tests/Pattern/scenarios/Rendering.inc` does: once at the
   default, once with `parameters.blockSizeForAudio` and `sampleRateForAudio` set to what the log says.
3. Compare the two. For drums, probe each expected hit with a one-pole low-pass near 120 Hz and the peak over a short
   window; a bare onset detector counts one ringing kick four times.
4. Touch the runner before building ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)),
   and delete the scenario and its `#include` afterwards. Write throwaway C++ with an editor or the Write tool, not a
   shell heredoc, which eats backslashes ([Development environment and reference material](development-environment.md)).

## The case that taught it

Three bars of `simpleBeat.rhinoedit` had no kick after the first beat while the device ran: twelve of twenty-five
kicks were lost, while a 512-sample render played all of them (commit 6d9477b, 2026-09-22). `DrumDevice::applyToBuffer`
consumed a MIDI message when its rounded timestamp reached a frame, so a message stamped at the block's full length was
never reached and was dropped, not delayed. Clips dragged into place kept floating-point residue (starts of
3.999999999999997 beside 6.000000000000003), so the notes of the clips that fell short arrived at the end of the
preceding block. Such a message now plays on the block's last frame, and a pad refuses a second strike within a
millisecond, because the engine also delivered a straddling clip's first note twice. The [Drum Rack](drum-rack.md)
replaced `DrumDevice` on 2026-10-06: the device SDK splits each block at its MIDI events, the millisecond guard lives
in `DrumRackEngine::strike`, and `--self-test` still drives the rack's `applyToBuffer` with both cases.

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Built-in devices](built-in-devices.md)
- [Drum Rack](drum-rack.md)
- [Track automation](automation.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
