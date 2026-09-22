// The control bar's readout, at the widest and the narrowest the layout will
// ever hand it. Each line now carries a run against the left edge and a
// second against the right, and the rule between them is what is worth
// checking: the right run appears when there is room for it and vanishes
// whole when there is not, without moving the position or the clock a pixel.
{
    Theme readoutTheme;
    juce::LookAndFeel::setDefaultLookAndFeel(&readoutTheme);
    TransportDisplay readout;
    readout.setDisplayText("005  1     120     4/4");
    readout.setSecondaryText("00:08:260 / 01:08:597   CPU 6%   RAM 249 MB");
    readout.setTrailingText("LOOP  001 - 035");
    readout.setSecondaryTrailingText("48.0 kHz   256   5.3 ms");

    // The first and last column holding text in a band of the box. The
    // threshold clears the background at 0xff101418 and the border at
    // 0xff3f4c55 while still catching the dimmest reading, 0xff76856f.
    const auto litSpan = [](const juce::Image& image, int top, int bottom, int right)
    {
        std::pair<int, int> span {-1, -1};
        for (int x = 1; x < right; ++x)
            for (int y = top; y < bottom; ++y)
                if (image.getPixelAt(x, y).getBrightness() > 0.35f)
                {
                    if (span.first < 0) span.first = x;
                    span.second = x;
                    break;
                }
        return span;
    };

    readout.setSize(400, 56);
    const auto wide = readout.createComponentSnapshot(readout.getLocalBounds());
    require(wide.getWidth() == 400 && wide.getHeight() == 56, "The readout renders at the size it is given");
    // Text is inset by eight and stops short of the chevron at 380.
    const auto widePosition = litSpan(wide, 4, 30, 372);
    const auto wideClock = litSpan(wide, 34, 52, 372);
    require(widePosition.first == 8, "The position starts at the left edge of the box");
    require(wideClock.first == 8, "The clock starts under the position");
    require(widePosition.second > 340 && widePosition.second < 372, "The loop is drawn against the right edge");
    require(wideClock.second > 340 && wideClock.second < 372, "The device reading is drawn against the right edge");

    // Narrow enough that the second line cannot hold both of its runs. The
    // clock keeps the line and the device reading goes, rather than the two
    // being squeezed into each other.
    readout.setSize(300, 56);
    const auto narrow = readout.createComponentSnapshot(readout.getLocalBounds());
    const auto narrowPosition = litSpan(narrow, 4, 30, 272);
    const auto narrowClock = litSpan(narrow, 34, 52, 272);
    require(narrowPosition.first == widePosition.first, "Narrowing the box does not move the position");
    require(narrowClock.first == wideClock.first, "Narrowing the box does not move the clock");
    require(narrowPosition.second > 240, "The loop still fits beside the position");
    require(narrowClock.second < 240, "The device reading is dropped rather than run into the clock");

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
