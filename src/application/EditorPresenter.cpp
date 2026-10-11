// SPDX-License-Identifier: AGPL-3.0-only
#include "application/EditorPresenter.h"
#include "domain/TimelineMath.h"
#include "infrastructure/Microphone.h"

namespace opentone
{
EditorPresenter::EditorPresenter(ProjectSession& model, Storage& files, AudioEngine& audio)
    : session(model), storage(files), engine(audio)
{
    session.onEdit = [this](const Snapshot& snapshot, bool structural)
    {
        if (snapshot.generation != generation) { generation = snapshot.generation; engine.replace(snapshot); }
        else engine.submit(snapshot, structural, !recording());
    };
    session.addChangeListener(this); engine.plugins.addChangeListener(this);
    engine.devices.addMidiInputDeviceCallback({}, this); refreshMidiInputs();
    startTimerHz(30);
    notice = "Ready - WAV / AIFF / FLAC / MP3 | 4/4 | original-speed audio";
}
EditorPresenter::~EditorPresenter()
{
    engine.devices.removeMidiInputDeviceCallback({}, this); cancelPendingUpdate();
    stopTimer(); finishRecording(); releaseControllerNotes(); storage.cancel(); session.removeChangeListener(this); engine.plugins.removeChangeListener(this); session.onEdit = nullptr;
}
void EditorPresenter::changeListenerCallback(juce::ChangeBroadcaster*)
{
    for (int i = state.selectedClips.size(); --i >= 0;)
        if (!findId(project().clips, state.selectedClips[i])) state.selectedClips.remove(i);
    for (auto it = state.trackHeights.begin(); it != state.trackHeights.end();)
        if (!findId(project().tracks, it->first)) it = state.trackHeights.erase(it); else ++it;
    if (state.timeSelection)
    {
        auto& tracks = state.timeSelection->tracks;
        for (int i = tracks.size(); --i >= 0;) if (!findId(project().tracks, tracks[i])) tracks.remove(i);
    }
    for (int i = state.armedTracks.size(); --i >= 0;) if (!findId(project().tracks, state.armedTracks[i])) state.armedTracks.remove(i);
    if (!findTrack(project(), state.selectedTrack)) state.selectedTrack.clear();
    if (!findId(project().clips, state.editedMidiClip)) { state.editedMidiClip.clear(); state.pianoOpen = false; }
    sendChangeMessage();
}
void EditorPresenter::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (recording() && recordConfiguration == engine.deviceConfiguration()) lastRecordTime = std::max(lastRecordTime, engine.position());
    if (now - lastMidiScan >= 2000) { lastMidiScan = now; refreshMidiInputs(); }
    if (recording() && ((recordFile != juce::File() && engine.recordingFailed()) || recordConfiguration != engine.deviceConfiguration() || noteLimitReached || recordInputOverflow || midiOverflow.load()))
    {
        const auto warning = noteLimitReached ? "Recording stopped: a MIDI take reached 512 notes." : recordInputOverflow || midiOverflow.load() ? "Recording stopped: MIDI input queue overflow." : "Recording stopped because the audio input or device changed.";
        finishRecording(); engine.stop(); message(warning);
    }
    if (now - lastPluginCapture >= 500 && !session.gesturing()) { lastPluginCapture = now; capturePluginStates(); }
    if (!session.gesturing() && !completedImports.empty())
    { auto ready = std::move(completedImports); completedImports.clear(); for (auto& publish : ready) publish(); }
}
void EditorPresenter::selectClip(juce::String id, bool additive)
{
    auto ids = additive ? state.selectedClips : juce::StringArray();
    if (additive && ids.contains(id)) ids.removeString(id);
    else if (id.isNotEmpty()) ids.add(id);
    selectClips(std::move(ids));
}
void EditorPresenter::selectClips(juce::StringArray ids)
{
    ids.removeDuplicates(false);
    for (int i = ids.size(); --i >= 0;) if (!findId(project().clips, ids[i])) ids.remove(i);
    state.selectedClips = std::move(ids); state.timeSelection.reset();
    if (!state.selectedClips.isEmpty()) state.selectedTrack = findId(project().clips, state.selectedClips[0])->track;
    sendChangeMessage();
}
void EditorPresenter::selectAllClips()
{ juce::StringArray ids; for (const auto& clip : project().clips) ids.add(clip.id); selectClips(std::move(ids)); }
void EditorPresenter::selectTime(TimeSelection selection)
{
    if (!std::isfinite(selection.start) || !std::isfinite(selection.end) || selection.start < 0 || selection.end < selection.start) return;
    juce::StringArray tracks;
    for (const auto& track : project().tracks) if (selection.tracks.contains(track.id)) tracks.add(track.id);
    selection.tracks = std::move(tracks); state.selectedClips.clear();
    if (!selection.tracks.isEmpty()) state.selectedTrack = selection.tracks[0];
    state.timeSelection = std::move(selection); sendChangeMessage();
}
std::vector<Clip> EditorPresenter::splitTimeSelection()
{ return state.timeSelection ? session.splitTimeSelection(*state.timeSelection) : std::vector<Clip>(); }
EditorPresenter::Clipboard EditorPresenter::captureSelection() const
{
    Clipboard copy;
    copy.range = state.timeSelection.has_value();
    copy.start = copy.range ? state.timeSelection->start : std::numeric_limits<double>::max();
    copy.end = copy.range ? state.timeSelection->end : 0;
    copy.firstTrack = static_cast<int>(project().tracks.size()); int lastTrack = -1;
    for (size_t row = 0; row < project().tracks.size(); ++row)
    {
        const auto& track = project().tracks[row];
        bool selectedTrack = copy.range && state.timeSelection->tracks.contains(track.id);
        for (auto clip : project().clips)
        {
            if (clip.track != track.id) continue;
            if (copy.range)
            { const auto part = clipInTimeSelection(clip, *state.timeSelection); if (!part) continue; clip = *part; }
            else if (!state.selectedClips.contains(clip.id)) continue;
            selectedTrack = true; copy.clips.emplace_back(clip, static_cast<int>(row));
            if (!copy.range) { copy.start = std::min(copy.start, clip.start); copy.end = std::max(copy.end, clip.start + clip.duration); }
        }
        if (selectedTrack) { copy.firstTrack = std::min(copy.firstTrack, static_cast<int>(row)); lastTrack = static_cast<int>(row); }
    }
    copy.trackCount = lastTrack - copy.firstTrack + 1;
    if (copy.trackCount > 0) copy.tracks.assign(project().tracks.begin() + copy.firstTrack, project().tracks.begin() + lastTrack + 1);
    for (auto& item : copy.clips) { item.first.start -= copy.start; item.second -= copy.firstTrack; }
    return copy;
}
void EditorPresenter::copySelection()
// shortcut: clipboard references current-project assets, copy media when cross-project paste is needed.
{ if (!capturePluginStates(true)) return; auto copy = captureSelection(); if (!copy.clips.empty()) clipboard = std::move(copy); }
void EditorPresenter::paste(const Clipboard& copy, double time, int firstTrack)
{
    if (copy.clips.empty() || !std::isfinite(time) || time < 0 || firstTrack < 0 || session.gesturing()) return;
    auto tracks = project().tracks; std::vector<Track> added;
    while (static_cast<int>(tracks.size()) < firstTrack + copy.trackCount)
    { const auto index = static_cast<int>(tracks.size()) - firstTrack;
      auto track = copy.tracks[static_cast<size_t>(index)]; track.id = newId(); for (auto& plugin : track.plugins) plugin.id = newId(); tracks.push_back(track); added.push_back(track); }
    std::vector<Clip> clips; juce::StringArray ids, selectedTracks;
    for (const auto& item : copy.clips)
    {
        auto clip = item.first; clip.id = newId(); clip.start += time;
        clip.track = tracks[static_cast<size_t>(firstTrack + item.second)].id;
        ids.add(clip.id); clips.push_back(std::move(clip));
    }
    if (!session.insertClips(clips, added)) { message("Could not paste selection; its audio may no longer be in this project."); return; }
    if (copy.range)
    {
        for (int row = firstTrack; row < firstTrack + copy.trackCount; ++row) selectedTracks.add(tracks[static_cast<size_t>(row)].id);
        selectTime({ time, time + copy.end - copy.start, selectedTracks });
    }
    else selectClips(std::move(ids));
}
void EditorPresenter::duplicateSelection()
{ const auto copy = captureSelection(); paste(copy, copy.end, copy.firstTrack); }
void EditorPresenter::pasteSelection()
{
    if (!clipboard) return;
    const auto target = captureSelection();
    if (state.timeSelection || !state.selectedClips.isEmpty())
        paste(*clipboard, target.start, state.timeSelection && state.timeSelection->tracks.isEmpty() ? 0 : target.firstTrack);
    else paste(*clipboard, engine.position(), 0);
}
void EditorPresenter::setZoom(double value) { if (std::isfinite(value)) state.pixelsPerSecond = juce::jlimit(8.0, 40000.0, value); }
void EditorPresenter::setSnap(bool value) { state.snap = value; }
void EditorPresenter::setScroll(int x, int y) { state.scrollX = std::max(0, x); state.scrollY = std::max(0, y); }
void EditorPresenter::setTrackHeight(const juce::String& id, int height)
{ if (findId(project().tracks, id)) { state.trackHeights[id] = juce::jlimit(72, 400, height); sendChangeMessage(); } }
juce::String EditorPresenter::addTrack() { auto id = session.addTrack(); selectTrack(id); return id; }
juce::String EditorPresenter::addMidiTrack() { auto id = session.addTrack("MIDI", true); selectTrack(id); return id; }
void EditorPresenter::selectTrack(const juce::String& id)
{ if (findTrack(project(), id)) { state.selectedTrack = id; sendChangeMessage(); } }
void EditorPresenter::openMidiClip(const juce::String& id)
{
    const auto* clip = findId(project().clips, id); if (!clip) return;
    const auto* track = findId(project().tracks, clip->track); if (!track || !track->midi) return;
    selectClip(id); state.editedMidiClip = id; state.pianoOpen = true; sendChangeMessage();
}
void EditorPresenter::setPianoPanel(bool open, int height)
{ state.pianoOpen = open; state.pianoHeight = juce::jlimit(1, 10000, height); sendChangeMessage(); }
void EditorPresenter::copyTrack()
{
    if (!capturePluginStates(true)) return;
    const auto* track = findId(project().tracks, state.selectedTrack); if (!track) return;
    trackClipboard = *track; trackClips.clear();
    for (const auto& clip : project().clips) if (clip.track == track->id) trackClips.push_back(clip);
}
void EditorPresenter::pasteTrack()
{
    if (!trackClipboard) return;
    auto track = *trackClipboard; track.id = newId(); track.name += " Copy"; for (auto& plugin : track.plugins) plugin.id = newId();
    auto clips = trackClips;
    for (auto& clip : clips) { clip.id = newId(); clip.track = track.id; }
    if (session.insertClips(clips, {track})) selectTrack(track.id);
    else message("Could not paste track; its audio is no longer in this project.");
}
void EditorPresenter::previewNotes(const juce::String& id, const std::vector<MidiNote>& notes)
{
    const auto* track = findId(project().tracks, id); if (!track) return;
    if (!track->plugins.empty() && track->plugins.front().instrument)
    { engine.plugins.release(id, -1, true); std::bitset<128> sounded;
      for (const auto& note : notes) if (!sounded.test(static_cast<size_t>(note.pitch)))
      { sounded.set(static_cast<size_t>(note.pitch)); engine.plugins.preview(id, note.pitch, note.velocity, false, false); }
      return;
    }
    if (!track->sampler || track->samplerBypass) return;
    const auto* asset = findId(project().assets, track->sample); if (!asset) return;
    engine.releaseEditPreviews(id);
    std::bitset<128> sounded;
    for (const auto& note : notes) if (!sounded.test(static_cast<size_t>(note.pitch)))
    { sounded.set(static_cast<size_t>(note.pitch)); engine.previewNote(*track, *asset, mediaDirectory().getChildFile(asset->path), note.pitch, note.velocity, false, false); }
}
bool EditorPresenter::capturePluginStates(bool force)
{
    if (session.gesturing()) return false;
    const auto states = engine.plugins.captureStates(force);
    if (!states.empty() && !session.pluginStates(states)) { message("Could not store plug-in settings; a plug-in state exceeds 16 MB."); return false; }
    return true;
}
void EditorPresenter::addDevice(const juce::String& identifier, juce::String track)
{
    const bool createTrack = track.isEmpty(); if (createTrack) track = newId();
    bool added = false;
    if (identifier == "builtin:sampler") added = session.addSampler(track, createTrack);
    else
    {
        for (const auto& description : engine.plugins.availablePlugins()) if (description.createIdentifierString() == identifier)
        {
            auto xml = description.createXml();
            if (xml) added = session.addPlugin(track, { newId(), description.name, xml->toString(), {}, description.isInstrument, false }, createTrack);
            break;
        }
    }
    if (added) selectTrack(track); else message("Choose an existing track; instruments need a MIDI track without another instrument.");
}
void EditorPresenter::addPlugin(const juce::String& track, const juce::PluginDescription& description)
{
    auto xml = description.createXml(); if (!xml) return;
    Plugin plugin { newId(), description.name, xml->toString(), {}, description.isInstrument, false };
    if (!session.addPlugin(track, std::move(plugin))) message("Instruments require a MIDI track without another instrument.");
    else selectTrack(track);
}
void EditorPresenter::removePlugin(const juce::String& track, const juce::String& id)
{ const auto destination = track, plugin = id; if (!capturePluginStates(true)) return; session.removePlugin(destination, plugin); }
void EditorPresenter::movePlugin(const juce::String& source, const juce::String& destination, const juce::String& id)
{ if (source == destination) return; if (!capturePluginStates(true)) return;
  if (session.movePlugin(source, destination, id)) selectTrack(destination);
  else message("Move an instrument to a MIDI track without another instrument."); }
bool EditorPresenter::previewNote(const juce::String& id, int note, double velocity, bool held)
{
    const auto* track = findId(project().tracks, id);
    if (track && !track->plugins.empty() && track->plugins.front().instrument) return engine.plugins.preview(id, note, velocity, held, true);
    if (!track || !track->midi || !track->sampler || track->samplerBypass) return false;
    const auto* asset = findId(project().assets, track->sample); if (!asset || asset->missing) return false;
    return engine.previewNote(*track, *asset, mediaDirectory().getChildFile(asset->path), note, velocity, held);
}
void EditorPresenter::removeSampler(const juce::String& track)
{ engine.releasePreview(track, -1); session.removeSampler(track); }
void EditorPresenter::moveSampler(const juce::String& source, const juce::String& destination)
{
    const auto origin = source, target = destination;
    if (origin == target) return;
    if (!session.moveSampler(origin, target)) { message("Move the sampler to a MIDI track without an instrument."); return; }
    engine.releasePreview(origin, -1); selectTrack(target);
}
void EditorPresenter::loadSampler(const juce::String& track, const juce::String& asset, double offset, double duration)
{
    const auto destination = track;
    if (!session.loadSampler(destination, asset, offset, duration)) message("Could not load sampler source.");
    else selectTrack(destination);
}
void EditorPresenter::renameTrack(const juce::String& id, const juce::String& name) { session.renameTrack(id, name); }
void EditorPresenter::deleteTrack(const juce::String& id) { const auto track = id; if (recording()) stop(); if (capturePluginStates(true)) session.deleteTrack(track); }
void EditorPresenter::controls(const juce::String& id, double gain, bool mute) { session.controls(id, gain, mute); }
void EditorPresenter::setBpm(double bpm) { session.setBpm(bpm); }
void EditorPresenter::insertClip(Clip clip, bool create) { session.insertClip(std::move(clip), create); }
void EditorPresenter::changeClip(const Clip& clip) { session.changeClip(clip); }
void EditorPresenter::changeClips(const std::vector<Clip>& clips) { session.changeClips(clips); }
void EditorPresenter::deleteSelection()
{
    if (state.timeSelection)
    {
        session.beginGesture(); juce::StringArray ids;
        for (const auto& clip : splitTimeSelection()) ids.add(clip.id);
        session.deleteClips(ids); session.endGesture(false);
    }
    else { selectClips(state.selectedClips); session.deleteClips(state.selectedClips); }
    selectClips({});
}
void EditorPresenter::beginGesture() { session.beginGesture(); }
void EditorPresenter::endGesture(bool cancel) { session.endGesture(cancel); }
void EditorPresenter::undo() { if (recording()) stop(); if (capturePluginStates(true)) session.undoEdit(); }
void EditorPresenter::redo() { if (recording()) stop(); if (capturePluginStates(true)) session.redoEdit(); }
void EditorPresenter::armTrack(const juce::String& id, bool enabled)
{
    if (!findId(project().tracks, id)) return;
    if (recording()) { message("Stop recording before changing armed tracks."); return; }
    if (enabled) state.armedTracks.addIfNotAlreadyThere(id); else state.armedTracks.removeString(id);
    if (enabled && !findId(project().tracks, id)->midi)
    {
        juce::WeakReference<EditorPresenter> safe(this);
        requestMicrophonePermission([safe, id](bool granted)
        {
            if (!safe || !safe->armed(id) || safe->recording()) return;
            if (!granted) { safe->message("Microphone access is denied. Enable Opentone in System Settings > Privacy & Security > Microphone."); return; }
            const auto result = safe->engine.enableMicrophone();
            if (result.failed()) safe->message(result.getErrorMessage());
            else safe->message("Microphone input ready");
        });
    }
    sendChangeMessage();
}
void EditorPresenter::toggleRecording()
{
    if (recording()) { finishRecording(); return; }
    if (requestingMicrophone) { message("Waiting for microphone permission"); return; }
    if (busy() || session.gesturing()) { message("Finish the current edit or file operation before recording."); return; }
    if (state.armedTracks.isEmpty() && findId(project().tracks, state.selectedTrack)) armTrack(state.selectedTrack, true);
    if (state.armedTracks.isEmpty()) { message("Arm an audio or MIDI track to record."); return; }
    const bool audio = std::any_of(project().tracks.begin(), project().tracks.end(), [this](const auto& track) { return armed(track.id) && !track.midi; });
    if (!audio) { beginRecording(); return; }
    requestingMicrophone = true;
    message("Waiting for microphone permission");
    juce::WeakReference<EditorPresenter> safe(this); const auto projectGeneration = session.snapshot().generation;
    requestMicrophonePermission([safe, projectGeneration](bool granted)
    {
        if (!safe || !safe->requestingMicrophone) return;
        safe->requestingMicrophone = false;
        if (safe->session.snapshot().generation != projectGeneration) return;
        if (!granted) { safe->message("Microphone access is denied. Enable Opentone in System Settings > Privacy & Security > Microphone."); return; }
        safe->beginRecording();
    });
}
void EditorPresenter::beginRecording()
{
    if (recording() || busy() || session.gesturing() || state.armedTracks.isEmpty()) return;
    bool audio = false;
    for (const auto& id : state.armedTracks) if (const auto* track = findId(project().tracks, id)) audio = audio || !track->midi;
    engine.stop();
    if (!engine.devices.getCurrentAudioDevice()) { message("Choose an audio device in Settings > Audio before recording."); return; }
    if (audio) { const auto result = engine.enableMicrophone(); if (result.failed()) { message(result.getErrorMessage()); return; } }
    const auto selection = captureSelection();
    recordStart = state.timeSelection ? state.timeSelection->start : selection.clips.empty() ? 0 : selection.start;
    recordAsset = {}; recordFile = juce::File();
    if (audio)
    {
        recordAsset = { newId(), {}, engine.rate(), 1, 0, false };
        recordAsset.path = "Audio/" + recordAsset.id + "-Recording.wav";
        recordFile = mediaDirectory().getChildFile(recordAsset.path);
        const auto result = engine.startAudioRecording(recordFile, recordStart);
        if (result.failed()) { message(result.getErrorMessage()); return; }
    }
    recordingNotes.clear(); noteLimitReached = recordInputOverflow = false; midiOverflow.store(false); lastRecordTime = recordStart;
    { const juce::ScopedLock lock(midiInputLock); midiInputEvents.clear(); }
    for (const auto& id : state.armedTracks) if (const auto* track = findId(project().tracks, id))
        recordTakes.push_back({ id, { newId(), id, track->midi ? juce::String() : recordAsset.id, recordStart, 0, 0 } });
    recordConfiguration = engine.deviceConfiguration();
    engine.submit(session.snapshot(), true, false);
    engine.seek(recordStart); engine.play(); message("Recording");
}
void EditorPresenter::recordNote(const juce::String& track, const juce::String& source, int pitch, double velocity, bool on, double time)
{
    if (!recording() || pitch < 0 || pitch > 127 || !std::isfinite(velocity)) return;
    auto* take = recordingTake(track); if (!take || take->clip.asset.isNotEmpty()) return;
    const auto position = std::max(0.0, time - recordStart); lastRecordTime = std::max(lastRecordTime, time);
    const auto key = std::make_pair(track, source);
    auto pending = recordingNotes.find(key);
    if (pending != recordingNotes.end())
    {
        auto& note = take->clip.notes[pending->second]; note.duration = std::max(1 / engine.rate(), position - note.start);
        recordingNotes.erase(pending);
    }
    if (on)
    {
        // shortcut: takes stop at 512 notes; split longer takes when multi-clip recording is added.
        if (take->clip.notes.size() >= 512) { noteLimitReached = true; return; }
        recordingNotes[key] = take->clip.notes.size();
        take->clip.notes.push_back({ pitch, position, 1 / engine.rate(), juce::jlimit(0.0, 1.0, velocity) });
    }
}
void EditorPresenter::inputNoteOn(const juce::String& track, int pitch, double velocity)
{ previewNote(track, pitch, velocity, true); recordNote(track, "keyboard:" + juce::String(pitch), pitch, velocity, true, engine.position()); }
void EditorPresenter::inputNoteOff(const juce::String& track, int pitch)
{ releasePreview(track, pitch); recordNote(track, "keyboard:" + juce::String(pitch), pitch, 0, false, engine.position()); }
void EditorPresenter::refreshMidiInputs()
{
    const auto available = juce::MidiInput::getAvailableDevices();
    for (auto it = knownMidiInputs.begin(); it != knownMidiInputs.end();)
    {
        if (std::any_of(available.begin(), available.end(), [&](const auto& device) { return device.identifier == *it; })) { ++it; continue; }
        for (auto held = controllerNotes.begin(); held != controllerNotes.end();)
        {
            if (!held->first.startsWith(*it + ":")) { ++held; continue; }
            for (const auto& track : held->second.tracks)
            { releasePreview(track, held->second.pitch); recordNote(track, held->first, held->second.pitch, 0, false, engine.position()); }
            held = controllerNotes.erase(held);
        }
        it = knownMidiInputs.erase(it);
    }
    for (const auto& input : available) if (knownMidiInputs.insert(input.identifier).second)
        engine.devices.setMidiInputDeviceEnabled(input.identifier, true);
}
void EditorPresenter::handleIncomingMidiMessage(juce::MidiInput* input, const juce::MidiMessage& value)
{
    if (!value.isNoteOnOrOff() && !value.isAllNotesOff() && !value.isAllSoundOff()) return;
    const juce::ScopedLock lock(midiInputLock);
    if (midiInputEvents.size() >= 4096) { midiOverflow.store(true); triggerAsyncUpdate(); return; }
    midiInputEvents.push_back({ (input ? input->getIdentifier() : juce::String("MIDI")) + ":" + juce::String(value.getChannel()), value, engine.position() });
    triggerAsyncUpdate();
}
void EditorPresenter::releaseControllerNotes()
{
    for (const auto& held : controllerNotes) for (const auto& track : held.second.tracks)
        releasePreview(track, held.second.pitch);
    controllerNotes.clear();
}
void EditorPresenter::handleAsyncUpdate()
{
    std::vector<InputEvent> events;
    { const juce::ScopedLock lock(midiInputLock); events.swap(midiInputEvents); }
    if (midiOverflow.exchange(false)) { releaseControllerNotes(); if (recording()) recordInputOverflow = true; }
    for (const auto& event : events)
    {
        if (event.message.isAllNotesOff() || event.message.isAllSoundOff())
        {
            for (const auto& held : controllerNotes) for (const auto& track : held.second.tracks)
                recordNote(track, held.first, held.second.pitch, 0, false, event.time);
            releaseControllerNotes(); continue;
        }
        const auto pitch = event.message.getNoteNumber();
        const auto key = event.source + ":" + juce::String(pitch);
        auto held = controllerNotes.find(key);
        if (held != controllerNotes.end())
        {
            for (const auto& track : held->second.tracks)
            { releasePreview(track, pitch); recordNote(track, key, pitch, 0, false, event.time); }
            controllerNotes.erase(held);
        }
        if (!event.message.isNoteOn()) continue;
        ControllerNote next {{}, pitch};
        for (const auto& track : project().tracks) if (track.midi && armed(track.id)) next.tracks.add(track.id);
        if (next.tracks.isEmpty()) if (const auto* track = findId(project().tracks, state.selectedTrack)) if (track->midi) next.tracks.add(track->id);
        for (const auto& track : next.tracks)
        { previewNote(track, pitch, event.message.getFloatVelocity(), true); recordNote(track, key, pitch, event.message.getFloatVelocity(), true, event.time); }
        controllerNotes[key] = std::move(next);
    }
}
void EditorPresenter::finishRecording()
{
    if (!recording()) return;
    handleAsyncUpdate();
    const auto end = std::max(engine.position(), lastRecordTime);
    for (const auto& pending : recordingNotes) if (auto* take = recordingTake(pending.first.first))
    { auto& note = take->clip.notes[pending.second]; note.duration = std::max(1 / engine.rate(), end - recordStart - note.start); }
    recordingNotes.clear();
    auto takes = std::move(recordTakes); recordTakes.clear();
    juce::String warning;
    if (session.gesturing()) session.endGesture(false);
    session.beginGesture();
    if (recordFile != juce::File())
    {
        const auto result = engine.finishAudioRecording(); if (result.failed()) warning = result.getErrorMessage();
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(recordFile));
        if (reader && reader->lengthInSamples > 0)
        {
            if (reader->lengthInSamples != engine.recordedFrames()) warning = "The WAV contains fewer frames than captured; check disk space. Available audio is preserved.";
            recordAsset.sampleRate = reader->sampleRate; recordAsset.length = reader->lengthInSamples; recordAsset.channels = static_cast<int>(reader->numChannels);
            if (!session.importAsset(recordAsset)) warning = "Could not add recorded audio. The WAV is preserved at " + recordFile.getFullPathName();
        }
        else if (reader) recordFile.deleteFile();
        else warning = "Could not read recorded audio. The file is preserved at " + recordFile.getFullPathName();
    }
    std::vector<Clip> clips;
    for (auto& take : takes)
    {
        const auto* track = findId(project().tracks, take.id); if (!track) continue;
        if (track->midi)
        {
            if (take.clip.notes.empty()) continue;
            take.clip.duration = std::max(0.001, end - recordStart);
            for (const auto& note : take.clip.notes) take.clip.duration = std::max(take.clip.duration, note.start + note.duration);
            take.clip.loopLength = take.clip.duration;
        }
        else
        {
            const auto* asset = findId(project().assets, recordAsset.id); if (!asset) continue;
            take.clip.start = engine.recordedStart();
            take.clip.duration = static_cast<double>(asset->length) / asset->sampleRate;
        }
        clips.push_back(std::move(take.clip));
    }
    const bool placed = clips.empty() || session.insertClips(clips);
    session.endGesture(false); recordFile = juce::File(); engine.submit(session.snapshot());
    if (!placed) warning = "Could not place the recorded clips. Recorded audio files are preserved in the project Audio folder.";
    if (placed && !clips.empty()) { juce::StringArray ids; for (const auto& clip : clips) ids.add(clip.id); selectClips(ids); }
    message(warning.isNotEmpty() ? warning : clips.empty() ? "Recording finished; no notes or audio captured" : "Recording finished");
}
void EditorPresenter::loopSelection()
{
    const auto selection = captureSelection();
    const auto start = state.timeSelection ? state.timeSelection->start : selection.start;
    const auto end = state.timeSelection ? state.timeSelection->end : selection.end;
    if (end - start >= 0.001) setLoop(start, end, !project().loopEnabled || std::abs(start - project().loopStart) > 1e-9 || std::abs(end - project().loopEnd) > 1e-9); else toggleLoop();
}
void EditorPresenter::togglePlayback()
{
    if (recording()) { stop(); return; }
    if (engine.playing()) { engine.stop(); return; }
    const auto selection = captureSelection();
    auto start = state.timeSelection ? state.timeSelection->start : (selection.clips.empty() ? 0 : selection.start);
    if (project().loopEnabled && start >= project().loopEnd) start = project().loopStart;
    engine.seek(start);
    engine.play();
}
void EditorPresenter::stop() { requestingMicrophone = false; engine.stop(); finishRecording(); }
void EditorPresenter::seek(double seconds) { finishRecording(); engine.seek(seconds); }
void EditorPresenter::setMaster(float gain) { session.controls("master", gain, false); }
juce::String EditorPresenter::status() const
{
    if (engine.error().isNotEmpty()) return engine.error();
    return juce::String(session.dirty() ? "Unsaved" : "Saved")
        + (recording() ? " | Recording" : "")
        + (engine.appliedRevision() < engine.requestedRevision() ? " | Preparing playback changes" : "")
        + (engine.clipped() ? " | CLIPPING" : "") + " | Underruns: " + juce::String(engine.underrunCount()) + " | " + notice;
}
void EditorPresenter::message(juce::String text) { notice = std::move(text); sendChangeMessage(); }
void EditorPresenter::guardUnsaved(std::function<void()> action)
{
    if (recording()) stop();
    if (session.gesturing()) session.endGesture(false);
    if (!capturePluginStates(true)) return;
    if (!session.dirty()) { action(); return; }
    if (!dialogs) return;
    juce::WeakReference<EditorPresenter> safe(this);
    dialogs->askSaveChanges([safe, action](SaveChoice choice)
    {
        if (!safe) return;
        if (choice == SaveChoice::save) safe->saveDocument(false, action);
        else if (choice == SaveChoice::discard) action();
    });
}
void EditorPresenter::clearPending()
{
    requestingMicrophone = false;
    releaseControllerNotes();
    storage.cancel(); ++operation; imports.clear(); busySave = busyOpen = false; pendingImports = 0;
    state = {}; clipboard.reset();
}
void EditorPresenter::newProject()
{
    juce::WeakReference<EditorPresenter> safe(this);
    guardUnsaved([safe]
    {
        if (!safe) return;
        safe->clearPending();
        Project project;
        project.tracks.push_back({ newId(), "Audio" });
        Track midi { newId(), "MIDI" }; midi.midi = true; project.tracks.push_back(std::move(midi));
        safe->session.replace(std::move(project), safe->storage.workspace(), true);
        safe->selectTrack(safe->project().tracks.front().id);
        safe->message("New project");
    });
}
void EditorPresenter::requestQuit(void (*done)()) { guardUnsaved(done); }
void EditorPresenter::open(juce::File directory)
{
    const auto ticket = ++operation, projectGeneration = session.snapshot().generation;
    message("Opening project..."); busyOpen = true;
    juce::WeakReference<EditorPresenter> safe(this);
    storage.open(directory, [safe, directory, ticket, projectGeneration](LoadResult result)
    {
        if (!safe) return;
        if (safe->operation == ticket) safe->busyOpen = false;
        if (safe->operation != ticket || safe->session.snapshot().generation != projectGeneration) return;
        if (result.result.failed()) { safe->message(result.result.getErrorMessage()); return; }
        // Re-check edits made while the background load was running.
        safe->guardUnsaved([safe, directory, result]
        {
            if (!safe) return;
            safe->clearPending(); safe->session.replace(result.project, directory, true);
            safe->message(result.warning.isEmpty() ? "Project opened" : result.warning);
        });
    });
}
void EditorPresenter::importFiles(const juce::StringArray& files, juce::String track, double start, bool browserOnly)
{
    const auto snapshot = session.snapshot();
    for (const auto& filename : files)
    {
        const juce::File source(filename); const auto id = newId();
        auto destinationTrack = files.size() == 1 ? track : juce::String();
        const bool create = destinationTrack.isEmpty(); if (create) destinationTrack = newId();
        if (!browserOnly) imports.push_back({ id, destinationTrack, source.getFileName(), start });
        message("Importing " + source.getFileName() + "..."); ++pendingImports;
        juce::WeakReference<EditorPresenter> safe(this);
        storage.importFile(source, snapshot.directory, [safe, snapshot, id, destinationTrack, create, start, browserOnly, source](ImportResult result)
        {
            if (!safe) return;
            auto publish = [safe, snapshot, id, destinationTrack, create, start, browserOnly, source, result]
            {
                if (!safe) return;
                if (safe->session.snapshot().generation != snapshot.generation)
                { if (result.result.wasOk()) snapshot.directory.getChildFile(result.asset.path).deleteFile(); return; }
                --safe->pendingImports;
                auto& loading = safe->imports;
                loading.erase(std::remove_if(loading.begin(), loading.end(), [&](const auto& item) { return item.operation == id; }), loading.end());
                if (result.result.failed()) safe->message(result.result.getErrorMessage());
                else
                {
                    std::optional<Clip> clip;
                    const auto* target = findId(safe->project().tracks, destinationTrack);
                    const bool sampler = !browserOnly && target && target->midi;
                    const auto duration = static_cast<double>(result.asset.length) / result.asset.sampleRate;
                    if (!browserOnly && !sampler) clip = Clip { newId(), destinationTrack, result.asset.id, start, 0, duration };
                    const bool imported = sampler ? safe->session.loadSampler(destinationTrack, result.asset.id, 0, duration, result.asset)
                                                  : safe->session.importAsset(result.asset, clip, !browserOnly && create);
                    if (!imported)
                    { snapshot.directory.getChildFile(result.asset.path).deleteFile(); safe->message("Import destination track no longer exists."); }
                    else { if (sampler) safe->selectTrack(destinationTrack); safe->message("Imported " + source.getFileName()); }
                }
                safe->sendChangeMessage();
            };
            // Completed imports are separate from the current drag's undo action.
            if (safe->session.gesturing()) safe->completedImports.push_back(std::move(publish));
            else publish();
        });
    }
}
void EditorPresenter::save(bool as) { saveDocument(as, {}); }
void EditorPresenter::saveTo(juce::File directory) { saveTo(std::move(directory), {}); }
void EditorPresenter::saveDocument(bool as, std::function<void()> after)
{
    if (recording()) stop();
    if (busySave) { message("A save is already in progress."); return; }
    if (!as && session.location() != storage.workspace()) { saveTo(session.location(), std::move(after)); return; }
    if (!dialogs) return;
    juce::WeakReference<EditorPresenter> safe(this);
    dialogs->chooseSaveDirectory([safe, after](juce::File directory)
    {
        if (!safe || directory == juce::File()) return;
        if (directory.getChildFile("project.opentone").exists() && directory != safe->session.location())
        {
            if (!safe->dialogs) return;
            safe->dialogs->confirmOverwrite(directory, [safe, directory, after](bool confirmed)
            { if (safe && confirmed) safe->saveTo(directory, after); });
        }
        else safe->saveTo(directory, after);
    });
}
void EditorPresenter::saveTo(juce::File directory, std::function<void()> after)
{
    if (busySave) { message("A save is already in progress."); return; }
    if (!capturePluginStates(true)) return; busySave = true; message("Saving project..."); const auto snapshot = session.snapshot();
    juce::WeakReference<EditorPresenter> safe(this);
    storage.save(snapshot, directory, [safe, snapshot, directory, after](juce::Result result)
    {
        if (!safe) return; safe->busySave = false;
        if (result.failed()) { safe->message(result.getErrorMessage()); return; }
        if (safe->session.snapshot().generation != snapshot.generation) return;
        safe->session.saved(snapshot, directory); safe->message("Project saved");
        if (after) { if (safe->session.dirty()) safe->guardUnsaved(after); else after(); }
    });
}
}
