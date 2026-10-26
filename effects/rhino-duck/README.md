<h1 align="center">Rhino Duck</h1>

<p align="center"><strong>Sidechain ducking without a compressor: a curve on the beat, a knob for how deep.</strong></p>

<p align="center">
  <a href="#build-duck-on-windows">Build Duck</a> ·
  <a href="../../README.md">Rhino DAW</a> ·
  <a href="../../wiki/pages/rhino-duck.md">Duck wiki</a>
</p>

> [!IMPORTANT]
> Rhino Duck is new and ships as a source-built Windows VST3 and standalone app. It has been rendered and measured by its own test suite; loading it in an outside host is the next step.

Rhino Duck applies a ducking curve straight to the sound on its track, so a bassline drops out of a kick's way with no compressor and no routing. Add it, pick a curve, turn Mix up until the pump fits, and bend the curve with the strip under it when the shape is nearly right.

## What it does

- **Sixteen curves** in five families: eight for sidechaining, two with slower rises for a sub, two that trim a kick's own tail, two that suck a sound away mid-cycle, and two that pulse. Every thumbnail is drawn from the same function the audio runs. The **preset bar** along the foot steps through them, or lists them by family.
- **Mix** sets how much of the curve reaches the sound, from dry to wet. Automate it between the build-up and the drop.
- **Length** repeats the curve every eighth, quarter, half or whole note of the song.
- **Attack, Hold, Release and Depth** bend the chosen curve. Attack eases the cut on the beat (0 to 100 ms), Hold moves the sloped part later or earlier, Release makes it longer or shorter, and Depth keeps that much of the dip. Hold and Release read in milliseconds at the song's tempo and scale with it, as the whole curve does. Dragging the sloped part in the scope moves Hold, and dragging the handle at its end moves Release, so the knobs and the curve always agree. The **Shift** arrows move the whole curve round the cycle by a sixteenth of its length, or a 256th for the fine pair. All of it is remembered per curve, saved with the project, and reset from the scope's corner.
- **Trigger** chooses the clock. **Sync** follows the song's beat and runs on at the song's tempo while it is stopped. **MIDI** restarts the curve on a note, from any channel and key or the ones chosen in the menu. **Audio** restarts it on each hit in the sidechain input, so an irregular kick pattern is followed as played. In MIDI and Audio mode **Loop** either repeats the curve until the next trigger or runs it once and holds at the **End** marker, which is dragged in the scope.
- **Band split** ducks only what lies below a cutoff from 20 Hz to 5.12 kHz, through a fourth-order Linkwitz-Riley crossover whose two halves sum back to the signal, with **Solo** to hear only the band being ducked, the duck applied. **Whole signal** turns the split off; the cutoff and Solo keep their settings.
- **The scope** shows one cycle of the curve over the kick on the sidechain (brown), the signal before the duck (grey) and after it (light grey), so the gap between the greys is what the duck took away.
- **The duck meter** is a rubber duck in a tank that sinks with the gain reduction actually applied, Mix and the band split included, against a scale from 0 dB to minus infinity, with the figure underneath.
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

`RhinoDuckTests --snapshot out.png [SYNC|MIDI|AUDIO] [BAND] [SOLO] [ONESHOT] [SHAPED] [CURVE=n] [SECONDS=s] [SCALE=f]` renders the panel to a PNG after two seconds (or `s`) of a bassline under a kick, without opening a window, with the duck meter moved along frame by frame. `SHAPED` bends the chosen curve with every knob on the strip; `SCALE` renders at that factor.

## Layout

| Directory | Holds |
| --- | --- |
| `core/` | The curves and the engine, headers only, with no JUCE GUI in them |
| `ui/` | The theme, the layout arithmetic, the controls, the drawn parts (plates, screws, the duck) and the duck meter, all headers |
| `src/` | The processor, its parameters, the scope and the editor (`DuckEditor.cpp` and its painting, `DuckEditorPaint.cpp`) |
| `tests/` | The test binary and the snapshot tool |
