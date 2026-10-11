// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "domain/Project.h"
namespace opentone
{
constexpr int rulerHeight = 32, rowHeight = 72, headerWidth = 234;
extern juce::Colour background, panel, accent, ink, line, primary;
class EditorPresenter;
void showTrackCreationMenu(EditorPresenter&, juce::Component&);
juce::Colour trackColour(const juce::String&, bool selected = false);
juce::MouseCursor bracketCursor(bool right);
void label(juce::Label&, const juce::String&);
juce::String volumeText(double);
void configurePanSlider(juce::Slider&);
void setSliderDefault(juce::Slider&, double);
juce::String assetName(const Asset&);
juce::Path waveformEnvelope(juce::AudioThumbnail&, juce::Rectangle<float>, double from, double to);
std::unique_ptr<juce::LookAndFeel_V4> createUiTheme();
void configureUiTheme(juce::LookAndFeel_V4&);
void loadUiColours();
void saveUiColours();
void resetUiColours();
void setUiThemePreset(bool dark);
int uiThemePreset();
}
