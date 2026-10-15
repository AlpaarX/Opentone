// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "application/EditorPresenter.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace opentone
{
class Transport final : public juce::Component
{
public:
    explicit Transport(EditorPresenter&);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    std::function<void()> onSettings, onMixer;
private:
    class Tempo final : public juce::TextEditor
    {
    public:
        std::function<void(int)> onStep;
        void mouseDoubleClick(const juce::MouseEvent&) override { setText("120", false); if (onReturnKey) onReturnKey(); }
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        { if (wheel.deltaY != 0 && onStep) onStep(wheel.deltaY > 0 ? 1 : -1); }
    } bpm;
    EditorPresenter& presenter;
    juce::TextButton play {"Play"}, stop {"Stop"}, metronome {"Metronome"}, record {"Record"}, loop {"Loop"};
    juce::TextEditor position, signature, loopStart, loopLength;
    class LinkButton final : public juce::TextButton
    {
    public:
        LinkButton() : TextButton("Link") {}
        std::function<void()> onContext;
        void mouseDown(const juce::MouseEvent& event) override
        { if (event.mods.isPopupMenu()) { if (onContext) onContext(); } else juce::TextButton::mouseDown(event); }
    } link;
    juce::TextButton tap {"Tap"}, follow {"Follow"}, overdub {"Overdub"}, automationArm {"Automation"};
    juce::TextButton punchIn {"Punch In"}, punchOut {"Punch Out"}, draw {"Draw"}, keyboard {"Keyboard"}, keyMap {"Key"}, midiMap {"MIDI"};
    juce::TextButton sampleRate {"48 kHz"}, mixer {"Mixer"}, nudgeDown {"-"}, nudgeUp {"+"};
    juce::ComboBox quantization, scaleRoot, scaleMode;
    juce::TextButton scale {"Scale"}; juce::Slider groove;
    std::optional<double> nudgeBefore;
    double barsTime(const juce::String&, bool length) const;
    juce::String barsText(double, bool length = false) const;
    void editTime(juce::TextEditor&, bool length, std::function<void(double)>);
};
}
