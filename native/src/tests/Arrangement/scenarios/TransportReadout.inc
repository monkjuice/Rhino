// The control bar's readout, at the widest and the narrowest the layout will
// ever hand it. It is three columns now rather than two lines: the position
// against the left edge, the tempo, signature and clock beside it, and the
// loop and the load meters against the right. Two rules are worth holding on
// to. The position never moves, whatever the box is doing around it - it is a
// clock, and a digit's worth of drift reads as the clock itself moving. And
// the readings that will not fit are dropped whole, lowest priority first,
// rather than truncated into each other.
{
    Theme readoutTheme;
    juce::LookAndFeel::setDefaultLookAndFeel(&readoutTheme);
    TransportDisplay readout;
    readout.setPosition("001.1.1");
    readout.setTempoAndSignature("120", "4/4");
    readout.setClock("00:08:260  /  01:08:597");
    readout.setLoop("001.1.1 - 035.1.0");
    readout.setStatistics("CPU 6%   RAM 249 MB");
    readout.setDeviceInfo("48.0 kHz   256   5.3 ms");

    // The first and last column holding text in a band of the box. The
    // threshold clears the inset ground at 0xff101010 and the border at
    // 0xff3a3a3a while still catching the dimmest reading, 0xff76856f.
    const auto lit = [](const juce::Image& image, int x, int y)
    {
        return image.getPixelAt(x, y).getBrightness() > 0.35f;
    };
    const auto litColumns = [&lit](const juce::Image& image, int left, int right)
    {
        std::pair<int, int> span {-1, -1};
        for (int x = left; x < right; ++x)
            for (int y = 1; y < image.getHeight() - 1; ++y)
                if (lit(image, x, y))
                {
                    if (span.first < 0) span.first = x;
                    span.second = x;
                    break;
                }
        return span;
    };
    const auto litRows = [&lit](const juce::Image& image, int left, int right)
    {
        std::pair<int, int> span {-1, -1};
        for (int y = 1; y < image.getHeight() - 1; ++y)
            for (int x = left; x < right; ++x)
                if (lit(image, x, y))
                {
                    if (span.first < 0) span.first = y;
                    span.second = y;
                    break;
                }
        return span;
    };

    // Wide enough for all three columns. The chevron is a child component and
    // is caught by a snapshot, so every scan stops short of it.
    readout.setSize(760, 38);
    const auto wide = readout.createComponentSnapshot(readout.getLocalBounds());
    require(wide.getWidth() == 760 && wide.getHeight() == 38, "The readout renders at the size it is given");
    const auto wideText = litColumns(wide, 1, 720);
    // Inside the padding, plus whatever left side bearing the first digit
    // carries - which is why this is a band rather than the exact sixteen.
    require(wideText.first >= 16 && wideText.first <= 22, "The position starts at the box's own padding");
    require(wideText.second > 700, "The right-hand column is set against the right edge");

    // The hierarchy the bar is built around: the position is set to be read
    // across a desk and everything beside it to be watched out of the corner
    // of the eye. Measured rather than asserted from the font sizes, because
    // what matters is the height on screen.
    const auto positionHeight = litRows(wide, 16, 70);
    const auto readingHeight = litRows(wide, 640, 720);
    require(positionHeight.second - positionHeight.first
                > (readingHeight.second - readingHeight.first) / 2,
            "The position is set larger than the readings beside it");

    // Narrow enough that the right column cannot be held as well as the
    // position and the clock. It goes whole, and the position does not shift
    // by a pixel to make room.
    readout.setSize(300, 38);
    const auto narrow = readout.createComponentSnapshot(readout.getLocalBounds());
    const auto narrowText = litColumns(narrow, 1, 260);
    require(narrowText.first == wideText.first, "Narrowing the box does not move the position");
    require(narrowText.second < 260, "The readings that will not fit are dropped, not run together");

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
