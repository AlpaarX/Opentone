// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <tuple>

namespace opentone
{
inline juce::String newId() { return juce::Uuid().toString(); }
struct Plugin
{
    juce::String id, name, description, state;
    bool instrument = false, bypass = false;
};
inline bool operator==(const Plugin& a, const Plugin& b)
{ return std::tie(a.id, a.name, a.description, a.state, a.instrument, a.bypass)
      == std::tie(b.id, b.name, b.description, b.state, b.instrument, b.bypass); }
struct AutomationPoint { double time = 0, value = 0; };
inline bool operator==(const AutomationPoint& a, const AutomationPoint& b) { return std::tie(a.time, a.value) == std::tie(b.time, b.value); }
inline double automationValue(const std::vector<AutomationPoint>& points, double time, double fallback)
{
    if (points.empty()) return fallback;
    const auto next = std::upper_bound(points.begin(), points.end(), time, [](double t, const auto& point) { return t < point.time; });
    if (next == points.begin()) return next->value;
    if (next == points.end()) return points.back().value;
    const auto& before = *(next - 1);
    return before.value + (next->value - before.value) * (time - before.time) / (next->time - before.time);
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
};
struct Asset
{
    juce::String id, path;
    double sampleRate = 0;
    int channels = 0;
    juce::int64 length = 0;
    bool missing = false;
};
struct MidiNote
{
    int pitch = 60; double start = 0, duration = 0.25, velocity = 0.8;
};
inline bool operator==(const MidiNote& a, const MidiNote& b)
{ return std::tie(a.pitch, a.start, a.duration, a.velocity) == std::tie(b.pitch, b.start, b.duration, b.velocity); }
struct Clip
{
    juce::String id, track, asset;
    double start = 0, offset = 0, duration = 0;
    std::vector<MidiNote> notes {};
    double loopStart = 0, loopLength = 0; bool loop = true;
};
struct TimeSelection
{
    double start = 0, end = 0;
    juce::StringArray tracks;
};
inline bool operator==(const TimeSelection& a, const TimeSelection& b)
{ return std::tie(a.start, a.end, a.tracks) == std::tie(b.start, b.end, b.tracks); }
struct Project
{
    double bpm = 120;
    std::vector<Track> tracks;
    std::vector<Asset> assets;
    std::vector<Clip> clips;
    Track master { "master", "Master" };
    double loopStart = 0, loopEnd = 2; bool loopEnabled = false;
};
inline bool operator==(const Track& a, const Track& b)
{ return std::tie(a.id, a.name, a.gain, a.mute, a.midi, a.sample, a.sampleOffset, a.sampleDuration, a.rootNote, a.sampler, a.samplerGain, a.solo, a.plugins, a.pan, a.volumeAutomation, a.panAutomation, a.samplerBypass)
      == std::tie(b.id, b.name, b.gain, b.mute, b.midi, b.sample, b.sampleOffset, b.sampleDuration, b.rootNote, b.sampler, b.samplerGain, b.solo, b.plugins, b.pan, b.volumeAutomation, b.panAutomation, b.samplerBypass); }
inline bool operator==(const Asset& a, const Asset& b)
{ return std::tie(a.id, a.path, a.sampleRate, a.channels, a.length, a.missing) == std::tie(b.id, b.path, b.sampleRate, b.channels, b.length, b.missing); }
inline bool operator==(const Clip& a, const Clip& b)
{ return std::tie(a.id, a.track, a.asset, a.start, a.offset, a.duration, a.notes, a.loopStart, a.loopLength, a.loop) == std::tie(b.id, b.track, b.asset, b.start, b.offset, b.duration, b.notes, b.loopStart, b.loopLength, b.loop); }
inline bool operator==(const Project& a, const Project& b)
{ return std::tie(a.bpm, a.tracks, a.assets, a.clips, a.master, a.loopStart, a.loopEnd, a.loopEnabled) == std::tie(b.bpm, b.tracks, b.assets, b.clips, b.master, b.loopStart, b.loopEnd, b.loopEnabled); }
template <typename T> const T* findId(const std::vector<T>& values, const juce::String& id)
{
    for (const auto& value : values) if (value.id == id) return &value;
    return nullptr;
}
inline const Track* findTrack(const Project& project, const juce::String& id)
{ return id == project.master.id ? &project.master : findId(project.tracks, id); }
inline Track* findTrack(Project& project, const juce::String& id)
{ if (id == project.master.id) return &project.master; for (auto& track : project.tracks) if (track.id == id) return &track; return nullptr; }
inline std::vector<const Track*> allTracks(const Project& project)
{ std::vector<const Track*> tracks; for (const auto& track : project.tracks) tracks.push_back(&track); tracks.push_back(&project.master); return tracks; }
inline std::vector<Track*> allTracks(Project& project)
{ std::vector<Track*> tracks; for (auto& track : project.tracks) tracks.push_back(&track); tracks.push_back(&project.master); return tracks; }
inline juce::Result validate(const Project& project)
{
    const auto nonnegative = [](double value) { return std::isfinite(value) && value >= 0; };
    if (!std::isfinite(project.bpm) || project.bpm <= 0)
        return juce::Result::fail("BPM must be positive and finite.");
    if (!nonnegative(project.loopStart) || !std::isfinite(project.loopEnd) || project.loopEnd - project.loopStart < 0.001
        || project.loopEnd > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1) return juce::Result::fail("Invalid arrangement loop.");
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
    for (const auto* item : allTracks(project))
    {
        const auto& track = *item;
        if (!std::isfinite(track.pan) || track.pan < -1 || track.pan > 1) return juce::Result::fail("Pan must be between -1 and 1.");
        for (const auto pan : {false, true})
        {
            const auto& points = pan ? track.panAutomation : track.volumeAutomation;
            if (points.size() > 4096) return juce::Result::fail("Automation exceeds 4096 points per envelope.");
            double previous = -1;
            for (const auto& point : points)
            {
                if (!nonnegative(point.time) || point.time <= previous
                    || point.time > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1 || !std::isfinite(point.value)
                    || point.value < (pan ? -1 : 0) || point.value > (pan ? 1 : 2)) return juce::Result::fail("Invalid automation envelope.");
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
        const auto* asset = findId(project.assets, clip.asset);
        const auto* track = findId(project.tracks, clip.track);
        if (track == nullptr || (!track->midi && asset == nullptr) || (track->midi && clip.asset.isNotEmpty())
            || !nonnegative(clip.start) || !nonnegative(clip.offset)
            || !std::isfinite(clip.duration) || clip.duration <= 0
            || !std::isfinite(clip.start + clip.duration)
            || (clip.start + clip.duration) > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1
            || (asset && !asset->missing && clip.offset + clip.duration
                  > static_cast<double>(asset->length) / asset->sampleRate + 1.0e-9))
            return juce::Result::fail("Invalid clip reference or source range.");
        if (!nonnegative(clip.loopStart) || !nonnegative(clip.loopLength)
            || !std::isfinite(clip.loopStart + clip.loopLength) || !std::isfinite(clip.offset + clip.duration)
            || (clip.loopLength > 0 && (!track->midi || clip.loopLength < 0.001))
            || clip.loopStart + clip.loopLength > static_cast<double>(std::numeric_limits<juce::int64>::max()) / 1000000.0 - 1)
            return juce::Result::fail("Invalid MIDI loop range.");
        if ((!track->midi && !clip.notes.empty()) || clip.notes.size() > 512)
            return juce::Result::fail("Invalid MIDI clip or more than 512 notes.");
        for (const auto& note : clip.notes)
            if (note.pitch < 0 || note.pitch > 127 || !nonnegative(note.start) || !std::isfinite(note.duration)
                || note.duration <= 0 || !std::isfinite(note.start + note.duration)
                || !nonnegative(note.velocity) || note.velocity > 1)
                return juce::Result::fail("Invalid MIDI note.");
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
