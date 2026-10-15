// SPDX-License-Identifier: AGPL-3.0-only
#include "domain/ProjectSession.h"
#include "domain/TimelineMath.h"

namespace opentone
{
namespace
{
void cutCovered(Project& project, const juce::StringArray& placed)
{
    std::vector<Clip> covers;
    for (const auto& clip : project.clips) if (placed.contains(clip.id)) covers.push_back(clip);
    for (const auto& cover : covers)
    {
        std::vector<Clip> remaining;
        const auto end = cover.start + cover.duration;
        for (const auto& clip : project.clips)
        {
            const auto clipEnd = clip.start + clip.duration;
            if (placed.contains(clip.id) || clip.scene != cover.scene || clip.track != cover.track || clipEnd <= cover.start || clip.start >= end)
            { remaining.push_back(clip); continue; }
            const bool leftRemains = clip.start < cover.start;
            if (leftRemains)
            { auto left = clip; left.duration = cover.start - clip.start; left.fadeIn = std::min(left.fadeIn, left.duration); left.fadeOut = std::min(left.fadeOut, left.duration); remaining.push_back(left); }
            if (clipEnd > end)
            { auto right = clip; if (leftRemains) right.id = newId(); right.offset += (end - clip.start) * clip.playbackRate;
              right.start = end; right.duration = clipEnd - end; right.fadeIn = std::min(right.fadeIn, right.duration); right.fadeOut = std::min(right.fadeOut, right.duration); remaining.push_back(right); }
        }
        project.clips = std::move(remaining);
    }
}
}
struct ProjectSession::Edit : juce::UndoableAction
{
    Edit(ProjectSession& owner, Project beforeIn, Project afterIn, bool structuralIn)
        : session(owner), before(std::move(beforeIn)), after(std::move(afterIn)), structural(structuralIn) {}
    bool perform() override { session.apply(after, structural); return true; }
    bool undo() override { session.apply(before, structural); return true; }
    int getSizeInUnits() override
    {
        size_t notes = 0;
        for (const auto* project : { &before, &after }) for (const auto* track : allTracks(*project))
        {
            notes += track->volumeAutomation.size() + track->panAutomation.size();
            for (const auto& plugin : track->plugins) notes += static_cast<size_t>(plugin.state.length()) / 4096 + 1;
        }
        for (const auto& clip : before.clips) notes += clip.notes.size();
        for (const auto& clip : after.clips) notes += clip.notes.size();
        return static_cast<int>(std::min<size_t>(std::numeric_limits<int>::max(), 1 + notes + before.clips.size() + after.clips.size()
                     + before.tracks.size() + after.tracks.size() + before.assets.size() + after.assets.size()));
    }
    ProjectSession& session; Project before, after; bool structural;
};

namespace
{
int nextTrackNumber(const Project& project)
{ return static_cast<int>(std::count_if(project.tracks.begin(), project.tracks.end(), [](const auto& track) { return track.returnIndex < 0; })) + 1; }
void insertTrack(Project& project, Track track)
{ track.sends.resize(static_cast<size_t>(std::max(2, returnCount(project))), 0.0); const auto before = std::find_if(project.tracks.begin(), project.tracks.end(), [](const auto& item) { return item.returnIndex >= 0; }); project.tracks.insert(before, std::move(track)); }
}
ProjectSession::ProjectSession() = default;
ProjectSession::~ProjectSession() = default;

bool ProjectSession::replace(Project next, juce::File location, bool saved)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (validate(next).failed()) return false;
    orderTracks(next);
    gesture.reset(); undo.clearUndoHistory(); state = std::move(next);
    directory = std::move(location); saveLocation = directory; ++generation; ++revision;
    savedRevision = saved ? revision : 0;
    notify(true);
    return true;
}

void ProjectSession::saved(const Snapshot& captured, const juce::File& location)
{
    if (captured.generation != generation) return;
    // Keep the runtime media root stable so Save As cannot orphan concurrent imports.
    saveLocation = location;
    if (captured.revision == revision) savedRevision = revision;
    notify(false);
}

juce::String ProjectSession::addTrack(juce::String name, bool midi, juce::String parent)
{
    auto id = newId(); auto next = state;
    Track track { id, numberedTrackName(name, nextTrackNumber(next)) }; track.sends = {0, 0}; track.midi = midi; track.monitor = midi ? 1 : 2; track.parentGroup = std::move(parent); insertTrack(next, std::move(track)); orderTracks(next);
    edit(std::move(next), "Add track"); return id;
}

juce::String ProjectSession::groupTracks(const juce::StringArray& ids)
{
    auto next = state; juce::StringArray roots;
    for (const auto& track : state.tracks) if (ids.contains(track.id) && track.returnIndex < 0)
    {
        bool nested = false;
        for (const auto& parent : state.tracks) if (parent.group && ids.contains(parent.id) && inGroup(state, track, parent.id)) nested = true;
        if (!nested) roots.add(track.id);
    }
    juce::String parent;
    if (!roots.isEmpty())
    { parent = findId(state.tracks, roots[0])->parentGroup; for (const auto& id : roots) if (findId(state.tracks, id)->parentGroup != parent) return {}; }
    else if (!ids.isEmpty()) return {};
    Track group {newId(), numberedTrackName("Group", nextTrackNumber(next))}; group.sends.assign(static_cast<size_t>(std::max(2, returnCount(next))), 0); group.group = true; group.inputEnabled = false; group.parentGroup = parent;
    const auto id = group.id;
    auto before = next.tracks.end();
    if (!roots.isEmpty()) before = std::find_if(next.tracks.begin(), next.tracks.end(), [&](const auto& track) { return track.id == roots[0]; });
    else before = std::find_if(next.tracks.begin(), next.tracks.end(), [](const auto& track) { return track.returnIndex >= 0; });
    next.tracks.insert(before, std::move(group));
    for (auto& track : next.tracks) if (roots.contains(track.id)) track.parentGroup = id;
    orderTracks(next); if (!edit(std::move(next), "Group tracks")) return {}; return id;
}
bool ProjectSession::ungroup(const juce::String& id)
{
    const auto* original = findId(state.tracks, id); if (!original || !original->group) return false;
    auto next = state; for (auto& track : next.tracks) if (track.parentGroup == id) track.parentGroup = original->parentGroup;
    for (auto& track : next.tracks) if (track.outputTrack == id) track.outputTrack = outputDestination(*original).isEmpty() ? "master" : outputDestination(*original);
    erase(next.tracks, [&](const auto& track) { return track.id == id; }); orderTracks(next); return edit(std::move(next), "Ungroup tracks");
}
bool ProjectSession::moveTracks(const juce::StringArray& ids, const juce::String& group)
{
    const auto* destination = findId(state.tracks, group); if (group.isNotEmpty() && (!destination || !destination->group)) return false;
    auto next = state;
    for (auto& track : next.tracks) if (ids.contains(track.id) && track.returnIndex < 0)
    {
        bool nested = false; for (const auto& parent : state.tracks) if (parent.group && ids.contains(parent.id) && inGroup(state, track, parent.id)) nested = true;
        if (nested) continue;
        if (track.id == group || (destination && inGroup(state, *destination, track.id))) return false;
        track.parentGroup = group;
    }
    if (auto* parent = findTrack(next, group)) parent->groupCollapsed = false;
    if (validate(next).failed()) return false;
    orderTracks(next); return edit(std::move(next), "Move tracks into group");
}
bool ProjectSession::foldGroup(const juce::String& id, bool collapsed)
{ auto next = state; auto* group = findTrack(next, id); if (!group || !group->group) return false; group->groupCollapsed = collapsed; return edit(std::move(next), "Fold group", false); }
bool ProjectSession::armTrack(const juce::String& id, bool armed)
{ auto next = state; auto* track = findTrack(next, id); if (!track || id == "master" || track->group || track->returnIndex >= 0) return false;
  track->armed = armed; return edit(std::move(next), "Arm track", false); }
bool ProjectSession::trackLayout(const juce::String& id, int height, bool folded)
{ auto next = state; auto* track = findTrack(next, id); if (!track) return false;
  track->height = height; track->folded = folded; return edit(std::move(next), "Track layout", false); }
bool ProjectSession::loadSampler(const juce::String& id, const juce::String& asset, double offset, double duration,
                               std::optional<Asset> imported)
{
    auto next = state;
    if (asset.isEmpty()) return false;
    if (imported) next.assets.push_back(*imported);
    for (auto& track : next.tracks) if (track.id == id && track.midi)
    { if (!track.sampler) track.samplerBypass = false; track.sampler = true; track.sample = asset; track.sampleOffset = offset; track.sampleDuration = duration;
      return edit(std::move(next), "Load sampler"); }
    return false;
}
bool ProjectSession::setRootNote(const juce::String& id, int note)
{
    auto next = state;
    for (auto& track : next.tracks) if (track.id == id && track.midi)
    { track.rootNote = note; return edit(std::move(next), "Sampler root note"); }
    return false;
}
bool ProjectSession::addSampler(const juce::String& id, bool createTrack)
{
    auto next = state;
    if (createTrack && !findId(next.tracks, id)) { Track track { id, numberedTrackName("MIDI", nextTrackNumber(next)) }; track.sends = {0, 0}; track.midi = true; track.monitor = 1; insertTrack(next, std::move(track)); }
    for (auto& track : next.tracks) if (track.id == id && track.midi)
    { if (!track.sampler) track.samplerBypass = false; track.sampler = true; return edit(std::move(next), "Add sampler instrument"); }
    return false;
}
bool ProjectSession::removeSampler(const juce::String& id)
{
    auto next = state;
    for (auto& track : next.tracks) if (track.id == id && track.sampler)
    { track.sampler = false; track.samplerBypass = false; track.sample.clear(); track.sampleOffset = track.sampleDuration = 0;
      track.rootNote = 60; track.samplerGain = 1; return edit(std::move(next), "Remove sampler instrument"); }
    return false;
}
bool ProjectSession::moveSampler(const juce::String& source, const juce::String& destination)
{
    auto next = state;
    auto from = std::find_if(next.tracks.begin(), next.tracks.end(), [&](const auto& track) { return track.id == source; });
    auto to = std::find_if(next.tracks.begin(), next.tracks.end(), [&](const auto& track) { return track.id == destination; });
    if (from == next.tracks.end() || to == next.tracks.end() || from == to || !from->sampler || !to->midi || to->sampler) return false;
    to->sampler = true; to->samplerBypass = from->samplerBypass; to->sample = from->sample; to->sampleOffset = from->sampleOffset;
    to->sampleDuration = from->sampleDuration; to->rootNote = from->rootNote; to->samplerGain = from->samplerGain;
    from->sampler = false; from->samplerBypass = false; from->sample.clear(); from->sampleOffset = from->sampleDuration = 0;
    from->rootNote = 60; from->samplerGain = 1;
    return edit(std::move(next), "Move sampler instrument");
}
bool ProjectSession::bypassSampler(const juce::String& id, bool bypass)
{ auto next = state; if (auto* track = findTrack(next, id)) if (track->sampler) { track->samplerBypass = bypass; return edit(std::move(next), "Sampler power"); } return false; }
bool ProjectSession::setSamplerGain(const juce::String& id, double gain)
{
    auto next = state;
    for (auto& track : next.tracks) if (track.id == id && track.sampler)
    { track.samplerGain = gain; return edit(std::move(next), "Sampler volume", false); }
    return false;
}

bool ProjectSession::renameTrack(const juce::String& id, const juce::String& name)
{
    auto next = state; for (auto& track : next.tracks) if (track.id == id)
    { track.name = name; return edit(std::move(next), "Rename track", false); }
    return false;
}

bool ProjectSession::deleteTrack(const juce::String& id)
{
    auto next = state;
    const auto* root = findId(state.tracks, id); if (!root) return false;
    juce::StringArray removed {id}; if (root->group) for (const auto& track : state.tracks) if (inGroup(state, track, id)) removed.add(track.id);
    for (auto& track : next.tracks) if (removed.contains(track.outputTrack)) track.outputTrack = "master";
    erase(next.tracks, [&](const auto& value) { return removed.contains(value.id); });
    erase(next.clips, [&](const auto& value) { return removed.contains(value.track); });
    return edit(std::move(next), "Delete track");
}

bool ProjectSession::addPlugin(const juce::String& id, Plugin plugin, bool createTrack)
{
    auto next = state;
    if (createTrack && !findId(next.tracks, id)) { Track track { id, numberedTrackName(plugin.instrument ? "MIDI" : "Audio", nextTrackNumber(next)) }; track.sends = {0, 0}; track.midi = plugin.instrument; track.monitor = plugin.instrument ? 1 : 2; insertTrack(next, std::move(track)); }
    if (auto* track = findTrack(next, id))
    { if (plugin.instrument) track->plugins.insert(track->plugins.begin(), std::move(plugin));
      else track->plugins.push_back(std::move(plugin)); return edit(std::move(next), "Add plug-in"); }
    return false;
}
bool ProjectSession::removePlugin(const juce::String& trackId, const juce::String& id)
{
    auto next = state;
    if (auto* track = findTrack(next, trackId))
    { erase(track->plugins, [&](const auto& plugin) { return plugin.id == id; }); return edit(std::move(next), "Remove plug-in"); }
    return false;
}
bool ProjectSession::movePlugin(const juce::String& source, const juce::String& destination, const juce::String& id)
{
    if (source == destination) return false;
    auto next = state; auto* from = findTrack(next, source); auto* to = findTrack(next, destination);
    if (!from || !to) return false;
    const auto* plugin = findId(from->plugins, id); if (!plugin) return false; const auto copy = *plugin;
    erase(from->plugins, [&](const auto& item) { return item.id == id; });
    if (copy.instrument) to->plugins.insert(to->plugins.begin(), copy); else to->plugins.push_back(copy);
    return edit(std::move(next), "Move plug-in");
}
bool ProjectSession::bypassPlugin(const juce::String& trackId, const juce::String& id, bool bypass)
{
    auto next = state;
    if (auto* track = findTrack(next, trackId)) for (auto& plugin : track->plugins) if (plugin.id == id)
    { plugin.bypass = bypass; return edit(std::move(next), "Plug-in bypass", false); }
    return false;
}
bool ProjectSession::pluginStates(const std::map<juce::String, juce::String>& states)
{
    auto next = state;
    for (auto* track : allTracks(next)) for (auto& plugin : track->plugins)
        if (const auto found = states.find(plugin.id); found != states.end()) plugin.state = found->second;
    return edit(std::move(next), "Plug-in settings", false);
}
bool ProjectSession::setSolo(const juce::String& id, bool solo)
{
    auto next = state;
    for (auto& track : next.tracks) if (track.id == id) { track.solo = solo; return edit(std::move(next), "Track solo", false); }
    return false;
}
bool ProjectSession::controls(const juce::String& id, double gain, bool mute)
{
    auto next = state; if (auto* track = findTrack(next, id))
    { track->gain = gain; track->mute = mute; return edit(std::move(next), "Track controls", false); }
    return false;
}

bool ProjectSession::routing(const juce::String& id, bool input, bool output, int monitor, int midiChannel, juce::String outputTrack)
{
    auto next = state; auto* track = findTrack(next, id); if (!track || id == "master") return false;
    track->inputEnabled = input; track->outputEnabled = output; track->monitor = monitor; track->midiChannel = midiChannel; track->outputTrack = std::move(outputTrack);
    return edit(std::move(next), "Track routing", false);
}
bool ProjectSession::setSend(const juce::String& id, int index, double gain)
{
    if (index < 0 || index >= maxReturns) return false;
    auto next = state; auto* track = findTrack(next, id); if (!track || id == "master") return false;
    track->sends.resize(static_cast<size_t>(std::max(2, returnCount(next))), 0);
    if (index >= static_cast<int>(track->sends.size())) return false;
    track->sends[static_cast<size_t>(index)] = gain; return edit(std::move(next), "Track send", false);
}
bool ProjectSession::setPan(const juce::String& id, double pan)
{ auto next = state; if (auto* track = findTrack(next, id)) { track->pan = pan; return edit(std::move(next), "Track pan", false); } return false; }
bool ProjectSession::setTrackAutomation(const juce::String& id, int parameter, std::vector<AutomationPoint> points)
{ if (parameter < 0 || parameter >= maxReturns + 3) return false; if (parameter < 2) return setAutomation(id, parameter == 1, std::move(points));
  auto next = state; auto* track = findTrack(next, id); if (!track || (id == "master" && parameter >= 3)) return false;
  if (parameter == 2) { for (auto& point : points) { point.value = point.value >= 0.5 ? 1 : 0; point.step = true; } track->muteAutomation = std::move(points); }
  else { track->sendAutomation.resize(std::max(track->sendAutomation.size(), static_cast<size_t>(parameter - 2))); track->sendAutomation[static_cast<size_t>(parameter - 3)] = std::move(points); }
  return edit(std::move(next), "Track automation", false); }
bool ProjectSession::setAutomation(const juce::String& id, bool pan, std::vector<AutomationPoint> points)
{ auto next = state; if (auto* track = findTrack(next, id)) { (pan ? track->panAutomation : track->volumeAutomation) = std::move(points); return edit(std::move(next), "Track automation", false); } return false; }

bool ProjectSession::setLoop(double start, double end, bool enabled)
{ auto next = state; next.loopStart = start; next.loopEnd = end; next.loopEnabled = enabled; return edit(std::move(next), "Arrangement loop"); }

bool ProjectSession::setGroove(double amount)
{ auto next = state; next.grooveAmount = amount; return edit(std::move(next), "Groove amount"); }
bool ProjectSession::setScale(int root, int mode, bool enabled)
{ auto next = state; next.scaleRoot = root; next.scaleMode = mode; next.scaleEnabled = enabled; return edit(std::move(next), "Scale", false); }
bool ProjectSession::setCue(double gain)
{ auto next = state; next.cueGain = gain; return edit(std::move(next), "Cue volume", false); }
bool ProjectSession::mapMidi(MidiMapping mapping)
{
    auto next = state;
    erase(next.midiMappings, [&](const auto& item) { return item.target == mapping.target; });
    next.midiMappings.push_back(std::move(mapping)); return edit(std::move(next), "MIDI mapping", false);
}
bool ProjectSession::removeMidiMapping(const juce::String& target)
{ auto next = state; erase(next.midiMappings, [&](const auto& item) { return item.target == target; }); return edit(std::move(next), "Remove MIDI mapping", false); }
bool ProjectSession::setMeter(int numerator, int denominator)
{ auto next = state; next.numerator = numerator; next.denominator = denominator; return edit(std::move(next), "Time signature", false); }
bool ProjectSession::addLocator(double time)
{
    auto next = state;
    for (const auto& locator : next.locators) if (std::abs(locator.time - time) < 0.001) return false;
    next.locators.push_back({time, juce::String(next.locators.size() + 1), {}});
    std::stable_sort(next.locators.begin(), next.locators.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return edit(std::move(next), "Add locator", false);
}
bool ProjectSession::setBpm(double bpm)
{ auto next = state; next.bpm = bpm; return edit(std::move(next), "BPM", false); }

bool ProjectSession::importAsset(Asset asset, std::optional<Clip> clip, bool createTrack)
{
    auto next = state;
    if (clip) if (const auto* parent = findId(state.tracks, clip->track)) if (parent->group)
    { Track child {newId(), numberedTrackName("Audio", nextTrackNumber(next))}; child.sends = {0, 0}; child.parentGroup = parent->id; clip->track = child.id; insertTrack(next, std::move(child)); }
    if (createTrack && clip) { Track track {clip->track, numberedTrackName("Audio", nextTrackNumber(next))}; track.sends = {0, 0}; insertTrack(next, std::move(track)); }
    orderTracks(next);
    next.assets.push_back(std::move(asset));
    if (clip) next.clips.push_back(*clip);
    return clip ? placeClips(std::move(next), { clip->id }, "Import audio") : edit(std::move(next), "Import audio");
}

bool ProjectSession::insertClip(Clip clip, bool createTrack)
{ Track track {clip.track, "Audio"}; track.sends = {0, 0}; return insertClips({clip}, createTrack ? std::vector<Track>{track} : std::vector<Track>()); }

bool ProjectSession::insertClips(const std::vector<Clip>& clips, const std::vector<Track>& tracks)
{
    auto next = state;
    for (auto track : tracks)
    { if (track.name == "Audio" || track.name == "MIDI") track.name = numberedTrackName(track.name, nextTrackNumber(next));
      insertTrack(next, std::move(track)); }
    for (auto clip : clips)
    { if (const auto* track = findId(next.tracks, clip.track)) if (track->midi && clip.loopLength == 0) clip.loopLength = std::max(0.001, clip.offset + clip.duration);
      next.clips.push_back(std::move(clip)); }
    orderTracks(next);
    juce::StringArray placed; for (const auto& clip : clips) placed.add(clip.id);
    return placeClips(std::move(next), placed, "Insert clips");
}

std::vector<Clip> ProjectSession::splitTimeSelection(const TimeSelection& selection)
{
    if (!std::isfinite(selection.start) || !std::isfinite(selection.end)
        || selection.start < 0 || selection.end <= selection.start) return {};
    for (const auto& track : selection.tracks) if (!findId(state.tracks, track)) return {};
    auto next = state; next.clips.clear(); std::vector<Clip> selected;
    for (const auto& clip : state.clips)
    {
        const auto middle = clipInTimeSelection(clip, selection);
        if (!middle) { next.clips.push_back(clip); continue; }
        if (middle->start > clip.start)
        { auto left = clip; left.id = newId(); left.duration = middle->start - clip.start; left.fadeIn = std::min(left.fadeIn, left.duration); left.fadeOut = std::min(left.fadeOut, left.duration); next.clips.push_back(left); }
        next.clips.push_back(*middle); selected.push_back(*middle);
        const auto end = middle->start + middle->duration, originalEnd = clip.start + clip.duration;
        if (end < originalEnd)
        { auto right = clip; right.id = newId(); right.start = end; right.offset += (end - clip.start) * clip.playbackRate;
          right.duration = originalEnd - end; right.fadeIn = std::min(right.fadeIn, right.duration); right.fadeOut = std::min(right.fadeOut, right.duration); next.clips.push_back(right); }
    }
    if (!edit(std::move(next), "Split time selection")) return {};
    return selected;
}

bool ProjectSession::changeClip(const Clip& clip)
{ return changeClips({ clip }); }

bool ProjectSession::changeClips(const std::vector<Clip>& clips, const std::vector<AutomationEdit>& automation)
{
    auto next = state;
    for (const auto& clip : clips)
    {
        auto found = std::find_if(next.clips.begin(), next.clips.end(), [&](const auto& value) { return value.id == clip.id; });
        if (found == next.clips.end()) return false;
        *found = clip;
    }
    for (const auto& edit : automation)
    {
        auto* track = findTrack(next, edit.track); if (!track || edit.parameter < 0 || edit.parameter >= 3 + returnCount(next)) return false;
        if (edit.parameter == 0) track->volumeAutomation = edit.points;
        else if (edit.parameter == 1) track->panAutomation = edit.points;
        else if (edit.parameter == 2) track->muteAutomation = edit.points;
        else { const auto index = static_cast<size_t>(edit.parameter - 3); if (track->sendAutomation.size() <= index) track->sendAutomation.resize(index + 1); track->sendAutomation[index] = edit.points; }
    }
    juce::StringArray placed; for (const auto& clip : clips) placed.add(clip.id);
    return placeClips(std::move(next), placed, "Move / trim clips");
}

bool ProjectSession::deleteClip(const juce::String& id)
{ return deleteClips({ id }); }

bool ProjectSession::deleteClips(const juce::StringArray& ids)
{
    if (ids.isEmpty()) return false;
    for (const auto& id : ids) if (!findId(state.clips, id)) return false;
    auto next = state; erase(next.clips, [&](const auto& clip) { return ids.contains(clip.id); });
    return edit(std::move(next), "Delete clips");
}

void ProjectSession::beginGesture()
{ if (!gesture) { gesture = state; gestureStructural = false; gesturePlacements.clear(); } }

void ProjectSession::endGesture(bool cancel)
{
    if (!gesture) return;
    auto before = std::move(*gesture); gesture.reset();
    if (state == before) return;
    auto after = state;
    if (!cancel) cutCovered(after, gesturePlacements);
    apply(std::move(before), gestureStructural);
    if (!cancel) { undo.beginNewTransaction("Edit gesture"); undo.perform(new Edit(*this, state, std::move(after), gestureStructural)); }
}

void ProjectSession::undoEdit()
{ if (!gesture) undo.undo(); }

void ProjectSession::redoEdit()
{ if (!gesture) undo.redo(); }

bool ProjectSession::edit(Project next, const juce::String& name, bool structural)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (validate(next).failed()) return false;
    if (next == state) return true;
    if (gesture) { gestureStructural = gestureStructural || structural; apply(std::move(next), structural); return true; }
    undo.beginNewTransaction(name);
    return undo.perform(new Edit(*this, state, std::move(next), structural));
}

bool ProjectSession::placeClips(Project next, const juce::StringArray& placed, const juce::String& name)
{
    if (validate(next).failed()) return false;
    if (gesture) for (const auto& id : placed) gesturePlacements.addIfNotAlreadyThere(id);
    else cutCovered(next, placed);
    return edit(std::move(next), name);
}

void ProjectSession::apply(Project next, bool structural)
{ state = std::move(next); ++revision; notify(structural); }

void ProjectSession::notify(bool structural)
{ if (onEdit) onEdit(snapshot(), structural); sendChangeMessage(); }
}
