// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/Editor.h"
#include "ui/UiTheme.h"
#include "ui/NativeWindow.h"
namespace opentone
{
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
class Editor::PianoGrip final : public juce::Component
{
public:
    explicit PianoGrip(Editor& editor) : owner(editor)
    { setTitle("Piano roll top edge; drag to resize or fully down to close"); setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    void paint(juce::Graphics& g) override
    { g.fillAll(panel); g.setColour(line); g.drawHorizontalLine(0, 0, static_cast<float>(getWidth()));
      g.setColour(ink); g.fillRoundedRectangle(static_cast<float>(getWidth() / 2 - 20), 2, 40, 2, 1); }
    void mouseDown(const juce::MouseEvent& e) override
    { if (e.mods.isLeftButtonDown()) { dragging = true; startY = e.getScreenY(); startHeight = owner.midiPanel.getHeight(); } }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging) return;
        const auto height = startHeight + startY - e.getScreenY();
        if (height <= 0) { owner.midiPanel.cancelGesture(); owner.presenter.setPianoPanel(false, startHeight); }
        else owner.presenter.setPianoPanel(true, height);
        owner.refreshPanels();
    }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
private: Editor& owner; int startY = 0, startHeight = 240; bool dragging = false;
};
Editor::Editor(EditorPresenter& owner)
    : presenter(owner), theme(createUiTheme()), browser(owner), transport(owner), timeline(owner), mixer(owner), midiPanel(owner),
      searchMenu(std::make_unique<SearchMenu>(*this)), pianoGrip(std::make_unique<PianoGrip>(*this))
{
    loadUiColours(); configureUiTheme(*theme); setLookAndFeel(theme.get());
    setSize(1200, 760); setWantsKeyboardFocus(true); setOpaque(true);
    setColour(juce::ResizableWindow::backgroundColourId, background);
    for (auto* item : std::initializer_list<juce::Component*> { &menu, &browser, &transport, &timeline, &mixer, &mixerButton, &status, &performance }) addAndMakeVisible(item);
#if JUCE_MAC
    menu.setVisible(false); juce::MenuBarModel::setMacMainMenu(this);
#endif
    addChildComponent(midiPanel); addChildComponent(*searchMenu); addChildComponent(*pianoGrip);
    performance.setJustificationType(juce::Justification::centredRight);
    performance.setTitle("Audio CPU load and peak UI lag in milliseconds"); label(performance, "CPU 0.0% | UI lag 0 ms");
    commands.registerAllCommandsForTarget(this); addKeyListener(commands.getKeyMappings());
    presenter.attach(*this); presenter.addChangeListener(this);
    presenter.plugins().keyboardListener = this; pluginKeyMonitor = installPluginKeyMonitor(*this);
    mixerButton.setTitle("Toggle mixer and arrangement (Tab)"); mixerButton.onClick = [this] { toggleMixer(); };
    timeline.onFiles = [this](const auto& paths, auto track, double start) { presenter.importFiles(paths, track, start); };
    timeline.onMidiClip = [this](auto clip) { presenter.openMidiClip(clip); refreshPanels(); };
    midiPanel.onAddDevice = [this](auto& anchor, bool instrument) { showDevicesMenu(presenter.viewState().selectedTrack, anchor, instrument); };
    midiPanel.onChooseClip = [this](auto& anchor)
    {
        std::vector<std::pair<juce::String, juce::String>> choices;
        for (const auto& clip : presenter.project().clips) if (const auto* asset = findId(presenter.project().assets, clip.asset))
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
    auto bounds = getLocalBounds(); auto menuBounds = bounds.removeFromTop(21);
    performance.setBounds(menuBounds.removeFromRight(208).reduced(6, 0));
#if !JUCE_MAC
    menu.setBounds(menuBounds);
#endif
    transport.setBounds(bounds.removeFromTop(24));
    auto footer = bounds.removeFromBottom(22).reduced(6, 2);
    mixerButton.setBounds(footer.removeFromLeft(64)); footer.removeFromLeft(8); status.setBounds(footer);
    bounds = bounds.reduced(6, 5);
    if (midiPanel.isVisible())
    {
        const auto rollHeight = midiPanel.pianoVisible() ? std::min(presenter.viewState().pianoHeight, std::max(0, bounds.getHeight() - 6)) : 0;
        auto bottom = bounds.removeFromBottom(rollHeight > 0 ? rollHeight + 6 : MidiEditor::instrumentHeight);
        pianoGrip->setVisible(rollHeight > 0); pianoGrip->setBounds(bottom.removeFromTop(rollHeight > 0 ? 6 : 0)); midiPanel.setBounds(bottom);
    }
    else pianoGrip->setVisible(false);
    const bool available = bounds.getHeight() > 30, mixing = presenter.viewState().mixerOpen;
    browser.setVisible(available); timeline.setVisible(available && !mixing); mixer.setVisible(available && mixing);
    mixerButton.setToggleState(mixing, juce::dontSendNotification);
    browser.setBounds(bounds.removeFromLeft(222)); bounds.removeFromLeft(5); timeline.setBounds(bounds); mixer.setBounds(bounds);
}
bool Editor::textFocused() const
{ auto* focused = juce::Component::getCurrentlyFocusedComponent();
  return dynamic_cast<juce::TextEditor*>(focused) != nullptr || (focused && focused->findParentComponentOfClass<juce::TextEditor>() != nullptr); }
bool Editor::keyPressed(const juce::KeyPress& key)
{
    if (textFocused() || juce::Component::getCurrentlyModalComponent() != nullptr) return false;
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
        if (clip.isEmpty()) return;
        presenter.openMidiClip(clip);
    }
    refreshPanels(); grabKeyboardFocus();
}
void Editor::refreshPanels()
{
    const auto& state = presenter.viewState(); const auto* track = findTrack(presenter.project(), state.selectedTrack);
    if (keyboardTrack != state.selectedTrack) { releaseKeyboard(true); keyboardTrack = state.selectedTrack; }
    const auto* clip = findId(presenter.project().clips, state.editedMidiClip);
    midiPanel.setVisible(track != nullptr);
    midiPanel.setContext(state.selectedTrack, clip && clip->track == state.selectedTrack ? clip->id : juce::String(), state.pianoOpen && clip && clip->track == state.selectedTrack);
    resized();
}
void Editor::getAllCommands(juce::Array<juce::CommandID>& ids)
{ for (int id = newProject; id <= loopSelection; ++id) ids.add(id); }
void Editor::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const char* names[] = { "", "New", "Open...", "Save", "Save As...", "Undo", "Redo", "Import audio...", "Settings...", "Quit", "Select all clips", "Duplicate", "Copy", "Paste", "Add audio track", "Add MIDI track", "Mixer view", "Loop selection" };
    info.setInfo(names[id], names[id], id == undo || id == redo || id >= selectAll ? "Edit" : "File", 0);
    const auto cmd = juce::ModifierKeys::commandModifier;
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
        case addMidiTrack: presenter.addMidiTrack(); break;
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
    if (index == 0) for (auto id : { newProject, openProject, saveProject, saveAs, importAudio, audioSettings, quit }) result.addCommandItem(&commands, id);
    else for (auto id : { undo, redo, duplicate, copy, paste, selectAll, addAudioTrack, addMidiTrack, mixerView, loopSelection }) result.addCommandItem(&commands, id);
    return result;
}
void Editor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    browser.refresh(); timeline.refresh(); mixer.refresh(); transport.refresh(); refreshPanels();
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>()) window->setName(presenter.windowTitle());
    matchTitleBar(*this, panel);
}
void Editor::timerCallback()
{
    presenter.plugins().updateEditorWindowLevels();
    releaseKeyboard(textFocused() || !juce::Process::isForegroundProcess()
        || (!hasKeyboardFocus(true) && presenter.plugins().focusedEditorTrack().isEmpty()));
    if (!isKeyboardKeyDown(juce::KeyPress::spaceKey)) pluginSpaceHeld = false;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    worstLag = std::max(worstLag, std::max(0.0, now - lastTick - 1000.0 / 30)); lastTick = now;
    if (now - lagWindow >= 1000) { recentLag = worstLag; worstLag = 0; lagWindow = now; }
    transport.refresh(); timeline.tick(); mixer.tick(); label(status, presenter.status());
    const auto lag = std::max(recentLag, worstLag);
    label(performance, "CPU " + juce::String(presenter.devices().getCpuUsage() * 100, 1)
                       + "% | UI lag " + juce::String(juce::roundToInt(lag)) + " ms");
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
    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Save project changes?").withMessage("Save your changes before continuing?")
        .withButton("Save").withButton("Discard").withButton("Cancel").withAssociatedComponent(this),
        [safe, reply](int choice)
        {
            if (!safe) return; --safe->pendingDialogs;
            reply(choice == 1 ? EditorPresenter::SaveChoice::save : (choice == 2 ? EditorPresenter::SaveChoice::discard : EditorPresenter::SaveChoice::cancel));
        });
}
void Editor::chooseOpen()
{
    if (choosing) return; choosing = true;
    chooser = std::make_unique<juce::FileChooser>("Open project.opentone", juce::File(), "*.opentone");
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& choice)
        { if (!safe) return; safe->choosing = false; if (choice.getResult().existsAsFile()) safe->presenter.open(choice.getResult().getParentDirectory()); });
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
        { const auto colour = text->findColour(juce::Label::backgroundColourId);
          if (!static_cast<bool>(text->getProperties()["track-title"]))
              text->setColour(juce::Label::textColourId, colour.isOpaque() ? colour.contrasting() : ink); }
        if (auto* text = dynamic_cast<juce::TextEditor*>(&component))
        { text->setColour(juce::TextEditor::textColourId, ink); text->setColour(juce::TextEditor::backgroundColourId, background); }
        if (auto* slider = dynamic_cast<juce::Slider*>(&component))
        { slider->setColour(juce::Slider::textBoxTextColourId, ink); slider->setColour(juce::Slider::textBoxBackgroundColourId, background); }
        for (int i = 0; i < component.getNumChildComponents(); ++i) update(*component.getChildComponent(i));
    };
    update(*this); sendLookAndFeelChange(); repaint();
}
void Editor::showDevices(bool plugins)
{
    if (pendingDialogs != 0) return;
    ++pendingDialogs; juce::Component::SafePointer<Editor> safe(this);
    auto tabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    tabs->addTab("Appearance", panel, new Appearance([safe] { if (safe) safe->applyColours(); }), true);
    tabs->addTab("Audio", panel, new juce::AudioDeviceSelectorComponent(presenter.devices(), 0, 2, 2, 2, true, false, true, false), true);
    tabs->addTab("Plug-ins", panel, presenter.plugins().scanner().release(), true);
    if (plugins) tabs->setCurrentTabIndex(2);
    tabs->setSize(680, 480); juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(tabs.release()); options.dialogTitle = "Settings";
    options.dialogBackgroundColour = panel; options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true; options.resizable = false;
    if (auto* dialog = options.launchAsync())
    { settingsWindow = dialog; juce::ModalComponentManager::getInstance()->attachCallback(dialog, juce::ModalCallbackFunction::create([safe](int) { if (safe) --safe->pendingDialogs; })); }
    else --pendingDialogs;
}
}
