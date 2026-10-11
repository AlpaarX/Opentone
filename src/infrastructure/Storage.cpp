// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/Storage.h"
#include <juce_events/juce_events.h>
#include <filesystem>

namespace opentone
{
namespace
{
juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* result = new juce::DynamicObject();
    for (const auto& property : properties) result->setProperty(property.first, property.second);
    return result;
}
bool number(const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); }
juce::Result media(const juce::File& file, Asset& asset)
{
    if (!file.hasFileExtension("wav;aif;aiff;flac;mp3"))
        return juce::Result::fail("Use mono or stereo WAV, AIFF, FLAC or MP3 files.");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->sampleRate <= 0
        || reader->numChannels < 1 || reader->numChannels > 2)
        return juce::Result::fail("Audio is unreadable, empty or has more than two channels: " + file.getFileName());
    asset.sampleRate = reader->sampleRate; asset.channels = static_cast<int>(reader->numChannels);
    asset.length = reader->lengthInSamples; asset.missing = false;
    return juce::Result::ok();
}
struct Job : juce::ThreadPoolJob
{
    explicit Job(std::function<void()> taskIn) : ThreadPoolJob("Project storage"), task(std::move(taskIn)) {}
    JobStatus runJob() override { task(); return jobHasFinished; }
    std::function<void()> task;
};
}
Storage::Storage()
{
    juce::PropertiesFile::Options options; options.applicationName = "Opentone"; options.filenameSuffix = ".library";
    options.osxLibrarySubFolder = "Application Support";
    libraryPreferences = std::make_unique<juce::PropertiesFile>(options);
    if (auto xml = libraryPreferences->getXmlValue("folders")) for (auto* item : xml->getChildIterator())
        if (item->hasTagName("folder") && item->getStringAttribute("path").isNotEmpty()) folders.addIfNotAlreadyThere(item->getStringAttribute("path"));
    temporary = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("Opentone-" + newId());
    temporary.createDirectory();
}
Storage::~Storage() { cancel(); pool.removeAllJobs(true, -1); temporary.deleteRecursively(); }
juce::Result Storage::saveFolders()
{
    juce::XmlElement xml("folders"); for (const auto& path : folders) xml.createNewChildElement("folder")->setAttribute("path", path);
    libraryPreferences->setValue("folders", &xml);
    return libraryPreferences->saveIfNeeded() ? juce::Result::ok() : juce::Result::fail("Could not save sample folder preferences.");
}
juce::Result Storage::addSampleFolder(const juce::File& folder)
{
    if (!folder.isDirectory()) return juce::Result::fail("Choose an existing sample directory.");
    folders.addIfNotAlreadyThere(folder.getFullPathName()); return saveFolders();
}
juce::Result Storage::removeSampleFolder(const juce::File& folder)
{ folders.removeString(folder.getFullPathName()); return saveFolders(); }
void Storage::run(std::function<void()> work) { pool.addJob(new Job(std::move(work)), true); }
bool Storage::safePath(const juce::File& directory, const juce::String& relative)
{
    if (relative.isEmpty() || relative.containsChar('\\') || relative.containsChar(':')) return false;
    const auto path = std::filesystem::u8path(relative.toStdString());
    if (path.is_absolute()) return false;
    for (const auto& part : path) if (part == ".." || part == ".") return false;
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(std::filesystem::u8path(directory.getFullPathName().toStdString()), error);
    if (error) return false;
    const auto target = std::filesystem::weakly_canonical(root / path, error);
    if (error) return false;
    auto base = root.begin(), child = target.begin();
    for (; base != root.end(); ++base, ++child)
        if (child == target.end() || *base != *child) return false;
    return child != target.end();
}
ImportResult Storage::copyAudio(const juce::File& source, const juce::File& directory)
{
    ImportResult output;
    output.result = media(source, output.asset);
    if (output.result.failed()) return output;
    output.asset.id = newId();
    output.asset.path = "Audio/" + output.asset.id + "-" + source.getFileName();
    if (!safePath(directory, output.asset.path))
    { output.result = juce::Result::fail("Unsafe import destination."); return output; }
    const auto destination = directory.getChildFile(output.asset.path);
    output.result = destination.getParentDirectory().createDirectory();
    if (output.result.failed()) return output;
    juce::TemporaryFile staging(destination);
    if (!source.copyFileTo(staging.getFile())) output.result = juce::Result::fail("Could not copy audio.");
    else
    {
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(staging.getFile().createInputStream()));
        if (reader && reader->lengthInSamples == output.asset.length && reader->numChannels == static_cast<unsigned>(output.asset.channels)
            && std::equal_to<double>()(reader->sampleRate, output.asset.sampleRate)) output.result = juce::Result::ok();
        else output.result = juce::Result::fail("Copied audio failed validation.");
    }
    if (output.result.wasOk() && !staging.overwriteTargetFileWithTemporary())
        output.result = juce::Result::fail("Could not commit imported audio.");
    return output;
}
LoadResult Storage::load(const juce::File& directory)
{
    LoadResult output;
    const auto fail = [&](juce::String message) { output.result = juce::Result::fail(message); return output; };
    const auto manifest = directory.getChildFile("project.opentone");
    if (!manifest.existsAsFile() || manifest.getSize() > 128 * 1024 * 1024)
        return fail("Manifest is missing or exceeds 128 MB.");
    juce::var root;
    auto parsed = juce::JSON::parse(manifest.loadFileAsString(), root);
    if (parsed.failed() || !root.isObject()) return fail("Malformed project manifest.");
    if (!root["version"].isInt() || (static_cast<int>(root["version"]) != 1 && static_cast<int>(root["version"]) != 2 && static_cast<int>(root["version"]) != 3 && static_cast<int>(root["version"]) != 4 && static_cast<int>(root["version"]) != 5))
        return fail("Unsupported project version.");
    if (!number(root["bpm"]) || !root["tracks"].isArray() || !root["assets"].isArray() || !root["clips"].isArray())
        return fail("Invalid project schema.");
    output.project.bpm = static_cast<double>(root["bpm"]);
    if (root.getDynamicObject()->hasProperty("loop"))
    { const auto loop = root["loop"]; if (!loop.isObject() || !number(loop["start"]) || !number(loop["end"]) || !loop["enabled"].isBool()) return fail("Invalid arrangement loop schema.");
      output.project.loopStart = static_cast<double>(loop["start"]); output.project.loopEnd = static_cast<double>(loop["end"]); output.project.loopEnabled = static_cast<bool>(loop["enabled"]); }
    auto trackValues = *root["tracks"].getArray();
    if (static_cast<int>(root["version"]) >= 5)
    { if (!root["master"].isObject()) return fail("Missing master track schema."); trackValues.add(root["master"]); }
    for (const auto& value : trackValues)
    {
        if (!value.isObject() || !value["id"].isString() || !value["name"].isString()
            || !number(value["gain"]) || !value["mute"].isBool()) return fail("Invalid track schema.");
        output.project.tracks.push_back({ value["id"].toString(), value["name"].toString(),
                                         static_cast<double>(value["gain"]), static_cast<bool>(value["mute"]) });
        auto& mixerTrack = output.project.tracks.back();
        if (static_cast<int>(root["version"]) >= 5)
        {
            if (!number(value["pan"])) return fail("Invalid pan schema.");
            mixerTrack.pan = static_cast<double>(value["pan"]);
            for (const auto pan : {false, true})
            {
                const auto envelope = value[pan ? "panAutomation" : "volumeAutomation"];
                if (!envelope.isArray() || envelope.getArray()->size() > 4096) return fail("Invalid automation schema.");
                auto& points = pan ? mixerTrack.panAutomation : mixerTrack.volumeAutomation;
                for (const auto& point : *envelope.getArray())
                { if (!point.isObject() || !number(point["time"]) || !number(point["value"])) return fail("Invalid automation point.");
                  points.push_back({static_cast<double>(point["time"]), static_cast<double>(point["value"])}); }
            }
        }
        if (value.getDynamicObject()->hasProperty("samplerBypass"))
        { if (!value["samplerBypass"].isBool()) return fail("Invalid sampler bypass schema."); mixerTrack.samplerBypass = static_cast<bool>(value["samplerBypass"]); }
        if (value.getDynamicObject()->hasProperty("plugins"))
        {
            if (!value["plugins"].isArray()) return fail("Invalid plug-in chain schema.");
            for (const auto& plugin : *value["plugins"].getArray())
            {
                if (!plugin.isObject() || !plugin["id"].isString() || !plugin["name"].isString()
                    || !plugin["description"].isString() || !plugin["state"].isString()
                    || !plugin["instrument"].isBool() || !plugin["bypass"].isBool()) return fail("Invalid plug-in schema.");
                juce::MemoryBlock bytes; if (plugin["state"].toString().isNotEmpty() && !bytes.fromBase64Encoding(plugin["state"].toString())) return fail("Invalid plug-in state.");
                if (!juce::XmlDocument::parse(plugin["description"].toString())) return fail("Invalid plug-in description.");
                output.project.tracks.back().plugins.push_back({ plugin["id"].toString(), plugin["name"].toString(), plugin["description"].toString(),
                    plugin["state"].toString(), static_cast<bool>(plugin["instrument"]), static_cast<bool>(plugin["bypass"]) });
            }
        }
        if (value.getDynamicObject()->hasProperty("solo"))
        { if (!value["solo"].isBool()) return fail("Invalid solo schema."); output.project.tracks.back().solo = static_cast<bool>(value["solo"]); }
        if (static_cast<int>(root["version"]) >= 2)
        {
            if (!value["midi"].isBool() || !value["sample"].isString() || !number(value["sampleOffset"])
                || !number(value["sampleDuration"]) || !value["rootNote"].isInt()) return fail("Invalid sampler schema.");
            auto& track = output.project.tracks.back(); track.midi = static_cast<bool>(value["midi"]);
            track.sample = value["sample"].toString(); track.sampleOffset = static_cast<double>(value["sampleOffset"]);
            track.sampleDuration = static_cast<double>(value["sampleDuration"]); track.rootNote = static_cast<int>(value["rootNote"]);
            if (value.getDynamicObject()->hasProperty("sampler") && !value["sampler"].isBool()) return fail("Invalid instrument schema.");
            track.sampler = value.getDynamicObject()->hasProperty("sampler") ? static_cast<bool>(value["sampler"]) : track.midi;
            if (value.getDynamicObject()->hasProperty("samplerGain"))
            { if (!number(value["samplerGain"])) return fail("Invalid sampler gain schema."); track.samplerGain = static_cast<double>(value["samplerGain"]); }
        }
    }
    if (static_cast<int>(root["version"]) >= 5)
    { output.project.master = std::move(output.project.tracks.back()); output.project.tracks.pop_back(); }
    for (const auto& value : *root["assets"].getArray())
    {
        if (!value.isObject() || !value["id"].isString() || !value["path"].isString()) return fail("Invalid asset schema.");
        Asset asset { value["id"].toString(), value["path"].toString(), 1, 1, 1, true };
        if (!safePath(directory, asset.path)) return fail("Asset path escapes the project directory.");
        if (media(directory.getChildFile(asset.path), asset).failed())
            output.warning += "Missing or unreadable audio: " + asset.path + "\n";
        output.project.assets.push_back(std::move(asset));
    }
    for (const auto& value : *root["clips"].getArray())
    {
        if (!value.isObject() || !value["id"].isString() || !value["track"].isString() || !value["asset"].isString()
            || !number(value["start"]) || !number(value["offset"]) || !number(value["duration"])) return fail("Invalid clip schema.");
        output.project.clips.push_back({ value["id"].toString(), value["track"].toString(), value["asset"].toString(),
            static_cast<double>(value["start"]), static_cast<double>(value["offset"]), static_cast<double>(value["duration"]) });
        if (value.getDynamicObject()->hasProperty("loopLength"))
        {
            if (!number(value["loopStart"]) || !number(value["loopLength"]) || !value["loop"].isBool()) return fail("Invalid MIDI loop schema.");
            auto& clip = output.project.clips.back(); clip.loopStart = static_cast<double>(value["loopStart"]);
            clip.loopLength = static_cast<double>(value["loopLength"]); clip.loop = static_cast<bool>(value["loop"]);
        }
        if (static_cast<int>(root["version"]) >= 2)
        {
            if (!value["notes"].isArray() || value["notes"].getArray()->size() > 512) return fail("Invalid MIDI notes schema.");
            for (const auto& note : *value["notes"].getArray())
            {
                if (!note.isObject() || !note["pitch"].isInt() || !number(note["start"])
                    || !number(note["duration"]) || !number(note["velocity"])) return fail("Invalid MIDI note schema.");
                output.project.clips.back().notes.push_back({ static_cast<int>(note["pitch"]), static_cast<double>(note["start"]),
                    static_cast<double>(note["duration"]), static_cast<double>(note["velocity"]) });
            }
        }
    }
    for (auto& clip : output.project.clips)
        if (const auto* track = findId(output.project.tracks, clip.track)) if (track->midi && clip.loopLength == 0)
            clip.loopLength = std::max(0.001, clip.offset + clip.duration);
    output.result = validate(output.project); return output;
}
juce::Result Storage::write(const Snapshot& snapshot, const juce::File& directory)
{
    auto valid = validate(snapshot.project); if (valid.failed()) return valid;
    valid = directory.createDirectory(); if (valid.failed()) return valid;
    struct Copies
    {
        std::vector<juce::File> files;
        bool committed = false;
        ~Copies() { if (!committed) for (const auto& file : files) file.deleteFile(); }
    } copies;
    juce::var previous;
    const auto manifest = directory.getChildFile("project.opentone");
    if (manifest.existsAsFile() && manifest.getSize() <= 128 * 1024 * 1024)
        juce::JSON::parse(manifest.loadFileAsString(), previous);
    juce::Array<juce::var> tracks, assets, clips;
    juce::var master;
    for (const auto* item : allTracks(snapshot.project))
    {
        const auto& track = *item;
        juce::Array<juce::var> volumeAutomation, panAutomation;
        for (const auto& point : track.volumeAutomation) volumeAutomation.add(object({{"time", point.time}, {"value", point.value}}));
        for (const auto& point : track.panAutomation) panAutomation.add(object({{"time", point.time}, {"value", point.value}}));
        juce::Array<juce::var> plugins;
        for (const auto& plugin : track.plugins) plugins.add(object({ {"id", plugin.id}, {"name", plugin.name}, {"description", plugin.description},
            {"state", plugin.state}, {"instrument", plugin.instrument}, {"bypass", plugin.bypass} }));
        const auto serialized = object({ {"id", track.id}, {"name", track.name}, {"gain", track.gain}, {"mute", track.mute},
            {"midi", track.midi}, {"sample", track.sample}, {"sampleOffset", track.sampleOffset},
            {"sampleDuration", track.sampleDuration}, {"rootNote", track.rootNote}, {"sampler", track.sampler}, {"samplerGain", track.samplerGain}, {"samplerBypass", track.samplerBypass}, {"solo", track.solo}, {"plugins", plugins}, {"pan", track.pan}, {"volumeAutomation", volumeAutomation}, {"panAutomation", panAutomation} });
        if (track.id == "master") master = serialized; else tracks.add(serialized);
    }
    for (const auto& asset : snapshot.project.assets)
    {
        if (!safePath(snapshot.directory, asset.path) || !safePath(directory, asset.path))
            return juce::Result::fail("Unsafe asset path.");
        const auto source = snapshot.directory.getChildFile(asset.path);
        auto target = directory.getChildFile(asset.path);
        auto path = asset.path;
        if (source != target && target.exists())
            if (const auto* savedAssets = previous["assets"].getArray())
                for (const auto& savedAsset : *savedAssets)
                    if (savedAsset["id"].toString() == asset.id && savedAsset["path"].isString()
                        && safePath(directory, savedAsset["path"].toString()))
                    { path = savedAsset["path"].toString(); target = directory.getChildFile(path); break; }
        if (source != target && source.existsAsFile())
        {
            if (!source.hasIdenticalContentTo(target))
            {
                // The old manifest must keep its audio even if this save fails later.
                target = target.getNonexistentSibling();
                path = target.getRelativePathFrom(directory).replaceCharacter('\\', '/');
                if (!safePath(directory, path)) return juce::Result::fail("Unsafe asset path.");
                auto made = target.getParentDirectory().createDirectory(); if (made.failed()) return made;
                juce::TemporaryFile staging(target);
                if (!source.copyFileTo(staging.getFile()) || !staging.overwriteTargetFileWithTemporary())
                    return juce::Result::fail("Failed to copy project audio.");
                copies.files.push_back(target);
            }
        }
        else if (!source.existsAsFile() && !asset.missing) return juce::Result::fail("Referenced audio disappeared before saving.");
        else if (source != target && target.exists())
            path = target.getNonexistentSibling().getRelativePathFrom(directory).replaceCharacter('\\', '/');
        assets.add(object({ {"id", asset.id}, {"path", path} }));
    }
    for (const auto& clip : snapshot.project.clips)
    {
        juce::Array<juce::var> notes;
        for (const auto& note : clip.notes) notes.add(object({ {"pitch", note.pitch}, {"start", note.start},
            {"duration", note.duration}, {"velocity", note.velocity} }));
        clips.add(object({ {"id", clip.id}, {"track", clip.track}, {"asset", clip.asset},
                           {"start", clip.start}, {"offset", clip.offset}, {"duration", clip.duration}, {"notes", notes}, {"loopStart", clip.loopStart}, {"loopLength", clip.loopLength}, {"loop", clip.loop} }));
    }
    // Preserve double precision when round-tripping arrangement timing.
    const auto json = juce::JSON::toString(object({ {"version", 5}, {"bpm", snapshot.project.bpm},
                                                   {"tracks", tracks}, {"master", master}, {"loop", object({{"start", snapshot.project.loopStart}, {"end", snapshot.project.loopEnd}, {"enabled", snapshot.project.loopEnabled}})}, {"assets", assets}, {"clips", clips} }), false, 17);
    if (json.getNumBytesAsUTF8() > 128 * 1024 * 1024) return juce::Result::fail("Project metadata exceeds 128 MB.");
    juce::TemporaryFile staging(directory.getChildFile("project.opentone"));
    if (!staging.getFile().replaceWithText(json) || !staging.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Failed to replace manifest; previous project is preserved.");
    copies.committed = true;
    return juce::Result::ok();
}
void Storage::importFile(juce::File source, juce::File directory, std::function<void(ImportResult)> done)
{
    const auto ticket = epoch->load(); const auto cancellation = epoch;
    run([this, source, directory, ticket, cancellation, callback = std::move(done)]
    {
        if (ticket != epoch->load()) return;
        auto result = copyAudio(source, directory);
        if (ticket != epoch->load())
        { if (result.result.wasOk()) directory.getChildFile(result.asset.path).deleteFile(); return; }
        juce::MessageManager::callAsync([output = std::move(result), callback, cancellation, ticket, directory]
        { if (ticket == cancellation->load()) callback(output);
          else if (output.result.wasOk()) directory.getChildFile(output.asset.path).deleteFile(); });
    });
}
void Storage::open(juce::File directory, std::function<void(LoadResult)> done)
{
    const auto ticket = epoch->load(); const auto cancellation = epoch;
    run([this, directory, ticket, cancellation, callback = std::move(done)]
    { auto result = load(directory); if (ticket == epoch->load())
        juce::MessageManager::callAsync([result, callback, cancellation, ticket] { if (ticket == cancellation->load()) callback(result); }); });
}
void Storage::save(Snapshot snapshot, juce::File directory, std::function<void(juce::Result)> done)
{
    const auto ticket = epoch->load(); const auto cancellation = epoch;
    run([this, captured = std::move(snapshot), directory, ticket, cancellation, callback = std::move(done)]
    { if (ticket != epoch->load()) return; auto result = write(captured, directory);
      if (ticket == epoch->load()) juce::MessageManager::callAsync([result, callback, cancellation, ticket] { if (ticket == cancellation->load()) callback(result); }); });
}
}
