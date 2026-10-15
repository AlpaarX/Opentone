// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/AbletonImport.h"
#include "infrastructure/BuiltinPlugins.h"
#include <cstring>
#include <stdexcept>

namespace opentone
{
namespace
{
using Xml = juce::XmlElement;
const Xml* path(const Xml* node, const juce::String& address)
{
    for (const auto& part : juce::StringArray::fromTokens(address, "/", ""))
    { if (!node) return nullptr; node = node->getChildByName(part); }
    return node;
}
juce::String value(const Xml* node, const juce::String& address, juce::String fallback = {})
{ const auto* item = path(node, address); return item ? item->getStringAttribute("Value", fallback) : fallback; }
double numeric(const juce::String& text, double fallback)
{
    if (text.isEmpty()) return fallback;
    char* end = nullptr; const auto* begin = text.toRawUTF8(); const auto result = std::strtod(begin, &end);
    if (end == begin || *end != 0 || !std::isfinite(result)) throw std::runtime_error("Invalid numeric value in Live Set.");
    return result;
}
double number(const Xml* node, const juce::String& address, double fallback)
{ return numeric(value(node, address), fallback); }
bool boolean(const Xml* node, const juce::String& address, bool fallback)
{
    const auto text = value(node, address);
    if (text.isEmpty()) return fallback;
    if (text != "true" && text != "false") throw std::runtime_error("Invalid boolean value in Live Set.");
    return text == "true";
}
const Xml* descendant(const Xml* node, const juce::String& tag)
{
    if (!node) return nullptr;
    if (node->hasTagName(tag)) return node;
    for (auto* child : node->getChildIterator()) if (const auto* found = descendant(child, tag)) return found;
    return nullptr;
}
int count(const Xml* node, const juce::String& tag)
{ if (!node) return 0; int result = node->hasTagName(tag) ? 1 : 0; for (auto* child : node->getChildIterator()) result += count(child, tag); return result; }
juce::String readXml(const juce::File& file)
{
    if (!file.existsAsFile() || file.getSize() > 64 * 1024 * 1024) throw std::runtime_error("Live Set is missing or exceeds 64 MB.");
    juce::FileInputStream source(file); if (!source.openedOk()) throw std::runtime_error("Could not read Live Set.");
    const auto compressed = source.readByte() == static_cast<char>(0x1f) && source.readByte() == static_cast<char>(0x8b); source.setPosition(0);
    juce::GZIPDecompressorInputStream gzip(&source, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::InputStream& input = compressed ? static_cast<juce::InputStream&>(gzip) : static_cast<juce::InputStream&>(source);
    juce::MemoryBlock bytes; std::array<char, 32768> buffer {};
    for (;;)
    { const auto size = input.read(buffer.data(), static_cast<int>(buffer.size())); if (size <= 0) break;
      if (bytes.getSize() + static_cast<size_t>(size) > 128 * 1024 * 1024) throw std::runtime_error("Expanded Live Set exceeds 128 MB."); bytes.append(buffer.data(), static_cast<size_t>(size)); }
    const auto text = juce::String::fromUTF8(static_cast<const char*>(bytes.getData()), static_cast<int>(bytes.getSize()));
    if (text.containsIgnoreCase("<!DOCTYPE") || text.containsIgnoreCase("<!ENTITY")) throw std::runtime_error("External XML declarations are not supported.");
    int depth = 0, nodes = 0;
    // Indexed JUCE string searches restart at the beginning; scan UTF-8 forwards instead.
    const auto* cursor = text.toRawUTF8();
    while (const auto* start = std::strchr(cursor, '<'))
    {
        const auto* end = std::strchr(start + 1, '>'); if (!end) break;
        if (start[1] == '/') --depth;
        else if (start[1] != '?' && start[1] != '!')
        { if (end[-1] != '/') ++depth; if (++nodes > 1000000 || depth > 128) throw std::runtime_error("Live Set XML is too complex."); }
        cursor = end + 1;
    }
    return text;
}
int trackColor(const Xml* input)
{
    // Live 10–12's 70-color palette, in the order stored by ALS Color.
    static constexpr int palette[] = {
        0xff94a6, 0xffa529, 0xcc9927, 0xf7f47c, 0xbffb00, 0x1aff2f, 0x25ffa8, 0x5cffe8, 0x8bc5ff, 0x5480e4, 0x92a7ff, 0xd86ce4, 0xe553a0, 0xffffff,
        0xff3636, 0xf66c03, 0x99724b, 0xfff034, 0x87ff67, 0x3dc300, 0x00bfaf, 0x19e9ff, 0x10a4ee, 0x007dc0, 0x886ce4, 0xb677c6, 0xff39d4, 0xd0d0d0,
        0xe2675a, 0xffa374, 0xd3ad71, 0xedffae, 0xd2e498, 0xbad074, 0x9bc48d, 0xd4fde1, 0xcdf1f8, 0xb9c1e3, 0xcdbbe4, 0xae98e5, 0xe5dce1, 0xa9a9a9,
        0xc6928b, 0xb78256, 0x99836a, 0xbfba69, 0xa6be00, 0x7db04d, 0x88c2ba, 0x9bb3c4, 0x85a5c2, 0x8393cc, 0xa595b5, 0xbf9fbe, 0xbc7196, 0x7b7b7b,
        0xaf3333, 0xa95131, 0x724f41, 0xdbc300, 0x85961f, 0x539f31, 0x0a9c8e, 0x236384, 0x1a2f96, 0x2f52a2, 0x624bad, 0xa34bad, 0xcc2e6e, 0x3c3c3c,
    };
    static_assert(std::size(palette) == 70);
    const auto text = value(input, "Color", value(input, "ColorIndex"));
    if (text.isEmpty()) return -1;
    const auto index = numeric(text, -1);
    if (index == -1) return -1;
    if (index < 0 || index >= static_cast<double>(std::size(palette)) || std::floor(index) != index)
        throw std::runtime_error("Unsupported track color index in Live Set.");
    return palette[static_cast<size_t>(index)];
}
struct Importer
{
    juce::File source, destination; Project project; juce::StringArray warnings;
    std::map<juce::String, size_t> assets; std::map<juce::String, juce::String> trackIds; std::vector<juce::File> libraries; double beat = 0.5;
    void warn(const juce::String& message)
    { if (!warnings.contains(message)) { if (warnings.size() >= 1024) throw std::runtime_error("Live Set has too many unsupported features to import."); warnings.add(message); } }
    juce::File resolve(const Xml* ref)
    {
        auto relative = value(ref, "FileRef/RelativePath").replace("\\", "/");
        const auto absolute = value(ref, "FileRef/Path").replace("\\", "/");
        if (juce::File::isAbsolutePath(absolute)) { const juce::File file(absolute); if (file.existsAsFile()) return file; }
        if (relative.isNotEmpty())
        {
            auto directory = source.getParentDirectory();
            for (int up = 0; up < 5; ++up)
            { if (Storage::safePath(directory, relative)) { const auto file = directory.getChildFile(relative); if (file.existsAsFile()) return file; }
              const auto parent = directory.getParentDirectory(); if (parent == directory) break; directory = parent; }
            for (const auto& library : libraries) if (Storage::safePath(library, relative))
            { const auto file = library.getChildFile(relative); if (file.existsAsFile()) return file; }
        }
        return {};
    }
    Asset sample(const Xml* ref)
    {
        const auto file = resolve(ref);
        const auto key = file.existsAsFile() ? file.getFullPathName() : value(ref, "FileRef/RelativePath") + "|" + value(ref, "FileRef/Path");
        if (const auto found = assets.find(key); found != assets.end()) return project.assets[found->second];
        Asset asset;
        if (file.existsAsFile())
        { auto result = Storage::copyAudio(file, destination); if (result.result.wasOk()) asset = result.asset;
          else warn("Unreadable sample retained as missing: " + file.getFileName() + " (" + result.result.getErrorMessage() + ")"); }
        if (asset.id.isEmpty())
        {
            auto filename = value(ref, "FileRef/RelativePath"); if (filename.isEmpty()) filename = value(ref, "FileRef/Path");
            if (filename.isEmpty()) filename = value(ref, "FileRef/Name", value(ref, "Name"));
            filename = filename.replace("\\", "/").fromLastOccurrenceOf("/", false, false);
            filename = juce::File::createLegalFileName(filename); if (filename.isEmpty()) filename = "Missing.wav";
            asset.id = newId(); asset.path = "Audio/" + asset.id + "-" + filename; asset.missing = true;
            asset.sampleRate = number(ref, "DefaultSampleRate", 48000); if (asset.sampleRate <= 0 || asset.sampleRate > 1000000) asset.sampleRate = 48000; asset.channels = 2;
            asset.length = static_cast<juce::int64>(juce::jlimit(1.0, 1.0e12, number(ref, "DefaultDuration", 48000)));
            warn("Missing sample: " + filename);
        }
        assets[key] = project.assets.size(); project.assets.push_back(asset); return asset;
    }
    void automation(const Xml* track, Track& output, const Xml* mixer)
    {
        const auto* envelopes = path(track, "AutomationEnvelopes/Envelopes"); if (!envelopes) return;
        for (auto* envelope : envelopes->getChildIterator())
        {
            const auto target = value(envelope, "EnvelopeTarget/PointeeId"); std::vector<AutomationPoint>* points = nullptr; int parameter = 0;
            for (const bool isPan : {false, true}) if (const auto* id = path(mixer, isPan ? "Pan/AutomationTarget" : "Volume/AutomationTarget"))
                if (id->getStringAttribute("Id") == target) { points = isPan ? &output.panAutomation : &output.volumeAutomation; parameter = isPan ? 1 : 0; }
            if (const auto* speaker = path(mixer, "Speaker/AutomationTarget")) if (speaker->getStringAttribute("Id") == target) { points = &output.muteAutomation; parameter = 2; }
            if (const auto* sends = path(mixer, "Sends")) for (auto* send : sends->getChildIterator())
            { const auto index = send->getIntAttribute("Id", -1); if (index < 0 || index >= maxReturns) continue;
              if (const auto* id = path(send, "Send/AutomationTarget")) if (id->getStringAttribute("Id") == target)
              { output.sendAutomation.resize(std::max(output.sendAutomation.size(), static_cast<size_t>(index + 1))); points = &output.sendAutomation[static_cast<size_t>(index)]; parameter = index + 3; } }
            if (!points)
            {
                const auto* tempoTarget = path(mixer, "Tempo/AutomationTarget"); const auto* events = path(envelope, "Automation/Events");
                if (output.id == "master" && tempoTarget && tempoTarget->getStringAttribute("Id") == target)
                { if (events && events->getNumChildElements() > 1) warn("Tempo automation was not imported; arrangement timing uses the set's fixed BPM."); }
                else warn("Device/other non-mixer automation was not imported.");
                continue;
            }
            if (const auto* events = path(envelope, "Automation/Events")) for (auto* event : events->getChildIterator())
            {
                if (!event->hasTagName("FloatEvent") && !event->hasTagName("BoolEvent") && !event->hasTagName("EnumEvent")) continue;
                const auto time = std::max(0.0, numeric(event->getStringAttribute("Time"), 0) * beat); const auto text = event->getStringAttribute("Value");
                auto val = text == "true" ? 1.0 : text == "false" ? 0.0 : numeric(text, 0); if (parameter == 2) val = 1 - val;
                const auto limits = automationLimits(parameter); AutomationPoint point {time, juce::jlimit(limits.first, limits.second, val)};
                point.x1 = numeric(event->getStringAttribute("CurveControl1X"), point.x1); point.y1 = numeric(event->getStringAttribute("CurveControl1Y"), point.y1);
                point.x2 = numeric(event->getStringAttribute("CurveControl2X"), point.x2); point.y2 = numeric(event->getStringAttribute("CurveControl2Y"), point.y2); point.step = parameter == 2;
                points->push_back(point);
            }
            std::stable_sort(points->begin(), points->end(), [](const auto& a, const auto& b) { return a.time < b.time; });
            std::vector<AutomationPoint> unique;
            for (const auto& point : *points) { if (!unique.empty() && std::equal_to<double>()(unique.back().time, point.time)) unique.back() = point; else unique.push_back(point); }
            if (unique.size() > 4096) throw std::runtime_error("A mixer automation envelope exceeds 4096 points.");
            *points = std::move(unique);
        }
    }
    void mixer(const Xml* input, Track& track)
    {
        track.color = trackColor(input);
        const auto* mix = path(input, "DeviceChain/Mixer");
        track.gain = juce::jlimit(0.0, 2.0, number(mix, "Volume/Manual", 1)); track.pan = juce::jlimit(-1.0, 1.0, number(mix, "Pan/Manual", 0));
        track.mute = !boolean(mix, "Speaker/Manual", true); track.solo = boolean(mix, "SoloSink", false);
        if (track.id != "master") if (const auto* sends = path(mix, "Sends"))
            for (auto* send : sends->getChildIterator())
            { const auto index = send->getIntAttribute("Id", -1); if (index >= 0 && index < maxReturns) { track.sends.resize(std::max(track.sends.size(), static_cast<size_t>(index + 1)), 0); const auto gain = juce::jlimit(0.0, maxSendGain, number(send, "Send/Manual", 0)); track.sends[static_cast<size_t>(index)] = gain <= 0.0003163 ? 0 : gain; } }
        const auto routing = value(input, "DeviceChain/AudioOutputRouting/Target"); track.outputEnabled = !routing.containsIgnoreCase("None");
        if (track.id != "master" && (routing.contains("Main") || routing.contains("Master"))) track.outputTrack = "master";
        else if (track.id != "master" && routing.contains("Track.")) track.outputTrack = routing.fromFirstOccurrenceOf("Track.", false, false).upToFirstOccurrenceOf("/", false, false);
        else if (routing.isNotEmpty() && !routing.contains("Main") && !routing.contains("Master") && !(routing.contains("Group") && track.parentGroup.isNotEmpty()) && !routing.containsIgnoreCase("None")) warn("External/unrecognized output routing was mapped to Main.");
        if (track.returnIndex < 0 && track.id != "master") track.monitor = juce::jlimit(0, 2, static_cast<int>(juce::jlimit(0.0, 2.0, number(input, "DeviceChain/MainSequencer/MonitoringEnum", 1))));
        if (track.returnIndex < 0 && track.id != "master" && !track.group)
        { track.armed = boolean(input, "DeviceChain/MainSequencer/Recorder/IsArmed", false);
          const auto incoming = value(input, track.midi ? "DeviceChain/MidiInputRouting/Target" : "DeviceChain/AudioInputRouting/Target");
          track.inputEnabled = !incoming.containsIgnoreCase("None");
          if (track.midi && incoming.startsWith("MidiIn/External"))
          { const auto channel = incoming.fromLastOccurrenceOf("/", false, false);
            if (channel.isNotEmpty() && channel.containsOnly("0123456789")) track.midiChannel = juce::jlimit(1, 16, channel.getIntValue() + 1); }
          if (incoming.isNotEmpty() && !incoming.startsWith(track.midi ? "MidiIn/External" : "AudioIn/External") && !incoming.containsIgnoreCase("None")) warn("Custom input routing on " + track.name + " was mapped to the default input."); }
        automation(input, track, mix);
    }
    void devices(const Xml* input, Track& track)
    {
        const auto* items = path(input, "DeviceChain/DeviceChain/Devices"); if (!items) return;
        if (items->getNumChildElements() > 128) throw std::runtime_error("A Live track exceeds 128 devices.");
        for (auto* device : items->getChildIterator())
        {
            const auto tag = device->getTagName(); std::map<juce::String, float> params; juce::String id;
            const auto manual = [&](const char* name, double fallback) { return static_cast<float>(number(device, juce::String(name) + "/Manual", fallback)); };
            if (tag == "Reverb")
            { id = "opentone.reverb"; params = {{"mix", manual("MixDirect", 0.25)}, {"predelay", manual("PreDelay", 15)}, {"size", manual("RoomSize", 65) / 100}, {"width", manual("StereoSeparation", 100) / 100}}; }
            else if (tag == "Delay" || tag == "PingPongDelay" || tag == "FilterDelay" || tag == "SimpleDelay")
            { id = "opentone.delay"; params = {{"mix", manual("DryWet", 0.25)}, {"feedback", manual("Feedback", 0.35)}, {"ping", boolean(device, "DelayLine_PingPong/Manual", tag == "PingPongDelay") ? 1.0f : 0.0f}}; }
            else if ((tag == "OriginalSimpler" || tag == "MultiSampler") && track.midi && !track.sampler && count(device, "SampleRef") <= 1)
            {
                track.sampler = true; track.samplerBypass = !boolean(device, "On/Manual", true);
                const auto* ref = descendant(device, "SampleRef");
                if (!ref || (value(ref, "FileRef/RelativePath").isEmpty() && value(ref, "FileRef/Path").isEmpty() && value(ref, "FileRef/Name").isEmpty() && number(ref, "DefaultDuration", 0) <= 0))
                { warn("Empty Live Sampler was retained as Opentone Sampler."); continue; }
                const auto asset = sample(ref); const auto* part = descendant(device, "MultiSamplePart");
                track.sampler = true; track.sample = asset.id; track.rootNote = juce::jlimit(0, 127, static_cast<int>(juce::jlimit(0.0, 127.0, number(part, "RootKey", 60))));
                track.sampleOffset = std::max(0.0, number(part, "SampleStart", 0) / asset.sampleRate);
                const auto end = std::min(static_cast<double>(asset.length) / asset.sampleRate, number(part, "SampleEnd", static_cast<double>(asset.length)) / asset.sampleRate);
                track.sampleDuration = std::max(1 / asset.sampleRate, end - track.sampleOffset); track.samplerBypass = !boolean(device, "On/Manual", true);
                warn("Simpler/Sampler was converted to Opentone Sampler; envelopes, filters, slicing and modulation differ."); continue;
            }
            if (id.isNotEmpty())
            { auto plugin = builtinPluginModel(id, params); plugin.bypass = !boolean(device, "On/Manual", true); track.plugins.push_back(std::move(plugin)); warn("Live " + tag + " was replaced with Opentone's built-in effect; DSP and some parameters differ."); }
            else
            {
                auto name = value(device, "UserName"); if (name.isEmpty()) name = value(device, "Name");
                for (const auto* field : {"PlugName", "PluginName"}) if (name.isEmpty())
                    if (const auto* item = descendant(device, field)) name = item->getStringAttribute("Value", item->getAllSubText());
                for (const auto* info : {"Vst3PluginInfo", "VstPluginInfo", "AuPluginInfo"}) if (name.isEmpty()) name = value(descendant(device, info), "Name");
                if (name.isEmpty()) name = tag;
                juce::PluginDescription description; description.name = name.substring(0, 256); description.descriptiveName = tag.substring(0, 256);
                description.pluginFormatName = "Ableton Live"; description.manufacturerName = "Ableton"; description.category = "Unavailable";
                description.fileOrIdentifier = "live:" + tag; description.numInputChannels = description.numOutputChannels = 2;
                const auto raw = device->toString(); juce::String state;
                if (raw.getNumBytesAsUTF8() <= 12 * 1024 * 1024) state = juce::MemoryBlock(raw.toRawUTF8(), raw.getNumBytesAsUTF8()).toBase64Encoding();
                else warn("State for unavailable device " + name + " exceeds the storage limit; the original ALS retains it.");
                // Unavailable devices are inert entries, preserving order without pretending to be a playable instrument.
                track.plugins.push_back({newId(), description.name, description.createXml()->toString(), state, false, !boolean(device, "On/Manual", true)});
                warn("Unavailable device retained on " + track.name + ": " + name + " (" + tag + ").");
            }
        }
    }
    void midiClip(const Xml* input, const Track& track)
    {
        Clip clip; clip.id = newId(); clip.track = track.id; clip.name = value(input, "Name"); clip.color = trackColor(input); clip.enabled = !boolean(input, "Disabled", false); clip.annotation = value(input, "Annotation");
        const auto start = number(input, "CurrentStart", numeric(input->getStringAttribute("Time"), 0));
        const auto end = number(input, "CurrentEnd", start); if (end <= start) return;
        clip.start = std::max(0.0, start * beat); clip.duration = (end - std::max(0.0, start)) * beat; if (clip.duration <= 0) return;
        clip.loopStart = number(input, "Loop/LoopStart", 0) * beat;
        clip.loopLength = (number(input, "Loop/LoopEnd", (end - start)) * beat) - clip.loopStart;
        clip.loop = boolean(input, "Loop/LoopOn", false) && clip.loopLength >= 0.001;
        clip.loopLength = std::max(0.0, clip.loopLength);
        clip.offset = clip.loopStart + number(input, "Loop/StartRelative", 0) * beat + std::max(0.0, -start * beat);
        if (const auto* keys = path(input, "Notes/KeyTracks")) for (auto* key : keys->getChildIterator())
        {
            const auto keyValue = number(key, "MidiKey", 60);
            if (keyValue < 0 || keyValue > 127 || std::floor(keyValue) != keyValue) throw std::runtime_error("Invalid MIDI key in Live Set.");
            const auto pitch = static_cast<int>(keyValue); const auto* notes = path(key, "Notes"); if (!notes) continue;
            for (auto* note : notes->getChildIterator())
            {
                const auto duration = numeric(note->getStringAttribute("Duration"), 0) * beat; if (duration <= 0) continue;
                clip.notes.push_back({pitch, numeric(note->getStringAttribute("Time"), 0) * beat, duration, juce::jlimit(0.0, 1.0, numeric(note->getStringAttribute("Velocity"), 100) / 127)});
                clip.notes.back().enabled = note->getStringAttribute("IsEnabled", "true") != "false";
                auto& imported = clip.notes.back(); imported.sourceId = note->getIntAttribute("NoteId", -1); imported.probability = juce::jlimit(0.0, 1.0, numeric(note->getStringAttribute("Probability"), 1));
                imported.velocityDeviation = juce::jlimit(-1.0, 1.0, numeric(note->getStringAttribute("VelocityDeviation"), 0) / 127.0);
                imported.releaseVelocity = juce::jlimit(0.0, 1.0, numeric(note->getStringAttribute("OffVelocity", note->getStringAttribute("ReleaseVelocity")), 64) / 127.0);
            }
        }
        if (clip.notes.size() > maxMidiNotes) throw std::runtime_error("A MIDI clip exceeds 4096 notes; split it in Live before importing.");
        double shift = std::max(0.0, -std::min(clip.offset, clip.loopStart)); for (const auto& note : clip.notes) shift = std::max(shift, -note.start);
        clip.offset += shift; clip.loopStart += shift; for (auto& note : clip.notes) note.start += shift;
        if (const auto* expressions = path(input, "Notes/PerNoteEventStore/EventLists"))
        { for (auto* list : expressions->getChildIterator())
          { const auto id = list->getIntAttribute("NoteId", -1); auto found = std::find_if(clip.notes.begin(), clip.notes.end(), [id](const auto& note) { return note.sourceId == id; }); if (found == clip.notes.end()) continue;
            if (found->expression.size() >= 16) throw std::runtime_error("A note exceeds 16 expression envelopes.");
            NoteExpression expression; expression.controller = list->getIntAttribute("CC", -1);
            if (const auto* events = path(list, "Events")) for (auto* event : events->getChildIterator())
            { if (expression.points.size() >= 4096) throw std::runtime_error("A note expression exceeds 4096 points.");
              AutomationPoint point {std::max(0.0, numeric(event->getStringAttribute("TimeOffset"), 0) * beat), numeric(event->getStringAttribute("Value"), 0)};
              point.x1 = numeric(event->getStringAttribute("CurveControl1X"), point.x1); point.y1 = numeric(event->getStringAttribute("CurveControl1Y"), point.y1); point.x2 = numeric(event->getStringAttribute("CurveControl2X"), point.x2); point.y2 = numeric(event->getStringAttribute("CurveControl2Y"), point.y2);
              if (!expression.points.empty() && expression.points.back().time >= point.time) { if (std::abs(expression.points.back().time - point.time) < 1e-9) expression.points.back() = point; else throw std::runtime_error("Note expression points are not ordered."); } else expression.points.push_back(point); }
            if (!expression.points.empty()) found->expression.push_back(std::move(expression)); found->bendRange = 48;
          }
          std::vector<size_t> ordered; for (size_t i = 0; i < clip.notes.size(); ++i) if (!clip.notes[i].expression.empty()) ordered.push_back(i);
          std::stable_sort(ordered.begin(), ordered.end(), [&](size_t a, size_t b) { return clip.notes[a].start < clip.notes[b].start; });
          std::array<double, 15> ends {}; ends.fill(-1);
          for (const auto index : ordered) { auto& note = clip.notes[index]; const auto available = std::find_if(ends.begin(), ends.end(), [&](double end) { return end <= note.start; });
            if (available == ends.end()) { warn("More than 15 simultaneous MPE notes: excess notes use the master MIDI channel without per-note expression."); note.channel = 1; }
            else { note.channel = static_cast<int>(std::distance(ends.begin(), available)) + 2; *available = note.start + note.duration; } }
          if (!ordered.empty()) warn("MPE expression is sent as MIDI pitch bend, pressure and controllers; instrument support and bend-range handling determine the sound.");
          if (!ordered.empty() && track.sampler) warn("Opentone Sampler does not reproduce per-note MPE modulation; expressions remain in the editable clip.");
        }
        if (project.clips.size() >= 10000) throw std::runtime_error("Live Set exceeds 10000 arrangement clips.");
        project.clips.push_back(std::move(clip));
    }
    double warpTime(const Xml* clip, double beats) const
    {
        const auto* markers = path(clip, "WarpMarkers"); std::vector<std::pair<double, double>> points;
        if (markers) for (auto* marker : markers->getChildIterator()) points.push_back({numeric(marker->getStringAttribute("BeatTime"), 0), numeric(marker->getStringAttribute("SecTime"), 0)});
        std::sort(points.begin(), points.end());
        if (points.size() < 2) return points.empty() ? beats * beat : points[0].second + (beats - points[0].first) * beat;
        size_t after = 1; while (after + 1 < points.size() && points[after].first < beats) ++after;
        const auto& a = points[after - 1]; const auto& b = points[after];
        return b.first > a.first ? a.second + (beats - a.first) * (b.second - a.second) / (b.first - a.first) : a.second;
    }
    void audioClip(const Xml* input, const Track& track)
    {
        const auto* ref = path(input, "SampleRef"); if (!ref) warn("An audio clip with no sample reference was retained as missing audio.");
        const auto asset = sample(ref); const bool warped = boolean(input, "IsWarped", false);
        const auto startBeat = number(input, "CurrentStart", numeric(input->getStringAttribute("Time"), 0)); const auto endBeat = number(input, "CurrentEnd", startBeat);
        double start = std::max(0.0, startBeat * beat), remaining = (endBeat - std::max(0.0, startBeat)) * beat; if (remaining <= 0) return;
        const auto at = [&](double time) { return warped ? warpTime(input, time) : time; };
        const auto loopStart = std::max(0.0, at(number(input, "Loop/LoopStart", 0)));
        const auto loopEnd = std::max(loopStart, at(number(input, "Loop/LoopEnd", static_cast<double>(asset.length) / asset.sampleRate)));
        auto offset = std::max(0.0, at(number(input, "Loop/LoopStart", 0) + number(input, "Loop/StartRelative", 0))) + std::max(0.0, -startBeat * beat);
        const auto sourceEnd = asset.missing ? std::max(offset + remaining, loopEnd) : static_cast<double>(asset.length) / asset.sampleRate;
        const auto end = boolean(input, "Loop/LoopOn", false) ? std::min(loopEnd, sourceEnd) : sourceEnd;
        const bool loop = boolean(input, "Loop/LoopOn", false) && end - loopStart >= 0.001;
        if (warped) warn("Warped audio uses original sample speed; Live's stretch modes are not reproduced.");
        const auto semitones = number(input, "PitchCoarse", 0) + number(input, "PitchFine", 0) / 100.0;
        const auto playbackRate = std::pow(2.0, juce::jlimit(-96.0, 96.0, semitones) / 12.0);
        if (warped && std::abs(semitones) > 1.0e-9) warn("Warped audio transposition uses repitch playback; pitch-preserving stretching is unavailable.");
        const auto fadeIn = std::max(0.0, number(input, "Fades/FadeInLength", 0)), fadeOut = std::max(0.0, number(input, "Fades/FadeOutLength", 0));
        if (fadeIn > 0 || fadeOut > 0) warn("Audio fades were converted to linear fades; Live's fade curves/crossfade shapes differ.");
        const auto arrangementStart = start, arrangementEnd = start + remaining;
        const auto gain = juce::jlimit(0.0, 8.0, number(input, "SampleVolume", 1)); int pieces = 0;
        while (remaining > 1.0e-9)
        {
            if (offset >= end && loop) offset = loopStart;
            const auto duration = std::min(remaining, (end - offset) / playbackRate); if (duration <= 1.0e-9) { warn("An audio clip exceeded its available source and was shortened."); break; }
            Clip clip {newId(), track.id, asset.id, start, offset, duration}; clip.loop = false; clip.name = value(input, "Name"); clip.gain = gain; clip.color = trackColor(input); clip.enabled = !boolean(input, "Disabled", false); clip.annotation = value(input, "Annotation"); clip.playbackRate = playbackRate;
            clip.fadeIn = std::min(duration, std::max(0.0, fadeIn - (start - arrangementStart)));
            clip.fadeOut = std::min(duration, std::max(0.0, fadeOut - (arrangementEnd - start - duration)));
            if (project.clips.size() >= 10000) throw std::runtime_error("Live Set exceeds 10000 arrangement clips.");
            project.clips.push_back(std::move(clip)); start += duration; remaining -= duration; offset += duration * playbackRate;
            if (++pieces > 10000) throw std::runtime_error("An audio loop creates too many clips.");
            if (!loop) { if (remaining > 1.0e-9) warn("An audio clip was shortened at its source-file end."); break; }
        }
    }
    void track(const Xml* input, int returnIndex)
    {
        Track output {newId(), value(input, "Name/EffectiveName", "Track")}; output.midi = input->hasTagName("MidiTrack"); output.returnIndex = returnIndex;
        output.group = input->hasTagName("GroupTrack"); output.groupCollapsed = output.group && !boolean(input, "TrackUnfolded", boolean(input, "Unfolded", true));
        output.folded = !boolean(input, "TrackUnfolded", true);
        output.height = static_cast<int>(juce::jlimit(17.0, 1000.0, number(input, "DeviceChain/AutomationLanes/AutomationLanes/AutomationLane/LaneHeight", 88)));
        output.annotation = value(input, "Name/Annotation");
        output.parentGroup = value(input, "TrackGroupId"); if (output.parentGroup == "-1") output.parentGroup.clear();
        trackIds[input->getStringAttribute("Id")] = output.id;
        if (returnIndex >= 0 || output.group) output.inputEnabled = false;
        mixer(input, output); devices(input, output);
        const auto* sequence = path(input, "DeviceChain/MainSequencer");
        const auto* events = path(sequence, output.midi ? "ClipTimeable/ArrangerAutomation/Events" : "Sample/ArrangerAutomation/Events");

        if (returnIndex < 0 && !output.group && events) for (auto* clip : events->getChildIterator())
        {
            if (!clip->hasTagName(output.midi ? "MidiClip" : "AudioClip")) continue;
            if (output.midi) midiClip(clip, output); else audioClip(clip, output);
        }
        if (sequence && count(path(sequence, "ClipSlotList"), output.midi ? "MidiClip" : "AudioClip") > 0) warn("Session View clips and scenes are intentionally omitted; Arrangement only.");
        project.tracks.push_back(std::move(output));
    }
};
}
LoadResult loadAbletonSet(const juce::File& source, const juce::File& directory)
{
    LoadResult output;
    try
    {
        auto root = juce::XmlDocument::parse(readXml(source)); const auto* live = path(root.get(), "LiveSet");
        if (!root || !root->hasTagName("Ableton") || !live) throw std::runtime_error("Not a supported Ableton Live Set.");
        Importer importer {source, directory};
        const auto* main = path(live, "MainTrack"); if (!main) main = path(live, "MasterTrack");
        importer.project.bpm = number(main, "DeviceChain/Mixer/Tempo/Manual", 120);
        if (importer.project.bpm <= 0 || importer.project.bpm > 1000) throw std::runtime_error("Unsupported project tempo.");
        importer.beat = 60 / importer.project.bpm;
        const auto signature = static_cast<int>(juce::jlimit(0.0, 692.0, number(main, "DeviceChain/Mixer/TimeSignature/Manual", 201)));
        importer.project.numerator = signature % 99 + 1; importer.project.denominator = 1 << (signature / 99);
        if (const auto* locators = path(live, "Locators/Locators")) for (auto* locator : locators->getChildIterator())
        { if (importer.project.locators.size() >= 4096) throw std::runtime_error("Live Set exceeds 4096 locators.");
          importer.project.locators.push_back({std::max(0.0, number(locator, "Time", 0) * importer.beat), value(locator, "Name"), value(locator, "Annotation")}); }
        std::stable_sort(importer.project.locators.begin(), importer.project.locators.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
#if JUCE_MAC
        for (const auto& entry : juce::RangedDirectoryIterator(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory), false, "Ableton*.app", juce::File::findDirectories))
        { const auto library = entry.getFile().getChildFile("Contents/App-Resources/Core Library"); if (library.isDirectory()) importer.libraries.push_back(library); }
#endif
        const auto* tracks = path(live, "Tracks"); if (!tracks || tracks->getNumChildElements() > 512) throw std::runtime_error("Live Set has no tracks or exceeds 512 tracks.");
        int returns = 0;
        for (auto* track : tracks->getChildIterator())
        {
            if (!track->hasTagName("GroupTrack") && !track->hasTagName("AudioTrack") && !track->hasTagName("MidiTrack") && !track->hasTagName("ReturnTrack")) continue;
            const bool aux = track->hasTagName("ReturnTrack");
            if (aux && returns >= maxReturns) throw std::runtime_error("Live Set exceeds 32 return tracks.");
            importer.track(track, aux ? returns++ : -1);
        }
        for (auto& track : importer.project.tracks) if (track.parentGroup.isNotEmpty())
        { if (const auto parent = importer.trackIds.find(track.parentGroup); parent != importer.trackIds.end()) track.parentGroup = parent->second;
          else { track.parentGroup.clear(); importer.warn("An unavailable parent group was removed from track routing."); } }
        for (auto& track : importer.project.tracks) if (track.outputTrack.isNotEmpty() && track.outputTrack != "master")
        { const auto target = importer.trackIds.find(track.outputTrack); const auto* destination = target != importer.trackIds.end() ? findTrack(importer.project, target->second) : nullptr;
          if (destination && !destination->midi && track.returnIndex < 0) track.outputTrack = destination->id;
          else { track.outputTrack = "master"; importer.warn("Unavailable/MIDI/return output route on " + track.name + " was mapped to Main."); } }
        for (auto& track : importer.project.tracks) if (routesTo(importer.project, track, track.id) || routingDepth(importer.project, track) >= 64)
        { track.outputTrack = "master"; importer.warn("Cyclic/deep audio routing on " + track.name + " was mapped to Main."); }
        std::stable_partition(importer.project.tracks.begin(), importer.project.tracks.end(), [](const auto& track) { return track.returnIndex < 0; });
        importer.mixer(main, importer.project.master); importer.devices(main, importer.project.master);
        importer.project.loopStart = std::max(0.0, number(live, "Transport/LoopStart", 0) * importer.beat);
        importer.project.loopEnd = importer.project.loopStart + std::max(0.001, number(live, "Transport/LoopLength", 4) * importer.beat);
        importer.project.loopEnabled = boolean(live, "Transport/LoopOn", false);
        for (auto* track : allTracks(importer.project)) track->sends.resize(static_cast<size_t>(std::max(2, returnCount(importer.project))), 0);
        output.result = validate(importer.project);
        if (output.result.failed()) return output;
        orderTracks(importer.project); output.project = std::move(importer.project);
        output.project.importNotes = "Imported: " + source.getFileName() + "\n" + juce::String(output.project.tracks.size()) + " tracks, " + juce::String(output.project.clips.size()) + " Arrangement clip segments.\n\n";
        output.project.importNotes += importer.warnings.joinIntoString("\n");
        output.project.importNotes += "\n\nCompatibility limits: Session clips/scenes and live launching are intentionally omitted. Editable Arrangement clips are retained. take lanes/comping, frozen processing, groove, MIDI/key mappings, external inputs/outputs, custom input routes, cyclic audio routes, MIDI routing and clip/device envelopes and tempo/meter changes and Live warp algorithms are not reproduced. Unavailable devices remain visible; their original Live XML is retained when within the state-size limit, but is not executed.\n";
        output.result = validate(output.project); if (output.result.failed()) return output;
        output.warning = importer.warnings.isEmpty() ? "Ableton arrangement imported" : "Ableton arrangement imported with limitations. See File > Import notes.";
    }
    catch (const std::exception& error) { output.result = juce::Result::fail("Could not import Live Set: " + juce::String(error.what())); }
    return output;
}
}
