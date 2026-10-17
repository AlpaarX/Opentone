// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/DemoProject.h"
#include "infrastructure/AbletonImport.h"

namespace opentone
{
LoadResult createDemoProject(const juce::File& directory)
{
    // The author's private-use-only project is installed locally, never bundled in releases.
    const auto source = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Opentone/Demo/House 4/House 4.als");
    auto result = loadAbletonSet(source, directory);
    if (result.result.failed()) return result;
    auto& project = result.project;
    const auto complete = project.assets.empty() && !project.clips.empty()
        && std::all_of(project.clips.begin(), project.clips.end(), [](const auto& clip) { return !clip.notes.empty(); });
    jassert(complete);
    if (!complete)
    { result.result = juce::Result::fail("House 4 must contain editable MIDI, without rendered audio. See README startup demo installation."); return result; }
    project.tracks.erase(std::remove_if(project.tracks.begin(), project.tracks.end(), [&](const auto& track)
        { return track.plugins.empty() && std::none_of(project.clips.begin(), project.clips.end(), [&](const auto& clip) { return clip.track == track.id; }); }), project.tracks.end());
    const std::array<const char*, 4> names {"Chords", "Bass", "Kick — Drum Rack", "Chord bus — sidechain"};
    for (size_t i = 0; i < project.tracks.size() && i < names.size(); ++i)
    { project.tracks[i].name = names[i]; project.tracks[i].height = 88; project.tracks[i].inputEnabled = false; }
    project.loopStart = 0; project.loopEnd = 0;
    for (const auto& clip : project.clips) project.loopEnd = std::max(project.loopEnd, clip.start + clip.duration);
    project.loopEnabled = true;
    project.importNotes = "House 4 — Keijiro Takahashi\n"
        "Source: https://github.com/keijiro/music/tree/master/Sketches/House%204%20Project\n"
        "Author's terms: Some rights reserved. For private use only. Project installed locally, not distributed with Opentone.\n"
        "Eight-bar MIDI sketch: synthesized chords, bass and kick. No rendered audio or external VST/AU devices.\n"
        "Native Operator, nested drum racks, Note Length, Dynamic Tube, Saturator and kick-sidechained Compressor.\n"
        "Two recorded Vengeance loop tracks are omitted: their samples are not in the public repository.\n"
        "Local adaptation repairs the source's kick pad routing (36 to 60 through the nested racks). Original ALS retained beside it.\n"
        "Opentone's original DSP implementations differ from Ableton; sound identity has not been verified.\n\n"
        + project.importNotes;
    result.result = validate(project);
    return result;
}
}
