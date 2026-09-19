# Native test map

The command-line suite deliberately has two layers:

- Small unit-style checks cover deterministic calculations and isolated device-rack behavior.
- Workflow checks cover Tracktion Engine state, undo/redo, rendering, persistence, and real pointer interactions where mocks would hide integration failures.

`Pattern/WorkflowTest.cpp` and `Arrangement/WorkflowTest.cpp` are only runners. Their `scenarios/` includes preserve one shared workflow while keeping each feature area small enough to inspect independently. A scenario may depend on state created by an earlier scenario, so keep their include order explicit.

**Anything that renders has to come before `scenarios/GesturesAndPersistence.inc`.** That scenario calls `Session::releaseAudioDevice`, and an offline render attempted afterwards never returns - `RenderTask::runJob` simply never reports `jobHasFinished`, so the symptom is the whole runner timing out rather than a failed assertion. `scenarios/GroupBusRouting.inc` sits where it does for exactly this reason.

Add a unit-style test when behavior can be exercised without a complete `Session`, audio render, desktop peer, or pointer sequence. Add a workflow scenario when the contract crosses those boundaries. Prefer extending the narrowest existing file; create a new scenario once a file approaches roughly 200 lines or mixes unrelated behavior.

CTest entry points:

- `native_arrangement_geometry`: clip edit bounds and playhead damage calculations
- `native_device_correctness`: device DSP and state restoration
- `native_pattern_workflow`: notes, presets, automation, renders, and project persistence
- `native_arrangement_workflow`: browser drops, drawing, editing, tracks, and arrangement persistence
- `native_startup_lifecycle`: application startup

Run all checks with `ctest --test-dir native/build -C Release --output-on-failure`.
