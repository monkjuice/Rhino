---
title: Mapped parameters, an external plugin's rack
type: component
summary: An external plugin's rack is a chosen list of its parameters, kept on the device by the plugin's own parameter ids, the ones its catalog entry names at first (Forge's nine most-played controls, Duck's nine), and grown by MAP, which opens the plugin's window and puts every control touched there on the rack to automate.
tags: [rhino, devices, automation, forge, duck, vst3]
sources: []
updated: 2026-10-26
---

# Mapped parameters, an external plugin's rack

Rhino's own devices and the engine's built-ins show every control they declare, or a curated few (4OSC's macros). Rhino Forge declares several hundred, and until 2026-10-13 its rack showed the first twelve the engine happened to list, which were none of the ones a synth is played by. An external plugin's rack is now a **chosen list** of its parameters: the controls it is most played by until someone says otherwise, and from then on whatever has been mapped from its own window. The model is `native/src/model/SessionMappedParameters.cpp`; the face is the generic grid in `native/src/ui/rack/DeviceEditorPanel.cpp`; the window it maps from is the rack's floating window (`DeviceRack::openDevice`).

## The list

- **Stored on the device.** `rhinoMappedParameters` on the plugin's state holds the ids in rack order, comma-separated. It rides the document, follows the device along the chain and through undo, and is written through the undo manager, so mapping and removing a knob are undo steps. Absent means the device's defaults.
- **By the plugin's own ids, not indices.** An id is what the host wrapper reports for the parameter (`juce::HostedAudioProcessorParameter::getParameterID`): for a VST3 the plugin's own parameter id, which for a JUCE plugin is a hash of its `paramID` string. Forge adds parameters in the middle of its list (every oscillator's spectral set was added inside the oscillator loop), so an index would move; the id stays as long as the `paramID` string does.
- **Defaults.** The names in the device's catalog entry, `rackParameters`, in that order, resolved by name among the plugin's parameters (`defaultMappedIds`, through `externalDevice`; since 2026-10-26, a literal Forge list in this unit before). Forge's are Cutoff, Resonance, Osc A/B/C Level, Env 1 Attack/Decay/Sustain/Release (`ForgeParameters.cpp` declares `cutoff`, `resonance`, `oscALevel`..., `env1Attack`...); [Rhino Duck](rhino-duck.md)'s are Mix, Curve, Length, Trigger, Loop, End, Band split, Band cutoff, Band solo. An entry that names none shows the plugin's first twelve automatable parameters, which is what the generic face showed before. The defaults are not written when the device is made: a document with no list reads them, and the first map or remove writes the whole list.
- **Resolution.** An id is found among the plugin's parameters (`AudioProcessor::getParameters`), and that parameter's engine twin is the `te::AutomatableParameter` whose `paramID` is the parameter's index as a string: Tracktion's `ExternalPlugin::buildParameterList` names a hosted parameter so unless it is an `AudioProcessorParameterWithID`, which JUCE's hosted VST3 parameters are not (pinned Tracktion `4536d8a`, JUCE `37c894f`). Resolving means a scan of ~700 parameters per id, and a knob drag asks on every pixel, so a list is resolved once into indices and kept (`resolvedListFor`, a static of at most eight lists, keyed by the plugin's `EditItemID`, the stored string and the plugin's parameter count) and each entry is checked against the engine parameter's id on use. Message thread only.
- **Missing entries keep their place.** A lane names a knob by its place in the list, so an id the plugin no longer has resolves to null and reads as "Missing control" rather than closing the gap; *Remove from rack* takes it off. `mapDeviceParameter` refuses a parameter the plugin marks non-automatable (Tracktion builds no `AutomatableParameter` for it, so nothing could drive a knob or a lane), an id holding a comma (the separator) and one already on the rack; the limit is `Session::maximumMappedParameters`, 32.
- **`exposedParameterAt` dispatches here** for any `te::ExternalPlugin` whose wrapped processor loaded (`hasMappedParameters`; `SessionDeviceMacros.cpp`), so every parameter path - knobs, lanes, the mirror, the arrangement's readout - sees the chosen list. `deviceParameters` and `deviceParameterCount` read it directly.

## Lanes

A lane's `parameter` is the knob's place in the list ([Track automation](automation.md)). So a knob only ever joins at the end (`mapDeviceParameter`), and `unmapDeviceParameter` deletes the knob's lane and moves the `parameter` of every lane above it down one, in the same undo step; the overrides kept by hand (`automationRuntime`) move with them. Undo restores the list and the lanes but not the runtime overrides, so an override can sit on the wrong knob after undoing a removal while a neighbour was taken over by hand. A `mapDeviceParameter` marks the document `ModelChange::none` (a knob renders nothing); an unmap marks `curves`.

## Mapping from the window

`Session::beginParameterMapping(track, slot)` puts a `juce::AudioProcessorListener` (`ParameterMapper`, a member of `Session`) on the plugin's wrapped processor; `endParameterMapping` takes it off. One device at a time. What the window reports:

- **A gesture is a hand on a control.** JUCE's VST3 host turns the plugin editor's `beginEdit` into `beginChangeGesture` on the hosted parameter, which reaches every `AudioProcessorListener` as `audioProcessorParameterChangeGestureBegin`. Forge's knobs gesture through `SliderAttachment`.
- **A value with no gesture counts when it comes alone.** Forge's choosers, its matrix depth and several other controls write `setValueNotifyingHost` with no gesture, so a value change on the message thread is taken too, but only when it was the only parameter changed since the last message: a preset loaded in the window changes dozens at once, and would otherwise map them all.
- **The audio thread is ignored.** A parameter the engine moved (a lane, a MIDI learn inside the plugin) is reported from the audio callback; no hand touched it.
- **Nothing happens inside the plugin's callback.** Both reports are queued and applied by an `AsyncUpdater` on the next message (`flushParameterMapping`, which a test calls by hand since the runners pump no loop), which is also what Tracktion's own `ProcessorChangedManager` does with the same callbacks. Each knob mapped is its own undo step; what the last touch did is `parameterMappingReport()`, a serial and a line, which the rack shows in the status line.
- **It ends** when asked, when its device is deleted (the listener comes off while the processor is there to take it off), when a chain change loses the device (`markModified` with `devices` checks, since an instrument switch removes the old plugin without a session call of its own), with the document (`newProject`, `restoreProject`), and when the rack's window closes or another device's replaces it (`DeviceRack::openDevice`, the close button); the same device's window opened again keeps its mapping, and `beginParameterMapping` on the device already being mapped from changes nothing. A device brought back by undo is a new processor the listener was never on, which is why the mapping holds the processor's address and compares it rather than following it (`isMappingParameters`).

## The face

The generic face of a device with a chosen list (`mappable`) shows MAP at the name bar's end, lit in the device's colour while mapping is on, with a tooltip saying what it does. A click opens the device's window (`openEditor`, set by the rack to `openDevice`) and begins mapping; a second click ends it and leaves the window. Right-clicking a knob adds *Remove from rack*. The grid stands two rows deep once it passes six knobs, `(count + 1) / 2` across (`genericColumns`), and the rack lays the chain out again when a face's preferred width changes without the chain changing (`DeviceRack::sync`). A face with every knob taken off says so and keeps MAP. The knobs wear the device's catalog colour, as a generated face's do.

## Tests

`native/src/tests/Pattern/scenarios/MappedParameters.inc` (defaults, map, undo and redo, the mapped knob moving the plugin's own parameter, a lane moving down with its knob, reopening, a gesture and a lone value mapping and a burst not, a touch after the end mapping nothing, the limit, deletion ending the mapping and undo not restoring it) and the Forge block at the end of `runPatternDeviceRackTest` (`DeviceRackTest.cpp`: the five-across layout, MAP's place, the click that begins and ends mapping through a stand-in for the window, the face widening to six across and the rack laying it out). Both run only where a Forge build is found ([Hosting Forge in Rhino](forge-hosting.md)). On 2026-10-13 all six CTest cases passed with the change, the pattern workflow in 44.6 s with the scenario in its output. `DeviceParameters.inc`'s older check that Rhino lists at least 35 Forge parameters now asks the wrapped processor (`getParameters().size() >= 35`), and a second check beside it holds the rack to exactly nine, so the default list's length is pinned there as well as in `MappedParameters.inc`; changing Forge's defaults means editing its catalog entry and both tests. The same scenario holds Duck's rack to nine with Mix first and Band solo last ([Hosting Forge in Rhino](forge-hosting.md)).

## Related

- [Device rack and device editors](device-rack.md)
- [Track automation](automation.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Session, the model](session-model.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Hold ids, not pointers](ids-not-pointers.md)
