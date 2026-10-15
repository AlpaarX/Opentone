// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "domain/Project.h"
#include "shared/RuntimeExports.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

namespace opentone
{
struct LoadResult { juce::Result result = juce::Result::ok(); Project project; juce::String warning; };
struct ImportResult { juce::Result result = juce::Result::ok(); Asset asset; };
class OPENTONE_API Storage
{
public:
    Storage();
    ~Storage();
    const juce::StringArray& recentProjects() const { return recent; }
    juce::Result rememberProject(const juce::File&);
    juce::Result cleanRecentProjects(bool clear);
    const juce::StringArray& sampleFolders() const { return folders; }
    juce::Result addSampleFolder(const juce::File&);
    juce::Result removeSampleFolder(const juce::File&);
    juce::File workspace() const { return temporary; }
    void cancel() { ++*epoch; }
    bool hasPendingJobs() const { return pool.getNumJobs() != 0; }
    void importFile(juce::File source, juce::File directory, std::function<void(ImportResult)> done);
    void open(juce::File directory, std::function<void(LoadResult)> done);
    void save(Snapshot snapshot, juce::File directory, std::function<void(juce::Result)> done);
    static LoadResult load(const juce::File& directory);
    static juce::Result write(const Snapshot&, const juce::File& directory);
    static ImportResult copyAudio(const juce::File& source, const juce::File& directory);
    static bool safePath(const juce::File& directory, const juce::String& relative);
private:
    void run(std::function<void()> work);
    juce::File temporary;
    std::unique_ptr<juce::PropertiesFile> libraryPreferences;
    juce::StringArray folders, recent;
    juce::Result saveRecentProjects();
    juce::Result saveFolders();
    std::shared_ptr<std::atomic<uint64_t>> epoch = std::make_shared<std::atomic<uint64_t>>(0);
    juce::ThreadPool pool { 1 };
};
}
