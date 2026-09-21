# Native test map

The command-line suite deliberately has two layers:

- Small unit-style checks cover deterministic calculations and isolated device-rack behavior.
- Workflow checks cover Tracktion Engine state, undo/redo, rendering, persistence, and real pointer interactions where mocks would hide integration failures.

`Pattern/WorkflowTest.cpp` and `Arrangement/WorkflowTest.cpp` are only runners. Their `scenarios/` includes preserve one shared workflow while keeping each feature area small enough to inspect independently. A scenario may depend on state created by an earlier scenario, so keep their include order explicit.

**Anything that renders has to come before `scenarios/GesturesAndPersistence.inc`.** That scenario calls `Session::releaseAudioDevice`, and an offline render attempted afterwards never returns - `RenderTask::runJob` simply never reports `jobHasFinished`, so the symptom is the whole runner timing out rather than a failed assertion. `scenarios/GroupBusRouting.inc` sits where it does for exactly this reason.

**Every scenario shares the runner function's stack frame, so a large local is a stack overflow waiting for the deepest call in the suite.** A `StepGrid` carries about 200 KB of row-addressed note caches, and the third one declared across the scenarios put the runner past the 1 MB Windows stack - the crash landed in the middle of `scenarios/Rendering.inc`, nowhere near the scenario that added the object, and reported only as `SegFault` with exception code `0xC00000FD`. Declare a `StepGrid`, an `Arrangement` or anything else of that size with `std::make_unique` rather than by value. The Windows event log names the exception code, which is what separates this from an ordinary null dereference:

```powershell
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=(Get-Date).AddMinutes(-20)}
```

**A claim about pitch, level or timbre is settled by measuring rendered audio, not by reading the DSP.** `AutoTuneTest.cpp` is the pattern: it pushes a synthesised saw or vowel through the engine and finds the fundamental with a windowed transform scanned across a band. That transform never looks for a period, so it cannot agree with the pitch tracker by construction — which is exactly what makes it worth having. Its pitch assertions report what they measured and what they wanted, because a bare line number on a number that came out of a transform costs a second run to learn anything at all.

Two of its checks were wrong before they were right, and both mistakes generalise. One asserted that C# corrects to C in C major: C# is the same distance from C and from D, so which way a tie falls is not a property worth testing, and a tracker landing a hundredth of a semitone either side flips it. The other measured a formant shift by the tallest partial, which moved from the sixth harmonic to the second when the envelope rose — the shift was working and the measurement said the opposite. Where a spectrum is being compared, prefer a statistic over the whole of it, such as the harmonic below which most of the magnitude lies, to any single peak.

Add a unit-style test when behavior can be exercised without a complete `Session`, audio render, desktop peer, or pointer sequence. Add a workflow scenario when the contract crosses those boundaries. Prefer extending the narrowest existing file; create a new scenario once a file approaches roughly 200 lines or mixes unrelated behavior.

CTest entry points:

- `native_arrangement_geometry`: clip edit bounds and playhead damage calculations
- `native_device_correctness`: device DSP and state restoration, including Rhino Tune's measured checks in `AutoTuneTest.cpp`
- `native_pattern_workflow`: notes, presets, automation, renders, and project persistence
- `native_arrangement_workflow`: browser drops, drawing, editing, tracks, and arrangement persistence
- `native_startup_lifecycle`: application startup

Run all checks with `ctest --test-dir native/build -C Release --output-on-failure`.
