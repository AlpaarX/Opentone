// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/UiTheme.h"
namespace opentone
{
class LevelMeter final : public juce::Component
{
public:
    LevelMeter() { setInterceptsMouseClicks(false, false); }
    void setLevels(std::pair<float, float> next)
    { if (std::abs(next.first - levels.first) > 0.001f || std::abs(next.second - levels.second) > 0.001f)
      { levels = next; repaint(); } }
    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(background.darker(0.25f));
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto level = channel == 0 ? levels.first : levels.second;
            const auto fraction = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(level, -60.0f) + 60) / 60);
            const auto height = juce::roundToInt(fraction * static_cast<float>(getHeight()));
            graphics.setColour(level >= 1 ? juce::Colour(0xffef6255) : juce::Colour(0xff59b77b));
            graphics.fillRect(channel * getWidth() / 2 + 1, getHeight() - height, std::max(1, getWidth() / 2 - 2), height);
        }
    }
private: std::pair<float, float> levels {0, 0};
};
}
