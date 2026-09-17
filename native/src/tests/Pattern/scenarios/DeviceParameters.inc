        {
            Session parameterSession;
            require(parameterSession.addAudioTrack().wasOk(), "A document adds the audio track these checks need");
            auto* parameterTrack = te::getAudioTracks(*parameterSession.edit)[1];
            require(parameterSession.addAudioEffect(Session::AudioEffect::Reverb, 1).wasOk(), "Audio FX browser action inserts Reverb");
            const auto reverbDevices = parameterSession.deviceSlots(1);
            const auto reverbSlot = reverbDevices.empty() ? -1 : reverbDevices.back().pluginIndex;
            auto reverbParameters = parameterSession.deviceParameters(1, reverbSlot);
            require(!reverbParameters.empty(), "Inserted Reverb exposes editable parameters");
            const auto oldReverbValue = reverbParameters.front().value;
            const auto newReverbValue = oldReverbValue == reverbParameters.front().minimum
                ? reverbParameters.front().maximum : reverbParameters.front().minimum;
            require(parameterSession.beginDeviceParameterGesture(1, reverbSlot, 0).wasOk(), "Device View starts parameter gestures");
            require(parameterSession.setDeviceParameter(1, reverbSlot, 0, newReverbValue).wasOk(), "Device View edits effect parameters");
            require(parameterSession.endDeviceParameterGesture(1, reverbSlot, 0).wasOk(), "Device View ends parameter gestures");
            reverbParameters = parameterSession.deviceParameters(1, reverbSlot);
            require(std::abs(reverbParameters.front().value - newReverbValue) < 0.0001f, "Edited effect parameter value is reflected in Device View");
            require(parameterSession.addAudioEffect(Session::AudioEffect::RhinoSpace, 1).wasOk(), "Audio FX browser action inserts Rhino Space");
            const auto rhinoDevices = parameterSession.deviceSlots(1);
            const auto rhinoSlot = rhinoDevices.empty() ? -1 : rhinoDevices.back().pluginIndex;
            auto rhinoParameters = parameterSession.deviceParameters(1, rhinoSlot);
            require(rhinoParameters.size() >= 6, "Rhino Space exposes its macro controls");
            require(parameterSession.setDeviceParameter(1, rhinoSlot, 0, rhinoParameters.front().maximum).wasOk(),
                    "Rhino Space macro controls are editable");
            require(parameterSession.addInstrument(Session::Instrument::FourOsc, 1).wasOk(), "Audio track can host a synth instrument");
            require(parameterSession.addMidiEffect(Session::MidiEffect::RhinoArp, 1).wasOk(), "MIDI FX browser action inserts Rhino Arp");
            int arpSlot = -1, synthSlot = -1;
            for (int i = 0; i < parameterTrack->pluginList.size(); ++i)
            {
                auto* plugin = parameterTrack->pluginList[i];
                if (plugin == nullptr) continue;
                if (plugin->getPluginType() == RhinoArpDevice::xmlTypeName) arpSlot = i;
                if (plugin->getPluginType() == te::FourOscPlugin::xmlTypeName) synthSlot = i;
            }
            require(arpSlot >= 0 && synthSlot >= 0 && arpSlot < synthSlot,
                    "Rhino Arp is inserted before the target instrument");
            auto arpParameters = parameterSession.deviceParameters(1, arpSlot);
            require(arpParameters.size() == 3, "Rhino Arp exposes rate, octaves, and gate controls");
            require(parameterSession.addInstrument(Session::Instrument::RhinoWave, 1).wasOk(), "Audio track can host Rhino Wave");
            int waveSlot = -1;
            for (int i = 0; i < parameterTrack->pluginList.size(); ++i)
            {
                auto* plugin = parameterTrack->pluginList[i];
                if (plugin != nullptr && plugin->getPluginType() == RhinoWaveDevice::xmlTypeName)
                    waveSlot = i;
            }
            require(waveSlot >= 0, "Rhino Wave is inserted in the target track chain");
            auto waveParameters = parameterSession.deviceParameters(1, waveSlot);
            require(waveParameters.size() >= 23 && waveParameters[0].name == "Position"
                    && waveParameters[2].name == "Motion" && waveParameters[3].name == "Osc 2"
                    && waveParameters[4].name == "Tune 2" && waveParameters[5].name == "Cutoff"
                    && waveParameters[10].name == "Attack" && waveParameters[17].name == "Output"
                    && waveParameters[18].name == "LFO Rate" && waveParameters[22].name == "LFO Motion",
                    "Rhino Wave exposes a full synth panel surface after the Device View macros");
            const auto developmentForge = juce::File(juce::String(RHINO_SOURCE_DIR))
                .getChildFile("../instruments/rhino-forge/build/RhinoForge_artefacts/Release/VST3/Rhino Forge.vst3");
            if (developmentForge.exists())
                require(parameterSession.isForgeAvailable(), "Rhino discovers the development Forge VST3 bundle");
            if (parameterSession.isForgeAvailable())
            {
                require(parameterSession.addInstrument(Session::Instrument::RhinoForge, 1).wasOk(),
                        "An arrangement drop can put the independent Rhino Forge VST3 on a track");
                require(parameterSession.createClip(1, 0.0).wasOk(),
                        "A clip can then be created on the track routed to Forge");
                int forgeSlot = -1;
                for (int i = 0; i < parameterTrack->pluginList.size(); ++i)
                    if (auto* plugin = parameterTrack->pluginList[i]; plugin != nullptr
                        && plugin->getPluginType() == te::ExternalPlugin::xmlTypeName
                        && plugin->getWrappedAudioProcessor() != nullptr
                        && plugin->getWrappedAudioProcessor()->getName().containsIgnoreCase("Forge"))
                        forgeSlot = i;
                require(forgeSlot >= 0, "Forge is represented by Tracktion's external-plugin wrapper");
                require(parameterTrack->pluginList[forgeSlot]->createEditor() != nullptr,
                        "Tracktion creates Forge's plugin-owned editor for Rhino");
                auto* forgeProcessor = parameterTrack->pluginList[forgeSlot]->getWrappedAudioProcessor();
                require(forgeProcessor != nullptr && forgeProcessor->hasEditor(),
                        "Forge exposes its custom editor through the wrapped VST3 processor");
                std::unique_ptr<juce::AudioProcessorEditor> firstForgeEditor(forgeProcessor->createEditorAndMakeActive());
                require(firstForgeEditor != nullptr && firstForgeEditor->getWidth() >= 900,
                        "Forge custom editor opens on the first direct request");
                firstForgeEditor.reset();
                std::unique_ptr<juce::AudioProcessorEditor> reopenedForgeEditor(forgeProcessor->createEditorAndMakeActive());
                require(reopenedForgeEditor != nullptr && reopenedForgeEditor->getWidth() >= 900,
                        "Forge custom editor reopens after its previous window closes");
                reopenedForgeEditor.reset();
                const auto forgeParameters = parameterSession.deviceParameters(1, forgeSlot);
                require(forgeParameters.size() >= 35,
                        "Tracktion exposes Forge VST3 parameters to Rhino");
                require(parameterSession.addInstrument(Session::Instrument::RhinoForge, 0).wasOk(),
                        "The Forge VST3 can replace the Pattern synth");
                require(parameterSession.projectSnapshot().getProperty("rhinoPatternInstrument").toString() == "forge",
                        "Pattern-grid Forge selection persists in the project state");
            }
