#include "ForgeTestTools.h"

#include "../src/ForgeProcessor.h"
#include "../src/ForgeEditor.h"
#include "../ui/ForgeVisuals.h"
#include "../src/ForgePresetBrowser.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

namespace rhino::forge::tests
{
namespace
{
// The browser lists a folder on a thread of its own and takes the result on
// the message loop; with no loop running here, this takes it by hand.
void settleListing(rhino::forge::PresetBrowser& browser)
{
    const auto until = juce::Time::getMillisecondCounterHiRes() + 60000.0;
    while (browser.listing && juce::Time::getMillisecondCounterHiRes() < until)
    {
        juce::Thread::sleep(1);
        browser.takeListings();
    }
}

rhino::forge::PresetBrowser* browserOf(juce::Component& editor)
{
    for (auto* child : editor.getChildren())
        if (auto* browser = dynamic_cast<rhino::forge::PresetBrowser*>(child)) return browser;
    return nullptr;
}
}

// Render the real editor without opening a window, for visual review.
int runSnapshot(int argc, char** argv)
{
    auto processor = std::make_unique<rhino::forge::Processor>();
    // Loaded before the editor is built, not after. A panel that draws what is
    // in the patch — a modulation ring, a macro's destination count, the name
    // under it — cannot be reviewed on an empty one, and the editor reads that
    // state as it is constructed. Nothing here runs a message loop, so anything
    // arriving after construction would never be noticed.
    // A `-` here is no preset, so the theme behind it can be named alone.
    if (argc > 7 && juce::String(argv[7]) != "-")
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[7]);
        const auto opened = processor->loadPreset(file);
        if (opened.failed())
        {
            std::cerr << "snapshot: " << opened.getErrorMessage() << '\n';
            return 1;
        }
    }
    // Argument 8 names the theme, chosen before the editor is built because
    // the editor copies the colours as it is constructed. A name Forge does
    // not know is refused rather than defaulted: a typo that quietly rendered
    // Forge would be hashed against the wrong baseline.
    if (argc > 8)
    {
        if (!rhino::forge::ui::knownThemeName(argv[8]))
        {
            std::cerr << "snapshot: unknown theme\n";
            return 2;
        }
        rhino::forge::ui::selectTheme(rhino::forge::ui::themeFromName(argv[8]));
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    if (argc > 4) editor->setSize(juce::String(argv[3]).getIntValue(), juce::String(argv[4]).getIntValue());
    // ARP is not one of the tabs — it is the plate beside the keyboard, and it
    // opens over whichever tab is showing. There is no button to reach for, so
    // the review takes the same route a hand does and presses the plate's
    // name: the lamp and the slot buttons beside it do other things. ARPHELD
    // is the same plate with four of the chord's notes held down long enough
    // to go ultra, beside two short notes that fall the ordinary way.
    const auto held = argc > 5 && juce::String(argv[5]).equalsIgnoreCase("ARPHELD");
    if (argc > 5 && (juce::String(argv[5]).equalsIgnoreCase("ARP") || held))
    {
        const auto at = rhino::forge::ui::arpPlateNameBounds(editor->getLocalBounds())
                            .getCentre().toFloat();
        const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), at,
                                     juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     editor.get(), editor.get(), juce::Time::getCurrentTime(),
                                     at, juce::Time::getCurrentTime(), 1, false);
        editor->mouseDown(click);
        // And a chord through the voices, so the review shows the plate's
        // well with notes falling. Nothing here runs a message loop, so the
        // editor's timer — which is what carries a note from the Processor
        // to the plate — is run by hand once it is due, and the sparks are
        // then given a moment to fall before the picture is taken.
        processor->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> audio(2, 512);
        juce::MidiBuffer midi;
        for (const auto note : {48, 52, 55, 60, 64, 67, 72, 76})
            processor->keyboardState.noteOn(1, note, 0.35f + 0.08f * static_cast<float>(note % 7));
        processor->processBlock(audio, midi);
        juce::Thread::sleep(80);
        juce::Timer::callPendingTimersSynchronously();
        if (held)
        {
            // The buffer still carries the chord the keyboard put into it, so
            // it is emptied before each block rather than played again.
            const auto play = [&processor, &audio, &midi]
            {
                midi.clear();
                processor->processBlock(audio, midi);
            };
            // The timers are run all through a wait, as a message loop would
            // run them. Run once at the end of a sleep, a timer is only called
            // if the timer thread has already counted it down, and a release
            // missed that way is seen seconds late, mid-charge.
            const auto wait = [] (double ms)
            {
                const auto until = juce::Time::getMillisecondCounterHiRes() + ms;
                while (juce::Time::getMillisecondCounterHiRes() < until)
                {
                    juce::Thread::sleep(15);
                    juce::Timer::callPendingTimersSynchronously();
                }
            };
            for (const auto note : {48, 55, 64, 76}) processor->keyboardState.noteOff(1, note, 0.0f);
            play();
            wait(rhino::forge::ui::sparkFallMs + rhino::forge::ui::sparkChargeMs);
            for (const auto note : {57, 69}) processor->keyboardState.noteOn(1, note, 0.6f);
            play();
            for (const auto note : {57, 69}) processor->keyboardState.noteOff(1, note, 0.0f);
            play();
            wait(120.0);
        }
        juce::Thread::sleep(450);
    }
    // PRESETS and SAVE press LOAD and SAVE, which open the browser over the
    // modules. The bank it lists is made beside the picture, in the working
    // directory, from the factory set: a review never writes into the bank in
    // Documents. The Serum 2 and Vital libraries are the machine's own, read.
    // PRESETS:VITAL or PRESETS:SERUM 2 opens that library's row instead, and
    // says how long its folder took to list.
    else if (argc > 5 && (juce::String(argv[5]).startsWithIgnoreCase("PRESETS")
                          || juce::String(argv[5]).equalsIgnoreCase("SAVE")))
    {
        rhino::forge::presetBank::setRootForTests(
            juce::File::getCurrentWorkingDirectory().getChildFile("snapshot-bank"));
        const auto caption = juce::String(argv[5]).equalsIgnoreCase("SAVE") ? "SAVE" : "LOAD";
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<rhino::forge::ui::PresetButton*>(child))
                if (button->getButtonText() == caption && button->onClick) button->onClick();
        const auto library = juce::String(argv[5]).fromFirstOccurrenceOf(":", false, false).toUpperCase();
        if (auto* browser = browserOf(*editor))
        {
            settleListing(*browser);
            if (library.isNotEmpty())
                for (int row = 0; row < static_cast<int>(browser->folderRows.size()); ++row)
                    if (browser->folderRows[static_cast<size_t>(row)].caption == library)
                    {
                        const auto started = juce::Time::getMillisecondCounterHiRes();
                        browser->chooseFolder(row);
                        settleListing(*browser);
                        std::cout << library << ": " << browser->listed.size() << " presets listed in "
                                  << juce::roundToInt(juce::Time::getMillisecondCounterHiRes() - started) << " ms\n";
                    }
        }
    }
    else if (argc > 5)
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<rhino::forge::ui::PageTab*>(child))
                if (tab->getButtonText() == juce::String(argv[5]).toUpperCase() && tab->onClick)
                    tab->onClick();
    const auto scale = argc > 6 ? juce::String(argv[6]).getFloatValue() : 1.0f;
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    auto stream = file.createOutputStream();
    if (stream == nullptr) return 1;
    stream->setPosition(0);
    stream->truncate();
    return juce::PNGImageFormat().writeImageToStream(snapshot, *stream) ? 0 : 1;
    return 0;
}

// What a frame actually costs. --snapshot answers "does it look right"; this
// answers "how long did that take", which is the only honest way to talk about
// resize and tab pacing. Software-rendered, so the absolute numbers are not the
// window's -- the comparison between two builds of the same panel is what it is
// for, and the way to take one is to build the other binary aside and run them
// alternately, because the machine drifts by more than some of these
// differences over a few minutes.
int runProfile(int argc, char** argv)
{
    auto processor = std::make_unique<rhino::forge::Processor>();
    // Argument 6 names a preset, loaded before the editor is built as
    // --snapshot's argument 7 is, so a page is timed with something on it: an
    // empty FX rack draws nothing at all.
    if (argc > 6 && juce::String(argv[6]) != "-")
    {
        const auto opened = processor->loadPreset(juce::File::getCurrentWorkingDirectory().getChildFile(argv[6]));
        if (opened.failed())
        {
            std::cerr << "profile: " << opened.getErrorMessage() << '\n';
            return 1;
        }
    }
    // Argument 4 names the theme, as --snapshot's argument 8 does, so the two
    // looks can be timed against each other from one binary. A `-` keeps the
    // default, so a page can be named alone.
    if (argc > 4 && juce::String(argv[4]) != "-")
    {
        if (!rhino::forge::ui::knownThemeName(argv[4]))
        {
            std::cerr << "profile: unknown theme\n";
            return 2;
        }
        rhino::forge::ui::selectTheme(rhino::forge::ui::themeFromName(argv[4]));
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    const auto width = argc > 2 ? juce::String(argv[2]).getIntValue() : editor->getWidth();
    const auto height = argc > 3 ? juce::String(argv[3]).getIntValue() : editor->getHeight();
    editor->setSize(width, height);

    // Argument 5 names the tab the idle and resize frames are timed on, FX for
    // the effects rack; without it they are the oscillators'.
    if (argc > 5 && juce::String(argv[5]) != "-")
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<rhino::forge::ui::PageTab*>(child))
                if (tab->getButtonText().equalsIgnoreCase(argv[5]) && tab->onClick) tab->onClick();

    juce::Image canvas(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    const auto paintOnce = [&editor, &canvas]
    {
        juce::Graphics g(canvas);
        editor->paint(g);
    };
    // The median frame, not the mean: one rebuild in a run of otherwise
    // cached frames is a thirty-millisecond outlier, and averaging it in
    // reports a cost no frame actually paid. That mistake read as a
    // regression in the idle frame here once already.
    const auto median = [] (int reps, auto&& body)
    {
        std::vector<double> times;
        for (int i = 0; i < reps; ++i)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            body(i);
            times.push_back(juce::Time::highResolutionTicksToSeconds(
                                juce::Time::getHighResolutionTicks() - start) * 1000.0);
        }
        std::sort(times.begin(), times.end());
        return times[times.size() / 2];
    };

    paintOnce();
    std::cout << "panel          " << editor->getWidth() << "x" << editor->getHeight() << "\n";
    // What the 24Hz timer costs when nothing has moved.
    std::cout << "idle frame     " << median(40, [&] (int) { paintOnce(); }) << " ms\n";

    // And what a frame costs while a chord is sounding: the well's sparks, the
    // envelope and LFO readings and every modulated knob's ring are all moving,
    // which is the panel a hand is turning a knob on. The whole panel is timed
    // here, children included, because the knobs repaint with it.
    const auto paintWhole = [&editor, &canvas]
    {
        juce::Graphics g(canvas);
        editor->paintEntireComponent(g, true);
    };
    paintWhole();
    std::cout << "whole panel    " << median(40, [&] (int) { paintWhole(); }) << " ms\n";
    // And what the tick actually repaints of it: the regions that move on
    // their own, with every control left to repaint itself.
    if (auto* panel = dynamic_cast<rhino::forge::Editor*>(editor.get()))
    {
        const auto live = panel->liveRegions();
        std::cout << "tick, live regions  " << median(40, [&] (int)
        {
            juce::Graphics g(canvas);
            g.reduceClipRegion(live);
            editor->paintEntireComponent(g, true);
        }) << " ms\n";
    }
    processor->prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    for (const auto note : {48, 52, 55, 60, 64, 67, 72, 76})
        processor->keyboardState.noteOn(1, note, 0.35f + 0.08f * static_cast<float>(note % 7));
    // A block, then the editor's tick, which is what carries the notes onto
    // the plate. Nothing here runs a message loop, so the timer is pumped by
    // hand once it is due, as --snapshot's ARP page does.
    const auto play = [&]
    {
        midi.clear();
        processor->processBlock(audio, midi);
        juce::Thread::sleep(45);
        juce::Timer::callPendingTimersSynchronously();
    };
    for (int i = 0; i < 4; ++i) play();
    {
        std::vector<double> times;
        for (int i = 0; i < 40; ++i)
        {
            play();
            const auto start = juce::Time::getHighResolutionTicks();
            paintWhole();
            times.push_back(juce::Time::highResolutionTicksToSeconds(
                                juce::Time::getHighResolutionTicks() - start) * 1000.0);
        }
        std::sort(times.begin(), times.end());
        std::cout << "whole panel, chord held  " << times[times.size() / 2] << " ms\n";
    }
    for (const auto note : {48, 52, 55, 60, 64, 67, 72, 76}) processor->keyboardState.noteOff(1, note, 0.0f);
    play();
    // And what one frame of a drag on a window edge costs.
    std::cout << "resize frame   " << median(40, [&] (int i)
    {
        editor->setSize(width - (i % 2), height);
        paintOnce();
    }) << " ms\n";
    editor->setSize(width, height);
    paintOnce();

    std::vector<rhino::forge::ui::EnableLed*> leds;
    for (auto* child : editor->getChildren())
        if (auto* led = dynamic_cast<rhino::forge::ui::EnableLed*>(child)) leds.push_back(led);
    if (!leds.empty())
        std::cout << "module on/off  " << median(20, [&] (int i)
        {
            auto* led = leds[static_cast<size_t>(i) % leds.size()];
            led->setToggleState(!led->getToggleState(), juce::sendNotificationSync);
            paintOnce();
        }) << " ms\n";
    paintOnce();

    std::vector<rhino::forge::ui::PageTab*> pageTabs;
    for (auto* child : editor->getChildren())
        if (auto* tab = dynamic_cast<rhino::forge::ui::PageTab*>(child))
            pageTabs.push_back(tab);
    // Walking every tab in turn, and flipping between two of them, which is
    // what the hand actually does and the case the held layer is for.
    if (!pageTabs.empty())
        std::cout << "tab walk       " << median(20, [&] (int i)
        {
            if (auto& click = pageTabs[static_cast<size_t>(i) % pageTabs.size()]->onClick) click();
            paintOnce();
        }) << " ms\n";
    if (pageTabs.size() > 2)
        std::cout << "tab flip       " << median(20, [&] (int i)
        {
            if (auto& click = pageTabs[static_cast<size_t>(i) % 2 == 0 ? 0 : 2]->onClick) click();
            paintOnce();
        }) << " ms\n";
    return 0;
}

// What the preset browser costs, on this machine: listing each library,
// loading a preset the way a click in the list does, opening the browser, and
// the frames that follow a hover in it. The bank in Documents and the Serum 2
// and Vital folders are the real ones, read and never written, because the
// numbers that matter are the ones the player feels here rather than on a
// scratch bank of three files.
int runProfileBrowser(int argc, char** argv)
{
    const auto now = [] { return juce::Time::getMillisecondCounterHiRes(); };
    const auto timed = [&now] (auto&& body)
    {
        const auto started = now();
        body();
        return now() - started;
    };
    const auto medianOf = [] (std::vector<double> times)
    {
        std::sort(times.begin(), times.end());
        return times.empty() ? 0.0 : times[times.size() / 2];
    };
    const auto print = [] (const char* what, double ms)
    {
        std::cout << what << juce::String(ms, 1) << " ms\n";
    };

    std::cout << "libraries\n";
    for (const auto& library : rhino::forge::presetBank::libraries())
    {
        juce::Array<juce::File> folders;
        const auto walk = timed([&] { folders = rhino::forge::presetBank::foldersUnder(library.root); });
        std::vector<rhino::forge::PresetEntry> entries;
        const auto cold = timed([&] { entries = rhino::forge::presetBank::presetsIn(library.root, true); });
        const auto warm = timed([&] { entries = rhino::forge::presetBank::presetsIn(library.root, true); });
        std::cout << "  " << library.name << ": " << folders.size() << " folders walked in "
                  << juce::String(walk, 1) << " ms; " << entries.size() << " presets listed in "
                  << juce::String(cold, 1) << " ms, listed again in " << juce::String(warm, 1) << " ms\n";
    }

    auto processor = std::make_unique<rhino::forge::Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    const auto width = argc > 2 ? juce::String(argv[2]).getIntValue() : editor->getWidth();
    const auto height = argc > 3 ? juce::String(argv[3]).getIntValue() : editor->getHeight();
    editor->setSize(width, height);
    std::cout << "panel " << editor->getWidth() << "x" << editor->getHeight() << "\n";

    // Every factory preset, through the same call a click in the list makes,
    // with the editor open so its attachments and its tick are paid for too.
    const auto factory = rhino::forge::presetBank::root().getChildFile(rhino::forge::presetBank::factoryFolderName);
    auto factoryFiles = factory.findChildFiles(juce::File::findFiles, true, "*.forgepreset");
    factoryFiles.sort();
    if (!factoryFiles.isEmpty())
    {
        std::vector<double> times;
        for (const auto& file : factoryFiles)
            times.push_back(timed([&] { processor->loadPreset(file); }));
        std::sort(times.begin(), times.end());
        std::cout << "load a factory preset       median " << juce::String(times[times.size() / 2], 1)
                  << " ms, max " << juce::String(times.back(), 1) << " ms, over "
                  << factoryFiles.size() << " presets\n";
    }
    for (const auto& library : rhino::forge::presetBank::libraries())
    {
        if (library.writable) continue;
        const auto found = library.root.findChildFiles(juce::File::findFiles, true,
                                                       library.name.startsWith("VITAL") ? "*.vital" : "*.SerumPreset");
        if (found.isEmpty()) continue;
        std::vector<double> times;
        for (int i = 0; i < juce::jmin(5, found.size()); ++i)
            times.push_back(timed([&] { processor->loadPreset(found[i]); }));
        std::cout << "load a " << library.name << " preset       median " << juce::String(medianOf(times), 1)
                  << " ms (converted on the way in)\n";
    }
    if (!factoryFiles.isEmpty()) processor->loadPreset(factoryFiles[0]);

    rhino::forge::PresetBrowser* browser = nullptr;
    const auto pressLoad = [&editor]
    {
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<rhino::forge::ui::PresetButton*>(child))
                if (button->getButtonText() == "LOAD" && button->onClick) button->onClick();
    };
    const auto opened = timed([&]
    {
        pressLoad();
        browser = browserOf(*editor);
        if (browser != nullptr) settleListing(*browser);
    });
    if (browser == nullptr)
    {
        std::cerr << "profile-browser: LOAD did not open the browser\n";
        return 1;
    }
    print("open the browser, first time     ", opened);
    {
        std::vector<double> times;
        for (int i = 0; i < 5; ++i)
            times.push_back(timed([&] { browser->openToBrowse(); settleListing(*browser); }));
        print("open the browser again           ", medianOf(times));
    }
    for (int row = 0; row < static_cast<int>(browser->folderRows.size()); ++row)
    {
        const auto& folder = browser->folderRows[static_cast<size_t>(row)];
        if (folder.kind != rhino::forge::PresetBrowser::FolderRow::Kind::library) continue;
        std::vector<double> times;
        for (int i = 0; i < 3; ++i)
            times.push_back(timed([&]
            {
                browser->openToBrowse();
                browser->chooseFolder(row);
                settleListing(*browser);
            }));
        std::cout << "open on " << folder.caption << " (" << browser->listed.size() << " presets)   "
                  << juce::String(medianOf(times), 1) << " ms\n";
    }
    browser->openToBrowse();
    settleListing(*browser);

    juce::Image canvas(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    const auto frame = [&editor, &canvas]
    {
        juce::Graphics g(canvas);
        editor->paintEntireComponent(g, true);
    };
    frame();
    {
        std::vector<double> times;
        for (int i = 0; i < 20; ++i) times.push_back(timed(frame));
        print("frame with the browser open, nothing moved   ", medianOf(times));
    }
    {
        std::vector<double> times;
        for (int i = 0; i < 20; ++i)
        {
            browser->repaint();
            times.push_back(timed(frame));
        }
        print("frame after a hover in the list              ", medianOf(times));
    }
    {
        std::vector<double> times;
        for (int i = 0; i < 10; ++i)
            times.push_back(timed([&]
            {
                juce::Graphics g(canvas);
                browser->paintEntireComponent(g, true);
            }));
        print("the browser painted whole                    ", medianOf(times));
    }
    return 0;
}
}
