        {
            Session parameterSession;
            auto* parameterTrack = te::getAudioTracks(*parameterSession.edit)[1];
            require(parameterSession.addAudioEffect(Session::AudioEffect::Reverb).wasOk(), "Audio FX browser action inserts Reverb");
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
            require(parameterSession.addAudioEffect(Session::AudioEffect::ThetaSpace).wasOk(), "Audio FX browser action inserts Theta Space");
            const auto thetaDevices = parameterSession.deviceSlots(1);
            const auto thetaSlot = thetaDevices.empty() ? -1 : thetaDevices.back().pluginIndex;
            auto thetaParameters = parameterSession.deviceParameters(1, thetaSlot);
            require(thetaParameters.size() >= 6, "Theta Space exposes its macro controls");
            require(parameterSession.setDeviceParameter(1, thetaSlot, 0, thetaParameters.front().maximum).wasOk(),
                    "Theta Space macro controls are editable");
            require(parameterSession.addInstrument(Session::Instrument::FourOsc, 1).wasOk(), "Audio track can host a synth instrument");
            require(parameterSession.addMidiEffect(Session::MidiEffect::ThetaArp, 1).wasOk(), "MIDI FX browser action inserts Theta Arp");
            int arpSlot = -1, synthSlot = -1;
            for (int i = 0; i < parameterTrack->pluginList.size(); ++i)
            {
                auto* plugin = parameterTrack->pluginList[i];
                if (plugin == nullptr) continue;
                if (plugin->getPluginType() == ThetaArpDevice::xmlTypeName) arpSlot = i;
                if (plugin->getPluginType() == te::FourOscPlugin::xmlTypeName) synthSlot = i;
            }
            require(arpSlot >= 0 && synthSlot >= 0 && arpSlot < synthSlot,
                    "Theta Arp is inserted before the target instrument");
            auto arpParameters = parameterSession.deviceParameters(1, arpSlot);
            require(arpParameters.size() == 3, "Theta Arp exposes rate, octaves, and gate controls");
            require(parameterSession.addInstrument(Session::Instrument::ThetaWave, 1).wasOk(), "Audio track can host Theta Wave");
            int waveSlot = -1;
            for (int i = 0; i < parameterTrack->pluginList.size(); ++i)
            {
                auto* plugin = parameterTrack->pluginList[i];
                if (plugin != nullptr && plugin->getPluginType() == ThetaWaveDevice::xmlTypeName)
                    waveSlot = i;
            }
            require(waveSlot >= 0, "Theta Wave is inserted in the target track chain");
            auto waveParameters = parameterSession.deviceParameters(1, waveSlot);
            require(waveParameters.size() >= 23 && waveParameters[0].name == "Position"
                    && waveParameters[2].name == "Motion" && waveParameters[3].name == "Osc 2"
                    && waveParameters[4].name == "Tune 2" && waveParameters[5].name == "Cutoff"
                    && waveParameters[10].name == "Attack" && waveParameters[17].name == "Output"
                    && waveParameters[18].name == "LFO Rate" && waveParameters[22].name == "LFO Motion",
                    "Theta Wave exposes a full synth panel surface after the Device View macros");
            const auto developmentForge = juce::File(juce::String(THETA_SOURCE_DIR))
                .getChildFile("../instruments/theta-forge/build/ThetaForge_artefacts/Release/VST3/Theta Forge.vst3");
            if (developmentForge.exists())
                require(parameterSession.isForgeAvailable(), "Theta discovers the development Forge VST3 bundle");
            if (parameterSession.isForgeAvailable())
            {
                require(parameterSession.addInstrument(Session::Instrument::ThetaForge, 1).wasOk(),
                        "An arrangement drop can put the independent Theta Forge VST3 on a track");
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
                        "Tracktion creates Forge's plugin-owned editor for Theta");
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
                        "Tracktion exposes Forge VST3 parameters to Theta");
                require(parameterSession.addInstrument(Session::Instrument::ThetaForge, 0).wasOk(),
                        "The Forge VST3 can replace the Pattern synth");
                require(parameterSession.projectSnapshot().getProperty("thetaPatternInstrument").toString() == "forge",
                        "Pattern-grid Forge selection persists in the project state");
            }
