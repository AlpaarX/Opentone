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
    juce::Label position, signature;
};
}
