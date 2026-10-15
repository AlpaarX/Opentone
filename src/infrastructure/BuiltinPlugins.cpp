// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/BuiltinPlugins.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
namespace opentone
{
namespace
{
struct Device : juce::AudioProcessor
{
    Device(juce::String title, juce::String identifier, bool instrument = false)
        : AudioProcessor(instrument ? BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
            : BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)), name(std::move(title)), id(std::move(identifier)), synth(instrument) {}
    juce::AudioParameterFloat* number(const juce::String& key, const juce::String& title, float low, float high, float initial, float midpoint = 0)
    { juce::NormalisableRange<float> range(low, high, 0.001f); if (midpoint > low && midpoint < high) range.setSkewForCentre(midpoint);
      auto* p = new juce::AudioParameterFloat(juce::ParameterID(key, 1), title, range, initial,
          juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int) { return juce::String(value, 1); }));
      addParameter(p); parameters[key] = p; order.push_back(key); return p; }
    void choice(const juce::String& key, const juce::String& title, juce::StringArray options, int initial = 0)
    { auto* p = new juce::AudioParameterChoice(juce::ParameterID(key, 1), title, options, initial); addParameter(p); parameters[key] = p; order.push_back(key); }
    void toggle(const juce::String& key, const juce::String& title, bool initial)
    { auto* p = new juce::AudioParameterBool(juce::ParameterID(key, 1), title, initial); addParameter(p); parameters[key] = p; order.push_back(key); }
    static float read(juce::RangedAudioParameter* p)
    { if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p)) return f->get(); if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p)) return b->get() ? 1.0f : 0.0f; if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p)) return static_cast<float>(c->getIndex()); return p->convertFrom0to1(p->getValue()); }
    float value(const juce::String& key) const { return read(parameters.at(key)); }
    float value(const char* key) const { for (const auto& p : parameters) if (p.first == key) return read(p.second); jassertfalse; return 0; }
    void change(const juce::String& key, float valueIn) { auto* p = parameters.at(key); p->setValueNotifyingHost(p->convertTo0to1(valueIn)); }
    const juce::String getName() const override { return name; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    { return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && (synth ? layout.getMainInputChannelSet().isDisabled() : layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()); }
    bool acceptsMidi() const override { return synth; } bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; } void changeProgramName(int, const juce::String&) override {}
    void releaseResources() override {}
    void getStateInformation(juce::MemoryBlock& bytes) override
    { juce::XmlElement xml("OpentoneDevice"); xml.setAttribute("id", id); xml.setAttribute("version", 1);
      for (const auto& p : parameters) { auto* item = xml.createNewChildElement("parameter"); item->setAttribute("id", p.first); item->setAttribute("value", static_cast<double>(p.second->getValue())); } copyXmlToBinary(xml, bytes); }
    void setStateInformation(const void* bytes, int size) override
    {
        if (!bytes || size <= 0 || size > 1024 * 1024) return; const auto xml = getXmlFromBinary(bytes, size);
        if (!xml || !xml->hasTagName("OpentoneDevice") || xml->getStringAttribute("id") != id || xml->getIntAttribute("version") != 1) return;
        for (auto* item : xml->getChildIterator()) { const auto text = item->getStringAttribute("value").trim(); const auto* begin = text.toRawUTF8(); char* end = nullptr; const auto v = std::strtod(begin, &end); if (end == begin || *end != 0 || !item->hasTagName("parameter") || !std::isfinite(v) || v < 0 || v > 1 || !parameters.count(item->getStringAttribute("id"))) return; }
        for (auto* item : xml->getChildIterator()) parameters.at(item->getStringAttribute("id"))->setValueNotifyingHost(static_cast<float>(item->getDoubleAttribute("value")));
    }
    juce::String name, id; bool synth; std::map<juce::String, juce::RangedAudioParameter*> parameters; std::vector<juce::String> order;
    std::atomic<float> reduction {0};
};
struct Biquad
{
    using Coefficients = std::array<double, 6>;
    Coefficients coefficients {1, 0, 0, 1, 0, 0}; std::array<double, 2> z1 {}, z2 {};
    void reset() { z1.fill(0); z2.fill(0); }
    void set(Coefficients next) { const auto a0 = next[3]; for (auto& v : next) v /= a0; coefficients = next; }
    float sample(int channel, float input)
    { const auto c = static_cast<size_t>(channel); const auto output = coefficients[0] * input + z1[c]; z1[c] = coefficients[1] * input - coefficients[4] * output + z2[c]; z2[c] = coefficients[2] * input - coefficients[5] * output; return static_cast<float>(output); }
};
struct Reverb final : Device
{
    Reverb() : Device("Reverb", "opentone.reverb")
    { number("size", "Size", 0, 1, 0.65f); number("decay", "Damping", 0, 1, 0.45f); number("width", "Width", 0, 1, 1); number("predelay", "Pre-delay (ms)", 0, 200, 15); number("mix", "Dry/Wet", 0, 1, 0.25f); toggle("freeze", "Freeze", false); }
    void prepareToPlay(double rate, int block) override
    { sampleRate = rate; previousMix = value("mix"); reverb.setSampleRate(rate); reverb.reset(); dry.setSize(2, block); pre.setSize(2, static_cast<int>(rate * 0.21) + 2); pre.clear(); cursor = 0; predelay.reset(rate, 0.02); predelay.setCurrentAndTargetValue(value("predelay") * static_cast<float>(rate / 1000)); }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard; const int count = audio.getNumSamples();
        for (int c = 0; c < 2; ++c) dry.copyFrom(c, 0, audio, c, 0, count);
        predelay.setTargetValue(value("predelay") * static_cast<float>(sampleRate / 1000));
        for (int i = 0; i < count; ++i)
        { const auto delay = predelay.getNextValue(); const auto whole = static_cast<int>(delay); const auto fraction = delay - static_cast<float>(whole);
          for (int c = 0; c < 2; ++c) { pre.setSample(c, cursor, audio.getSample(c, i)); const auto a = (cursor - whole + pre.getNumSamples()) % pre.getNumSamples(), b = (a + pre.getNumSamples() - 1) % pre.getNumSamples(); audio.setSample(c, i, pre.getSample(c, a) * (1 - fraction) + pre.getSample(c, b) * fraction); }
          cursor = (cursor + 1) % pre.getNumSamples(); }
        juce::Reverb::Parameters p; p.roomSize = value("size"); p.damping = value("decay"); p.width = value("width"); p.wetLevel = 1; p.dryLevel = 0; p.freezeMode = value("freeze"); reverb.setParameters(p);
        reverb.processStereo(audio.getWritePointer(0), audio.getWritePointer(1), count);
        const auto mix = value("mix"); for (int c = 0; c < 2; ++c) { audio.applyGainRamp(c, 0, count, previousMix, mix); dry.applyGainRamp(c, 0, count, 1 - previousMix, 1 - mix); audio.addFrom(c, 0, dry, c, 0, count); } previousMix = mix;
    }
    float previousMix = 0.25f;
    juce::Reverb reverb; juce::AudioBuffer<float> pre, dry; int cursor = 0; double sampleRate = 48000; juce::SmoothedValue<float> predelay;
};
struct Delay final : Device
{
    Delay() : Device("Delay", "opentone.delay")
    { number("time", "Time (ms)", 1, 2000, 375, 300); toggle("sync", "Tempo sync", true); choice("division", "Division", {"1/16", "1/8", "1/4", "1/2", "1 bar", "1/8 dotted", "1/8 triplet"}, 1);
      number("stereo", "Stereo offset", 0, 0.5f, 0); number("feedback", "Feedback", 0, 0.95f, 0.35f); number("mix", "Dry/Wet", 0, 1, 0.25f); number("low", "Low cut (Hz)", 20, 2000, 80, 200); number("high", "High cut (Hz)", 1000, 20000, 10000, 5000); toggle("ping", "Ping-pong", false); }
    void prepareToPlay(double rate, int) override
    { sampleRate = rate; mix.setCurrentAndTargetValue(value("mix")); mix.reset(rate, 0.02); ring.setSize(2, static_cast<int>(rate * 8) + 4); ring.clear(); cursor = 0; for (auto& t : times) { t.reset(rate, 0.03); t.setCurrentAndTargetValue(static_cast<float>(rate * 0.25)); } highpass.reset(); lowpass.reset(); }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard; double bpm = 120; if (auto* head = getPlayHead()) if (const auto position = head->getPosition()) if (position->getBpm()) bpm = *position->getBpm();
        static const double beats[] {0.25, 0.5, 1, 2, 4, 0.75, 1.0 / 3};
        const auto seconds = value("sync") > 0.5f ? beats[juce::jlimit(0, 6, static_cast<int>(value("division")))] * 60 / std::max(1.0, bpm) : value("time") / 1000.0;
        for (int c = 0; c < 2; ++c) times[static_cast<size_t>(c)].setTargetValue(static_cast<float>(juce::jlimit(1.0, static_cast<double>(ring.getNumSamples() - 2), seconds * sampleRate * (c == 1 ? 1 + value("stereo") : 1))));
        highpass.set(juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(sampleRate, std::min(static_cast<double>(value("low")), sampleRate * 0.45)));
        lowpass.set(juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate, std::min(static_cast<double>(value("high")), sampleRate * 0.45)));
        mix.setTargetValue(value("mix")); const auto feedback = value("feedback"); const bool ping = value("ping") > 0.5f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const auto wetMix = mix.getNextValue(); std::array<float, 2> dry {}, wet {};
            for (int c = 0; c < 2; ++c) { dry[static_cast<size_t>(c)] = audio.getSample(c, i); const auto time = times[static_cast<size_t>(c)].getNextValue(); const auto whole = static_cast<int>(time); const auto f = time - static_cast<float>(whole); const auto a = (cursor - whole + ring.getNumSamples()) % ring.getNumSamples(), b = (a + ring.getNumSamples() - 1) % ring.getNumSamples(); wet[static_cast<size_t>(c)] = lowpass.sample(c, highpass.sample(c, ring.getSample(c, a) * (1 - f) + ring.getSample(c, b) * f)); }
            for (int c = 0; c < 2; ++c) { const auto k = static_cast<size_t>(c), other = static_cast<size_t>(ping ? 1 - c : c); auto input = dry[k] + wet[other] * feedback; if (!std::isfinite(input)) input = 0; ring.setSample(c, cursor, juce::jlimit(-8.0f, 8.0f, input)); audio.setSample(c, i, dry[k] * (1 - wetMix) + wet[k] * wetMix); }
            cursor = (cursor + 1) % ring.getNumSamples();
        }
    }
    juce::AudioBuffer<float> ring; juce::SmoothedValue<float> mix; std::array<juce::SmoothedValue<float>, 2> times; Biquad highpass, lowpass; double sampleRate = 48000; int cursor = 0;
};
struct Dynamics final : Device
{
    explicit Dynamics(bool limiter) : Device(limiter ? "Limiter" : "Compressor", limiter ? "opentone.limiter" : "opentone.compressor"), limiting(limiter)
    { if (limiting) { number("input", "Input (dB)", -12, 24, 0); number("ceiling", "Ceiling (dB)", -18, 0, -1); number("release", "Release (ms)", 5, 2000, 100, 200); }
      else { number("threshold", "Threshold (dB)", -60, 0, -18); number("ratio", "Ratio", 1, 20, 4, 4); number("attack", "Attack (ms)", 0.1f, 100, 10, 10); number("release", "Release (ms)", 5, 2000, 100, 200); number("knee", "Knee (dB)", 0, 24, 6); number("makeup", "Makeup (dB)", -12, 24, 0); number("mix", "Dry/Wet", 0, 1, 1); } }
    void prepareToPlay(double rate, int) override { sampleRate = rate; envelope = 0; gain = 1; reduction.store(0); }
    double getTailLengthSeconds() const override { return 0; }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard; const auto release = static_cast<float>(std::exp(-1 / (sampleRate * value("release") / 1000)));
        const auto attack = limiting ? 0.0f : static_cast<float>(std::exp(-1 / (sampleRate * value("attack") / 1000)));
        const auto input = limiting ? juce::Decibels::decibelsToGain(value("input")) : 1.0f, ceiling = limiting ? juce::Decibels::decibelsToGain(value("ceiling")) : 1.0f;
        const auto threshold = limiting ? 0 : value("threshold"), ratio = limiting ? 1 : value("ratio"), knee = limiting ? 0 : value("knee");
        const auto makeup = limiting ? 1.0f : juce::Decibels::decibelsToGain(value("makeup")), mix = limiting ? 1.0f : value("mix");
        for (int i = 0; i < audio.getNumSamples(); ++i)
        { const auto l = audio.getSample(0, i) * input, r = audio.getSample(1, i) * input, peak = std::max(std::abs(l), std::abs(r));
          if (limiting) { const auto target = peak > ceiling ? ceiling / peak : 1.0f; gain = target < gain ? target : release * gain + (1 - release) * target; }
          else { const auto coefficient = peak > envelope ? attack : release; envelope = coefficient * envelope + (1 - coefficient) * peak;
            const auto over = juce::Decibels::gainToDecibels(envelope, -120.0f) - threshold; float db = 0;
            if (knee > 0 && over > -knee / 2 && over < knee / 2) db = (1 / ratio - 1) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
            else if (over >= knee / 2) db = (1 / ratio - 1) * over;
            gain = juce::Decibels::decibelsToGain(db); }
          for (int c = 0; c < 2; ++c) { const auto dry = audio.getSample(c, i), wet = dry * input * gain * makeup; audio.setSample(c, i, limiting ? juce::jlimit(-ceiling, ceiling, wet) : dry * (1 - mix) + wet * mix); }
        }
        reduction.store(-juce::Decibels::gainToDecibels(gain, -120.0f));
    }
    bool limiting; double sampleRate = 48000; float envelope = 0, gain = 1;
};
struct Equalizer final : Device
{
    Equalizer() : Device("Parametric EQ", "opentone.eq8")
    { static const float frequencies[] {30, 90, 250, 700, 2000, 5000, 10000, 18000};
      for (int band = 0; band < 8; ++band) { const auto p = prefix(band); toggle(p + "on", "Band " + juce::String(band + 1), band > 0 && band < 7); choice(p + "type", "Type", {"Bell", "Low shelf", "High shelf", "High pass", "Low pass", "Notch"}, band == 0 ? 3 : band == 7 ? 4 : 0); number(p + "freq", "Frequency (Hz)", 20, 20000, frequencies[band], 1000); number(p + "gain", "Gain (dB)", -24, 24, 0); number(p + "q", "Q", 0.1f, 20, 0.707f, 1);
        int i = 0; for (const auto& key : {"on", "type", "freq", "gain", "q"}) inputs[static_cast<size_t>(band)][static_cast<size_t>(i++)] = parameters.at(p + key); }
      number("output", "Output (dB)", -24, 12, 0); }
    static juce::String prefix(int band) { return "b" + juce::String(band + 1) + "_"; }
    Biquad::Coefficients coefficients(int band, double rate) const
    { const auto& p = inputs[static_cast<size_t>(band)]; const auto f = static_cast<double>(read(p[2])), g = static_cast<double>(juce::Decibels::decibelsToGain(read(p[3]))), q = static_cast<double>(read(p[4])); const auto hz = std::min(f, rate * 0.45);
      using C = juce::dsp::IIR::ArrayCoefficients<double>;
      switch (static_cast<int>(read(p[1]))) { case 1: return C::makeLowShelf(rate, hz, q, g); case 2: return C::makeHighShelf(rate, hz, q, g); case 3: return C::makeHighPass(rate, hz, q); case 4: return C::makeLowPass(rate, hz, q); case 5: return C::makeNotch(rate, hz, q); default: return C::makePeakFilter(rate, hz, q, g); } }
    void prepareToPlay(double rate, int) override { sampleRate = rate; previousGain = juce::Decibels::decibelsToGain(value("output")); for (auto& filter : filters) filter.reset(); }
    double getTailLengthSeconds() const override { return 0; }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    { juce::ScopedNoDenormals guard; for (int band = 0; band < 8; ++band) { if (read(inputs[static_cast<size_t>(band)][0]) < 0.5f) { filters[static_cast<size_t>(band)].reset(); continue; }
        auto& filter = filters[static_cast<size_t>(band)]; filter.set(coefficients(band, sampleRate)); for (int c = 0; c < 2; ++c) for (int i = 0; i < audio.getNumSamples(); ++i) audio.setSample(c, i, filter.sample(c, audio.getSample(c, i))); }
      const auto gain = juce::Decibels::decibelsToGain(value("output")); audio.applyGainRamp(0, audio.getNumSamples(), previousGain, gain); previousGain = gain; }
    std::array<std::array<juce::RangedAudioParameter*, 5>, 8> inputs {};
    std::array<Biquad, 8> filters; double sampleRate = 48000; float previousGain = 1;
};
struct FM final : Device
{
    struct Settings { float ratio = 1, level = 0, tune = 0; int wave = 0; juce::ADSR::Parameters envelope; };
    struct Voice { int note = -1, channel = 0; bool held = false; uint64_t age = 0; float velocity = 0; double frequency = 440, feedback = 0; std::array<double, 4> phase {}; std::array<juce::ADSR, 4> envelopes; };
    FM() : Device("FM Synth", "opentone.fm", true)
    {
        choice("algorithm", "Algorithm", {"D → C → B → A", "C+D → B → A", "D → B+C → A", "B+C+D → A", "B → A / D → C", "D → A+B+C", "C → A / D → B", "A+B+C+D"});
        number("feedback", "Feedback", 0, 1, 0); number("filter", "Cutoff (Hz)", 20, 20000, 18000, 2000); number("resonance", "Resonance", 0.1f, 1, 0.15f); number("volume", "Volume (dB)", -48, 6, -18);
        for (int op = 0; op < 4; ++op)
        { const auto p = prefix(op); number(p + "ratio", "Ratio", 0.25f, 32, 1, 2); number(p + "level", "Level", 0, 1, op == 0 ? 0.9f : op == 1 ? 0.5f : 0); number(p + "tune", "Detune (cents)", -100, 100, 0);
          choice(p + "wave", "Wave", {"Sine", "Triangle", "Saw", "Square"}); number(p + "attack", "Attack (ms)", 1, 10000, 5, 100); number(p + "decay", "Decay (ms)", 1, 10000, 400, 400); number(p + "sustain", "Sustain", 0, 1, op == 0 ? 0.8f : 0.3f); number(p + "release", "Release (ms)", 1, 20000, 200, 400);
          int i = 0; for (const auto& key : {"ratio", "level", "tune", "wave", "attack", "decay", "sustain", "release"}) inputs[static_cast<size_t>(op)][static_cast<size_t>(i++)] = parameters.at(p + key); }
    }
    static juce::String prefix(int op) { return juce::String::charToString(static_cast<juce::juce_wchar>('a' + op)) + "_"; }
    void prepareToPlay(double rate, int block) override
    {
        sampleRate = rate; oversampling = std::make_unique<juce::dsp::Oversampling<float>>(2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true); oversampling->initProcessing(static_cast<size_t>(block)); oversampling->reset();
        setLatencySamples(static_cast<int>(std::ceil(oversampling->getLatencyInSamples())));
        for (auto& voice : voices) { voice.note = -1; for (auto& env : voice.envelopes) { env.setSampleRate(rate * 2); env.reset(); } }
        bend.fill(1); bendRange.fill(2); rpnMsb.fill(127); rpnLsb.fill(127); sustain.fill(false); filter.reset(); volume.reset(rate * 2, 0.02); volume.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(value("volume")));
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals guard;
        for (int op = 0; op < 4; ++op) { const auto& p = inputs[static_cast<size_t>(op)]; auto& s = settings[static_cast<size_t>(op)]; s.ratio = read(p[0]); s.level = read(p[1]); s.tune = read(p[2]); s.ratio *= std::pow(2.0f, s.tune / 1200); s.wave = static_cast<int>(read(p[3]));
            s.envelope = {read(p[4]) / 1000, read(p[5]) / 1000, read(p[6]), read(p[7]) / 1000}; for (auto& voice : voices) voice.envelopes[static_cast<size_t>(op)].setParameters(s.envelope); }
        algorithm = static_cast<int>(value("algorithm")); feedback = value("feedback"); volume.setTargetValue(juce::Decibels::decibelsToGain(value("volume")));
        filter.set(juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate * 2, std::min(static_cast<double>(value("filter")), sampleRate * 0.45), 0.5 + value("resonance") * 4));
        audio.clear(); juce::dsp::AudioBlock<float> base(audio); auto up = oversampling->processSamplesUp(base); int cursor = 0;
        for (const auto metadata : midi) { const auto at = juce::jlimit(cursor, static_cast<int>(up.getNumSamples()), metadata.samplePosition * 2); render(up, cursor, at); message(metadata.getMessage()); cursor = at; }
        render(up, cursor, static_cast<int>(up.getNumSamples())); oversampling->processSamplesDown(base);
    }
    void release(Voice& voice, bool immediate)
    { voice.held = false; for (auto& env : voice.envelopes) if (immediate) env.reset(); else env.noteOff(); if (immediate) voice.note = -1; }
    void message(const juce::MidiMessage& midi)
    {
        const int channel = midi.getChannel() - 1; if (channel < 0 || channel >= 16) return;
        if (midi.isPitchWheel()) { bend[static_cast<size_t>(channel)] = std::pow(2.0, (midi.getPitchWheelValue() - 8192) / 8192.0 * bendRange[static_cast<size_t>(channel)] / 12); return; }
        if (midi.isController())
        { const auto i = static_cast<size_t>(channel), controller = static_cast<size_t>(midi.getControllerNumber());
          if (controller == 101) rpnMsb[i] = midi.getControllerValue(); if (controller == 100) rpnLsb[i] = midi.getControllerValue();
          if (controller == 6 && rpnMsb[i] == 0 && rpnLsb[i] == 0) bendRange[i] = juce::jlimit(1, 127, midi.getControllerValue()); }
        if (midi.isController() && midi.getControllerNumber() == 64) { sustain[static_cast<size_t>(channel)] = midi.getControllerValue() >= 64; if (!sustain[static_cast<size_t>(channel)]) for (auto& v : voices) if (v.channel == channel && !v.held) release(v, false); return; }
        if (midi.isAllNotesOff() || midi.isAllSoundOff()) { for (auto& v : voices) if (v.channel == channel) release(v, midi.isAllSoundOff()); return; }
        if (midi.isNoteOn())
        { auto* selected = &voices[0]; for (auto& v : voices) { if (v.note < 0) { selected = &v; break; } if (v.age < selected->age) selected = &v; }
          selected->note = midi.getNoteNumber(); selected->channel = channel; selected->held = true; selected->velocity = midi.getFloatVelocity(); selected->frequency = juce::MidiMessage::getMidiNoteInHertz(selected->note); selected->age = ++age; selected->feedback = 0; selected->phase.fill(0);
          for (auto& env : selected->envelopes) { env.reset(); env.noteOn(); } }
        else if (midi.isNoteOff()) for (auto& v : voices) if (v.note == midi.getNoteNumber() && v.channel == channel) { v.held = false; if (!sustain[static_cast<size_t>(channel)]) release(v, false); }
    }
    double oscillator(Voice& v, int op, double modulation)
    {
        const auto i = static_cast<size_t>(op); const auto& s = settings[i]; const auto step = std::min(0.45, v.frequency * bend[static_cast<size_t>(v.channel)] * s.ratio / (sampleRate * 2));
        v.phase[i] += step; v.phase[i] -= std::floor(v.phase[i]); const auto level = v.envelopes[i].getNextSample() * s.level; if (level <= 0) return 0;
        const auto phase = v.phase[i] + modulation * 2; const auto wrapped = phase - std::floor(phase); double wave = 0;
        switch (s.wave) { case 1: wave = 1 - 4 * std::abs(wrapped - 0.5); break; case 2: wave = wrapped * 2 - 1; break; case 3: wave = wrapped < 0.5 ? 1 : -1; break; default: wave = std::sin(phase * juce::MathConstants<double>::twoPi); }
        return wave * level;
    }
    void render(juce::dsp::AudioBlock<float>& audio, int begin, int end)
    {
        for (int sample = begin; sample < end; ++sample)
        {
            double output = 0;
            for (auto& v : voices) if (v.note >= 0)
            {
                const auto d = oscillator(v, 3, v.feedback * feedback); v.feedback = d; double a = 0, b = 0, c = 0;
                switch (algorithm) { case 1: c = oscillator(v, 2, 0); b = oscillator(v, 1, c + d); a = oscillator(v, 0, b); break;
                    case 2: c = oscillator(v, 2, d); b = oscillator(v, 1, d); a = oscillator(v, 0, b + c); break;
                    case 3: c = oscillator(v, 2, 0); b = oscillator(v, 1, 0); a = oscillator(v, 0, b + c + d); break;
                    case 4: c = oscillator(v, 2, d); b = oscillator(v, 1, 0); a = (oscillator(v, 0, b) + c) * 0.5; break;
                    case 5: c = oscillator(v, 2, d); b = oscillator(v, 1, d); a = (oscillator(v, 0, d) + b + c) / 3; break;
                    case 6: c = oscillator(v, 2, 0); b = oscillator(v, 1, d); a = (oscillator(v, 0, c) + b) * 0.5; break;
                    case 7: c = oscillator(v, 2, 0); b = oscillator(v, 1, 0); a = (oscillator(v, 0, 0) + b + c + d) * 0.25; break;
                    default: c = oscillator(v, 2, d); b = oscillator(v, 1, c); a = oscillator(v, 0, b); }
                output += a * v.velocity; bool alive = false; for (const auto& env : v.envelopes) alive = alive || env.isActive(); if (!alive) v.note = -1;
            }
            const auto value = filter.sample(0, static_cast<float>(output) * volume.getNextValue()); audio.getChannelPointer(0)[sample] = value; audio.getChannelPointer(1)[sample] = value;
        }
    }
    std::array<std::array<juce::RangedAudioParameter*, 8>, 4> inputs {};
    std::array<Voice, 16> voices; std::array<Settings, 4> settings; std::array<int, 16> bendRange {}, rpnMsb {}, rpnLsb {}; std::array<double, 16> bend {}; std::array<bool, 16> sustain {}; uint64_t age = 0;
    double sampleRate = 48000; int algorithm = 0; float feedback = 0; Biquad filter; juce::SmoothedValue<float> volume;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
};
struct Control final : juce::Component
{
    void bind(Device& owner, const juce::String& key)
    {
        sliderAttachment.reset(); choiceAttachment.reset(); buttonAttachment.reset(); auto* p = owner.parameters.at(key);
        addAndMakeVisible(label); label.setText(p->getName(40), juce::dontSendNotification); label.setFont(juce::FontOptions(10)); label.setJustificationType(juce::Justification::centred);
        slider.setVisible(false); combo.setVisible(false); button.setVisible(false);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) { addAndMakeVisible(combo); combo.clear(); combo.addItemList(choice->choices, 1); choiceAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*p, combo, nullptr); }
        else if (dynamic_cast<juce::AudioParameterBool*>(p)) { addAndMakeVisible(button); button.setButtonText("On"); button.setClickingTogglesState(true); buttonAttachment = std::make_unique<juce::ButtonParameterAttachment>(*p, button, nullptr); }
        else { addAndMakeVisible(slider); slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
          sliderAttachment = std::make_unique<juce::SliderParameterAttachment>(*p, slider, nullptr); slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue())); slider.setNumDecimalPlacesToDisplay(1); }
        for (auto* control : std::initializer_list<juce::Component*> {&slider, &combo, &button}) control->getProperties().set("plugin-parameter", key);
        setTitle(p->getName(40)); resized();
    }
    void resized() override { auto area = getLocalBounds().reduced(2); label.setBounds(area.removeFromTop(15)); slider.setBounds(area); combo.setBounds(area.withHeight(22).withY(area.getCentreY() - 11)); button.setBounds(area.withSizeKeepingCentre(38, 22)); }
    juce::Label label; juce::Slider slider; juce::ComboBox combo; juce::TextButton button;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment; std::unique_ptr<juce::ComboBoxParameterAttachment> choiceAttachment; std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;
};
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Device& device) : AudioProcessorEditor(device), owner(device)
    {
        eq = dynamic_cast<Equalizer*>(&owner); fm = dynamic_cast<FM*>(&owner);
        if (eq) { for (int i = 0; i < 8; ++i) addSelector(juce::String(i + 1), i); for (const auto& key : {"on", "type", "freq", "gain", "q"}) addControl(Equalizer::prefix(0) + key); addControl("output"); setSize(600, 178); }
        else if (fm)
        { addAndMakeVisible(algorithm); algorithm.addItemList(dynamic_cast<juce::AudioParameterChoice*>(owner.parameters.at("algorithm"))->choices, 1); algorithmAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*owner.parameters.at("algorithm"), algorithm, nullptr);
          for (int i = 0; i < 4; ++i) addSelector(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)), i);
          for (const auto& key : {"volume", "filter", "resonance", "feedback"}) addControl(key);
          for (const auto& key : {"ratio", "level", "tune", "wave", "attack", "decay", "sustain", "release"}) addControl(FM::prefix(0) + key); setSize(700, 178); }
        else { for (const auto& key : owner.order) addControl(key); columns = controls.size() <= 6 ? 3 : controls.size() <= 8 ? 4 : 5; setSize(columns * 94 + 12, 170); }
        select(0); addMouseListener(this, true); observed.resize(owner.parameters.size(), -1); startTimerHz(30);
    }
    ~Editor() override { endDrag(); stopTimer(); removeMouseListener(this); }
    void addControl(const juce::String& key) { auto control = std::make_unique<Control>(); control->bind(owner, key); addAndMakeVisible(*control); controls.push_back(std::move(control)); }
    void addSelector(const juce::String& text, int index)
    { auto button = std::make_unique<juce::TextButton>(text); button->onClick = [this, index] { select(index); }; addAndMakeVisible(*button); selectors.push_back(std::move(button)); }
    void select(int index)
    {
        endDrag(); selected = index; for (size_t i = 0; i < selectors.size(); ++i) selectors[i]->setToggleState(static_cast<int>(i) == index, juce::dontSendNotification);
        if (eq) { int i = 0; for (const auto& key : {"on", "type", "freq", "gain", "q"}) controls[static_cast<size_t>(i++)]->bind(owner, Equalizer::prefix(index) + key); }
        if (fm) { int i = 4; for (const auto& key : {"ratio", "level", "tune", "wave", "attack", "decay", "sustain", "release"}) controls[static_cast<size_t>(i++)]->bind(owner, FM::prefix(index) + key); }
        repaint();
    }
    void resized() override
    {
        if (eq) { for (size_t i = 0; i < selectors.size(); ++i) selectors[i]->setBounds(8 + static_cast<int>(i) * 32, 2, 28, 20); for (size_t i = 0; i < controls.size(); ++i) controls[i]->setBounds(8 + static_cast<int>(i) * (getWidth() - 16) / 6, getHeight() - 72, (getWidth() - 16) / 6, 70); }
        else if (fm) { algorithm.setBounds(8, 3, 264, 24); for (size_t i = 0; i < selectors.size(); ++i) selectors[i]->setBounds(8 + static_cast<int>(i) * 44, 34, 40, 23);
          for (int i = 0; i < 4; ++i) controls[static_cast<size_t>(i)]->setBounds(getWidth() - 320 + i * 78, 0, 78, 75);
          for (int i = 0; i < 8; ++i) controls[static_cast<size_t>(i + 4)]->setBounds(8 + i * (getWidth() - 16) / 8, getHeight() - 78, (getWidth() - 16) / 8, 76); }
        else { const int rows = static_cast<int>((controls.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns)); const int height = (getHeight() - 22) / std::max(1, rows);
          for (size_t i = 0; i < controls.size(); ++i) controls[i]->setBounds(6 + static_cast<int>(i % static_cast<size_t>(columns)) * (getWidth() - 12) / columns, static_cast<int>(i / static_cast<size_t>(columns)) * height, (getWidth() - 12) / columns, height); }
    }
    juce::Rectangle<float> graph() const { return getLocalBounds().toFloat().reduced(8).withTrimmedTop(20).withTrimmedBottom(72); }
    float frequencyX(float frequency) const { const auto area = graph(); return area.getX() + std::log(frequency / 20) / std::log(1000.0f) * area.getWidth(); }
    float gainY(float gain) const { const auto area = graph(); return area.getCentreY() - gain / 48 * area.getHeight(); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); const auto ink = findColour(juce::Label::textColourId), primary = findColour(juce::TextButton::buttonOnColourId);
        if (eq)
        {
            const auto area = graph(); g.setColour(ink.withAlpha(0.06f)); g.fillRect(area); g.setColour(ink.withAlpha(0.2f)); g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
            for (const auto f : {100.0f, 1000.0f, 10000.0f}) g.drawVerticalLine(juce::roundToInt(frequencyX(f)), area.getY(), area.getBottom());
            const auto rate = owner.getSampleRate() > 0 ? owner.getSampleRate() : 48000; std::array<Biquad::Coefficients, 8> coefficients; std::array<bool, 8> active;
            for (int b = 0; b < 8; ++b) { coefficients[static_cast<size_t>(b)] = eq->coefficients(b, rate); active[static_cast<size_t>(b)] = owner.value(Equalizer::prefix(b) + "on") > 0.5f; }
            juce::Path curve;
            for (int x = 0; x < static_cast<int>(area.getWidth()); ++x)
            { const auto frequency = 20 * std::pow(1000.0, x / static_cast<double>(area.getWidth())); const auto z = std::polar(1.0, -juce::MathConstants<double>::twoPi * frequency / rate); double magnitude = 1;
              for (int b = 0; b < 8; ++b) if (active[static_cast<size_t>(b)]) { const auto& c = coefficients[static_cast<size_t>(b)]; magnitude *= std::abs((c[0] + c[1] * z + c[2] * z * z) / (c[3] + c[4] * z + c[5] * z * z)); }
              const auto db = static_cast<float>(20 * std::log10(std::max(1e-12, magnitude))); const auto y = juce::jlimit(area.getY(), area.getBottom(), gainY(db)); if (x == 0) curve.startNewSubPath(area.getX(), y); else curve.lineTo(area.getX() + static_cast<float>(x), y); }
            g.setColour(primary); g.strokePath(curve, juce::PathStrokeType(1.5f));
            for (int b = 0; b < 8; ++b) { const auto p = Equalizer::prefix(b); g.setColour(b == selected ? primary : ink.withAlpha(active[static_cast<size_t>(b)] ? 0.8f : 0.25f)); g.fillEllipse(frequencyX(owner.value(p + "freq")) - 3, gainY(owner.value(p + "type") <= 2 ? owner.value(p + "gain") : 0) - 3, 6, 6); }
        }
        else if (fm)
        {
            const auto p = FM::prefix(selected); const float attack = owner.value(p + "attack"), decay = owner.value(p + "decay"), release = owner.value(p + "release"), sustain = owner.value(p + "sustain"); const auto total = attack + decay + release + 500;
            juce::Path envelope; envelope.startNewSubPath(188, 63); envelope.lineTo(188 + attack / total * 160, 33); envelope.lineTo(188 + (attack + decay) / total * 160, 63 - sustain * 30); envelope.lineTo(188 + (attack + decay + 500) / total * 160, 63 - sustain * 30); envelope.lineTo(348, 63); g.setColour(primary); g.strokePath(envelope, juce::PathStrokeType(1.5f));
        }
        else if (dynamic_cast<Dynamics*>(&owner)) { g.setColour(ink.withAlpha(0.2f)); g.fillRect(8, getHeight() - 15, getWidth() - 96, 5); g.setColour(primary); g.fillRect(8, getHeight() - 15, juce::roundToInt(juce::jlimit(0.0f, 1.0f, owner.reduction.load() / 24) * static_cast<float>(getWidth() - 96)), 5); g.setColour(ink); g.setFont(juce::FontOptions(11)); g.drawText("GR " + juce::String(owner.reduction.load(), 1) + " dB", getWidth() - 84, getHeight() - 23, 80, 20, juce::Justification::centredRight); }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!eq || dragging || !e.mods.isLeftButtonDown() || e.originalComponent != this || !graph().contains(e.position)) return;
        float nearest = 12; int band = selected; for (int b = 0; b < 8; ++b) { const auto p = Equalizer::prefix(b); const auto distance = e.position.getDistanceFrom({frequencyX(owner.value(p + "freq")), gainY(owner.value(p + "type") <= 2 ? owner.value(p + "gain") : 0)}); if (distance < nearest) { nearest = distance; band = b; } }
        select(band); dragging = true; const auto p = Equalizer::prefix(band); owner.parameters.at(p + "freq")->beginChangeGesture(); owner.parameters.at(p + "gain")->beginChangeGesture(); owner.change(p + "on", 1); mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    { if (!dragging) return; const auto area = graph(); const auto p = Equalizer::prefix(selected); owner.change(p + "freq", 20 * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth()))); if (owner.value(p + "type") <= 2) owner.change(p + "gain", juce::jlimit(-24.0f, 24.0f, (area.getCentreY() - e.position.y) / area.getHeight() * 48)); repaint(); }
    void mouseUp(const juce::MouseEvent&) override { endDrag(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    { if (!eq || !graph().contains(e.getEventRelativeTo(this).position)) { juce::AudioProcessorEditor::mouseWheelMove(e, wheel); return; } const auto p = Equalizer::prefix(selected) + "q"; owner.parameters.at(p)->beginChangeGesture(); owner.change(p, juce::jlimit(0.1f, 20.0f, owner.value(p) * std::exp(wheel.deltaY * 2))); owner.parameters.at(p)->endChangeGesture(); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    { auto* component = e.originalComponent; while (component && dynamic_cast<juce::Slider*>(component) == nullptr) component = component->getParentComponent();
      if (auto* slider = dynamic_cast<juce::Slider*>(component)) { if (auto* label = dynamic_cast<juce::Label*>(e.originalComponent)) label->hideEditor(true); slider->setValue(slider->getDoubleClickReturnValue(), juce::sendNotificationSync); } }
private:
    void endDrag() { if (dragging) { dragging = false; const auto p = Equalizer::prefix(selected); owner.parameters.at(p + "freq")->endChangeGesture(); owner.parameters.at(p + "gain")->endChangeGesture(); } }
    void timerCallback() override
    { if (eq) controls[3]->setEnabled(owner.value(Equalizer::prefix(selected) + "type") <= 2);
      bool changed = false; size_t index = 0; for (const auto& p : owner.parameters) { const auto v = p.second->getValue(); if (!std::equal_to<float>()(v, observed[index])) { observed[index] = v; changed = true; } ++index; }
      const auto gr = owner.reduction.load(); if (std::abs(gr - lastReduction) > 0.05f) { lastReduction = gr; changed = true; } if (changed) repaint(); }
    Device& owner; Equalizer* eq = nullptr; FM* fm = nullptr; int selected = 0, columns = 3; bool dragging = false; float lastReduction = 0; juce::ComboBox algorithm;
    std::unique_ptr<juce::ComboBoxParameterAttachment> algorithmAttachment; std::vector<std::unique_ptr<Control>> controls; std::vector<std::unique_ptr<juce::TextButton>> selectors; std::vector<float> observed;
};
juce::AudioProcessorEditor* Device::createEditor() { return new Editor(*this); }
}
juce::Array<juce::PluginDescription> builtinPluginDescriptions()
{
    juce::Array<juce::PluginDescription> result;
    for (const auto& item : {std::pair<const char*, const char*> {"FM Synth", "opentone.fm"}, {"Reverb", "opentone.reverb"}, {"Delay", "opentone.delay"}, {"Compressor", "opentone.compressor"}, {"Limiter", "opentone.limiter"}, {"Parametric EQ", "opentone.eq8"}})
    { juce::PluginDescription p; p.name = p.descriptiveName = item.first; p.fileOrIdentifier = item.second; p.pluginFormatName = p.manufacturerName = "Opentone"; p.version = "1"; p.isInstrument = p.fileOrIdentifier == "opentone.fm"; p.category = p.isInstrument ? "Instrument" : "Effect"; p.numInputChannels = p.isInstrument ? 0 : 2; p.numOutputChannels = 2; p.uniqueId = p.fileOrIdentifier.hashCode(); result.add(p); }
    return result;
}
std::unique_ptr<juce::AudioProcessor> createBuiltinPlugin(const juce::String& id)
{
    if (id == "opentone.fm") return std::make_unique<FM>(); if (id == "opentone.reverb") return std::make_unique<Reverb>(); if (id == "opentone.delay") return std::make_unique<Delay>();
    if (id == "opentone.compressor") return std::make_unique<Dynamics>(false); if (id == "opentone.limiter") return std::make_unique<Dynamics>(true); if (id == "opentone.eq8") return std::make_unique<Equalizer>(); return nullptr;
}
Plugin builtinPluginModel(const juce::String& id, const std::map<juce::String, float>& values)
{
    auto instance = createBuiltinPlugin(id); if (!instance) return {};
    for (auto* item : instance->getParameters()) if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(item))
        if (const auto value = values.find(parameter->paramID); value != values.end())
        { const auto& range = parameter->getNormalisableRange(); parameter->setValue(parameter->convertTo0to1(juce::jlimit(range.start, range.end, value->second))); }
    juce::MemoryBlock state; instance->getStateInformation(state);
    for (const auto& description : builtinPluginDescriptions()) if (description.fileOrIdentifier == id)
        return {newId(), description.name, description.createXml()->toString(), state.toBase64Encoding(), description.isInstrument, false};
    return {};
}

void addDefaultReturns(Project& project)
{
    for (int index = 0; index < 2; ++index)
    {
        if (std::any_of(project.tracks.begin(), project.tracks.end(), [index](const auto& track) { return track.returnIndex == index; })) continue;
        Track track {newId(), index == 0 ? "A Reverb" : "B Delay"}; track.returnIndex = index; track.inputEnabled = false;
        track.plugins.push_back(builtinPluginModel(index == 0 ? "opentone.reverb" : "opentone.delay", {{"mix", 1}}));
        project.tracks.push_back(std::move(track));
    }
    for (auto* track : allTracks(project)) track->sends.resize(static_cast<size_t>(std::max(2, returnCount(project))), 0);
}

}
