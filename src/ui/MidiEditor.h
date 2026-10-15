// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "application/EditorPresenter.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace opentone
{
class MidiEditor final : public juce::Component, public juce::FileDragAndDropTarget,
                         public juce::DragAndDropTarget, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MidiEditor(EditorPresenter&);
    ~MidiEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void setContext(juce::String track, juce::String clip, bool piano);
    void cancelGesture();
    bool canReload() const { return !choosing; }
    bool pianoVisible() const { return showPiano; }
    static constexpr int instrumentHeight = 218;
    void scrollDevices(double proportion);
    void paintDeviceOverview(juce::Graphics&, juce::Rectangle<float>) const;
    std::pair<double, double> deviceViewRange() const;
    void setKeyboardNotes(std::bitset<128> notes);
    bool noteFocused() const;
    void selectAllNotes();
    void copyNotes();
    void pasteNotes();
    void duplicateNotes();
    std::function<void(juce::Component&)> onChooseClip;
    std::function<void(juce::Component&, bool instrument)> onAddDevice;
    void mouseDown(const juce::MouseEvent&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray&, int, int) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
private:
    class Roll;
    class Waveform;
    class DeviceHeader;
    class PluginCard;
    class DeviceSurface;
    void refresh();
    void chooseSource();
    void setZoom(double zoom, int keyHeight, juce::Point<int> anchor);
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    EditorPresenter& presenter;
    juce::String trackId, clipId;
    juce::TextButton load { "Load" }, clips { "Use clip" };
    juce::Label source, rootLabel, volumeLabel, startLabel, endLabel;
    juce::Slider root, volume, sampleStart, sampleEnd;
    juce::ToggleButton loopToggle {"Loop"};
    juce::Label loopStartLabel, loopLengthLabel, clipLengthLabel;
    juce::Slider loopStart, loopLength, clipLength, horizontalZoom, verticalZoom;
    juce::Label horizontalLabel, verticalLabel, dropEffects;
    juce::Viewport viewport, deviceViewport;
    juce::Component deviceContent;
    std::vector<std::unique_ptr<PluginCard>> pluginCards;
    std::unique_ptr<Roll> roll;
    std::unique_ptr<Waveform> waveform;
    std::unique_ptr<DeviceHeader> deviceHeader;
    std::unique_ptr<DeviceSurface> samplerSurface;
    std::unique_ptr<juce::FileChooser> chooser;
    bool showPiano = false, choosing = false;
    std::bitset<128> keyboardNotes, activeNotes;
};
}
