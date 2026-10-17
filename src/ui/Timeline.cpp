// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Timeline.h"
#include "ui/UiTheme.h"
#include "ui/LevelMeter.h"
#include "ui/WaveformDetail.h"
#include "ui/AutomationLane.h"
#include "domain/TimelineMath.h"
namespace opentone
{
namespace
{
const juce::Colour selectedClipBodyColour(0xffb6d1db);
int trackHeaderIndent(const Project& project, const Track& track)
{ return std::min(8, groupDepth(project, track)) * 8; }
constexpr int steppedTrackHeight(int height)
{ return std::clamp(18 + ((std::clamp(height, 18, 1000) - 18 + 9) / 18) * 18, 18, 1000); }
static_assert(steppedTrackHeight(17) == 18 && steppedTrackHeight(36) == 36
              && steppedTrackHeight(54) == 54 && steppedTrackHeight(72) == 72
              && steppedTrackHeight(88) == 90 && steppedTrackHeight(1001) == 1000);
int laneHeight(const EditorPresenter& presenter, const juce::String& id)
{ const auto* track = findTrack(presenter.project(), id); if (track && (track->returnIndex >= 0 || hiddenByGroup(presenter.project(), *track))) return 0; if (presenter.trackFolded(id) && (!track || !track->group)) return 18; return track ? std::max(18, track->height) : rowHeight; }
int laneTop(const EditorPresenter& presenter, int row)
{
    int y = 0;
    for (int i = 0; i < row && i < static_cast<int>(presenter.project().tracks.size()); ++i)
        y += laneHeight(presenter, presenter.project().tracks[static_cast<size_t>(i)].id);
    return y;
}
int laneRowAt(const EditorPresenter& presenter, int y)
{
    if (y < 0) return -1;
    int top = 0;
    for (size_t row = 0; row < presenter.project().tracks.size(); ++row)
    { top += laneHeight(presenter, presenter.project().tracks[row].id); if (y < top) return static_cast<int>(row); }
    return static_cast<int>(presenter.project().tracks.size());
}
}
class Timeline::BeatRuler final : public juce::Component
{
public:
    explicit BeatRuler(Timeline& owner) : timeline(owner) {}
    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel);
        const auto& project = timeline.presenter.project(); const auto& view = timeline.presenter.viewState();
        const auto beat = 60.0 / project.bpm * 4.0 / project.denominator;
        auto step = beat * view.pixelsPerSecond >= 42 ? beat : beat * project.numerator;
        while (step * view.pixelsPerSecond < 42) step *= 2;
        const auto first = static_cast<int>(std::floor(view.scrollX / view.pixelsPerSecond / step));
        g.setColour(ink); g.setFont(juce::FontOptions(10));
        for (int i = first; i * step * view.pixelsPerSecond < view.scrollX + getWidth(); ++i)
        {
            const auto x = juce::roundToInt(i * step * view.pixelsPerSecond - view.scrollX);
            const auto beatIndex = static_cast<int>(std::round(i * step / beat));
            const auto text = juce::String(beatIndex / project.numerator + 1) + (beatIndex % project.numerator == 0 ? juce::String() : "." + juce::String(beatIndex % project.numerator + 1));
            g.drawVerticalLine(x, static_cast<float>(getHeight() - 5), static_cast<float>(getHeight()));
            g.drawText(text, x + 4, 1, 60, getHeight() - 3, juce::Justification::centredLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown()) return;
        auto& presenter = timeline.presenter;
        const auto time = snapToGrid((e.x + presenter.viewState().scrollX) / presenter.viewState().pixelsPerSecond,
            timelineGridStep(presenter.project().bpm, presenter.viewState().pixelsPerSecond), presenter.viewState().snap);
        auto tracks = presenter.viewState().selectedTracks;
        if (tracks.isEmpty() && !presenter.project().tracks.empty()) tracks.add(presenter.project().tracks.front().id);
        presenter.selectTime({time, time, tracks}); presenter.seek(time);
    }
private: Timeline& timeline;
};
class Timeline::TimeRuler final : public juce::Component
{
public:
    explicit TimeRuler(EditorPresenter& owner) : presenter(owner) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(background); g.setColour(ink.withAlpha(0.7f)); g.setFont(juce::FontOptions(10));
        const auto& view = presenter.viewState();
        const auto seconds = 90.0 / view.pixelsPerSecond;
        const auto step = std::pow(10.0, std::floor(std::log10(seconds)));
        const auto interval = step * (seconds / step <= 2 ? 2 : seconds / step <= 5 ? 5 : 10);
        const auto first = static_cast<int>(view.scrollX / view.pixelsPerSecond / interval);
        const auto last = static_cast<int>((view.scrollX + getWidth()) / view.pixelsPerSecond / interval) + 1;
        for (int tick = first; tick <= last; ++tick)
        {
            const auto time = tick * interval; const auto x = juce::roundToInt(time * view.pixelsPerSecond - view.scrollX);
            g.drawVerticalLine(x, 0, 3);
            g.drawText(juce::String(static_cast<int>(time) / 60) + ":" + juce::String(std::fmod(time, 60.0), interval < 1 ? 3 : 2).paddedLeft('0', interval < 1 ? 6 : 5), x + 3, 2, 85, 15, juce::Justification::centredLeft);
        }
    }
private: EditorPresenter& presenter;
};
class Timeline::LoopBrace final : public juce::Component
{
public:
    explicit LoopBrace(EditorPresenter& owner) : presenter(owner) { setTitle("Arrangement loop; drag ends to resize, middle to move; double click to toggle"); }
    ~LoopBrace() override { if (mode >= 0) presenter.endGesture(false); }
    float xAt(double time) const { return static_cast<float>(time * presenter.viewState().pixelsPerSecond - presenter.viewState().scrollX); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel); const auto& loop = presenter.project(); const auto left = xAt(loop.loopStart), right = xAt(loop.loopEnd);
        g.setColour(loop.loopEnabled ? accent.contrasting() : ink.withAlpha(0.45f));
        const auto width = std::min(static_cast<float>(getWidth()), right) - std::max(0.0f, left);
        if (width > 0) g.fillRect(std::max(0.0f, left), 3.0f, width, 3.0f);
        juce::Path ends;
        if (left >= 0 && left <= getWidth()) { g.drawVerticalLine(juce::roundToInt(left), 3, 15); ends.addTriangle(left, 3, left + 6, 3, left, 10); }
        if (right >= 0 && right <= getWidth()) { g.drawVerticalLine(juce::roundToInt(right), 3, 15); ends.addTriangle(right, 3, right - 6, 3, right, 10); }
        g.fillPath(ends);
        if (!loop.locators.empty())
        { g.setColour(ink); g.setFont(juce::FontOptions(10));
          for (size_t i = 0; i < loop.locators.size(); ++i)
          { const auto x = juce::roundToInt(xAt(loop.locators[i].time)); const auto next = i + 1 < loop.locators.size() ? juce::roundToInt(xAt(loop.locators[i + 1].time)) : getWidth();
            if (next > 0 && x < getWidth()) { g.fillRect(x, 19, 4, 10); g.drawText(loop.locators[i].name, x + 6, 17, std::max(0, std::min(next - x - 8, 120)), 15, juce::Justification::centredLeft); } } }
    }
    void mouseMove(const juce::MouseEvent& e) override
    { const auto left = xAt(presenter.project().loopStart), right = xAt(presenter.project().loopEnd);
      setMouseCursor(std::abs(e.position.x - left) < 7 ? bracketCursor(false) : std::abs(e.position.x - right) < 7 ? bracketCursor(true) : juce::MouseCursor::DraggingHandCursor); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        { juce::PopupMenu menu; menu.addItem(1, "Loop selection"); menu.addItem(2, presenter.project().loopEnabled ? "Disable loop" : "Enable loop");
          juce::WeakReference<EditorPresenter> safe(&presenter); menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice) { if (!safe) return; if (choice == 1) safe->loopSelection(); if (choice == 2) safe->toggleLoop(); }); return; }
        if (!e.mods.isLeftButtonDown()) return;
        if (e.y >= 17 && !presenter.project().locators.empty())
        { const auto& locators = presenter.project().locators;
          const auto time = (e.position.x + presenter.viewState().scrollX) / presenter.viewState().pixelsPerSecond;
          const auto after = std::upper_bound(locators.begin(), locators.end(), time, [](double t, const auto& locator) { return t < locator.time; });
          if (after != locators.begin()) { const auto at = (after - 1)->time; presenter.selectTime({at, at, {presenter.viewState().selectedTrack}}); presenter.seek(at); } return; }
        if (e.getNumberOfClicks() >= 2) { presenter.toggleLoop(); return; }
        beforeStart = presenter.project().loopStart; beforeEnd = presenter.project().loopEnd; down = e.position.x;
        const auto left = xAt(beforeStart), right = xAt(beforeEnd);
        mode = std::abs(down - left) < 7 ? 0 : std::abs(down - right) < 7 ? 1 : down >= left && down <= right ? 2 : 3;
        presenter.beginGesture();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mode < 0) return;
        const auto zoom = presenter.viewState().pixelsPerSecond, step = timelineGridStep(presenter.project().bpm, zoom);
        const auto snap = [&](double time) { return snapToGrid(time, step, presenter.viewState().snap && !e.mods.isAltDown()); };
        const auto time = snap((e.position.x + presenter.viewState().scrollX) / zoom);
        const auto minimum = presenter.viewState().snap && !e.mods.isAltDown() ? std::max(0.001, std::min(beforeEnd - beforeStart, step)) : 0.001;
        if (mode == 0) presenter.setLoop(std::min(beforeEnd - minimum, time), beforeEnd, presenter.project().loopEnabled);
        else if (mode == 1) presenter.setLoop(beforeStart, std::max(beforeStart + minimum, time), presenter.project().loopEnabled);
        else if (mode == 2) { const auto start = snap(beforeStart + (e.position.x - down) / zoom); presenter.setLoop(start, start + beforeEnd - beforeStart, presenter.project().loopEnabled); }
        else { const auto anchor = snap((down + presenter.viewState().scrollX) / zoom); presenter.setLoop(std::min(anchor, time), std::max(std::max(anchor, time), std::min(anchor, time) + (presenter.viewState().snap && !e.mods.isAltDown() ? std::max(0.001, step) : 0.001)), true); }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (mode >= 0) { mode = -1; presenter.endGesture(false); } }
private: EditorPresenter& presenter; int mode = -1; float down = 0; double beforeStart = 0, beforeEnd = 2;
};
class Timeline::Master final : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit Master(Timeline& owner) : timeline(owner), presenter(owner.presenter), volume(owner.presenter, "master"), automation(presenter, "master")
    {
        for (auto* control : std::initializer_list<juce::Component*> {&name, &fold, &volume, &cue, &meter, &pan, &parameter, &outputRoute, &cueOutput, &volumeLabel, &cueLabel, &panLabel}) addAndMakeVisible(control);
        addChildComponent(automation); label(name, "Master"); name.addMouseListener(this, false); setWantsKeyboardFocus(true);
        name.getProperties().set("track-title", true); name.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        fold.getProperties().set("track-fold", true); fold.onClick = [this] { presenter.toggleTrackFold("master"); timeline.resized(); };
        label(volumeLabel, "Volume"); label(cueLabel, "Cue"); label(panLabel, "Pan");
        for (auto* field : {&volumeLabel, &cueLabel, &panLabel}) field->setFont(juce::FontOptions(9));
        for (auto* slider : std::initializer_list<juce::Slider*> {&volume, &cue})
        {
            slider->getProperties().set("mixer-volume", true); slider->setScrollWheelEnabled(false);
            slider->setRange(0, 2, 0.001); slider->setSkewFactor(0.5); slider->setSliderStyle(juce::Slider::LinearBarVertical);
            slider->setSliderSnapsToMousePosition(false); slider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 14);
            slider->textFromValueFunction = [](double value) { return volumeText(value).replace(" dB", ""); };
            slider->valueFromTextFunction = [](const juce::String& text) { return text.containsChar(0x221e) || text.containsIgnoreCase("inf") ? 0.0 : juce::jlimit(0.0, 2.0, std::pow(10.0, text.getDoubleValue() / 20)); };
            slider->updateText(); setSliderDefault(*slider, 1);
            slider->onDragStart = [this] { presenter.beginGesture(); }; slider->onDragEnd = [this] { presenter.endGesture(false); };
        }
        volume.getProperties().set("master-volume", true); volume.getProperties().set("midi-target", "track/master/volume");
        volume.setTitle("Master volume — all tracks and returns");
        volume.onDragStart = [this] { presenter.selectTrack("master"); presenter.beginGesture(); };
        cue.getProperties().set("midi-target", "cue"); cue.setTitle("Cue / metronome volume"); cue.onValueChange = [this] { presenter.setCue(cue.getValue()); };
        configurePanSlider(pan); pan.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 14); pan.setScrollWheelEnabled(false);
        pan.getProperties().set("midi-target", "track/master/pan"); pan.onValueChange = [this] { presenter.setPan("master", pan.getValue()); };
        pan.onDragStart = [this] { presenter.beginGesture(); }; pan.onDragEnd = [this] { presenter.endGesture(false); };
        parameter.setScrollWheelEnabled(false); parameter.addItem("Volume", 1); parameter.addItem("Pan", 2); parameter.addItem("Mute", 3);
        parameter.onChange = [this] { presenter.setAutomationParameter("master", parameter.getSelectedId() - 1); };
        outputRoute.setTitle("Master output"); outputRoute.setScrollWheelEnabled(false); cueOutput.setTitle("Cue output"); cueOutput.setScrollWheelEnabled(false);
        outputRoute.onChange = [this] { if (outputRoute.getSelectedId() > 0) presenter.setOutputPair(outputRoute.getSelectedId() - 1); };
    }
    int desiredHeight() const { return presenter.trackFolded("master") ? 18 : 88; }
    void tick()
    {
        if (!volume.isMouseButtonDown(true)) volume.setValue(presenter.trackValue("master", false), juce::dontSendNotification);
        if (!cue.isMouseButtonDown()) cue.setValue(presenter.project().cueGain, juce::dontSendNotification);
        if (!pan.isMouseButtonDown()) pan.setValue(presenter.trackValue("master", true), juce::dontSendNotification);
        const auto colour = trackColour(presenter.project(), "master", presenter.viewState().selectedTracks.contains("master"));
        name.setColour(juce::Label::textColourId, colour.contrasting()); fold.setToggleState(!presenter.trackFolded("master"), juce::dontSendNotification);
        meter.setLevels(presenter.masterLevels()); parameter.setVisible(!presenter.trackFolded("master") && presenter.viewState().automationView);
        parameter.setSelectedId(presenter.automationParameter("master") + 1, juce::dontSendNotification);
        if (auto* device = presenter.devices().getCurrentAudioDevice())
        {
            const auto names = device->getOutputChannelNames(); const auto active = device->getActiveOutputChannels().findNextSetBit(0) / 2;
            if (outputRoute.getNumItems() != names.size() / 2)
            { outputRoute.clear(juce::dontSendNotification); for (int pair = 0; pair < names.size() / 2; ++pair) outputRoute.addItem(juce::String(pair * 2 + 1) + "/" + juce::String(pair * 2 + 2), pair + 1); }
            outputRoute.setSelectedId(active + 1, juce::dontSendNotification);
            if (cueOutput.getText() != outputRoute.getText())
            { cueOutput.clear(juce::dontSendNotification); if (outputRoute.getText().isNotEmpty()) { cueOutput.addItem(outputRoute.getText(), 1); cueOutput.setSelectedId(1, juce::dontSendNotification); } }
        }
        automation.setVisible(presenter.viewState().automationView); automation.setView(presenter.viewState().scrollX / presenter.viewState().pixelsPerSecond, presenter.viewState().pixelsPerSecond);
    }
    void resized() override
    {
        const auto header = getLocalBounds().removeFromRight(headerWidth); const auto x = header.getX();
        fold.setBounds(x + 3, 3, 14, 14); name.setBounds(x + 18, 2, 77, 18);
        const bool expanded = !presenter.trackFolded("master");
        for (auto* control : std::initializer_list<juce::Component*> {&volume, &cue, &pan, &outputRoute, &cueOutput, &volumeLabel, &cueLabel, &panLabel}) control->setVisible(expanded);
        outputRoute.setBounds(x + 100, 3, 79, 14); cueOutput.setBounds(x + 100, 21, 79, 14);
        volumeLabel.setBounds(x + 184, 3, 38, 14); volume.setBounds(x + 225, 3, 59, 14);
        cueLabel.setBounds(x + 184, 21, 38, 14); cue.setBounds(x + 225, 21, 59, 14);
        panLabel.setBounds(x + 184, 39, 38, 14); pan.setBounds(x + 225, 39, 59, 14);
        meter.setBounds(header.getRight() - 11, 3, 7, getHeight() - 7); parameter.setBounds(x + 100, 39, 79, 14);
        automation.setBounds(0, 22, timeline.lanes.getViewWidth(), std::max(0, getHeight() - 24));
    }
    void paint(juce::Graphics& graphics) override
    {
        const auto header = getLocalBounds().removeFromRight(headerWidth);
        const auto lane = getLocalBounds().withWidth(timeline.lanes.getViewWidth()); const auto& view = presenter.viewState();
        graphics.setColour(background); graphics.fillRect(lane); drawArrangementGrid(graphics, lane, presenter.project(), view.pixelsPerSecond, view.scrollX);
        const auto needle = presenter.position() * view.pixelsPerSecond - view.scrollX;
        if (needle >= 0 && needle < lane.getWidth()) { graphics.setColour(ink); graphics.fillRect(static_cast<int>(needle), 0, 2, getHeight()); }
        graphics.setColour(background); graphics.fillRect(header);
        graphics.setColour(trackColour(presenter.project(), "master", view.selectedTracks.contains("master"))); graphics.fillRect(header.withWidth(97));
        graphics.setColour(line); graphics.drawRect(header); graphics.drawVerticalLine(header.getX() + 181, 0, static_cast<float>(getHeight()));
    }
    void mouseDown(const juce::MouseEvent& event) override
    { if (event.getEventRelativeTo(this).x >= getWidth() - headerWidth) { presenter.selectTrack("master", event.mods.isCommandDown() || event.mods.isCtrlDown(), event.mods.isShiftDown()); grabKeyboardFocus(); } }
    bool automationCommand(char command)
    { return automation.hasKeyboardFocus(true) && automation.keyPressed(juce::KeyPress(command, juce::ModifierKeys::commandModifier, 0)); }
    bool isInterestedInDragSource(const SourceDetails& details) override
    { const auto text = details.description.toString(); return text.startsWith("device:") || text.startsWith("plugin:"); }
    void itemDropped(const SourceDetails& details) override
    { const auto text = details.description.toString(); if (text.startsWith("device:")) presenter.addDevice(text.substring(7), "master"); else presenter.dropPlugin(text, "master"); }
    bool hitTest(int x, int) override
    { return x < timeline.lanes.getViewWidth() || x >= getWidth() - headerWidth; }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    { timeline.lanes.mouseWheelMove(event.getEventRelativeTo(&timeline.lanes), wheel); }
private: Timeline& timeline; EditorPresenter& presenter; juce::Label name, volumeLabel, cueLabel, panLabel; juce::TextButton fold; TrackVolumeSlider volume; juce::Slider cue, pan; juce::ComboBox parameter, outputRoute, cueOutput; LevelMeter meter; AutomationLane automation;
};
class Timeline::Headers final : public juce::Component
{
public:
    struct Row : juce::Component, juce::DragAndDropTarget
    {
        Row(EditorPresenter& owner, juce::String trackId, int number) : presenter(owner), id(std::move(trackId))
        {
            setWantsKeyboardFocus(true); setTitle("Track header; drag the bottom border to resize height");
            for (auto* item : std::initializer_list<juce::Component*> { &name, &fold, &gain, &pan, &sendA, &sendB, &gainValue, &parameter, &inputSource, &inputChannel, &outputRoute, &monitorIn, &monitorAuto, &monitorOff, &mute, &solo, &arm, &meter }) addAndMakeVisible(item);
            resizeGrip.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            resizeGrip.setTitle("Drag to resize or unfold track"); resizeGrip.addMouseListener(this, false);
            fold.getProperties().set("track-fold", true); fold.setTitle("Fold or unfold track");
            fold.onClick = [this] { if (!presenter.viewState().selectedTracks.contains(id)) presenter.selectTrack(id); presenter.toggleTrackFold(id); };
            name.getProperties().set("track-title", true);
            name.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack); name.setFont(juce::FontOptions(11));
            name.setEditable(false, true); name.setTitle("Track name; double click to rename");
            name.addMouseListener(this, false);
            name.onTextChange = [this] { presenter.renameTrack(id, name.getText()); };
            gainValue.getProperties().set("track-title", true); gainValue.addMouseListener(this, false);
            gain.setScrollWheelEnabled(false); pan.setScrollWheelEnabled(false);
            for (auto* control : { &parameter, &inputSource, &inputChannel, &outputRoute }) control->setScrollWheelEnabled(false);
            inputSource.addItem("Ext. In", 1); inputSource.addItem("No Input", 2);
            inputChannel.addItem("1", 1); outputRoute.addItem("Master", 1); outputRoute.addItem("No Output", 2);
            inputSource.setTitle("Track input"); inputChannel.setTitle("Input channel"); outputRoute.setTitle("Audio output");
            const auto routing = [this]
            { if (const auto* track = findTrack(presenter.project(), id))
              { const auto selected = outputRoute.getSelectedId(); juce::String destination;
                if (selected == 3) destination = "master"; else if (selected >= 4 && selected - 4 < static_cast<int>(destinations.size())) destination = destinations[static_cast<size_t>(selected - 4)].first;
                presenter.setTrackRouting(id, inputSource.getSelectedId() == 1, selected != 2, track->monitor, track->midi ? inputChannel.getSelectedId() - 1 : 0, destination); } };
            inputSource.onChange = inputChannel.onChange = outputRoute.onChange = routing;
            int mode = 0;
            for (auto* button : { &monitorIn, &monitorAuto, &monitorOff })
            { button->setClickingTogglesState(true); const auto index = mode++; button->setTitle("Input monitoring " + button->getButtonText());
              button->onClick = [this, index]
              { if (const auto* track = findTrack(presenter.project(), id)) presenter.setTrackRouting(id, track->inputEnabled, track->outputEnabled, index, track->midiChannel, track->outputTrack); }; }
            gain.getProperties().set("midi-target", "track/" + id + "/volume"); pan.getProperties().set("midi-target", "track/" + id + "/pan");
            mute.getProperties().set("midi-target", "track/" + id + "/mute"); solo.getProperties().set("midi-target", "track/" + id + "/solo"); arm.getProperties().set("midi-target", "track/" + id + "/arm");
            pan.getProperties().set("mixer-pan", true); gain.getProperties().set("mixer-volume", true);
            configurePanSlider(pan); pan.setSliderStyle(juce::Slider::LinearBarVertical); pan.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 14); pan.updateText(); pan.onValueChange = [this] { presenter.setPan(id, pan.getValue()); };
            pan.onDragStart = [this] { presenter.selectTrack(id); presenter.beginGesture(); }; pan.onDragEnd = [this] { presenter.endGesture(false); };
            parameter.addItem("Volume", 1); parameter.addItem("Pan", 2); parameter.addItem("Mute", 3);
            parameter.onChange = [this] { presenter.setAutomationParameter(id, parameter.getSelectedId() - 1); };
            gain.setRange(0, 2, 0.001); gain.setSliderStyle(juce::Slider::LinearBarVertical); gain.setSliderSnapsToMousePosition(false); gain.setSkewFactor(0.3);
            gain.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 49, 14);
            gain.textFromValueFunction = [](double value)
            {
                if (value <= 0) return juce::String::fromUTF8("−∞");
                const auto text = juce::String(20 * std::log10(value), 1);
                if (text == "-0.0") return juce::String("0");
                return text == "0.0" ? juce::String("0") : text;
            };
            gain.valueFromTextFunction = [](const juce::String& value) { return (value.containsIgnoreCase("inf") || value.containsChar(0x221e)) ? 0.0 : juce::jlimit(0.0, 2.0, std::pow(10.0, value.getDoubleValue() / 20)); };
            gain.updateText(); setSliderDefault(gain, 1); gain.setTitle("Track volume");
            gain.onDragStart = [this] { presenter.beginGesture(); };
            gain.onDragEnd = [this] { presenter.endGesture(false); };
            gain.onValueChange = [this] { if (auto* track = findId(presenter.project().tracks, id)) presenter.controls(id, gain.getValue(), track->mute); };
            mute.getProperties().set("track-active", true);
            sendA.bind(presenter, id, 0, 14); sendB.bind(presenter, id, 1, 14);
            mute.setButtonText(juce::String(number)); mute.setTitle("Track " + juce::String(number) + " on/off; checked means muted");
            mute.onClick = [this] { if (auto* track = findId(presenter.project().tracks, id)) presenter.controls(id, track->gain, mute.getToggleState()); };
            solo.getProperties().set("track-solo", true); arm.getProperties().set("track-arm", true);
            solo.setButtonText("S"); solo.setTitle("Solo track"); solo.setClickingTogglesState(true);
            solo.onClick = [this] { presenter.setSolo(id, solo.getToggleState()); };
            arm.setButtonText("Record"); arm.setTitle("Arm track for recording"); arm.setClickingTogglesState(true);
            arm.onClick = [this] { if (!presenter.viewState().selectedTracks.contains(id)) presenter.selectTrack(id, juce::ModifierKeys::getCurrentModifiers().isCommandDown() || juce::ModifierKeys::getCurrentModifiers().isCtrlDown()); presenter.armTrack(id, arm.getToggleState(), juce::ModifierKeys::getCurrentModifiers().isCommandDown() || juce::ModifierKeys::getCurrentModifiers().isCtrlDown()); arm.setToggleState(presenter.armed(id), juce::dontSendNotification); };
        }
        void resized() override
        {
            const auto* track = findTrack(presenter.project(), id); const bool aux = track && track->returnIndex >= 0;
            const bool group = track && track->group; const bool expanded = aux || group || !presenter.trackFolded(id);
            const auto height = getHeight();
            gain.setVisible(expanded && height >= 36); pan.setVisible(expanded && height >= 36);
            fold.setVisible(!aux); arm.setVisible(!aux && !group);
            sendA.setVisible(expanded && height >= 54); sendB.setVisible(expanded && height >= 54 && !aux);
            inputSource.setVisible(expanded && !aux && !group && height >= 88);
            inputChannel.setVisible(expanded && !aux && !group && height >= 72);
            for (auto* control : { &monitorIn, &monitorAuto, &monitorOff })
                control->setVisible(expanded && !aux && !group && height >= 54);
            outputRoute.setVisible(expanded && !aux && height >= 36);
            gainValue.setVisible(false);
            parameter.setVisible(expanded && height >= 36 && presenter.viewState().automationView);
            const auto indent = track ? trackHeaderIndent(presenter.project(), *track) : 0;
            fold.setBounds(3 + indent, 3, 14, 14); name.setBounds(aux ? 4 : 18 + indent, 2, aux ? 91 : 77 - indent, 18);
            int routingY = 3;
            inputSource.setBounds(100, routingY, 79, 14); if (inputSource.isVisible()) routingY += 18;
            inputChannel.setBounds(100, routingY, 79, 14); if (inputChannel.isVisible()) routingY += 18;
            monitorIn.setBounds(100, routingY, 18, 14); monitorAuto.setBounds(121, routingY, 31, 14); monitorOff.setBounds(155, routingY, 24, 14);
            if (monitorIn.isVisible()) routingY += 18;
            outputRoute.setBounds(100, routingY, 79, 14); if (outputRoute.isVisible()) routingY += 18;
            parameter.setBounds(100, std::min(height - 14, aux ? 21 : routingY), 79, 14);
            mute.setBounds(184, 3, 48, 14); solo.setBounds(237, 3, 21, 14); arm.setBounds(263, 3, 21, 14);
            gain.setBounds(184, 21, 48, 14); pan.setBounds(237, 21, 47, 14);
            sendA.setBounds(184, 39, 48, 14); sendB.setBounds(237, 39, 47, 14);
            meter.setBounds(getWidth() - 11, 3, 7, getHeight() - 7);
            updateResizeGrip();
        }
        void parentHierarchyChanged() override
        {
            if (auto* parent = getParentComponent()) parent->addAndMakeVisible(resizeGrip);
            else if (auto* parent = resizeGrip.getParentComponent()) parent->removeChildComponent(&resizeGrip);
            updateResizeGrip();
        }
        void moved() override { updateResizeGrip(); }
        void visibilityChanged() override { updateResizeGrip(); }
        void updateResizeGrip()
        {
            // A border grab belongs to this track on either side of the boundary.
            resizeGrip.setBounds(getX(), getBottom() - 3, getWidth(), 6);
            resizeGrip.setVisible(isVisible() && getHeight() > 0 && getParentComponent() != nullptr);
            resizeGrip.toFront(false);
        }
        void beginResize(const juce::MouseEvent& event)
        {
            if (!event.mods.isLeftButtonDown() || presenter.gesturing()) return;
            if (!presenter.viewState().selectedTracks.contains(id)) presenter.selectTrack(id);
            initialY = event.getScreenY(); resizing = true; initialHeights.clear();
            for (const auto& track : presenter.project().tracks)
                if (presenter.viewState().selectedTracks.contains(track.id) && track.returnIndex < 0 && !hiddenByGroup(presenter.project(), track))
                    initialHeights.emplace_back(track.id, laneHeight(presenter, track.id));
            grabKeyboardFocus(); presenter.beginGesture();
        }
        void tick()
        {
            meter.setLevels(presenter.trackLevels(id));
            if (const auto* track = findTrack(presenter.project(), id))
            { sendA.refresh(); sendB.refresh(); }
            if (!gain.isMouseButtonDown()) gain.setValue(presenter.trackValue(id, false), juce::dontSendNotification);
            if (!pan.isMouseButtonDown()) pan.setValue(presenter.trackValue(id, true), juce::dontSendNotification);
            label(gainValue, volumeText(gain.getValue())); gainValue.setFont(juce::FontOptions(10)); gainValue.setColour(juce::Label::textColourId, ink);
        }
        void paint(juce::Graphics& graphics) override
        {
            const bool selected = presenter.viewState().selectedTracks.contains(id) || presenter.viewState().selectedTrack == id;
            const auto* track = findTrack(presenter.project(), id);
            const auto indent = track ? trackHeaderIndent(presenter.project(), *track) : 0;
            const auto bottomIndent = indent + (track && track->group && !track->groupCollapsed ? 8 : 0);
            graphics.setColour(selected ? background.brighter(0.08f) : background); graphics.fillRect(indent, 0, getWidth() - indent, getHeight());
            graphics.setColour(trackColour(presenter.project(), id, selected)); graphics.fillRect(indent, 0, 97 - indent, getHeight());
            graphics.setColour(line); graphics.drawHorizontalLine(0, static_cast<float>(indent), static_cast<float>(getWidth()));
            graphics.drawHorizontalLine(getHeight() - 1, static_cast<float>(bottomIndent), static_cast<float>(getWidth()));
            graphics.drawVerticalLine(indent, 0, static_cast<float>(getHeight())); graphics.drawVerticalLine(getWidth() - 1, 0, static_cast<float>(getHeight()));
            graphics.setColour(trackColour(presenter.project(), id).contrasting()); graphics.setFont(juce::FontOptions(10));
            if (outputRoute.isVisible() && !parameter.isVisible())
            {
                const auto bounds = juce::Rectangle<int>(100, std::min(getHeight() - 14, outputRoute.getBottom() + 4), 79, 14);
                graphics.setColour(panel); graphics.fillRect(bounds);
                graphics.setColour(line); graphics.drawRect(bounds);
            }
            graphics.setColour(line); graphics.drawVerticalLine(181, 0, static_cast<float>(getHeight()));
        }
        void mouseMove(const juce::MouseEvent& event) override
        { setMouseCursor(event.getEventRelativeTo(this).y >= getHeight() - 5 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }
        void mouseExit(const juce::MouseEvent&) override
        { if (!resizing) setMouseCursor(juce::MouseCursor::NormalCursor); }
        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.originalComponent == &resizeGrip) { beginResize(event); return; }
            const bool resizeEdge = event.mods.isLeftButtonDown() && event.getEventRelativeTo(this).y >= getHeight() - 5;
            const bool selected = presenter.viewState().selectedTracks.contains(id);
            if (!(selected && (resizeEdge || event.mods.isRightButtonDown())))
                presenter.selectTrack(id, (event.mods.isCommandDown() || event.mods.isCtrlDown()), event.mods.isShiftDown());
            grabKeyboardFocus();
            if (event.originalComponent == &gainValue) return;
            if (event.mods.isRightButtonDown())
            {
                juce::PopupMenu menu; menu.addItem(4, "Rename"); menu.addItem(5, "Duplicate track");
                menu.addItem(6, presenter.trackFolded(id) ? "Unfold track" : "Fold track");
                menu.addItem(7, "Group tracks", true);
                const auto* model = findTrack(presenter.project(), id);
                if (model && model->group) menu.addItem(8, "Ungroup tracks");
                else if (model && model->parentGroup.isNotEmpty()) menu.addItem(9, "Move out of group");
                menu.addSeparator(); menu.addItem(1, "Copy track"); menu.addItem(2, "Paste track"); menu.addItem(3, "Delete track");
                juce::WeakReference<EditorPresenter> owner(&presenter); const auto track = id; juce::Component::SafePointer<Row> row(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [owner, track, row](int choice)
                { if (!owner) return;
                  if (choice == 7) { owner->groupSelectedTracks(); return; }
                  if (choice == 9) { owner->removeFromGroup(track); return; }
                  if (choice == 6) { owner->toggleTrackFold(track); return; }
                  owner->selectTrack(track); if (choice == 8) { owner->ungroupSelectedTrack(); return; } if (choice == 1) owner->copyTrack();
                  if (choice == 2) owner->pasteTrack(); if (choice == 3) owner->deleteTrack(track);
                  if (choice == 4 && row) row->name.showEditor(); if (choice == 5) owner->duplicateTrack(); }); return;
            }
            if (resizeEdge) beginResize(event);
        }
        void mouseDoubleClick(const juce::MouseEvent& event) override
        { if (event.originalComponent == &gainValue) gain.setValue(1, juce::sendNotificationSync); }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            if (resizing)
            {
                for (const auto& [track, height] : initialHeights)
                    presenter.setTrackHeight(track, steppedTrackHeight(height + event.getScreenY() - initialY));
                return;
            }
            if (!event.mods.isLeftButtonDown() || event.getDistanceFromDragStart() < 4 || event.getMouseDownPosition().x >= 97) return;
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!container->isDragAndDropActive())
            { juce::Array<juce::var> ids; const auto selected = presenter.viewState().selectedTracks.isEmpty() ? juce::StringArray{id} : presenter.viewState().selectedTracks;
              for (const auto& track : selected) ids.add(track); container->startDragging("tracks:" + juce::JSON::toString(juce::var(ids), true), this, juce::ScaledImage(), true); }
        }
        void mouseUp(const juce::MouseEvent&) override
        { if (resizing) { resizing = false; presenter.endGesture(false); setMouseCursor(juce::MouseCursor::NormalCursor); } }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (!resizing && key.getModifiers().isCommandDown() && (key.getKeyCode() == 'r' || key.getKeyCode() == 'R'))
            { name.showEditor(); return true; }
            if (!resizing && (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey))
            { const auto track = id; presenter.deleteTrack(track); return true; }
            if (!resizing || key.getKeyCode() != juce::KeyPress::escapeKey) return false;
            resizing = false; presenter.endGesture(true);
            setMouseCursor(juce::MouseCursor::NormalCursor); return true;
        }
        void update(const Track& track)
        { setName(track.name + " track"); name.setTooltip(track.annotation);
          mute.getProperties().set("group-muted", mutedByGroup(presenter.project(), track));
          solo.getProperties().set("group-solo-child", track.group && std::any_of(presenter.project().tracks.begin(), presenter.project().tracks.end(), [&](const auto& child) { return child.solo && inGroup(presenter.project(), child, id); }));
          const auto& choices = presenter.outputChoices();
          if (destinations != choices || routeParent != track.parentGroup)
          { destinations = choices; routeParent = track.parentGroup; outputRoute.clear(juce::dontSendNotification);
            const auto* parent = findTrack(presenter.project(), track.parentGroup); outputRoute.addItem(parent ? parent->name : "Master", 1); outputRoute.addItem("No Output", 2);
            if (parent) outputRoute.addItem("Master", 3);
            for (size_t index = 0; index < destinations.size(); ++index) if (destinations[index].first != id)
            { outputRoute.addItem(destinations[index].second, static_cast<int>(index) + 4);
 } }
          if (!name.isBeingEdited()) label(name, track.name);
          if (track.returnIndex >= 0) mute.setButtonText(track.returnIndex < 26 ? juce::String::charToString(static_cast<juce::juce_wchar>('A' + track.returnIndex)) : juce::String(track.returnIndex + 1));
          sendA.refresh(); sendB.refresh();
          const auto count = returnCount(presenter.project());
          if (count != parameterReturns)
          { parameterReturns = count; parameter.clear(juce::dontSendNotification); parameter.addItem("Volume", 1); parameter.addItem("Pan", 2); parameter.addItem("Mute", 3);
            for (const auto& target : presenter.project().tracks) if (target.returnIndex >= 0) parameter.addItem("Send " + target.name, target.returnIndex + 4); }
          fold.setToggleState(!presenter.trackFolded(id), juce::dontSendNotification);
          if (midiInputs != track.midi)
          { inputSource.changeItemText(1, track.midi ? "All Ins" : "Ext. In"); inputChannel.clear(juce::dontSendNotification);
            if (track.midi) { inputChannel.addItem("All Channels", 1); for (int channel = 1; channel <= 16; ++channel) inputChannel.addItem(juce::String(channel), channel + 1); }
            else inputChannel.addItem("1", 1); midiInputs = track.midi; }
          inputSource.setSelectedId(track.inputEnabled ? 1 : 2, juce::dontSendNotification); inputChannel.setSelectedId(track.midi ? track.midiChannel + 1 : 1, juce::dontSendNotification);
          int outputChoice = track.outputEnabled ? 1 : 2;
          if (track.outputEnabled && track.outputTrack.isNotEmpty())
          { if (track.outputTrack == "master") outputChoice = track.parentGroup.isEmpty() ? 1 : 3;
            else for (size_t index = 0; index < destinations.size(); ++index) if (destinations[index].first == track.outputTrack) outputChoice = static_cast<int>(index) + 4; }
          outputRoute.setSelectedId(outputChoice, juce::dontSendNotification);
          monitorIn.setToggleState(track.monitor == 0, juce::dontSendNotification); monitorAuto.setToggleState(track.monitor == 1, juce::dontSendNotification); monitorOff.setToggleState(track.monitor == 2, juce::dontSendNotification);
          parameter.setVisible(!presenter.trackFolded(id) && presenter.viewState().automationView); parameter.setSelectedId(presenter.automationParameter(id) + 1, juce::dontSendNotification);
          const auto colour = trackColour(presenter.project(), id, presenter.viewState().selectedTracks.contains(id) || presenter.viewState().selectedTrack == id);
          name.setColour(juce::Label::textColourId, colour.contrasting());
          name.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
          gain.setValue(track.gain, juce::dontSendNotification); mute.setToggleState(track.mute, juce::dontSendNotification); solo.setToggleState(track.solo, juce::dontSendNotification); arm.setToggleState(presenter.armed(id), juce::dontSendNotification); resized(); repaint(); }
        bool isInterestedInDragSource(const SourceDetails& details) override
        { const auto* track = findId(presenter.project().tracks, id);
          return track && (details.description.toString().startsWith("tracks:") || presenter.isBrowserDrag(details.description.toString()) || details.description.toString().startsWith("plugin:")
              || (details.description.toString().startsWith("instrument:") && track->midi && !track->sampler)); }
        void itemDropped(const SourceDetails& details) override
        { const auto text = details.description.toString(); if (text.startsWith("tracks:")) { presenter.dropTracks(text, id); return; } if (presenter.isBrowserDrag(text)) presenter.dropBrowserItem(text, id, presenter.viewState().timeSelection ? presenter.viewState().timeSelection->start : presenter.position());
          else if (text.startsWith("plugin:")) presenter.dropPlugin(text, id);
          else presenter.moveSampler(text.substring(11), id); }
        EditorPresenter& presenter; juce::String id; juce::Label name, gainValue; juce::Slider gain, pan; SendSlider sendA, sendB; juce::ComboBox parameter, inputSource, inputChannel, outputRoute;
        juce::TextButton monitorIn {"In"}, monitorAuto {"Auto"}, monitorOff {"Off"}; bool midiInputs = false; juce::ToggleButton mute; juce::TextButton solo, arm, fold; LevelMeter meter; juce::Component resizeGrip;
        std::vector<std::pair<juce::String, juce::String>> destinations; juce::String routeParent;
        std::vector<std::pair<juce::String, int>> initialHeights;
        int initialY = 0, parameterReturns = -1; bool resizing = false;
    };
    Headers(EditorPresenter& owner) : presenter(owner)
    { setWantsKeyboardFocus(true); }
    void refresh()
    {
        const bool keepFocus = hasKeyboardFocus(true);
        bool rebuild = rows.size() != presenter.project().tracks.size();
        if (!rebuild) for (size_t i = 0; i < rows.size(); ++i) if (rows[i]->id != presenter.project().tracks[i].id) rebuild = true;
        if (rebuild)
        {
            rows.clear(); if (keepFocus) grabKeyboardFocus();
            for (const auto& track : presenter.project().tracks)
            { auto row = std::make_unique<Row>(presenter, track.id, static_cast<int>(rows.size()) + 1); addAndMakeVisible(*row); row->sendLookAndFeelChange(); rows.push_back(std::move(row)); }
        }
        for (size_t i = 0; i < rows.size(); ++i)
        { const auto& track = presenter.project().tracks[i]; rows[i]->update(track);
          rows[i]->setVisible(track.returnIndex < 0 && !hiddenByGroup(presenter.project(), track));
          rows[i]->setBounds(0, laneTop(presenter, static_cast<int>(i)), getWidth(), laneHeight(presenter, track.id)); }
        repaint();
    }
    void mouseDown(const juce::MouseEvent& event) override
    { if (event.mods.isPopupMenu()) showTrackCreationMenu(presenter, *this); }
    void tick() { for (auto& row : rows) row->tick(); }
    void resized() override { refresh(); }
    void paint(juce::Graphics& graphics) override
    { graphics.fillAll(panel);
      const auto& project = presenter.project();
      for (size_t index = 0; index < project.tracks.size(); ++index)
      {
          const auto& group = project.tracks[index]; if (!group.group || hiddenByGroup(project, group)) continue;
          auto height = laneHeight(presenter, group.id);
          for (const auto& child : project.tracks) if (inGroup(project, child, group.id)) height += laneHeight(presenter, child.id);
          const auto indent = trackHeaderIndent(project, group);
          const juce::Rectangle<int> bracket(indent, laneTop(presenter, static_cast<int>(index)), 97 - indent, height);
          graphics.setColour(trackColour(project, group.id, presenter.viewState().selectedTracks.contains(group.id))); graphics.fillRect(bracket);
          graphics.setColour(line); graphics.drawRect(bracket);
      }
    }
private:
    EditorPresenter& presenter; std::vector<std::unique_ptr<Row>> rows;
};
class Timeline::Returns final : public juce::Component
{
public:
    explicit Returns(Timeline& owner) : timeline(owner) {}
    void refresh()
    {
        rows.resize(static_cast<size_t>(returnCount(timeline.presenter.project()))); automation.resize(rows.size());
        const auto count = static_cast<int>(std::count_if(timeline.presenter.project().tracks.begin(), timeline.presenter.project().tracks.end(), [](const auto& track) { return track.returnIndex >= 0; }));
        const auto visible = std::max(1, getHeight() / 54); page = juce::jlimit(0, std::max(0, count - visible), page);
        int y = 0, ordinal = 0;
        for (size_t index = 0; index < rows.size(); ++index)
        {
            const auto& tracks = timeline.presenter.project().tracks;
            const auto track = std::find_if(tracks.begin(), tracks.end(), [index](const auto& item) { return item.returnIndex == static_cast<int>(index); });
            if (track == tracks.end()) { rows[index].reset(); automation[index].reset(); continue; }
            if (!rows[index] || rows[index]->id != track->id)
            { rows[index] = std::make_unique<Headers::Row>(timeline.presenter, track->id, 0); addAndMakeVisible(*rows[index]); rows[index]->sendLookAndFeelChange();
              automation[index] = std::make_unique<AutomationLane>(timeline.presenter, track->id); addAndMakeVisible(*automation[index]); }
            const auto shown = ordinal >= page && ordinal < page + visible; ++ordinal; rows[index]->setVisible(shown); automation[index]->setVisible(shown && timeline.presenter.viewState().automationView);
            if (!shown) continue;
            rows[index]->update(*track); rows[index]->setBounds(getWidth() - headerWidth, y, headerWidth, 54);
            automation[index]->setBounds(0, y + 22, timeline.lanes.getViewWidth(), 20);
            automation[index]->setView(timeline.presenter.viewState().scrollX / timeline.presenter.viewState().pixelsPerSecond, timeline.presenter.viewState().pixelsPerSecond);
            automation[index]->setVisible(timeline.presenter.viewState().automationView); y += 54;
        }
    }
    bool automationCommand(char command)
    { for (const auto& lane : automation) if (lane && lane->hasKeyboardFocus(true)) return lane->keyPressed(juce::KeyPress(command, juce::ModifierKeys::commandModifier, 0)); return false; }
    int requiredHeight() const
    { return std::min(std::max(54, timeline.lanes.getViewHeight() / 3), 54 * static_cast<int>(std::count_if(timeline.presenter.project().tracks.begin(), timeline.presenter.project().tracks.end(), [](const auto& track) { return track.returnIndex >= 0; }))); }
    void resized() override { refresh(); }
    void tick() { for (auto& row : rows) if (row) row->tick(); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(background); const auto& view = timeline.presenter.viewState();
        drawArrangementGrid(g, getLocalBounds().withWidth(getWidth() - headerWidth), timeline.presenter.project(), view.pixelsPerSecond, view.scrollX);
        g.setColour(line); for (int y = 0; y < getHeight(); y += 54) g.drawHorizontalLine(y, 0, static_cast<float>(getWidth()));
        const auto x = timeline.presenter.position() * view.pixelsPerSecond - view.scrollX;
        if (x >= 0 && x < getWidth() - headerWidth) { g.setColour(ink); g.drawVerticalLine(static_cast<int>(x), 0, static_cast<float>(getHeight())); }
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    { if (54 * static_cast<int>(rows.size()) > getHeight() && std::abs(wheel.deltaY) > 0.001f) { page += wheel.deltaY < 0 ? 1 : -1; refresh(); repaint(); }
      else timeline.lanes.mouseWheelMove(event.getEventRelativeTo(&timeline.lanes), wheel); }
private: Timeline& timeline; int page = 0; std::vector<std::unique_ptr<Headers::Row>> rows;
    std::vector<std::unique_ptr<AutomationLane>> automation;
};
class Timeline::Canvas final : public juce::Component, public juce::FileDragAndDropTarget,
                              public juce::DragAndDropTarget, private juce::ChangeListener
{
public:
    explicit Canvas(Timeline& parent) : owner(parent)
    {
        setWantsKeyboardFocus(true); setTitle("Arrangement timeline; select clips by their headers; drag below headers to select time; drag a selected header to move audio"); addAndMakeVisible(playhead);
        detail.addChangeListener(this);
        trimCursors = { bracketCursor(false), bracketCursor(true) };
    }
    ~Canvas() override { detail.removeChangeListener(this); for (auto& pair : thumbnails) pair.second->removeChangeListener(this); }
    struct Playhead : juce::Component
    { Playhead() { setInterceptsMouseClicks(false, false); }
      void paint(juce::Graphics& graphics) override { graphics.fillAll(ink); } } playhead;
    int trackIndex(const juce::String& id) const
    { const auto& tracks = owner.presenter.project().tracks; for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].id == id) return static_cast<int>(i); return -1; }
    juce::String trackAt(int y) const
    { const auto index = laneRowAt(owner.presenter, y);
      if (y < 0 || index < 0 || index >= static_cast<int>(owner.presenter.project().tracks.size())) return {};
      return owner.presenter.project().tracks[static_cast<size_t>(index)].id; }
    juce::Rectangle<float> boundsFor(const Clip& clip) const
    {
        const auto* track = findTrack(owner.presenter.project(), clip.track);
        if (clip.scene >= 0 || !track || hiddenByGroup(owner.presenter.project(), *track)) return {};
        const auto x = std::min(static_cast<double>(getWidth()) + 1, clip.start * owner.presenter.viewState().pixelsPerSecond);
        return { static_cast<float>(x), static_cast<float>(laneTop(owner.presenter, trackIndex(clip.track)) + 2),
                 static_cast<float>(std::min(clip.duration * owner.presenter.viewState().pixelsPerSecond, std::max(0.0, getWidth() - x))),
                 static_cast<float>(laneHeight(owner.presenter, clip.track) - 4) };
    }
    int headerEdge(const Clip& clip, juce::Point<float> point) const
    {
        auto bounds = boundsFor(clip); if (bounds.isEmpty()) return -1;
        bounds = bounds.withHeight(std::min(19.0f, bounds.getHeight()));
        if (!bounds.contains(point)) return -1;
        const auto width = std::min(8.0f, bounds.getWidth() / 2);
        return point.x < bounds.getX() + width ? 1 : (point.x > bounds.getRight() - width ? 2 : 0);
    }
    void mouseMove(const juce::MouseEvent& event) override
    {
        if (owner.presenter.gesturing()) return;
        for (auto it = owner.presenter.project().clips.rbegin(); it != owner.presenter.project().clips.rend(); ++it)
        {
            const auto edge = headerEdge(*it, event.position);
            if (edge < 0) continue;
            setMouseCursor(edge > 0 ? trimCursors[static_cast<size_t>(edge - 1)] : juce::MouseCursor(juce::MouseCursor::NormalCursor));
            return;
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    void mouseEnter(const juce::MouseEvent& event) override { mouseMove(event); }
    void mouseExit(const juce::MouseEvent&) override
    { if (!owner.presenter.gesturing()) setMouseCursor(juce::MouseCursor::NormalCursor); }
    double snappedTime(double seconds, bool snapIn) const
    {
        return snapToGrid(seconds, timelineGridStep(owner.presenter.project().bpm, owner.presenter.viewState().pixelsPerSecond), snapIn);
    }
    double timeAt(int x) const { return snappedTime(x / owner.presenter.viewState().pixelsPerSecond, owner.presenter.viewState().snap); }
    double gridTimeAt(int x) const { return snappedTime(x / owner.presenter.viewState().pixelsPerSecond, true); }
    TimeSelection timeSelectionAt(const juce::Point<float>& point) const
    {
        const auto anchor = gridTimeAt(static_cast<int>(down.x)), end = timeAt(static_cast<int>(point.x));
        TimeSelection selection { std::min(anchor, end), std::max(anchor, end), {} };
        const auto count = static_cast<int>(owner.presenter.project().tracks.size());
        if (count > 0)
        {
            const auto first = juce::jlimit(0, count - 1, laneRowAt(owner.presenter, static_cast<int>(down.y)));
            const auto last = juce::jlimit(0, count - 1, laneRowAt(owner.presenter, static_cast<int>(point.y)));
            for (int row = std::min(first, last); row <= std::max(first, last); ++row)
            {
                const auto& track = owner.presenter.project().tracks[static_cast<size_t>(row)];
                if (hiddenByGroup(owner.presenter.project(), track) || track.returnIndex >= 0) continue;
                selection.tracks.addIfNotAlreadyThere(track.id);
                if (track.group) for (const auto& child : owner.presenter.project().tracks)
                    if (inGroup(owner.presenter.project(), child, track.id)) selection.tracks.addIfNotAlreadyThere(child.id);
            }
        }
        return selection;
    }
    void refresh()
    {
        const auto& project = owner.presenter.project();
        const auto& selection = owner.presenter.viewState().timeSelection;
        setDescription(selection ? "Time selection from " + juce::String(selection->start, 2) + " to " + juce::String(selection->end, 2) + " seconds"
                                 : juce::String(owner.presenter.viewState().selectedClips.size()) + " clips selected");
        const auto& directory = owner.presenter.mediaDirectory();
        for (const auto& asset : project.assets)
        {
            if (asset.missing) continue;
            const auto file = directory.getChildFile(asset.path);
            const auto key = file.getFullPathName();
            if (thumbnails.count(key) == 0)
            {
                auto thumbnail = std::make_unique<juce::AudioThumbnail>(512, formats, cache);
                thumbnail->addChangeListener(this); thumbnail->setSource(new juce::FileInputSource(file));
                thumbnails[key] = std::move(thumbnail);
            }
        }
        std::set<juce::String> keep;
        for (const auto& asset : project.assets) keep.insert(directory.getChildFile(asset.path).getFullPathName());
        for (auto it = thumbnails.begin(); it != thumbnails.end();)
            if (keep.count(it->first) == 0) { it->second->removeChangeListener(this); it = thumbnails.erase(it); } else ++it;
        if (!owner.presenter.viewState().automationView) envelopes.clear();
        else
        {
            for (auto it = envelopes.begin(); it != envelopes.end();)
            { const auto* track = findId(project.tracks, it->first); if (!track || hiddenByGroup(project, *track)) it = envelopes.erase(it); else ++it; }
            for (size_t row = 0; row < project.tracks.size(); ++row)
            {
                const auto& track = project.tracks[row]; if (track.returnIndex >= 0 || hiddenByGroup(project, track)) continue; auto& envelope = envelopes[track.id];
                if (!envelope) { envelope = std::make_unique<AutomationLane>(owner.presenter, track.id); addAndMakeVisible(*envelope); }
                envelope->setBounds(0, laneTop(owner.presenter, static_cast<int>(row)) + 22, getWidth(), laneHeight(owner.presenter, track.id) - 24);
                envelope->setVisible(track.group || !owner.presenter.trackFolded(track.id));
                envelope->setView(0, owner.presenter.viewState().pixelsPerSecond);
            }
        }
        playhead.toFront(false); repaint();
    }
    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(background); const auto visible = graphics.getClipBounds();
        const auto& project = owner.presenter.project();
        std::set<juce::String> visibleClips;
        for (const auto& clip : project.clips)
            if (boundsFor(clip).toNearestInt().withTrimmedTop(22).reduced(3).intersects(owner.lanes.getViewArea())) visibleClips.insert(clip.id);
        for (auto it = waveforms.begin(); it != waveforms.end();)
            if (visibleClips.count(it->first) == 0) it = waveforms.erase(it); else ++it;
        const auto beat = 60.0 / project.bpm * 4.0 / project.denominator;
        const auto step = timelineGridStep(project.bpm, owner.presenter.viewState().pixelsPerSecond);
        const auto spacing = step * owner.presenter.viewState().pixelsPerSecond;
        const auto first = std::isfinite(spacing) ? static_cast<int>(visible.getX() / spacing) : 0;
        const auto last = std::isfinite(spacing) ? static_cast<int>(visible.getRight() / spacing) + 1 : -1;
        drawArrangementGrid(graphics, visible, project, owner.presenter.viewState().pixelsPerSecond);
        for (const auto& track : project.tracks)
            if (owner.presenter.viewState().selectedTracks.contains(track.id) && !hiddenByGroup(project, track))
            {
                const auto row = trackIndex(track.id);
                graphics.setColour(accent.withAlpha(0.16f));
                graphics.fillRect(visible.getX(), laneTop(owner.presenter, row), visible.getWidth(), laneHeight(owner.presenter, track.id));
            }

        for (size_t index = 0; index <= project.tracks.size(); ++index)
        { graphics.setColour(background.darker(0.55f)); graphics.fillRect(visible.getX(), laneTop(owner.presenter, static_cast<int>(index)) - 1, visible.getWidth(), 2); }
        if (const auto& selection = owner.presenter.viewState().timeSelection)
        {
            const auto from = static_cast<float>(std::min(static_cast<double>(getWidth()), selection->start * owner.presenter.viewState().pixelsPerSecond));
            const auto to = static_cast<float>(std::min(static_cast<double>(getWidth()), selection->end * owner.presenter.viewState().pixelsPerSecond));
            graphics.setColour(accent.withAlpha(0.65f));
            if (to > from) for (const auto& track : selection->tracks)
                graphics.fillRect(from, static_cast<float>(laneTop(owner.presenter, trackIndex(track))), to - from, static_cast<float>(laneHeight(owner.presenter, track)));
        }
        for (const auto& group : project.tracks)
        {
            if (!group.group || !owner.presenter.trackFolded(group.id) || hiddenByGroup(project, group)) continue;
            const auto top = laneTop(owner.presenter, trackIndex(group.id));
            const auto height = laneHeight(owner.presenter, group.id);
            if (!visible.intersects(juce::Rectangle<int>(visible.getX(), top, visible.getWidth(), height))) continue;
            std::map<juce::String, int> members;
            for (const auto& track : project.tracks)
                if (!track.group && inGroup(project, track, group.id)) members.emplace(track.id, static_cast<int>(members.size()));
            if (members.empty()) continue;
            const auto strip = static_cast<float>(height - 4) / static_cast<float>(members.size());
            for (const auto& clip : project.clips)
            {
                const auto member = members.find(clip.track);
                if (clip.scene >= 0 || member == members.end()) continue;
                juce::Rectangle<float> bounds(static_cast<float>(clip.start * owner.presenter.viewState().pixelsPerSecond),
                    static_cast<float>(top) + 2.0f + static_cast<float>(member->second) * strip,
                    static_cast<float>(clip.duration * owner.presenter.viewState().pixelsPerSecond), std::max(1.0f, strip - 1.0f));
                if (!bounds.intersects(visible.toFloat())) continue;
                const auto colour = clipColour(project, clip);
                graphics.setColour(clip.enabled ? colour : colour.withMultipliedSaturation(0.1f));
                graphics.fillRect(bounds);
            }
        }
        for (const auto& clip : project.clips)
        {
            if (clip.scene >= 0) continue;
            const auto* laneTrack = findTrack(project, clip.track); if (laneTrack && hiddenByGroup(project, *laneTrack)) continue;
            const auto bounds = boundsFor(clip); if (!bounds.intersects(visible.toFloat())) continue;
            const auto* asset = findId(project.assets, clip.asset);
            const auto* track = findId(project.tracks, clip.track); if (!track) continue;
            const bool clipSelected = owner.presenter.viewState().selectedClips.contains(clip.id);
            const auto baseColour = asset && asset->missing ? juce::Colour(0xffd0a5a7) : clipColour(owner.presenter.project(), clip);
            const auto colour = clip.enabled ? baseColour : baseColour.withMultipliedSaturation(0.1f).darker(0.3f);
            const auto bodyColour = clipSelected ? selectedClipBodyColour : colour.withMultipliedSaturation(0.85f).brighter(0.05f);
            graphics.setColour(bodyColour); graphics.fillRect(bounds);
            graphics.setColour(colour); graphics.fillRect(bounds.withHeight(19));
            auto selectedBody = clipSelected ? bounds.withTrimmedTop(19) : juce::Rectangle<float>();
            if (!clipSelected) if (const auto& selection = owner.presenter.viewState().timeSelection)
                if (selection->tracks.contains(clip.track))
                {
                    const auto zoom = owner.presenter.viewState().pixelsPerSecond;
                    selectedBody = bounds.withTrimmedTop(19).getIntersection(juce::Rectangle<float>(
                        static_cast<float>(selection->start * zoom), bounds.getY() + 19,
                        static_cast<float>((selection->end - selection->start) * zoom), std::max(0.0f, bounds.getHeight() - 19)));
                    graphics.setColour(selectedClipBodyColour); graphics.fillRect(selectedBody);
                }
            if (!selectedBody.isEmpty())
            {
                graphics.setColour(line.withAlpha(0.12f));
                for (int i = first; i * spacing < visible.getRight(); ++i)
                    if (i * spacing >= selectedBody.getX() && i * spacing < selectedBody.getRight())
                        graphics.drawVerticalLine(juce::roundToInt(i * spacing), selectedBody.getY(), selectedBody.getBottom());
            }
            if (asset && asset->missing && bounds.getHeight() >= 28)
            {
                graphics.setColour(juce::Colours::black); graphics.setFont(juce::FontOptions(11));
                graphics.drawFittedText("Missing sample: " + assetName(*asset), bounds.toNearestInt().withTrimmedTop(22).reduced(6), juce::Justification::topLeft, 2);
            }
            else if (asset && bounds.getHeight() >= 28)
            {
                const auto found = thumbnails.find(owner.presenter.mediaDirectory().getChildFile(asset->path).getFullPathName());
                if (found != thumbnails.end()) drawWaveform(graphics, *found->second, found->first, clip, bounds.toNearestInt().withTrimmedTop(22).reduced(3), juce::Colours::black);
            }
            else if (!asset && bounds.getHeight() >= 28)
            {
                juce::Graphics::ScopedSaveState saved(graphics); graphics.reduceClipRegion(bounds.toNearestInt().withTrimmedTop(22));
                graphics.setColour(colour.contrasting().withAlpha(0.7f));
                const auto zoom = owner.presenter.viewState().pixelsPerSecond;
                for (const auto& note : clip.notes)
                {
                    if (!midiPattern(clip, note)) continue;
                    const auto y = bounds.getY() + 23 + static_cast<float>(71 - juce::jlimit(48, 71, note.pitch)) * (bounds.getHeight() - 25) / 24;
                    if (clip.loop && clip.loopLength > 0 && clip.loopLength * zoom < 2)
                    { const auto from = std::max(bounds.getX(), static_cast<float>(visible.getX()));
                      const auto to = std::min(bounds.getRight(), static_cast<float>(visible.getRight())); graphics.fillRect(from, y, to - from, 3.0f); }
                    else forEachMidiOccurrence(clip, note, visible.getX() / zoom, visible.getRight() / zoom,
                        [&](double from, double to) { graphics.fillRect(static_cast<float>(from * zoom), y, static_cast<float>((to - from) * zoom), 3.0f); });
                }
            }
            graphics.setColour(colour.contrasting()); graphics.setFont(juce::FontOptions(11));
            graphics.drawText((asset && asset->missing ? "Missing: " : "") + (clip.name.isNotEmpty() ? clip.name : asset ? assetName(*asset) : juce::String("MIDI")), bounds.toNearestInt().reduced(5, 0).withHeight(19), juce::Justification::centredLeft, true);
            if (clip.loop && clip.loopLength > 0 && clip.loopLength * owner.presenter.viewState().pixelsPerSecond >= 8)
            {
                const auto zoomIn = owner.presenter.viewState().pixelsPerSecond;
                auto phase = std::fmod(clip.offset - clip.loopStart, clip.loopLength); if (phase < 0) phase += clip.loopLength;
                const auto first = clip.start + clip.loopLength - phase;
                auto time = first + std::max(0.0, std::ceil((visible.getX() / zoomIn - first) / clip.loopLength)) * clip.loopLength;
                graphics.setColour(colour.contrasting().withAlpha(0.75f));
                for (; time < clip.start + clip.duration && time * zoomIn < visible.getRight(); time += clip.loopLength)
                { const auto x = static_cast<float>(time * zoomIn); graphics.fillRect(x - 0.5f, bounds.getY() + 1, 1.0f, 6.0f);
                  juce::Path tick; tick.addTriangle(x - 3, bounds.getY() + 1, x + 3, bounds.getY() + 1, x, bounds.getY() + 4); graphics.fillPath(tick); }
            }
            if (project.loopEnabled) for (const auto time : { project.loopStart, project.loopEnd })
                if (time > clip.start && time < clip.start + clip.duration && time * owner.presenter.viewState().pixelsPerSecond >= visible.getX() && time * owner.presenter.viewState().pixelsPerSecond <= visible.getRight())
                { graphics.setColour(colour.contrasting().withAlpha(0.75f)); graphics.fillRect(static_cast<float>(time * owner.presenter.viewState().pixelsPerSecond) - 1, bounds.getY() + 1, 2.0f, 6.0f); }
        }
        if (owner.presenter.recording())
        {
            const auto from = owner.presenter.recordingStart() * owner.presenter.viewState().pixelsPerSecond;
            const auto to = std::max(from + 2, owner.presenter.position() * owner.presenter.viewState().pixelsPerSecond);
            const auto left = std::max(static_cast<double>(visible.getX()), from), right = std::min(static_cast<double>(visible.getRight()), to);
            if (right > left) for (size_t row = 0; row < project.tracks.size(); ++row) if (owner.presenter.recordingTrack(project.tracks[row].id))
            {
                const juce::Rectangle<float> bounds(static_cast<float>(left), static_cast<float>(laneTop(owner.presenter, static_cast<int>(row)) + 2),
                    static_cast<float>(right - left), static_cast<float>(laneHeight(owner.presenter, project.tracks[row].id) - 4));
                graphics.setColour(juce::Colour(0xffe55b50).withAlpha(0.18f)); graphics.fillRect(bounds);
                graphics.setColour(juce::Colour(0xffe55b50)); graphics.drawRect(bounds, 1);
                graphics.drawText("Recording", bounds.toNearestInt().reduced(5, 0).withHeight(19), juce::Justification::centredLeft, true);
            }
        }
        for (const auto& item : owner.presenter.loading())
        {
            int index = trackIndex(item.track); if (index < 0) index = static_cast<int>(project.tracks.size());
            juce::Rectangle<int> bounds(static_cast<int>(item.start * owner.presenter.viewState().pixelsPerSecond),
                                        laneTop(owner.presenter, index) + 2, 160, laneHeight(owner.presenter, item.track) - 4);
            graphics.setColour(panel); graphics.fillRect(bounds);
            graphics.setColour(ink); graphics.drawText("Loading: " + item.name, bounds.reduced(6), juce::Justification::centredLeft, true);
        }
        if (project.tracks.empty() && owner.presenter.loading().empty())
        { graphics.setColour(ink.withAlpha(0.7f)); graphics.setFont(juce::FontOptions(12));
          graphics.drawText("Drop audio files here", owner.lanes.getViewArea().withY(92).withHeight(26), juce::Justification::centred); }
        if (const auto& selection = owner.presenter.viewState().timeSelection)
        {
            const auto selectionColour = accent;
            const auto zoomIn = owner.presenter.viewState().pixelsPerSecond;
            const auto from = static_cast<float>(std::min(static_cast<double>(getWidth()), selection->start * zoomIn));
            const auto to = static_cast<float>(std::min(static_cast<double>(getWidth()), selection->end * zoomIn));
            if (to > from) for (const auto& track : selection->tracks)
            {
                juce::Rectangle<float> bounds(from, static_cast<float>(laneTop(owner.presenter, trackIndex(track))),
                                              to - from, static_cast<float>(laneHeight(owner.presenter, track)));
                graphics.setColour(selectionColour); graphics.drawRect(bounds, 1);
            }
            const auto markerRow = selection->tracks.isEmpty() ? 0 : trackIndex(selection->tracks[0]);
            graphics.setColour(selectionColour);
            const auto markerHeight = selection->tracks.isEmpty() ? rowHeight : laneHeight(owner.presenter, selection->tracks[0]);
            graphics.fillRect(from - 1, static_cast<float>(laneTop(owner.presenter, markerRow)), 2.0f, static_cast<float>(markerHeight));
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown())
        { bool clipAtPoint = false; for (const auto& clip : owner.presenter.project().clips) if (boundsFor(clip).contains(event.position)) clipAtPoint = true;
          if (!clipAtPoint && event.y >= 0) showTrackCreationMenu(owner.presenter, *this); return; }
        if (!event.mods.isLeftButtonDown()) return;
        grabKeyboardFocus();
        down = event.position; selectionBefore = owner.presenter.viewState().selectedClips;
        timeBefore = owner.presenter.viewState().timeSelection;
        if (event.getNumberOfClicks() == 1) doubleClickSelection = timeBefore;
        dragRange.reset();
        originals.clear(); envelopesBefore.clear(); mode = -1;
        copying = event.mods.isAltDown(); copiesInserted = false;
        for (auto it = owner.presenter.project().clips.rbegin(); it != owner.presenter.project().clips.rend(); ++it)
        {
            const auto edge = headerEdge(*it, event.position);
            if (edge >= 0)
            {
                original = *it;
                const auto seconds = event.x / owner.presenter.viewState().pixelsPerSecond;
                const bool additive = event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown();
                if (edge == 0 && !additive && timeBefore && timeBefore->tracks.contains(it->track)
                    && seconds >= timeBefore->start && seconds < timeBefore->end)
                { dragRange = timeBefore; mode = 0; owner.presenter.beginGesture(); repaint(); return; }
                if (edge > 0 && timeBefore && clipInTimeSelection(*it, *timeBefore))
                {
                    juce::StringArray ids;
                    for (const auto& clip : owner.presenter.project().clips)
                        if (clipInTimeSelection(clip, *timeBefore)) ids.add(clip.id);
                    owner.presenter.selectClips(std::move(ids));
                }
                else if (edge == 0 && additive) owner.presenter.selectClip(it->id, true);
                else if (!owner.presenter.viewState().selectedClips.contains(it->id)) owner.presenter.selectClip(it->id, additive);
                if (!owner.presenter.viewState().selectedClips.contains(it->id)) { repaint(); return; }
                for (const auto& clip : owner.presenter.project().clips)
                    if (owner.presenter.viewState().selectedClips.contains(clip.id)) originals.push_back(clip);
                mode = edge;
                owner.presenter.beginGesture(); repaint(); return;
            }
        }
        mode = 3;
        if (event.mods.isShiftDown() && timeBefore && !timeBefore->tracks.isEmpty())
        { down.x = static_cast<float>(timeBefore->start * owner.presenter.viewState().pixelsPerSecond);
          down.y = static_cast<float>(laneTop(owner.presenter, trackIndex(timeBefore->tracks[0])) + 2); }
        auto selection = timeSelectionAt(event.position);
        if (!event.mods.isShiftDown()) selection.end = selection.start = gridTimeAt(event.x);
        owner.presenter.selectTime(selection); owner.presenter.beginGesture();
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!owner.presenter.gesturing() || mode < 0) return;
        if (mode == 3)
        {
            owner.presenter.selectTime(timeSelectionAt(event.position)); repaint(); return;
        }
        if (event.getDistanceFromDragStart() < 3) return;
        if (dragRange && originals.empty())
        {
            if (copying)
            {
                for (const auto& clip : owner.presenter.project().clips)
                    if (const auto part = clipInTimeSelection(clip, *dragRange)) originals.push_back(*part);
            }
            else originals = owner.presenter.splitTimeSelection();
            const auto found = std::find_if(originals.begin(), originals.end(), [&](const auto& clip) { return clip.id == original.id; });
            if (found == originals.end()) { finishGesture(true); return; }
            original = *found;
        }
        const auto delta = (event.position.x - down.x) / owner.presenter.viewState().pixelsPerSecond;
        if (mode == 1 || mode == 2)
        {
            const auto anchor = mode == 1 ? original.start : original.start + original.duration;
            const auto shift = snappedTime(anchor + delta, owner.presenter.viewState().snap) - anchor;
            auto resized = originals;
            for (auto& clip : resized)
            {
                if (mode == 1) clip = trimLeft(clip, clip.start + shift);
                else
                {
                    const auto* asset = findId(owner.presenter.project().assets, clip.asset);
                    if (asset && asset->missing) return;
                    const auto end = clip.start + clip.duration + shift;
                    clip = trimRight(clip, end, asset ? static_cast<double>(asset->length) / asset->sampleRate
                        : clip.offset + std::max(clip.duration, end - clip.start) * clip.playbackRate);
                }
            }
            owner.presenter.changeClips(resized); return;
        }
        else
        {
            auto moved = originals;
            double earliest = original.start;
            int firstTrack = trackIndex(original.track), lastTrack = firstTrack;
            for (const auto& value : originals)
            { earliest = std::min(earliest, value.start); firstTrack = std::min(firstTrack, trackIndex(value.track)); lastTrack = std::max(lastTrack, trackIndex(value.track)); }
            if (dragRange)
            {
                earliest = std::min(earliest, dragRange->start);
                for (const auto& track : dragRange->tracks)
                { firstTrack = std::min(firstTrack, trackIndex(track)); lastTrack = std::max(lastTrack, trackIndex(track)); }
            }
            const auto anchorStart = dragRange ? dragRange->start : original.start;
            const auto shift = std::max(-earliest, snappedTime(anchorStart + delta, owner.presenter.viewState().snap) - anchorStart);
            const auto targetTrack = trackIndex(trackAt(event.y));
            const auto trackShift = targetTrack < 0 ? 0 : juce::jlimit(-firstTrack,
                static_cast<int>(owner.presenter.project().tracks.size()) - 1 - lastTrack, targetTrack - trackIndex(original.track));
            for (auto& value : moved)
            { value.start += shift; value.track = owner.presenter.project().tracks[static_cast<size_t>(trackIndex(value.track) + trackShift)].id; }
            if (copying && !copiesInserted)
            {
                if (std::abs(shift) < 1.0e-9 && trackShift == 0) return;
                auto copies = originals; juce::StringArray ids;
                for (auto& clip : copies) { clip.id = newId(); ids.add(clip.id); }
                if (!owner.presenter.insertClips(copies)) { finishGesture(true); return; }
                for (size_t i = 0; i < copies.size(); ++i) originals[i].id = copies[i].id;
                copiesInserted = true; owner.presenter.selectClips(ids);
            }
            if (copying) for (size_t i = 0; i < moved.size(); ++i) moved[i].id = originals[i].id;
            if (!owner.presenter.changeClips(moved, movedEnvelopes(moved)))
            { if (copying) finishGesture(true); return; }
            if (dragRange)
            {
                auto selection = *dragRange; selection.start += shift; selection.end += shift;
                for (auto& track : selection.tracks) track = owner.presenter.project().tracks[static_cast<size_t>(trackIndex(track) + trackShift)].id;
                owner.presenter.selectTime(std::move(selection));
            }
            return;
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto* target = findId(owner.presenter.project().tracks, trackAt(event.y));
        if (mode == 0 && !copying && original.asset.isNotEmpty() && target && target->midi && event.getDistanceFromDragStart() >= 3)
        { const auto track = target->id; const auto source = original; finishGesture(true);
          owner.presenter.loadSampler(track, source.asset, source.offset, source.duration); }
        else finishGesture(false);
        mouseMove(event);
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        // JUCE sends double-click immediately after mouseDown, before the resize drag.
        if (!event.mods.isLeftButtonDown() || mode == 1 || mode == 2 || event.mods.isAltDown() || event.mods.isShiftDown() || event.mods.isCommandDown() || event.mods.isCtrlDown()) return;
        const auto trackId = trackAt(event.y); const auto* track = findId(owner.presenter.project().tracks, trackId);
        if (!track || !track->midi) return;
        finishGesture(false);
        const auto seconds = event.x / owner.presenter.viewState().pixelsPerSecond;
        const auto selection = doubleClickSelection;
        const bool inSelection = selection && selection->end > selection->start && selection->tracks.contains(trackId)
            && seconds >= selection->start && seconds < selection->end;
        for (const auto& clip : owner.presenter.project().clips) if (clip.track == trackId && boundsFor(clip).contains(event.position))
        {
            if (boundsFor(clip).withHeight(19).contains(event.position))
            { if (owner.onMidiClip) owner.onMidiClip(clip.id); return; }
            if (!inSelection) return;
        }
        Clip clip { newId(), trackId, {}, inSelection ? selection->start : gridTimeAt(event.x), 0,
                    inSelection ? selection->end - selection->start : 240 / owner.presenter.project().bpm };
        owner.presenter.insertClip(clip, false);
        if (findId(owner.presenter.project().clips, clip.id) && owner.onMidiClip) owner.onMidiClip(clip.id);
    }
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override
    {
        if (owner.presenter.gesturing() || !std::isfinite(scaleFactor) || scaleFactor <= 0) return;
        const auto oldZoom = owner.presenter.viewState().pixelsPerSecond;
        const double pointerX = event.position.x;
        const auto viewX = pointerX - owner.lanes.getViewPositionX();
        owner.presenter.setZoom(oldZoom * scaleFactor);
        const auto nextZoom = owner.presenter.viewState().pixelsPerSecond;
        const auto scroll = pointerX / oldZoom * nextZoom - viewX;
        owner.presenter.setScroll(static_cast<int>(std::llround(juce::jlimit(0.0, 10000000.0, scroll))), owner.presenter.viewState().scrollY);
        owner.zoom.setValue(nextZoom, juce::dontSendNotification); owner.refresh();
    }
    std::vector<AutomationEdit> movedEnvelopes(const std::vector<Clip>& moved)
    {
        if (owner.presenter.viewState().lockAutomation) return {};
        const auto capture = [&](const juce::String& id)
        {
            if (envelopesBefore.count(id)) return;
            const auto* track = findTrack(owner.presenter.project(), id); if (!track) return;
            auto& values = envelopesBefore[id];
            for (int parameter = 0; parameter < 3 + returnCount(owner.presenter.project()); ++parameter) values.push_back(automationPoints(*track, parameter));
        };
        for (const auto& clip : originals) capture(clip.track); for (const auto& clip : moved) capture(clip.track);
        auto changed = envelopesBefore;
        for (size_t i = 0; i < originals.size(); ++i)
        {
            const auto& source = originals[i]; const auto& destination = moved[i];
            for (size_t parameter = 0; parameter < envelopesBefore[source.track].size(); ++parameter)
            {
                const auto& points = envelopesBefore[source.track][parameter]; auto& target = changed[destination.track][parameter]; auto& old = changed[source.track][parameter];
                const auto inside = [&](const auto& point) { return point.time >= source.start && point.time < source.start + source.duration; };
                if (std::none_of(points.begin(), points.end(), inside)) continue;
                if (!copying) old.erase(std::remove_if(old.begin(), old.end(), inside), old.end());
                target.erase(std::remove_if(target.begin(), target.end(), [&](const auto& point) { return point.time >= destination.start && point.time < destination.start + destination.duration; }), target.end());
            }
        }
        for (size_t i = 0; i < originals.size(); ++i)
        {
            const auto& source = originals[i]; const auto& destination = moved[i];
            for (size_t parameter = 0; parameter < envelopesBefore[source.track].size(); ++parameter)
                for (auto point : envelopesBefore[source.track][parameter]) if (point.time >= source.start && point.time < source.start + source.duration)
                { point.time += destination.start - source.start; changed[destination.track][parameter].push_back(point); }
        }
        std::vector<AutomationEdit> edits;
        for (auto& [track, values] : changed) for (size_t parameter = 0; parameter < values.size(); ++parameter)
        {
            auto& points = values[parameter]; std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
            std::vector<AutomationPoint> unique;
            for (const auto& point : points) { if (!unique.empty() && std::abs(unique.back().time - point.time) < 1.0e-9) unique.back() = point; else unique.push_back(point); }
            edits.push_back({track, static_cast<int>(parameter), std::move(unique)});
        }
        return edits;
    }
    void finishGesture(bool cancel)
    {
        if (cancel && mode >= 0)
        { if (timeBefore) owner.presenter.selectTime(*timeBefore); else owner.presenter.selectClips(selectionBefore); }
        mode = -1; originals.clear(); dragRange.reset(); copying = copiesInserted = false;
        if (owner.presenter.gesturing()) owner.presenter.endGesture(cancel);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
    void cancelGesture()
    { if (owner.presenter.gesturing()) finishGesture(true); else owner.presenter.selectClips({}); }
    bool isInterestedInFileDrag(const juce::StringArray& files) override { return !files.isEmpty(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override
    { const auto track = trackAt(y); const auto* target = findId(owner.presenter.project().tracks, track);
      if (owner.onFiles) owner.onFiles(target && target->midi && !files.isEmpty() ? juce::StringArray { files[0] } : files, track, timeAt(x)); }
    bool isInterestedInDragSource(const SourceDetails& details) override
    { return owner.presenter.isBrowserDrag(details.description.toString()) || details.description.toString().startsWith("asset:") || details.description.toString().startsWith("clip:")
        || details.description.toString().startsWith("instrument:") || details.description.toString().startsWith("plugin:") || details.description.toString().startsWith("tracks:"); }
    void itemDropped(const SourceDetails& details) override
    {
        const auto trackId = trackAt(details.localPosition.y); const auto* target = findId(owner.presenter.project().tracks, trackId);
        if (details.description.toString().startsWith("tracks:")) { owner.presenter.dropTracks(details.description.toString(), trackId); return; }
        if (owner.presenter.isBrowserDrag(details.description.toString())) { owner.presenter.dropBrowserItem(details.description.toString(), trackId, timeAt(details.localPosition.x)); return; }
        if (details.description.toString().startsWith("plugin:")) { owner.presenter.dropPlugin(details.description.toString(), trackId); return; }
        if (details.description.toString().startsWith("instrument:"))
        { owner.presenter.moveSampler(details.description.toString().substring(11), trackId); return; }
        if (details.description.toString().startsWith("clip:"))
        {
            if (target && target->midi)
                if (const auto* clip = findId(owner.presenter.project().clips, details.description.toString().substring(5)))
                    owner.presenter.loadSampler(trackId, clip->asset, clip->offset, clip->duration * clip->playbackRate);
            return;
        }
        const auto* asset = findId(owner.presenter.project().assets, details.description.toString().substring(6));
        if (!asset || asset->missing) return;
        if (target && target->midi)
        { owner.presenter.loadSampler(trackId, asset->id, 0, static_cast<double>(asset->length) / asset->sampleRate); return; }
        auto track = trackAt(details.localPosition.y); const bool create = track.isEmpty(); if (create) track = newId();
        owner.presenter.insertClip({ newId(), track, asset->id, timeAt(details.localPosition.x), 0, static_cast<double>(asset->length) / asset->sampleRate }, create);
    }
private:
    void drawWaveform(juce::Graphics& graphics, juce::AudioThumbnail& thumbnail, const juce::String& file,
                      const Clip& clip, juce::Rectangle<int> bounds, juce::Colour waveformColour)
    {
        // Cache the viewport so playhead-only repaints do not decode audio again.
        const auto visible = bounds.getIntersection(owner.lanes.getViewArea());
        if (visible.isEmpty()) return;
        const juce::Rectangle<int> area(visible.getX(), bounds.getY(), visible.getWidth(), bounds.getHeight());
        const auto secondsPerPixel = clip.duration * clip.playbackRate / bounds.getWidth();
        const auto from = clip.offset + (area.getX() - bounds.getX()) * secondsPerPixel;
        const auto to = clip.offset + (area.getRight() - bounds.getX()) * secondsPerPixel;
        const auto scale = graphics.getInternalContext().getPhysicalPixelScaleFactor();
        const juce::Point<int> size(area.getWidth(), area.getHeight()); auto& cached = waveforms[clip.id];
        if (!cached.image.isValid() || std::tie(cached.file, cached.from, cached.to, cached.scale, cached.size, cached.colour)
                                      != std::tie(file, from, to, scale, size, waveformColour))
        {
            cached.image = juce::Image(juce::Image::ARGB, static_cast<int>(std::ceil(static_cast<double>(size.x) * scale)),
                                      static_cast<int>(std::ceil(static_cast<double>(size.y) * scale)), true);
            juce::Graphics waveform(cached.image); waveform.addTransform(juce::AffineTransform::scale(scale));
            waveform.setColour(waveformColour);
            const auto* asset = findId(owner.presenter.project().assets, clip.asset);
            if (!asset || !detail.draw(waveform, file, { 0, 0, static_cast<float>(size.x), static_cast<float>(size.y) }, from, to, asset->sampleRate, waveformColour))
                waveform.fillPath(waveformEnvelope(thumbnail, { 0, 0, static_cast<float>(size.x), static_cast<float>(size.y) }, from, to));
            cached.file = file; cached.from = from; cached.to = to; cached.scale = scale; cached.size = size; cached.colour = waveformColour;
#if defined(OPENTONE_UI_TESTING)
            getProperties().set("waveform-renders", static_cast<int>(getProperties()["waveform-renders"]) + 1);
            getProperties().set("waveform-max-width", std::max(size.x, static_cast<int>(getProperties()["waveform-max-width"])));
#endif
        }
        graphics.setOpacity(1.0f); graphics.drawImage(cached.image, area.toFloat());
    }
    void changeListenerCallback(juce::ChangeBroadcaster* changed) override
    {
        if (changed == &detail) { waveforms.clear(); repaint(); return; }
        for (const auto& thumbnail : thumbnails)
        {
            if (thumbnail.second.get() != changed) continue;
            for (auto it = waveforms.begin(); it != waveforms.end();)
            {
                if (it->second.file == thumbnail.first) it = waveforms.erase(it); else ++it;
            }
        }
        repaint();
    }
    Timeline& owner; Clip original; std::vector<Clip> originals;
    juce::Point<float> down; int mode = -1; bool copying = false, copiesInserted = false;
    juce::StringArray selectionBefore;
    std::optional<TimeSelection> timeBefore, dragRange, doubleClickSelection;
    std::array<juce::MouseCursor, 2> trimCursors;
    juce::AudioFormatManager formats;
    WaveformDetail detail;
    juce::AudioThumbnailCache cache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    struct Waveform
    {
        juce::Image image; juce::String file; juce::Point<int> size; juce::Colour colour;
        double from = 0, to = 0; float scale = 0;
    };
    std::map<juce::String, Waveform> waveforms;
    std::map<juce::String, std::vector<std::vector<AutomationPoint>>> envelopesBefore;
    std::map<juce::String, std::unique_ptr<AutomationLane>> envelopes;
public:
    bool automationCommand(char command)
    { for (auto& item : envelopes) if (item.second->hasKeyboardFocus(true)) return item.second->keyPressed(juce::KeyPress(command, juce::ModifierKeys::commandModifier, 0)); return false; }
    void registerFormats() { formats.registerBasicFormats(); }
};
Timeline::Timeline(EditorPresenter& owner) : presenter(owner)
{
    canvas = std::make_unique<Canvas>(*this); canvas->registerFormats();
    trackHeaders = std::make_unique<Headers>(presenter); master = std::make_unique<Master>(*this); addAndMakeVisible(*master);
    returns = std::make_unique<Returns>(*this); addAndMakeVisible(*returns);
    loopBrace = std::make_unique<LoopBrace>(presenter); addAndMakeVisible(*loopBrace);
    timeRuler = std::make_unique<TimeRuler>(presenter); addAndMakeVisible(*timeRuler);
    beatRuler = std::make_unique<BeatRuler>(*this); addAndMakeVisible(*beatRuler);
    for (auto* button : {&setLocator, &automationButton, &automationLock, &previousLocator, &nextLocator}) addAndMakeVisible(button);
    automationButton.setClickingTogglesState(true); automationButton.getProperties().set("footer-icon", "automation");
    automationButton.onClick = [this] { presenter.setAutomationView(automationButton.getToggleState()); };
    automationLock.getProperties().set("footer-icon", "lock"); automationLock.onClick = [this] { presenter.setAutomationLock(!presenter.viewState().lockAutomation); }; automationLock.setTitle("Lock envelopes: keep automation in place when moving clips");
    previousLocator.getProperties().set("footer-icon", "previous"); nextLocator.getProperties().set("footer-icon", "next");
    setLocator.onClick = [this] { presenter.addLocator(); };
    previousLocator.onClick = [this] { presenter.jumpLocator(false); }; nextLocator.onClick = [this] { presenter.jumpLocator(true); };
    lanes.setViewedComponent(canvas.get(), false); headers.setViewedComponent(trackHeaders.get(), false);
    lanes.addMouseListener(this, true);
    lanes.onZoom = [this](const auto& event, float scale) { canvas->mouseMagnify(event.getEventRelativeTo(canvas.get()), scale); };
    headers.setScrollBarsShown(false, false); headers.wheelTarget = &lanes; lanes.setScrollBarsShown(true, true);
    lanes.getVerticalScrollBar().setAutoHide(true);
    lanes.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    lanes.onScroll = [this](auto bounds) { headers.setViewPosition(0, bounds.getY()); if (!restoring) presenter.setScroll(bounds.getX(), bounds.getY()); master->repaint(); returns->refresh(); returns->repaint(); loopBrace->repaint(); timeRuler->repaint(); beatRuler->repaint(); repaint(); };
    for (auto* item : std::initializer_list<juce::Component*> { &snap, &zoom, &headers, &lanes }) addAndMakeVisible(item);
    snap.setToggleState(presenter.viewState().snap, juce::dontSendNotification); snap.setTitle("Snap clip edits and time selections to the visible grid; zoom in for finer divisions");
    snap.onClick = [this] { presenter.setSnap(snap.getToggleState()); canvas->repaint(); };
    zoom.setRange(8, 40000); zoom.setSkewFactorFromMidPoint(256); zoom.setValue(presenter.viewState().pixelsPerSecond, juce::dontSendNotification);
    zoom.setTitle("Timeline zoom in pixels per second"); zoom.setSliderStyle(juce::Slider::LinearHorizontal);
    zoom.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoom.onValueChange = [this] { presenter.setZoom(zoom.getValue()); refresh(); };
}
Timeline::~Timeline() = default;
void Timeline::paint(juce::Graphics& graphics)
{
    graphics.fillAll(panel); graphics.setColour(line); graphics.drawRect(getLocalBounds());
    graphics.setColour(ink); graphics.setFont(juce::FontOptions(11));
    const auto overview = juce::Rectangle<float>(5, 4, static_cast<float>(std::max(1, getWidth() - headerWidth - 12)), 14);
    graphics.setColour(background); graphics.fillRoundedRectangle(overview, 2);
    double duration = 30;
    for (const auto& clip : presenter.project().clips) if (clip.scene < 0) duration = std::max(duration, clip.start + clip.duration);
    const auto scale = overview.getWidth() / duration;
    for (const auto& clip : presenter.project().clips)
    {
        if (clip.scene >= 0) continue;
        graphics.setColour(clipColour(presenter.project(), clip));
        graphics.fillRect(overview.getX() + static_cast<float>(clip.start * scale), overview.getY() + 5, std::max(1.0f, static_cast<float>(clip.duration * scale)), 3.0f);
    }
    const auto& view = presenter.viewState();
    auto visible = overview.withX(overview.getX() + static_cast<float>(view.scrollX / view.pixelsPerSecond * scale))
        .withWidth(static_cast<float>(lanes.getViewWidth() / view.pixelsPerSecond * scale)).getIntersection(overview);
    graphics.setColour(ink.withAlpha(0.5f)); graphics.drawRoundedRectangle(visible, 2, 1);
}
void Timeline::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isMiddleButtonDown() && event.eventComponent != this && !presenter.gesturing())
    { panning = true; panStart = event.getScreenPosition(); panScroll = lanes.getViewPosition();
      canvas->setMouseCursor(juce::MouseCursor::DraggingHandCursor); return; }
    if (event.eventComponent != this) return;
    if (!event.mods.isLeftButtonDown() || event.y >= 22 || event.x < 5 || event.x >= getWidth() - headerWidth) return;
    double duration = 30;
    for (const auto& clip : presenter.project().clips) if (clip.scene < 0) duration = std::max(duration, clip.start + clip.duration);
    const auto time = (event.x - 5.0) / std::max(1, getWidth() - headerWidth - 12) * duration;
    lanes.setViewPosition(juce::roundToInt(std::max(0.0, time * presenter.viewState().pixelsPerSecond - lanes.getViewWidth() * 0.5)), lanes.getViewPositionY());
}
void Timeline::mouseDrag(const juce::MouseEvent& event)
{
    if (panning) { lanes.setViewPosition(panScroll - (event.getScreenPosition() - panStart)); return; }
    if (event.eventComponent == this && event.getMouseDownY() < 22) mouseDown(event);
}
void Timeline::mouseUp(const juce::MouseEvent&)
{
    if (!panning) return;
    panning = false; canvas->setMouseCursor(juce::MouseCursor::NormalCursor);
}
void Timeline::resized()
{
    restoring = true;
    auto bounds = getLocalBounds().reduced(1); auto toolbar = bounds.removeFromTop(22).reduced(3);
    snap.setBounds(toolbar.removeFromRight(52)); zoom.setBounds(toolbar.removeFromRight(100));
    auto time = bounds.removeFromBottom(18); time.removeFromRight(headerWidth); timeRuler->setBounds(time);
    fixedBraceHeight = presenter.project().locators.empty() ? 18 : 34;
    auto fixed = bounds.removeFromTop(rulerHeight + fixedBraceHeight);
    const auto tools = fixed.removeFromRight(headerWidth);
    setLocator.setBounds(tools.getX() + 4, tools.getY() + 2, 30, 16);
    automationButton.setBounds(tools.getX() + 39, tools.getY() + 2, 25, 16);
    automationLock.setBounds(tools.getX() + 69, tools.getY() + 2, 25, 16);
    previousLocator.setBounds(tools.getX() + 4, tools.getY() + 22, 15, 15);
    nextLocator.setBounds(tools.getX() + 22, tools.getY() + 22, 15, 15);
    beatRuler->setBounds(fixed.withHeight(rulerHeight));
    headers.setBounds(bounds.removeFromRight(headerWidth)); lanes.setBounds(bounds); refresh();
}
void Timeline::refresh()
{
    if (fixedBraceHeight != (presenter.project().locators.empty() ? 18 : 34)) { resized(); return; }
    restoring = true;
    double end = std::max({30.0, presenter.position() + 10, presenter.project().loopEnd + 10});
    for (const auto& clip : presenter.project().clips) if (clip.scene < 0) end = std::max(end, clip.start + clip.duration + 10);
    for (const auto* track : allTracks(presenter.project())) for (const auto* points : { &track->volumeAutomation, &track->panAutomation })
        if (!points->empty()) end = std::max(end, points->back().time + 10);
    const auto height = std::max(lanes.getHeight() - lanes.getScrollBarThickness(),
                                laneTop(presenter, static_cast<int>(presenter.project().tracks.size())) + returns->requiredHeight() + rowHeight);
    // shortcut: canvas is capped at ten million pixels, tile the viewport for longer arrangements.
    const auto width = std::max(lanes.getWidth() - lanes.getScrollBarThickness(), static_cast<int>(std::min(10000000.0, end * presenter.viewState().pixelsPerSecond)));
    canvas->setSize(width, height); trackHeaders->setSize(headerWidth, height); trackHeaders->refresh();
    headers.setBounds(lanes.getRight(), lanes.getY(), headerWidth, lanes.getViewHeight());
    beatRuler->setSize(lanes.getViewWidth(), rulerHeight);
    const auto braceHeight = presenter.project().locators.empty() ? 18 : 34;
    loopBrace->setBounds(lanes.getX(), lanes.getY() - braceHeight, lanes.getViewWidth(), braceHeight); loopBrace->repaint();
    master->setBounds(lanes.getX(), lanes.getY() + lanes.getViewHeight() - master->desiredHeight(), lanes.getWidth() + headerWidth, master->desiredHeight());
    returns->setBounds(lanes.getX(), master->getY() - returns->requiredHeight(), lanes.getWidth() + headerWidth, returns->requiredHeight());
    returns->refresh(); returns->toFront(false); master->toFront(false);
    lanes.setViewPosition(presenter.viewState().scrollX, presenter.viewState().scrollY);
    headers.setViewPosition(0, lanes.getViewPositionY());
    restoring = false;
    presenter.setScroll(lanes.getViewPositionX(), lanes.getViewPositionY());
    automationButton.setToggleState(presenter.viewState().automationView, juce::dontSendNotification); automationLock.setToggleState(presenter.viewState().lockAutomation, juce::dontSendNotification);
    canvas->refresh(); timeRuler->repaint(); beatRuler->repaint(); repaint(); tick();
}
void Timeline::tick()
{
    master->tick(); master->repaint(); returns->tick(); returns->repaint(); trackHeaders->tick();
    if (presenter.recording()) canvas->repaint(lanes.getViewArea());
    if (presenter.playing() && !restoring && canvas->getWidth() < 10000000
        && presenter.position() * presenter.viewState().pixelsPerSecond >= canvas->getWidth()) { refresh(); return; }
    if (presenter.playing() && presenter.viewState().follow)
    {
        const auto x = presenter.position() * presenter.viewState().pixelsPerSecond;
        if (x >= lanes.getViewPositionX() + lanes.getViewWidth() - 40 || x < lanes.getViewPositionX())
            lanes.setViewPosition(juce::roundToInt(std::max(0.0, x - lanes.getViewWidth() * 0.25)), lanes.getViewPositionY());
    }
    canvas->playhead.setBounds(static_cast<int>(std::min(static_cast<double>(canvas->getWidth()), presenter.position() * presenter.viewState().pixelsPerSecond)), 0, 2, canvas->getHeight()); }
bool Timeline::trackFocused() const { return (trackHeaders->hasKeyboardFocus(true) || returns->hasKeyboardFocus(true)); }
void Timeline::deleteSelection() { canvas->finishGesture(false); presenter.deleteSelection(); }
void Timeline::cancelGesture() { canvas->cancelGesture(); }
bool Timeline::automationCommand(char command)
{ return isVisible() && (canvas->automationCommand(command) || master->automationCommand(command) || returns->automationCommand(command)); }

}
