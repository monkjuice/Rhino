#include "ForgeTestTools.h"

#include "../src/ForgeProcessor.h"
#include "../ui/ForgeVisuals.h"

#include <iostream>
#include <memory>
#include <vector>

namespace rhino::forge::tests
{
// Render the real editor without opening a window, for visual review.
int runSnapshot(int argc, char** argv)
{
    auto processor = std::make_unique<rhino::forge::Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    if (argc > 4) editor->setSize(juce::String(argv[3]).getIntValue(), juce::String(argv[4]).getIntValue());
    if (argc > 5)
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
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    const auto width = argc > 2 ? juce::String(argv[2]).getIntValue() : editor->getWidth();
    const auto height = argc > 3 ? juce::String(argv[3]).getIntValue() : editor->getHeight();
    editor->setSize(width, height);

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
}
