// The two fields the control bar sets its tempo and its time signature with.
// Both are dragged rather than clicked, so the drag is the whole of what has to
// be covered: how far one step is, where the field stops, and the ladder the
// lower half of a signature walks instead of a range.
{
    const auto fieldDrag = [](juce::Component& target, juce::Point<float> down, juce::Point<float> to,
                              juce::ModifierKeys mods)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), to, mods, 1.0f, 0, 0, 0, 0,
                                &target, &target, juce::Time::getCurrentTime(), down,
                                juce::Time::getCurrentTime(), 1, true);
    };
    // Positive pixels is upwards, which is the direction that adds.
    const auto dragBy = [&fieldDrag](juce::Component& target, float pixelsUp, bool fine = false)
    {
        const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier
                                             | (fine ? juce::ModifierKeys::ctrlModifier : 0));
        const auto down = target.getLocalBounds().getCentre().toFloat();
        const auto to = down.translated(0.0f, -pixelsUp);
        target.mouseDown(fieldDrag(target, down, down, mods));
        target.mouseDrag(fieldDrag(target, down, to, mods));
        target.mouseUp(fieldDrag(target, down, to, mods));
    };

    ValueDragBox tempoField;
    tempoField.setSize(106, 42);
    tempoField.setRange(Session::minimumTempo, Session::maximumTempo, 1.0, 0.01);
    tempoField.setValue(120.0);
    // The gesture bracket is what folds a drag into one undo step, so it has to
    // open and close exactly once however many pixels the pointer travelled.
    int gestureDepth = 0, gesturesOpened = 0;
    tempoField.onDragStart = [&gestureDepth, &gesturesOpened] { ++gestureDepth; ++gesturesOpened; };
    tempoField.onDragEnd = [&gestureDepth] { --gestureDepth; };

    dragBy(tempoField, 30.0f);
    require(close(tempoField.value(), 130.0), "Dragging the tempo field up ten steps adds ten BPM");
    require(gestureDepth == 0 && gesturesOpened == 1, "One drag opens and closes exactly one gesture");
    dragBy(tempoField, -30.0f);
    require(close(tempoField.value(), 120.0), "Dragging the same distance back down returns the tempo it started at");
    // Ctrl is hundredths, which is the only way to set a tempo between two
    // whole numbers without typing it.
    dragBy(tempoField, 30.0f, true);
    require(close(tempoField.value(), 120.1), "Ctrl-dragging moves the tempo in hundredths");

    tempoField.setValue(Session::maximumTempo - 1.0);
    dragBy(tempoField, 3000.0f);
    require(close(tempoField.value(), Session::maximumTempo),
            "The field stops at the fastest tempo the model accepts");
    tempoField.setValue(Session::minimumTempo + 1.0);
    dragBy(tempoField, -3000.0f);
    require(close(tempoField.value(), Session::minimumTempo),
            "The field stops at the slowest tempo the model accepts");

    // The wheel is the same step as a drag, one notch at a time.
    tempoField.setValue(120.0);
    juce::MouseWheelDetails notch;
    notch.deltaY = 1.0f;
    const auto over = tempoField.getLocalBounds().getCentre().toFloat();
    tempoField.mouseWheelMove(fieldDrag(tempoField, over, over,
                                        juce::ModifierKeys()), notch);
    require(close(tempoField.value(), 121.0), "A wheel notch is one step");

    TimeSignatureField signature;
    signature.setSize(86, 42);
    std::vector<double> denominators;
    for (const auto denominator : Session::timeSignatureDenominators)
        denominators.push_back(static_cast<double>(denominator));
    signature.setLimits(Session::minimumTimeSignatureNumerator,
                        Session::maximumTimeSignatureNumerator, denominators);
    signature.setSignature(4, 4);
    signature.resized();
    int reportedNumerator = 0, reportedDenominator = 0;
    signature.onChange = [&reportedNumerator, &reportedDenominator](int n, int d)
    {
        reportedNumerator = n;
        reportedDenominator = d;
    };
    require(signature.getNumChildComponents() == 2, "A signature is two fields over one rule");
    auto& upper = *signature.getChildComponent(0);
    auto& lower = *signature.getChildComponent(1);

    dragBy(upper, 9.0f);
    require(signature.numerator() == 7 && signature.denominator() == 4,
            "Dragging the upper number up three steps counts three more beats to the bar");
    require(reportedNumerator == 7 && reportedDenominator == 4,
            "Either half reports the whole signature, because the model takes them together");
    dragBy(upper, 3000.0f);
    require(signature.numerator() == Session::maximumTimeSignatureNumerator,
            "The numerator stops at Live's own limit of 99");
    dragBy(upper, -3000.0f);
    require(signature.numerator() == Session::minimumTimeSignatureNumerator,
            "The numerator stops at one rather than running to zero");

    // The lower number is a ladder, not a range: it steps 1, 2, 4, 8, 16 and
    // has nothing between the rungs.
    signature.setSignature(4, 4);
    dragBy(lower, 10.0f);
    require(signature.denominator() == 8, "One step up the ladder from 4 is 8");
    dragBy(lower, 10.0f);
    require(signature.denominator() == 16, "The next step is 16");
    dragBy(lower, 1000.0f);
    require(signature.denominator() == 16, "The ladder stops at 16");
    dragBy(lower, -1000.0f);
    require(signature.denominator() == 1, "And at 1 going the other way");

    // Every rung the field offers has to be one setTimeSignature accepts, or
    // the control and the model have quietly drifted apart.
    for (const auto denominator : Session::timeSignatureDenominators)
        require(session.setTimeSignature(5, denominator).wasOk(),
                "Every denominator the field steps through is one the model takes");
    require(session.setTimeSignature(4, 4).wasOk(), "Back to four four");
}
