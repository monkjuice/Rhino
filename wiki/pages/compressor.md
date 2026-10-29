---
title: Compressor
type: component
summary: Tracktion's compressor made Rhino's own (CompressorDevice, rhino.compressor.v1) so it can say how far it turns the audio down - the reduction read off the audio itself, output over input less make-up, never modelled - and shown as a gain reduction meter at the right of its plain-grid face.
tags: [rhino, devices, dsp, ui, metering, tracktion]
sources: []
updated: 2026-10-28
---

# Compressor

Catalog id `Compressor`. Since 2026-10-28 (commit `532f3b4`) it is `CompressorDevice` (`native/src/devices/audio/CompressorDevice.*`), a `final` subclass of `te::CompressorPlugin` that sounds exactly like it and overrides only `getPluginType` and `applyToBuffer`. The user asked for a meter showing "how much is being compressed in dB".

## A type name of its own

Its type name is `rhino.compressor.v1`, not the engine's `compressor`: Tracktion registers its built-ins before Rhino's and silently refuses a second type under a taken name ([Tracktion refuses a second built-in type under a name it already has](tracktion-refuses-duplicate-built-in-type.md)). The catalog entry keeps its id and now carries `.create = factory<CompressorDevice>()`, so `--device-test` holds it to [the native device standard](native-device-standard.md) like Rhino's other devices. A `.rhinoedit` saved before this opens its compressor as the engine's plain plugin, which the catalog no longer names: no meter, no catalog id. That is accepted ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)). `SessionSidechain.cpp` still finds it by `dynamic_cast<te::CompressorPlugin*>` ([Sidechains](sidechains.md)).

## How it measures

Tracktion keeps the detector private (`currentLevel`) and exposes no gain reduction, so nothing models it. The compressor multiplies every frame by one gain, make-up times reduction, so the reduction is read off the audio:

- `applyToBuffer` hands the block to the base in pieces of 64 frames, each piece's input copied to the stack, and divides output by input times make-up per frame, on the louder channel.
- Frames quieter than `quietest` (1e-4) are skipped. Tracktion's stereo path adds and subtracts 1.0 on every frame to flush denormals, so output over input is never exactly the make-up: the error is up to a part in 1700 (about 0.005 dB) at 1e-4 and more below it. So silence, or a sidechain driving a silent main input, reads 0.
- **A deadband, `faintestDb` (0.01 dB).** A block whose reduction measures under it reads as none and is not published, because that is the rounding above, not compression. Replicating the engine's +1/-1 before dividing was rejected: it ties the measurement to an engine internal, and `/fp:fast` could fold it away. The deadband is well under the meter's 0.1 dB resolution.
- The block's smallest gain becomes its reduction in dB, kept as a running maximum in a `std::atomic<float>` by compare-exchange. `takeGainReductionDb()` exchanges it with 0; the face is its one reader.
- The pieces are bit-identical to one call, because the base reads its parameters once at the head of each call and carries its detector across calls. No allocation, no lock ([Real-time audio rules](real-time-audio-rules.md)).

The meter therefore shows the gain applied and cannot drift from it ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

## The meter

The face stays the plain grid; `DeviceEditorPanelCompressor.cpp` adds a 44 px column at its right ([Device rack and device editors](device-rack.md)): caption "GR", a bar falling from the top in `palette::gainReduction` (amber, so it never reads as a level meter), figures 0, 3, 6, 12 and 24 dB on a square-root scale (a mix's few dB take a third of the height), a held line, and the held value printed ("-4.2 dB"). Ballistics are the track meters' `MeterBallistics` fed dB of reduction: instant attack, 24 dB/s release, 1.5 s hold ([Track level meters](level-meters.md)). It ticks on the face's 24 Hz timer only while the compressor is enabled, and a reading gathered while nobody looked (the first frame, or a gap over 0.5 s) is discarded.

## Tests

All six CTest cases passed with these on 2026-10-28 (commit `532f3b4`): the Compressor passes all six standard checks, and `--device-test` checked 19 devices.

- `tests/CompressorMeterTest.cpp` (`checkCompressorMeter`, run by `--device-test` after the conformance loop): -6 dB DC into 4:1 at -20 dB reads the static curve (7.96 dB) within 0.02 dB and the rendered gain within 0.01 dB; a reading is spent once taken; +6 dB make-up leaves it alone; mono reads the same; under threshold and silence read 0; Rhino's and the engine's compressor match bit for bit in blocks of 480, 1000 and 4096, with and without a sidechain. The under-threshold check is what found the rounding: on the first run it read a tiny reduction with no compression and failed `native_device_conformance`, which led to `faintestDb`.
- `tests/Pattern/DeviceRackTest.cpp` covers the face; `ReturnTracks.inc` finds the compressor by `deviceId` rather than the engine's type name.

## Related

- [Built-in devices](built-in-devices.md)
- [Device catalog](device-catalog.md)
- [Sidechains](sidechains.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
