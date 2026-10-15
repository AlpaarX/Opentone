// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <functional>
#include <optional>
#include <array>
#include <set>
#include <tuple>

namespace opentone
{
constexpr size_t maxMidiNotes = 4096;
constexpr int maxReturns = 32;
constexpr double maxSendGain = 1.9952623149688795; // +6 dB.
inline juce::String newId() { return juce::Uuid().toString(); }
inline juce::String numberedTrackName(const juce::String& name, int number)
{
    jassert(number > 0);
    const auto prefix = name.upToFirstOccurrenceOf(" ", false, false);
    if (name.containsChar(' ') && prefix.isNotEmpty() && prefix.containsOnly("0123456789")) return name;
    return juce::String(number) + " " + name;
}
struct Plugin
{
    juce::String id, name, description, state;
    bool instrument = false, bypass = false;
};
inline bool operator==(const Plugin& a, const Plugin& b)
{ return std::tie(a.id, a.name, a.description, a.state, a.instrument, a.bypass)
      == std::tie(b.id, b.name, b.description, b.state, b.instrument, b.bypass); }
struct AutomationPoint { double time = 0, value = 0; double x1 = 1.0 / 3, y1 = 1.0 / 3, x2 = 2.0 / 3, y2 = 2.0 / 3; bool step = false; };
inline bool operator==(const AutomationPoint& a, const AutomationPoint& b) { return std::tie(a.time, a.value, a.x1, a.y1, a.x2, a.y2, a.step) == std::tie(b.time, b.value, b.x1, b.y1, b.x2, b.y2, b.step); }
inline double automationValue(const std::vector<AutomationPoint>& points, double time, double fallback)
{
    if (points.empty()) return fallback;
    const auto next = std::upper_bound(points.begin(), points.end(), time, [](double t, const auto& point) { return t < point.time; });
    if (next == points.begin()) return next->value;
    if (next == points.end()) return points.back().value;
    const auto& before = *(next - 1);
    if (before.step) return before.value;
    const auto fraction = (time - before.time) / (next->time - before.time);
    if (std::abs(before.x1 - before.y1) < 1e-12 && std::abs(before.x2 - before.y2) < 1e-12) return before.value + (next->value - before.value) * fraction;
    const auto cubic = [](double t, double a, double b) { const auto u = 1 - t; return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t; };
    double low = 0, high = 1;
    for (int i = 0; i < 20; ++i) { const auto t = (low + high) / 2; if (cubic(t, before.x1, before.x2) < fraction) low = t; else high = t; }
    const auto phase = cubic((low + high) / 2, before.y1, before.y2); jassert(std::isfinite(phase));
    return before.value + (next->value - before.value) * phase;
}
inline float panGain(double pan, int channel) { return static_cast<float>(channel == 0 ? 1 - std::max(0.0, pan) : 1 + std::min(0.0, pan)); }
struct Track
{
    juce::String id, name; double gain = 1.0; bool mute = false;
    bool midi = false; juce::String sample {};
    double sampleOffset = 0, sampleDuration = 0; int rootNote = 60;
    bool sampler = false;
    double samplerGain = 1; bool solo = false;
    std::vector<Plugin> plugins {};
    double pan = 0;
    std::vector<AutomationPoint> volumeAutomation {}, panAutomation {};
    bool samplerBypass = false;
    bool inputEnabled = true, outputEnabled = true;
    int monitor = 2, midiChannel = 0; // In, Auto, Off; zero means all MIDI channels.
    int returnIndex = -1;
    std::vector<double> sends {0, 0};
    bool group = false, groupCollapsed = false;
    juce::String parentGroup {};
    int color = -1;
    std::vector<AutomationPoint> muteAutomation {}; std::vector<std::vector<AutomationPoint>> sendAutomation {};
    int height = 88; bool folded = false; juce::String annotation {}; bool armed = false; juce::String outputTrack {};
};
struct Asset
{
    juce::String id, path;
    double sampleRate = 0;
    int channels = 0;
    juce::int64 length = 0;
    bool missing = false;
};
struct NoteExpression { int controller = -1; std::vector<AutomationPoint> points; };
inline bool operator==(const NoteExpression& a, const NoteExpression& b) { return a.controller == b.controller && a.points == b.points; }
struct MidiNote
{
    int pitch = 60; double start = 0, duration = 0.25, velocity = 0.8;
    bool enabled = true; double probability = 1, velocityDeviation = 0, releaseVelocity = 64.0 / 127;
    int sourceId = -1, channel = 1, bendRange = 48; std::vector<NoteExpression> expression {};
};
inline bool operator==(const MidiNote& a, const MidiNote& b)
{ return std::tie(a.pitch, a.start, a.duration, a.velocity, a.enabled, a.probability, a.velocityDeviation, a.releaseVelocity, a.sourceId, a.channel, a.bendRange, a.expression) == std::tie(b.pitch, b.start, b.duration, b.velocity, b.enabled, b.probability, b.velocityDeviation, b.releaseVelocity, b.sourceId, b.channel, b.bendRange, b.expression); }
struct Clip
{
    juce::String id, track, asset;
    double start = 0, offset = 0, duration = 0;
    std::vector<MidiNote> notes {};
    double loopStart = 0, loopLength = 0; bool loop = true;
    juce::String name {}; double gain = 1;
    int color = -1; bool enabled = true; juce::String annotation {};
    double playbackRate = 1, fadeIn = 0, fadeOut = 0;
    int scene = -1; bool sessionLoop = false; double sessionLength = 0;
};
struct TimeSelection
{
    double start = 0, end = 0;
    juce::StringArray tracks;
};
inline bool operator==(const TimeSelection& a, const TimeSelection& b)
{ return std::tie(a.start, a.end, a.tracks) == std::tie(b.start, b.end, b.tracks); }
// Retain legacy v11 Session data on save; the UI and playback use Arrangement only.
struct Scene { juce::String name; int color = -1; };
inline bool operator==(const Scene& a, const Scene& b) { return std::tie(a.name, a.color) == std::tie(b.name, b.color); }
struct Locator
{
    double time = 0; juce::String name, annotation;
};
inline bool operator==(const Locator& a, const Locator& b)
{ return std::tie(a.time, a.name, a.annotation) == std::tie(b.time, b.name, b.annotation); }
struct AutomationEdit { juce::String track; int parameter = 0; std::vector<AutomationPoint> points; };
struct MidiMapping
{
    juce::String source, target; int channel = 1, number = 0; bool note = false;
};
inline bool operator==(const MidiMapping& a, const MidiMapping& b)
{ return std::tie(a.source, a.target, a.channel, a.number, a.note) == std::tie(b.source, b.target, b.channel, b.number, b.note); }
struct Project
{
    double bpm = 120, cueGain = 1, grooveAmount = 0;
    int scaleRoot = 0, scaleMode = 0; bool scaleEnabled = false;
    std::vector<MidiMapping> midiMappings {};
    std::vector<Track> tracks;
    std::vector<Asset> assets;
    std::vector<Clip> clips;
    Track master { "master", "Master" };
    double loopStart = 0, loopEnd = 2; bool loopEnabled = false;
    juce::String importNotes {};
    int numerator = 4, denominator = 4; std::vector<Locator> locators {}; std::vector<Scene> scenes {};
};
inline bool operator==(const Track& a, const Track& b)
{ return std::tie(a.id, a.name, a.gain, a.mute, a.midi, a.sample, a.sampleOffset, a.sampleDuration, a.rootNote, a.sampler, a.samplerGain, a.solo, a.plugins, a.pan, a.volumeAutomation, a.panAutomation, a.samplerBypass, a.inputEnabled, a.outputEnabled, a.monitor, a.midiChannel, a.returnIndex, a.sends, a.group, a.groupCollapsed, a.parentGroup, a.color, a.height, a.folded, a.annotation, a.armed, a.muteAutomation, a.sendAutomation, a.outputTrack)
      == std::tie(b.id, b.name, b.gain, b.mute, b.midi, b.sample, b.sampleOffset, b.sampleDuration, b.rootNote, b.sampler, b.samplerGain, b.solo, b.plugins, b.pan, b.volumeAutomation, b.panAutomation, b.samplerBypass, b.inputEnabled, b.outputEnabled, b.monitor, b.midiChannel, b.returnIndex, b.sends, b.group, b.groupCollapsed, b.parentGroup, b.color, b.height, b.folded, b.annotation, b.armed, b.muteAutomation, b.sendAutomation, b.outputTrack); }
inline bool operator==(const Asset& a, const Asset& b)
{ return std::tie(a.id, a.path, a.sampleRate, a.channels, a.length, a.missing) == std::tie(b.id, b.path, b.sampleRate, b.channels, b.length, b.missing); }
inline bool operator==(const Clip& a, const Clip& b)
{ return std::tie(a.id, a.track, a.asset, a.start, a.offset, a.duration, a.notes, a.loopStart, a.loopLength, a.loop, a.name, a.gain, a.color, a.enabled, a.annotation, a.playbackRate, a.fadeIn, a.fadeOut, a.scene, a.sessionLoop, a.sessionLength) == std::tie(b.id, b.track, b.asset, b.start, b.offset, b.duration, b.notes, b.loopStart, b.loopLength, b.loop, b.name, b.gain, b.color, b.enabled, b.annotation, b.playbackRate, b.fadeIn, b.fadeOut, b.scene, b.sessionLoop, b.sessionLength); }
inline bool operator==(const Project& a, const Project& b)
{ return std::tie(a.bpm, a.cueGain, a.grooveAmount, a.scaleRoot, a.scaleMode, a.scaleEnabled, a.midiMappings, a.tracks, a.assets, a.clips, a.master, a.loopStart, a.loopEnd, a.loopEnabled, a.numerator, a.denominator, a.locators, a.scenes) == std::tie(b.bpm, b.cueGain, b.grooveAmount, b.scaleRoot, b.scaleMode, b.scaleEnabled, b.midiMappings, b.tracks, b.assets, b.clips, b.master, b.loopStart, b.loopEnd, b.loopEnabled, b.numerator, b.denominator, b.locators, b.scenes); }
template <typename T> const T* findId(const std::vector<T>& values, const juce::String& id)
{
    for (const auto& value : values) if (value.id == id) return &value;
    return nullptr;
}
inline const Track* findTrack(const Project& project, const juce::String& id)
{ return id == project.master.id ? &project.master : findId(project.tracks, id); }
inline Track* findTrack(Project& project, const juce::String& id)
{ if (id == project.master.id) return &project.master; for (auto& track : project.tracks) if (track.id == id) return &track; return nullptr; }
inline const std::vector<AutomationPoint>& automationPoints(const Track& track, int parameter)
{
    if (parameter == 0) return track.volumeAutomation; if (parameter == 1) return track.panAutomation; if (parameter == 2) return track.muteAutomation;
    static const std::vector<AutomationPoint> empty; const auto index = parameter - 3;
    return index >= 0 && static_cast<size_t>(index) < track.sendAutomation.size() ? track.sendAutomation[static_cast<size_t>(index)] : empty;
}
inline std::pair<double, double> automationLimits(int parameter)
{ return parameter == 1 ? std::pair<double, double>{-1, 1} : std::pair<double, double>{0, parameter == 2 ? 1.0 : parameter >= 3 ? maxSendGain : 2.0}; }
inline double automationFallback(const Track& track, int parameter)
{ if (parameter == 0) return track.gain; if (parameter == 1) return track.pan; if (parameter == 2) return track.mute ? 1 : 0;
  const auto index = parameter - 3; return index >= 0 && static_cast<size_t>(index) < track.sends.size() ? track.sends[static_cast<size_t>(index)] : 0; }
inline juce::String outputDestination(const Track& track)
{ return track.outputTrack == "master" ? juce::String() : track.outputTrack.isEmpty() ? track.parentGroup : track.outputTrack; }
inline bool routesTo(const Project& project, const Track& track, const juce::String& id)
{ auto target = outputDestination(track); for (int depth = 0; depth < 64 && target.isNotEmpty(); ++depth)
  { if (target == id) return true; const auto* next = findTrack(project, target); if (!next) break; target = outputDestination(*next); } return false; }
inline int routingDepth(const Project& project, const Track& track)
{ int depth = 0; auto target = outputDestination(track); while (depth < 64 && target.isNotEmpty())
  { ++depth; const auto* next = findTrack(project, target); if (!next) break; target = outputDestination(*next); } return depth; }
inline int returnCount(const Project& project)
{ int count = 0; for (const auto& track : project.tracks) count = std::max(count, track.returnIndex + 1); return count; }
inline std::vector<const Track*> allTracks(const Project& project)
{ std::vector<const Track*> tracks; for (const auto& track : project.tracks) tracks.push_back(&track); tracks.push_back(&project.master); return tracks; }
inline std::vector<Track*> allTracks(Project& project)
{ std::vector<Track*> tracks; for (auto& track : project.tracks) tracks.push_back(&track); tracks.push_back(&project.master); return tracks; }
inline bool inGroup(const Project& project, const Track& track, const juce::String& id)
{
    auto parent = track.parentGroup;
    for (int depth = 0; depth < 64 && parent.isNotEmpty(); ++depth)
    { if (parent == id) return true; const auto* group = findId(project.tracks, parent); if (!group) break; parent = group->parentGroup; }
    return false;
}
inline int groupDepth(const Project& project, const Track& track)
{
    int depth = 0; auto parent = track.parentGroup;
    while (depth < 64 && parent.isNotEmpty())
    { const auto* group = findId(project.tracks, parent); if (!group) break; ++depth; parent = group->parentGroup; }
    return depth;
}
inline bool hiddenByGroup(const Project& project, const Track& track)
{
    auto parent = track.parentGroup;
    for (int depth = 0; depth < 64 && parent.isNotEmpty(); ++depth)
    { const auto* group = findId(project.tracks, parent); if (!group) break; if (group->groupCollapsed) return true; parent = group->parentGroup; }
    return false;
}
inline bool mutedByGroup(const Project& project, const Track& track)
{
    auto parent = track.parentGroup;
    for (int depth = 0; depth < 64 && parent.isNotEmpty(); ++depth)
    { const auto* group = findId(project.tracks, parent); if (!group) break; if (group->mute && group->muteAutomation.empty()) return true; parent = group->parentGroup; }
    return false;
}
inline void orderTracks(Project& project)
{
    const auto original = project.tracks; std::vector<Track> ordered; ordered.reserve(original.size());
    const auto visit = [&](const auto& self, const juce::String& parent, int depth) -> void
    { if (depth > 64) return; for (const auto& track : original) if (track.returnIndex < 0 && track.parentGroup == parent)
      { ordered.push_back(track); if (track.group) self(self, track.id, depth + 1); } };
    visit(visit, {}, 0);
    for (const auto& track : original) if (track.returnIndex >= 0) ordered.push_back(track);
    if (ordered.size() == original.size()) project.tracks = std::move(ordered);
}
inline juce::Result validate(const Project& project)
{
    if (project.importNotes.length() > 1024 * 1024) return juce::Result::fail("Import notes exceed 1 MB.");
    const auto nonnegative = [](double value) { return std::isfinite(value) && value >= 0; };
    if (!std::isfinite(project.bpm) || project.bpm <= 0)
        return juce::Result::fail("BPM must be positive and finite.");
    if (!nonnegative(project.loopStart) || !std::isfinite(project.loopEnd) || project.loopEnd - project.loopStart < 0.001
        || project.loopEnd > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1) return juce::Result::fail("Invalid arrangement loop.");
    if (project.numerator < 1 || project.numerator > 99 || project.denominator < 1 || project.denominator > 64
        || (project.denominator & (project.denominator - 1)) != 0 || project.locators.size() > 4096) return juce::Result::fail("Invalid meter or locators.");
    if (project.scenes.size() > 512) return juce::Result::fail("More than 512 scenes.");
    for (const auto& scene : project.scenes) if (scene.color < -1 || scene.color > 0xffffff || scene.name.length() > 65536) return juce::Result::fail("Invalid scene.");
    for (const auto& locator : project.locators)
        if (!nonnegative(locator.time) || locator.time > 1.0e9 || locator.name.length() > 65536 || locator.annotation.length() > 65536) return juce::Result::fail("Invalid locator.");
    if (!std::isfinite(project.grooveAmount) || project.grooveAmount < 0 || project.grooveAmount > 1 || project.scaleRoot < 0 || project.scaleRoot > 11 || project.scaleMode < 0 || project.scaleMode > 1) return juce::Result::fail("Invalid groove or scale.");
    if (!std::isfinite(project.cueGain) || project.cueGain < 0 || project.cueGain > 2 || project.midiMappings.size() > 4096) return juce::Result::fail("Invalid cue level or MIDI mappings.");
    std::set<juce::String> mappedTargets;
    for (const auto& mapping : project.midiMappings)
        if (mapping.source.isEmpty() || mapping.source.length() > 512 || mapping.target.isEmpty() || mapping.target.length() > 512
            || mapping.channel < 1 || mapping.channel > 16 || mapping.number < 0 || mapping.number > 127
            || !mappedTargets.insert(mapping.target).second) return juce::Result::fail("Invalid MIDI mapping.");
    std::set<juce::String> ids;
    const auto unique = [&ids](const auto& values)
    {
        for (const auto& value : values)
            if (value.id.isEmpty() || !ids.insert(value.id).second) return false;
        return true;
    };
    if (!unique(project.tracks) || !unique(project.assets) || !unique(project.clips))
        return juce::Result::fail("Project IDs must be nonempty and unique.");
    if (project.master.id != "master" || project.master.midi || project.master.sampler || project.master.sample.isNotEmpty()
        || !ids.insert(project.master.id).second) return juce::Result::fail("Invalid master track.");
    std::array<bool, maxReturns> returns {};
    for (const auto* item : allTracks(project))
    {
        const auto& track = *item;
        if (track.group && (track.id == "master" || track.midi || track.sampler || track.inputEnabled || track.returnIndex >= 0)) return juce::Result::fail("Invalid group track.");
        if (track.parentGroup.isNotEmpty())
        {
            if (track.id == "master" || track.returnIndex >= 0) return juce::Result::fail("Master/returns cannot belong to a group.");
            auto parent = track.parentGroup; int depth = 0;
            while (parent.isNotEmpty())
            { const auto* group = findId(project.tracks, parent); if (!group || !group->group || parent == track.id || ++depth > 64) return juce::Result::fail("Invalid or cyclic group hierarchy."); parent = group->parentGroup; }
        }
        if (track.outputTrack.isNotEmpty() && track.outputTrack != "master")
        { const auto* target = findTrack(project, track.outputTrack);
          if (!target || target->midi || track.id == "master" || track.returnIndex >= 0) return juce::Result::fail("Invalid audio output destination."); }
        if (routesTo(project, track, track.id) || routingDepth(project, track) >= 64) return juce::Result::fail("Audio routing forms a cycle or is too deep.");
        if (track.returnIndex < -1 || track.returnIndex >= maxReturns) return juce::Result::fail("Invalid return channel.");
        if (track.returnIndex >= 0)
        { if (track.id == "master" || track.midi || track.inputEnabled || returns[static_cast<size_t>(track.returnIndex)]) return juce::Result::fail("Invalid or duplicate return channel.");
          returns[static_cast<size_t>(track.returnIndex)] = true; }
        if (track.armed && (track.id == "master" || track.group || track.returnIndex >= 0)) return juce::Result::fail("Invalid armed track.");
        if (track.height < 17 || track.height > 1000 || track.annotation.length() > 65536) return juce::Result::fail("Invalid track layout or annotation.");
        if (track.color < -1 || track.color > 0xffffff) return juce::Result::fail("Invalid track color.");
        if (track.sends.size() < 2 || track.sends.size() > maxReturns) return juce::Result::fail("Invalid send count.");
        for (const auto send : track.sends) if (!std::isfinite(send) || send < 0 || send > maxSendGain || (track.id == "master" && send > 0)) return juce::Result::fail("Invalid send level.");
        if (track.monitor < 0 || track.monitor > 2 || track.midiChannel < 0 || track.midiChannel > 16) return juce::Result::fail("Invalid track routing.");
        if (!std::isfinite(track.pan) || track.pan < -1 || track.pan > 1) return juce::Result::fail("Pan must be between -1 and 1.");
        if (track.sendAutomation.size() > maxReturns) return juce::Result::fail("Too many send automation envelopes.");
        for (int parameter = 0; parameter < 3 + static_cast<int>(track.sendAutomation.size()); ++parameter)
        {
            const auto& points = automationPoints(track, parameter); const auto limits = automationLimits(parameter);
            if (points.size() > 4096) return juce::Result::fail("Automation exceeds 4096 points per envelope.");
            double previous = -1;
            for (const auto& point : points)
            {
                if (!nonnegative(point.time) || point.time <= previous || point.time > 1.0e9 || !std::isfinite(point.value)
                    || point.value < limits.first || point.value > limits.second || (track.id == "master" && parameter >= 3 && point.value > 0)) return juce::Result::fail("Invalid automation envelope.");
                for (const auto control : {point.x1, point.x2}) if (!nonnegative(control) || control > 1) return juce::Result::fail("Invalid automation curve.");
                if (!std::isfinite(point.y1) || !std::isfinite(point.y2) || std::abs(point.y1) > 64 || std::abs(point.y2) > 64) return juce::Result::fail("Invalid automation curve height.");
                previous = point.time;
            }
        }
        int instruments = track.sampler ? 1 : 0;
        for (size_t index = 0; index < track.plugins.size(); ++index)
        {
            const auto& plugin = track.plugins[index];
            if (plugin.id.isEmpty() || !ids.insert(plugin.id).second || plugin.description.isEmpty()
                || plugin.description.length() > 65536 || plugin.state.length() > 16 * 1024 * 1024)
                return juce::Result::fail("Invalid plug-in metadata or oversized plug-in state.");
            if (plugin.instrument && (!track.midi || index != 0 || ++instruments > 1))
                return juce::Result::fail("Only one instrument can be placed first on a MIDI track.");
        }
        if (track.sampler && !track.midi) return juce::Result::fail("Instruments require a MIDI track.");
        if (!nonnegative(track.samplerGain) || track.samplerGain > 2) return juce::Result::fail("Sampler gain must be between zero and two.");
        if (!nonnegative(track.gain) || track.gain > 2) return juce::Result::fail("Track gain must be between zero and two.");
        if (track.rootNote < 0 || track.rootNote > 127 || !nonnegative(track.sampleOffset) || !nonnegative(track.sampleDuration))
            return juce::Result::fail("Invalid sampler settings.");
        if (track.sample.isNotEmpty())
        {
            const auto* sample = findId(project.assets, track.sample);
            if (!track.midi || !sample || track.sampleDuration <= 0
                || (!sample->missing && track.sampleOffset + track.sampleDuration > static_cast<double>(sample->length) / sample->sampleRate + 1.0e-9))
                return juce::Result::fail("Invalid sampler source range.");
        }
    }
    for (const auto& asset : project.assets)
        if (asset.path.isEmpty() || !std::isfinite(asset.sampleRate) || asset.sampleRate <= 0
            || asset.sampleRate > 1000000 || asset.channels < 1 || asset.channels > 2 || asset.length <= 0)
            return juce::Result::fail("Invalid asset metadata.");
    for (const auto& clip : project.clips)
    {
        if (clip.scene < -1 || clip.scene >= static_cast<int>(project.scenes.size()) || !nonnegative(clip.sessionLength) || clip.sessionLength > 1.0e9 || (clip.scene >= 0 && clip.sessionLength < 0.001)) return juce::Result::fail("Invalid session clip.");
        if (!std::isfinite(clip.playbackRate) || clip.playbackRate < 1.0 / 256 || clip.playbackRate > 256
            || !nonnegative(clip.fadeIn) || !nonnegative(clip.fadeOut) || clip.fadeIn > clip.duration || clip.fadeOut > clip.duration) return juce::Result::fail("Invalid clip playback rate/fades.");
        if (clip.color < -1 || clip.color > 0xffffff || clip.annotation.length() > 65536) return juce::Result::fail("Invalid clip color or annotation.");
        const auto* asset = findId(project.assets, clip.asset);
        const auto* track = findId(project.tracks, clip.track);
        if (track == nullptr || track->returnIndex >= 0 || track->group || (!track->midi && asset == nullptr) || (track->midi && clip.asset.isNotEmpty())
            || !std::isfinite(clip.gain) || clip.gain < 0 || clip.gain > 8
            || !nonnegative(clip.start) || !nonnegative(clip.offset)
            || !std::isfinite(clip.duration) || clip.duration <= 0
            || !std::isfinite(clip.start + clip.duration)
            || (clip.start + clip.duration) > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1
            || (asset && !asset->missing && clip.offset + clip.duration * clip.playbackRate
                  > static_cast<double>(asset->length) / asset->sampleRate + 1.0e-9))
            return juce::Result::fail("Invalid clip reference or source range.");
        if (!nonnegative(clip.loopStart) || !nonnegative(clip.loopLength)
            || !std::isfinite(clip.loopStart + clip.loopLength) || !std::isfinite(clip.offset + clip.duration)
            || (clip.loopLength > 0 && (!track->midi || clip.loopLength < 0.001))
            || clip.loopStart + clip.loopLength > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1)
            return juce::Result::fail("Invalid MIDI loop range.");
        if ((!track->midi && !clip.notes.empty()) || clip.notes.size() > maxMidiNotes)
            return juce::Result::fail("Invalid MIDI clip or more than 4096 notes.");
        for (const auto& note : clip.notes)
        {
            if (note.pitch < 0 || note.pitch > 127 || !nonnegative(note.start) || !std::isfinite(note.duration)
                || note.duration <= 0 || !std::isfinite(note.start + note.duration)
                || !nonnegative(note.velocity) || note.velocity > 1 || !nonnegative(note.probability) || note.probability > 1
                || !std::isfinite(note.velocityDeviation) || std::abs(note.velocityDeviation) > 1 || !nonnegative(note.releaseVelocity) || note.releaseVelocity > 1)
                return juce::Result::fail("Invalid MIDI note.");
            if (note.channel < 1 || note.channel > 16 || note.bendRange < 1 || note.bendRange > 127 || note.expression.size() > 16) return juce::Result::fail("Invalid expressive note routing.");
            for (const auto& expression : note.expression)
            { if (expression.controller < -2 || expression.controller > 127 || expression.points.size() > 4096) return juce::Result::fail("Invalid note expression."); double previous = -1;
              for (const auto& point : expression.points)
              { if (!nonnegative(point.time) || point.time <= previous || point.time > 1.0e9 || !std::isfinite(point.value) || std::abs(point.value) > 12700) return juce::Result::fail("Invalid note expression point.");
                for (const auto x : {point.x1, point.x2}) if (!nonnegative(x) || x > 1) return juce::Result::fail("Invalid expression curve.");
                for (const auto y : {point.y1, point.y2}) if (!std::isfinite(y) || std::abs(y) > 64) return juce::Result::fail("Invalid expression curve height."); previous = point.time; } }
        }
    }
    return juce::Result::ok();
}
struct Snapshot
{
    Project project;
    juce::File directory;
    uint64_t generation = 0, revision = 0;
};

}
