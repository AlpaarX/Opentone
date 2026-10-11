// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "application/EditorPresenter.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace opentone
{
class Timeline final : public juce::Component
{
public:
    explicit Timeline(EditorPresenter&);
    ~Timeline() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    void tick();
    void deleteSelection();
    bool trackFocused() const;
    void cancelGesture();
    bool automationCommand(char);
    std::function<void(const juce::StringArray&, juce::String, double)> onFiles;
    std::function<void(juce::String)> onMidiClip;
private:
    class Canvas;
    class Headers;
    class Master;
    class LoopBrace;
    class Scroll : public juce::Viewport
    { public: std::function<void(juce::Rectangle<int>)> onScroll;
      void visibleAreaChanged(const juce::Rectangle<int>& bounds) override { if (onScroll) onScroll(bounds); } };
    EditorPresenter& presenter;
    juce::ToggleButton snap { "Snap" };
    juce::Slider zoom;
    Scroll lanes;
    juce::Viewport headers;
    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<Headers> trackHeaders;
    std::unique_ptr<Master> master;
    std::unique_ptr<LoopBrace> loopBrace;
    bool restoring = true;
};
}
