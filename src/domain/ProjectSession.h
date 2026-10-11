// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "domain/Project.h"
#include "shared/RuntimeExports.h"
#include <juce_data_structures/juce_data_structures.h>
#include <map>

namespace opentone
{
// Message-thread owner. UI edits use operations; snapshots alone cross to workers.
class OPENTONE_API ProjectSession : public juce::ChangeBroadcaster
{
public:
    ProjectSession();
    ~ProjectSession() override;
    const Project& project() const { return state; }
    Snapshot snapshot() const { return { state, directory, generation, revision }; }
    const juce::File& location() const { return saveLocation; }
    const juce::File& mediaDirectory() const { return directory; }
    bool dirty() const { return revision != savedRevision; }
    bool gesturing() const { return gesture.has_value(); }
    std::function<void(const Snapshot&, bool)> onEdit;

    bool replace(Project next, juce::File location, bool saved);
    void saved(const Snapshot& captured, const juce::File& location);
    juce::String addTrack(juce::String name = "Audio", bool midi = false);
    bool loadSampler(const juce::String& track, const juce::String& asset, double offset, double duration,
                     std::optional<Asset> imported = {});
    bool setRootNote(const juce::String& track, int note);
    bool addSampler(const juce::String& track, bool createTrack = false);
    bool removeSampler(const juce::String& track);
    bool moveSampler(const juce::String& source, const juce::String& destination);
    bool bypassSampler(const juce::String&, bool);
    bool setSamplerGain(const juce::String& track, double gain);
    bool renameTrack(const juce::String& id, const juce::String& name);
    bool deleteTrack(const juce::String& id);
    bool addPlugin(const juce::String&, Plugin, bool createTrack = false);
    bool removePlugin(const juce::String&, const juce::String&);
    bool movePlugin(const juce::String&, const juce::String&, const juce::String&);
    bool bypassPlugin(const juce::String&, const juce::String&, bool);
    bool pluginStates(const std::map<juce::String, juce::String>&);
    bool setSolo(const juce::String&, bool);
    bool controls(const juce::String& id, double gain, bool mute);
    bool setBpm(double bpm);
    bool setLoop(double start, double end, bool enabled);
    bool setPan(const juce::String&, double);
    bool setAutomation(const juce::String&, bool pan, std::vector<AutomationPoint>);
    bool importAsset(Asset asset, std::optional<Clip> clip = {}, bool createTrack = false);
    bool insertClip(Clip clip, bool createTrack = false);
    bool insertClips(const std::vector<Clip>& clips, const std::vector<Track>& tracks = {});
    std::vector<Clip> splitTimeSelection(const TimeSelection&);
    bool changeClip(const Clip& clip);
    bool changeClips(const std::vector<Clip>& clips);
    bool deleteClip(const juce::String& id);
    bool deleteClips(const juce::StringArray& ids);
    void beginGesture();
    void endGesture(bool cancel);
    void undoEdit();
    void redoEdit();

private:
    template <typename T, typename Predicate> static void erase(std::vector<T>& values, Predicate predicate)
    { values.erase(std::remove_if(values.begin(), values.end(), predicate), values.end()); }
    struct Edit;
    bool edit(Project next, const juce::String& name, bool structural = true);
    bool placeClips(Project next, const juce::StringArray& placed, const juce::String& name);
    void apply(Project next, bool structural);
    void notify(bool structural);
    Project state;
    juce::File directory;
    juce::File saveLocation;
    juce::UndoManager undo { 10000 };
    std::optional<Project> gesture;
    juce::StringArray gesturePlacements;
    bool gestureStructural = false;
    uint64_t generation = 1, revision = 0, savedRevision = 0;
};
}
