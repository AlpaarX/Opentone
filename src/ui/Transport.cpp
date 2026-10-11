// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Transport.h"
#include "ui/UiTheme.h"
namespace opentone
{
Transport::Transport(EditorPresenter& owner) : presenter(owner)
{
    for (auto* component : std::initializer_list<juce::Component*> { &play, &stop, &position, &bpm, &signature, &metronome, &record, &loop }) addAndMakeVisible(component);
    label(signature, "4 / 4"); signature.setFont(juce::FontOptions(11)); signature.setJustificationType(juce::Justification::centred);
    position.setJustificationType(juce::Justification::centred);
    bpm.setJustification(juce::Justification::centred); bpm.setFont(juce::FontOptions(11)); bpm.setIndents(2, 1);
    bpm.setTitle("Tempo in BPM; scroll to change by one"); bpm.setText("120");
    bpm.onReturnKey = [this]
    {
        auto text = bpm.getText().trim(); char* end = nullptr; const auto value = std::strtod(text.toRawUTF8(), &end);
        if (end != text.toRawUTF8() && *end == 0) presenter.setBpm(value);
        refresh(); giveAwayKeyboardFocus();
    };
    bpm.onFocusLost = [this] { bpm.onReturnKey(); };
    bpm.onStep = [this](int direction)
    { const auto next = std::max(1.0, std::round(presenter.project().bpm) + direction); presenter.setBpm(next); bpm.setText(juce::String(next, 0), false); };
    play.setTitle("Play / pause"); stop.setTitle("Stop"); play.onClick = [this] { presenter.togglePlayback(); }; stop.onClick = [this] { presenter.stop(); };
    loop.setTitle("Arrangement loop; Cmd/Ctrl+L loops the selection"); loop.onClick = [this] { presenter.toggleLoop(); };
    record.setTitle("Record armed audio and MIDI tracks; click again to finish the take"); record.onClick = [this] { presenter.toggleRecording(); };
    metronome.setTitle("Metronome"); metronome.setClickingTogglesState(true);
    metronome.onClick = [this] { presenter.setMetronome(metronome.getToggleState()); };
}
void Transport::paint(juce::Graphics& graphics)
{ graphics.fillAll(panel); graphics.setColour(line); graphics.drawHorizontalLine(getHeight() - 1, 0, static_cast<float>(getWidth())); }
void Transport::resized()
{
    auto bounds = getLocalBounds().reduced(8, 4); bpm.setBounds(bounds.removeFromLeft(50)); bounds.removeFromLeft(6);
    signature.setBounds(bounds.removeFromLeft(34)); bounds.removeFromLeft(6); metronome.setBounds(bounds.removeFromLeft(22));
    const auto centre = getWidth() / 2;
    position.setBounds(centre - 82, 4, 70, getHeight() - 8); play.setBounds(centre - 6, 4, 20, getHeight() - 8); stop.setBounds(centre + 18, 4, 20, getHeight() - 8); record.setBounds(centre + 42, 4, 20, getHeight() - 8); loop.setBounds(centre + 66, 4, 20, getHeight() - 8);
}
void Transport::refresh()
{
    signature.setColour(juce::Label::backgroundColourId, background.brighter(0.08f)); position.setColour(juce::Label::backgroundColourId, background.brighter(0.08f));
    play.setButtonText(presenter.playing() ? "Pause" : "Play"); play.setToggleState(presenter.playing(), juce::dontSendNotification);
    loop.setToggleState(presenter.project().loopEnabled, juce::dontSendNotification);
    record.setToggleState(presenter.recording(), juce::dontSendNotification);
    metronome.setToggleState(presenter.metronome(), juce::dontSendNotification);
    label(position, juce::String(presenter.position(), 2) + " s"); position.setFont(juce::FontOptions(11));
    if (!bpm.hasKeyboardFocus(true)) bpm.setText(juce::String(presenter.project().bpm, 2), false);
}
}
