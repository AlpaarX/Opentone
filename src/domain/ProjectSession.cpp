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
            if (placed.contains(clip.id) || clip.track != cover.track || clipEnd <= cover.start || clip.start >= end)
            { remaining.push_back(clip); continue; }
            const bool leftRemains = clip.start < cover.start;
            if (leftRemains)
            { auto left = clip; left.duration = cover.start - clip.start; remaining.push_back(left); }
            if (clipEnd > end)
            { auto right = clip; if (leftRemains) right.id = newId(); right.offset += end - clip.start;
              right.start = end; right.duration = clipEnd - end; remaining.push_back(right); }
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

ProjectSession::ProjectSession() = default;
ProjectSession::~ProjectSession() = default;

bool ProjectSession::replace(Project next, juce::File location, bool saved)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (validate(next).failed()) return false;
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

juce::String ProjectSession::addTrack(juce::String name, bool midi)
{
    auto id = newId(); auto next = state;
    Track track { id, std::move(name) }; track.midi = midi; next.tracks.push_back(track);
    edit(std::move(next), "Add track"); return id;
}

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
    if (createTrack && !findId(next.tracks, id)) { Track track { id, "MIDI" }; track.midi = true; next.tracks.push_back(std::move(track)); }
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
    if (!findId(next.tracks, id)) return false;
    erase(next.tracks, [&](const auto& value) { return value.id == id; });
    erase(next.clips, [&](const auto& value) { return value.track == id; });
    return edit(std::move(next), "Delete track");
}

bool ProjectSession::addPlugin(const juce::String& id, Plugin plugin, bool createTrack)
{
    auto next = state;
    if (createTrack && !findId(next.tracks, id)) { Track track { id, plugin.instrument ? "MIDI" : "Audio" }; track.midi = plugin.instrument; next.tracks.push_back(std::move(track)); }
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

bool ProjectSession::setPan(const juce::String& id, double pan)
{ auto next = state; if (auto* track = findTrack(next, id)) { track->pan = pan; return edit(std::move(next), "Track pan", false); } return false; }
bool ProjectSession::setAutomation(const juce::String& id, bool pan, std::vector<AutomationPoint> points)
{ auto next = state; if (auto* track = findTrack(next, id)) { (pan ? track->panAutomation : track->volumeAutomation) = std::move(points); return edit(std::move(next), "Track automation", false); } return false; }

bool ProjectSession::setLoop(double start, double end, bool enabled)
{ auto next = state; next.loopStart = start; next.loopEnd = end; next.loopEnabled = enabled; return edit(std::move(next), "Arrangement loop"); }

bool ProjectSession::setBpm(double bpm)
{ auto next = state; next.bpm = bpm; return edit(std::move(next), "BPM", false); }

bool ProjectSession::importAsset(Asset asset, std::optional<Clip> clip, bool createTrack)
{
    auto next = state;
    if (createTrack && clip) next.tracks.push_back({ clip->track, "Audio" });
    next.assets.push_back(std::move(asset));
    if (clip) next.clips.push_back(*clip);
    return clip ? placeClips(std::move(next), { clip->id }, "Import audio") : edit(std::move(next), "Import audio");
}

bool ProjectSession::insertClip(Clip clip, bool createTrack)
{ return insertClips({ clip }, createTrack ? std::vector<Track> { { clip.track, "Audio" } } : std::vector<Track>()); }

bool ProjectSession::insertClips(const std::vector<Clip>& clips, const std::vector<Track>& tracks)
{
    auto next = state;
    next.tracks.insert(next.tracks.end(), tracks.begin(), tracks.end());
    for (auto clip : clips)
    { if (const auto* track = findId(next.tracks, clip.track)) if (track->midi && clip.loopLength == 0) clip.loopLength = std::max(0.001, clip.offset + clip.duration);
      next.clips.push_back(std::move(clip)); }
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
        { auto left = clip; left.id = newId(); left.duration = middle->start - clip.start; next.clips.push_back(left); }
        next.clips.push_back(*middle); selected.push_back(*middle);
        const auto end = middle->start + middle->duration, originalEnd = clip.start + clip.duration;
        if (end < originalEnd)
        { auto right = clip; right.id = newId(); right.start = end; right.offset += end - clip.start;
          right.duration = originalEnd - end; next.clips.push_back(right); }
    }
    if (!edit(std::move(next), "Split time selection")) return {};
    return selected;
}

bool ProjectSession::changeClip(const Clip& clip)
{ return changeClips({ clip }); }

bool ProjectSession::changeClips(const std::vector<Clip>& clips)
{
    auto next = state;
    for (const auto& clip : clips)
    {
        auto found = std::find_if(next.clips.begin(), next.clips.end(), [&](const auto& value) { return value.id == clip.id; });
        if (found == next.clips.end()) return false;
        *found = clip;
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
