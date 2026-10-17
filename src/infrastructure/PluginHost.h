// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "domain/Project.h"
#include "shared/RuntimeExports.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <bitset>
#include <atomic>
#include <map>

namespace opentone
{
class OPENTONE_API PluginHost final : public juce::ChangeBroadcaster, private juce::ChangeListener,
                                      private juce::AudioProcessorListener, private juce::AudioPlayHead
{
public:
    struct Slot
    {
        Plugin model;
        std::unique_ptr<juce::AudioProcessor> instance;
        juce::String error, capturedState, track;
        bool loading = true, builtin = false;
    };
    struct Bus
    {
        struct Preview { bool on = false, held = false; juce::int64 end = 0; float velocity = 0.8f; };
        juce::AudioBuffer<float> audio, feedback, postFx;
        juce::MidiBuffer midi, queued;
        std::vector<std::shared_ptr<Slot>> slots;
        std::array<Preview, 128> previews {};
        std::atomic<uint64_t> low {0}, high {0};
        std::atomic<float> peakLeft {0}, peakRight {0};
        float target = 1, previousGain = 1, baseGain = 1, pan = 0, basePan = 0, previousPan = 0;
        bool group = false, muted = false, inputEnabled = false, outputEnabled = true, armed = false; int monitor = 2;
        std::vector<AutomationPoint> volumeAutomation, panAutomation, muteAutomation;
        std::vector<std::vector<AutomationPoint>> sendAutomation;
        std::shared_ptr<Bus> parent;
        int returnIndex = -1; std::vector<float> sends {0, 0}, previousSends {0, 0};
        bool chase = true;
    };
    PluginHost();
    ~PluginHost() override;
    void sync(const Project&, uint64_t generation, bool allowLoop = true);
    void prepare(double rate, int blockSize);
    std::shared_ptr<Bus> bus(const juce::String&) const;
    void beginBlock(juce::int64 frame, double bpm, bool playing, int samples);
    void setArmed(const juce::StringArray&);
    void monitorInput(const float* const*, int channels, int offset, int samples);
    void transport(juce::int64 frame, double bpm, bool playing);
    void finishBlock(float* const*, int channels, int samples, float master, float masterPan = 0);
    bool preview(const juce::String&, int pitch, double velocity, bool held, bool replace);
    void liveMidi(const juce::String&, const juce::MidiMessage&);
    std::shared_ptr<Slot> editorSlot(const juce::String&) const;
    void release(const juce::String&, int pitch, bool editsOnly = false);
    std::bitset<128> activeNotes(const juce::String&) const;
    std::pair<float, float> levels(const juce::String&) const;
    std::map<juce::String, juce::String> captureStates(bool force = false);
    juce::Array<juce::PluginDescription> availablePlugins() const;
    void assignCategory(const juce::String& identifier, bool instrument);
    void showEditor(const juce::String&);
    juce::String focusedEditorTrack(const juce::Component* source = nullptr) const;
    void updateEditorWindowLevels();
    juce::KeyListener* keyboardListener = nullptr;
    juce::String status(const juce::String&) const;
    std::unique_ptr<juce::Component> scanner();
    static bool scanChild(const juce::String& commandLine);
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::CriticalSection mutex;
    int capacity() const { return block; }
private:
    class Window;
    void load(const std::shared_ptr<Slot>&);
    void prepareSlot(Slot&);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { stateDirty.store(true); }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override { stateDirty.store(true); }
    juce::Optional<PositionInfo> getPosition() const override;
    std::map<juce::String, std::shared_ptr<Bus>> buses;
    std::map<juce::String, std::shared_ptr<Slot>> slots;
    std::map<juce::String, std::unique_ptr<Window>> windows;
    std::unique_ptr<juce::PropertiesFile> properties;
    std::map<juce::String, bool> categories;
    std::atomic<bool> stateDirty {false};
    uint64_t projectGeneration = 0;
    double rate = 48000, bpm = 120;
    int block = 256, numerator = 4, denominator = 4;
    juce::int64 frame = 0, expectedFrame = -1, previewClock = 0;
    std::vector<std::shared_ptr<Bus>> returnBuses; bool soloReturn = false;
    std::vector<std::shared_ptr<Bus>> processingOrder;
    bool playing = false, wasPlaying = false, looping = false; double loopStart = 0, loopEnd = 2;
    JUCE_DECLARE_WEAK_REFERENCEABLE(PluginHost)
};
}
