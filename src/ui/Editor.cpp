// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Editor.h"
#include "ui/UiTheme.h"
#include "ui/NativeWindow.h"
namespace opentone
{
namespace
{
class SaveChangesDialog final : public juce::AlertWindow
{
    class Style final : public juce::LookAndFeel_V4
    {
    public:
        int getAlertBoxWindowFlags() override { return juce::ComponentPeer::windowHasDropShadow; }
        void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool hover, bool down) override
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced(1);
            g.setColour(panel.brighter(hover ? 0.1f : 0).darker(down ? 0.1f : 0));
            g.fillRoundedRectangle(bounds, bounds.getHeight() / 2);
            g.setColour(button.getName() == "Save" || button.hasKeyboardFocus(true) ? ink : line);
            g.drawRoundedRectangle(bounds, bounds.getHeight() / 2, 1.2f);
        }
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            g.setColour(ink); g.setFont(juce::FontOptions(14));
            g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
        }
    } style;
    juce::String message;
public:
    SaveChangesDialog(const juce::String& name, juce::Component& owner)
        : AlertWindow("Save changes", "Save changes to \"" + name + "\" before closing?", juce::MessageBoxIconType::WarningIcon, &owner),
          message("Save changes to \"" + name + "\" before closing?")
    {
        setLookAndFeel(&style); setOpaque(false);
        addButton(juce::String::fromUTF8("Don’t Save"), 2);
        addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        setSize(600, 180); resized();
        centreAroundComponent(&owner, getWidth(), getHeight());
    }
    ~SaveChangesDialog() override { setLookAndFeel(nullptr); }
    void resized() override
    {
        if (auto* button = getButton(juce::String::fromUTF8("Don’t Save"))) button->setBounds(36, 138, 132, 20);
        if (auto* button = getButton("Cancel")) button->setBounds(getWidth() - 292, 138, 132, 20);
        if (auto* button = getButton("Save")) button->setBounds(getWidth() - 156, 138, 132, 20);
    }
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1);
        juce::Path outline; outline.addRoundedRectangle(bounds, 18);
        g.saveState(); g.reduceClipRegion(outline);
        g.fillAll(background); g.setColour(panel); g.fillRect(0, 0, getWidth(), 36);
        g.restoreState(); g.setColour(ink.withAlpha(0.5f)); g.strokePath(outline, juce::PathStrokeType(1));
        juce::Path warning; warning.startNewSubPath(79, 59); warning.lineTo(44, 119);
        warning.lineTo(114, 119); warning.closeSubPath();
        warning = warning.createPathWithRoundedCorners(5);
        g.setColour(ink); g.fillPath(warning);
        g.setColour(background); g.setFont(juce::FontOptions(52, juce::Font::bold));
        g.drawText("!", 44, 66, 70, 54, juce::Justification::centred);
        g.setColour(ink); g.setFont(juce::FontOptions(14, juce::Font::bold));
        g.drawFittedText(message, 150, 64, getWidth() - 174, 50, juce::Justification::centredLeft, 2);
    }
};
}
class Editor::SearchMenu final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit SearchMenu(Editor& editor) : owner(editor), list({}, this)
    {
        addAndMakeVisible(search); addAndMakeVisible(list); addAndMakeVisible(empty);
        search.setTextToShowWhenEmpty("Search...", ink.withAlpha(0.6f)); search.setTitle("Dropdown search");
        search.setColour(juce::TextEditor::textColourId, ink); search.setColour(juce::TextEditor::backgroundColourId, background);
        search.onTextChange = [this] { filter(); };
        search.onEscapeKey = [this] { close(); };
        search.onReturnKey = [this] { choose(list.getSelectedRow()); };
        label(empty, "No matches"); list.setRowHeight(30); setVisible(false);
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }
    ~SearchMenu() override { juce::Desktop::getInstance().removeGlobalMouseListener(this); }
    void show(juce::Component& button, std::vector<std::pair<juce::String, juce::String>> choices,
              std::function<void(juce::String)> selected, const juce::String& title)
    {
        if (isVisible() && anchor == &button) { close(); return; }
        anchor = &button; items = std::move(choices); onChoose = std::move(selected); setTitle(title);
        search.setText({}, false); filter();
        const auto position = owner.getLocalArea(&button, button.getLocalBounds());
        const int width = 280, height = 210;
        const auto y = position.getBottom() + height <= owner.getHeight() ? position.getBottom() : position.getY() - height;
        setBounds(juce::jlimit(0, std::max(0, owner.getWidth() - width), position.getX()),
                  juce::jlimit(0, std::max(0, owner.getHeight() - height), y), width, height);
        setVisible(true); toFront(false); search.grabKeyboardFocus();
    }
    void close() { setVisible(false); owner.grabKeyboardFocus(); }
    void paint(juce::Graphics& g) override { g.fillAll(panel); g.setColour(ink); g.drawRect(getLocalBounds()); }
    void resized() override
    { auto area = getLocalBounds().reduced(7); search.setBounds(area.removeFromTop(27)); area.removeFromTop(5);
      list.setBounds(area); empty.setBounds(area.withHeight(30)); }
private:
    void filter()
    {
        rows.clear(); const auto text = search.getText().trim();
        for (size_t i = 0; i < items.size(); ++i) if (items[i].second.containsIgnoreCase(text)) rows.push_back(i);
        list.updateContent(); list.selectRow(rows.empty() ? -1 : 0); empty.setVisible(rows.empty());
    }
    int getNumRows() override { return static_cast<int>(rows.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    { if (row < 0 || static_cast<size_t>(row) >= rows.size()) return;
      g.fillAll(selected ? accent.withAlpha(0.3f) : background); g.setColour(ink); g.setFont(juce::FontOptions(13));
      g.drawText(items[rows[static_cast<size_t>(row)]].second, 8, 0, width - 16, height, juce::Justification::centredLeft); }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override { choose(row); }
    void returnKeyPressed(int row) override { choose(row); }
    void choose(int row)
    { if (row < 0 || static_cast<size_t>(row) >= rows.size()) return;
      const auto id = items[rows[static_cast<size_t>(row)]].first; close(); if (onChoose) onChoose(id); }
    void mouseDown(const juce::MouseEvent& event) override
    { if (isVisible() && !getScreenBounds().contains(event.getScreenPosition())
          && (!anchor || !anchor->getScreenBounds().contains(event.getScreenPosition()))) setVisible(false); }
    Editor& owner; juce::TextEditor search; juce::ListBox list; juce::Label empty;
    juce::Component::SafePointer<juce::Component> anchor;
    std::vector<std::pair<juce::String, juce::String>> items; std::vector<size_t> rows;
    std::function<void(juce::String)> onChoose;
};
class Editor::MappingOverlay final : public juce::Component
{
public:
    explicit MappingOverlay(Editor& editor) : owner(editor) { setWantsKeyboardFocus(true); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    juce::Rectangle<int> boundsFor(juce::Component& control) const
    {
        auto bounds = owner.getLocalArea(&control, control.getLocalBounds());
        for (auto* parent = control.getParentComponent(); parent && parent != &owner; parent = parent->getParentComponent())
            bounds = bounds.getIntersection(owner.getLocalArea(parent, parent->getLocalBounds()));
        return bounds;
    }
    void visit(juce::Component& component, const std::function<void(juce::Component&)>& fn) const
    {
        if (&component == this || !component.isVisible()) return;
        if (component.getProperties()["midi-target"].isString()) { fn(component); return; }
        for (int i = 0; i < component.getNumChildComponents(); ++i) visit(*component.getChildComponent(i), fn);
    }
    juce::Component* targetAt(juce::Point<int> point) const
    { juce::Component* hit = nullptr; visit(owner, [&](juce::Component& control) { if (boundsFor(control).contains(point)) hit = &control; }); return hit; }
    bool hitTest(int x, int y) override { return targetAt({x, y}) != nullptr; }
    void paint(juce::Graphics& g) override
    {
        visit(owner, [&](juce::Component& control)
        {
            const auto bounds = boundsFor(control); if (bounds.isEmpty()) return;
            const auto target = control.getProperties()["midi-target"].toString();
            g.setColour(uiTone(UiTone::solo).withAlpha(0.65f)); g.fillRect(bounds);
            g.setColour(owner.presenter.viewState().midiLearnTarget == target ? juce::Colours::white : line); g.drawRect(bounds);
            for (const auto& mapping : owner.presenter.project().midiMappings) if (mapping.target == target)
            { g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(10));
              g.drawText(mapping.source == "Computer" ? juce::String::charToString(static_cast<juce::juce_wchar>(mapping.number)) : juce::String(mapping.channel) + "/" + (mapping.note ? "N" : "CC") + juce::String(mapping.number), bounds, juce::Justification::centred, true); }
        });
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        auto* control = targetAt(event.getPosition()); if (!control) return;
        const auto target = control->getProperties()["midi-target"].toString();
        if (event.mods.isPopupMenu()) { owner.presenter.removeMidiMapping(target); return; }
        if (target.startsWith("plugin-device/"))
        {
            const auto parts = juce::StringArray::fromTokens(target, "/", ""); if (parts.size() != 2) return;
            const auto slot = owner.presenter.plugins().editorSlot(parts[1]); if (!slot || !slot->instance) return;
            juce::PopupMenu menu; const auto parameters = slot->instance->getParameters();
            for (int i = 0; i < parameters.size(); ++i) menu.addItem(i + 1, parameters[i]->getName(64));
            juce::Component::SafePointer<MappingOverlay> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(control), [safe, parts](int choice) { if (safe && choice > 0) safe->owner.presenter.learnMidi("plugin/" + parts[1] + "/#" + juce::String(choice - 1)); });
        }
        else { owner.presenter.learnMidi(target); grabKeyboardFocus(); }
        repaint();
    }
private: Editor& owner;
};
class Editor::DetailGrip final : public juce::Component
{
public:
    explicit DetailGrip(Editor& editor) : owner(editor)
    { setTitle("Device view or piano roll edge; drag up to show, down to hide"); setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    void paint(juce::Graphics& g) override
    { g.fillAll(panel); g.setColour(line); g.drawHorizontalLine(0, 0, static_cast<float>(getWidth()));
      g.setColour(ink); g.fillRoundedRectangle(static_cast<float>(getWidth() / 2 - 20), 2, 40, 2, 1); }
    void mouseDown(const juce::MouseEvent& e) override
    { if (e.mods.isLeftButtonDown()) { dragging = true; startY = e.getScreenY(); startHeight = owner.midiPanel.isVisible() ? owner.midiPanel.getHeight() : 0; } }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging) return;
        if (!owner.midiPanel.pianoVisible())
        {
            const auto delta = e.getScreenY() - startY;
            if (std::abs(delta) < 2) return;
            owner.detailOpen = delta < 0;
            if (!owner.detailOpen) owner.midiPanel.cancelGesture();
            owner.refreshPanels(); return;
        }
        const auto height = startHeight + startY - e.getScreenY();
        owner.detailOpen = height > 60;
        if (!owner.detailOpen) owner.midiPanel.cancelGesture();
        else owner.presenter.setPianoPanel(true, height);
        owner.refreshPanels();
    }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
private: Editor& owner; int startY = 0, startHeight = 240; bool dragging = false;
};
class Editor::BrowserGrip final : public juce::Component
{
public:
    explicit BrowserGrip(Editor& editor) : owner(editor)
    { setTitle("Drag to resize or close the browser; double click to reset width"); setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }
    void paint(juce::Graphics& g) override
    { g.setColour(ink.withAlpha(isMouseOverOrDragging() ? 0.5f : 0.15f));
      g.drawVerticalLine(getWidth() / 2, 3, static_cast<float>(getHeight() - 3)); }
    void mouseDown(const juce::MouseEvent& event) override
    { if (event.mods.isLeftButtonDown()) { startX = event.getScreenX(); startWidth = owner.browserOpen ? owner.browser.getWidth() : 0; } }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        const auto maximum = std::max(120, owner.getWidth() - headerWidth - 198);
        const auto width = juce::jlimit(0, maximum, startWidth + event.getScreenX() - startX);
        owner.browserOpen = width >= 120;
        if (owner.browserOpen) owner.browserWidth = width;
        owner.resized();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { owner.browserOpen = true; owner.browserWidth = 360; owner.resized(); }
private: Editor& owner; int startX = 0, startWidth = 360;
};
class Editor::DeviceOverview final : public juce::Component
{
public:
    explicit DeviceOverview(Editor& editor) : owner(editor)
    { setTitle("Device chain overview; click or drag to navigate"); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1);
        g.setColour(uiTone(UiTone::control)); g.fillRoundedRectangle(bounds, 4);
        const auto* track = findTrack(owner.presenter.project(), owner.presenter.viewState().selectedTrack);
        if (!track) return;
        owner.midiPanel.paintDeviceOverview(g, bounds);
        if (!owner.midiPanel.pianoVisible() && owner.detailOpen)
        {
            const auto range = owner.midiPanel.deviceViewRange();
            g.setColour(ink.withAlpha(0.7f));
            g.drawRoundedRectangle(bounds.withX(bounds.getX() + static_cast<float>(range.first) * bounds.getWidth()).withWidth(static_cast<float>(range.second) * bounds.getWidth()), 3, 1);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown() || getWidth() <= 0) return;
        owner.selectedTrackButton.onClick(); owner.midiPanel.scrollDevices(static_cast<double>(event.x) / getWidth()); repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override { mouseDown(event); }
private: Editor& owner;
};
Editor::Editor(EditorPresenter& owner)
    : presenter(owner), theme(createUiTheme()), browser(owner), transport(owner), timeline(owner), mixer(owner), midiPanel(owner),
      searchMenu(std::make_unique<SearchMenu>(*this)), detailGrip(std::make_unique<DetailGrip>(*this)), browserGrip(std::make_unique<BrowserGrip>(*this)), deviceOverview(std::make_unique<DeviceOverview>(*this))
{
    mappingOverlay = std::make_unique<MappingOverlay>(*this); addChildComponent(*mappingOverlay);
    browserButton.getProperties().set("footer-icon", "browser");
    transport.onSettings = [this] { showDevices(false, true); }; transport.onMixer = [this] { toggleMixer(); };
    loadUiColours(); configureUiTheme(*theme); setLookAndFeel(theme.get());
    setSize(1200, 760); setWantsKeyboardFocus(true); setOpaque(true);
    setColour(juce::ResizableWindow::backgroundColourId, background);
    for (auto* item : std::initializer_list<juce::Component*> { &menu, &browser, &transport, &timeline, &mixer, &mixerButton, &browserButton, &infoButton, &detailButton, &viewButton, &selectedTrackButton, deviceOverview.get(), &status, &performance }) addAndMakeVisible(item);
#if JUCE_MAC
    menu.setVisible(false);
#endif
    addChildComponent(midiPanel); addChildComponent(*searchMenu); addChildComponent(*detailGrip); addChildComponent(*browserGrip);
    performance.setJustificationType(juce::Justification::centredRight);
    performance.setTitle("Audio CPU load and peak UI lag in milliseconds"); label(performance, "CPU 0.0% | UI lag 0 ms");
    commands.setFirstCommandTarget(this);
    commands.registerAllCommandsForTarget(this); addKeyListener(commands.getKeyMappings());
    setApplicationCommandManagerToWatch(&commands);
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#endif
    presenter.attach(*this); presenter.addChangeListener(this);
    presenter.plugins().keyboardListener = this; pluginKeyMonitor = installPluginKeyMonitor(*this);
    browserButton.setTitle("Show or hide the library"); browserButton.onClick = [this] { browserOpen = !browserOpen; resized(); };
    infoButton.getProperties().set("footer-icon", "info"); mixerButton.getProperties().set("footer-icon", "mixer");
    detailButton.getProperties().set("footer-icon", "detail"); viewButton.getProperties().set("footer-icon", "device");
    selectedTrackButton.getProperties().set("footer-track", true);
    infoButton.setTitle("Show control information in the info bar"); infoButton.onClick = [this] { infoVisible = !infoVisible; refreshFooter(); };
    detailButton.setTitle("Show or hide the bottom view"); detailButton.onClick = [this]
    { detailOpen = !detailOpen; if (!detailOpen) midiPanel.cancelGesture(); refreshPanels(); };
    viewButton.setTitle("Switch Clip and Device View (~)"); viewButton.onClick = [this] { togglePiano(); };
    selectedTrackButton.setTitle("Show this track's devices"); selectedTrackButton.onClick = [this]
    { detailOpen = true; midiPanel.cancelGesture(); presenter.setPianoPanel(false, presenter.viewState().pianoHeight); refreshPanels(); };
    mixerButton.setTitle("Toggle mixer and arrangement (Tab)"); mixerButton.onClick = [this] { toggleMixer(); };
    timeline.onFiles = [this](const auto& paths, auto track, double start) { presenter.importFiles(paths, track, start); };
    timeline.onMidiClip = [this](auto clip) { detailOpen = true; presenter.openMidiClip(clip); refreshPanels(); };
    midiPanel.onAddDevice = [this](auto& anchor, bool instrument) { showDevicesMenu(presenter.viewState().selectedTrack, anchor, instrument); };
    midiPanel.onChooseClip = [this](auto& anchor)
    {
        std::vector<std::pair<juce::String, juce::String>> choices;
        for (const auto& clip : presenter.project().clips) if (clip.scene < 0) if (const auto* asset = findId(presenter.project().assets, clip.asset))
            choices.emplace_back(clip.id, assetName(*asset) + " @ " + juce::String(clip.start, 2) + " s");
        const auto track = presenter.viewState().selectedTrack;
        searchMenu->show(anchor, std::move(choices), [this, track](auto id)
        { if (const auto* clip = findId(presenter.project().clips, id)) presenter.loadSampler(track, clip->asset, clip->offset, clip->duration); }, "Audio clip search dropdown");
    };
    browser.refresh(); mixer.refresh(); transport.refresh(); refreshPanels();
    applyColours(); startTimerHz(30);
}
Editor::~Editor()
{
    setApplicationCommandManagerToWatch(nullptr);
#if JUCE_MAC
    if (juce::MenuBarModel::getMacMainMenu() == this) juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
    removePluginKeyMonitor(pluginKeyMonitor);
    if (presenter.plugins().keyboardListener == this) presenter.plugins().keyboardListener = nullptr;
    delete settingsWindow.getComponent();
    if (themedWindow && &themedWindow->getLookAndFeel() == theme.get()) themedWindow->setLookAndFeel(nullptr);
    stopTimer(); releaseKeyboard(true); presenter.removeChangeListener(this); removeKeyListener(commands.getKeyMappings()); menu.setModel(nullptr); presenter.detach(*this); setLookAndFeel(nullptr); }
void Editor::parentHierarchyChanged()
{
    auto* window = findParentComponentOfClass<juce::DocumentWindow>();
    if (themedWindow && themedWindow != window && &themedWindow->getLookAndFeel() == theme.get()) themedWindow->setLookAndFeel(nullptr);
    themedWindow = window;
    if (window) { window->setLookAndFeel(theme.get()); window->setBackgroundColour(panel); window->setName(presenter.windowTitle()); }
    matchTitleBar(*this, panel);
}
void Editor::paint(juce::Graphics& graphics) { graphics.fillAll(panel); }
void Editor::resized()
{
    matchTitleBar(*this, panel);
    auto bounds = getLocalBounds();
#if !JUCE_MAC
    menu.setBounds(bounds.removeFromTop(20));
#endif
    auto toolbar = bounds.removeFromTop(30).reduced(6, 4);
    performance.setBounds(toolbar.removeFromRight(getWidth() >= 900 ? 95 : 70));
    browserButton.setBounds(toolbar.removeFromLeft(20)); toolbar.removeFromLeft(4);
    browserButton.setToggleState(browserOpen, juce::dontSendNotification);
    transport.setBounds(toolbar);
    auto footer = bounds.removeFromBottom(22).reduced(6, 2);
    infoButton.setBounds(footer.removeFromLeft(18)); footer.removeFromLeft(4);
    mixerButton.setBounds(footer.removeFromLeft(18)); footer.removeFromLeft(8);
    detailButton.setBounds(footer.removeFromRight(18)); footer.removeFromRight(6);
    deviceOverview->setBounds(footer.removeFromRight(96)); footer.removeFromRight(6);
    selectedTrackButton.setBounds(footer.removeFromRight(140)); footer.removeFromRight(5);
    viewButton.setBounds(footer.removeFromRight(18)); footer.removeFromRight(8);
    status.setBounds(footer); status.setColour(juce::Label::backgroundColourId, background);
    bounds = bounds.reduced(6, 4);
    const bool hasTrack = findTrack(presenter.project(), presenter.viewState().selectedTrack) != nullptr;
    detailGrip->setVisible(hasTrack);
    if (midiPanel.isVisible())
    {
        const auto height = std::min(midiPanel.pianoVisible() ? presenter.viewState().pianoHeight : MidiEditor::instrumentHeight, std::max(0, bounds.getHeight() - 6));
        auto bottom = bounds.removeFromBottom(height + 6);
        detailGrip->setBounds(bottom.removeFromTop(6)); midiPanel.setBounds(bottom);
    }
    else if (hasTrack) detailGrip->setBounds(bounds.removeFromBottom(6));
    const bool available = bounds.getHeight() > 30, mixing = presenter.viewState().mixerOpen;
    browser.setVisible(available && browserOpen); timeline.setVisible(available && !mixing); mixer.setVisible(available && mixing);
    mixerButton.setToggleState(mixing, juce::dontSendNotification);
    browserGrip->setVisible(available);
    if (browserOpen)
    {
        const auto width = juce::jlimit(120, std::max(120, bounds.getWidth() - headerWidth - 186), browserWidth);
        browser.setBounds(bounds.removeFromLeft(width));
    }
    browserGrip->setBounds(bounds.removeFromLeft(6));
    timeline.setBounds(bounds); mixer.setBounds(bounds);
    mappingOverlay->setBounds(getLocalBounds()); mappingOverlay->setVisible(presenter.viewState().midiMapMode || presenter.viewState().keyMapMode); mappingOverlay->toFront(false);
}
bool Editor::textFocused() const
{ auto* focused = juce::Component::getCurrentlyFocusedComponent();
  return dynamic_cast<juce::TextEditor*>(focused) != nullptr || (focused && focused->findParentComponentOfClass<juce::TextEditor>() != nullptr); }
bool Editor::keyPressed(const juce::KeyPress& key)
{
    if (textFocused() || juce::Component::getCurrentlyModalComponent() != nullptr) return false;
    if (key.getKeyCode() == juce::KeyPress::escapeKey && (presenter.viewState().midiMapMode || presenter.viewState().keyMapMode)) { presenter.setMidiMapMode(false); return true; }
    if (!key.getModifiers().isAnyModifierKeyDown() && presenter.mappedKey(key.getKeyCode(), true)) return true;
    if (key.getKeyCode() == '0' && !key.getModifiers().isAnyModifierKeyDown() && !presenter.viewState().selectedClips.isEmpty())
    { std::vector<Clip> edited; const auto* first = findId(presenter.project().clips, presenter.viewState().selectedClips[0]);
      if (first) for (const auto& clip : presenter.project().clips) if (presenter.viewState().selectedClips.contains(clip.id)) { edited.push_back(clip); edited.back().enabled = !first->enabled; }
      presenter.changeClips(edited); return true; }
    if (key.getKeyCode() == juce::KeyPress::tabKey && !key.getModifiers().isAnyModifierKeyDown()) { toggleMixer(); return true; }
    if (key.getKeyCode() == '`' || key.getKeyCode() == '~') { togglePiano(); return true; }
    if (key.getKeyCode() == juce::KeyPress::spaceKey) { presenter.togglePlayback(); return true; }
    if (key.getKeyCode() == juce::KeyPress::escapeKey) { timeline.cancelGesture(); return true; }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    { if (mixer.trackFocused() || timeline.trackFocused()) presenter.deleteTrack(presenter.viewState().selectedTrack);
      else if (!presenter.viewState().mixerOpen) timeline.deleteSelection(); return true; }
    return keyboardKeyPressed(key);
}
bool Editor::keyboardKeyPressed(const juce::KeyPress& key)
{
    if (!key.getModifiers().isAnyModifierKeyDown() && presenter.mappedKey(key.getKeyCode(), true)) return true;
    if (!presenter.viewState().computerKeyboard || presenter.viewState().midiMapMode || presenter.viewState().keyMapMode) return false;
    if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown())
    {
        const auto character = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
        const auto lower = juce::String("zsxdcvgbhnjm,l.;/").indexOfChar(character);
        const auto upper = juce::String("q2w3er5t6y7ui9o0p").indexOfChar(character);
        const auto note = lower >= 0 ? 60 + lower : upper >= 0 ? 72 + upper : -1;
        const auto* track = findTrack(presenter.project(), presenter.viewState().selectedTrack);
        if (note >= 0 && track && track->midi)
        {
            if (keyboardTrack != track->id) releaseKeyboard(true);
            if (keyboardNotes.count(key.getKeyCode()) == 0)
            {
                bool sounding = false; for (const auto& held : keyboardNotes) if (held.second == note) sounding = true;
                if (!sounding) presenter.inputNoteOn(track->id, note, 0.8);
                keyboardTrack = track->id; keyboardNotes[key.getKeyCode()] = note;
                std::bitset<128> pressed; for (const auto& held : keyboardNotes) pressed.set(static_cast<size_t>(held.second));
                midiPanel.setKeyboardNotes(pressed);
            }
            return true;
        }
    }
    return false;
}
bool Editor::keyStateChanged(bool) { releaseKeyboard(); return !keyboardNotes.empty(); }
bool Editor::keyPressed(const juce::KeyPress& key, juce::Component* source)
{
    const auto track = presenter.plugins().focusedEditorTrack(source);
    if (track.isEmpty() || juce::Component::getCurrentlyModalComponent() != nullptr) return false;
    if (key.getKeyCode() == juce::KeyPress::tabKey && !textFocused() && !key.getModifiers().isAnyModifierKeyDown()) { toggleMixer(); return true; }
    if (key.getKeyCode() == juce::KeyPress::spaceKey && !key.getModifiers().isAnyModifierKeyDown())
    { if (!pluginSpaceHeld) presenter.togglePlayback(); pluginSpaceHeld = true; return true; }
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (textFocused() && (!source || (focused && focused->getTopLevelComponent() == source->getTopLevelComponent()))) return false;
    if (presenter.viewState().selectedTrack != track) presenter.selectTrack(track);
    return keyboardKeyPressed(key);
}
bool Editor::keyStateChanged(bool, juce::Component*)
{ releaseKeyboard(); if (!isKeyboardKeyDown(juce::KeyPress::spaceKey)) pluginSpaceHeld = false; return false; }
void Editor::releaseKeyboard(bool all)
{
    for (auto it = keyboardNotes.begin(); it != keyboardNotes.end();)
    {
        if (!all && isKeyboardKeyDown(it->first)) { ++it; continue; }
        const auto note = it->second; it = keyboardNotes.erase(it);
        bool sounding = false; for (const auto& held : keyboardNotes) if (held.second == note) sounding = true;
        if (!sounding) presenter.inputNoteOff(keyboardTrack, note);
    }
    std::bitset<128> pressed; for (const auto& held : keyboardNotes) pressed.set(static_cast<size_t>(held.second));
    midiPanel.setKeyboardNotes(pressed);
}
void Editor::showDevicesMenu(const juce::String& trackId, juce::Component& anchor, bool instrument)
{
    const auto track = trackId; const auto* model = findTrack(presenter.project(), track); if (!model) return;
    if (instrument && (!model->midi || model->sampler || (!model->plugins.empty() && model->plugins.front().instrument)))
    { presenter.message("Remove the current instrument before adding another."); return; }
    presenter.selectTrack(track); refreshPanels();
    const auto types = presenter.plugins().availablePlugins();
    std::vector<std::pair<juce::String, juce::String>> choices;
    if (instrument) choices.emplace_back("sampler", "Sampler");
    for (const auto& type : types) if (type.isInstrument == instrument)
        choices.emplace_back(type.createIdentifierString(), type.name + " [" + type.pluginFormatName + "]");
    choices.emplace_back("scan", "Scan plug-ins...");
    searchMenu->show(anchor, std::move(choices), [this, track, types](const juce::String& id)
    {
        if (id == "scan") { showDevices(true); return; }
        if (id == "sampler") presenter.addSampler(track);
        else for (const auto& type : types) if (type.createIdentifierString() == id) { presenter.addPlugin(track, type); break; }
        refreshPanels();
    }, instrument ? "Instrument search" : "Effect search");
}
void Editor::toggleMixer()
{
    if (presenter.gesturing()) { midiPanel.cancelGesture(); if (presenter.gesturing()) timeline.cancelGesture(); }
    presenter.setMixerOpen(!presenter.viewState().mixerOpen); mixer.refresh(); refreshPanels();
    if (presenter.viewState().mixerOpen) mixer.grabKeyboardFocus(); else grabKeyboardFocus();
}
void Editor::togglePiano()
{
    if (!detailOpen && midiPanel.pianoVisible()) { detailOpen = true; refreshPanels(); grabKeyboardFocus(); return; }
    detailOpen = true;
    const auto& state = presenter.viewState();
    if (midiPanel.pianoVisible()) { midiPanel.cancelGesture(); presenter.setPianoPanel(false, state.pianoHeight); }
    else
    {
        juce::String clip;
        if (const auto* previous = findId(presenter.project().clips, state.editedMidiClip))
            if (previous->track == state.selectedTrack) clip = previous->id;
        for (const auto& value : presenter.project().clips) if (value.track == state.selectedTrack)
        { if (clip.isEmpty()) clip = value.id;
          if (state.selectedClips.contains(value.id)) { clip = value.id; break; } }
        if (clip.isEmpty()) { refreshPanels(); return; }
        presenter.openMidiClip(clip);
    }
    refreshPanels(); grabKeyboardFocus();
}
void Editor::refreshPanels()
{
    const auto& state = presenter.viewState(); const auto* track = findTrack(presenter.project(), state.selectedTrack);
    if (keyboardTrack != state.selectedTrack) { releaseKeyboard(true); keyboardTrack = state.selectedTrack; }
    const auto* clip = findId(presenter.project().clips, state.editedMidiClip);
    midiPanel.setVisible(track != nullptr && detailOpen);
    midiPanel.setContext(state.selectedTrack, clip && clip->track == state.selectedTrack ? clip->id : juce::String(), state.pianoOpen && clip && clip->track == state.selectedTrack);
    refreshFooter(); resized();
}
void Editor::refreshFooter()
{
    const auto* track = findTrack(presenter.project(), presenter.viewState().selectedTrack);
    selectedTrackButton.setButtonText(track ? track->name : "No track");
    selectedTrackButton.getProperties().set("footer-track-colour", static_cast<int>(track ? trackColour(presenter.project(), track->id).getARGB() : panel.getARGB()));
    for (auto* control : std::initializer_list<juce::Component*> { &selectedTrackButton, &viewButton, &detailButton, deviceOverview.get() }) control->setEnabled(track != nullptr);
    infoButton.setToggleState(infoVisible, juce::dontSendNotification); detailButton.setToggleState(detailOpen && track, juce::dontSendNotification);
    viewButton.getProperties().set("footer-icon", midiPanel.pianoVisible() ? "clip" : "device");
    deviceOverview->repaint();
}
void Editor::getAllCommands(juce::Array<juce::CommandID>& ids)
{ for (int id = newProject; id <= keyMapping; ++id) ids.add(id); }
void Editor::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const char* names[] = { "", "New", "Open...", "Save", "Save As...", "Undo", "Redo", "Import audio...", "Settings...", "Quit", "Select all clips", "Duplicate", "Copy", "Paste", "Add audio track", "Add MIDI track", "Mixer view", "Loop selection", "Import notes...", "Group tracks", "Ungroup tracks", "MIDI mapping", "Key mapping" };
    info.setInfo(names[id], names[id], id == undo || id == redo || id >= selectAll ? "Edit" : "File", 0);
    if (id == groupTracks) info.setActive(findTrack(presenter.project(), presenter.viewState().selectedTrack) != nullptr);
    if (id == ungroupTracks) { const auto* track = findTrack(presenter.project(), presenter.viewState().selectedTrack); info.setActive(track && (track->group || track->parentGroup.isNotEmpty())); }
    if (id == importNotes) info.setActive(presenter.project().importNotes.isNotEmpty());
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (id == midiMapping) info.addDefaultKeypress('m', cmd);
    if (id == keyMapping) info.addDefaultKeypress('k', cmd);
    if (id == groupTracks) info.addDefaultKeypress('g', cmd);
    if (id == ungroupTracks) info.addDefaultKeypress('g', cmd | juce::ModifierKeys::shiftModifier);
    if (id == newProject) info.addDefaultKeypress('n', cmd);
    if (id == openProject) info.addDefaultKeypress('o', cmd);
    if (id == saveProject) info.addDefaultKeypress('s', cmd);
    if (id == saveAs) info.addDefaultKeypress('s', cmd | juce::ModifierKeys::shiftModifier);
    if (id == undo) info.addDefaultKeypress('z', cmd);
    if (id == redo) { info.addDefaultKeypress('z', cmd | juce::ModifierKeys::shiftModifier); info.addDefaultKeypress('y', cmd); }
    if (id == importAudio) info.addDefaultKeypress('i', cmd);
    if (id == quit) info.addDefaultKeypress('q', cmd);
    if (id == selectAll) info.addDefaultKeypress('a', cmd);
    if (id == duplicate) info.addDefaultKeypress('d', cmd);
    if (id == copy) info.addDefaultKeypress('c', cmd);
    if (id == loopSelection) info.addDefaultKeypress('l', cmd);
    if (id == mixerView) info.addDefaultKeypress(juce::KeyPress::tabKey, 0);
    if (id == paste) info.addDefaultKeypress('v', cmd);
    if (id == addAudioTrack || id == addMidiTrack)
    {
        const auto shift = id == addMidiTrack ? juce::ModifierKeys::shiftModifier : 0;
        info.addDefaultKeypress('t', juce::ModifierKeys::ctrlModifier | shift);
        if (cmd != juce::ModifierKeys::ctrlModifier) info.addDefaultKeypress('t', cmd | shift);
    }
}
bool Editor::perform(const InvocationInfo& info)
{
    if (info.commandID >= selectAll && (textFocused() || juce::Component::getCurrentlyModalComponent() != nullptr || presenter.gesturing())) return false;
    switch (info.commandID)
    {
        case newProject: presenter.newProject(); break;
        case openProject: chooseOpen(); break;
        case midiMapping: presenter.setMidiMapMode(!presenter.viewState().midiMapMode); break;
        case keyMapping: presenter.setKeyMapMode(!presenter.viewState().keyMapMode); break;
        case saveProject: presenter.save(false); break;
        case saveAs: presenter.save(true); break;
        case undo: if (textFocused()) return false; presenter.undo(); break;
        case redo: if (textFocused()) return false; presenter.redo(); break;
        case importAudio: chooseImport(); break;
        case audioSettings: showDevices(); break;
        case quit: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case loopSelection: presenter.loopSelection(); break;
        case mixerView: toggleMixer(); break;
        case addAudioTrack: presenter.addTrack(); break;
        case groupTracks: presenter.groupSelectedTracks(); break;
        case ungroupTracks: presenter.ungroupSelectedTrack(); break;
        case addMidiTrack: presenter.addMidiTrack(); break;
        case importNotes: juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Ableton import notes", presenter.project().importNotes, "Close", this); break;
        case selectAll:
            if (timeline.automationCommand('a')) break;
            if (midiPanel.noteFocused()) midiPanel.selectAllNotes(); else presenter.selectAllClips(); break;
        case duplicate: if (timeline.automationCommand('d')) break; if (midiPanel.noteFocused()) midiPanel.duplicateNotes(); else if (timeline.trackFocused() || mixer.trackFocused()) presenter.duplicateTrack(); else presenter.duplicateSelection(); break;
        case copy: if (timeline.automationCommand('c')) break; if (midiPanel.noteFocused()) midiPanel.copyNotes(); else if (timeline.trackFocused() || mixer.trackFocused()) presenter.copyTrack(); else presenter.copySelection(); break;
        case paste: if (timeline.automationCommand('v')) break; if (midiPanel.noteFocused()) midiPanel.pasteNotes(); else if (timeline.trackFocused() || mixer.trackFocused()) presenter.pasteTrack(); else presenter.pasteSelection(); break;
        default: return false;
    }
    return true;
}
juce::StringArray Editor::getMenuBarNames() { return { "File", "Edit" }; }
juce::PopupMenu Editor::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu result;
    if (index == 0)
    {
        result.addCommandItem(&commands, newProject); result.addCommandItem(&commands, openProject);
        juce::PopupMenu recent; recentMenuFiles = presenter.recentProjects();
        for (int i = 0; i < recentMenuFiles.size(); ++i)
        {
            const juce::File file(recentMenuFiles[i]);
            recent.addItem(1000 + i, file.getFileName() + " [" + file.getParentDirectory().getFileName() + "]", file.existsAsFile());
        }
        if (recentMenuFiles.isEmpty()) recent.addItem(1099, "No recent projects", false);
        recent.addSeparator(); recent.addItem(1100, "Remove Unavailable Projects", !recentMenuFiles.isEmpty());
        recent.addItem(1101, "Clear List", !recentMenuFiles.isEmpty()); result.addSubMenu("Open Recent", recent);
        result.addSeparator();
        for (auto id : { saveProject, saveAs, importAudio, importNotes, audioSettings, quit }) result.addCommandItem(&commands, id);
    }
    else for (auto id : { undo, redo, duplicate, copy, paste, selectAll, addAudioTrack, addMidiTrack, mixerView, loopSelection, groupTracks, ungroupTracks, midiMapping, keyMapping }) result.addCommandItem(&commands, id);
    return result;
}
void Editor::menuItemSelected(int item, int)
{
    if (item >= 1000 && item < 1000 + recentMenuFiles.size())
    {
        const juce::File file(recentMenuFiles[item - 1000]);
        if (file.existsAsFile()) presenter.open(file.hasFileExtension("als") ? file : file.getParentDirectory());
    }
    else if (item == 1100 || item == 1101) presenter.cleanRecentProjects(item == 1101);
}
void Editor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    browser.refresh(); timeline.refresh(); mixer.refresh(); transport.refresh(); refreshPanels(); commands.commandStatusChanged();
    if (recentMenuFiles != presenter.recentProjects()) menuItemsChanged();
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>()) window->setName(presenter.windowTitle());
    matchTitleBar(*this, panel);
}
void Editor::timerCallback()
{
    presenter.releaseMappedKeys();
    presenter.plugins().updateEditorWindowLevels();
    releaseKeyboard(textFocused() || !juce::Process::isForegroundProcess()
        || (!hasKeyboardFocus(true) && presenter.plugins().focusedEditorTrack().isEmpty()));
    if (!isKeyboardKeyDown(juce::KeyPress::spaceKey)) pluginSpaceHeld = false;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    worstLag = std::max(worstLag, std::max(0.0, now - lastTick - 1000.0 / 30)); lastTick = now;
    if (now - lagWindow >= 1000) { recentLag = worstLag; worstLag = 0; lagWindow = now; }
    transport.refresh(); timeline.tick(); mixer.tick();
    auto message = presenter.status().replace(" | Underruns: 0", "").replace("Saved | ", "");
    if (infoVisible)
    {
        auto* hovered = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
        while (hovered && hovered != this && hovered->getTitle().isEmpty()) hovered = hovered->getParentComponent();
        if (hovered && hovered != this && isParentOf(hovered) && hovered != &status) message += (message.isEmpty() ? "" : " | ") + hovered->getTitle();
    }
    label(status, message); status.setFont(juce::FontOptions(10)); deviceOverview->repaint();
    const auto lag = std::max(recentLag, worstLag);
    label(performance, juce::String(presenter.devices().getCpuUsage() * 100, 1) + "% | " + juce::String(juce::roundToInt(lag)) + " ms");
    performance.setFont(juce::FontOptions(10)); performance.setTitle("CPU load / UI delay");
    performance.setColour(juce::Label::textColourId, lag > 33 ? juce::Colour(0xffa23b32) : ink);
}
void Editor::requestQuit(void (*done)()) { presenter.requestQuit(done); }
bool Editor::canReload() const
{
    return !presenter.gesturing() && !textFocused() && !choosing && pendingDialogs == 0 && !searchMenu->isVisible() && midiPanel.canReload() && browser.canReload()
        && juce::Component::getCurrentlyModalComponent() == nullptr;
}
void Editor::askSaveChanges(std::function<void(EditorPresenter::SaveChoice)> reply)
{
    juce::Component::SafePointer<Editor> safe(this); ++pendingDialogs;
    const auto name = presenter.projectName();
    auto* dialog = new SaveChangesDialog(name.isEmpty() ? juce::String("Untitled") : name, *this);
    dialog->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, reply](int choice)
    {
        if (!safe) return; --safe->pendingDialogs;
        reply(choice == 1 ? EditorPresenter::SaveChoice::save : (choice == 2 ? EditorPresenter::SaveChoice::discard : EditorPresenter::SaveChoice::cancel));
    }), true);
    if (auto* button = dialog->getButton("Save")) button->grabKeyboardFocus();
}
void Editor::chooseOpen()
{
    if (choosing) return; choosing = true;
    chooser = std::make_unique<juce::FileChooser>("Open Opentone project or Ableton Live Set", juce::File(), "*.opentone;*.als");
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& choice)
        { if (!safe) return; safe->choosing = false; if (choice.getResult().existsAsFile()) safe->presenter.open(choice.getResult().hasFileExtension("als") ? choice.getResult() : choice.getResult().getParentDirectory()); });
}
void Editor::chooseImport()
{
    if (choosing) return; choosing = true;
    chooser = std::make_unique<juce::FileChooser>("Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [safe](const juce::FileChooser& choice)
        { if (!safe) return; safe->choosing = false; juce::StringArray files; for (const auto& file : choice.getResults()) files.add(file.getFullPathName()); safe->presenter.importFiles(files, {}, 0, true); });
}
void Editor::chooseSaveDirectory(std::function<void(juce::File)> reply)
{
    if (choosing) { reply({}); return; } choosing = true;
    chooser = std::make_unique<juce::FileChooser>("Choose a project directory", juce::File());
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe, reply](const juce::FileChooser& choice) { if (!safe) return; safe->choosing = false; reply(choice.getResult()); });
}
void Editor::confirmOverwrite(const juce::File&, std::function<void(bool)> reply)
{
    juce::Component::SafePointer<Editor> safe(this); ++pendingDialogs;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Replace existing project?")
        .withMessage("This directory already contains a project. Replace its manifest?")
        .withButton("Replace").withButton("Cancel").withAssociatedComponent(this),
        [safe, reply](int choice) { if (!safe) return; --safe->pendingDialogs; reply(choice == 1); });
}
namespace
{
class Appearance final : public juce::Component, private juce::ChangeListener
{
public:
    explicit Appearance(std::function<void()> changed) : onChange(std::move(changed)), picker(juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace)
    {
        addAndMakeVisible(role); addAndMakeVisible(picker); addAndMakeVisible(reset);
        addAndMakeVisible(preset); addAndMakeVisible(presetLabel); label(presetLabel, "Theme");
        preset.addItemList({"Light", "Dark", "Custom"}, 1);
        darkBase = background.getPerceivedBrightness() < 0.5f;
        preset.setSelectedId(uiThemePreset(), juce::dontSendNotification);
        preset.onChange = [this]
        { if (preset.getSelectedId() == 3) return;
          darkBase = preset.getSelectedId() == 2; setUiThemePreset(darkBase); role.onChange(); onChange(); };
        role.addItemList({"Background", "Panels", "Highlight", "Text", "Grid and borders", "Primary"}, 1);
        role.onChange = [this] { picker.setCurrentColour(*colours[static_cast<size_t>(role.getSelectedId() - 1)], juce::dontSendNotification); };
        role.setSelectedId(1); role.onChange(); picker.addChangeListener(this);
        reset.onClick = [this] { setUiThemePreset(darkBase); preset.setSelectedId(darkBase ? 2 : 1, juce::dontSendNotification); role.onChange(); onChange(); };
    }
    ~Appearance() override { picker.removeChangeListener(this); saveUiColours(); }
    void resized() override
    { auto area = getLocalBounds().reduced(12); auto themes = area.removeFromTop(28);
      presetLabel.setBounds(themes.removeFromLeft(60)); preset.setBounds(themes); area.removeFromTop(8); auto bar = area.removeFromTop(28);
      reset.setBounds(bar.removeFromRight(100)); bar.removeFromRight(8); role.setBounds(bar); area.removeFromTop(8); picker.setBounds(area); }
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    { *colours[static_cast<size_t>(role.getSelectedId() - 1)] = picker.getCurrentColour().withAlpha(1.0f); preset.setSelectedId(uiThemePreset(), juce::dontSendNotification); onChange(); }
    std::function<void()> onChange; bool darkBase = false; juce::ComboBox role, preset; juce::Label presetLabel; juce::TextButton reset {"Reset colors"}; juce::ColourSelector picker;
    std::array<juce::Colour*, 6> colours { &background, &panel, &accent, &ink, &line, &primary };
};
}
void Editor::applyColours()
{
    configureUiTheme(*theme); matchTitleBar(*this, panel);
    if (themedWindow) { themedWindow->setBackgroundColour(panel); themedWindow->repaint(); }
    std::function<void(juce::Component&)> update = [&](juce::Component& component)
    {
        if (auto* text = dynamic_cast<juce::Label*>(&component))
        { if (!static_cast<bool>(text->getProperties()["track-title"]))
          { const auto* slider = text->findParentComponentOfClass<juce::Slider>();
            text->setColour(juce::Label::textColourId, slider ? slider->findColour(juce::Slider::textBoxTextColourId) : ink); } }
        if (auto* text = dynamic_cast<juce::TextEditor*>(&component))
        { const auto* slider = text->findParentComponentOfClass<juce::Slider>();
          text->setColour(juce::TextEditor::textColourId, slider ? slider->findColour(juce::Slider::textBoxTextColourId) : ink); text->setColour(juce::TextEditor::backgroundColourId, slider && static_cast<bool>(slider->getProperties()["mixer-volume"]) ? uiTone(UiTone::editField) : uiTone(UiTone::control)); }
        if (auto* slider = dynamic_cast<juce::Slider*>(&component))
        { slider->setColour(juce::Slider::textBoxTextColourId, static_cast<bool>(slider->getProperties()["mixer-pan"]) || static_cast<bool>(slider->getProperties()["mixer-volume"]) || static_cast<bool>(slider->getProperties()["mixer-send"]) ? ink : theme->findColour(juce::Slider::textBoxTextColourId));
          slider->setColour(juce::Slider::textBoxBackgroundColourId, slider->isBar() ? juce::Colours::transparentBlack : uiTone(UiTone::control)); }
        for (int i = 0; i < component.getNumChildComponents(); ++i) update(*component.getChildComponent(i));
    };
    update(*this); sendLookAndFeelChange(); repaint();
}
void Editor::showDevices(bool plugins, bool audio)
{
    if (pendingDialogs != 0) return;
    ++pendingDialogs; juce::Component::SafePointer<Editor> safe(this);
    auto tabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    tabs->addTab("Appearance", panel, new Appearance([safe] { if (safe) safe->applyColours(); }), true);
    tabs->addTab("Audio", panel, new juce::AudioDeviceSelectorComponent(presenter.devices(), 0, 2, 2, 2, true, false, true, false), true);
    tabs->addTab("Plug-ins", panel, presenter.plugins().scanner().release(), true);
    if (plugins) tabs->setCurrentTabIndex(2); else if (audio) tabs->setCurrentTabIndex(1);
    tabs->setSize(680, 480); juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(tabs.release()); options.dialogTitle = "Settings";
    options.dialogBackgroundColour = panel; options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true; options.resizable = false;
    if (auto* dialog = options.launchAsync())
    { settingsWindow = dialog; juce::ModalComponentManager::getInstance()->attachCallback(dialog, juce::ModalCallbackFunction::create([safe](int) { if (safe) --safe->pendingDialogs; })); }
    else --pendingDialogs;
}
}
