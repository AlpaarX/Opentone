// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "domain/Project.h"
namespace opentone
{
constexpr int rulerHeight = 24, rowHeight = 88, headerWidth = 304;
extern juce::Colour background, panel, accent, ink, line, primary;
enum class UiTone { control, display, range, selectionText, pianoBlack, pianoWhite, detail, record, solo, play, editField };
juce::Colour uiTone(UiTone);
class EditorPresenter;
class TrackVolumeSlider final : public juce::Slider
{
public:
    TrackVolumeSlider(EditorPresenter&, juce::String track);
private:
    void valueChanged() override;
    juce::WeakReference<EditorPresenter> presenter; juce::String track;
};
class SendSlider final : public juce::Slider
{
public:
    void bind(EditorPresenter&, juce::String track, int index, int height);
    void refresh();
    void mouseDown(const juce::MouseEvent&) override;
private:
    juce::WeakReference<EditorPresenter> presenter; juce::String track; int index = 0;
};
void showTrackCreationMenu(EditorPresenter&, juce::Component&);
juce::Colour trackColour(const Project&, const juce::String&, bool selected = false);
juce::Colour clipColour(const Project&, const Clip&);
juce::MouseCursor bracketCursor(bool right);
void drawArrangementGrid(juce::Graphics&, juce::Rectangle<int>, const Project&, double zoom, int scrollX = 0);
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
