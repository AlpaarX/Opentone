// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Transport.h"
#include "ui/UiTheme.h"
namespace opentone
{
Transport::Transport(EditorPresenter& owner) : presenter(owner)
{
    for (auto* c : std::initializer_list<juce::Component*> {&play, &stop, &position, &bpm, &signature, &metronome, &record, &loop, &link, &tap, &follow, &overdub, &automationArm, &punchIn, &punchOut, &draw, &keyboard, &keyMap, &midiMap, &sampleRate, &mixer, &loopStart, &loopLength, &quantization, &nudgeDown, &nudgeUp, &scale, &scaleRoot, &scaleMode, &groove}) addAndMakeVisible(c);
    for (auto* field : {static_cast<juce::TextEditor*>(&bpm), &signature, &position, &loopStart, &loopLength})
    { field->setJustification(juce::Justification::centred); field->setFont(juce::FontOptions(10)); field->setIndents(1, 0); }
    bpm.setTitle("Tempo; scroll to change by one"); bpm.setText("120.00"); bpm.getProperties().set("midi-target", "tempo");
    bpm.onReturnKey = [this] { auto text = bpm.getText().trim(); char* end = nullptr; const auto value = std::strtod(text.toRawUTF8(), &end); if (end != text.toRawUTF8() && *end == 0 && value <= 999) presenter.setBpm(value); giveAwayKeyboardFocus(); refresh(); };
    bpm.onFocusLost = [this] { bpm.onReturnKey(); };
    bpm.onStep = [this](int direction) { presenter.setBpm(juce::jlimit(1.0, 999.0, std::round(presenter.project().bpm) + direction)); refresh(); };
    signature.setTitle("Time signature");
    signature.onReturnKey = [this] { const auto parts = juce::StringArray::fromTokens(signature.getText(), "/", ""); if (parts.size() == 2) presenter.setMeter(parts[0].getIntValue(), parts[1].getIntValue()); giveAwayKeyboardFocus(); refresh(); };
    signature.onFocusLost = [this] { signature.onReturnKey(); };
    editTime(position, false, [this](double time) { presenter.selectTime({time, time, presenter.viewState().selectedTracks}); presenter.seek(time); }); position.setTitle("Song position");
    editTime(loopStart, false, [this](double time) { const auto& p = presenter.project(); presenter.setLoop(time, time + p.loopEnd - p.loopStart, p.loopEnabled); }); loopStart.setTitle("Loop / punch start");
    editTime(loopLength, true, [this](double time) { const auto& p = presenter.project(); presenter.setLoop(p.loopStart, p.loopStart + std::max(0.001, time), p.loopEnabled); }); loopLength.setTitle("Loop / punch length");
    for (auto* b : {&play, &stop, &record, &loop, &metronome}) { b->getProperties().set("transport-icon", true); b->getProperties().set("midi-target", b == &play ? "play" : b == &stop ? "stop" : b == &record ? "record" : b == &loop ? "loop" : "metronome"); }
    play.setTitle("Play / pause"); stop.setTitle("Stop"); play.onClick = [this] { presenter.togglePlayback(); }; stop.onClick = [this] { presenter.stop(); };
    record.setTitle("Arrangement record"); record.onClick = [this] { presenter.toggleRecording(); };
    loop.setTitle("Arrangement loop"); loop.onClick = [this] { presenter.toggleLoop(); };
    metronome.setTitle("Metronome"); metronome.onClick = [this] { presenter.setMetronome(!presenter.metronome()); };
    link.setClickingTogglesState(true); link.onClick = [this] { presenter.setLinkEnabled(link.getToggleState()); };
    link.onContext = [this] {
        juce::PopupMenu menu; menu.addItem(1, "Sync Start/Stop", true, presenter.linkStartStopSync());
        juce::Component::SafePointer<Transport> safe(this); menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&link), [safe](int choice) { if (safe && choice == 1) safe->presenter.setLinkStartStopSync(!safe->presenter.linkStartStopSync()); }); };
    tap.setTitle("Tap tempo"); tap.getProperties().set("midi-target", "tap"); tap.onClick = [this] { presenter.tapTempo(); };
    for (auto* b : {&nudgeDown, &nudgeUp}) { const auto direction = b == &nudgeUp ? 1 : -1; b->setTitle("Temporary tempo nudge"); b->onStateChange = [this, b, direction] { if (b->isDown() && !nudgeBefore) { nudgeBefore = presenter.project().bpm; presenter.setBpm(*nudgeBefore * (1 + direction * 0.01)); } else if (!b->isDown() && nudgeBefore) { presenter.setBpm(*nudgeBefore); nudgeBefore.reset(); } }; }
    quantization.addItem("1 Bar", 1); quantization.addItem("2 Bars", 2); quantization.addItem("4 Bars", 3); quantization.addItem("1 Beat", 4); quantization.addItem("1/2", 5); quantization.addItem("1/4", 6); quantization.setSelectedId(1); quantization.setScrollWheelEnabled(false); quantization.setTitle("Link start quantization");
    quantization.onChange = [this] { const auto bar = presenter.project().numerator * 4.0 / presenter.project().denominator; const double values[] {bar, bar * 2, bar * 4, 1, 0.5, 0.25}; presenter.setLinkQuantum(values[juce::jlimit(1, 6, quantization.getSelectedId()) - 1]); };
    scale.getProperties().set("footer-icon", "scale"); scale.onClick = [this] { const auto& p = presenter.project(); presenter.setScale(p.scaleRoot, p.scaleMode, !p.scaleEnabled); };
    const juce::StringArray roots {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; scaleRoot.addItemList(roots, 1); scaleMode.addItem("Major", 1); scaleMode.addItem("Minor", 2);
    scaleRoot.setScrollWheelEnabled(false); scaleMode.setScrollWheelEnabled(false); scaleRoot.setTitle("Scale root"); scaleMode.setTitle("Scale mode");
    scaleRoot.onChange = scaleMode.onChange = [this] { presenter.setScale(scaleRoot.getSelectedId() - 1, scaleMode.getSelectedId() - 1, presenter.project().scaleEnabled); };
    groove.setColour(juce::Slider::textBoxTextColourId, ink); groove.setRange(0, 1, 0.01); groove.setSliderStyle(juce::Slider::LinearBarVertical); groove.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 34, 16); groove.setScrollWheelEnabled(false);
    groove.textFromValueFunction = [](double value) { return juce::String(juce::roundToInt(value * 100)) + "%"; }; groove.valueFromTextFunction = [](const juce::String& text) { return juce::jlimit(0.0, 1.0, text.getDoubleValue() / 100); }; groove.updateText(); setSliderDefault(groove, 0); groove.getProperties().set("midi-target", "groove"); groove.setTitle("MIDI sixteenth-note swing amount"); groove.onValueChange = [this] { presenter.setGroove(groove.getValue()); };
    for (auto* b : {&follow, &overdub, &automationArm, &punchIn, &punchOut, &draw, &keyboard, &mixer}) b->getProperties().set("footer-icon", b == &follow ? "follow" : b == &overdub ? "overdub" : b == &automationArm ? "automation" : b == &punchIn ? "punch-in" : b == &punchOut ? "punch-out" : b == &draw ? "draw" : b == &keyboard ? "keyboard" : "mixer");
    for (auto* b : {&follow, &overdub, &automationArm, &punchIn, &punchOut, &draw, &keyboard, &scale}) b->getProperties().set("midi-target", b == &follow ? "follow" : b == &overdub ? "overdub" : b == &automationArm ? "automation-record" : b == &punchIn ? "punch-in" : b == &punchOut ? "punch-out" : b == &draw ? "draw" : b == &keyboard ? "keyboard" : "scale");
    follow.setTitle("Follow playback"); follow.onClick = [this] { presenter.setFollow(!presenter.viewState().follow); };
    overdub.setTitle("MIDI overdub"); overdub.onClick = [this] { presenter.setOverdub(!presenter.viewState().overdub); };
    automationArm.setTitle("Automation record"); automationArm.onClick = [this] { presenter.setAutomationRecord(!presenter.viewState().automationRecord); };
    punchIn.setTitle("Punch-in at loop start"); punchIn.onClick = [this] { presenter.setPunch(true, !presenter.viewState().punchIn); };
    punchOut.setTitle("Punch-out at loop end"); punchOut.onClick = [this] { presenter.setPunch(false, !presenter.viewState().punchOut); };
    draw.setTitle("Draw notes"); draw.onClick = [this] { presenter.setDrawMode(!presenter.viewState().drawMode); };
    keyboard.setTitle("Computer MIDI keyboard"); keyboard.onClick = [this] { presenter.setComputerKeyboard(!presenter.viewState().computerKeyboard); };
    keyMap.setTitle("Key mapping"); keyMap.getProperties().set("map-mode", true); keyMap.onClick = [this] { presenter.setKeyMapMode(!presenter.viewState().keyMapMode); };
    midiMap.setTitle("MIDI mapping"); midiMap.getProperties().set("map-mode", true); midiMap.onClick = [this] { presenter.setMidiMapMode(!presenter.viewState().midiMapMode); };
    sampleRate.setTitle("Audio settings"); sampleRate.onClick = [this] { if (onSettings) onSettings(); };
    mixer.setTitle("Mixer view"); mixer.onClick = [this] { if (onMixer) onMixer(); };
}
double Transport::barsTime(const juce::String& text, bool length) const
{
    if (!text.containsOnly("0123456789. ")) return -1;
    auto parts = juce::StringArray::fromTokens(text, ". ", ""); parts.removeEmptyStrings(); if (parts.isEmpty() || parts.size() > 3) return -1;
    const auto offset = length ? 0 : 1; const auto bar = parts[0].getLargeIntValue() - offset;
    const auto beat = parts.size() > 1 ? parts[1].getIntValue() - offset : 0;
    const auto division = parts.size() > 2 ? parts[2].getIntValue() - offset : 0;
    if (bar < 0 || bar > 1000000 || beat < 0 || beat >= presenter.project().numerator || division < 0 || division > 3) return -1;
    return (static_cast<double>(bar) * presenter.project().numerator + beat + division * 0.25) * 60 / presenter.project().bpm * 4 / presenter.project().denominator;
}
juce::String Transport::barsText(double time, bool length) const
{
    const auto beats = std::max(0.0, time) * presenter.project().bpm / 60 * presenter.project().denominator / 4;
    const auto bar = static_cast<int>(beats / presenter.project().numerator), beat = static_cast<int>(beats) % presenter.project().numerator;
    const auto division = static_cast<int>((beats - std::floor(beats)) * 4 + 1.0e-6); const auto offset = length ? 0 : 1;
    return juce::String(bar + offset) + " . " + juce::String(beat + offset) + " . " + juce::String(division + offset);
}
void Transport::editTime(juce::TextEditor& field, bool length, std::function<void(double)> change)
{ field.onReturnKey = [this, &field, length, change] { const auto time = barsTime(field.getText(), length); if (time >= 0) change(time); giveAwayKeyboardFocus(); refresh(); }; field.onFocusLost = field.onReturnKey; }
void Transport::paint(juce::Graphics& g) { g.fillAll(panel); }
void Transport::resized()
{
    auto area = getLocalBounds().reduced(0, 1); const bool wide = getWidth() >= 760; const bool scaled = getWidth() >= 860;
    for (auto* c : std::initializer_list<juce::Component*> {&tap, &nudgeDown, &nudgeUp, &quantization, &overdub, &automationArm, &loopStart, &loopLength, &punchIn, &punchOut, &draw, &keyMap, &sampleRate, &mixer}) c->setVisible(wide);
    for (auto* c : std::initializer_list<juce::Component*> {&scale, &scaleRoot, &scaleMode, &groove}) c->setVisible(scaled);
    const auto put = [&](juce::Component& c, int width) { c.setBounds(area.removeFromLeft(width)); area.removeFromLeft(1); };
    put(link, 34); if (wide) put(tap, 22); put(bpm, 43); if (wide) { put(nudgeDown, 10); put(nudgeUp, 10); }
    put(signature, 30); put(metronome, 18); if (scaled) put(groove, 30); if (wide) put(quantization, 44);
    if (scaled) { area.removeFromLeft(6); put(scale, 16); put(scaleRoot, 32); put(scaleMode, 45); }
    if (wide) area.removeFromLeft(std::max(12, (getWidth() - (scaled ? 880 : 745)) / 2)); put(follow, 18); put(position, 58); put(play, 18); put(stop, 18); put(record, 18);
    if (wide) { put(overdub, 18); put(automationArm, 18); area.removeFromLeft(8); put(loopStart, 58); put(punchIn, 18); }
    put(loop, 18); if (wide) { put(punchOut, 18); put(loopLength, 58); }
    auto right = getLocalBounds().reduced(0, 1); const auto rightPut = [&](juce::Component& c, int width) { c.setBounds(right.removeFromRight(width)); right.removeFromRight(1); };
    if (wide) { rightPut(mixer, 16); rightPut(sampleRate, 38); }
    rightPut(midiMap, 28); if (wide) rightPut(keyMap, 22); rightPut(keyboard, 16); if (wide) rightPut(draw, 16);
}
void Transport::refresh()
{
    const auto& state = presenter.viewState(); const auto& p = presenter.project();
    for (auto* f : {&position, &signature, &loopStart, &loopLength}) f->setColour(juce::TextEditor::backgroundColourId, background);
    if (!position.hasKeyboardFocus(true)) position.setText(barsText(presenter.position()), false);
    if (!signature.hasKeyboardFocus(true)) signature.setText(juce::String(p.numerator) + " / " + juce::String(p.denominator), false);
    if (!loopStart.hasKeyboardFocus(true)) loopStart.setText(barsText(p.loopStart), false);
    if (!loopLength.hasKeyboardFocus(true)) loopLength.setText(barsText(p.loopEnd - p.loopStart, true), false);
    if (quantization.getSelectedId() <= 3 && quantization.getSelectedId() > 0) { const auto bars = 1 << (quantization.getSelectedId() - 1); presenter.setLinkQuantum(p.numerator * 4.0 / p.denominator * bars); }
    scale.setToggleState(p.scaleEnabled, juce::dontSendNotification); scaleRoot.setSelectedId(p.scaleRoot + 1, juce::dontSendNotification); scaleMode.setSelectedId(p.scaleMode + 1, juce::dontSendNotification);
    if (!groove.isMouseButtonDown()) groove.setValue(p.grooveAmount, juce::dontSendNotification);
    if (!bpm.hasKeyboardFocus(true)) bpm.setText(juce::String(p.bpm, 2), false);
    play.setButtonText(presenter.playing() ? "Pause" : "Play"); play.setToggleState(presenter.playing(), juce::dontSendNotification);
    record.setToggleState(presenter.recording(), juce::dontSendNotification); loop.setToggleState(p.loopEnabled, juce::dontSendNotification); metronome.setToggleState(presenter.metronome(), juce::dontSendNotification);
    link.setToggleState(presenter.linkEnabled(), juce::dontSendNotification); link.setTitle("Link: " + juce::String(static_cast<int>(presenter.linkPeers())) + " peers; right click for Start/Stop sync");
    follow.setToggleState(state.follow, juce::dontSendNotification); overdub.setToggleState(state.overdub, juce::dontSendNotification); automationArm.setToggleState(state.automationRecord, juce::dontSendNotification);
    punchIn.setToggleState(state.punchIn, juce::dontSendNotification); punchOut.setToggleState(state.punchOut, juce::dontSendNotification); draw.setToggleState(state.drawMode, juce::dontSendNotification);
    keyboard.setToggleState(state.computerKeyboard, juce::dontSendNotification); keyMap.setToggleState(state.keyMapMode, juce::dontSendNotification); midiMap.setToggleState(state.midiMapMode, juce::dontSendNotification);
    mixer.setToggleState(state.mixerOpen, juce::dontSendNotification); nudgeDown.setEnabled(!presenter.linkEnabled()); nudgeUp.setEnabled(!presenter.linkEnabled());
    if (auto* device = presenter.devices().getCurrentAudioDevice()) { const auto rate = device->getCurrentSampleRate() / 1000; sampleRate.setButtonText(juce::String(rate, std::abs(rate - std::round(rate)) < 0.01 ? 0 : 1) + " kHz"); }
}
}
