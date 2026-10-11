// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "application/EditorPresenter.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace opentone
{
class Mixer final : public juce::Component
{
public:
    explicit Mixer(EditorPresenter&);
    ~Mixer() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void tick();
    bool trackFocused() const;
private:
    class Strip;
    class Channels;
    class Scroll final : public juce::Viewport
    { public: std::function<void(juce::Rectangle<int>)> onScroll;
      void visibleAreaChanged(const juce::Rectangle<int>& area) override { if (onScroll) onScroll(area); } };
    EditorPresenter& presenter;
    Scroll tracks, masterView;
    std::unique_ptr<Channels> channels;
    std::unique_ptr<Strip> master;
    bool synchronising = false;
};
}
