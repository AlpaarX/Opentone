// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "domain/Project.h"
#include "shared/RuntimeExports.h"
#include "infrastructure/PluginHost.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <bitset>
#include <map>
#include <mutex>
#include <thread>

namespace opentone
{
// Application owns engine and session. Message thread submits immutable snapshots.
// Worker owns readers/resamplers. Callback owns the clock, ring consumption,
// mixing and plug-in processing; host edits use a nonblocking callback lock.
// Shutdown: remove device callback, join preparer, destroy voices, stop read-ahead.
class OPENTONE_API AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;
    juce::String initialise(const juce::XmlElement* settings = nullptr);
    void configureOffline(double rate, int blockSize);
    void submit(const Snapshot&, bool structural = true, bool allowLoop = true);
    void replace(const Snapshot&);
    void play();
    void stop();
    void seek(double seconds);
    void render(float* const* output, int channels, int samples) noexcept;
    bool playing() const { return running.load(); }
    double position() const { return static_cast<double>(playbackFrame(clock.load())) / sampleRate.load(); }
    double rate() const { return sampleRate.load(); }
    uint64_t deviceConfiguration() const { return configuration.load(); }
    juce::Result enableMicrophone();
    juce::Result startAudioRecording(const juce::File&, double start);
    juce::Result finishAudioRecording();
    bool recordingFailed() const { return recordFailed.load(); }
    juce::int64 recordedFrames() const { return recordFrames.load(); }
    double recordedStart() const { return static_cast<double>(recordFirstFrame.load()) / recordRate.load(); }
    std::pair<float, float> inputLevels() const { return { inputPeak.load(), inputPeak.load() }; }
    uint64_t appliedRevision() const { return acknowledged.load(); }
    uint64_t requestedRevision() const { return requested.load(); }
    uint64_t underrunCount() const { return underruns.load(); }
    bool clipped() const { return clipping.load(); }
    std::pair<float, float> masterLevels() const { return { outputPeakLeft.load(), outputPeakRight.load() }; }
    void setMaster(float gain) { if (std::isfinite(gain) && gain >= 0 && gain <= 2) masterTarget.store(gain); }
    float master() const noexcept { return masterTarget.load(); }
    void setMetronome(bool enabled) { metronomeEnabled.store(enabled); }
    bool metronome() const noexcept { return metronomeEnabled.load(); }
    bool previewNote(const Track&, const Asset&, const juce::File&, int note, double velocity, bool held, bool replace = true);
    void releaseEditPreviews(const juce::String&);
    void releasePreview(const juce::String& track, int note);
    std::bitset<128> activeNotes(const juce::String& track) const;
    juce::String error() const;
    static bool isRendering() noexcept { return rendering; }
#if defined(OPENTONE_TESTING)
    std::atomic<bool> pauseStreaming { false }, pausePreparation { false }, pauseCleanup { false };
    std::atomic<size_t> ownedArrangements { 0 };
#endif
    PluginHost plugins;
    juce::AudioDeviceManager devices;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override;
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;
private:
    static constexpr int chunk = 256, ringSize = 64, retireSize = 16;
    static thread_local bool rendering;
    struct Control
    {
        std::shared_ptr<PluginHost::Bus> bus;
        std::atomic<float> target { 1 }, panTarget {0}; float current = 1;
        std::atomic<float> samplerTarget { 1 };
        std::atomic<uint64_t> notesLow { 0 }, notesHigh { 0 };
        uint64_t localLow = 0, localHigh = 0;
        void clearNotes() { localLow = localHigh = 0; }
        void markNote(int note) { if (note < 64) localLow |= uint64_t(1) << note; else localHigh |= uint64_t(1) << (note - 64); }
        void publishNotes() { notesLow.store(localLow); notesHigh.store(localHigh); }
    };
    struct Voice;
    struct Arrangement;
    struct PreviewSlot
    {
        // Worker owns allocation; callback returns ownership by publishing state 4.
        std::atomic<int> state { 0 };
        std::atomic<bool> release { false };
        std::atomic<juce::int64> position { 0 };
        std::unique_ptr<Voice> voice;
        std::shared_ptr<Control> control;
        juce::String track; int note = 0; bool held = false;
        int releaseLeft = 0; float level = 0;
    };
    struct PreviewRequest { size_t slot; Track track; Asset asset; juce::File file; int note; double velocity; bool held; std::shared_ptr<Control> control; };
    struct Request
    {
        Snapshot snapshot;
        std::map<juce::String, std::shared_ptr<Control>> controls;
        uint64_t configuration = 0, seekSerial = 0;
        juce::int64 seekFrame = 0;
        bool seek = false;
    };
    // Coalesced mailbox consumed by the preparation future and streaming worker.
    struct PlaybackPreparer
    {
        std::mutex mutex;
        std::optional<Request> latest;
        Request desired;
        std::vector<PreviewRequest> previews;
    };
    void workerLoop();
    std::unique_ptr<Arrangement> prepare(const Request&, Arrangement* base);
    void resetResources();
    void startWorker();
    void retire(Arrangement*) noexcept;
    bool retirementAvailable() const noexcept;
    void setError(juce::String);
    bool pluginBlock = false;
    juce::int64 playbackFrame(juce::int64 frame) const noexcept
    { if (!looping.load()) return frame; const auto start = static_cast<juce::int64>(std::llround(loopStart.load() * sampleRate.load()));
      const auto end = static_cast<juce::int64>(std::llround(loopEnd.load() * sampleRate.load())); return frame >= end && end > start ? start + (frame - start) % (end - start) : frame; }
    std::atomic<bool> looping {false}; std::atomic<double> loopStart {0}, loopEnd {2};
    void renderBlock(float* const*, int, int) noexcept;
    void renderPlayback(float* const*, int, int) noexcept;
    void renderPreviews(float* const*, int, int) noexcept;
    std::atomic<bool> exiting { false }, running { false }, seeking { false }, fadeFinished { true };
    std::atomic<bool> resumeAfterSeek { false };
    std::atomic<double> sampleRate { 48000 };
    std::atomic<int> quantum { 256 };
    std::atomic<juce::int64> clock { 0 };
    std::atomic<uint64_t> configuration { 1 }, acknowledged { 0 }, requested { 0 }, underruns { 0 }, seekSerial { 0 };
    std::atomic<bool> clipping { false };
    std::atomic<float> outputPeakLeft {0}, outputPeakRight {0};
    std::atomic<float> masterTarget { 1 }, masterPanTarget {0};
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<double> tempo { 120 };
    float masterCurrent = 1;
    std::atomic<Arrangement*> pending { nullptr }, activeView { nullptr };
    Arrangement* active = nullptr;
    Arrangement* outgoing = nullptr;
    int transitionLeft = 0, seekFadeLeft = 0;
    std::array<Arrangement*, retireSize> retired {};
    std::atomic<unsigned> retireRead { 0 }, retireWrite { 0 };
    PlaybackPreparer preparer;
    std::map<juce::String, std::shared_ptr<Control>> controls;
    mutable std::mutex errorMutex;
    juce::String lastError;
    std::thread worker;
    juce::TimeSliceThread readAhead { "Audio read-ahead" };
    juce::TimeSliceThread recordThread { "Audio recording" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recordWriter;
    juce::int64 recordStart = 0;
    std::atomic<bool> recordFailed {false};
    std::atomic<juce::int64> recordFrames {0};
    std::atomic<juce::int64> recordFirstFrame {0};
    std::atomic<double> recordRate {48000};
    int inputLatency = 0;
    std::atomic<float> inputPeak {0};
    std::vector<std::unique_ptr<Arrangement>> owned;
    std::array<PreviewSlot, 16> previews;
};
}
