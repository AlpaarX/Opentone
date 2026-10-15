// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "domain/ProjectSession.h"
#include "infrastructure/AudioEngine.h"
#include "infrastructure/Storage.h"

namespace opentone
{
// Message-thread application coordinator. Views can be replaced; these jobs and state survive.
class OPENTONE_API EditorPresenter final : public juce::ChangeBroadcaster,
                                          private juce::ChangeListener, private juce::Timer,
                                          private juce::MidiInputCallback, private juce::AsyncUpdater
{
public:
    enum class SaveChoice { save, discard, cancel };
    struct Dialogs
    {
        virtual ~Dialogs() = default;
        virtual void askSaveChanges(std::function<void(SaveChoice)>) = 0;
        virtual void chooseSaveDirectory(std::function<void(juce::File)>) = 0;
        virtual void confirmOverwrite(const juce::File&, std::function<void(bool)>) = 0;
    };
    struct ViewState
    {
        juce::StringArray selectedClips;
        std::optional<TimeSelection> timeSelection;
        
        double pixelsPerSecond = 64;
        bool snap = true, automationView = false, follow = false, lockAutomation = false, computerKeyboard = true, drawMode = false, midiMapMode = false, keyMapMode = false, overdub = false, automationRecord = false, punchIn = false, punchOut = false;
        juce::String midiLearnTarget;
        std::map<juce::String, int> automationParameters;
        int scrollX = 0, scrollY = 0;
        juce::String selectedTrack, editedMidiClip;
        juce::StringArray armedTracks, selectedTracks;
        int pianoHeight = 240; bool pianoOpen = false, mixerOpen = false;
        double pianoZoom = 180; int pianoKeyHeight = 14;
    };
    struct Loading { juce::String operation, track, name; double start; };

    EditorPresenter(ProjectSession&, Storage&, AudioEngine&);
    ~EditorPresenter() override;
    void attach(Dialogs& next) { dialogs = &next; }
    void detach(Dialogs& previous) { if (dialogs == &previous) dialogs = nullptr; }
    const Project& project() const { return session.project(); }
    const juce::File& mediaDirectory() const { return session.mediaDirectory(); }
    juce::String projectName() const
    { return session.location() == storage.workspace() ? importedName : session.location() == juce::File() ? juce::String() : session.location().getFileName(); }
    juce::String windowTitle() const { const auto name = projectName(); return (name.isEmpty() ? juce::String("Untitled") : name) + " - Opentone"; }
    bool dirty() const { return session.dirty(); }
    bool gesturing() const { return session.gesturing(); }
    const ViewState& viewState() const { return state; }
    const std::vector<Loading>& loading() const { return imports; }
    bool busy() const { return busySave || busyOpen || pendingImports != 0; }
    void selectClip(juce::String, bool additive = false);
    void selectClips(juce::StringArray);
    void selectAllClips();
    void selectTime(TimeSelection);
    std::vector<Clip> splitTimeSelection();
    void duplicateSelection();
    void copySelection();
    void pasteSelection();
    void setZoom(double);
    void setSnap(bool);
    void setLoop(double start, double end, bool enabled) { session.setLoop(start, end, enabled); }
    void loopSelection();
    void toggleLoop() { setLoop(project().loopStart, project().loopEnd, !project().loopEnabled); }
    void copyAutomation(std::vector<AutomationPoint> points, int parameter) { automationClipboard = std::move(points); automationClipboardParameter = parameter; }
    const std::vector<AutomationPoint>& copiedAutomation() const { return automationClipboard; }
    int copiedAutomationParameter() const { return automationClipboardParameter; }
    void setScroll(int x, int y);
    void setTrackHeight(const juce::String&, int height);
    bool trackFolded(const juce::String& id) const { const auto* track = findTrack(project(), id); return track && track->group ? track->groupCollapsed : track && (track->folded || track->height <= 18); }
    void toggleTrackFold(const juce::String&);
    void selectTrack(const juce::String&, bool additive = false, bool range = false);
    void groupSelectedTracks();
    void createGroup();
    void ungroupSelectedTrack();
    void removeFromGroup(const juce::String&);
    void dropTracks(const juce::String& description, const juce::String& destination);
    void openMidiClip(const juce::String&);
    void setPianoPanel(bool open, int height);
    void setPianoZoom(double zoom, int height)
    { if (!std::isfinite(zoom)) return; state.pianoZoom = juce::jlimit(32.0, 40000.0, zoom); state.pianoKeyHeight = juce::jlimit(8, 48, height); sendChangeMessage(); }
    void setAutomationView(bool enabled) { state.automationView = enabled; sendChangeMessage(); }
    int automationParameter(const juce::String& id) const
    { const auto found = state.automationParameters.find(id); return found == state.automationParameters.end() ? 0 : found->second; }
    bool automationPan(const juce::String& id) const { return automationParameter(id) == 1; }
    void setAutomationParameter(const juce::String& id, int parameter) { state.automationParameters[id] = parameter; sendChangeMessage(); }
    void setAutomationPan(const juce::String& id, bool pan) { setAutomationParameter(id, pan ? 1 : 0); }
    const std::vector<AutomationPoint>& automation(const juce::String& id) const
    { static const std::vector<AutomationPoint> empty; const auto* track = findTrack(project(), id); return track ? automationPoints(*track, automationParameter(id)) : empty; }
    void setAutomation(const juce::String& id, int parameter, std::vector<AutomationPoint> points) { session.setTrackAutomation(id, parameter, std::move(points)); }
    void setPan(const juce::String& id, double pan);
    double trackAutomationValue(const juce::String& id, int parameter) const
    { const auto* track = findTrack(project(), id); if (!track) return 0; const auto limits = automationLimits(parameter);
      return juce::jlimit(limits.first, limits.second, automationValue(automationPoints(*track, parameter), position(), automationFallback(*track, parameter))); }
    double trackValue(const juce::String& id, bool pan) const
    { const auto* track = findTrack(project(), id); return track ? automationValue(pan ? track->panAutomation : track->volumeAutomation, position(), pan ? track->pan : track->gain) : 0; }
    void setMixerOpen(bool open) { state.mixerOpen = open; sendChangeMessage(); }
    std::pair<float, float> trackLevels(const juce::String& track) const
    { const auto* value = findId(project().tracks, track); return armed(track) && value && !value->midi ? engine.inputLevels() : engine.plugins.levels(track); }
    std::pair<float, float> masterLevels() const { return engine.masterLevels(); }

    juce::String addTrack();
    juce::String addMidiTrack();
    const juce::StringArray& recentProjects() const { return storage.recentProjects(); }
    void cleanRecentProjects(bool clear)
    { const auto result = storage.cleanRecentProjects(clear); if (result.failed()) message(result.getErrorMessage()); else sendChangeMessage(); }
    const juce::StringArray& sampleFolders() const { return storage.sampleFolders(); }
    void addSampleFolder(const juce::File& folder) { auto result = storage.addSampleFolder(folder); if (result.failed()) message(result.getErrorMessage()); else sendChangeMessage(); }
    void removeSampleFolder(const juce::File& folder) { auto result = storage.removeSampleFolder(folder); if (result.failed()) message(result.getErrorMessage()); else sendChangeMessage(); }
    void addDevice(const juce::String& identifier, juce::String track);
    bool isBrowserDrag(const juce::String& text) const { return text.startsWith("device:") || text.startsWith("sample:"); }
    void dropBrowserItem(const juce::String& text, const juce::String& track, double start = 0)
    { if (text.startsWith("device:")) addDevice(text.substring(7), track); else if (text.startsWith("sample:")) importFiles({text.substring(7)}, track, start); }
    void loadSampler(const juce::String& track, const juce::String& asset, double offset, double duration);
    void setRootNote(const juce::String& track, int note) { session.setRootNote(track, note); }
    void addSampler(const juce::String& track) { session.addSampler(track); }
    void bypassSampler(const juce::String& id, bool bypass)
    { if (bypass) engine.releasePreview(id, -1); session.bypassSampler(id, bypass); }
    void removeSampler(const juce::String& track);
    void moveSampler(const juce::String& source, const juce::String& destination);
    void setSamplerGain(const juce::String& track, double gain) { session.setSamplerGain(track, gain); }
    void copyMidiNotes(std::vector<MidiNote> notes) { midiClipboard = std::move(notes); }
    const std::vector<MidiNote>& midiNotesClipboard() const { return midiClipboard; }
    bool previewNote(const juce::String& track, int note, double velocity = 0.8, bool held = false);
    void releasePreview(const juce::String& track, int note) { engine.releasePreview(track, note); }
    double samplerPosition(const juce::String& id) const { return engine.samplerPosition(id); }
    std::bitset<128> activeNotes(const juce::String& track) const { return engine.activeNotes(track); }
    void previewNotes(const juce::String&, const std::vector<MidiNote>&);
    void inputNoteOn(const juce::String&, int pitch, double velocity = 0.8);
    void inputNoteOff(const juce::String&, int pitch);
    void setSend(const juce::String& id, int index, double gain);
    const std::vector<std::pair<juce::String, juce::String>>& outputChoices() const;
    void setTrackRouting(const juce::String&, bool input, bool output, int monitor, int channel, juce::String outputTrack = {});
    void armTrack(const juce::String&, bool, bool additive = false);
    bool armed(const juce::String& track) const { return state.armedTracks.contains(track); }
    void toggleRecording();
    bool recording() const { return !recordTakes.empty(); }
    bool recordingTrack(const juce::String& track) const
    { return findId(recordTakes, track) != nullptr; }
    double recordingStart() const { return recordStart; }
    void copyTrack();
    void pasteTrack();
    void duplicateTrack() { copyTrack(); pasteTrack(); }
    PluginHost& plugins() { return engine.plugins; }
    void addPlugin(const juce::String& track, const juce::PluginDescription&);
    void removePlugin(const juce::String& track, const juce::String& id);
    void dropPlugin(const juce::String& description, const juce::String& destination)
    { const auto parts = juce::StringArray::fromTokens(description, ":", "");
      if (parts.size() == 3 && parts[0] == "plugin") movePlugin(parts[1], destination, parts[2]); }
    void movePlugin(const juce::String& source, const juce::String& destination, const juce::String& id);
    void bypassPlugin(const juce::String& track, const juce::String& id, bool bypass) { session.bypassPlugin(track, id, bypass); }
    bool capturePluginStates(bool force = false);
    void setSolo(const juce::String& id, bool solo) { session.setSolo(id, solo); }
    void renameTrack(const juce::String&, const juce::String&);
    void deleteTrack(const juce::String&);
    void controls(const juce::String&, double gain, bool mute);
    void setBpm(double);
    void tapTempo(double hostTimeMs = 0);
    void setOutputPair(int pair);
    void setGroove(double value) { session.setGroove(value); }
    void setScale(int root, int mode, bool enabled) { session.setScale(root, mode, enabled); }
    void setCue(double gain) { session.setCue(gain); }
    void setAutomationLock(bool enabled) { state.lockAutomation = enabled; sendChangeMessage(); }
    void setFollow(bool enabled) { state.follow = enabled; sendChangeMessage(); }
    void setComputerKeyboard(bool enabled) { state.computerKeyboard = enabled; sendChangeMessage(); }
    void setDrawMode(bool enabled) { state.drawMode = enabled; sendChangeMessage(); }
    void setMidiMapMode(bool);
    void setKeyMapMode(bool);
    bool mappedKey(int key, bool down);
    void releaseMappedKeys();
    void setOverdub(bool value) { state.overdub = value; sendChangeMessage(); }
    void setAutomationRecord(bool value) { state.automationRecord = value; sendChangeMessage(); }
    void setPunch(bool in, bool value) { (in ? state.punchIn : state.punchOut) = value; sendChangeMessage(); }
    void learnMidi(const juce::String& target);
    void removeMidiMapping(const juce::String& target) { session.removeMidiMapping(target); }
    void setLinkEnabled(bool enabled) { engine.enableLink(enabled); sendChangeMessage(); }
    bool linkEnabled() const { return engine.linkEnabled(); }
    size_t linkPeers() const { return engine.linkPeers(); }
    void setLinkStartStopSync(bool enabled) { engine.setLinkStartStopSync(enabled); }
    bool linkStartStopSync() const { return engine.linkStartStopSync(); }
    void setLinkQuantum(double quantum) { engine.setLinkQuantum(quantum); }
    double linkQuantum() const { return engine.linkQuantum(); }
    void setMeter(int numerator, int denominator) { session.setMeter(numerator, denominator); }
    void addLocator() { session.addLocator(playing() ? position() : state.timeSelection ? state.timeSelection->start : position()); }
    void jumpLocator(bool next);
    void insertClip(Clip, bool createTrack);
    bool insertClips(const std::vector<Clip>& clips) { return session.insertClips(clips); }
    void changeClip(const Clip&);
    bool changeClips(const std::vector<Clip>&, const std::vector<AutomationEdit>& automation = {});
    void deleteSelection();
    void beginGesture();
    void endGesture(bool cancel);
    void undo();
    void redo();
    void togglePlayback();
    void stop();
    void seek(double);
    void setMaster(float);
    bool playing() const { return engine.playing(); }
    double position() const { return engine.position(); }
    float master() const { return engine.master(); }
    void setMetronome(bool enabled) { engine.setMetronome(enabled); }
    bool metronome() const { return engine.metronome(); }
    juce::AudioDeviceManager& devices() { return engine.devices; }
    juce::String status() const;
    void message(juce::String);

    void newProject();
    void open(juce::File directory);
    void save(bool as = false);
    void saveTo(juce::File directory);
    void importFiles(const juce::StringArray&, juce::String track, double start, bool browserOnly = false);
    void requestQuit(void (*done)());
private:
    std::vector<AutomationPoint> automationClipboard; int automationClipboardParameter = 0;
    std::vector<MidiNote> midiClipboard;
    std::optional<Track> trackClipboard; std::vector<Clip> trackClips; std::vector<Track> trackChildren;
    struct Clipboard
    {
        std::vector<std::pair<Clip, int>> clips;
        std::vector<Track> tracks;
        double start = 0, end = 0;
        int firstTrack = 0, trackCount = 0;
        bool range = false;
    };
    Clipboard captureSelection() const;
    void paste(const Clipboard&, double time, int firstTrack);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void guardUnsaved(std::function<void()>);
    void saveDocument(bool as, std::function<void()> after);
    void saveTo(juce::File, std::function<void()> after);
    void clearPending();
    void beginRecording();
    void finishRecording();
    void recordNote(const juce::String&, const juce::String& source, int pitch, double velocity, bool on, double time);
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void refreshMidiInputs();
    void releaseControllerNotes();
    struct Take { juce::String id; Clip clip; };
    std::vector<Take> recordTakes;
    Take* recordingTake(const juce::String& id)
    { for (auto& take : recordTakes) if (take.id == id) return &take; return nullptr; }
    Asset recordAsset;
    juce::File recordFile;
    double recordStart = 0, lastRecordTime = 0, recordEnd = std::numeric_limits<double>::infinity();
    uint64_t recordConfiguration = 0;
    bool noteLimitReached = false, automationLimitReached = false, recordInputOverflow = false, requestingMicrophone = false;
    std::map<std::pair<juce::String, juce::String>, size_t> recordingNotes;
    struct InputEvent { juce::String source; juce::MidiMessage message; double time; double hostTimeMs = 0; };
    bool mappedMidi(const InputEvent&);
    void recordAutomation(const juce::String&, int parameter, double value);
    std::set<juce::String> heldMappings;
    std::vector<double> tempoTaps;
    juce::CriticalSection midiInputLock;
    std::vector<InputEvent> midiInputEvents;
    std::atomic<bool> midiOverflow {false};
    std::set<juce::String> knownMidiInputs;
    struct ControllerNote { juce::StringArray tracks; int pitch; };
    std::map<juce::String, ControllerNote> controllerNotes;
    std::map<juce::String, juce::StringArray> controllerRoutes;
    double lastMidiScan = 0;
    mutable uint64_t routeRevision = 0;
    mutable std::vector<std::pair<juce::String, juce::String>> routeChoices;
    ProjectSession& session;
    Storage& storage;
    AudioEngine& engine;
    Dialogs* dialogs = nullptr;
    ViewState state;
    std::optional<Clipboard> clipboard;
    std::vector<Loading> imports;
    std::vector<std::function<void()>> completedImports;
    juce::String notice, importedName;
    uint64_t generation = 0, operation = 0;
    double lastPluginCapture = 0;
    bool busySave = false, busyOpen = false;
    int pendingImports = 0;
    JUCE_DECLARE_WEAK_REFERENCEABLE(EditorPresenter)
};
}
