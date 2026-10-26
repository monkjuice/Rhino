<h1 align="center">Rhino Duck</h1>

<p align="center"><strong>Sidechain ducking without a compressor: a curve on the beat, a knob for how deep.</strong></p>

<p align="center">
  <a href="#build-duck-on-windows">Build Duck</a> ·
  <a href="../../README.md">Rhino DAW</a> ·
  <a href="../../wiki/pages/rhino-duck.md">Duck wiki</a>
</p>

> [!IMPORTANT]
> Rhino Duck is new and ships as a source-built Windows VST3 and standalone app. It has been rendered and measured by its own test suite; loading it in an outside host is the next step.

Rhino Duck applies a ducking curve straight to the sound on its track, so a bassline drops out of a kick's way with no compressor, no routing and no attack and release to tune. Add it, pick a curve, turn Mix up until the pump fits.

## What it does

- **Sixteen curves** in five families: eight for sidechaining, two with slower rises for a sub, two that trim a kick's own tail, two that suck a sound away mid-cycle, and two that pulse. Every thumbnail is drawn from the same function the audio runs.
- **Mix** sets how deep the duck goes, from nothing to the whole curve. Automate it between the build-up and the drop.
- **Length** repeats the curve every eighth, quarter, half or whole note of the song.
- **Slope** drags the sloped part of a curve left or right to fit the kick, and the **Shift** arrows move the whole curve round the cycle by a sixteenth of its length, or a 256th for the fine pair. Both are remembered per curve, saved with the project, and reset from the scope's corner.
- **Trigger** chooses the clock. **Sync** follows the song's beat and runs on at the song's tempo while it is stopped. **MIDI** restarts the curve on a note, from any channel and key or the ones chosen in the menu. **Audio** restarts it on each hit in the sidechain input, so an irregular kick pattern is followed as played. In MIDI and Audio mode **Loop** either repeats the curve until the next trigger or runs it once and holds at the **End** marker, which is dragged in the scope.
- **Band split** ducks only what lies below a cutoff from 20 Hz to 5.12 kHz, through a fourth-order Linkwitz-Riley crossover whose two halves sum back to the signal, with **Solo** to hear only the band being ducked.
- **The scope** shows one cycle of the curve over the ducked output and, in the warmer colour, the sidechain, so the slope can be fitted to the kick by eye.
- **Latency** is nothing in Sync. Audio triggering looks 10 ms ahead so the duck lands on the kick's onset, and anti-click smoothing, on by default and in the menu, leans the gain into each restart over 6 ms. MIDI with smoothing is 6 ms, Audio 10 or 16. The host is told, and a change of mode may need playback restarted.

## Build Duck on Windows

Duck takes JUCE from Rhino's pinned checkout, so fetch Rhino's dependencies first (`python native/scripts/fetch-dependencies.py` from the repository root). Then, from the repository root:

```powershell
cmake -S effects/rhino-duck -B effects/rhino-duck/build -G "Visual Studio 17 2022" -A x64
cmake --build effects/rhino-duck/build --config Release --parallel 2 -- /p:BuildInParallel=false
```

Outputs land under `effects/rhino-duck/build/RhinoDuck_artefacts/Release/`: `VST3/Rhino Duck.vst3` and `Standalone/Rhino Duck.exe`. Copy or link the `.vst3` bundle into `C:\Program Files\Common Files\VST3` for a host to find it, and route the kick into the plugin's sidechain input for the Audio trigger and the scope's kick view.

## Test

One binary, one file per area, one CTest case per area: `curves`, `engine`, `processor` and `layout`.

```powershell
cmake --build effects/rhino-duck/build --config Release --target RhinoDuckTests --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir effects/rhino-duck/build -C Release --output-on-failure
```

`RhinoDuckTests --snapshot out.png [SYNC|MIDI|AUDIO] [BAND] [ONESHOT]` renders the panel to a PNG after two seconds of a bassline under a kick, without opening a window.

## Layout

| Directory | Holds |
| --- | --- |
| `core/` | The curves and the engine, headers only, with no JUCE GUI in them |
| `ui/` | The theme, the layout arithmetic and the controls, all headers |
| `src/` | The processor, its parameters, the scope and the editor |
| `tests/` | The test binary and the snapshot tool |
