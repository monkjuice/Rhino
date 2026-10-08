#include "DeviceRackTest.h"
#include "../../DeviceRack.h"
#include "../../DrumRackWindow.h"
#include "../../Session.h"
#include "../../Theme.h"
#include "ContentLibrary.h"
#include "instruments/DrumRackDevice.h"
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace rhino
{
void runDrumRackWindowTest()
{
    const auto require = [](bool valid, const juce::String& message)
    {
        if (!valid) throw std::runtime_error(("Drum Rack window: " + message).toStdString());
    };
    Session session;
    require(session.addDrumKit(ContentLibrary::file("Drums/Kits/808 Kit.rdk"), 0).wasOk(),
            "the first MIDI track takes the 808 kit");
    DeviceRack rack(session);
    juce::Component* heard = nullptr;
    juce::KeyPress forwarded;
    rack.listenForKeys = [&heard] (juce::Component& window) { heard = &window; };
    rack.shortcut = [&forwarded] (const juce::KeyPress& key) { forwarded = key; return true; };
    rack.selectTrack(0);
    rack.setSize(1400, 280);
    DeviceEditorPanel* inRack = nullptr;
    auto drumsAt = -1;
    for (int i = 0; i < rack.devicePanels.size(); ++i)
        if (rack.devicePanels[i]->showsDrumRack())
        {
            inRack = rack.devicePanels[i];
            drumsAt = i;
        }
    require(inRack != nullptr && inRack->openInWindow != nullptr, "the rack's Drum Rack face can open a window");
    const auto slot = inRack->devicePluginIndex();
    auto* drums = dynamic_cast<DrumRackDevice*>(session.devicePlugin(0, slot));
    require(drums != nullptr, "the face shows the rack on the track");
    const auto rackId = drums->itemID;

    // Every point of a face where a test holds, as one rectangle.
    const auto areaWhere = [] (const DeviceEditorPanel& panel, int step, const std::function<bool(juce::Point<int>)>& holds)
    {
        juce::Rectangle<int> area;
        for (int y = 0; y < panel.getHeight(); y += step)
            for (int x = 0; x < panel.getWidth(); x += step)
                if (holds({x, y}))
                    area = area.isEmpty() ? juce::Rectangle<int>(x, y, step, step) : area.getUnion({x, y, step, step});
        return area;
    };
    const auto mouse = [] (DeviceEditorPanel& panel, juce::Point<int> at, juce::Point<int> down)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(),
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                &panel, &panel, juce::Time::getCurrentTime(), down.toFloat(), juce::Time::getCurrentTime(),
                                1, at != down);
    };
    const auto click = [&mouse] (DeviceEditorPanel& panel, juce::Point<int> at)
    {
        panel.mouseDown(mouse(panel, at, at));
        panel.mouseUp(mouse(panel, at, at));
    };

    // The button at the right end of the rack face's name bar opens the
    // window. The rack is not on the screen here, so neither is the window.
    const auto button = areaWhere(*inRack, 1, [&inRack] (juce::Point<int> at)
    {
        return at.y < DeviceEditorPanel::headerHeight && inRack->drumTooltip(at).startsWith("Open the Drum Rack in a window");
    });
    require(!button.isEmpty() && button.getX() > inRack->getWidth() - 40,
            "the window button stands at the right end of the name bar");
    click(*inRack, button.getCentre());
    require(rack.drumWindow != nullptr && rack.drumWindow->view().device() == rackId && !rack.drumWindow->isVisible(),
            "the button opens the rack in a window of its own, unshown from a rack off the screen");
    require(heard == rack.drumWindow.get(), "the typing keyboard listens to the window");
    require(rack.drumWindow->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && forwarded.getKeyCode() == juce::KeyPress::spaceKey,
            "and the shell's shortcuts answer there");
    require(rack.drumWindow->getName().contains(session.trackName(0)), "the window is named for the rack and its track");
    auto* window = rack.drumWindow.get();
    rack.selectDevice(drumsAt);
    rack.openSelectedDevice();
    require(rack.drumWindow != nullptr && rack.drumWindow->view().device() == rackId && rack.floatingWindow == nullptr,
            "Edit on a Drum Rack opens the same window rather than the fallback's six knobs");
    window = rack.drumWindow.get();

    // The window's face: the same rack, stacked, its pads above and the
    // selected pad's side across the whole width below.
    auto& view = rack.drumWindow->view();
    auto& face = view.face;
    view.setSize(1280, 800);
    require(face.showsDrumRack() && face.devicePluginIndex() == slot && face.openInWindow == nullptr
                && face.getHeight() >= DeviceEditorPanel::drumStackedHeight,
            "the window shows the same rack, tall enough to stack, with no window button of its own");
    // Where each part is, found by asking the face in one pass: the pads by
    // the note under a point, the rest by what the face says a point is.
    constexpr int shown = DrumRackDevice::bankSize;
    const auto first = drums->firstShownNote();
    std::array<juce::Rectangle<int>, shown> pads;
    juce::Rectangle<int> map, picture, classic;
    const auto grow = [] (juce::Rectangle<int>& area, juce::Point<int> at)
    {
        area = area.isEmpty() ? juce::Rectangle<int>(at.x, at.y, 2, 2) : area.getUnion({at.x, at.y, 2, 2});
    };
    for (int y = 0; y < face.getHeight(); y += 2)
        for (int x = 0; x < face.getWidth(); x += 2)
        {
            if (const auto note = face.drumPadAt({x, y}); juce::isPositiveAndBelow(note - first, shown))
                grow(pads[static_cast<size_t>(note - first)], {x, y});
            const auto tip = face.drumTooltip({x, y});
            if (tip.startsWith("All 128 notes"))
                grow(map, {x, y});
            else if (tip.startsWith("The sample, the part a strike plays lit"))
                grow(picture, {x, y});
            else if (tip.startsWith("Classic:"))
                grow(classic, {x, y});
        }
    for (size_t pad = 0; pad < pads.size(); ++pad)
    {
        require(!pads[pad].isEmpty() && face.getLocalBounds().contains(pads[pad])
                    && pads[pad].getY() > DeviceEditorPanel::headerHeight && !pads[pad].intersects(map),
                "pad " + juce::String(static_cast<int>(pad) + 1) + " stands on the face, below the name bar, clear of the map");
        require(pads[pad].getWidth() > 120 && pads[pad].getHeight() > 60, "and is bigger than the rack's");
        require(picture.getY() > pads[pad].getBottom(), "and the sample's picture stands below it");
        for (auto other = pad + 1; other < pads.size(); ++other)
            require(!pads[pad].intersects(pads[other]), "no two pads overlap");
    }
    require(pads[0].getX() < pads[1].getX() && pads[0].getY() > pads[4].getY() && pads[15].getY() < pads[11].getY(),
            "the lowest note sits bottom left, and the pads rise along a row and then up");
    require(!map.isEmpty() && map.getRight() < pads[0].getX() && map.getHeight() > 200,
            "the map stands left of the pads, taller than the rack's");
    require(picture.getWidth() > face.getWidth() * 3 / 4 && !classic.isEmpty() && classic.getRight() < picture.getX(),
            "the picture runs most of the window's width, the modes beside it");
    auto knobs = 0;
    for (auto* sliders : { &face.drumSliders, &face.drumSampleSliders })
        for (auto* knob : *sliders)
            if (knob->isVisible())
            {
                ++knobs;
                require(face.getLocalBounds().contains(knob->getBounds()) && knob->getY() > picture.getBottom()
                            && knob->getWidth() > 34,
                        "every knob stands under the picture, larger than the rack's");
                for (auto* other : face.drumSampleSliders)
                    require(other == knob || !other->isVisible() || !other->getBounds().intersects(knob->getBounds()),
                            "and clear of the others");
            }
    require(knobs == DrumRackDevice::controlCount + 4, "the pad's six knobs and a one-shot's four");

    // A knob on a lane follows the engine in the window as in the rack: the
    // kick's Tune, ramped from the bottom of its range to the middle.
    const auto tuneIndex = DrumRackDevice::parameterIndex(first, DrumRackDevice::tune);
    const auto tune = session.deviceParameter(0, slot, tuneIndex);
    require(tune.has_value(), "the kick's Tune can be read");
    const auto quarter = tune->minimum + (tune->maximum - tune->minimum) * 0.25f;
    require(session.setTrackAutomationPoints({0, slot, tuneIndex},
                                             {{0.0, tune->minimum}, {2.0, (tune->minimum + tune->maximum) * 0.5f}}).wasOk()
                && face.followsAutomation(),
            "a lane on the kick's Tune is one the window's face follows");
    for (auto* parameter : drums->getAutomatableParameters())
        if (parameter->hasAutomationPoints())
            parameter->updateToFollowCurve(tracktion::core::TimePosition::fromSeconds(1.0));
    view.followAutomation();
    require(std::abs(face.parameters[static_cast<size_t>(tuneIndex)].value - quarter) < 0.05f,
            "the window shows where the engine has moved it: "
                + juce::String(face.parameters[static_cast<size_t>(tuneIndex)].value, 2));
    require(session.clearTrackAutomationPoints({0, slot, tuneIndex}).wasOk() && !face.followsAutomation(),
            "and stops following once the lane is cleared");

    // Two faces show one rack now. A pad picked in the window is the one the
    // rack's face shows from its next frame, so neither face's knobs turn a
    // pad other than the one they show.
    click(face, pads[5].getTopLeft() + juce::Point<int>(10, 8));
    require(drums->selectedPad() == first + 5 && face.drumSliders[0]->getTooltip().startsWith("F2 Tune:"),
            "a pad clicked in the window is selected there");
    inRack->tickDrums();
    require(inRack->drumSliders[0]->getTooltip().startsWith("F2 Tune:"), "and the rack's face catches up on its next frame");
    click(face, pads[0].getTopLeft() + juce::Point<int>(10, 8));
    inRack->tickDrums();
    require(drums->selectedPad() == first && inRack->drumSliders[0]->getTooltip().startsWith("C2 Tune:"), "and back to C2");

    // The map's bigger cells still say which bank they show.
    click(face, {map.getCentreX(), map.getY() + 1});
    require(drums->firstShownNote() == DrumRackEngine::padCount - shown, "the map's top shows the highest bank");
    click(face, {map.getCentreX(), map.getBottom() - 2});
    require(drums->firstShownNote() == 0, "and its bottom the lowest");
    require(session.showDrumBank(0, slot, first).wasOk() && drums->firstShownNote() == first, "and back to C2");

    // Sounds land on the window's pads as on the rack's.
    const auto inView = [&view, &face] (juce::Point<int> local) { return view.getLocalPoint(&face, local); };
    const auto bongo = ContentLibrary::file("Samples/VinylDrums/Percussion/Bongo 02 High.wav");
    const auto sampleDrop = "rhino-browser:file:" + bongo.getFullPathName();
    require(view.isInterestedInDragSource({sampleDrop, nullptr, inView(pads[9].getCentre())}), "the window takes a sample");
    view.itemDragMove({sampleDrop, nullptr, inView(pads[9].getCentre())});
    require(face.drumDropTarget == first + 9, "a sample held over a pad lights it");
    view.itemDropped({sampleDrop, nullptr, inView(pads[9].getCentre())});
    require(face.drumDropTarget == -1 && drums->pad(first + 9).sound.has_value()
                && drums->pad(first + 9).sound->sample.contains("Bongo 02"),
            "and dropped there, the pad plays it");
    const auto clap = ContentLibrary::file("Drums/Presets/Clap/Analog Clap.rdp");
    view.itemDropped({"rhino-browser:drum-preset:" + clap.getFullPathName(), nullptr, inView(pads[13].getCentre())});
    require(drums->pad(first + 13).sound.has_value() && drums->pad(first + 13).sound->name == "Analog Clap",
            "a drum preset becomes a pad's sound");
    const auto roll = ContentLibrary::file("Samples/VinylDrums/Snare/Snare Roll 02 Crisp Tight.wav");
    view.filesDropped({bongo.getFullPathName(), roll.getFullPathName()}, inView(pads[2].getCentre()).x,
                      inView(pads[2].getCentre()).y);
    require(drums->pad(first + 2).sound->sample.contains("Bongo 02") && drums->pad(first + 3).sound->sample.contains("Snare Roll"),
            "files from the desktop fill pads upward from the one dropped on");
    const auto devicesBefore = session.deviceSlots(0).size();
    view.itemDropped({"rhino-browser:drumkit:" + ContentLibrary::file("Drums/Kits/Analog Kit.rdk").getFullPathName(),
                      nullptr, inView(face.getLocalBounds().getCentre())});
    require(session.deviceSlots(0).size() == devicesBefore && drums->pad(first + 9).sound->source == DrumRackEngine::Source::synth,
            "a kit loads into the rack");
    session.undo();
    require(drums->pad(first + 9).sound->sample.contains("Bongo 02"), "and an undo takes it back");

    // The rack's face and the window draw one pad at two widths, and the
    // device keeps both pictures rather than drawing them again by turns.
    const auto& narrow = drums->samplePicture(first, 300);
    const auto& wide = drums->samplePicture(first, 1100);
    require(&narrow != &wide && narrow.highs.size() == 300 && wide.highs.size() == 1100,
            "a pad's picture is kept at two widths at once");
    require(&drums->samplePicture(first, 700) == &narrow && &drums->samplePicture(first, 1100) == &wide,
            "and a third width takes the place of the one asked for longer ago");

    // A sliced roll, for the picture.
    require(session.loadDrumPadSample(0, slot, first + 3, roll).wasOk(), "a roll on a pad");
    drums->setSelectedPad(first + 3);
    auto sliced = drums->pad(first + 3).sound->playback;
    sliced.mode = DrumRackEngine::PlayMode::slice;
    session.editDeviceSettings(0, slot, "Slice the roll", [drums, sliced, note = first + 3] { drums->setPadPlayback(note, sliced); });
    Theme theme;
    view.setLookAndFeel(&theme);
    const auto snapshot = view.createComponentSnapshot(view.getLocalBounds());
    view.setLookAndFeel(nullptr);
    require(snapshot.getWidth() == 1280 && snapshot.getHeight() == 800, "the window paints at its size");
    if (const auto path = juce::SystemStats::getEnvironmentVariable("RHINO_DRUM_WINDOW_SNAPSHOT", {}); path.isNotEmpty())
    {
        const juce::File file(path);
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            require(juce::PNGImageFormat().writeImageToStream(snapshot, *stream), "the window snapshot is writable");
        else
            require(false, "the window snapshot path is writable");
    }

    // The window follows its rack, not a place in a chain: a MIDI effect in
    // front of it moves the rack along, and a track moved moves it to
    // another index.
    const auto rackAt = [&session, rackId] (int track)
    {
        for (const auto& each : session.deviceSlots(track))
            if (const auto* plugin = session.devicePlugin(track, each.pluginIndex); plugin != nullptr && plugin->itemID == rackId)
                return each.pluginIndex;
        return -1;
    };
    require(session.addDevice("RhinoArp", 0).wasOk() && rackAt(0) != slot, "an arpeggiator stands in front of the rack");
    require(rack.drumWindow.get() == window && face.devicePluginIndex() == rackAt(0), "and the window follows the rack along");
    require(session.moveTrack(0, 2).wasOk() && rackAt(2) >= 0, "the rack's track moves down two");
    require(rack.drumWindow.get() == window && view.track == 2 && face.devicePluginIndex() == rackAt(2),
            "and the window goes with it");
    session.undo();
    session.undo();
    require(rack.drumWindow.get() == window && view.track == 0 && face.devicePluginIndex() == slot,
            "undone, it is back where it started");

    // It goes when the rack leaves the edit, and with the document.
    require(session.deleteDevice(0, slot).wasOk() && rack.drumWindow == nullptr, "deleting the rack closes its window");
    session.undo();
    require(rackAt(0) == slot && rack.drumWindow == nullptr, "and an undo brings the rack back, not the window");
    rack.sync();
    rack.openDrumWindow(slot);
    require(rack.drumWindow != nullptr, "the window opens again");
    session.newProject();
    require(rack.drumWindow == nullptr, "and a new document closes it");
}
}
