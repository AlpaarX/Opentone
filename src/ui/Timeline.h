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
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
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
    class Returns;
    class LoopBrace;
    class TimeRuler;
    class BeatRuler;
    class Scroll : public juce::Viewport
    { public: std::function<void(juce::Rectangle<int>)> onScroll;
      std::function<void(const juce::MouseEvent&, float)> onZoom;
      juce::Viewport* wheelTarget = nullptr;
      void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
      { if (wheelTarget) wheelTarget->mouseWheelMove(event.getEventRelativeTo(wheelTarget), wheel);
        else if (onZoom && (event.mods.isCommandDown() || event.mods.isCtrlDown())) onZoom(event, std::exp(wheel.deltaY * 2));
        else juce::Viewport::mouseWheelMove(event, wheel); }
      void visibleAreaChanged(const juce::Rectangle<int>& bounds) override { if (onScroll) onScroll(bounds); } };
    EditorPresenter& presenter;
    juce::ToggleButton snap { "Snap" };
    juce::Slider zoom;
    Scroll lanes;
    Scroll headers;
    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<Headers> trackHeaders;
    std::unique_ptr<Master> master;
    std::unique_ptr<Returns> returns;
    std::unique_ptr<LoopBrace> loopBrace;
    std::unique_ptr<TimeRuler> timeRuler;
    std::unique_ptr<BeatRuler> beatRuler;
    juce::TextButton automationButton {"Automation"}, automationLock {"Lock"}, setLocator {"Set"}, previousLocator {"Previous"}, nextLocator {"Next"};
    bool restoring = true; int fixedBraceHeight = 18;
    bool panning = false;
    juce::Point<int> panStart, panScroll;
};
}
