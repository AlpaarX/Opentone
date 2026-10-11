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
int laneHeight(const EditorPresenter& presenter, const juce::String& id)
{ const auto found = presenter.viewState().trackHeights.find(id); return found == presenter.viewState().trackHeights.end() ? rowHeight : found->second; }
int laneTop(const EditorPresenter& presenter, int row)
{
    int y = rulerHeight;
    for (int i = 0; i < row && i < static_cast<int>(presenter.project().tracks.size()); ++i)
        y += laneHeight(presenter, presenter.project().tracks[static_cast<size_t>(i)].id);
    return y;
}
int laneRowAt(const EditorPresenter& presenter, int y)
{
    if (y < rulerHeight) return -1;
    int top = rulerHeight;
    for (size_t row = 0; row < presenter.project().tracks.size(); ++row)
    { top += laneHeight(presenter, presenter.project().tracks[row].id); if (y < top) return static_cast<int>(row); }
    return static_cast<int>(presenter.project().tracks.size());
}
}
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
    explicit Master(Timeline& owner) : timeline(owner), presenter(owner.presenter), automation(presenter, "master")
    {
        addAndMakeVisible(name); addAndMakeVisible(volume); addAndMakeVisible(meter); addAndMakeVisible(pan); addAndMakeVisible(gainValue); addAndMakeVisible(parameter); addChildComponent(automation); label(name, "Master");
        name.addMouseListener(this, false); gainValue.addMouseListener(this, false); setWantsKeyboardFocus(true);
        gainValue.getProperties().set("track-title", true);
        configurePanSlider(pan); pan.onValueChange = [this] { presenter.setPan("master", pan.getValue()); };
        pan.onDragStart = [this] { presenter.beginGesture(); }; pan.onDragEnd = [this] { presenter.endGesture(false); };
        parameter.addItem("Volume", 1); parameter.addItem("Pan", 2);
        parameter.onChange = [this] { presenter.setAutomationPan("master", parameter.getSelectedId() == 2); };
        name.setJustificationType(juce::Justification::centredLeft);
        name.setFont(juce::FontOptions(12));
        name.getProperties().set("track-title", true);
        name.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        name.setColour(juce::Label::textColourId, trackColour("master").contrasting());
        volume.setRange(0, 2, 0.001); volume.setSliderStyle(juce::Slider::LinearVertical);
        volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); setSliderDefault(volume, 1);
        volume.onDragStart = [this] { presenter.beginGesture(); }; volume.onDragEnd = [this] { presenter.endGesture(false); };
        volume.setTitle("Master volume"); volume.onValueChange = [this] { presenter.setMaster(static_cast<float>(volume.getValue())); };
    }
    void tick()
    {
        if (!volume.isMouseButtonDown()) volume.setValue(presenter.trackValue("master", false), juce::dontSendNotification);
        if (!pan.isMouseButtonDown()) pan.setValue(presenter.trackValue("master", true), juce::dontSendNotification);
        label(gainValue, volumeText(volume.getValue())); gainValue.setFont(juce::FontOptions(10)); gainValue.setColour(juce::Label::textColourId, trackColour("master").contrasting());
        meter.setLevels(presenter.masterLevels()); parameter.setVisible(presenter.viewState().automationView);
        parameter.setSelectedId(presenter.automationPan("master") ? 2 : 1, juce::dontSendNotification);
        automation.setBounds(0, 22, timeline.lanes.getViewWidth(), getHeight() - 24);
        automation.setVisible(presenter.viewState().automationView); automation.setView(presenter.viewState().scrollX / presenter.viewState().pixelsPerSecond, presenter.viewState().pixelsPerSecond);
    }
    void resized() override
    {
        const auto header = getLocalBounds().removeFromRight(headerWidth);
        name.setBounds(header.getX() + 5, 3, header.getWidth() - 59, 23);
        volume.setBounds(header.getRight() - 45, 6, 24, getHeight() - 32);
        meter.setBounds(header.getRight() - 17, 6, 12, getHeight() - 32);
        gainValue.setBounds(header.getRight() - 64, getHeight() - 23, 59, 20);
        pan.setBounds(header.getX() + 103, 30, 73, 36); parameter.setBounds(header.getX() + 9, 52, 88, 18);
        automation.setBounds(0, 22, timeline.lanes.getViewWidth(), getHeight() - 24);
    }
    void paint(juce::Graphics& graphics) override
    {
        const auto header = getLocalBounds().removeFromRight(headerWidth);
        const auto lane = getLocalBounds().withWidth(timeline.lanes.getViewWidth());
        graphics.setColour(background); graphics.fillRect(lane);
        const auto& view = presenter.viewState();
        const auto beat = 60.0 / presenter.project().bpm;
        const auto step = timelineGridStep(presenter.project().bpm, view.pixelsPerSecond);
        const auto spacing = step * view.pixelsPerSecond;
        for (int i = static_cast<int>(view.scrollX / spacing); i * spacing < view.scrollX + lane.getWidth(); ++i)
        {
            const bool bar = std::abs(std::remainder(i * step, beat * 4)) < step * 0.01;
            graphics.setColour(bar ? line : line.withAlpha(0.35f));
            graphics.drawVerticalLine(static_cast<int>(i * spacing) - view.scrollX, 0, static_cast<float>(getHeight()));
        }
        const auto needle = presenter.position() * view.pixelsPerSecond - view.scrollX;
        if (needle >= 0 && needle < lane.getWidth())
        { graphics.setColour(ink); graphics.fillRect(static_cast<int>(needle), 0, 2, getHeight()); }
        graphics.setColour(trackColour("master", presenter.viewState().selectedTrack == "master")); graphics.fillRect(header);
        graphics.setColour(line); graphics.drawHorizontalLine(0, 0, static_cast<float>(lane.getWidth())); graphics.drawRect(header);
    }
    void mouseDown(const juce::MouseEvent& event) override
    { if (event.getEventRelativeTo(this).x >= getWidth() - headerWidth) { presenter.selectTrack("master"); grabKeyboardFocus(); } }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    { if (event.originalComponent == &gainValue) volume.setValue(1, juce::sendNotificationSync); }
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
private: Timeline& timeline; EditorPresenter& presenter; juce::Label name, gainValue; juce::Slider volume, pan; juce::ComboBox parameter; LevelMeter meter; AutomationLane automation;
};
class Timeline::Headers final : public juce::Component
{
public:
    struct Row : juce::Component, juce::DragAndDropTarget
    {
        Row(EditorPresenter& owner, juce::String trackId, int number) : presenter(owner), id(std::move(trackId))
        {
            setWantsKeyboardFocus(true); setTitle("Track header; drag the bottom border to resize height");
            for (auto* item : std::initializer_list<juce::Component*> { &name, &gain, &pan, &gainValue, &parameter, &mute, &solo, &arm, &meter }) addAndMakeVisible(item);
            name.getProperties().set("track-title", true);
            name.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack); name.setFont(juce::FontOptions(12));
            name.setEditable(false, true); name.setTitle("Track name; double click to rename");
            name.addMouseListener(this, false);
            name.onTextChange = [this] { presenter.renameTrack(id, name.getText()); };
            gainValue.getProperties().set("track-title", true); gainValue.addMouseListener(this, false);
            configurePanSlider(pan); pan.onValueChange = [this] { presenter.setPan(id, pan.getValue()); };
            pan.onDragStart = [this] { presenter.selectTrack(id); presenter.beginGesture(); }; pan.onDragEnd = [this] { presenter.endGesture(false); };
            parameter.addItem("Volume", 1); parameter.addItem("Pan", 2);
            parameter.onChange = [this] { presenter.setAutomationPan(id, parameter.getSelectedId() == 2); };
            gain.setRange(0, 2, 0.001); gain.setSliderStyle(juce::Slider::LinearVertical);
            gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); setSliderDefault(gain, 1); gain.setTitle("Track volume");
            gain.onDragStart = [this] { presenter.beginGesture(); };
            gain.onDragEnd = [this] { presenter.endGesture(false); };
            gain.onValueChange = [this] { if (auto* track = findId(presenter.project().tracks, id)) presenter.controls(id, gain.getValue(), track->mute); };
            mute.setButtonText(juce::String(number)); mute.setTitle("Track " + juce::String(number) + " on/off; checked means muted");
            mute.onClick = [this] { if (auto* track = findId(presenter.project().tracks, id)) presenter.controls(id, track->gain, mute.getToggleState()); };
            solo.setButtonText("S"); solo.setTitle("Solo track"); solo.setClickingTogglesState(true);
            solo.onClick = [this] { presenter.setSolo(id, solo.getToggleState()); };
            arm.setButtonText("Record"); arm.setTitle("Arm track for recording"); arm.setClickingTogglesState(true);
            arm.onClick = [this] { presenter.selectTrack(id); presenter.armTrack(id, arm.getToggleState()); arm.setToggleState(presenter.armed(id), juce::dontSendNotification); };
        }
        void resized() override
        { name.setBounds(5, 3, getWidth() - 59, 23); mute.setBounds(9, 30, 34, 20);
          solo.setBounds(47, 30, 22, 20); arm.setBounds(73, 30, 22, 20);
          gain.setBounds(getWidth() - 45, 6, 24, getHeight() - 32);
          meter.setBounds(getWidth() - 17, 6, 12, getHeight() - 32);
          gainValue.setBounds(getWidth() - 64, getHeight() - 23, 59, 20);
          pan.setBounds(103, 30, 73, 36); parameter.setBounds(9, 52, 88, 18); }
        void tick()
        {
            meter.setLevels(presenter.trackLevels(id));
            if (!gain.isMouseButtonDown()) gain.setValue(presenter.trackValue(id, false), juce::dontSendNotification);
            if (!pan.isMouseButtonDown()) pan.setValue(presenter.trackValue(id, true), juce::dontSendNotification);
            label(gainValue, volumeText(gain.getValue())); gainValue.setFont(juce::FontOptions(10)); gainValue.setColour(juce::Label::textColourId, trackColour(id).contrasting());
        }
        void paint(juce::Graphics& graphics) override
        {
            const bool selected = presenter.viewState().selectedTrack == id;
            graphics.fillAll(trackColour(id, selected));
            graphics.setColour(line); graphics.drawRect(getLocalBounds());
            graphics.setColour(trackColour(id).contrasting()); graphics.setFont(juce::FontOptions(10));
            const auto* track = findId(presenter.project().tracks, id);
            if (getHeight() >= 70 && track && !presenter.viewState().automationView) graphics.drawText(track->mute ? "Muted" : track->midi ? "MIDI" : "Audio", 9, 52, 88, 16, juce::Justification::centredLeft);
        }
        void mouseMove(const juce::MouseEvent& event) override
        { setMouseCursor(event.y >= getHeight() - 5 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }
        void mouseExit(const juce::MouseEvent&) override
        { if (!resizing) setMouseCursor(juce::MouseCursor::NormalCursor); }
        void mouseDown(const juce::MouseEvent& event) override
        {
            presenter.selectTrack(id); grabKeyboardFocus();
            if (event.originalComponent == &gainValue) return;
            if (event.mods.isPopupMenu())
            {
                juce::PopupMenu menu; menu.addItem(1, "Copy track"); menu.addItem(2, "Paste track"); menu.addItem(3, "Delete track");
                juce::WeakReference<EditorPresenter> owner(&presenter); const auto track = id;
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [owner, track](int choice)
                { if (!owner) return; owner->selectTrack(track); if (choice == 1) owner->copyTrack();
                  if (choice == 2) owner->pasteTrack(); if (choice == 3) owner->deleteTrack(track); }); return;
            }
            if (!event.mods.isLeftButtonDown() || event.getEventRelativeTo(this).y < getHeight() - 5 || presenter.gesturing()) return;
            initialHeight = getHeight(); initialY = event.getScreenY(); resizing = true;
            grabKeyboardFocus(); presenter.beginGesture();
        }
        void mouseDoubleClick(const juce::MouseEvent& event) override
        { if (event.originalComponent == &gainValue) gain.setValue(1, juce::sendNotificationSync); }
        void mouseDrag(const juce::MouseEvent& event) override
        { if (resizing) presenter.setTrackHeight(id, initialHeight + event.getScreenY() - initialY); }
        void mouseUp(const juce::MouseEvent&) override
        { if (resizing) { resizing = false; presenter.endGesture(false); setMouseCursor(juce::MouseCursor::NormalCursor); } }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (!resizing && (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey))
            { const auto track = id; presenter.deleteTrack(track); return true; }
            if (!resizing || key.getKeyCode() != juce::KeyPress::escapeKey) return false;
            presenter.setTrackHeight(id, initialHeight); resizing = false; presenter.endGesture(true);
            setMouseCursor(juce::MouseCursor::NormalCursor); return true;
        }
        void update(const Track& track)
        { setName(track.name + " track"); if (!name.isBeingEdited()) label(name, track.name);
          parameter.setVisible(presenter.viewState().automationView); parameter.setSelectedId(presenter.automationPan(id) ? 2 : 1, juce::dontSendNotification);
          const auto colour = trackColour(id, presenter.viewState().selectedTrack == id);
          name.setColour(juce::Label::textColourId, colour.contrasting());
          name.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
          gain.setValue(track.gain, juce::dontSendNotification); mute.setToggleState(track.mute, juce::dontSendNotification); solo.setToggleState(track.solo, juce::dontSendNotification); arm.setToggleState(presenter.armed(id), juce::dontSendNotification); repaint(); }
        bool isInterestedInDragSource(const SourceDetails& details) override
        { const auto* track = findId(presenter.project().tracks, id);
          return track && (presenter.isBrowserDrag(details.description.toString()) || details.description.toString().startsWith("plugin:")
              || (details.description.toString().startsWith("instrument:") && track->midi && !track->sampler)); }
        void itemDropped(const SourceDetails& details) override
        { const auto text = details.description.toString(); if (presenter.isBrowserDrag(text)) presenter.dropBrowserItem(text, id, presenter.viewState().timeSelection ? presenter.viewState().timeSelection->start : presenter.position());
          else if (text.startsWith("plugin:")) presenter.dropPlugin(text, id);
          else presenter.moveSampler(text.substring(11), id); }
        EditorPresenter& presenter; juce::String id; juce::Label name, gainValue; juce::Slider gain, pan; juce::ComboBox parameter; juce::ToggleButton mute; juce::TextButton solo, arm; LevelMeter meter;
        int initialHeight = 0, initialY = 0; bool resizing = false;
    };
    Headers(EditorPresenter& owner) : presenter(owner)
    { setWantsKeyboardFocus(true); addAndMakeVisible(automationButton); automationButton.setClickingTogglesState(true);
      automationButton.onClick = [this] { presenter.setAutomationView(automationButton.getToggleState()); }; }
    void refresh()
    {
        automationButton.setToggleState(presenter.viewState().automationView, juce::dontSendNotification);
        const bool keepFocus = hasKeyboardFocus(true);
        bool rebuild = rows.size() != presenter.project().tracks.size();
        if (!rebuild) for (size_t i = 0; i < rows.size(); ++i) if (rows[i]->id != presenter.project().tracks[i].id) rebuild = true;
        if (rebuild)
        {
            rows.clear(); if (keepFocus) grabKeyboardFocus();
            for (const auto& track : presenter.project().tracks)
            { auto row = std::make_unique<Row>(presenter, track.id, static_cast<int>(rows.size()) + 1); addAndMakeVisible(*row); rows.push_back(std::move(row)); }
        }
        for (size_t i = 0; i < rows.size(); ++i)
        { const auto& track = presenter.project().tracks[i]; rows[i]->update(track);
          rows[i]->setBounds(0, laneTop(presenter, static_cast<int>(i)), getWidth(), laneHeight(presenter, track.id)); }
    }
    void mouseDown(const juce::MouseEvent& event) override
    { if (event.mods.isPopupMenu()) showTrackCreationMenu(presenter, *this); }
    void tick() { for (auto& row : rows) row->tick(); }
    void resized() override { automationButton.setBounds(getWidth() - 94, 5, 86, 22); refresh(); }
    void paint(juce::Graphics& graphics) override
    { graphics.fillAll(panel); graphics.setColour(ink); graphics.setFont(juce::FontOptions(11));
      graphics.drawText("TRACK / MIX", 8, 0, getWidth() - 108, rulerHeight, juce::Justification::centredLeft);
      graphics.setColour(line); graphics.drawHorizontalLine(rulerHeight - 1, 0, static_cast<float>(getWidth())); }
private:
    EditorPresenter& presenter; juce::TextButton automationButton {"Automation"}; std::vector<std::unique_ptr<Row>> rows;
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
      if (y < rulerHeight || index < 0 || index >= static_cast<int>(owner.presenter.project().tracks.size())) return {};
      return owner.presenter.project().tracks[static_cast<size_t>(index)].id; }
    juce::Rectangle<float> boundsFor(const Clip& clip) const
    {
        const auto x = std::min(static_cast<double>(getWidth()) + 1, clip.start * owner.presenter.viewState().pixelsPerSecond);
        return { static_cast<float>(x), static_cast<float>(laneTop(owner.presenter, trackIndex(clip.track)) + 2),
                 static_cast<float>(std::min(clip.duration * owner.presenter.viewState().pixelsPerSecond, std::max(0.0, getWidth() - x))),
                 static_cast<float>(laneHeight(owner.presenter, clip.track) - 4) };
    }
    int headerEdge(const Clip& clip, juce::Point<float> point) const
    {
        const auto bounds = boundsFor(clip).withHeight(19);
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
                selection.tracks.add(owner.presenter.project().tracks[static_cast<size_t>(row)].id);
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
            for (auto it = envelopes.begin(); it != envelopes.end();) if (!findId(project.tracks, it->first)) it = envelopes.erase(it); else ++it;
            for (size_t row = 0; row < project.tracks.size(); ++row)
            {
                const auto& track = project.tracks[row]; auto& envelope = envelopes[track.id];
                if (!envelope) { envelope = std::make_unique<AutomationLane>(owner.presenter, track.id); addAndMakeVisible(*envelope); }
                envelope->setBounds(0, laneTop(owner.presenter, static_cast<int>(row)) + 22, getWidth(), laneHeight(owner.presenter, track.id) - 24);
                envelope->setView(0, owner.presenter.viewState().pixelsPerSecond);
            }
        }
        playhead.toFront(false); repaint();
    }
    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(background); const auto visible = graphics.getClipBounds();
        const auto& project = owner.presenter.project();
        const auto selectedRow = trackIndex(owner.presenter.viewState().selectedTrack);
        if (selectedRow >= 0 && selectedRow < static_cast<int>(project.tracks.size()))
        {
            graphics.setColour(background.interpolatedWith(accent, 0.25f));
            graphics.fillRect(visible.getX(), laneTop(owner.presenter, selectedRow), visible.getWidth(),
                              laneHeight(owner.presenter, owner.presenter.viewState().selectedTrack));
        }
        std::set<juce::String> visibleClips;
        for (const auto& clip : project.clips)
            if (boundsFor(clip).toNearestInt().withTrimmedTop(22).reduced(3).intersects(owner.lanes.getViewArea())) visibleClips.insert(clip.id);
        for (auto it = waveforms.begin(); it != waveforms.end();)
            if (visibleClips.count(it->first) == 0) it = waveforms.erase(it); else ++it;
        const auto beat = 60.0 / project.bpm;
        const auto step = timelineGridStep(project.bpm, owner.presenter.viewState().pixelsPerSecond);
        const auto spacing = step * owner.presenter.viewState().pixelsPerSecond;
        const auto first = std::isfinite(spacing) ? static_cast<int>(visible.getX() / spacing) : 0;
        const auto last = std::isfinite(spacing) ? static_cast<int>(visible.getRight() / spacing) + 1 : -1;
        graphics.setColour(panel); graphics.fillRect(visible.withHeight(rulerHeight));
        for (int i = first; i <= last; ++i)
        {
            const auto x = static_cast<int>(i * spacing);
            const bool bar = std::abs(std::remainder(i * step, beat * 4)) < step * 0.01;
            graphics.setColour(bar ? line : line.withAlpha(0.35f));
            graphics.drawVerticalLine(x, 0, static_cast<float>(getHeight()));
            if (bar)
            { graphics.setColour(ink); graphics.setFont(juce::FontOptions(11));
              graphics.drawText(juce::String(i * step / beat / 4 + 1, 0), x + 4, 2, 60, 22, juce::Justification::centredLeft); }
        }
        for (size_t index = 0; index <= project.tracks.size(); ++index)
        { graphics.setColour(line); graphics.drawHorizontalLine(laneTop(owner.presenter, static_cast<int>(index)), static_cast<float>(visible.getX()), static_cast<float>(visible.getRight())); }
        for (const auto& clip : project.clips)
        {
            const auto bounds = boundsFor(clip); if (!bounds.intersects(visible.toFloat())) continue;
            const auto* asset = findId(project.assets, clip.asset);
            const auto* track = findId(project.tracks, clip.track); if (!track) continue;
            const bool clipSelected = owner.presenter.viewState().selectedClips.contains(clip.id);
            const auto colour = asset && asset->missing ? juce::Colour(0xffd0a5a7) : trackColour(clip.track);
            graphics.setColour(colour.withMultipliedSaturation(0.85f).brighter(0.05f)); graphics.fillRect(bounds);
            graphics.setColour(colour); graphics.fillRect(bounds.withHeight(19));
            if (asset)
            {
                const auto found = thumbnails.find(owner.presenter.mediaDirectory().getChildFile(asset->path).getFullPathName());
                if (found != thumbnails.end()) drawWaveform(graphics, *found->second, found->first, clip, bounds.toNearestInt().withTrimmedTop(22).reduced(3));
            }
            else
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
            graphics.drawText(asset ? (asset->missing ? "! Missing: " : "") + assetName(*asset) : "MIDI", bounds.toNearestInt().reduced(5, 0).withHeight(19), juce::Justification::centredLeft, true);
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
            if (clipSelected)
            {
                graphics.setColour(ink); graphics.drawRect(bounds.reduced(1), 2);
            }
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
            const auto selectionColour = accent.interpolatedWith(background.getPerceivedBrightness() < 0.5f ? juce::Colours::white : juce::Colours::black, 0.65f);
            const auto zoomIn = owner.presenter.viewState().pixelsPerSecond;
            const auto from = static_cast<float>(std::min(static_cast<double>(getWidth()), selection->start * zoomIn));
            const auto to = static_cast<float>(std::min(static_cast<double>(getWidth()), selection->end * zoomIn));
            if (to > from) for (const auto& track : selection->tracks)
            {
                juce::Rectangle<float> bounds(from, static_cast<float>(laneTop(owner.presenter, trackIndex(track))),
                                              to - from, static_cast<float>(laneHeight(owner.presenter, track)));
                graphics.setColour(selectionColour.withAlpha(0.5f)); graphics.fillRect(bounds);
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
        if (event.mods.isPopupMenu())
        { bool clipAtPoint = false; for (const auto& clip : owner.presenter.project().clips) if (boundsFor(clip).contains(event.position)) clipAtPoint = true;
          if (!clipAtPoint && event.y >= rulerHeight) showTrackCreationMenu(owner.presenter, *this); return; }
        if (!event.mods.isLeftButtonDown()) return;
        grabKeyboardFocus();
        if (event.y < rulerHeight)
        {
            juce::StringArray tracks;
            if (owner.presenter.viewState().timeSelection) tracks = owner.presenter.viewState().timeSelection->tracks;
            else for (const auto& clip : owner.presenter.project().clips)
                if (owner.presenter.viewState().selectedClips.contains(clip.id)) tracks.addIfNotAlreadyThere(clip.track);
            if (tracks.isEmpty() && !owner.presenter.project().tracks.empty()) tracks.add(owner.presenter.project().tracks[0].id);
            owner.presenter.selectTime({ gridTimeAt(event.x), gridTimeAt(event.x), tracks }); owner.presenter.seek(gridTimeAt(event.x)); return;
        }
        down = event.position; selectionBefore = owner.presenter.viewState().selectedClips;
        timeBefore = owner.presenter.viewState().timeSelection;
        if (event.getNumberOfClicks() == 1) doubleClickSelection = timeBefore;
        dragRange.reset();
        originals.clear(); mode = -1;
        for (auto it = owner.presenter.project().clips.rbegin(); it != owner.presenter.project().clips.rend(); ++it)
        {
            const auto edge = headerEdge(*it, event.position);
            if (edge >= 0)
            {
                original = *it;
                const auto seconds = event.x / owner.presenter.viewState().pixelsPerSecond;
                if (edge == 0 && !event.mods.isShiftDown() && timeBefore && timeBefore->tracks.contains(it->track)
                    && seconds >= timeBefore->start && seconds < timeBefore->end)
                { dragRange = timeBefore; mode = 0; owner.presenter.beginGesture(); repaint(); return; }
                if (event.mods.isShiftDown()) owner.presenter.selectClip(it->id, true);
                else if (!owner.presenter.viewState().selectedClips.contains(it->id)) owner.presenter.selectClip(it->id);
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
        if (mode == 0 && event.mods.isAltDown() && original.asset.isNotEmpty())
        {
            const auto id = original.id; finishGesture(true);
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) container->startDragging("clip:" + id, this, juce::ScaledImage(), true);
            return;
        }
        if (dragRange && originals.empty())
        {
            originals = owner.presenter.splitTimeSelection();
            const auto found = std::find_if(originals.begin(), originals.end(), [&](const auto& clip) { return clip.id == original.id; });
            if (found == originals.end()) { finishGesture(true); return; }
            original = *found;
        }
        auto clip = original;
        const auto delta = (event.position.x - down.x) / owner.presenter.viewState().pixelsPerSecond;
        if (mode == 1) clip = trimLeft(clip, snappedTime(original.start + delta, owner.presenter.viewState().snap));
        else if (mode == 2)
        {
            const auto* asset = findId(owner.presenter.project().assets, clip.asset);
            const auto end = snappedTime(original.start + original.duration + delta, owner.presenter.viewState().snap);
            if (asset && asset->missing) return;
            clip = trimRight(clip, end, asset ? static_cast<double>(asset->length) / asset->sampleRate
                                            : clip.offset + std::max(clip.duration, end - clip.start));
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
            owner.presenter.changeClips(moved);
            if (dragRange)
            {
                auto selection = *dragRange; selection.start += shift; selection.end += shift;
                for (auto& track : selection.tracks) track = owner.presenter.project().tracks[static_cast<size_t>(trackIndex(track) + trackShift)].id;
                owner.presenter.selectTime(std::move(selection));
            }
            return;
        }
        owner.presenter.changeClip(clip);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto* target = findId(owner.presenter.project().tracks, trackAt(event.y));
        if (mode == 0 && original.asset.isNotEmpty() && target && target->midi && event.getDistanceFromDragStart() >= 3)
        { const auto track = target->id; const auto source = original; finishGesture(true);
          owner.presenter.loadSampler(track, source.asset, source.offset, source.duration); }
        else finishGesture(false);
        mouseMove(event);
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
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
    void finishGesture(bool cancel)
    {
        if (cancel && mode >= 0)
        { if (timeBefore) owner.presenter.selectTime(*timeBefore); else owner.presenter.selectClips(selectionBefore); }
        mode = -1; originals.clear(); dragRange.reset();
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
        || details.description.toString().startsWith("instrument:") || details.description.toString().startsWith("plugin:"); }
    void itemDropped(const SourceDetails& details) override
    {
        const auto trackId = trackAt(details.localPosition.y); const auto* target = findId(owner.presenter.project().tracks, trackId);
        if (owner.presenter.isBrowserDrag(details.description.toString())) { owner.presenter.dropBrowserItem(details.description.toString(), trackId, timeAt(details.localPosition.x)); return; }
        if (details.description.toString().startsWith("plugin:")) { owner.presenter.dropPlugin(details.description.toString(), trackId); return; }
        if (details.description.toString().startsWith("instrument:"))
        { owner.presenter.moveSampler(details.description.toString().substring(11), trackId); return; }
        if (details.description.toString().startsWith("clip:"))
        {
            if (target && target->midi)
                if (const auto* clip = findId(owner.presenter.project().clips, details.description.toString().substring(5)))
                    owner.presenter.loadSampler(trackId, clip->asset, clip->offset, clip->duration);
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
                      const Clip& clip, juce::Rectangle<int> bounds)
    {
        // Cache the viewport so playhead-only repaints do not decode audio again.
        const auto visible = bounds.getIntersection(owner.lanes.getViewArea());
        if (visible.isEmpty()) return;
        const juce::Rectangle<int> area(visible.getX(), bounds.getY(), visible.getWidth(), bounds.getHeight());
        const auto secondsPerPixel = clip.duration / bounds.getWidth();
        const auto from = clip.offset + (area.getX() - bounds.getX()) * secondsPerPixel;
        const auto to = clip.offset + (area.getRight() - bounds.getX()) * secondsPerPixel;
        const auto scale = graphics.getInternalContext().getPhysicalPixelScaleFactor();
        const juce::Point<int> size(area.getWidth(), area.getHeight()); auto& cached = waveforms[clip.id];
        if (!cached.image.isValid() || std::tie(cached.file, cached.from, cached.to, cached.scale, cached.size)
                                      != std::tie(file, from, to, scale, size))
        {
            cached.image = juce::Image(juce::Image::ARGB, static_cast<int>(std::ceil(static_cast<double>(size.x) * scale)),
                                      static_cast<int>(std::ceil(static_cast<double>(size.y) * scale)), true);
            juce::Graphics waveform(cached.image); waveform.addTransform(juce::AffineTransform::scale(scale));
            waveform.setColour(trackColour(clip.track).contrasting());
            const auto* asset = findId(owner.presenter.project().assets, clip.asset);
            if (!asset || !detail.draw(waveform, file, { 0, 0, static_cast<float>(size.x), static_cast<float>(size.y) }, from, to, asset->sampleRate, trackColour(clip.track).contrasting()))
                waveform.fillPath(waveformEnvelope(thumbnail, { 0, 0, static_cast<float>(size.x), static_cast<float>(size.y) }, from, to));
            cached.file = file; cached.from = from; cached.to = to; cached.scale = scale; cached.size = size;
#if defined(OPENTONE_UI_TESTING)
            getProperties().set("waveform-renders", static_cast<int>(getProperties()["waveform-renders"]) + 1);
            getProperties().set("waveform-max-width", std::max(size.x, static_cast<int>(getProperties()["waveform-max-width"])));
#endif
        }
        graphics.drawImage(cached.image, area.toFloat());
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
    juce::Point<float> down; int mode = -1;
    juce::StringArray selectionBefore;
    std::optional<TimeSelection> timeBefore, dragRange, doubleClickSelection;
    std::array<juce::MouseCursor, 2> trimCursors;
    juce::AudioFormatManager formats;
    WaveformDetail detail;
    juce::AudioThumbnailCache cache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    struct Waveform
    {
        juce::Image image; juce::String file; juce::Point<int> size;
        double from = 0, to = 0; float scale = 0;
    };
    std::map<juce::String, Waveform> waveforms;
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
    loopBrace = std::make_unique<LoopBrace>(presenter); addAndMakeVisible(*loopBrace);
    lanes.setViewedComponent(canvas.get(), false); headers.setViewedComponent(trackHeaders.get(), false);
    headers.setScrollBarsShown(false, false); lanes.setScrollBarsShown(true, true);
    lanes.getVerticalScrollBar().setAutoHide(true);
    lanes.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    lanes.onScroll = [this](auto bounds) { headers.setViewPosition(0, bounds.getY()); if (!restoring) presenter.setScroll(bounds.getX(), bounds.getY()); master->repaint(); loopBrace->repaint(); };
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
    graphics.drawText("ARRANGEMENT", 9, 0, 110, 30, juce::Justification::centredLeft);
    graphics.drawText("Zoom", 124, 0, 35, 30, juce::Justification::centredLeft);
}
void Timeline::resized()
{
    restoring = true;
    auto bounds = getLocalBounds().reduced(1); auto toolbar = bounds.removeFromTop(30).reduced(3);
    snap.setBounds(toolbar.removeFromRight(98)); toolbar.removeFromLeft(160); zoom.setBounds(toolbar.removeFromLeft(110));
    bounds.removeFromTop(18); headers.setBounds(bounds.removeFromRight(headerWidth)); lanes.setBounds(bounds); refresh();
}
void Timeline::refresh()
{
    restoring = true;
    double end = std::max({30.0, presenter.position() + 10, presenter.project().loopEnd + 10});
    for (const auto& clip : presenter.project().clips) end = std::max(end, clip.start + clip.duration + 10);
    for (const auto* track : allTracks(presenter.project())) for (const auto* points : { &track->volumeAutomation, &track->panAutomation })
        if (!points->empty()) end = std::max(end, points->back().time + 10);
    const auto height = std::max(lanes.getHeight() - lanes.getScrollBarThickness(),
                                laneTop(presenter, static_cast<int>(presenter.project().tracks.size())) + rowHeight);
    // shortcut: canvas is capped at ten million pixels, tile the viewport for longer arrangements.
    const auto width = std::max(lanes.getWidth() - lanes.getScrollBarThickness(), static_cast<int>(std::min(10000000.0, end * presenter.viewState().pixelsPerSecond)));
    canvas->setSize(width, height); trackHeaders->setSize(headerWidth, height); trackHeaders->refresh();
    headers.setBounds(lanes.getRight(), lanes.getY(), headerWidth, lanes.getViewHeight());
    loopBrace->setBounds(lanes.getX(), lanes.getY() - 18, lanes.getViewWidth(), 18); loopBrace->repaint();
    master->setBounds(lanes.getX(), lanes.getY() + lanes.getViewHeight() - rowHeight, lanes.getWidth() + headerWidth, rowHeight);
    master->toFront(false);
    lanes.setViewPosition(presenter.viewState().scrollX, presenter.viewState().scrollY);
    headers.setViewPosition(0, lanes.getViewPositionY());
    restoring = false;
    presenter.setScroll(lanes.getViewPositionX(), lanes.getViewPositionY());
    canvas->refresh(); tick();
}
void Timeline::tick()
{
    master->tick(); master->repaint(); trackHeaders->tick();
    if (presenter.recording()) canvas->repaint(lanes.getViewArea());
    if (presenter.playing() && !restoring && canvas->getWidth() < 10000000
        && presenter.position() * presenter.viewState().pixelsPerSecond >= canvas->getWidth()) { refresh(); return; }
    canvas->playhead.setBounds(static_cast<int>(std::min(static_cast<double>(canvas->getWidth()), presenter.position() * presenter.viewState().pixelsPerSecond)), 0, 2, canvas->getHeight()); }
bool Timeline::trackFocused() const { return trackHeaders->hasKeyboardFocus(true); }
void Timeline::deleteSelection() { canvas->finishGesture(false); presenter.deleteSelection(); }
void Timeline::cancelGesture() { canvas->cancelGesture(); }
bool Timeline::automationCommand(char command)
{ return isVisible() && (canvas->automationCommand(command) || master->automationCommand(command)); }

}
