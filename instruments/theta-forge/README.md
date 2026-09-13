# Theta Forge

Theta Forge is Theta's independent synthesizer project. It is a VST3 and
standalone JUCE application that deliberately owns no Tracktion or Theta-DAW
types. Its initial sound engine is a focused, two-oscillator wavetable-style
synth with sub/noise, unison, a resonant filter, ADSR, and host automation.

It takes inspiration from the fast, visual sound-design workflow of modern
hybrid synths. It does not reuse Serum code, assets, names, presets, or UI.

## Build on Windows

First fetch the parent project's dependencies, then configure this directory:

```powershell
cmake -S instruments/theta-forge -B instruments/theta-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/theta-forge/build --config Release --parallel 2
```

The VST3 is emitted below `build/ThetaForge_artefacts/Release/VST3`. Install or
copy it only after validating it in a host; do not add generated plugin bundles
to Git.

Theta's development build discovers that bundle directly, then instantiates it
through Tracktion Engine's standard external-plugin wrapper. The editor shown in
Theta is this plugin's own editor; Forge has no dependency on Theta or Tracktion.
