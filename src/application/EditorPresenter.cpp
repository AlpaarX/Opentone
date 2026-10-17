// SPDX-License-Identifier: AGPL-3.0-only
#include "application/EditorPresenter.h"
#include "infrastructure/BuiltinPlugins.h"
#include "domain/TimelineMath.h"
#include "infrastructure/Microphone.h"

namespace opentone
{
namespace
{
juce::String preserveMidiTake(const Clip& clip, double bpm)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Opentone/Recovered MIDI");
    if (folder.createDirectory().failed()) return "Could not preserve the recorded MIDI take.";
    const auto file = folder.getChildFile(clip.id + ".mid"); auto stream = file.createOutputStream();
    if (!stream) return "Could not preserve the recorded MIDI take.";
    juce::MidiMessageSequence sequence; auto tempo = juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000 / bpm)); tempo.setTimeStamp(0); sequence.addEvent(tempo);
    for (const auto& note : clip.notes)
    {
        auto on = juce::MidiMessage::noteOn(note.channel, note.pitch, static_cast<float>(note.velocity)); on.setTimeStamp(note.start * bpm / 60 * 960); sequence.addEvent(on);
        auto off = juce::MidiMessage::noteOff(note.channel, note.pitch, static_cast<float>(note.releaseVelocity)); off.setTimeStamp((note.start + note.duration) * bpm / 60 * 960); sequence.addEvent(off);
    }
    sequence.updateMatchedPairs(); juce::MidiFile midi; midi.setTicksPerQuarterNote(960); midi.addTrack(sequence);
    if (!midi.writeTo(*stream)) return "Could not preserve the recorded MIDI take.";
    return "Recorded MIDI was preserved at " + file.getFullPathName();
}
}
EditorPresenter::EditorPresenter(ProjectSession& model, Storage& files, AudioEngine& audio)
    : session(model), storage(files), engine(audio)
{
    session.onEdit = [this](const Snapshot& snapshot, bool structural)
    {
        if (snapshot.generation != generation) { generation = snapshot.generation; engine.replace(snapshot); }
        else engine.submit(snapshot, structural, !recording());
    };
    libraryPlayer.setSource(&libraryTransport); engine.devices.addAudioCallback(&libraryPlayer);
    session.addChangeListener(this); engine.plugins.addChangeListener(this);
    engine.devices.addMidiInputDeviceCallback({}, this); refreshMidiInputs();
    startTimerHz(30);
    notice = "Ready - WAV / AIFF / FLAC / MP3 | 4/4 | original-speed audio";
}
EditorPresenter::~EditorPresenter()
{
    stopLibraryPreview(); engine.devices.removeAudioCallback(&libraryPlayer);
    libraryPreviewPool.removeAllJobs(true, -1); libraryPlayer.setSource(nullptr); libraryReadAhead.stopThread(-1);
    engine.devices.removeMidiInputDeviceCallback({}, this); cancelPendingUpdate();
    stopTimer(); finishRecording(); releaseControllerNotes(); storage.cancel(); session.removeChangeListener(this); engine.plugins.removeChangeListener(this); session.onEdit = nullptr;
}
void EditorPresenter::changeListenerCallback(juce::ChangeBroadcaster*)
{
    libraryTransport.setGain(static_cast<float>(project().cueGain));
    for (int i = state.selectedClips.size(); --i >= 0;)
        if (!findId(project().clips, state.selectedClips[i])) state.selectedClips.remove(i);
    for (int i = state.selectedTracks.size(); --i >= 0;) if (!findTrack(project(), state.selectedTracks[i])) state.selectedTracks.remove(i);
    if (state.timeSelection)
    {
        auto& tracks = state.timeSelection->tracks;
        for (int i = tracks.size(); --i >= 0;) if (!findId(project().tracks, tracks[i])) tracks.remove(i);
    }
    state.armedTracks.clear();
    for (const auto& track : project().tracks) if (track.armed) state.armedTracks.add(track.id);
    engine.plugins.setArmed(state.armedTracks);
    if (!findTrack(project(), state.selectedTrack)) state.selectedTrack.clear();
    if (!findId(project().clips, state.editedMidiClip)) { state.editedMidiClip.clear(); state.pianoOpen = false; }
    sendChangeMessage();
}
void EditorPresenter::timerCallback()
{
    if (engine.linkEnabled())
    {
        const auto bpm = engine.linkTempo();
        if (std::abs(bpm - project().bpm) > 0.001 && !gesturing()) session.setBpm(bpm);
        const auto remote = engine.linkRemoteTransport();
        if (remote > 0) { engine.seek(state.timeSelection ? state.timeSelection->start : 0); engine.play(false); }
        else if (remote < 0) { if (recording()) finishRecording(); engine.stop(false); }
    }
    if (recording() && position() >= recordEnd) finishRecording();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (recording() && recordConfiguration == engine.deviceConfiguration()) lastRecordTime = std::max(lastRecordTime, engine.position());
    if (now - lastMidiScan >= 2000) { lastMidiScan = now; refreshMidiInputs(); }
    if (recording() && ((recordFile != juce::File() && engine.recordingFailed()) || recordConfiguration != engine.deviceConfiguration() || noteLimitReached || recordInputOverflow || midiOverflow.load()))
    {
        const auto warning = noteLimitReached ? "Recording stopped: a MIDI take reached 4096 notes." : recordInputOverflow || midiOverflow.load() ? "Recording stopped: MIDI input queue overflow." : "Recording stopped because the audio input or device changed.";
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
    state.selectedTracks = state.selectedTrack.isEmpty() ? juce::StringArray() : juce::StringArray{state.selectedTrack};
    sendChangeMessage();
}
void EditorPresenter::selectAllClips()
{ juce::StringArray ids; for (const auto& clip : project().clips) if (clip.scene < 0) ids.add(clip.id); selectClips(std::move(ids)); }
void EditorPresenter::selectTime(TimeSelection selection)
{
    if (!std::isfinite(selection.start) || !std::isfinite(selection.end) || selection.start < 0 || selection.end < selection.start) return;
    juce::StringArray tracks;
    for (const auto& track : project().tracks) if (selection.tracks.contains(track.id)) tracks.add(track.id);
    selection.tracks = std::move(tracks); state.selectedClips.clear();
    if (!selection.tracks.isEmpty()) { state.selectedTrack = selection.tracks[0]; state.selectedTracks = selection.tracks; }
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
            if (clip.scene >= 0) continue;
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
void EditorPresenter::toggleTrackFold(const juce::String& id)
{
    const auto* clicked = findTrack(project(), id); if (!clicked) return;
    const bool folded = !trackFolded(id);
    const auto ids = state.selectedTracks.contains(id) ? state.selectedTracks : juce::StringArray{id};
    const bool ownGesture = !gesturing(); if (ownGesture) beginGesture();
    for (const auto& selected : ids)
    {
        const auto* track = findTrack(project(), selected); if (!track) continue;
        if (track->group) session.foldGroup(selected, folded);
        else session.trackLayout(selected, std::max(36, track->height), folded);
    }
    if (ownGesture) endGesture(false);
}
void EditorPresenter::setTrackHeight(const juce::String& id, int height)
{
    if (const auto* track = findId(project().tracks, id))
        session.trackLayout(id, height <= 18 && !track->group ? std::max(36, track->height) : juce::jlimit(18, 1000, height),
                            !track->group && height <= 18);
}
juce::String EditorPresenter::addTrack()
{ const auto* parent = findTrack(project(), state.selectedTrack); auto id = session.addTrack("Audio", false, parent ? (parent->group ? parent->id : parent->parentGroup) : juce::String()); selectTrack(id); return id; }
juce::String EditorPresenter::addMidiTrack()
{ const auto* parent = findTrack(project(), state.selectedTrack); auto id = session.addTrack("MIDI", true, parent ? (parent->group ? parent->id : parent->parentGroup) : juce::String()); selectTrack(id); return id; }
void EditorPresenter::selectTrack(const juce::String& id, bool additive, bool range)
{
    if (!findTrack(project(), id)) return;
    if (range && !state.selectedTrack.isEmpty())
    {
        int first = -1, last = -1;
        for (size_t index = 0; index < project().tracks.size(); ++index)
        { if (project().tracks[index].id == state.selectedTrack) first = static_cast<int>(index); if (project().tracks[index].id == id) last = static_cast<int>(index); }
        if (first >= 0 && last >= 0)
        { if (!additive) state.selectedTracks.clear(); for (int index = std::min(first, last); index <= std::max(first, last); ++index)
          { const auto& track = project().tracks[static_cast<size_t>(index)]; if (!hiddenByGroup(project(), track)) state.selectedTracks.addIfNotAlreadyThere(track.id); } }
    }
    else if (additive)
    { if (state.selectedTracks.isEmpty() && !state.selectedTrack.isEmpty()) state.selectedTracks.add(state.selectedTrack);
      if (state.selectedTracks.contains(id)) state.selectedTracks.removeString(id); else state.selectedTracks.add(id); }
    else state.selectedTracks = {id};
    state.selectedTrack = state.selectedTracks.contains(id) ? id : state.selectedTracks.isEmpty() ? juce::String() : state.selectedTracks[state.selectedTracks.size() - 1];
    sendChangeMessage();
}
void EditorPresenter::groupSelectedTracks()
{
    if (recording()) stop(); if (!capturePluginStates(true)) return;
    const auto ids = state.selectedTracks.isEmpty() ? juce::StringArray{state.selectedTrack} : state.selectedTracks;
    const auto group = session.groupTracks(ids); if (group.isEmpty()) message("Select audio/MIDI tracks or groups from the same group level."); else selectTrack(group);
}
void EditorPresenter::createGroup()
{ if (recording()) stop(); const auto group = session.groupTracks({}); if (group.isNotEmpty()) selectTrack(group); }
void EditorPresenter::ungroupSelectedTrack()
{
    if (recording()) stop(); if (!capturePluginStates(true)) return;
    const auto* selected = findTrack(project(), state.selectedTrack); if (!selected) return;
    const auto id = selected->group ? selected->id : selected->parentGroup; if (id.isEmpty()) return;
    const auto member = std::find_if(project().tracks.begin(), project().tracks.end(), [&](const auto& track) { return track.parentGroup == id; });
    const auto next = member != project().tracks.end() ? member->id : juce::String();
    if (session.ungroup(id) && next.isNotEmpty()) selectTrack(next);
}
void EditorPresenter::removeFromGroup(const juce::String& id)
{
    if (recording()) stop(); const auto* track = findTrack(project(), id); if (!track) return;
    const auto* parent = findTrack(project(), track->parentGroup); if (!parent) return;
    session.moveTracks({id}, parent->parentGroup);
}
void EditorPresenter::dropTracks(const juce::String& text, const juce::String& destination)
{
    const auto data = juce::JSON::parse(text.substring(7)); if (!data.isArray() || data.getArray()->size() > 512) return;
    juce::StringArray ids; for (const auto& id : *data.getArray()) { if (!id.isString()) return; ids.add(id.toString()); }
    if (recording()) stop(); const auto* target = findTrack(project(), destination);
    const auto group = target ? (target->group ? target->id : target->parentGroup) : juce::String();
    if (!session.moveTracks(ids, group)) message("Tracks cannot be moved into that group.");
}

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
    trackClipboard = *track; trackClips.clear(); trackChildren.clear(); juce::StringArray included {track->id};
    if (track->group) for (const auto& child : project().tracks) if (inGroup(project(), child, track->id)) { trackChildren.push_back(child); included.add(child.id); }
    for (const auto& clip : project().clips) if (included.contains(clip.track)) trackClips.push_back(clip);
}
void EditorPresenter::pasteTrack()
{
    if (!trackClipboard) return;
    std::vector<Track> tracks {*trackClipboard}; tracks.insert(tracks.end(), trackChildren.begin(), trackChildren.end());
    std::map<juce::String, juce::String> ids;
    for (const auto& track : tracks) ids[track.id] = newId();
    const auto root = ids[trackClipboard->id];
    for (auto& track : tracks)
    { const auto original = track.id; track.id = ids[original]; if (original == trackClipboard->id) track.name += " Copy";
      if (const auto parent = ids.find(track.parentGroup); parent != ids.end()) track.parentGroup = parent->second;
      else if (!findTrack(project(), track.parentGroup)) track.parentGroup.clear();
      for (auto& plugin : track.plugins) plugin.id = newId(); }
    auto clips = trackClips;
    for (auto& track : tracks)
    { if (ids.count(track.outputTrack)) track.outputTrack = ids[track.outputTrack];
      for (auto& plugin : track.plugins)
      { if (ids.count(plugin.sidechain)) plugin.sidechain = ids[plugin.sidechain];
        else if (plugin.sidechain.isNotEmpty() && !findTrack(project(), plugin.sidechain)) plugin.sidechain.clear(); } }
    for (auto& clip : clips) { clip.id = newId(); clip.track = ids[clip.track]; }
    if (session.insertClips(clips, tracks)) selectTrack(root);
    else message("Could not paste track; its audio is no longer in this project.");
}
void EditorPresenter::previewNotes(const juce::String& id, const std::vector<MidiNote>& notes)
{
    const auto* track = findId(project().tracks, id); if (!track) return;
    if (std::any_of(track->plugins.begin(), track->plugins.end(), [](const auto& plugin) { return plugin.instrument; }))
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
void EditorPresenter::stopLibraryPreview()
{
    ++*libraryPreviewEpoch;
    libraryTransport.stop(); libraryTransport.setSource(nullptr); librarySource.reset();
}
void EditorPresenter::previewLibraryItem(const juce::String& description)
{
    stopLibraryPreview();
    juce::File file; juce::String instrument; int preset = -1;
    if (description.startsWith("sample:")) file = juce::File(description.substring(7));
    else if (description.startsWith("asset:"))
    {
        const auto* asset = findId(project().assets, description.substring(6));
        if (!asset || asset->missing) { message("This sound's audio file is missing."); return; }
        file = mediaDirectory().getChildFile(asset->path);
    }
    else if (description.startsWith("device:preset:"))
    {
        const auto rest = description.substring(14), index = rest.upToFirstOccurrenceOf(":", false, false);
        if (index.isEmpty() || index.length() > 2 || !index.containsOnly("0123456789") || !rest.containsChar(':')) return;
        preset = index.getIntValue(); const auto identifier = rest.fromFirstOccurrenceOf(":", false, false);
        if (identifier == "builtin:sampler") instrument = identifier;
        else for (const auto& plugin : engine.plugins.availablePlugins())
            if (plugin.createIdentifierString() == identifier && plugin.pluginFormatName == "Opentone" && plugin.isInstrument)
            { instrument = plugin.fileOrIdentifier; break; }
        if (instrument.isEmpty() || preset >= builtinPresetNames(instrument).size()) return;
    }
    else return;
    if (!engine.devices.getCurrentAudioDevice()) { message("Choose an audio output in Settings > Audio to preview sounds."); return; }
    if (!libraryReadAhead.isThreadRunning() && !libraryReadAhead.startThread())
    { message("Could not start library audio preview."); return; }
    const auto epoch = libraryPreviewEpoch; const auto request = epoch->load(); const auto directory = storage.workspace();
    juce::WeakReference<EditorPresenter> owner(this);
    libraryPreviewPool.addJob(std::function<void()>([owner, epoch, request, file, instrument, preset, directory]() mutable
    {
        if (epoch->load() != request) return;
        std::shared_ptr<juce::PositionableAudioSource> source; double rate = 48000; juce::String error;
        if (instrument == "builtin:sampler")
        {
            file = directory.getChildFile("Library Previews/Sampler " + juce::String(preset) + ".wav");
            if (!file.existsAsFile())
            { const auto result = writeSamplerPreset(preset, file); if (result.failed()) error = result.getErrorMessage(); }
        }
        if (instrument.isNotEmpty() && instrument != "builtin:sampler")
        {
            auto synth = createBuiltinPlugin(instrument);
            if (!synth || preset < 0 || preset >= synth->getNumPrograms()) error = "Could not load preset preview.";
            else
            {
                synth->setCurrentProgram(preset); synth->setRateAndBufferSizeDetails(rate, 256); synth->prepareToPlay(rate, 256);
                // shortcut: auditions cap the release tail at three seconds, extend when longer preset previews are needed.
                juce::AudioBuffer<float> audio(2, static_cast<int>(rate * 3)); audio.clear();
                const auto noteOff = static_cast<int>(rate * 0.5);
                for (int offset = 0; offset < audio.getNumSamples(); offset += 256)
                {
                    if (epoch->load() != request) { synth->releaseResources(); return; }
                    const auto count = std::min(256, audio.getNumSamples() - offset);
                    juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, offset, count); juce::MidiBuffer midi;
                    if (offset == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
                    if (noteOff >= offset && noteOff < offset + count) midi.addEvent(juce::MidiMessage::noteOff(1, 60), noteOff - offset);
                    synth->processBlock(block, midi);
                }
                synth->releaseResources();
                bool finite = true;
                for (int c = 0; c < audio.getNumChannels(); ++c) for (int i = 0; i < audio.getNumSamples(); ++i)
                    finite = finite && std::isfinite(audio.getSample(c, i));
                jassert(finite);
                if (!finite) error = "Preset preview produced invalid audio.";
                else
                {
                    audio.applyGainRamp(0, 240, 0, 1); audio.applyGainRamp(audio.getNumSamples() - 960, 960, 1, 0);
                    source = std::make_shared<juce::MemoryAudioSource>(audio, true);
                }
            }
        }
        else if (error.isEmpty())
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 || reader->sampleRate > 1000000
                || reader->lengthInSamples <= 0 || reader->numChannels < 1 || reader->numChannels > 2)
                error = "Could not preview this sound. Choose a readable mono or stereo audio file.";
            else { rate = reader->sampleRate; source = std::make_shared<juce::AudioFormatReaderSource>(reader.release(), true); }
        }
        if (epoch->load() != request) return;
        juce::MessageManager::callAsync([owner, epoch, request, source, rate, error]
        {
            if (epoch->load() != request || !owner) return;
            if (error.isNotEmpty()) { owner->message(error); return; }
            if (!source) return;
            owner->librarySource = source;
            owner->libraryTransport.setSource(source.get(), 32768, &owner->libraryReadAhead, rate);
            owner->libraryTransport.setGain(static_cast<float>(owner->project().cueGain));
            owner->libraryTransport.setPosition(0); owner->libraryTransport.start();
        });
    }));
}

void EditorPresenter::addDevice(const juce::String& identifier, juce::String track)
{
    auto device = identifier; int preset = -1;
    if (device.startsWith("preset:"))
    {
        const auto rest = device.substring(7), index = rest.upToFirstOccurrenceOf(":", false, false);
        if (index.isEmpty() || index.length() > 2 || !index.containsOnly("0123456789") || !rest.containsChar(':')) return;
        preset = index.getIntValue(); device = rest.fromFirstOccurrenceOf(":", false, false);
    }
    const bool createTrack = track.isEmpty(); if (createTrack) track = newId();
    bool added = false;
    if (device == "builtin:sampler")
    {
        if (preset >= samplerPresetNames().size()) return;
        const bool ownGesture = preset >= 0 && !session.gesturing(); if (ownGesture) session.beginGesture();
        const auto* target = findTrack(project(), track);
        added = target && target->sampler ? true : session.addSampler(track, createTrack);
        if (added && preset >= 0) loadSamplerPreset(track, preset);
        if (ownGesture) session.endGesture(false);
    }
    else
    {
        for (const auto& description : engine.plugins.availablePlugins()) if (description.createIdentifierString() == device)
        {
            if (preset >= 0)
            {
                if (description.pluginFormatName != "Opentone" || preset >= builtinPresetNames(description.fileOrIdentifier).size()) return;
                if (!capturePluginStates(true)) return;
                auto plugin = builtinPluginModel(description.fileOrIdentifier, {}, preset);
                if (const auto* target = findTrack(project(), track))
                    for (const auto& current : target->plugins) if (current.instrument)
                    {
                        juce::PluginDescription existing; const auto xml = juce::XmlDocument::parse(current.description);
                        if (xml && existing.loadFromXml(*xml) && existing.pluginFormatName == "Opentone" && existing.fileOrIdentifier == description.fileOrIdentifier)
                        { session.pluginStates({{current.id, plugin.state}}); selectTrack(track); return; }
                    }
                added = session.addPlugin(track, std::move(plugin), createTrack);
            }
            else
            {
                auto xml = description.createXml();
                if (xml) added = session.addPlugin(track, { newId(), description.name, xml->toString(), {}, description.isInstrument, false }, createTrack);
            }
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
    if (track && std::any_of(track->plugins.begin(), track->plugins.end(), [](const auto& plugin) { return plugin.instrument; })) return engine.plugins.preview(id, note, velocity, held, true);
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
void EditorPresenter::loadSamplerPreset(const juce::String& id, int index)
{
    const auto destination = id; const auto* track = findTrack(project(), destination);
    const auto names = samplerPresetNames();
    if (!track || !track->sampler || index < 0 || index >= names.size()) return;
    const auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Opentone/Factory Samples").getChildFile(names[index] + ".wav");
    const auto generated = writeSamplerPreset(index, file);
    if (generated.failed()) { message(generated.getErrorMessage()); return; }
    const auto imported = Storage::copyAudio(file, mediaDirectory());
    if (imported.result.failed()) { message(imported.result.getErrorMessage()); return; }
    if (!session.loadSampler(destination, imported.asset.id, 0, static_cast<double>(imported.asset.length) / imported.asset.sampleRate, imported.asset, 60))
    { mediaDirectory().getChildFile(imported.asset.path).deleteFile(); message("Could not load sampler preset."); return; }
    engine.releasePreview(destination, -1); selectTrack(destination);
}
void EditorPresenter::renameTrack(const juce::String& id, const juce::String& name) { session.renameTrack(id, name); }
void EditorPresenter::deleteTrack(const juce::String& id) { const auto track = id; if (recording()) stop(); if (capturePluginStates(true)) session.deleteTrack(track); }
void EditorPresenter::controls(const juce::String& id, double gain, bool mute)
{ if (session.controls(id, gain, mute)) { recordAutomation(id, 0, gain); recordAutomation(id, 2, mute ? 1 : 0); } }
void EditorPresenter::setPan(const juce::String& id, double pan)
{ if (session.setPan(id, pan)) recordAutomation(id, 1, pan); }
void EditorPresenter::setSend(const juce::String& id, int index, double gain)
{ if (session.setSend(id, index, gain)) recordAutomation(id, index + 3, gain); }
void EditorPresenter::recordAutomation(const juce::String& id, int parameter, double value)
{
    if (!state.automationRecord || !recording() || (id != "master" && !armed(id))) return;
    const auto time = position(); if (time < recordStart || time > recordEnd) return;
    const auto* track = findTrack(project(), id); if (!track) return;
    auto points = automationPoints(*track, parameter); const auto at = std::lower_bound(points.begin(), points.end(), time, [](const auto& point, double t) { return point.time < t; });
    if (at != points.end() && std::abs(at->time - time) < 0.03) at->value = value;
    else if (at != points.begin() && time - (at - 1)->time < 0.03) { (at - 1)->time = time; (at - 1)->value = value; }
    else { if (points.size() >= 4096) { if (!automationLimitReached) message("Automation reached 4096 points; audio and MIDI recording continue."); automationLimitReached = true; return; } points.insert(at, {time, value}); }
    session.setTrackAutomation(id, parameter, std::move(points));
}
void EditorPresenter::jumpLocator(bool next)
{
    const auto at = position();
    if (next) { for (const auto& locator : project().locators) if (locator.time > at + 0.001) { seek(locator.time); return; } }
    else for (auto i = project().locators.rbegin(); i != project().locators.rend(); ++i) if (i->time < at - 0.001) { seek(i->time); return; }
}
void EditorPresenter::tapTempo(double hostTimeMs)
{
    const auto now = hostTimeMs > 0 ? hostTimeMs : juce::Time::getMillisecondCounterHiRes();
    if (!tempoTaps.empty() && now - tempoTaps.back() > 3500) tempoTaps.clear();
    if (!tempoTaps.empty() && now <= tempoTaps.back()) return;
    tempoTaps.push_back(now); if (tempoTaps.size() > 8) tempoTaps.erase(tempoTaps.begin());
    if (tempoTaps.size() >= 2) setBpm(juce::jlimit(20.0, 999.0, 60000 * static_cast<double>(tempoTaps.size() - 1) / (tempoTaps.back() - tempoTaps.front())));
}
void EditorPresenter::setBpm(double bpm) { if (session.setBpm(bpm) && engine.linkEnabled()) engine.requestLinkTempo(bpm); }
void EditorPresenter::insertClip(Clip clip, bool create) { session.insertClip(std::move(clip), create); }
void EditorPresenter::changeClip(const Clip& clip) { session.changeClip(clip); }
bool EditorPresenter::changeClips(const std::vector<Clip>& clips, const std::vector<AutomationEdit>& automation) { return session.changeClips(clips, automation); }
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
const std::vector<std::pair<juce::String, juce::String>>& EditorPresenter::outputChoices() const
{
    const auto revision = session.editRevision();
    if (revision != routeRevision)
    { routeRevision = revision; routeChoices.clear();
      for (const auto& track : project().tracks) if (!track.midi) routeChoices.emplace_back(track.id, track.name); }
    return routeChoices;
}
void EditorPresenter::setTrackRouting(const juce::String& id, bool input, bool output, int monitor, int channel, juce::String outputTrack)
{
    if (recording()) { message("Stop recording before changing track routing."); return; }
    if (!session.routing(id, input, output, monitor, channel, std::move(outputTrack))) { sendChangeMessage(); return; }
    engine.plugins.setArmed(state.armedTracks);
    engine.releasePreview(id, -1); engine.plugins.liveMidi(id, juce::MidiMessage::allNotesOff(1));
    if (input && monitor != 2) if (const auto* track = findTrack(project(), id)) if (!track->midi)
    {
        juce::WeakReference<EditorPresenter> safe(this);
        requestMicrophonePermission([safe](bool granted)
        { if (!safe) return; if (!granted) safe->message("Microphone access is denied. Enable Opentone in System Settings > Privacy & Security > Microphone.");
          else { const auto result = safe->engine.enableMicrophone(); if (result.failed()) safe->message(result.getErrorMessage()); } });
    }
    sendChangeMessage();
}
void EditorPresenter::armTrack(const juce::String& id, bool enabled, bool additive)
{
    const auto* track = findId(project().tracks, id); if (!track || track->returnIndex >= 0 || track->group) return;
    if (recording()) { message("Stop recording before changing armed tracks."); return; }
    const bool ownGesture = enabled && !additive && !gesturing(); if (ownGesture) beginGesture();
    if (enabled && !additive)
    {
        juce::StringArray previous;
        for (const auto& candidate : project().tracks) if (candidate.armed && candidate.id != id) previous.add(candidate.id);
        for (const auto& other : previous) session.armTrack(other, false);
        state.armedTracks.clear();
    }
    const bool changed = session.armTrack(id, enabled); if (ownGesture) endGesture(false);
    if (!changed) return;
    if (enabled) state.armedTracks.addIfNotAlreadyThere(id); else state.armedTracks.removeString(id);
    engine.plugins.setArmed(state.armedTracks);
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
    const bool audio = std::any_of(project().tracks.begin(), project().tracks.end(), [this](const auto& track) { return armed(track.id) && !track.midi && track.inputEnabled; });
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
    for (const auto& id : state.armedTracks) if (const auto* track = findId(project().tracks, id)) audio = audio || (!track->midi && track->inputEnabled);
    const bool eligible = std::any_of(project().tracks.begin(), project().tracks.end(), [this](const auto& track) { return armed(track.id) && (track.midi || track.inputEnabled); });
    if (!eligible) { message("Enable an input on an armed audio track before recording."); return; }
    engine.stop();
    if (!engine.devices.getCurrentAudioDevice()) { message("Choose an audio device in Settings > Audio before recording."); return; }
    if (audio) { const auto result = engine.enableMicrophone(); if (result.failed()) { message(result.getErrorMessage()); return; } }
    const auto selection = captureSelection();
    recordStart = state.timeSelection ? state.timeSelection->start : selection.clips.empty() ? 0 : selection.start;
    const auto playStart = recordStart;
    if (state.punchIn) recordStart = std::max(recordStart, project().loopStart);
    recordEnd = state.punchOut ? project().loopEnd : std::numeric_limits<double>::infinity();
    if (recordEnd <= recordStart) { message("Punch-out must be after the recording start."); return; }
    recordAsset = {}; recordFile = juce::File();
    if (audio)
    {
        recordAsset = { newId(), {}, engine.rate(), 1, 0, false };
        recordAsset.path = "Audio/" + recordAsset.id + "-Recording.wav";
        recordFile = mediaDirectory().getChildFile(recordAsset.path);
        const auto result = engine.startAudioRecording(recordFile, recordStart, std::isfinite(recordEnd) ? recordEnd : -1);
        if (result.failed()) { message(result.getErrorMessage()); return; }
    }
    recordingNotes.clear(); noteLimitReached = automationLimitReached = recordInputOverflow = false; midiOverflow.store(false); lastRecordTime = recordStart;
    { const juce::ScopedLock lock(midiInputLock); midiInputEvents.clear(); }
    for (const auto& id : state.armedTracks) if (const auto* track = findId(project().tracks, id)) if (track->midi || track->inputEnabled)
        recordTakes.push_back({ id, { newId(), id, track->midi ? juce::String() : recordAsset.id, recordStart, 0, 0 } });
    recordConfiguration = engine.deviceConfiguration();
    engine.submit(session.snapshot(), true, false);
    engine.seek(playStart); engine.play(); message("Recording");
}
void EditorPresenter::recordNote(const juce::String& track, const juce::String& source, int pitch, double velocity, bool on, double time)
{
    if (!recording() || pitch < 0 || pitch > 127 || !std::isfinite(velocity)) return;
    auto* take = recordingTake(track); if (!take || take->clip.asset.isNotEmpty()) return;
    if (time >= recordEnd && on) return;
    time = std::min(time, recordEnd);
    const auto position = std::max(0.0, time - recordStart); lastRecordTime = std::max(lastRecordTime, time);
    const auto key = std::make_pair(track, source);
    auto pending = recordingNotes.find(key);
    if (pending != recordingNotes.end())
    {
        auto& note = take->clip.notes[pending->second]; note.duration = time < recordStart ? 0 : std::max(1 / engine.rate(), position - note.start);
        recordingNotes.erase(pending);
    }
    if (on)
    {
        // shortcut: takes stop at 4096 notes; split longer takes when multi-clip recording is added.
        if (take->clip.notes.size() >= maxMidiNotes) { noteLimitReached = true; return; }
        recordingNotes[key] = take->clip.notes.size();
        take->clip.notes.push_back({ pitch, position, 1 / engine.rate(), juce::jlimit(0.0, 1.0, velocity) });
    }
}
void EditorPresenter::inputNoteOn(const juce::String& track, int pitch, double velocity)
{ if (const auto* model = findTrack(project(), track)) if (model->monitor != 2) previewNote(track, pitch, velocity, true); recordNote(track, "keyboard:" + juce::String(pitch), pitch, velocity, true, engine.position()); }
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
        for (auto route = controllerRoutes.begin(); route != controllerRoutes.end();)
        { if (!route->first.startsWith(*it + ":")) { ++route; continue; } for (const auto& track : route->second) engine.plugins.liveMidi(track, juce::MidiMessage::allSoundOff(1)); route = controllerRoutes.erase(route); }
        it = knownMidiInputs.erase(it);
    }
    for (const auto& input : available) if (knownMidiInputs.insert(input.identifier).second)
        engine.devices.setMidiInputDeviceEnabled(input.identifier, true);
}
void EditorPresenter::handleIncomingMidiMessage(juce::MidiInput* input, const juce::MidiMessage& value)
{
    if (!value.isNoteOnOrOff() && !value.isAllNotesOff() && !value.isAllSoundOff() && !value.isPitchWheel() && !value.isController()) return;
    const juce::ScopedLock lock(midiInputLock);
    if (midiInputEvents.size() >= 4096) { midiOverflow.store(true); triggerAsyncUpdate(); return; }
    midiInputEvents.push_back({ (input ? input->getIdentifier() : juce::String("MIDI")) + ":" + juce::String(value.getChannel()), value, engine.position(), juce::Time::getMillisecondCounterHiRes() });
    triggerAsyncUpdate();
}
void EditorPresenter::releaseControllerNotes()
{
    for (const auto& held : controllerNotes) for (const auto& track : held.second.tracks)
        releasePreview(track, held.second.pitch);
    controllerNotes.clear();
    for (const auto& route : controllerRoutes) for (const auto& track : route.second) engine.plugins.liveMidi(track, juce::MidiMessage::allSoundOff(1));
    controllerRoutes.clear();
}
void EditorPresenter::setMidiMapMode(bool enabled)
{ releaseControllerNotes(); state.midiMapMode = enabled; state.keyMapMode = false; state.midiLearnTarget.clear(); heldMappings.clear(); sendChangeMessage(); }
void EditorPresenter::setKeyMapMode(bool enabled)
{ releaseControllerNotes(); state.keyMapMode = enabled; state.midiMapMode = false; state.midiLearnTarget.clear(); heldMappings.clear(); sendChangeMessage(); }
bool EditorPresenter::mappedKey(int key, bool down)
{
    if (state.midiMapMode || key < 0 || key > 127 || (!state.keyMapMode && std::none_of(project().midiMappings.begin(), project().midiMappings.end(), [&](const auto& mapping) { return mapping.source == "Computer" && mapping.number == key; }))) return false;
    return mappedMidi({"Computer", down ? juce::MidiMessage::noteOn(1, key, 1.0f) : juce::MidiMessage::noteOff(1, key), position()});
}
void EditorPresenter::releaseMappedKeys()
{ for (const auto& mapping : project().midiMappings) if (mapping.source == "Computer" && !juce::KeyPress::isKeyCurrentlyDown(mapping.number)) mappedKey(mapping.number, false); }
void EditorPresenter::learnMidi(const juce::String& target)
{ if (state.midiMapMode || state.keyMapMode) { state.midiLearnTarget = target; message(state.keyMapMode ? "Press a key to assign it" : "Move a MIDI control to assign it"); } }
bool EditorPresenter::mappedMidi(const InputEvent& event)
{
    const auto& midi = event.message;
    if (!midi.isController() && !midi.isNoteOnOrOff()) return false;
    const bool note = midi.isNoteOnOrOff(); const auto number = note ? midi.getNoteNumber() : midi.getControllerNumber();
    if (state.midiMapMode || (state.keyMapMode && event.source == "Computer"))
    {
        if (state.midiLearnTarget.isNotEmpty() && (!note || midi.isNoteOn()))
        {
            if (session.mapMidi({event.source, state.midiLearnTarget, midi.getChannel(), number, note}))
            { state.midiLearnTarget.clear(); message("MIDI control assigned"); }
        }
        return true;
    }
    if (state.keyMapMode) return true;
    const auto mappings = project().midiMappings;
    bool mapped = false;
    for (const auto& mapping : mappings)
    {
        if (mapping.source != event.source || mapping.channel != midi.getChannel() || mapping.number != number || mapping.note != note) continue;
        mapped = true;
        const auto value = note ? (midi.isNoteOn() ? midi.getFloatVelocity() : 0.0f) : midi.getControllerValue() / 127.0f;
        const bool down = note ? midi.isNoteOn() : value >= 0.5f;
        const bool pressed = down && heldMappings.insert(mapping.target).second;
        if (!down) heldMappings.erase(mapping.target);
        const auto parts = juce::StringArray::fromTokens(mapping.target, "/", "");
        if (parts.size() == 1)
        {
            if (mapping.target == "tempo" && (!note || down)) setBpm(20 + value * 280);
            else if (mapping.target == "groove" && (!note || down)) setGroove(value);
            else if (mapping.target == "cue" && (!note || down)) setCue(2 * std::pow(value, 1.0 / 0.3));
            else if (pressed)
            {
                if (mapping.target == "tap") tapTempo(event.hostTimeMs);
                else if (mapping.target == "play") togglePlayback(); else if (mapping.target == "stop") stop();
                else if (mapping.target == "record") toggleRecording(); else if (mapping.target == "loop") toggleLoop();
                else if (mapping.target == "metronome") setMetronome(!metronome());
                else if (mapping.target == "follow") setFollow(!state.follow);
                else if (mapping.target == "overdub") setOverdub(!state.overdub);
                else if (mapping.target == "automation-record") setAutomationRecord(!state.automationRecord);
                else if (mapping.target == "punch-in") setPunch(true, !state.punchIn);
                else if (mapping.target == "punch-out") setPunch(false, !state.punchOut);
                else if (mapping.target == "draw") setDrawMode(!state.drawMode);
                else if (mapping.target == "keyboard") setComputerKeyboard(!state.computerKeyboard);
                else if (mapping.target == "scale") setScale(project().scaleRoot, project().scaleMode, !project().scaleEnabled);
            }
        }
        else if (parts.size() == 3 && parts[0] == "track")
        {
            const auto* track = findTrack(project(), parts[1]); if (!track) continue;
            if (parts[2] == "volume" && (!note || down))
            { const auto gain = 2 * std::pow(value, 1.0 / 0.3); if (track->id == "master") setMaster(static_cast<float>(gain)); else controls(track->id, gain, track->mute); }
            else if (parts[2] == "pan" && (!note || down)) setPan(track->id, value * 2 - 1);
            else if (parts[2] == "sampler-volume" && (!note || down)) setSamplerGain(track->id, 2 * std::pow(value, 1.0 / 0.3));
            else if (parts[2] == "sampler-root" && (!note || down)) session.setRootNote(track->id, juce::roundToInt(value * 127));
            else if (parts[2].startsWith("send") && (!note || down)) setSend(track->id, parts[2].substring(4).getIntValue(), maxSendGain * std::pow(value, 1.0 / 0.3));
            else if (pressed)
            { if (parts[2] == "arm") armTrack(track->id, !armed(track->id)); else if (parts[2] == "mute") controls(track->id, track->gain, !track->mute); else if (parts[2] == "solo") setSolo(track->id, !track->solo); }
        }
        else if (parts.size() == 3 && parts[0] == "plugin")
        {
            const auto slot = engine.plugins.editorSlot(parts[1]);
            if (slot && slot->instance && (!note || down))
            { const juce::ScopedLock lock(engine.plugins.mutex);
              const auto apply = [&](juce::AudioProcessorParameter& parameter)
              { if (note && parameter.getNumSteps() >= 2 && parameter.getNumSteps() <= 128)
                { if (pressed) { const auto steps = parameter.getNumSteps(); parameter.setValueNotifyingHost(static_cast<float>((juce::roundToInt(parameter.getValue() * (steps - 1)) + 1) % steps) / static_cast<float>(steps - 1)); } }
                else parameter.setValueNotifyingHost(value); };
              for (auto* parameter : slot->instance->getParameters())
                  if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)) if (identified->paramID == parts[2]) apply(*identified);
              if (parts[2].startsWith("#")) { const auto index = parts[2].substring(1).getIntValue(); const auto parameters = slot->instance->getParameters(); if (index >= 0 && index < parameters.size()) apply(*parameters[index]); } }
        }
    }
    return mapped;
}
void EditorPresenter::setOutputPair(int pair)
{
    auto* device = engine.devices.getCurrentAudioDevice(); if (!device || pair < 0 || pair * 2 + 1 >= device->getOutputChannelNames().size()) return;
    auto setup = engine.devices.getAudioDeviceSetup(); setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear(); setup.outputChannels.setRange(pair * 2, 2, true);
    const auto error = engine.devices.setAudioDeviceSetup(setup, true); if (error.isNotEmpty()) message(error); else sendChangeMessage();
}
void EditorPresenter::handleAsyncUpdate()
{
    std::vector<InputEvent> events;
    { const juce::ScopedLock lock(midiInputLock); events.swap(midiInputEvents); }
    if (midiOverflow.exchange(false)) { releaseControllerNotes(); if (recording()) recordInputOverflow = true; }
    for (const auto& event : events)
    {
        if (mappedMidi(event)) continue;
        if (event.message.isAllNotesOff() || event.message.isAllSoundOff())
        {
            for (const auto& held : controllerNotes) for (const auto& track : held.second.tracks)
                recordNote(track, held.first, held.second.pitch, 0, false, event.time);
            releaseControllerNotes(); continue;
        }
        if (event.message.isPitchWheel() || event.message.isController())
        {
            juce::StringArray tracks; for (const auto& track : project().tracks) if (track.midi && track.inputEnabled && (track.midiChannel == 0 || track.midiChannel == event.message.getChannel()) && (armed(track.id) || track.monitor == 0)) tracks.add(track.id);
            if (tracks.isEmpty()) if (const auto* track = findId(project().tracks, state.selectedTrack)) if (track->midi && track->inputEnabled && (track->midiChannel == 0 || track->midiChannel == event.message.getChannel())) tracks.add(track->id);
            for (const auto& track : controllerRoutes[event.source]) tracks.addIfNotAlreadyThere(track);
            for (const auto& track : tracks) engine.plugins.liveMidi(track, event.message); continue;
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
        for (const auto& track : project().tracks) if (track.midi && track.inputEnabled && (track.midiChannel == 0 || track.midiChannel == event.message.getChannel()) && (armed(track.id) || track.monitor == 0)) next.tracks.add(track.id);
        if (next.tracks.isEmpty()) if (const auto* track = findId(project().tracks, state.selectedTrack)) if (track->midi && track->inputEnabled && (track->midiChannel == 0 || track->midiChannel == event.message.getChannel())) next.tracks.add(track->id);
        for (const auto& track : next.tracks) controllerRoutes[event.source].addIfNotAlreadyThere(track);
        for (const auto& track : next.tracks)
        { if (const auto* model = findTrack(project(), track)) if (model->monitor != 2) previewNote(track, pitch, event.message.getFloatVelocity(), true); recordNote(track, key, pitch, event.message.getFloatVelocity(), true, event.time); }
        controllerNotes[key] = std::move(next);
    }
}
void EditorPresenter::finishRecording()
{
    if (!recording()) return;
    handleAsyncUpdate();
    const auto end = std::min(std::max(engine.position(), lastRecordTime), recordEnd);
    for (const auto& pending : recordingNotes) if (auto* take = recordingTake(pending.first.first))
    { auto& note = take->clip.notes[pending.second]; note.duration = engine.position() <= recordStart ? 0 : std::max(1 / engine.rate(), end - recordStart - note.start); }
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
            take.clip.notes.erase(std::remove_if(take.clip.notes.begin(), take.clip.notes.end(), [](const auto& note) { return note.duration <= 0; }), take.clip.notes.end());
            if (take.clip.notes.empty()) continue;
            take.clip.duration = std::max(0.001, end - recordStart);
            for (const auto& note : take.clip.notes) take.clip.duration = std::max(take.clip.duration, note.start + note.duration);
            take.clip.loopLength = take.clip.duration;
            if (state.overdub)
            {
                const auto original = std::find_if(project().clips.begin(), project().clips.end(), [&](const auto& clip) { return clip.scene < 0 && clip.track == take.id && clip.asset.isEmpty() && clip.start <= take.clip.start && clip.start + clip.duration > take.clip.start; });
                if (original != project().clips.end())
                {
                    auto merged = *original;
                    if (merged.notes.size() + take.clip.notes.size() <= maxMidiNotes)
                    {
                        for (auto note : take.clip.notes)
                        { note.start = midiSourceTime(merged, take.clip.start - merged.start + note.start); merged.notes.push_back(std::move(note)); }
                        merged.duration = std::max(merged.duration, end - merged.start);
                        if (session.changeClip(merged)) continue;
                    }
                    // Preserve the original clip when a full clip cannot accept more overdub notes.
                    auto extra = *track; extra.id = newId(); extra.name += " Overdub"; extra.armed = false;
                    for (auto& plugin : extra.plugins) plugin.id = newId();
                    take.clip.track = extra.id;
                    if (session.insertClips({take.clip}, {extra})) continue;
                    warning = "Could not place MIDI overdub; the original clip was preserved. " + preserveMidiTake(take.clip, project().bpm); continue;
                }
            }
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
    auto start = state.timeSelection ? state.timeSelection->start : std::numeric_limits<double>::max();
    if (!state.timeSelection)
        for (const auto& clip : project().clips)
            if (clip.scene < 0 && state.selectedClips.contains(clip.id)) start = std::min(start, clip.start);
    if (start == std::numeric_limits<double>::max()) start = 0;
    if (project().loopEnabled && start >= project().loopEnd) start = project().loopStart;
    engine.seek(start);
    engine.play();
}
void EditorPresenter::stop() { requestingMicrophone = false; engine.stop(); finishRecording(); }
void EditorPresenter::seek(double seconds) { finishRecording(); engine.seek(seconds); }
void EditorPresenter::setMaster(float gain) { controls("master", gain, false); }
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
    stopLibraryPreview();
    requestingMicrophone = false;
    releaseControllerNotes();
    storage.cancel(); ++operation; imports.clear(); busySave = busyOpen = false; pendingImports = 0;
    state = {}; importedName.clear(); clipboard.reset();
}
void EditorPresenter::newProject()
{
    juce::WeakReference<EditorPresenter> safe(this);
    guardUnsaved([safe]
    {
        if (!safe) return;
        safe->clearPending();
        Project project;
        project.tracks.push_back({ newId(), "1 Audio" }); project.tracks.back().sends = {0, 0};
        Track midi { newId(), "2 MIDI" }; midi.sends = {0, 0}; midi.midi = true; midi.monitor = 1; project.tracks.push_back(std::move(midi));
        addDefaultReturns(project);
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
            safe->clearPending();
            const bool imported = directory.hasFileExtension("als");
            if (imported) safe->importedName = directory.getFileNameWithoutExtension();
            if (!safe->session.replace(result.project, imported ? safe->storage.workspace() : directory, !imported))
            { safe->message("Imported project failed validation."); return; }
            const auto recent = safe->storage.rememberProject(directory);
            if (!safe->project().tracks.empty()) safe->selectTrack(safe->project().tracks.front().id);
            safe->message((result.warning.isEmpty() ? juce::String("Project opened") : result.warning)
                          + (recent.failed() ? "; " + recent.getErrorMessage() : juce::String()));
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
        safe->session.saved(snapshot, directory);
        const auto recent = safe->storage.rememberProject(directory);
        safe->message(recent.failed() ? "Project saved; " + recent.getErrorMessage() : "Project saved");
        if (after) { if (safe->session.dirty()) safe->guardUnsaved(after); else after(); }
    });
}
}
