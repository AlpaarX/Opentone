// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/BuiltinPlugins.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
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
    struct Preset { juce::String name; std::map<juce::String, float> values; };
    std::vector<Preset> presets;
    int program = -1;
    int getNumPrograms() override { return std::max(1, static_cast<int>(presets.size())); }
    int getCurrentProgram() override { return std::max(0, program); }
    void setCurrentProgram(int index) override
    {
        if (index < 0 || index >= static_cast<int>(presets.size())) return;
        for (const auto& p : parameters) p.second->beginChangeGesture();
        for (const auto& p : parameters)
        { const auto found = presets[static_cast<size_t>(index)].values.find(p.first);
          p.second->setValueNotifyingHost(found == presets[static_cast<size_t>(index)].values.end() ? p.second->getDefaultValue() : p.second->convertTo0to1(found->second)); }
        program = index;
        for (const auto& p : parameters) p.second->endChangeGesture();
        updateHostDisplay();
    }
    const juce::String getProgramName(int index) override { return index >= 0 && index < static_cast<int>(presets.size()) ? presets[static_cast<size_t>(index)].name : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}
    void validatePresets() const
    {
       #if JUCE_DEBUG
        jassert(presets.size() == 20); juce::StringArray names;
        for (const auto& patch : presets)
        { jassert(!names.contains(patch.name)); names.add(patch.name);
          for (const auto& item : patch.values)
          { jassert(parameters.count(item.first) == 1); const auto& range = parameters.at(item.first)->getNormalisableRange();
            jassert(std::isfinite(item.second) && item.second >= range.start && item.second <= range.end); } }
       #endif
    }
    void releaseResources() override {}
    void getStateInformation(juce::MemoryBlock& bytes) override
    { juce::XmlElement xml("OpentoneDevice"); xml.setAttribute("id", id); xml.setAttribute("version", 1); xml.setAttribute("program", program);
      for (const auto& p : parameters) { auto* item = xml.createNewChildElement("parameter"); item->setAttribute("id", p.first); item->setAttribute("value", static_cast<double>(p.second->getValue())); } copyXmlToBinary(xml, bytes); }
    void setStateInformation(const void* bytes, int size) override
    {
        if (!bytes || size <= 0 || size > 1024 * 1024) return; const auto xml = getXmlFromBinary(bytes, size);
        if (!xml || !xml->hasTagName("OpentoneDevice") || xml->getStringAttribute("id") != id || xml->getIntAttribute("version") != 1) return;
        for (auto* item : xml->getChildIterator()) { const auto text = item->getStringAttribute("value").trim(); const auto* begin = text.toRawUTF8(); char* end = nullptr; const auto v = std::strtod(begin, &end); if (end == begin || *end != 0 || !item->hasTagName("parameter") || !std::isfinite(v) || v < 0 || v > 1 || !parameters.count(item->getStringAttribute("id"))) return; }
        program = juce::jlimit(-1, static_cast<int>(presets.size()) - 1, xml->getIntAttribute("program", -1));
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
struct Saturator final : Device
{
    struct Shape
    {
        int type = 0;
        float threshold = 1, drive = 1, linear = 0.5f, curve = 0.5f, damp = 0, depth = 0, period = 0;
    };
    Saturator() : Device("Saturator", "opentone.saturator")
    {
        number("drive", "Drive (dB)", -36, 36, 0); number("output", "Output (dB)", -36, 36, 0); number("mix", "Dry/Wet", 0, 1, 1);
        choice("type", "Curve", {"Analog Clip", "Soft Sine", "Bass Shaper", "Medium Curve", "Hard Curve", "Sinoid Fold", "Digital Clip", "Waveshaper"});
        number("threshold", "Bass threshold (dB)", -50, 0, -12);
        choice("post", "Post Clip", {"Off", "Soft Clip", "Hard Clip"});
        toggle("color", "Color", true); number("low", "Amt Lo (dB)", -36, 36, 0);
        number("high", "Amt Hi (dB)", -36, 36, 0); number("frequency", "Frequency (Hz)", 30, 18500, 1000, 1000); number("width", "Width (%)", 0, 100, 30);
        number("shape_drive", "Shaper Drive (%)", 0, 100, 100); number("linear", "Linear (%)", 0, 100, 50);
        number("curve", "Curve (%)", 0, 100, 50); number("damp", "Damp (%)", 0, 100, 0);
        number("depth", "Depth (%)", 0, 100, 0); number("period", "Period (%)", 0, 100, 0);
        toggle("dc", "Pre-DC Filter", true); toggle("quality", "Hi-Quality", true);
       #if JUCE_DEBUG
        for (int type = 0; type < 8; ++type)
        {
            Shape settings; settings.type = type;
            for (const float x : {0.0f, 0.1f, 0.5f, 1.0f, 16.0f, 1000.0f})
            { const auto y = shape(x, settings); jassert(std::isfinite(y)); jassert(std::abs(y + shape(-x, settings)) < 0.0001f); }
            jassert(shape(0, settings) == 0);
        }
        Shape settings; settings.type = 7; settings.drive = 0;
        jassert(shape(0.25f, settings) == 0.25f);
        settings.type = 6; jassert(shape(2, settings) == 1 && shape(-2, settings) == -1);
        settings.type = 2; settings.threshold = 0.1f;
        jassert(shape(0.05f, settings) == 0.05f && shape(10, settings) <= 1);
        settings.type = 7; settings.drive = settings.curve = settings.damp = settings.depth = settings.period = 1;
        jassert(std::isfinite(shape(1000, settings)) && shape(0, settings) == 0);
        const auto coefficients = colorCoefficients(48000);
        for (const auto& c : coefficients)
        { const auto z = std::polar(1.0, -0.3); const auto numerator = c[0] + c[1] * z + c[2] * z * z;
          const auto denominator = c[3] + c[4] * z + c[5] * z * z;
          jassert(std::abs(numerator / denominator * (denominator / numerator) - 1.0) < 1.0e-9); }
       #endif
    }
    Shape shapeSettings() const
    { return {static_cast<int>(value("type")), juce::Decibels::decibelsToGain(value("threshold")), value("shape_drive") / 100,
        value("linear") / 100, value("curve") / 100, value("damp") / 100, value("depth") / 100, value("period") / 100}; }
    static float analog(float x)
    {
        const auto a = std::abs(x);
        return a <= 0.5f ? x : std::copysign(1 - 0.5f * std::exp(-2 * (a - 0.5f)), x);
    }
    static float shape(float x, const Shape& s)
    {
        const auto a = std::abs(x);
        switch (s.type)
        {
            case 0: return analog(x);
            case 1: return std::sin(juce::jlimit(-juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, x));
            case 2:
                if (s.threshold >= 0.9999f) return juce::jlimit(-1.0f, 1.0f, x);
                return a <= s.threshold ? x : std::copysign(s.threshold + (1 - s.threshold) * std::tanh((a - s.threshold) / (1 - s.threshold)), x);
            case 3: return std::atan(x * juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::halfPi;
            case 4: return a > 1000000 ? std::copysign(1.0f, x) : x / std::sqrt(std::sqrt(1 + a * a * a * a));
            case 5: return std::sin(x);
            case 6: return juce::jlimit(-1.0f, 1.0f, x);
            case 7:
            {
                const auto bounded = juce::jlimit(-8.0f, 8.0f, x);
                auto y = s.linear * x + (1 - s.linear) * std::tanh(x + 4 * s.curve * bounded * bounded * bounded);
                y += s.depth * std::sin(x * juce::MathConstants<float>::pi * (1 + 31 * s.period));
                if (s.damp > 0) y *= 1 - std::exp(-a / (s.damp * 0.25f));
                return x + s.drive * (y - x);
            }
            default: return x;
        }
    }
    static float colorQ(float width) { return 0.1f * std::pow(100.0f, 1 - width / 100); }
    std::array<Biquad::Coefficients, 2> colorCoefficients(double rate) const
    {
        using C = juce::dsp::IIR::ArrayCoefficients<double>;
        return {C::makeLowShelf(rate, std::min(100.0, rate * 0.45), 0.707, juce::Decibels::decibelsToGain(static_cast<double>(value("low")))),
            C::makePeakFilter(rate, std::min(static_cast<double>(value("frequency")), rate * 0.45), colorQ(value("width")), juce::Decibels::decibelsToGain(static_cast<double>(value("high"))))};
    }
    void prepareToPlay(double rate, int block) override
    {
        sampleRate = rate; oversampling.initProcessing(static_cast<size_t>(block)); oversampling.reset();
        const auto latency = oversampling.getLatencyInSamples(); setLatencySamples(static_cast<int>(std::round(latency)));
        lowQualityDelay.prepare({rate, static_cast<juce::uint32>(block), 2}); lowQualityDelay.setDelay(latency); lowQualityDelay.reset();
        mixer.setMixingRule(juce::dsp::DryWetMixingRule::linear);
        mixer.setWetMixProportion(value("mix")); mixer.prepare({rate, static_cast<juce::uint32>(block), 2}); mixer.setWetLatency(latency); mixer.reset();
        quality = value("quality") > 0.5f; resetFilters();
        drive.reset(rate * (quality ? 4 : 1), 0.02); drive.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(value("drive")));
        output.reset(rate, 0.02); output.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(value("output"))); inputPeak.store(0);
    }
    void resetFilters() { dc.reset(); for (auto& filter : preColor) filter.reset(); for (auto& filter : postColor) filter.reset(); }
    double getTailLengthSeconds() const override { return 0; }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard;
        const bool nextQuality = value("quality") > 0.5f;
        if (quality != nextQuality)
        { quality = nextQuality; resetFilters(); oversampling.reset(); lowQualityDelay.reset(); drive.reset(sampleRate * (quality ? 4 : 1), 0.02); }
        drive.setTargetValue(juce::Decibels::decibelsToGain(value("drive"))); output.setTargetValue(juce::Decibels::decibelsToGain(value("output")));
        mixer.setWetMixProportion(value("mix"));
        const auto rate = sampleRate * (quality ? 4 : 1); dc.set(juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(rate, 5));
        const auto coefficients = colorCoefficients(rate);
        for (size_t i = 0; i < coefficients.size(); ++i)
        { preColor[i].set(coefficients[i]); const auto& c = coefficients[i]; postColor[i].set({c[3], c[4], c[5], c[0], c[1], c[2]}); }
        const auto settings = shapeSettings(); const bool useDC = value("dc") > 0.5f, useColor = value("color") > 0.5f;
        const auto post = static_cast<int>(value("post")); float peak = 0;
        juce::dsp::AudioBlock<float> base(audio); mixer.pushDrySamples(base);
        auto wet = quality ? oversampling.processSamplesUp(base) : base;
        for (size_t i = 0; i < wet.getNumSamples(); ++i)
        {
            const auto inputGain = drive.getNextValue();
            for (size_t c = 0; c < wet.getNumChannels(); ++c)
            {
                auto* samples = wet.getChannelPointer(c); const auto channel = static_cast<int>(c);
                auto x = samples[i]; const auto filtered = dc.sample(channel, x); if (useDC) x = filtered;
                x *= inputGain;
                for (auto& filter : preColor) { const auto colored = filter.sample(channel, x); if (useColor) x = colored; }
                peak = std::max(peak, std::abs(x)); x = shape(x, settings);
                for (auto& filter : postColor) { const auto colored = filter.sample(channel, x); if (useColor) x = colored; }
                if (post == 1) x = analog(x); else if (post == 2) x = juce::jlimit(-1.0f, 1.0f, x);
                samples[i] = x;
            }
        }
        if (quality) oversampling.processSamplesDown(base);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const auto gain = output.getNextValue();
            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                auto x = audio.getSample(c, i);
                if (!quality) { lowQualityDelay.pushSample(c, x); x = lowQualityDelay.popSample(c); }
                // Keep the selected post-clip ceiling after the oversampling filters.
                if (post != 0) x = juce::jlimit(-1.0f, 1.0f, x);
                audio.setSample(c, i, x * gain);
            }
        }
        inputPeak.store(std::max(peak, inputPeak.load() * static_cast<float>(std::exp(-audio.getNumSamples() / (sampleRate * 0.15)))));
        mixer.mixWetSamples(base);
    }
    void getStateInformation(juce::MemoryBlock& bytes) override
    {
        Device::getStateInformation(bytes); auto xml = getXmlFromBinary(bytes.getData(), static_cast<int>(bytes.getSize()));
        xml->setAttribute("saturatorVersion", 2); copyXmlToBinary(*xml, bytes);
    }
    void setStateInformation(const void* bytes, int size) override
    {
        if (!bytes || size <= 0 || size > 1024 * 1024) return;
        auto xml = getXmlFromBinary(bytes, size); if (!xml || !xml->hasTagName("OpentoneDevice") || xml->getStringAttribute("id") != id) return;
        if (xml->getIntAttribute("saturatorVersion", 1) < 2)
        {
            // Preserve the original three-knob device's dB values as the ranges expand.
            for (auto* item : xml->getChildIterator())
            {
                const auto key = item->getStringAttribute("id"), text = item->getStringAttribute("value").trim();
                const auto* begin = text.toRawUTF8(); char* end = nullptr; const auto v = std::strtod(begin, &end);
                if (end == begin || *end != 0 || !std::isfinite(v) || v < 0 || v > 1) return;
                if (key == "drive") item->setAttribute("value", (v + 1) / 2);
                if (key == "output") item->setAttribute("value", v / 2);
            }
        }
        juce::MemoryBlock state; copyXmlToBinary(*xml, state); Device::setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    }
    juce::dsp::Oversampling<float> oversampling {2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true};
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> lowQualityDelay {128};
    juce::dsp::DryWetMixer<float> mixer {128};
    juce::SmoothedValue<float> drive, output; Biquad dc; std::array<Biquad, 2> preColor, postColor;
    std::atomic<float> inputPeak {0}; double sampleRate = 48000; bool quality = true;
};
struct Dynamics final : Device
{
    explicit Dynamics(bool limiter) : Device(limiter ? "Limiter" : "Compressor", limiter ? "opentone.limiter" : "opentone.compressor"), limiting(limiter)
    { if (limiting) { number("input", "Input (dB)", -12, 24, 0); number("ceiling", "Ceiling (dB)", -18, 0, -1); number("release", "Release (ms)", 5, 2000, 100, 200); }
      else { number("threshold", "Threshold (dB)", -60, 0, -18); number("ratio", "Ratio", 1, 20, 4, 4); number("attack", "Attack (ms)", 0.1f, 100, 10, 10); number("release", "Release (ms)", 5, 2000, 100, 200); number("knee", "Knee (dB)", 0, 24, 6); number("makeup", "Makeup (dB)", -12, 24, 0); number("mix", "Dry/Wet", 0, 1, 1); toggle("auto_makeup", "Auto makeup", false);
        toggle("side_eq", "Sidechain EQ", false); number("side_frequency", "Sidechain frequency (Hz)", 20, 20000, 276, 200);
        number("side_q", "Sidechain Q", 0.1f, 12, 1); } }
    void prepareToPlay(double rate, int) override { sampleRate = rate; envelope = 0; gain = 1; sideFilter.reset(); reduction.store(0); }
    double getTailLengthSeconds() const override { return 0; }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard; const auto release = static_cast<float>(std::exp(-1 / (sampleRate * value("release") / 1000)));
        const auto attack = limiting ? 0.0f : static_cast<float>(std::exp(-1 / (sampleRate * value("attack") / 1000)));
        const auto input = limiting ? juce::Decibels::decibelsToGain(value("input")) : 1.0f, ceiling = limiting ? juce::Decibels::decibelsToGain(value("ceiling")) : 1.0f;
        const auto threshold = limiting ? 0 : value("threshold"), ratio = limiting ? 1 : value("ratio"), knee = limiting ? 0 : value("knee");
        const auto makeup = limiting ? 1.0f : juce::Decibels::decibelsToGain(value("makeup") + (value("auto_makeup") > 0.5f ? -threshold * (1 - 1 / ratio) * 0.5f : 0)), mix = limiting ? 1.0f : value("mix");
        const auto filtered = !limiting && value("side_eq") > 0.5f;
        if (filtered) sideFilter.set(juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(sampleRate, std::min(static_cast<double>(value("side_frequency")), sampleRate * 0.45), value("side_q")));
        for (int i = 0; i < audio.getNumSamples(); ++i)
        { const auto* detector = sidechain && !limiting ? sidechain : &audio;
          auto l = detector->getSample(0, i) * input, r = detector->getSample(1, i) * input;
          if (filtered) { l = sideFilter.sample(0, l); r = sideFilter.sample(1, r); }
          const auto peak = std::max(std::abs(l), std::abs(r));
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
    const juce::AudioBuffer<float>* sidechain = nullptr; Biquad sideFilter; bool limiting; double sampleRate = 48000; float envelope = 0, gain = 1;
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
struct FM : Device
{
    struct Settings { float ratio = 1, level = 0, tune = 0; int wave = 0; juce::ADSR::Parameters envelope; };
    struct Voice { int note = -1, channel = 0; bool held = false; uint64_t age = 0; float velocity = 0; double frequency = 440, feedback = 0; std::array<double, 4> phase {}; std::array<juce::ADSR, 4> envelopes; };
    explicit FM(bool table = false, bool live = false) : Device(table ? "Wavetable Synth" : "FM Synth", table ? "opentone.wavetable" : "opentone.fm", true), wavetable(table)
    {
        if (table) { number("filter", "Cutoff (Hz)", 20, 20000, 18000, 2000); number("resonance", "Resonance", 0.1f, 1, 0.15f); number("volume", "Volume (dB)", -48, 6, -18); return; }
        choice("algorithm", "Algorithm", {"D → C → B → A", "C+D → B → A", "D → B+C → A", "B+C+D → A", "B → A / D → C", "D → A+B+C", "C → A / D → B", "A+B+C+D"});
        number("feedback", "Feedback", 0, 1, 0); number("filter", "Cutoff (Hz)", 20, 20000, 18000, 2000); number("resonance", "Resonance", 0.1f, 1, 0.15f); number("volume", "Volume (dB)", -48, 6, -18);
        for (int op = 0; op < 4; ++op)
        { const auto p = prefix(op); number(p + "ratio", "Ratio", live ? 0.01f : 0.25f, live ? 48.0f : 32.0f, 1, 2); number(p + "level", "Level", 0, 1, op == 0 ? 0.9f : op == 1 ? 0.5f : 0); number(p + "tune", "Detune (cents)", -100, 100, 0);
          choice(p + "wave", "Wave", {"Sine", "Triangle", "Saw", "Square"}); number(p + "attack", "Attack (ms)", live ? 0.1f : 1.0f, live ? 20000.0f : 10000.0f, 5, 100); number(p + "decay", "Decay (ms)", 1, live ? 60000.0f : 10000.0f, 400, 400); number(p + "sustain", "Sustain", 0, 1, op == 0 ? 0.8f : 0.3f); number(p + "release", "Release (ms)", 1, live ? 60000.0f : 20000.0f, 200, 400);
          int i = 0; for (const auto& key : {"ratio", "level", "tune", "wave", "attack", "decay", "sustain", "release"}) inputs[static_cast<size_t>(op)][static_cast<size_t>(i++)] = parameters.at(p + key); }
        presets.push_back({"Velvet Electric Piano", {{"algorithm", 4}, {"b_ratio", 1}, {"b_level", 0.28f}, {"b_decay", 700}, {"b_sustain", 0}, {"d_ratio", 7}, {"d_level", 0.15f}, {"c_level", 0.55f}, {"a_decay", 1800}, {"a_sustain", 0.15f}, {"a_release", 650}}});
        presets.push_back({"Bright Tines", {{"b_ratio", 3}, {"b_level", 0.65f}, {"b_decay", 900}, {"b_sustain", 0}, {"a_decay", 2100}, {"a_sustain", 0}, {"a_release", 800}}});
        presets.push_back({"Glass Bell", {{"b_ratio", 3.5f}, {"b_level", 0.8f}, {"b_decay", 1800}, {"b_sustain", 0}, {"a_decay", 3500}, {"a_sustain", 0}, {"a_release", 1800}}});
        presets.push_back({"Tubular Chime", {{"b_ratio", 2.75f}, {"b_level", 0.7f}, {"c_ratio", 5.5f}, {"c_level", 0.15f}, {"a_decay", 4000}, {"a_sustain", 0}, {"a_release", 2000}}});
        presets.push_back({"Music Box", {{"b_ratio", 5}, {"b_level", 0.35f}, {"b_decay", 400}, {"b_sustain", 0}, {"a_decay", 1400}, {"a_sustain", 0}, {"a_release", 600}}});
        presets.push_back({"Marimba", {{"b_ratio", 4}, {"b_level", 0.6f}, {"b_decay", 90}, {"b_sustain", 0}, {"a_decay", 500}, {"a_sustain", 0}, {"a_release", 100}}});
        presets.push_back({"Rubber Bass", {{"b_ratio", 1}, {"b_level", 0.8f}, {"b_decay", 140}, {"b_sustain", 0.08f}, {"a_decay", 250}, {"a_sustain", 0.65f}, {"filter", 2400}}});
        presets.push_back({"Deep Sub", {{"b_level", 0.04f}, {"a_sustain", 1}, {"a_release", 90}, {"filter", 700}}});
        presets.push_back({"Metal Bass", {{"b_ratio", 2}, {"b_level", 0.85f}, {"c_ratio", 3}, {"c_level", 0.2f}, {"feedback", 0.2f}, {"filter", 3200}, {"a_release", 80}}});
        presets.push_back({"Picked Bass", {{"b_ratio", 2}, {"b_level", 0.6f}, {"b_decay", 80}, {"b_sustain", 0}, {"a_decay", 450}, {"a_sustain", 0.3f}, {"filter", 4200}}});
        presets.push_back({"Reed Organ", {{"algorithm", 7}, {"a_level", 0.8f}, {"b_ratio", 2}, {"b_level", 0.4f}, {"c_ratio", 3}, {"c_level", 0.25f}, {"d_ratio", 4}, {"d_level", 0.15f}, {"b_sustain", 1}, {"c_sustain", 1}, {"d_sustain", 1}}});
        presets.push_back({"Warm Brass", {{"b_ratio", 1}, {"b_level", 0.45f}, {"a_attack", 70}, {"b_attack", 100}, {"filter", 3200}, {"a_release", 250}}});
        presets.push_back({"Breathy Flute", {{"b_ratio", 1}, {"b_level", 0.08f}, {"a_attack", 90}, {"a_release", 300}, {"filter", 4800}}});
        presets.push_back({"Solo Reed", {{"b_ratio", 2}, {"b_level", 0.32f}, {"a_attack", 35}, {"filter", 5000}, {"resonance", 0.22f}}});
        presets.push_back({"Silk Pad", {{"algorithm", 4}, {"a_attack", 1200}, {"c_attack", 1500}, {"c_level", 0.65f}, {"c_tune", 7}, {"b_level", 0.18f}, {"d_level", 0.12f}, {"a_release", 3000}, {"c_release", 3500}, {"filter", 3500}}});
        presets.push_back({"Ice Pad", {{"algorithm", 6}, {"a_attack", 1800}, {"b_attack", 2000}, {"b_level", 0.5f}, {"c_level", 0.2f}, {"c_ratio", 7}, {"d_level", 0.15f}, {"d_ratio", 5}, {"a_release", 4000}, {"b_release", 4000}}});
        presets.push_back({"Plucked Wire", {{"b_ratio", 3}, {"b_level", 0.8f}, {"b_decay", 120}, {"b_sustain", 0}, {"a_decay", 650}, {"a_sustain", 0}, {"a_release", 180}}});
        presets.push_back({"Digital Lead", {{"b_ratio", 1}, {"b_level", 0.4f}, {"b_wave", 2}, {"filter", 6500}, {"a_release", 120}}});
        presets.push_back({"Space Mallet", {{"b_ratio", 6.25f}, {"b_level", 0.5f}, {"feedback", 0.22f}, {"a_decay", 2200}, {"a_sustain", 0}, {"a_release", 1200}}});
        presets.push_back({"Soft Sine Keys", {{"b_level", 0}, {"a_decay", 1200}, {"a_sustain", 0.25f}, {"a_release", 450}, {"filter", 5000}}});
        validatePresets();
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
        updateSettings();
        filter.set(juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate * 2, std::min(static_cast<double>(value("filter")), sampleRate * 0.45), 0.5 + value("resonance") * 4));
        audio.clear(); juce::dsp::AudioBlock<float> base(audio); auto up = oversampling->processSamplesUp(base); int cursor = 0;
        for (const auto metadata : midi) { const auto at = juce::jlimit(cursor, static_cast<int>(up.getNumSamples()), metadata.samplePosition * 2); render(up, cursor, at); message(metadata.getMessage()); cursor = at; }
        render(up, cursor, static_cast<int>(up.getNumSamples())); oversampling->processSamplesDown(base);
    }
    virtual void updateSettings()
    {
        for (int op = 0; op < 4; ++op) { const auto& p = inputs[static_cast<size_t>(op)]; auto& s = settings[static_cast<size_t>(op)]; s.ratio = read(p[0]); s.level = read(p[1]); s.tune = read(p[2]); s.ratio *= std::pow(2.0f, s.tune / 1200); s.wave = static_cast<int>(read(p[3]));
            s.envelope = {read(p[4]) / 1000, read(p[5]) / 1000, read(p[6]), read(p[7]) / 1000}; for (auto& voice : voices) voice.envelopes[static_cast<size_t>(op)].setParameters(s.envelope); }
        algorithm = static_cast<int>(value("algorithm")); feedback = value("feedback"); volume.setTargetValue(juce::Decibels::decibelsToGain(value("volume")));
    }
    virtual void release(Voice& voice, bool immediate)
    { voice.held = false; for (auto& env : voice.envelopes) if (immediate) env.reset(); else env.noteOff(); if (immediate) voice.note = -1; }
    virtual void message(const juce::MidiMessage& midi)
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
    virtual double oscillator(Voice& v, int op, double modulation)
    {
        const auto i = static_cast<size_t>(op); const auto& s = settings[i]; const auto step = std::min(0.45, v.frequency * bend[static_cast<size_t>(v.channel)] * s.ratio / (sampleRate * 2));
        v.phase[i] += step; v.phase[i] -= std::floor(v.phase[i]); const auto level = v.envelopes[i].getNextSample() * s.level; if (level <= 0) return 0;
        return waveAt(v.phase[i] + modulation * 2, s.wave) * level;
    }
    static double waveAt(double phase, int wave)
    {
        const auto wrapped = phase - std::floor(phase);
        switch (wave) { case 1: return 1 - 4 * std::abs(wrapped - 0.5); case 2: return wrapped * 2 - 1; case 3: return wrapped < 0.5 ? 1 : -1; default: return std::sin(phase * juce::MathConstants<double>::twoPi); }
    }
    virtual double voiceSample(Voice& v)
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
        return a;
    }
    void render(juce::dsp::AudioBlock<float>& audio, int begin, int end)
    {
        for (int sample = begin; sample < end; ++sample)
        {
            double output = 0;
            for (auto& v : voices) if (v.note >= 0)
            {
                const auto a = voiceSample(v);
                output += a * v.velocity; bool alive = false; for (size_t op = 0; op < (wavetable ? 1u : 4u); ++op) alive = alive || v.envelopes[op].isActive(); if (!alive) v.note = -1;
            }
            const auto value = perVoiceFilter ? static_cast<float>(output) * volume.getNextValue() : filter.sample(0, static_cast<float>(output) * volume.getNextValue()); audio.getChannelPointer(0)[sample] = value; audio.getChannelPointer(1)[sample] = value;
        }
    }
    std::array<std::array<juce::RangedAudioParameter*, 8>, 4> inputs {};
    std::array<Voice, 16> voices; std::array<Settings, 4> settings; std::array<int, 16> bendRange {}, rpnMsb {}, rpnLsb {}; std::array<double, 16> bend {}; std::array<bool, 16> sustain {}; uint64_t age = 0;
    bool wavetable = false, perVoiceFilter = false;
    double sampleRate = 48000; int algorithm = 0; float feedback = 0; Biquad filter; juce::SmoothedValue<float> volume;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
};
#include "infrastructure/LiveDevices.inc"
struct Wavetable final : FM
{
    static constexpr int tableSize = 1024, levels = 7, frames = 4;
    using Table = std::array<float, tableSize + 1>;
    using Tables = std::array<std::array<std::array<Table, levels>, frames>, 3>;
    static const Tables& tables()
    {
        static const auto data = []
        {
            Tables result {};
            for (int bank = 0; bank < 3; ++bank) for (int frame = 0; frame < frames; ++frame) for (int level = 0; level < levels; ++level)
            {
                auto& table = result[static_cast<size_t>(bank)][static_cast<size_t>(frame)][static_cast<size_t>(level)];
                for (int harmonic = 1; harmonic <= (64 >> level); ++harmonic)
                {
                    double weight = 0;
                    if (bank == 0)
                    {
                        if (frame == 0) weight = harmonic == 1 ? 1 : 0;
                        if (frame == 1 && harmonic % 2) weight = (harmonic % 4 == 1 ? 1.0 : -1.0) / (harmonic * harmonic);
                        if (frame == 2) weight = 1.0 / harmonic;
                        if (frame == 3 && harmonic % 2) weight = 1.0 / harmonic;
                    }
                    else if (bank == 1) weight = (0.25 + 0.75 * std::cos(harmonic * (frame + 1) * 0.7)) / std::pow(harmonic, 0.8 + frame * 0.15);
                    else weight = (0.2 / harmonic + std::exp(-std::pow((harmonic - (3 + frame * 3)) / 2.0, 2)) + 0.5 * std::exp(-std::pow((harmonic - (10 + frame * 4)) / 3.0, 2))) / std::sqrt(harmonic);
                    for (int i = 0; i < tableSize; ++i) table[static_cast<size_t>(i)] += static_cast<float>(weight * std::sin(juce::MathConstants<double>::twoPi * harmonic * i / tableSize));
                }
                float peak = 0; for (const auto sample : table) peak = std::max(peak, std::abs(sample));
                jassert(std::isfinite(peak) && peak > 0);
                if (peak > 0) for (auto& sample : table) sample /= peak;
                table[tableSize] = table[0];
            }
            return result;
        }();
        return data;
    }
    Wavetable() : FM(true), waveTables(tables())
    {
        choice("table", "Wavetable", {"Classic", "Digital", "Vocal"});
        number("position", "Position", 0, 3, 1.5f); number("scan", "Scan depth", 0, 1.5f, 0.2f);
        number("rate", "Scan rate (Hz)", 0.01f, 20, 0.25f, 1); number("detune", "Detune (cents)", 0, 50, 7);
        number("blend", "Oscillator blend", 0, 1, 0.5f);
        number("attack", "Attack (ms)", 1, 10000, 10, 100); number("decay", "Decay (ms)", 1, 10000, 500, 400);
        number("sustain", "Sustain", 0, 1, 0.75f); number("release", "Release (ms)", 1, 20000, 500, 400);
        order = {"table", "position", "scan", "rate", "detune", "blend", "filter", "resonance", "volume", "attack", "decay", "sustain", "release"};
        presets.push_back({"Warm Saw", {{"position", 2}, {"scan", 0}, {"filter", 3000}}});
        presets.push_back({"Pure Sub", {{"position", 0}, {"scan", 0}, {"detune", 0}, {"blend", 0}, {"filter", 600}, {"attack", 5}, {"release", 100}}});
        presets.push_back({"Round Triangle", {{"position", 1}, {"scan", 0}, {"filter", 5000}, {"detune", 0}, {"release", 180}}});
        presets.push_back({"Pulse Lead", {{"position", 3}, {"scan", 0}, {"detune", 4}, {"filter", 7000}, {"release", 160}}});
        presets.push_back({"Wide Supersaw", {{"position", 2}, {"scan", 0.3f}, {"rate", 0.4f}, {"detune", 24}, {"filter", 10000}, {"volume", -21}}});
        presets.push_back({"Analog Pluck", {{"position", 2.3f}, {"scan", 0}, {"decay", 320}, {"sustain", 0}, {"release", 120}, {"filter", 4200}}});
        presets.push_back({"Soft Keys", {{"position", 0.7f}, {"scan", 0.1f}, {"decay", 1300}, {"sustain", 0.2f}, {"release", 450}, {"filter", 4800}}});
        presets.push_back({"Slow Bloom", {{"position", 1.5f}, {"scan", 1.2f}, {"rate", 0.12f}, {"attack", 2200}, {"release", 4000}, {"filter", 4500}}});
        presets.push_back({"Shimmer Pad", {{"table", 1}, {"position", 2}, {"scan", 0.7f}, {"rate", 0.3f}, {"attack", 1600}, {"release", 3500}, {"detune", 18}, {"filter", 6500}}});
        presets.push_back({"Vocal Air", {{"table", 2}, {"position", 1.4f}, {"scan", 1.1f}, {"rate", 0.18f}, {"attack", 800}, {"release", 2500}, {"filter", 8000}}});
        presets.push_back({"Talking Bass", {{"table", 2}, {"position", 0.8f}, {"scan", 0.8f}, {"rate", 3}, {"filter", 2600}, {"resonance", 0.3f}, {"release", 120}}});
        presets.push_back({"Digital Glass", {{"table", 1}, {"position", 0}, {"scan", 0.25f}, {"rate", 0.8f}, {"decay", 2200}, {"sustain", 0}, {"release", 1200}, {"filter", 12000}}});
        presets.push_back({"Hollow Bells", {{"table", 1}, {"position", 2.7f}, {"scan", 0.15f}, {"decay", 3200}, {"sustain", 0}, {"release", 1800}, {"detune", 3}}});
        presets.push_back({"Spectral Sweep", {{"table", 1}, {"position", 1.5f}, {"scan", 1.5f}, {"rate", 0.6f}, {"attack", 300}, {"release", 900}}});
        presets.push_back({"Nasal Reed", {{"table", 2}, {"position", 0.3f}, {"scan", 0.2f}, {"rate", 2}, {"attack", 50}, {"release", 280}, {"detune", 0}, {"filter", 4800}}});
        presets.push_back({"Choir Drift", {{"table", 2}, {"position", 2.1f}, {"scan", 0.8f}, {"rate", 0.09f}, {"attack", 1800}, {"release", 4500}, {"detune", 15}, {"filter", 5500}}});
        presets.push_back({"Acid Pulse", {{"position", 2.8f}, {"scan", 0.2f}, {"rate", 4}, {"filter", 1400}, {"resonance", 0.65f}, {"decay", 220}, {"sustain", 0.4f}, {"release", 100}}});
        presets.push_back({"Arcade Pluck", {{"table", 1}, {"position", 1.2f}, {"scan", 0.4f}, {"rate", 8}, {"decay", 180}, {"sustain", 0}, {"release", 80}, {"detune", 0}}});
        presets.push_back({"Dark Drone", {{"table", 2}, {"position", 2.6f}, {"scan", 0.4f}, {"rate", 0.04f}, {"attack", 3000}, {"release", 6000}, {"filter", 900}, {"detune", 30}}});
        presets.push_back({"Bright Motion", {{"table", 1}, {"position", 1.6f}, {"scan", 1.3f}, {"rate", 2.5f}, {"attack", 20}, {"release", 400}, {"filter", 14000}, {"detune", 12}}});
        validatePresets();
    }
    void updateSettings() override
    {
        bank = static_cast<int>(value("table")); position = value("position"); depth = value("scan"); scanStep = value("rate") / (sampleRate * 2);
        spread = std::pow(2.0, value("detune") / 2400); blend = value("blend"); smoothing = 1 - std::exp(-1 / (sampleRate * 0.02));
        const juce::ADSR::Parameters envelope {value("attack") / 1000, value("decay") / 1000, value("sustain"), value("release") / 1000};
        for (auto& voice : voices) voice.envelopes[0].setParameters(envelope);
        volume.setTargetValue(juce::Decibels::decibelsToGain(value("volume")));
    }
    double lookup(double phase, double step, double frame) const
    {
        int level = 0; while (level < levels - 1 && (64 >> level) * step > 0.45) ++level;
        const auto first = std::min(frames - 2, static_cast<int>(frame)); const auto morph = frame - first;
        const auto at = phase * tableSize; const auto index = static_cast<size_t>(at); const auto fraction = at - static_cast<double>(index);
        const auto& a = waveTables[static_cast<size_t>(bank)][static_cast<size_t>(first)][static_cast<size_t>(level)];
        const auto& b = waveTables[static_cast<size_t>(bank)][static_cast<size_t>(first + 1)][static_cast<size_t>(level)];
        return (a[index] + (a[index + 1] - a[index]) * fraction) * (1 - morph) + (b[index] + (b[index + 1] - b[index]) * fraction) * morph;
    }
    double voiceSample(Voice& voice) override
    {
        voice.phase[2] += scanStep; voice.phase[2] -= std::floor(voice.phase[2]);
        const auto target = juce::jlimit(0.0, 3.0, position + depth * std::sin(voice.phase[2] * juce::MathConstants<double>::twoPi));
        voice.feedback += (target - voice.feedback) * smoothing;
        const auto frequency = voice.frequency * bend[static_cast<size_t>(voice.channel)]; double output = 0;
        for (size_t osc = 0; osc < 2; ++osc)
        {
            const auto step = std::min(0.45, frequency * (osc == 0 ? 1 / spread : spread) / (sampleRate * 2));
            voice.phase[osc] += step; voice.phase[osc] -= std::floor(voice.phase[osc]);
            output += lookup(voice.phase[osc], step, voice.feedback) * (osc == 0 ? 1 - blend : blend);
        }
        return output * voice.envelopes[0].getNextSample();
    }
    const Tables& waveTables;
    int bank = 0; double position = 0, depth = 0, scanStep = 0, spread = 1, blend = 0.5, smoothing = 0;
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
class SaturatorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SaturatorEditor(Saturator& device) : AudioProcessorEditor(device), owner(device)
    {
        for (const auto& key : owner.order)
        { auto control = std::make_unique<Control>(); control->bind(owner, key); addAndMakeVisible(*control); controls[key] = std::move(control); }
        addAndMakeVisible(colorTab); addAndMakeVisible(shapeTab);
        colorTab.onClick = [this] { waveshaper = false; updateControls(); };
        shapeTab.onClick = [this] { waveshaper = true; updateControls(); };
        setSize(1060, 178); updateControls(); addMouseListener(this, true); startTimerHz(30);
    }
    ~SaturatorEditor() override { endDrag(); removeMouseListener(this); }
    juce::Rectangle<float> shaperGraph() const { return {232, 18, 202, static_cast<float>(getHeight() - 70)}; }
    juce::Rectangle<float> colorGraph() const { return {540, 35, static_cast<float>(getWidth() - 552), static_cast<float>(std::max(25, getHeight() - 126))}; }
    float frequencyX(float frequency) const { const auto r = colorGraph(); return r.getX() + std::log(frequency / 20) / std::log(1000.0f) * r.getWidth(); }
    float gainY(float gain) const { return colorGraph().getCentreY() - gain / 72 * colorGraph().getHeight(); }
    void resized() override
    {
        controls.at("type")->setBounds(4, 0, 220, 43);
        for (int i = 0; i < 3; ++i) controls.at(std::array<const char*, 3> {"drive", "output", "mix"}[static_cast<size_t>(i)])->setBounds(4 + i * 73, 43, 73, getHeight() - 88);
        controls.at("post")->setBounds(4, getHeight() - 45, 220, 43);
        controls.at("dc")->setBounds(232, getHeight() - 47, 100, 43); controls.at("quality")->setBounds(334, getHeight() - 47, 100, 43);
        controls.at("color")->setBounds(442, 0, 90, 43); controls.at("threshold")->setBounds(442, 51, 90, getHeight() - 64);
        colorTab.setBounds(540, 3, 98, 24); shapeTab.setBounds(644, 3, 108, 24);
        int i = 0; const int colorWidth = (getWidth() - 540) / 4;
        for (const auto& key : {"low", "high", "frequency", "width"}) controls.at(key)->setBounds(540 + i++ * colorWidth, getHeight() - 80, colorWidth, 78);
        i = 0; const int shapeWidth = (getWidth() - 540) / 6;
        for (const auto& key : {"shape_drive", "linear", "curve", "damp", "depth", "period"}) controls.at(key)->setBounds(540 + i++ * shapeWidth, 59, shapeWidth, getHeight() - 64);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        const auto ink = findColour(juce::Label::textColourId), accent = findColour(juce::TextButton::buttonOnColourId);
        const auto area = shaperGraph(); drawGrid(g, area, ink); const auto settings = owner.shapeSettings();
        juce::Path path;
        for (int i = 0; i <= 202; ++i)
        {
            const auto x = static_cast<float>(i) / 202 * 4 - 2;
            const auto y = juce::jlimit(-1.2f, 1.2f, Saturator::shape(x, settings));
            const auto point = juce::Point<float>(area.getX() + static_cast<float>(i), area.getCentreY() - y * area.getHeight() / 2.4f);
            if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
        }
        g.setColour(accent); g.strokePath(path, juce::PathStrokeType(1.5f));
        const auto peak = std::min(2.0f, owner.inputPeak.load()); g.setColour(accent.withAlpha(0.16f));
        g.fillRect(area.withX(area.getCentreX() - peak * area.getWidth() / 4).withWidth(peak * area.getWidth() / 2));
        g.setColour(ink); g.setFont(juce::FontOptions(10)); g.drawText("Shaper / input level", 232, 0, 202, 17, juce::Justification::centred);
        if (waveshaper)
        { g.setColour(ink.withAlpha(0.6f)); g.drawText(owner.value("type") == 7 ? "Custom shaping curve" : "Select Waveshaper to edit its six controls", 540, 31, getWidth() - 548, 22, juce::Justification::centredLeft); return; }
        const auto colorArea = colorGraph(); drawGrid(g, colorArea, ink);
        const auto rate = owner.getSampleRate() > 0 ? owner.getSampleRate() : 48000;
        const auto filterRate = rate * (owner.value("quality") > 0.5f ? 4 : 1);
        const auto coefficients = owner.colorCoefficients(filterRate); juce::Path colorPath;
        for (int i = 0; i <= static_cast<int>(colorArea.getWidth()); ++i)
        {
            const auto frequency = 20 * std::pow(1000.0, i / static_cast<double>(colorArea.getWidth()));
            const auto z = std::polar(1.0, -juce::MathConstants<double>::twoPi * frequency / filterRate); double magnitude = 1;
            for (const auto& c : coefficients) magnitude *= std::abs((c[0] + c[1] * z + c[2] * z * z) / (c[3] + c[4] * z + c[5] * z * z));
            const auto y = juce::jlimit(colorArea.getY(), colorArea.getBottom(), gainY(static_cast<float>(20 * std::log10(std::max(1e-12, magnitude)))));
            if (i == 0) colorPath.startNewSubPath(colorArea.getX(), y); else colorPath.lineTo(colorArea.getX() + static_cast<float>(i), y);
        }
        g.setColour(accent.withAlpha(owner.value("color") > 0.5f ? 1.0f : 0.35f)); g.strokePath(colorPath, juce::PathStrokeType(1.5f));
        for (const auto point : {juce::Point<float>(frequencyX(100), gainY(owner.value("low"))), juce::Point<float>(frequencyX(owner.value("frequency")), gainY(owner.value("high")))})
            g.fillEllipse(point.x - 3, point.y - 3, 6, 6);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.originalComponent != this) return;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu; menu.addItem(1, "Hi-Quality", true, owner.value("quality") > 0.5f); menu.addItem(2, "Pre-DC Filter", true, owner.value("dc") > 0.5f);
            juce::Component::SafePointer<SaturatorEditor> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice)
            { if (safe && choice > 0) { const auto key = choice == 1 ? "quality" : "dc"; auto* p = safe->owner.parameters.at(key); p->beginChangeGesture(); safe->owner.change(key, safe->owner.value(key) > 0.5f ? 0 : 1); p->endChangeGesture(); } });
            return;
        }
        if (waveshaper || !e.mods.isLeftButtonDown() || !colorGraph().contains(e.position) || owner.value("color") < 0.5f) return;
        const auto low = juce::Point<float>(frequencyX(100), gainY(owner.value("low")));
        const auto high = juce::Point<float>(frequencyX(owner.value("frequency")), gainY(owner.value("high")));
        const auto lowDistance = low.getDistanceFrom(e.position), highDistance = high.getDistanceFrom(e.position);
        if (std::min(lowDistance, highDistance) > 14) return;
        dragging = lowDistance < highDistance ? 1 : 2;
        owner.parameters.at(dragging == 1 ? "low" : "high")->beginChangeGesture();
        if (dragging == 2) owner.parameters.at("frequency")->beginChangeGesture(); mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging) return; const auto area = colorGraph();
        owner.change(dragging == 1 ? "low" : "high", juce::jlimit(-36.0f, 36.0f, (area.getCentreY() - e.position.y) / area.getHeight() * 72));
        if (dragging == 2) owner.change("frequency", juce::jlimit(30.0f, 18500.0f, 20 * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth()))));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { endDrag(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        auto* component = e.originalComponent; while (component && !dynamic_cast<juce::Slider*>(component)) component = component->getParentComponent();
        if (auto* slider = dynamic_cast<juce::Slider*>(component)) { if (auto* label = dynamic_cast<juce::Label*>(e.originalComponent)) label->hideEditor(true); slider->setValue(slider->getDoubleClickReturnValue(), juce::sendNotificationSync); }
    }
private:
    static void drawGrid(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    { g.setColour(ink.withAlpha(0.06f)); g.fillRect(r); g.setColour(ink.withAlpha(0.2f)); g.drawHorizontalLine(juce::roundToInt(r.getCentreY()), r.getX(), r.getRight()); g.drawVerticalLine(juce::roundToInt(r.getCentreX()), r.getY(), r.getBottom()); }
    void endDrag()
    {
        if (!dragging) return; owner.parameters.at(dragging == 1 ? "low" : "high")->endChangeGesture();
        if (dragging == 2) owner.parameters.at("frequency")->endChangeGesture(); dragging = 0;
    }
    void updateControls()
    {
        colorTab.setToggleState(!waveshaper, juce::dontSendNotification); shapeTab.setToggleState(waveshaper, juce::dontSendNotification);
        controls.at("threshold")->setEnabled(owner.value("type") == 2);
        for (const auto& key : {"low", "high", "frequency", "width"}) { controls.at(key)->setVisible(!waveshaper); controls.at(key)->setEnabled(owner.value("color") > 0.5f); }
        for (const auto& key : {"shape_drive", "linear", "curve", "damp", "depth", "period"}) { controls.at(key)->setVisible(waveshaper); controls.at(key)->setEnabled(owner.value("type") == 7); }
        repaint();
    }
    void timerCallback() override { updateControls(); }
    Saturator& owner; std::map<juce::String, std::unique_ptr<Control>> controls;
    juce::TextButton colorTab {"Color"}, shapeTab {"Waveshaper"}; bool waveshaper = false; int dragging = 0;
};
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Device& device) : AudioProcessorEditor(device), owner(device)
    {
        eq = dynamic_cast<Equalizer*>(&owner); wt = dynamic_cast<Wavetable*>(&owner); fm = wt ? nullptr : dynamic_cast<FM*>(&owner);
        if (eq) { for (int i = 0; i < 8; ++i) addSelector(juce::String(i + 1), i); for (const auto& key : {"on", "type", "freq", "gain", "q"}) addControl(Equalizer::prefix(0) + key); addControl("output"); setSize(600, 178); }
        else if (fm)
        { addAndMakeVisible(algorithm); algorithm.addItemList(dynamic_cast<juce::AudioParameterChoice*>(owner.parameters.at("algorithm"))->choices, 1); algorithmAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*owner.parameters.at("algorithm"), algorithm, nullptr);
          for (int i = 0; i < 4; ++i) addSelector(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)), i);
          if (dynamic_cast<Operator*>(fm))
          { for (int i = 0; i < 4; ++i) addSelector(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)) + "+", i + 4); addSelector("Filter", 8); addSelector("Pitch", 9); }
          for (const auto& key : {"volume", "filter", "resonance", "feedback"}) addControl(key);
          for (const auto& key : {"ratio", "level", "tune", "wave", "attack", "decay", "sustain", "release"}) addControl(FM::prefix(0) + key); setSize(dynamic_cast<Operator*>(fm) ? 1000 : 700, 178); }
        else { for (const auto& key : owner.order) addControl(key); columns = owner.synth ? 7 : controls.size() <= 6 ? 3 : controls.size() <= 8 ? 4 : 5; setSize(columns * (owner.synth ? 100 : 94) + (wt ? 220 : 0) + 12, owner.synth ? 178 : 170); }
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
        if (fm)
        {
            juce::StringArray keys;
            if (index < 4) for (const auto* key : {"ratio", "level", "tune", "wave", "attack", "decay", "sustain", "release"}) keys.add(FM::prefix(index) + key);
            else if (index < 8) for (const auto* key : {"fixed", "frequency", "phase", "velocity"}) keys.add(FM::prefix(index - 4) + key);
            else if (index == 8) keys = {"filter_on", "filter_env", "filter_attack", "filter_decay", "filter_sustain", "filter_release"};
            else keys = {"pitch_on", "pitch_peak", "pitch_attack", "pitch_decay", "pitch_sustain", "pitch_release", "transpose"};
            for (int i = 0; i < 8; ++i)
            { controls[static_cast<size_t>(i + 4)]->setVisible(i < keys.size()); if (i < keys.size()) controls[static_cast<size_t>(i + 4)]->bind(owner, keys[i]); }
        }
        repaint();
    }
    void resized() override
    {
        if (eq) { for (size_t i = 0; i < selectors.size(); ++i) selectors[i]->setBounds(8 + static_cast<int>(i) * 32, 2, 28, 20); for (size_t i = 0; i < controls.size(); ++i) controls[i]->setBounds(8 + static_cast<int>(i) * (getWidth() - 16) / 6, getHeight() - 72, (getWidth() - 16) / 6, 70); }
        else if (fm) { algorithm.setBounds(8, 3, 264, 24); for (size_t i = 0; i < selectors.size(); ++i) selectors[i]->setBounds(8 + static_cast<int>(i) * 44, 34, 40, 23);
          for (int i = 0; i < 4; ++i) controls[static_cast<size_t>(i)]->setBounds(getWidth() - 320 + i * 78, 0, 78, 75);
          for (int i = 0; i < 8; ++i) controls[static_cast<size_t>(i + 4)]->setBounds(8 + i * (getWidth() - 16) / 8, getHeight() - 78, (getWidth() - 16) / 8, 76); }
        else { const int left = wt ? 220 : 0; const int rows = static_cast<int>((controls.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns)); const int height = (getHeight() - (owner.synth ? 4 : 22)) / std::max(1, rows);
          for (size_t i = 0; i < controls.size(); ++i) controls[i]->setBounds(left + 6 + static_cast<int>(i % static_cast<size_t>(columns)) * (getWidth() - left - 12) / columns, static_cast<int>(i / static_cast<size_t>(columns)) * height, (getWidth() - left - 12) / columns, height); }
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
            const auto area = juce::Rectangle<float>(8, 66, 166, 28); juce::Path wave;
            g.setColour(ink.withAlpha(0.06f)); g.fillRect(area);
            for (int i = 0; i <= 128; ++i)
            { const auto y = area.getCentreY() - static_cast<float>(FM::waveAt(i / 128.0, static_cast<int>(owner.value(FM::prefix(selected % 4) + "wave")))) * (area.getHeight() * 0.4f);
              const auto x = area.getX() + area.getWidth() * static_cast<float>(i) / 128;
              if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y); }
            g.setColour(primary); g.strokePath(wave, juce::PathStrokeType(1.2f));
            if (selected < 4) {
            const auto p = FM::prefix(selected); const float attack = owner.value(p + "attack"), decay = owner.value(p + "decay"), release = owner.value(p + "release"), sustain = owner.value(p + "sustain"); const auto total = attack + decay + release + 500;
            juce::Path envelope; envelope.startNewSubPath(188, 63); envelope.lineTo(188 + attack / total * 160, 33); envelope.lineTo(188 + (attack + decay) / total * 160, 63 - sustain * 30); envelope.lineTo(188 + (attack + decay + 500) / total * 160, 63 - sustain * 30); envelope.lineTo(348, 63); g.setColour(primary); g.strokePath(envelope, juce::PathStrokeType(1.5f)); }
        }
        else if (wt)
        {
            auto area = juce::Rectangle<float>(8, 8, 204, static_cast<float>(getHeight() - 16));
            g.setColour(ink.withAlpha(0.06f)); g.fillRect(area);
            g.setColour(ink); g.setFont(juce::FontOptions(11)); g.drawText("Waveform", area.removeFromTop(20), juce::Justification::centred);
            const auto position = owner.value("position"); const auto bank = static_cast<size_t>(owner.value("table"));
            const auto frame = std::min(Wavetable::frames - 2, static_cast<int>(position)); const auto mix = position - static_cast<float>(frame);
            const auto& tables = Wavetable::tables()[bank];
            g.setFont(juce::FontOptions(10));
            g.drawText(owner.parameters.at("table")->getCurrentValueAsText() + " / " + juce::String(position, 2), area.removeFromBottom(20), juce::Justification::centred);
            area.reduce(6, 6); g.setColour(ink.withAlpha(0.2f)); g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
            juce::Path wave;
            for (int i = 0; i <= 256; ++i)
            {
                const auto index = static_cast<size_t>(i * Wavetable::tableSize / 256);
                const auto sample = tables[static_cast<size_t>(frame)][0][index] * (1 - mix) + tables[static_cast<size_t>(frame + 1)][0][index] * mix;
                const auto x = area.getX() + area.getWidth() * static_cast<float>(i) / 256, y = area.getCentreY() - sample * area.getHeight() * 0.45f;
                if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
            }
            g.setColour(primary); g.strokePath(wave, juce::PathStrokeType(1.5f));
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
    Device& owner; Equalizer* eq = nullptr; FM* fm = nullptr; Wavetable* wt = nullptr; int selected = 0, columns = 3; bool dragging = false; float lastReduction = 0; juce::ComboBox algorithm;
    std::unique_ptr<juce::ComboBoxParameterAttachment> algorithmAttachment; std::vector<std::unique_ptr<Control>> controls; std::vector<std::unique_ptr<juce::TextButton>> selectors; std::vector<float> observed;
};
juce::AudioProcessorEditor* Device::createEditor()
{ if (auto* rack = dynamic_cast<DrumRack*>(this)) return new RackEditor(*rack); if (auto* saturator = dynamic_cast<Saturator*>(this)) return new SaturatorEditor(*saturator); return new Editor(*this); }
}
juce::Array<juce::PluginDescription> builtinPluginDescriptions()
{
    juce::Array<juce::PluginDescription> result;
    for (const auto& item : {std::pair<const char*, const char*> {"FM Synth", "opentone.fm"}, {"Wavetable Synth", "opentone.wavetable"}, {"Operator", "opentone.operator"}, {"Drum Rack", "opentone.drumrack"}, {"Dynamic Tube", "opentone.tube"}, {"Note Length", "opentone.notelength"}, {"Reverb", "opentone.reverb"}, {"Delay", "opentone.delay"}, {"Saturator", "opentone.saturator"}, {"Compressor", "opentone.compressor"}, {"Limiter", "opentone.limiter"}, {"Parametric EQ", "opentone.eq8"}})
    { juce::PluginDescription p; p.name = p.descriptiveName = item.first; p.fileOrIdentifier = item.second; p.pluginFormatName = p.manufacturerName = "Opentone"; p.version = "1"; p.isInstrument = (p.fileOrIdentifier == "opentone.fm" || p.fileOrIdentifier == "opentone.wavetable" || p.fileOrIdentifier == "opentone.operator" || p.fileOrIdentifier == "opentone.drumrack"); p.category = p.fileOrIdentifier == "opentone.notelength" ? "MIDI Effect" : p.isInstrument ? "Instrument" : "Effect"; p.numInputChannels = p.isInstrument ? 0 : 2; p.numOutputChannels = 2; p.uniqueId = p.fileOrIdentifier.hashCode(); result.add(p); }
    return result;
}
std::unique_ptr<juce::AudioProcessor> createBuiltinPlugin(const juce::String& id)
{
    if (id == "opentone.operator") return std::make_unique<Operator>();
    if (id == "opentone.drumrack") return std::make_unique<DrumRack>();
    if (id == "opentone.tube") return std::make_unique<Tube>();
    if (id == "opentone.notelength") return std::make_unique<NoteLength>();
    if (id == "opentone.wavetable") return std::make_unique<Wavetable>();
    if (id == "opentone.saturator") return std::make_unique<Saturator>();
    if (id == "opentone.fm") return std::make_unique<FM>(); if (id == "opentone.reverb") return std::make_unique<Reverb>(); if (id == "opentone.delay") return std::make_unique<Delay>();
    if (id == "opentone.compressor") return std::make_unique<Dynamics>(false); if (id == "opentone.limiter") return std::make_unique<Dynamics>(true); if (id == "opentone.eq8") return std::make_unique<Equalizer>(); return nullptr;
}
Plugin builtinPluginModel(const juce::String& id, const std::map<juce::String, float>& values, int preset)
{
    auto instance = createBuiltinPlugin(id); if (!instance) return {};
   #if JUCE_DEBUG
    for (const auto& item : values) { const auto* device = dynamic_cast<Device*>(instance.get()); jassert(device && device->parameters.count(item.first)); }
   #endif
    if (preset >= 0) { if (preset >= instance->getNumPrograms()) return {}; instance->setCurrentProgram(preset); }
    for (auto* item : instance->getParameters()) if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(item))
        if (const auto value = values.find(parameter->paramID); value != values.end())
        { const auto& range = parameter->getNormalisableRange(); parameter->setValue(parameter->convertTo0to1(juce::jlimit(range.start, range.end, value->second))); }
    juce::MemoryBlock state; instance->getStateInformation(state);
    for (const auto& description : builtinPluginDescriptions()) if (description.fileOrIdentifier == id)
        return {newId(), description.name, description.createXml()->toString(), state.toBase64Encoding(), description.isInstrument, false};
    return {};
}

Plugin builtinDrumRackModel(const std::vector<DrumRackBranch>& branches)
{
    DrumRack rack;
    for (const auto& model : branches)
    {
        DrumRack::Branch branch; branch.name = model.name; branch.receive = model.receive; branch.send = model.send;
        branch.gain = model.gain; branch.pan = model.pan; branch.mute = model.mute;
        for (const auto& plugin : model.devices)
        {
            const auto xml = juce::XmlDocument::parse(plugin.description); juce::PluginDescription description;
            if (!xml || !description.loadFromXml(*xml) || description.pluginFormatName != "Opentone") return {};
            DrumRack::Slot slot; slot.identifier = description.fileOrIdentifier; slot.bypass = plugin.bypass; slot.processor = createBuiltinPlugin(slot.identifier);
            juce::MemoryBlock bytes; if (!slot.processor || !bytes.fromBase64Encoding(plugin.state)) return {};
            slot.processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize())); branch.slots.push_back(std::move(slot));
        }
        rack.branches.push_back(std::move(branch));
    }
    juce::MemoryBlock bytes; rack.getStateInformation(bytes); auto model = builtinPluginModel("opentone.drumrack"); model.state = bytes.toBase64Encoding(); return model;
}
void setBuiltinSidechain(juce::AudioProcessor& processor, const juce::AudioBuffer<float>* audio)
{ if (auto* dynamics = dynamic_cast<Dynamics*>(&processor)) dynamics->sidechain = audio; }

namespace
{
struct SamplerPreset
{
    const char* name; int wave; double decay, attack, length, brightness, noise, modulation;
};
const std::array<SamplerPreset, 20> samplerPatches {{
    {"Felt Keys", 0, 0.8, 0.005, 3, 0.25, 0, 0},
    {"Bright Piano", 1, 0.65, 0.002, 3, 0.8, 0.01, 0},
    {"Electric Tines", 2, 1.1, 0.003, 4, 0.5, 0, 1.2},
    {"Toy Piano", 2, 0.5, 0.002, 2.5, 0.9, 0, 2.4},
    {"Music Box", 3, 0.9, 0.002, 4, 0.6, 0, 0},
    {"Glass Chime", 3, 1.5, 0.003, 5, 1, 0, 0},
    {"Wood Marimba", 2, 0.25, 0.002, 1.5, 0.3, 0.015, 2},
    {"Steel Kalimba", 3, 0.45, 0.003, 2.5, 0.35, 0.01, 0},
    {"Nylon Pluck", 1, 0.4, 0.004, 2.5, 0.4, 0.025, 0},
    {"Muted Guitar", 1, 0.12, 0.002, 1, 0.6, 0.04, 0},
    {"Round Bass", 0, 0.5, 0.008, 2.5, 0.1, 0, 0},
    {"Picked Bass", 1, 0.25, 0.002, 1.5, 0.7, 0.015, 0},
    {"Reed Organ", 4, 8, 0.015, 5, 0.7, 0, 0},
    {"Soft Flute", 0, 5, 0.12, 4, 0.08, 0.03, 0},
    {"Warm Strings", 1, 6, 0.7, 5, 0.45, 0.02, 0},
    {"Air Choir", 4, 5, 0.9, 5, 0.2, 0.09, 0},
    {"Synth Brass", 1, 3, 0.08, 4, 0.9, 0, 0},
    {"Deep Kick", 5, 0.18, 0.001, 1.2, 0.1, 0.02, 0},
    {"Snare Snap", 6, 0.09, 0.001, 0.8, 0.5, 0.85, 0},
    {"Closed Hat", 7, 0.035, 0.001, 0.35, 1, 1, 0}
}};
}
juce::StringArray samplerPresetNames()
{
    juce::StringArray names; for (const auto& patch : samplerPatches) names.add(patch.name); return names;
}
juce::StringArray builtinPresetNames(const juce::String& identifier)
{
    if (identifier == "builtin:sampler") return samplerPresetNames();
    static const auto names = []
    {
        std::map<juce::String, juce::StringArray> result;
        for (const auto* id : {"opentone.fm", "opentone.wavetable", "opentone.operator"})
        { auto device = createBuiltinPlugin(id); for (int i = 0; i < device->getNumPrograms(); ++i) result[id].add(device->getProgramName(i)); }
        return result;
    }();
    const auto found = names.find(identifier); return found == names.end() ? juce::StringArray() : found->second;
}
juce::Result writeSamplerPreset(int index, const juce::File& destination)
{
    if (index < 0 || index >= static_cast<int>(samplerPatches.size())) return juce::Result::fail("Unknown sampler preset.");
    const auto& patch = samplerPatches[static_cast<size_t>(index)];
    constexpr double rate = 48000; const auto count = static_cast<int>(patch.length * rate);
    juce::AudioBuffer<float> audio(1, count); juce::Random random(0x4f70656e + index); double lastNoise = 0, filteredNoise = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto t = i / rate, phase = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz(60) * t;
        double tone = 0;
        if (patch.wave == 0) tone = std::sin(phase) + patch.brightness * std::sin(phase * 2) * std::exp(-t * 3);
        if (patch.wave == 1 || patch.wave == 4)
            for (int harmonic = 1; harmonic <= 12; ++harmonic)
                if (patch.wave == 1 || harmonic % 2) tone += std::sin(phase * harmonic) * std::pow(patch.brightness, harmonic - 1) / harmonic * std::exp(-t * (harmonic - 1) * 0.3);
        if (patch.wave == 2) tone = std::sin(phase + patch.modulation * std::sin(phase * (1 + patch.brightness * 6)) * std::exp(-t * 4));
        if (patch.wave == 3) tone = std::sin(phase) + patch.brightness * std::sin(phase * 2.756) * std::exp(-t * 2) + patch.brightness * 0.5 * std::sin(phase * 5.404) * std::exp(-t * 4);
        if (patch.wave == 5) tone = std::sin(juce::MathConstants<double>::twoPi * (65 * t + 100 * 0.025 * (1 - std::exp(-t / 0.025))));
        if (patch.wave == 6) tone = std::sin(juce::MathConstants<double>::twoPi * 180 * t);
        const auto noise = random.nextDouble() * 2 - 1;
        filteredNoise += 0.12 * (noise - filteredNoise);
        const auto texture = patch.wave >= 6 ? (noise - lastNoise) * 0.5 : filteredNoise;
        lastNoise = noise;
        const auto envelope = std::min(1.0, t / patch.attack) * std::exp(-t / patch.decay) * std::min(1.0, (patch.length - t) / 0.1);
        audio.setSample(0, i, static_cast<float>((tone * (1 - patch.noise) + texture * patch.noise) * envelope));
    }
    const auto peak = audio.getMagnitude(0, count); jassert(std::isfinite(peak) && peak > 0); if (peak > 0) audio.applyGain(0.8f / peak);
    const auto directory = destination.getParentDirectory().createDirectory(); if (directory.failed()) return directory;
    juce::TemporaryFile staging(destination); auto stream = staging.getFile().createOutputStream();
    if (!stream) return juce::Result::fail("Could not create factory sample.");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), rate, 1, 24, {}, 0));
    if (!writer) return juce::Result::fail("Could not encode factory sample.");
    stream.release(); const auto written = writer->writeFromAudioSampleBuffer(audio, 0, count); writer.reset();
    if (!written || !staging.overwriteTargetFileWithTemporary()) return juce::Result::fail("Could not write factory sample.");
    return juce::Result::ok();
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
