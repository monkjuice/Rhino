        {
            Session parameterSession;
            auto* parameterTrack = te::getAudioTracks(*parameterSession.edit)[1];
            require(parameterSession.addAudioEffect(Session::AudioEffect::Reverb).wasOk(), "Audio FX browser action inserts Reverb");
            const auto reverbSlot = static_cast<int>(parameterTrack->pluginList.size()) - 1;
            auto reverbParameters = parameterSession.deviceParameters(1, reverbSlot);
            require(!reverbParameters.empty(), "Inserted Reverb exposes editable parameters");
            const auto oldReverbValue = reverbParameters.front().value;
            const auto newReverbValue = oldReverbValue == reverbParameters.front().minimum
                ? reverbParameters.front().maximum : reverbParameters.front().minimum;
            require(parameterSession.beginDeviceParameterGesture(1, reverbSlot, 0).wasOk(), "Device rack starts parameter gestures");
            require(parameterSession.setDeviceParameter(1, reverbSlot, 0, newReverbValue).wasOk(), "Device rack edits effect parameters");
            require(parameterSession.endDeviceParameterGesture(1, reverbSlot, 0).wasOk(), "Device rack ends parameter gestures");
            reverbParameters = parameterSession.deviceParameters(1, reverbSlot);
            require(std::abs(reverbParameters.front().value - newReverbValue) < 0.0001f, "Edited effect parameter value is reflected in the rack");
            require(parameterSession.addAudioEffect(Session::AudioEffect::ThetaSpace).wasOk(), "Audio FX browser action inserts Theta Space");
            const auto thetaSlot = static_cast<int>(parameterTrack->pluginList.size()) - 1;
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
                    "Theta Wave exposes a full synth panel surface after the rack macros");
            const auto developmentForge = juce::File(juce::String(THETA_SOURCE_DIR))
                .getChildFile("../instruments/theta-forge/build/ThetaForge_artefacts/Release/VST3/Theta Forge.vst3");
            if (developmentForge.exists())
                require(parameterSession.isForgeAvailable(), "Theta discovers the development Forge VST3 bundle");
            if (parameterSession.isForgeAvailable())
            {
                require(parameterSession.insertInstrumentClip(Session::Instrument::ThetaForge, 1, 0.0).wasOk(),
                        "An arrangement drop can create a clip routed to the independent Theta Forge VST3");
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
                const auto forgeParameters = parameterSession.deviceParameters(1, forgeSlot);
                require(forgeParameters.size() >= 35,
                        "Tracktion exposes Forge VST3 parameters to Theta");
                require(parameterSession.addInstrument(Session::Instrument::ThetaForge, 0).wasOk(),
                        "The Forge VST3 can replace the Pattern synth");
                require(parameterSession.projectSnapshot().getProperty("thetaPatternInstrument").toString() == "forge",
                        "Pattern-grid Forge selection persists in the project state");
            }
