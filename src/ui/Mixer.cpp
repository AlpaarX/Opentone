// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Mixer.h"
#include "ui/UiTheme.h"
#include "ui/LevelMeter.h"
namespace opentone
{
namespace { constexpr int stripWidth = 112; }
class Mixer::Strip final : public juce::Component, public juce::DragAndDropTarget
{
public:
    Strip(Mixer& mixer, juce::String trackId) : id(std::move(trackId)), owner(mixer)
    {
        setWantsKeyboardFocus(true);
        for (auto* child : std::initializer_list<juce::Component*> { &name, &kind, &gain, &pan, &mute, &solo, &arm, &meter }) addAndMakeVisible(child);
        name.setJustificationType(juce::Justification::centred); name.setEditable(false, !isMaster()); name.addMouseListener(this, false);
        name.onTextChange = [this] { if (!isMaster()) owner.presenter.renameTrack(id, name.getText()); };
        kind.setJustificationType(juce::Justification::centred);
        gain.setRange(0, 2, 0.001); gain.setSliderStyle(juce::Slider::LinearVertical); gain.setSkewFactor(0.5);
        gain.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 18); setSliderDefault(gain, 1);
        gain.setTitle(isMaster() ? "Master volume" : "Track volume; double click for unity gain");
        gain.textFromValueFunction = [](double value) { return value <= 0 ? juce::String("-inf dB") : juce::String(20 * std::log10(value), 1) + " dB"; };
        gain.valueFromTextFunction = [](const juce::String& text) { return text.containsIgnoreCase("inf") ? 0.0 : std::pow(10.0, text.getDoubleValue() / 20); };
        gain.onDragStart = [this] { owner.presenter.selectTrack(id); owner.presenter.beginGesture(); };
        gain.onDragEnd = [this] { owner.presenter.endGesture(false); };
        gain.onValueChange = [this]
        { if (isMaster()) owner.presenter.setMaster(static_cast<float>(gain.getValue()));
          else if (const auto* track = findTrack(owner.presenter.project(), id)) owner.presenter.controls(id, gain.getValue(), track->mute); };
        configurePanSlider(pan); pan.onValueChange = [this] { owner.presenter.setPan(id, pan.getValue()); };
        pan.onDragStart = [this] { owner.presenter.selectTrack(id); owner.presenter.beginGesture(); }; pan.onDragEnd = [this] { owner.presenter.endGesture(false); };
        mute.getProperties().set("track-active", true);
        mute.setClickingTogglesState(true); solo.setClickingTogglesState(true); mute.setTitle("Mute track"); solo.setTitle("Solo track");
        mute.onClick = [this]
        { owner.presenter.selectTrack(id); if (const auto* track = findTrack(owner.presenter.project(), id)) owner.presenter.controls(id, track->gain, mute.getToggleState()); };
        solo.onClick = [this] { owner.presenter.selectTrack(id); owner.presenter.setSolo(id, solo.getToggleState()); };
        arm.setTitle("Arm track for recording"); arm.setClickingTogglesState(true);
        arm.onClick = [this] { owner.presenter.selectTrack(id); owner.presenter.armTrack(id, arm.getToggleState()); arm.setToggleState(owner.presenter.armed(id), juce::dontSendNotification); };
        refresh();
    }
    bool isMaster() const { return id == "master"; }
    void refresh()
    {
        const auto* track = findTrack(owner.presenter.project(), id);
        const auto colour = trackColour(id, owner.presenter.viewState().selectedTrack == id);
        name.setColour(juce::Label::backgroundColourId, colour);
        if (!name.isBeingEdited()) label(name, isMaster() ? "Master" : track ? track->name : "");
        name.setColour(juce::Label::textColourId, colour.contrasting());
        label(kind, isMaster() ? "Output" : track && track->midi ? "MIDI" : "Audio");
        { kind.setColour(juce::Label::backgroundColourId, colour); kind.setColour(juce::Label::textColourId, colour.contrasting()); }
        gain.setValue(owner.presenter.trackValue(id, false), juce::dontSendNotification); pan.setValue(owner.presenter.trackValue(id, true), juce::dontSendNotification);
        mute.setVisible(!isMaster()); solo.setVisible(!isMaster()); arm.setVisible(!isMaster()); arm.setToggleState(owner.presenter.armed(id), juce::dontSendNotification);
        if (track) { mute.setToggleState(track->mute, juce::dontSendNotification); solo.setToggleState(track->solo, juce::dontSendNotification); }
        repaint();
    }
    void tick() { meter.setLevels(isMaster() ? owner.presenter.masterLevels() : owner.presenter.trackLevels(id));
      if (!gain.isMouseButtonDown()) gain.setValue(owner.presenter.trackValue(id, false), juce::dontSendNotification);
      if (!pan.isMouseButtonDown()) pan.setValue(owner.presenter.trackValue(id, true), juce::dontSendNotification); }
    void paint(juce::Graphics& graphics) override
    {
        const bool selected = owner.presenter.viewState().selectedTrack == id;
        graphics.fillAll(trackColour(id, selected)); graphics.setColour(line); graphics.drawRect(getLocalBounds());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(6); name.setBounds(area.removeFromTop(25)); area.removeFromTop(4); kind.setBounds(area.removeFromTop(18));
        auto buttons = area.removeFromTop(24); mute.setBounds(buttons.removeFromLeft(28)); buttons.removeFromLeft(4); solo.setBounds(buttons.removeFromLeft(28)); buttons.removeFromLeft(4); arm.setBounds(buttons.removeFromLeft(28));
        pan.setBounds(area.removeFromTop(38));
        area = area.reduced(0, 6);
        auto meters = area.removeFromRight(18); meters.removeFromBottom(22); meter.setBounds(meters); gain.setBounds(area);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        owner.presenter.selectTrack(id); if (!name.isBeingEdited()) grabKeyboardFocus();
        if (event.mods.isPopupMenu() && !isMaster())
        { juce::PopupMenu menu; menu.addItem(1, "Copy track"); menu.addItem(2, "Paste track"); menu.addItem(3, "Delete track");
          juce::WeakReference<EditorPresenter> presenterRef(&owner.presenter); const auto track = id;
          menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [presenterRef, track](int choice)
          { if (!presenterRef) return; presenterRef->selectTrack(track); if (choice == 1) presenterRef->copyTrack();
            if (choice == 2) presenterRef->pasteTrack(); if (choice == 3) presenterRef->deleteTrack(track); }); }
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent())) return false;
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        { if (!isMaster()) { const auto track = id; owner.presenter.deleteTrack(track); } return true; }
        return false;
    }
    bool isInterestedInDragSource(const SourceDetails& details) override
    { const auto text = details.description.toString(); return isMaster() ? text.startsWith("device:") || text.startsWith("plugin:") : owner.presenter.isBrowserDrag(text) || text.startsWith("plugin:") || text.startsWith("instrument:"); }
    void itemDropped(const SourceDetails& details) override
    { const auto text = details.description.toString(); if (owner.presenter.isBrowserDrag(text)) owner.presenter.dropBrowserItem(text, id, owner.presenter.viewState().timeSelection ? owner.presenter.viewState().timeSelection->start : owner.presenter.position());
      else if (text.startsWith("plugin:")) owner.presenter.dropPlugin(text, id); else owner.presenter.moveSampler(text.substring(11), id); }
    juce::String id;
private:
    LevelMeter meter;
    Mixer& owner;
    juce::Label name, kind; juce::Slider gain, pan;
    juce::TextButton mute {"M"}, solo {"S"}, arm {"Record"};
};
class Mixer::Channels final : public juce::Component
{
public:
    explicit Channels(Mixer& mixer) : owner(mixer) {}
    void refresh()
    {
        bool changed = strips.size() != owner.presenter.project().tracks.size();
        if (!changed) for (size_t i = 0; i < strips.size(); ++i) if (strips[i]->id != owner.presenter.project().tracks[i].id) changed = true;
        const bool keepFocus = hasKeyboardFocus(true);
        if (changed)
        { strips.clear(); if (keepFocus) owner.grabKeyboardFocus();
          for (const auto& track : owner.presenter.project().tracks)
          { auto strip = std::make_unique<Strip>(owner, track.id); addAndMakeVisible(*strip); strips.push_back(std::move(strip)); } }
        for (auto& strip : strips) strip->refresh(); resized();
    }
    void resized() override
    { for (size_t i = 0; i < strips.size(); ++i) strips[i]->setBounds(static_cast<int>(i) * stripWidth, 0, stripWidth - 4, getHeight()); }
    void mouseDown(const juce::MouseEvent& event) override
    { owner.grabKeyboardFocus(); if (event.mods.isPopupMenu()) showTrackCreationMenu(owner.presenter, *this); }
    void paint(juce::Graphics& graphics) override { graphics.fillAll(background); }
    void tick() { for (auto& strip : strips) strip->tick(); }
private: Mixer& owner; std::vector<std::unique_ptr<Strip>> strips;
};
Mixer::Mixer(EditorPresenter& owner) : presenter(owner), channels(std::make_unique<Channels>(*this)), master(std::make_unique<Strip>(*this, "master"))
{
    setWantsKeyboardFocus(true); addAndMakeVisible(tracks); addAndMakeVisible(masterView);
    tracks.setViewedComponent(channels.get(), false); masterView.setViewedComponent(master.get(), false);
    tracks.setScrollBarsShown(true, true); masterView.setScrollBarsShown(false, false);
    tracks.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never); masterView.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    tracks.onScroll = [this](auto area) { if (!synchronising) { synchronising = true; masterView.setViewPosition(0, area.getY()); synchronising = false; } };
}
Mixer::~Mixer() { tracks.setViewedComponent(nullptr, false); masterView.setViewedComponent(nullptr, false); }
void Mixer::paint(juce::Graphics& graphics)
{ graphics.fillAll(panel); graphics.setColour(line); graphics.drawRect(getLocalBounds()); graphics.setColour(ink); graphics.setFont(juce::FontOptions(11));
  graphics.drawText("MIXER", 9, 0, getWidth() - 18, 30, juce::Justification::centredLeft); }
void Mixer::resized()
{
    auto area = getLocalBounds().reduced(1); area.removeFromTop(30); masterView.setBounds(area.removeFromRight(stripWidth)); area.removeFromRight(4); tracks.setBounds(area);
    const int height = std::max(240, tracks.getHeight() - tracks.getScrollBarThickness());
    channels->setSize(std::max(tracks.getViewWidth(), static_cast<int>(presenter.project().tracks.size()) * stripWidth), height);
    master->setSize(stripWidth - 2, height);
}
void Mixer::refresh() { channels->refresh(); master->refresh(); resized(); tick(); }
void Mixer::tick() { if (isVisible()) { channels->tick(); master->tick(); } }
bool Mixer::trackFocused() const { return isVisible() && hasKeyboardFocus(true) && !masterView.hasKeyboardFocus(true); }
}
