// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/PluginHost.h"

namespace opentone
{
namespace
{
juce::File preferencesDirectory()
{ return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Opentone"); }
juce::PluginDescription utilityDescription()
{
    juce::PluginDescription description; description.name = "Utility"; description.descriptiveName = "Utility";
    description.pluginFormatName = "Opentone"; description.manufacturerName = "Opentone"; description.category = "Utility";
    description.fileOrIdentifier = "opentone.utility"; description.uniqueId = 0x4f545554;
    description.version = "1"; description.numInputChannels = description.numOutputChannels = 2; return description;
}
class Utility final : public juce::AudioProcessor
{
public:
    Utility() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        gain = new juce::AudioParameterFloat(juce::ParameterID {"gain", 1}, "Gain (dB)", juce::NormalisableRange<float>(-24, 12, 0.1f), 0);
        balance = new juce::AudioParameterFloat(juce::ParameterID {"balance", 1}, "Balance", juce::NormalisableRange<float>(-1, 1, 0.01f), 0);
        addParameter(gain); addParameter(balance);
    }
    const juce::String getName() const override { return "Utility"; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    { const auto output = layout.getMainOutputChannelSet(); return layout.getMainInputChannelSet() == output
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo()); }
    void prepareToPlay(double, int) override { previous = channelGains(); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        const auto next = channelGains();
        for (int channel = 0; channel < std::min(2, audio.getNumChannels()); ++channel)
            audio.applyGainRamp(channel, 0, audio.getNumSamples(), previous[static_cast<size_t>(channel)], next[static_cast<size_t>(channel)]);
        previous = next;
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& bytes) override
    { juce::MemoryOutputStream stream(bytes, false); stream.writeFloat(gain->get()); stream.writeFloat(balance->get()); }
    void setStateInformation(const void* data, int size) override
    {
        if (size != 8) return;
        juce::MemoryInputStream stream(data, static_cast<size_t>(size), false); const auto g = stream.readFloat(), b = stream.readFloat();
        if (!std::isfinite(g) || !std::isfinite(b)) return;
        gain->setValueNotifyingHost(gain->convertTo0to1(juce::jlimit(-24.0f, 12.0f, g)));
        balance->setValueNotifyingHost(balance->convertTo0to1(juce::jlimit(-1.0f, 1.0f, b)));
    }
private:
    std::array<float, 2> channelGains() const
    { const auto volume = juce::Decibels::decibelsToGain(gain->get()), pan = balance->get();
      return { volume * (pan > 0 ? 1 - pan : 1), volume * (pan < 0 ? 1 + pan : 1) }; }
    juce::AudioParameterFloat *gain = nullptr, *balance = nullptr;
    std::array<float, 2> previous {1, 1};
};
class Scanner final : public juce::KnownPluginList::CustomScanner
{
    bool findPluginTypesFor(juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                            const juce::String& identifier) override
    {
        const auto output = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("opentone-scan", ".xml");
        juce::ChildProcess child;
        const juce::StringArray args { juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
            "--scan-plugin", juce::Base64::toBase64(format.getName()), juce::Base64::toBase64(identifier), juce::Base64::toBase64(output.getFullPathName()) };
        if (!child.start(args, 0)) return false;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 60000;
        while (child.isRunning() && !shouldExit() && juce::Time::getMillisecondCounterHiRes() < deadline)
            child.waitForProcessToFinish(100);
        if (child.isRunning()) { child.kill(); output.deleteFile(); return false; }
        const auto xml = juce::XmlDocument::parse(output); output.deleteFile();
        if (!xml || child.getExitCode() != 0) return false;
        for (auto* item : xml->getChildIterator())
        { auto description = std::make_unique<juce::PluginDescription>();
          if (description->loadFromXml(*item)) result.add(description.release()); }
        return true;
    }
};
}
class PluginHost::Window final : public juce::DocumentWindow, private juce::KeyListener
{
public:
    Window(PluginHost& host, const juce::String& name, juce::AudioProcessorEditor* editor)
        : DocumentWindow(name, juce::Colour(0xff30343b), closeButton), owner(host)
    { setUsingNativeTitleBar(true); setContentOwned(editor, true); setResizable(editor->isResizable(), false); setAlwaysOnTop(true);
      addKeyListener(this); editor->addKeyListener(this); centreWithSize(getWidth(), getHeight()); setVisible(true); }
    void closeButtonPressed() override { setVisible(false); }
private:
    bool keyPressed(const juce::KeyPress& key, juce::Component* source) override
    { return owner.keyboardListener && owner.keyboardListener->keyPressed(key, source); }
    bool keyStateChanged(bool down, juce::Component* source) override
    { return owner.keyboardListener && owner.keyboardListener->keyStateChanged(down, source); }
    PluginHost& owner;
};
PluginHost::PluginHost()
{
    juce::addDefaultFormatsToManager(formats);
    juce::PropertiesFile::Options options; options.applicationName = "Opentone"; options.filenameSuffix = ".plugins";
    options.osxLibrarySubFolder = "Application Support";
    properties = std::make_unique<juce::PropertiesFile>(options);
    if (auto xml = properties->getXmlValue("list")) known.recreateFromXml(*xml);
    if (auto xml = properties->getXmlValue("categories")) for (auto* item : xml->getChildIterator())
        if (item->hasTagName("plugin") && item->getStringAttribute("id").isNotEmpty()) categories[item->getStringAttribute("id")] = item->getBoolAttribute("instrument");
    known.setCustomScanner(std::make_unique<Scanner>()); known.addChangeListener(this);
}
PluginHost::~PluginHost()
{
    known.removeChangeListener(this); windows.clear();
    for (auto& pair : slots) if (pair.second->instance)
    { pair.second->instance->removeListener(this); pair.second->instance->releaseResources(); }
}
juce::Array<juce::PluginDescription> PluginHost::availablePlugins() const
{
    auto result = known.getTypes();
    for (auto& plugin : result) if (const auto found = categories.find(plugin.createIdentifierString()); found != categories.end()) plugin.isInstrument = found->second;
    result.add(utilityDescription()); return result;
}
void PluginHost::assignCategory(const juce::String& identifier, bool instrument)
{
    categories[identifier] = instrument; juce::XmlElement xml("categories");
    for (const auto& item : categories)
    { auto* entry = xml.createNewChildElement("plugin"); entry->setAttribute("id", item.first); entry->setAttribute("instrument", item.second); }
    properties->setValue("categories", &xml); properties->saveIfNeeded(); sendChangeMessage();
}
void PluginHost::changeListenerCallback(juce::ChangeBroadcaster*)
{ if (auto xml = known.createXml()) properties->setValue("list", xml.get()); properties->saveIfNeeded(); sendChangeMessage(); }
std::unique_ptr<juce::Component> PluginHost::scanner()
{
    preferencesDirectory().createDirectory();
    auto component = std::make_unique<juce::PluginListComponent>(formats, known, preferencesDirectory().getChildFile("scan-in-progress.txt"), properties.get(), true);
    component->setNumberOfThreadsForScanning(1); component->setOptionsButtonText("Scan / folders...");
    component->setScanDialogText("Scan plug-ins", "Discovering installed plug-ins");
    return component;
}
bool PluginHost::scanChild(const juce::String& commandLine)
{
    auto args = juce::StringArray::fromTokens(commandLine, true); args.removeEmptyStrings();
    for (auto& argument : args) argument = argument.unquoted();
    if (args.isEmpty() || args[0] != "--scan-plugin") return false;
    if (args.size() != 4) { juce::JUCEApplication::getInstance()->setApplicationReturnValue(1); juce::JUCEApplication::quit(); return true; }
    for (int i = 1; i < args.size(); ++i)
    { juce::MemoryOutputStream decoded; if (!juce::Base64::convertFromBase64(decoded, args[i]))
      { juce::JUCEApplication::getInstance()->setApplicationReturnValue(1); juce::JUCEApplication::quit(); return true; }
      args.set(i, decoded.toString()); }
    juce::AudioPluginFormatManager manager; juce::addDefaultFormatsToManager(manager);
    juce::XmlElement xml("plugins"); bool found = false;
    for (auto* format : manager.getFormats()) if (format->getName() == args[1])
    { juce::OwnedArray<juce::PluginDescription> descriptions; format->findAllTypesForFile(descriptions, args[2]);
      for (const auto* description : descriptions) xml.addChildElement(description->createXml().release()); found = true; break; }
    const bool saved = found && xml.writeTo(juce::File(args[3]));
    juce::JUCEApplication::getInstance()->setApplicationReturnValue(saved ? 0 : 1); juce::JUCEApplication::quit(); return true;
}
std::shared_ptr<PluginHost::Bus> PluginHost::bus(const juce::String& id) const
{ const auto it = buses.find(id); return it == buses.end() ? nullptr : it->second; }
void PluginHost::prepareSlot(Slot& slot)
{
    if (!slot.instance) return;
    auto& instance = *slot.instance;
    auto layout = instance.getBusesLayout();
    for (int i = 1; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference(i) = juce::AudioChannelSet::disabled();
    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference(i) = juce::AudioChannelSet::disabled();
    if (!layout.inputBuses.isEmpty()) layout.inputBuses.getReference(0) = slot.model.instrument ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo();
    if (!layout.outputBuses.isEmpty()) layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    if (!instance.setBusesLayout(layout))
    { if (!layout.inputBuses.isEmpty()) layout.inputBuses.getReference(0) = slot.model.instrument ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::mono();
      if (!layout.outputBuses.isEmpty()) layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono(); instance.setBusesLayout(layout); }
    if (instance.getTotalNumInputChannels() > 2 || instance.getTotalNumOutputChannels() > 2 || instance.getTotalNumOutputChannels() < 1)
    { slot.error = "This plug-in does not support a mono or stereo output."; return; }
    instance.setRateAndBufferSizeDetails(rate, block); instance.setPlayHead(this); instance.prepareToPlay(rate, block);
}
void PluginHost::prepare(double nextRate, int nextBlock)
{
    const juce::ScopedLock lock(mutex); rate = nextRate; block = std::max(1, nextBlock); expectedFrame = -1;
    for (auto& pair : buses) pair.second->audio.setSize(2, block);
    for (auto& pair : slots) if (pair.second->instance)
    { pair.second->instance->releaseResources(); prepareSlot(*pair.second); }
}
void PluginHost::load(const std::shared_ptr<Slot>& slot)
{
    juce::PluginDescription description;
    const auto xml = juce::XmlDocument::parse(slot->model.description);
    if (!xml || !description.loadFromXml(*xml) || description.isInstrument != slot->model.instrument) { slot->loading = false; slot->error = "Invalid plug-in description."; return; }
    juce::WeakReference<PluginHost> owner(this); const auto generation = projectGeneration;
    auto loaded = [owner, slot, generation](std::unique_ptr<juce::AudioProcessor> instance, const juce::String& error)
    {
        if (!owner || owner->projectGeneration != generation) return;
        const auto found = owner->slots.find(slot->model.id); if (found == owner->slots.end() || found->second != slot) return;
        const juce::ScopedLock lock(owner->mutex); slot->loading = false; slot->error = error; slot->instance = std::move(instance);
        if (slot->instance)
        { juce::MemoryBlock bytes; if (slot->model.state.isNotEmpty() && bytes.fromBase64Encoding(slot->model.state)) slot->instance->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
          owner->prepareSlot(*slot); slot->instance->addListener(owner.get());
          for (auto& pair : owner->buses) pair.second->chase = true; }
        owner->sendChangeMessage();
    };
    if (description.pluginFormatName == "Opentone")
    { if (description.fileOrIdentifier == "opentone.utility") loaded(std::make_unique<Utility>(), {});
      else loaded(nullptr, "Unknown built-in effect."); }
    else formats.createPluginInstanceAsync(description, rate, block, std::move(loaded));
}
void PluginHost::sync(const Project& project, uint64_t generation, bool allowLoop)
{
    std::vector<std::shared_ptr<Slot>> loading;
    {
        const juce::ScopedLock lock(mutex);
        looping = project.loopEnabled && allowLoop; loopStart = project.loopStart; loopEnd = project.loopEnd;
        if (generation != projectGeneration)
        { windows.clear(); for (auto& pair : slots) if (pair.second->instance) pair.second->instance->removeListener(this);
          slots.clear(); buses.clear(); projectGeneration = generation; expectedFrame = -1; }
        std::set<juce::String> retained, tracks;
        const bool solo = std::any_of(project.tracks.begin(), project.tracks.end(), [](const auto& track) { return track.solo; });
        for (const auto* item : allTracks(project))
        {
            const auto& track = *item;
            tracks.insert(track.id); auto& input = buses[track.id];
            if (!input) { input = std::make_shared<Bus>(); input->audio.setSize(2, block); input->midi.ensureSize(65536); input->queued.ensureSize(4096); }
            input->muted = track.mute || (track.id != "master" && solo && !track.solo);
            input->baseGain = static_cast<float>(track.gain); input->basePan = static_cast<float>(track.pan);
            input->target = input->muted ? 0 : input->baseGain;
            input->volumeAutomation = track.volumeAutomation; input->panAutomation = track.panAutomation; 
            bool changed = input->slots.size() != track.plugins.size();
            if (!changed) for (size_t i = 0; i < track.plugins.size(); ++i) if (input->slots[i]->model.id != track.plugins[i].id) changed = true;
            if (changed) input->chase = true;
            input->slots.clear();
            for (const auto& plugin : track.plugins)
            {
                retained.insert(plugin.id); auto& slot = slots[plugin.id];
                if (!slot) { slot = std::make_shared<Slot>(); slot->model = plugin; loading.push_back(slot); input->chase = true; }
                else
                { if (slot->model.state != plugin.state && slot->instance && slot->capturedState != plugin.state)
                  { juce::MemoryBlock bytes; if (bytes.fromBase64Encoding(plugin.state)) slot->instance->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize())); input->chase = true; }
                  if (slot->model.bypass != plugin.bypass) input->chase = true;
                  slot->model = plugin; }
                slot->track = track.id; input->slots.push_back(slot);
            }
        }
        for (auto it = buses.begin(); it != buses.end();) if (!tracks.count(it->first)) it = buses.erase(it); else ++it;
        for (auto it = slots.begin(); it != slots.end();)
            if (!retained.count(it->first))
            { windows.erase(it->first); if (it->second->instance) it->second->instance->removeListener(this); it = slots.erase(it); }
            else ++it;
    }
    for (const auto& slot : loading) load(slot);
}
void PluginHost::transport(juce::int64 nextFrame, double nextBpm, bool nextPlaying)
{
    if (nextFrame != expectedFrame || nextPlaying != wasPlaying)
        for (auto& pair : buses) pair.second->chase = true;
    frame = nextFrame; bpm = nextBpm; playing = nextPlaying;
}
void PluginHost::beginBlock(juce::int64 nextFrame, double nextBpm, bool nextPlaying, int samples)
{
    transport(nextFrame, nextBpm, nextPlaying);
    for (auto& pair : buses)
    {
        auto& input = *pair.second; input.audio.clear(); input.midi.clear();
        input.target = input.muted ? 0 : static_cast<float>(automationValue(input.volumeAutomation, static_cast<double>(frame) / rate, input.baseGain));
        input.pan = static_cast<float>(automationValue(input.panAutomation, static_cast<double>(frame) / rate, input.basePan));
        input.midi.addEvents(input.queued, 0, -1, 0); input.queued.clear();
        uint64_t low = 0, high = 0;
        for (size_t pitch = 0; pitch < input.previews.size(); ++pitch)
        {
            auto& note = input.previews[pitch]; if (!note.on) continue;
            if (!note.held && note.end < previewClock + samples)
            { input.midi.addEvent(juce::MidiMessage::noteOff(1, static_cast<int>(pitch)), static_cast<int>(std::max<juce::int64>(0, note.end - previewClock))); note.on = false; }
            if (note.on) { if (pitch < 64) low |= uint64_t(1) << pitch; else high |= uint64_t(1) << (pitch - 64); }
        }
        input.low.store(low); input.high.store(high);
    }
}
void PluginHost::finishBlock(float* const* output, int channels, int samples, float master, float masterPan)
{
    const auto decay = static_cast<float>(std::exp(-samples / (rate * 0.15)));
    const auto process = [&](Bus& input, bool isMaster)
    {
        auto midi = &input.midi;
        // shortcut: parallel tracks have no latency compensation; add it when hosting look-ahead effects.
        for (const auto& slot : input.slots)
        {
            if (!slot->instance || slot->error.isNotEmpty()) continue;
            if (slot->model.bypass) { if (slot->model.instrument) input.audio.clear(); continue; }
            auto& instance = *slot->instance;
            const int count = std::max(1, std::max(instance.getTotalNumInputChannels(), instance.getTotalNumOutputChannels()));
            if (count == 1 && !slot->model.instrument)
            { input.audio.addFrom(0, 0, input.audio, 1, 0, samples); input.audio.applyGain(0, 0, samples, 0.5f); }
            juce::AudioBuffer<float> buffer(input.audio.getArrayOfWritePointers(), count, samples);
            instance.processBlock(buffer, *midi);
            if (instance.getTotalNumOutputChannels() == 1) input.audio.copyFrom(1, 0, input.audio, 0, 0, samples);
        }
        const auto end = static_cast<double>(frame + (playing ? samples : 0)) / rate;
        const auto gain = input.muted ? 0.0f : static_cast<float>(automationValue(input.volumeAutomation, end, isMaster ? master : input.baseGain));
        const auto pan = static_cast<float>(automationValue(input.panAutomation, end, isMaster ? masterPan : input.basePan));
        for (int channel = 0; channel < 2; ++channel)
            input.audio.applyGainRamp(channel, 0, samples, input.previousGain * panGain(input.previousPan, channel), gain * panGain(pan, channel));
        input.previousGain = gain; input.previousPan = pan;
        input.peakLeft.store(std::max(input.audio.getMagnitude(0, 0, samples), input.peakLeft.load() * decay));
        input.peakRight.store(std::max(input.audio.getMagnitude(1, 0, samples), input.peakRight.load() * decay));
        input.chase = false;
    };
    for (auto& pair : buses)
    {
        if (pair.first == "master") continue;
        auto& input = *pair.second; process(input, false);
        for (int channel = 0; channel < std::min(channels, 2); ++channel) if (output[channel])
            juce::FloatVectorOperations::add(output[channel], input.audio.getReadPointer(channel), samples);
    }
    if (const auto found = buses.find("master"); found != buses.end())
    {
        auto& input = *found->second;
        for (int channel = 0; channel < 2; ++channel)
            if (channel < channels && output[channel]) input.audio.copyFrom(channel, 0, output[channel], samples);
            else input.audio.clear(channel, 0, samples);
        process(input, true);
        for (int channel = 0; channel < std::min(channels, 2); ++channel) if (output[channel])
            juce::FloatVectorOperations::copy(output[channel], input.audio.getReadPointer(channel), samples);
    }
    expectedFrame = playing ? frame + samples : frame; wasPlaying = playing; previewClock += samples;
}
juce::Optional<juce::AudioPlayHead::PositionInfo> PluginHost::getPosition() const
{
    juce::AudioPlayHead::PositionInfo info;
    const auto seconds = static_cast<double>(frame) / rate, beats = seconds * bpm / 60;
    info.setTimeInSamples(frame); info.setTimeInSeconds(seconds); info.setBpm(bpm);
    info.setTimeSignature(juce::AudioPlayHead::TimeSignature {4, 4});
    info.setPpqPosition(beats); info.setPpqPositionOfLastBarStart(std::floor(beats / 4) * 4);
    info.setIsPlaying(playing); info.setIsRecording(false); info.setIsLooping(looping);
    info.setLoopPoints(juce::AudioPlayHead::LoopPoints {loopStart * bpm / 60, loopEnd * bpm / 60});
    return info;
}
bool PluginHost::preview(const juce::String& track, int pitch, double velocity, bool held, bool replace)
{
    if (pitch < 0 || pitch > 127 || !std::isfinite(velocity) || velocity <= 0 || velocity > 1) return false;
    const juce::ScopedLock lock(mutex); const auto input = bus(track);
    if (!input || input->slots.empty() || !input->slots.front()->model.instrument) return false;
    if (replace && !held) release(track, -1, true);
    auto& note = input->previews[static_cast<size_t>(pitch)];
    if (note.on) input->queued.addEvent(juce::MidiMessage::noteOff(1, pitch), 0);
    input->queued.addEvent(juce::MidiMessage::noteOn(1, pitch, static_cast<float>(velocity)), 0);
    note = {true, held, previewClock + static_cast<juce::int64>(rate * 0.25), static_cast<float>(velocity)}; return true;
}
void PluginHost::release(const juce::String& track, int pitch, bool editsOnly)
{
    const juce::ScopedLock lock(mutex); const auto input = bus(track); if (!input) return;
    for (size_t i = 0; i < input->previews.size(); ++i)
    { auto& note = input->previews[i]; if (note.on && (pitch < 0 || static_cast<int>(i) == pitch) && (!editsOnly || !note.held))
      { input->queued.addEvent(juce::MidiMessage::noteOff(1, static_cast<int>(i)), 0); note.on = false; } }
}
std::bitset<128> PluginHost::activeNotes(const juce::String& track) const
{
    const auto input = bus(track); if (!input) return {};
    std::bitset<128> notes; const auto low = input->low.load(), high = input->high.load();
    for (size_t i = 0; i < 64; ++i) { if (low & (uint64_t(1) << i)) notes.set(i); if (high & (uint64_t(1) << i)) notes.set(i + 64); }
    return notes;
}
std::pair<float, float> PluginHost::levels(const juce::String& track) const
{ const auto input = bus(track); return input ? std::make_pair(input->peakLeft.load(), input->peakRight.load()) : std::make_pair(0.0f, 0.0f); }
std::map<juce::String, juce::String> PluginHost::captureStates(bool force)
{
    std::map<juce::String, juce::String> result;
    if (!force && !stateDirty.exchange(false)) return result;
    const juce::ScopedLock lock(mutex);
    for (auto& pair : slots) if (pair.second->instance && pair.second->error.isEmpty())
    { juce::MemoryBlock bytes; pair.second->instance->getStateInformation(bytes); auto state = bytes.toBase64Encoding();
      if (state != pair.second->model.state) { result[pair.first] = state; pair.second->capturedState = state; } }
    return result;
}
juce::String PluginHost::focusedEditorTrack(const juce::Component* source) const
{
    for (const auto& pair : windows) if (pair.second->isVisible() && (source ? source->getTopLevelComponent() == pair.second.get()
            : pair.second->isActiveWindow() || pair.second->hasKeyboardFocus(true)))
        if (const auto found = slots.find(pair.first); found != slots.end()) return found->second->track;
    return {};
}
void PluginHost::updateEditorWindowLevels()
{
    const bool active = juce::Process::isForegroundProcess();
    for (const auto& pair : windows) pair.second->setAlwaysOnTop(active);
}
void PluginHost::showEditor(const juce::String& id)
{
    const auto found = slots.find(id); if (found == slots.end() || !found->second->instance) return;
    auto& window = windows[id];
    if (!window)
    { auto& instance = *found->second->instance;
      auto* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : new juce::GenericAudioProcessorEditor(instance);
      if (editor) window = std::make_unique<Window>(*this, found->second->model.name, editor); }
    if (window) { window->setVisible(true); window->toFront(true); }
}
juce::String PluginHost::status(const juce::String& id) const
{ const auto found = slots.find(id); if (found == slots.end()) return {};
  return found->second->loading ? "Loading..." : found->second->error; }
}
