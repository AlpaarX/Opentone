// SPDX-License-Identifier: AGPL-3.0-only
// Included inside BuiltinPlugins.cpp to share the native device and FM implementation.
struct Operator final : FM
{
    struct ExtraVoice
    {
        juce::ADSR filterEnvelope, pitchEnvelope;
        Biquad filter; double pitch = 1; int filterCounter = 0;
    };
    Operator() : FM(false, true)
    {
        name = "Operator"; id = "opentone.operator"; perVoiceFilter = true;
        toggle("filter_on", "Filter on", false); number("filter_env", "Filter envelope (oct)", -8, 8, 0);
        number("filter_attack", "Filter attack (ms)", 0.1f, 20000, 0.1f, 100);
        number("filter_decay", "Filter decay (ms)", 1, 60000, 600, 400);
        number("filter_sustain", "Filter sustain", 0, 1, 0); number("filter_release", "Filter release (ms)", 1, 60000, 50, 400);
        toggle("pitch_on", "Pitch envelope on", false); number("pitch_peak", "Pitch peak (st)", -96, 96, 12);
        number("pitch_attack", "Pitch attack (ms)", 0.1f, 20000, 0.1f, 100);
        number("pitch_decay", "Pitch decay (ms)", 1, 60000, 600, 400);
        number("pitch_sustain", "Pitch sustain (st)", -96, 96, 0); number("pitch_release", "Pitch release (ms)", 1, 60000, 50, 400);
        number("transpose", "Transpose (st)", -48, 48, 0);
        for (int op = 0; op < 4; ++op)
        {
            const auto p = prefix(op); toggle(p + "fixed", "Fixed frequency", false);
            number(p + "frequency", "Frequency (Hz)", 0.01f, 20000, 100, 100);
            number(p + "phase", "Phase", 0, 1, 0); number(p + "velocity", "Velocity sensitivity", 0, 1, 1);
        }
    }
    void prepareToPlay(double rate, int block) override
    {
        FM::prepareToPlay(rate, block);
        for (auto& voice : extra)
        { voice.filter.reset(); voice.filterEnvelope.setSampleRate(rate * 2); voice.pitchEnvelope.setSampleRate(rate * 2); voice.filterEnvelope.reset(); voice.pitchEnvelope.reset(); voice.filterCounter = 0; }
    }
    void updateSettings() override
    {
        FM::updateSettings();
        filterOn = value("filter_on") > 0.5f; filterAmount = value("filter_env"); cutoff = value("filter"); q = 0.5 + value("resonance") * 4;
        pitchOn = value("pitch_on") > 0.5f; pitchPeak = value("pitch_peak"); pitchSustain = value("pitch_sustain"); transpose = std::pow(2.0, value("transpose") / 12.0);
        const juce::ADSR::Parameters filterParams {value("filter_attack") / 1000, value("filter_decay") / 1000, value("filter_sustain"), value("filter_release") / 1000};
        const juce::ADSR::Parameters pitchParams {value("pitch_attack") / 1000, value("pitch_decay") / 1000, 0, value("pitch_release") / 1000};
        for (auto& voice : extra) { voice.filterEnvelope.setParameters(filterParams); voice.pitchEnvelope.setParameters(pitchParams); }
        for (int op = 0; op < 4; ++op)
        { const auto p = prefix(op); const auto i = static_cast<size_t>(op);
          fixed[i] = value(p + "fixed") > 0.5f; frequencies[i] = value(p + "frequency"); phases[i] = value(p + "phase"); velocities[i] = value(p + "velocity"); }
    }
    void message(const juce::MidiMessage& midi) override
    {
        FM::message(midi);
        if (midi.isNoteOn()) for (size_t i = 0; i < voices.size(); ++i) if (voices[i].note >= 0 && voices[i].age == age)
        { voices[i].phase = phases; auto& voice = extra[i]; voice.filter.reset(); voice.filterCounter = 0;
          voice.filterEnvelope.reset(); voice.pitchEnvelope.reset(); voice.filterEnvelope.noteOn(); voice.pitchEnvelope.noteOn(); }
    }
    void release(Voice& voice, bool immediate) override
    {
        FM::release(voice, immediate); auto& state = extra[static_cast<size_t>(&voice - voices.data())];
        if (immediate) { state.filterEnvelope.reset(); state.pitchEnvelope.reset(); }
        else { state.filterEnvelope.noteOff(); state.pitchEnvelope.noteOff(); }
    }
    double oscillator(Voice& voice, int op, double modulation) override
    {
        const auto i = static_cast<size_t>(op); const auto& s = settings[i];
        const auto& state = extra[static_cast<size_t>(&voice - voices.data())];
        const auto frequency = (fixed[i] ? frequencies[i] : voice.frequency * s.ratio * transpose) * state.pitch * bend[static_cast<size_t>(voice.channel)];
        const auto step = std::min(0.45, frequency / (sampleRate * 2));
        voice.phase[i] += step; voice.phase[i] -= std::floor(voice.phase[i]);
        const auto envelope = voice.envelopes[i].getNextSample();
        const auto velocity = 1 - velocities[i] + velocities[i] * voice.velocity;
        return waveAt(voice.phase[i] + modulation * 2, s.wave) * envelope * s.level * velocity / std::max(0.001f, voice.velocity);
    }
    double voiceSample(Voice& voice) override
    {
        auto& state = extra[static_cast<size_t>(&voice - voices.data())];
        const auto pitch = state.pitchEnvelope.getNextSample();
        state.pitch = pitchOn ? std::pow(2.0, (pitchSustain + pitch * (pitchPeak - pitchSustain)) / 12.0) : 1;
        auto sample = static_cast<float>(FM::voiceSample(voice)); const auto envelope = state.filterEnvelope.getNextSample();
        if (filterOn)
        {
            if (state.filterCounter++ % 16 == 0)
                state.filter.set(juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate * 2,
                    juce::jlimit(20.0, sampleRate * 0.45, cutoff * std::pow(2.0, filterAmount * envelope * envelope)), q));
            sample = state.filter.sample(0, sample);
        }
        return sample;
    }
    std::array<ExtraVoice, 16> extra;
    std::array<bool, 4> fixed {}; std::array<double, 4> frequencies {}, phases {}, velocities {};
    bool filterOn = false, pitchOn = false; double filterAmount = 0, cutoff = 18000, q = 0.7, pitchPeak = 12, pitchSustain = 0, transpose = 1;
};
struct Tube final : Device
{
    Tube() : Device("Dynamic Tube", "opentone.tube")
    {
        number("drive", "Drive (dB)", 0, 35, 0); number("output", "Output (dB)", -35, 35, 0);
        number("bias", "Bias", -1, 1, 0); number("envelope", "Envelope", -1, 1, 0);
        number("attack", "Attack (ms)", 0.1f, 200, 5, 10); number("release", "Release (ms)", 1, 1000, 35, 100);
        number("tone", "Tone", -1, 1, 0); choice("type", "Tube", {"A", "B", "C"}); number("mix", "Dry/Wet", 0, 1, 1);
        for (int type = 0; type < 3; ++type) { jassert(curve(0, type) == 0); jassert(std::isfinite(curve(100, type))); }
    }
    void prepareToPlay(double rate, int block) override
    {
        sampleRate = rate; oversampling.initProcessing(static_cast<size_t>(block)); oversampling.reset();
        mixer.prepare({rate, static_cast<juce::uint32>(block), 2}); mixer.reset(); mixer.setWetLatency(oversampling.getLatencyInSamples());
        setLatencySamples(static_cast<int>(std::ceil(oversampling.getLatencyInSamples()))); dc.reset(); tone.reset(); envelope = 0;
        drive.reset(rate * 2, 0.02); output.reset(rate, 0.02); drive.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(value("drive"))); output.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(value("output")));
    }
    static float curve(float x, int type)
    { if (type == 1) return std::tanh(x + 0.18f * x * x / (1 + std::abs(x))); if (type == 2) return static_cast<float>(std::atan(x * 1.8f) / std::atan(1.8f)); return std::tanh(x); }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard; juce::dsp::AudioBlock<float> base(audio); mixer.setWetMixProportion(value("mix")); mixer.pushDrySamples(base);
        auto up = oversampling.processSamplesUp(base); const auto bias = value("bias"), amount = value("envelope"); const auto type = static_cast<int>(value("type"));
        const auto attack = std::exp(-1 / (sampleRate * 2 * value("attack") / 1000)), release = std::exp(-1 / (sampleRate * 2 * value("release") / 1000));
        drive.setTargetValue(juce::Decibels::decibelsToGain(value("drive"))); output.setTargetValue(juce::Decibels::decibelsToGain(value("output")));
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const auto peak = std::max(std::abs(up.getChannelPointer(0)[i]), std::abs(up.getChannelPointer(1)[i]));
            const auto coefficient = peak > envelope ? attack : release; envelope = static_cast<float>(coefficient * envelope + (1 - coefficient) * peak);
            const auto offset = juce::jlimit(-2.0f, 2.0f, bias + amount * envelope), gain = drive.getNextValue();
            for (int channel = 0; channel < 2; ++channel) up.getChannelPointer(static_cast<size_t>(channel))[i] = curve(up.getChannelPointer(static_cast<size_t>(channel))[i] * gain + offset, type) - curve(offset, type);
        }
        oversampling.processSamplesDown(base);
        dc.set(juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(sampleRate, 15));
        tone.set(juce::dsp::IIR::ArrayCoefficients<double>::makeHighShelf(sampleRate, std::min(2000.0, sampleRate * 0.4), 0.707, juce::Decibels::decibelsToGain(static_cast<double>(value("tone") * 12))));
        for (int i = 0; i < audio.getNumSamples(); ++i) { const auto gain = output.getNextValue(); for (int c = 0; c < 2; ++c) audio.setSample(c, i, tone.sample(c, dc.sample(c, audio.getSample(c, i))) * gain); }
        mixer.mixWetSamples(base);
    }
    juce::dsp::Oversampling<float> oversampling {2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true};
    juce::dsp::DryWetMixer<float> mixer {128}; Biquad dc, tone; juce::SmoothedValue<float> drive, output; double sampleRate = 48000; float envelope = 0;
};
struct NoteLength final : Device
{
    NoteLength() : Device("Note Length", "opentone.notelength")
    { number("length", "Length (ms)", 1, 60000, 100, 100); number("gate", "Gate (%)", 1, 200, 100); toggle("sync", "Tempo sync", false); choice("division", "Division", {"1/16", "1/8", "1/4", "1/2", "1 bar"}, 2);
       #if JUCE_DEBUG
        prepareToPlay(1000, 128); juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); midi.addEvent(juce::MidiMessage::noteOff(1, 60), 1);
        processBlock(audio, midi); jassert(midi.getNumEvents() == 2);
        for (const auto event : midi) if (event.getMessage().isNoteOff()) jassert(event.samplePosition == 100);
       #endif
    }
    bool acceptsMidi() const override { return true; } bool producesMidi() const override { return true; }
    void prepareToPlay(double rate, int) override { sampleRate = rate; clock = 0; ends.fill(-1); output.ensureSize(32768); }
    void flush(juce::int64 until, int block)
    {
        for (size_t key = 0; key < ends.size(); ++key) if (ends[key] >= 0 && ends[key] <= until)
        { output.addEvent(juce::MidiMessage::noteOff(static_cast<int>(key / 128) + 1, static_cast<int>(key % 128)), juce::jlimit(0, block - 1, static_cast<int>(ends[key] - clock))); ends[key] = -1; }
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        output.clear(); const auto count = audio.getNumSamples(); if (count == 0) return;
        double bpm = 120; if (auto* head = getPlayHead()) if (auto position = head->getPosition()) if (position->getBpm()) bpm = *position->getBpm();
        const auto seconds = value("sync") > 0.5f ? std::pow(2.0, value("division") - 2) * 60 / std::max(1.0, bpm) : value("length") / 1000;
        const auto length = std::max<juce::int64>(1, static_cast<juce::int64>(seconds * value("gate") / 100 * sampleRate));
        for (const auto metadata : midi)
        {
            const auto at = juce::jlimit(0, count - 1, metadata.samplePosition); flush(clock + at, count); const auto message = metadata.getMessage();
            if (message.isAllNotesOff() || message.isAllSoundOff())
            { for (int key = (message.getChannel() - 1) * 128; key < message.getChannel() * 128; ++key) ends[static_cast<size_t>(key)] = -1; output.addEvent(message, at); }
            else if (message.isNoteOn())
            { const auto key = static_cast<size_t>((message.getChannel() - 1) * 128 + message.getNoteNumber());
              if (ends[key] >= 0) output.addEvent(juce::MidiMessage::noteOff(message.getChannel(), message.getNoteNumber()), at);
              output.addEvent(message, at); ends[key] = clock + at + length; }
            else if (!message.isNoteOff()) output.addEvent(message, at);
        }
        flush(clock + count - 1, count); midi.swapWith(output); clock += count;
    }
    std::array<juce::int64, 2048> ends {}; juce::MidiBuffer output; juce::int64 clock = 0; double sampleRate = 48000;
};
struct DrumRack final : Device
{
    struct Slot { juce::String identifier; bool bypass = false; std::unique_ptr<juce::AudioProcessor> processor; };
    struct Branch { juce::String name; int receive = -1, send = 60; float gain = 1, pan = 0; bool mute = false; std::vector<Slot> slots; };
    DrumRack() : Device("Drum Rack", "opentone.drumrack", true) {}
    void prepareToPlay(double rate, int block) override
    {
        sampleRate = rate; blockSize = block; scratch.setSize(2, block); notes.ensureSize(32768);
        for (auto& branch : branches) for (auto& slot : branch.slots)
        { slot.processor->setPlayHead(getPlayHead()); slot.processor->setRateAndBufferSizeDetails(rate, block); slot.processor->prepareToPlay(rate, block); }
    }
    void releaseResources() override { for (auto& branch : branches) for (auto& slot : branch.slots) slot.processor->releaseResources(); }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        audio.clear(); const auto count = audio.getNumSamples(); jassert(count <= scratch.getNumSamples());
        for (auto& branch : branches) if (!branch.mute)
        {
            scratch.clear(); notes.clear();
            for (const auto metadata : midi)
            { auto note = metadata.getMessage(); if (note.isNoteOnOrOff() || note.isAftertouch())
              { if (branch.receive >= 0 && note.getNoteNumber() != branch.receive) continue; if (branch.receive >= 0) note.setNoteNumber(branch.send); }
              notes.addEvent(note, metadata.samplePosition); }
            juce::AudioBuffer<float> buffer(scratch.getArrayOfWritePointers(), 2, count);
            for (auto& slot : branch.slots) if (!slot.bypass) slot.processor->processBlock(buffer, notes);
            for (int channel = 0; channel < 2; ++channel) audio.addFrom(channel, 0, scratch, channel, 0, count, branch.gain * static_cast<float>(panGain(branch.pan, channel)));
        }
    }
    void getStateInformation(juce::MemoryBlock& bytes) override
    {
        juce::XmlElement xml("OpentoneRack"); xml.setAttribute("version", 1);
        for (auto& branch : branches)
        {
            auto* item = xml.createNewChildElement("branch"); item->setAttribute("name", branch.name); item->setAttribute("receive", branch.receive); item->setAttribute("send", branch.send);
            item->setAttribute("gain", branch.gain); item->setAttribute("pan", branch.pan); item->setAttribute("mute", branch.mute);
            for (auto& slot : branch.slots)
            { auto* child = item->createNewChildElement("device"); child->setAttribute("id", slot.identifier); child->setAttribute("bypass", slot.bypass);
              juce::MemoryBlock state; slot.processor->getStateInformation(state); child->setAttribute("state", state.toBase64Encoding()); }
        }
        copyXmlToBinary(xml, bytes);
    }
    void setStateInformation(const void* data, int size) override
    {
        static thread_local int depth = 0;
        if (!data || size <= 0 || size > 12 * 1024 * 1024 || depth >= 8) return;
        const juce::ScopedValueSetter<int> recursion(depth, depth + 1); const auto xml = getXmlFromBinary(data, size);
        if (!xml || !xml->hasTagName("OpentoneRack") || xml->getIntAttribute("version") != 1 || xml->getNumChildElements() > 128) return;
        std::vector<Branch> next;
        for (const auto* item : xml->getChildIterator())
        {
            Branch branch; branch.name = item->getStringAttribute("name").substring(0, 256); branch.receive = item->getIntAttribute("receive", -1); branch.send = item->getIntAttribute("send", 60);
            branch.gain = static_cast<float>(item->getDoubleAttribute("gain", 1)); branch.pan = static_cast<float>(item->getDoubleAttribute("pan")); branch.mute = item->getBoolAttribute("mute");
            if (!item->hasTagName("branch") || branch.receive < -1 || branch.receive > 127 || branch.send < 0 || branch.send > 127 || !std::isfinite(branch.gain) || branch.gain < 0 || branch.gain > 2 || !std::isfinite(branch.pan) || std::abs(branch.pan) > 1 || item->getNumChildElements() > 128) return;
            for (const auto* child : item->getChildIterator())
            {
                Slot slot; slot.identifier = child->getStringAttribute("id"); slot.bypass = child->getBoolAttribute("bypass"); slot.processor = createBuiltinPlugin(slot.identifier);
                juce::MemoryBlock state;
                if (!child->hasTagName("device") || !slot.processor || !state.fromBase64Encoding(child->getStringAttribute("state"))) return;
                slot.processor->setStateInformation(state.getData(), static_cast<int>(state.getSize())); branch.slots.push_back(std::move(slot));
            }
            next.push_back(std::move(branch));
        }
        releaseResources(); branches = std::move(next); if (sampleRate > 0) prepareToPlay(sampleRate, blockSize);
    }
    std::vector<Branch> branches; juce::AudioBuffer<float> scratch; juce::MidiBuffer notes; double sampleRate = 0; int blockSize = 256;
};
class RackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RackEditor(DrumRack& rack) : AudioProcessorEditor(rack), tabs(juce::TabbedButtonBar::TabsAtTop)
    {
        addAndMakeVisible(tabs); int width = 700, height = 178;
        for (auto& branch : rack.branches) for (auto& slot : branch.slots)
        {
            auto* editor = slot.processor->createEditorIfNeeded(); if (!editor) continue;
            editors.push_back({slot.processor.get(), editor}); width = std::max(width, editor->getWidth()); height = std::max(height, editor->getHeight());
            tabs.addTab(branch.name + " / " + slot.processor->getName(), juce::Colours::transparentBlack, editor, false);
        }
        setSize(width, height + 30);
    }
    ~RackEditor() override
    { tabs.clearTabs(); for (auto& item : editors) { item.first->editorBeingDeleted(item.second); delete item.second; } }
    void resized() override { tabs.setBounds(getLocalBounds()); }
    juce::TabbedComponent tabs; std::vector<std::pair<juce::AudioProcessor*, juce::AudioProcessorEditor*>> editors;
};
