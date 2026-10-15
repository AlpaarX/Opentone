// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/Browser.h"
#include "ui/Transport.h"
#include "ui/Timeline.h"
#include "ui/Mixer.h"
#include "ui/MidiEditor.h"
namespace opentone
{
class Editor final : public juce::Component, public juce::DragAndDropContainer, private juce::ChangeListener, private juce::Timer,
                     public juce::MenuBarModel, public juce::ApplicationCommandTarget,
                     public EditorPresenter::Dialogs, private juce::KeyListener
{
public:
    explicit Editor(EditorPresenter&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    void requestQuit(void (*done)());
    bool canReload() const;
    void getAllCommands(juce::Array<juce::CommandID>&) override;
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform(const InvocationInfo&) override;
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int, const juce::String&) override;
    void menuItemSelected(int, int) override;
private:
    enum Command { newProject = 1, openProject, saveProject, saveAs, undo, redo, importAudio, audioSettings, quit, selectAll, duplicate, copy, paste, addAudioTrack, addMidiTrack, mixerView, loopSelection, importNotes, groupTracks, ungroupTracks, midiMapping, keyMapping };
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void chooseOpen();
    void chooseImport();
    void showDevices(bool plugins = false, bool audio = false);
    void applyColours();
    void askSaveChanges(std::function<void(EditorPresenter::SaveChoice)>) override;
    void chooseSaveDirectory(std::function<void(juce::File)>) override;
    void confirmOverwrite(const juce::File&, std::function<void(bool)>) override;
    bool textFocused() const;
    bool keyboardKeyPressed(const juce::KeyPress&);
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool, juce::Component*) override;
    void* pluginKeyMonitor = nullptr;
    bool pluginSpaceHeld = false;
    class SearchMenu;
    class DetailGrip;
    class MappingOverlay;
    class BrowserGrip;
    class DeviceOverview;
    void refreshPanels();
    void refreshFooter();
    void togglePiano();
    void toggleMixer();
    void showDevicesMenu(const juce::String&, juce::Component&, bool instrument);
    void releaseKeyboard(bool all = false);
    EditorPresenter& presenter;
    std::unique_ptr<juce::LookAndFeel_V4> theme;
    juce::ApplicationCommandManager commands;
    juce::MenuBarComponent menu { this };
    Browser browser;
    Transport transport;
    Timeline timeline;
    Mixer mixer;
    juce::TextButton mixerButton {"Mixer"}, browserButton {"Browser"};
    juce::TextButton infoButton {"Info"}, detailButton {"Detail view"}, viewButton {"Clip / Device"}, selectedTrackButton {"Selected track"};
    bool infoVisible = false;
    bool browserOpen = true;
    int browserWidth = 360;
    bool detailOpen = true;
    MidiEditor midiPanel;
    std::unique_ptr<SearchMenu> searchMenu;
    std::unique_ptr<DetailGrip> detailGrip;
    std::unique_ptr<MappingOverlay> mappingOverlay;
    std::unique_ptr<BrowserGrip> browserGrip;
    std::unique_ptr<DeviceOverview> deviceOverview;
    std::map<int, int> keyboardNotes;
    juce::String keyboardTrack;
    juce::Label status;
    juce::Label performance;
    double lastTick = juce::Time::getMillisecondCounterHiRes(), lagWindow = lastTick, worstLag = 0, recentLag = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray recentMenuFiles;
    bool choosing = false;
    int pendingDialogs = 0;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    juce::Component::SafePointer<juce::DocumentWindow> themedWindow;
};
}
