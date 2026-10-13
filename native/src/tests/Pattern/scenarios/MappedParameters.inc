        // An external plugin's rack is a chosen list of its parameters: the
        // controls a synth is played by at first, and from then on whatever is
        // mapped onto it from the plugin's own window. A lane names a knob by
        // its place in the list. Only where a Forge build is found.
        {
            auto document = std::make_unique<Session>();
            auto& mapped = *document;
            if (mapped.isForgeAvailable())
            {
                require(mapped.addInstrument(Session::Instrument::RhinoForge, 2).wasOk(),
                        "Forge goes on the third track, a MIDI lane");
                const auto forgeSlots = mapped.deviceSlots(2);
                require(forgeSlots.size() == 1 && forgeSlots.front().deviceId == "RhinoForge",
                        "and is the lane's one device");
                const auto slot = forgeSlots.front().pluginIndex;
                require(mapped.deviceHasMappedParameters(2, slot), "Forge's rack is a chosen list of its parameters");
                const auto names = [&mapped, slot]
                {
                    juce::StringArray found;
                    for (const auto& parameter : mapped.deviceParameters(2, slot))
                        found.add(parameter.name);
                    return found;
                };
                require(names() == juce::StringArray {"Cutoff", "Resonance", "Osc A Level", "Osc B Level", "Osc C Level",
                                                      "Env 1 Attack", "Env 1 Decay", "Env 1 Sustain", "Env 1 Release"},
                        "A fresh Forge shows the filter, the three oscillator levels and the amplitude envelope");
                require(mapped.deviceParameterCount(2, slot) == 9, "nine knobs in all");
                auto* forge = mapped.devicePlugin(2, slot);
                auto* processor = forge != nullptr ? forge->getWrappedAudioProcessor() : nullptr;
                require(processor != nullptr, "Forge's processor is reachable");
                const auto pluginParameter = [processor] (const juce::String& name)
                {
                    const auto& parameters = processor->getParameters();
                    for (int i = 0; i < parameters.size(); ++i)
                        if (parameters[i]->getName(1024) == name)
                            return i;
                    return -1;
                };
                const auto env2Attack = pluginParameter("Env 2 Attack");
                require(env2Attack >= 0, "Forge declares Env 2 Attack");
                require(mapped.mapDeviceParameter(2, slot, env2Attack).wasOk(), "Env 2 Attack can be mapped onto the rack");
                require(mapped.deviceParameterCount(2, slot) == 10 && names()[9] == "Env 2 Attack", "and joins at the end");
                require(mapped.mapDeviceParameter(2, slot, env2Attack).failed() && mapped.deviceParameterCount(2, slot) == 10,
                        "Mapping it again is refused and changes nothing");
                mapped.undo();
                require(mapped.deviceParameterCount(2, slot) == 9, "One undo takes the knob off again");
                mapped.redo();
                require(mapped.deviceParameterCount(2, slot) == 10, "and a redo puts it back");
                // The knob is the engine's own parameter: turning it moves the plugin.
                const auto attack = mapped.deviceParameters(2, slot)[9];
                require(mapped.setDeviceParameter(2, slot, 9, attack.maximum).wasOk(), "The mapped knob can be turned");
                require(std::abs(processor->getParameters()[env2Attack]->getValue() - 1.0f) < 0.01f,
                        "and the plugin's own parameter follows it");

                // A lane names a knob by its place in the list, so taking a
                // knob off moves the lanes of the knobs after it down.
                require(mapped.setTrackAutomationPoints({2, slot, 9}, {{0.0, attack.minimum}, {2.0, attack.maximum}}).wasOk(),
                        "A lane is drawn on Env 2 Attack");
                require(mapped.unmapDeviceParameter(2, slot, 0).wasOk(), "Cutoff can be taken off the rack");
                require(mapped.deviceParameterCount(2, slot) == 9 && names()[0] == "Resonance" && names()[8] == "Env 2 Attack",
                        "The knobs after it move down one");
                require(mapped.trackAutomationState({2, slot, 8}).active && !mapped.trackAutomationState({2, slot, 9}).visible,
                        "and the lane moves down with its knob");
                const auto lanes = mapped.trackAutomations(2);
                require(lanes.size() == 1 && lanes.front().parameterName == "Env 2 Attack" && lanes.front().points.size() == 2,
                        "The lane still drives Env 2 Attack with both its points");
                mapped.undo();
                require(mapped.deviceParameterCount(2, slot) == 10 && names()[0] == "Cutoff"
                            && mapped.trackAutomationState({2, slot, 9}).active,
                        "One undo puts the knob and the lane back where they were");

                // The list rides the document.
                juce::TemporaryFile saved(".rhinoedit");
                require(mapped.projectSnapshot().createXml()->writeTo(saved.getFile()), "The document is written");
                auto reopened = std::make_unique<Session>();
                require(reopened->restoreProject(juce::ValueTree::fromXml(*juce::parseXML(saved.getFile())), saved.getFile()).wasOk(),
                        "and opens again");
                const auto reopenedSlot = reopened->deviceSlots(2).front().pluginIndex;
                juce::StringArray reopenedNames;
                for (const auto& parameter : reopened->deviceParameters(2, reopenedSlot))
                    reopenedNames.add(parameter.name);
                require(reopenedNames.size() == 10 && reopenedNames[0] == "Cutoff" && reopenedNames[9] == "Env 2 Attack",
                        "The mapped knobs come back in their order");
                require(reopened->trackAutomationState({2, reopenedSlot, 9}).active,
                        "and the lane on the mapped knob with them");
                reopened->releaseAudioDevice();
                reopened.reset();

                // Mapping from the window: a control touched there joins the rack.
                require(mapped.beginParameterMapping(2, slot).wasOk() && mapped.isMappingParameters(2, slot),
                        "Mapping from Forge's window can begin");
                const auto lfo1Rate = pluginParameter("LFO 1 Rate");
                require(lfo1Rate >= 0, "Forge declares LFO 1 Rate");
                processor->getParameters()[lfo1Rate]->beginChangeGesture();
                processor->getParameters()[lfo1Rate]->endChangeGesture();
                mapped.flushParameterMapping();
                require(mapped.deviceParameterCount(2, slot) == 11 && names()[10] == "LFO 1 Rate",
                        "A knob taken hold of in the window joins the rack");
                require(mapped.parameterMappingReport().text.contains("LFO 1 Rate"), "and the report says so");
                // A control that sends no gesture counts when its value comes
                // alone; a preset's burst of values does not.
                for (const auto* name : {"LFO 2 Rate", "LFO 3 Rate", "LFO 4 Rate"})
                    processor->getParameters()[pluginParameter(name)]->setValueNotifyingHost(0.4f);
                mapped.flushParameterMapping();
                require(mapped.deviceParameterCount(2, slot) == 11, "Three values at once map nothing");
                processor->getParameters()[pluginParameter("LFO 2 Rate")]->setValueNotifyingHost(0.5f);
                mapped.flushParameterMapping();
                require(mapped.deviceParameterCount(2, slot) == 12 && names()[11] == "LFO 2 Rate",
                        "A value on its own maps its control");
                mapped.endParameterMapping();
                require(!mapped.isMappingParameters(2, slot), "Mapping ends when asked");
                processor->getParameters()[pluginParameter("LFO 3 Rate")]->beginChangeGesture();
                processor->getParameters()[pluginParameter("LFO 3 Rate")]->endChangeGesture();
                mapped.flushParameterMapping();
                require(mapped.deviceParameterCount(2, slot) == 12, "and a touch after that maps nothing");

                // The rack holds a limited number of knobs.
                for (int i = 0; mapped.deviceParameterCount(2, slot) < Session::maximumMappedParameters; ++i)
                {
                    require(i < processor->getParameters().size(), "Forge has enough parameters to fill the rack");
                    mapped.mapDeviceParameter(2, slot, i);
                }
                require(mapped.deviceParameterCount(2, slot) == Session::maximumMappedParameters, "The rack fills to its limit");
                require(mapped.mapDeviceParameter(2, slot, pluginParameter("LFO 3 Rate")).failed(),
                        "and refuses a knob past it");

                // Deleting the device ends a mapping from its window.
                require(mapped.beginParameterMapping(2, slot).wasOk(), "Mapping begins again");
                require(mapped.deleteDevice(2, slot).wasOk(), "Forge is deleted");
                require(!mapped.isMappingParameters(2, slot), "and the mapping ends with it");
                mapped.undo();
                require(mapped.deviceSlots(2).size() == 1
                            && !mapped.isMappingParameters(2, mapped.deviceSlots(2).front().pluginIndex),
                        "A Forge brought back by undo is not being mapped from");
            }
            mapped.releaseAudioDevice();
        }
