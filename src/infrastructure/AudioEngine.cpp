// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/AudioEngine.h"
#include "domain/TimelineMath.h"
#include <chrono>
#include <future>
#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>

namespace opentone
{
class AudioEngine::LinkState
{
public:
    ableton::Link link {120};
    ableton::link::HostTimeFilter<ableton::Link::Clock> hostTimeFilter;
    std::atomic<double> requestedTempo {0}, quantum {4};
    std::atomic<int> transportRequest {0};
    std::atomic<bool> startStop {false}, wantedPlaying {false}, waiting {false}, resetFilter {false};
    std::chrono::microseconds hostTime {0}, startTime {0};
    double sampleCounter = 0, blockBeat = -1;
    int outputLatency = 0;
};
thread_local bool AudioEngine::rendering = false;
namespace
{
double playedVelocity(const MidiNote& note, juce::uint32 seed, uint64_t occurrence)
{
    if (!note.enabled || note.probability <= 0) return 0;
    if (note.probability >= 1 && std::abs(note.velocityDeviation) < 1e-12) return note.velocity;
    auto hash = [](juce::uint32 x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); };
    const auto random = hash(seed + static_cast<juce::uint32>(occurrence) * 0x9e3779b9u);
    if (static_cast<double>(random) / 4294967296.0 >= note.probability) return 0;
    const auto velocity = juce::jlimit(0.0, 1.0, note.velocity + note.velocityDeviation * static_cast<double>(hash(random ^ 0x85ebca6bu)) / 4294967295.0);
    jassert(std::isfinite(velocity) && velocity >= 0 && velocity <= 1); return velocity;
}
}

struct AudioEngine::Voice
{
    struct Block { juce::int64 frame = 0; std::array<std::array<float, chunk>, 2> data {}; bool failed = false; };
    struct Source : juce::AudioSource
    {
        explicit Source(juce::AudioFormatReader* input) : reader(input) {}
        void prepareToPlay(int, double) override {}
        void releaseResources() override {}
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
        {
            jassert(!AudioEngine::isRendering());
            if (!reader->read(info.buffer, info.startSample, info.numSamples, position, true, true)) failed = true;
            position += info.numSamples;
        }
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::int64 position = 0;
        bool failed = false;
    };
    Voice(const Clip& clipIn, const Asset& asset, const juce::File& file, double rateIn,
          juce::AudioFormatReader* reader, double pitchIn = 1, double gainIn = 1, bool midiIn = false, double repeat = 0, double gateFrom = -1, double gateTo = -1, juce::AudioFormatReader* secondReader = nullptr, double globalStart = 0, double globalEnd = 0, const MidiNote* expressionIn = nullptr)
        : clip(clipIn), path(file), rate(rateIn), pitch(pitchIn), gain(gainIn), midi(midiIn), source(reader), resampler(&source, false, 2), scratch(2, chunk)
    {
        if (expressionIn) { expression = *expressionIn; hasExpression = true; }
        start = static_cast<juce::int64>(std::llround(clip.start * rate));
        noteLength = std::max<juce::int64>(1, static_cast<juce::int64>(std::llround(clip.duration * rate)));
        period = repeat > 0 ? std::max<juce::int64>(1, static_cast<juce::int64>(std::llround(repeat * rate))) : 0;
        windowStart = gateFrom >= 0 ? static_cast<juce::int64>(std::llround(gateFrom * rate)) : start;
        end = static_cast<juce::int64>(std::llround((gateTo >= 0 ? gateTo : clip.start + clip.duration) * rate));
        sourceRate = asset.sampleRate;
        resampler.setResamplingRatio(sourceRate * pitch / rate);
        resampler.prepareToPlay(chunk, rate);
        if (secondReader)
        {
            loopStart = static_cast<juce::int64>(std::llround(globalStart * rate)); loopEnd = static_cast<juce::int64>(std::llround(globalEnd * rate));
            alternate = std::make_unique<Source>(secondReader);
            alternateResampler = std::make_unique<juce::ResamplingAudioSource>(alternate.get(), false, 2);
            alternateResampler->setResamplingRatio(sourceRate * pitch / rate); alternateResampler->prepareToPlay(chunk, rate);
        }
    }
    bool matches(const Clip& value, const juce::File& file, double pitchIn, double gainIn, bool midiIn, double repeat, double gateFrom, double gateTo, double globalStart, double globalEnd, const MidiNote* expressionIn) const
    { return loopStart == static_cast<juce::int64>(std::llround(globalStart * rate)) && loopEnd == static_cast<juce::int64>(std::llround(globalEnd * rate)) && period == (repeat > 0 ? std::max<juce::int64>(1, static_cast<juce::int64>(std::llround(repeat * rate))) : 0)
          && windowStart == (gateFrom >= 0 ? static_cast<juce::int64>(std::llround(gateFrom * rate)) : start)
          && end == static_cast<juce::int64>(std::llround((gateTo >= 0 ? gateTo : value.start + value.duration) * rate))
          && hasExpression == (expressionIn != nullptr) && (!hasExpression || expression == *expressionIn)
          && clip.id == value.id && clip.track == value.track && clip.asset == value.asset && path == file
          && std::tie(clip.start, clip.offset, clip.duration, clip.gain, clip.fadeIn, clip.fadeOut, pitch, gain, midi)
               == std::tie(value.start, value.offset, value.duration, value.gain, value.fadeIn, value.fadeOut, pitchIn, gainIn, midiIn); }
    juce::int64 onsetAt(juce::int64 frame) const
    { return period > 0 ? start + static_cast<juce::int64>(std::floor(static_cast<double>(frame - start) / static_cast<double>(period))) * period : start; }
    void initialise(juce::int64 frame)
    { readIndex.store(0); writeIndex.store(0); nextFrame = frame; sourceOutput = alternateOutput = -1;
      sourceOccurrence = alternateOccurrence = std::numeric_limits<juce::int64>::min(); }
    void fill(juce::int64 current, int targetBlocks = ringSize - 1)
    {
        // SPSC slots are never overwritten until the callback has released them.
        for (int count = 0; count < targetBlocks; ++count)
        {
            auto write = writeIndex.load(std::memory_order_relaxed);
            if (write - readIndex.load(std::memory_order_acquire) >= static_cast<unsigned>(targetBlocks)) break;
            if (nextFrame < current) nextFrame = (current / chunk) * chunk;
            auto& block = blocks[write % ringSize]; block.frame = nextFrame; block.failed = false;
            for (auto& channel : block.data) channel.fill(0);
            bool failed = false; auto actual = nextFrame;
            while (actual < nextFrame + chunk)
            {
                const auto timeline = mappedFrame(actual);
                auto count = static_cast<int>(nextFrame + chunk - actual);
                if (loopEnd > loopStart) count = static_cast<int>(std::min<juce::int64>(count, loopEnd - timeline));
                const bool useAlternate = alternate && actual >= loopStart && ((actual - loopStart) / (loopEnd - loopStart)) % 2 != 0;
                auto& input = useAlternate ? *alternate : source;
                auto& converter = useAlternate ? *alternateResampler : resampler;
                auto& outputFrame = useAlternate ? alternateOutput : sourceOutput;
                auto& occurrence = useAlternate ? alternateOccurrence : sourceOccurrence;
                auto cursor = std::max(windowStart, timeline), limit = std::min(end, timeline + count);
                while (cursor < limit)
                {
                    const auto onset = onsetAt(cursor), from = std::max(cursor, onset), to = std::min(limit, onset + noteLength);
                    if (to <= from) { if (period <= 0) break; cursor = onset + period; continue; }
                    if (outputFrame != from || occurrence != onset)
                    { input.position = static_cast<juce::int64>(std::llround((clip.offset + static_cast<double>(from - onset) / rate * pitch) * sourceRate));
                      converter.flushBuffers(); occurrence = onset; }
                    input.failed = false; const auto length = static_cast<int>(to - from);
                    converter.getNextAudioBlock({ &scratch, 0, length });
                    if (hasExpression) scratch.applyGain(0, length, static_cast<float>(expressionVelocity(onset, actual)));
                    for (int channel = 0; channel < 2; ++channel)
                        std::copy_n(scratch.getReadPointer(channel), length, block.data[static_cast<size_t>(channel)].data() + actual - nextFrame + from - timeline);
                    if (input.failed) { outputFrame = -1; converter.flushBuffers(); failed = true; break; }
                    outputFrame = to; cursor = to;
                }
                if (failed) break;
                actual += count;
            }
            if (failed) break;
            nextFrame += chunk;
            writeIndex.store(write + 1, std::memory_order_release);
        }
    }
    bool ready(int targetBlocks = 8) const { return writeIndex.load(std::memory_order_acquire) - readIndex.load() >= static_cast<unsigned>(targetBlocks); }
    bool sample(juce::int64 frame, float& left, float& right) noexcept
    {
        auto read = readIndex.load(std::memory_order_relaxed);
        const auto write = writeIndex.load(std::memory_order_acquire);
        while (read != write && blocks[read % ringSize].frame + chunk <= frame) ++read;
        readIndex.store(read, std::memory_order_release);
        const auto timeline = mappedFrame(frame), onset = onsetAt(timeline);
        if (timeline < windowStart || timeline < onset || timeline >= end || timeline >= onset + noteLength) { left = right = 0; return true; }
        if (read == write || blocks[read % ringSize].frame > frame) { left = right = 0; return false; }
        const auto& block = blocks[read % ringSize];
        const auto offset = static_cast<size_t>(frame - block.frame);
        auto level = gain;
        if (midi) level *= std::min(1.0, static_cast<double>(std::min(timeline - std::max(windowStart, onset), std::min(end, onset + noteLength) - timeline)) / (rate * 0.003));
        if (!midi)
        { const auto elapsed = static_cast<double>(timeline - onset) / rate;
          if (clip.fadeIn > 0) level *= juce::jlimit(0.0, 1.0, elapsed / clip.fadeIn);
          if (clip.fadeOut > 0) level *= juce::jlimit(0.0, 1.0, (clip.duration - elapsed) / clip.fadeOut); }
        left = block.data[0][offset] * static_cast<float>(level); right = block.data[1][offset] * static_cast<float>(level); return !block.failed;
    }
    double expressionVelocity(juce::int64 onset, juce::int64 actual) const
    { const auto cycle = period > 0 ? static_cast<uint64_t>((onset - start) / period) : 0;
      const auto round = loopEnd > loopStart && actual >= loopStart ? static_cast<uint64_t>((actual - loopStart) / (loopEnd - loopStart)) : 0;
      return hasExpression ? playedVelocity(expression, static_cast<juce::uint32>(clip.id.hashCode()), cycle + round * 1048576) : 1; }
    MidiNote expression {}; bool hasExpression = false;
    juce::int64 mappedFrame(juce::int64 frame) const
    { return loopEnd > loopStart && frame >= loopEnd ? loopStart + (frame - loopStart) % (loopEnd - loopStart) : frame; }
    juce::int64 loopStart = 0, loopEnd = 0, alternateOutput = -1, alternateOccurrence = 0;
    std::unique_ptr<Source> alternate;
    std::unique_ptr<juce::ResamplingAudioSource> alternateResampler;
    Clip clip; juce::File path;
    double rate, pitch = 1, gain = 1; bool midi = false; double sourceRate = 0;
    juce::int64 start = 0, end = 0, windowStart = 0, noteLength = 0, period = 0, nextFrame = 0, sourceOutput = -1, sourceOccurrence = 0;
    Source source;
    juce::ResamplingAudioSource resampler;
    juce::AudioBuffer<float> scratch;
    std::array<Block, ringSize> blocks;
    std::atomic<unsigned> readIndex { 0 }, writeIndex { 0 };
};
struct AudioEngine::Arrangement
{
    struct Note
    {
        std::shared_ptr<Control> control; int pitch;
        juce::int64 start, length, period, windowStart, windowEnd; float velocity; MidiNote expression {}; juce::uint32 seed = 0; double expressionOffset = 0;
        juce::int64 onsetAt(juce::int64 frame) const
        { return period > 0 ? start + static_cast<juce::int64>(std::floor(static_cast<double>(frame - start) / static_cast<double>(period))) * period : start; }
        double velocityAt(juce::int64 onset, uint64_t round) const
        { return playedVelocity(expression, seed, (period > 0 ? static_cast<uint64_t>((onset - start) / period) : 0) + round * 1048576); }
        bool contains(juce::int64 frame, uint64_t round = 0) const
        { const auto onset = onsetAt(frame); return frame >= windowStart && frame < windowEnd && frame >= onset && frame < onset + length && velocityAt(onset, round) > 0; }
    };
    std::vector<Note> notes;
    struct Entry { std::shared_ptr<Voice> voice; std::shared_ptr<Control> control; bool reused = false; };
    std::vector<Entry> voices;
    std::vector<std::shared_ptr<Control>> controls;
    uint64_t generation = 0, revision = 0, baseRevision = 0, configuration = 0, seekSerial = 0;
    juce::int64 activation = 0;
    bool seek = false;
};
AudioEngine::AudioEngine() : linkState(std::make_unique<LinkState>()) { startWorker(); }
AudioEngine::~AudioEngine()
{
    devices.removeAudioCallback(this);
    finishAudioRecording(); recordThread.stopThread(-1);
    resetResources();
}
void AudioEngine::startWorker() { exiting.store(false); worker = std::thread([this] { workerLoop(); }); }
void AudioEngine::resetResources()
{
    running.store(false); exiting.store(true);
    if (worker.joinable()) worker.join();
    for (auto& preview : previews) { preview.voice.reset(); preview.control.reset(); preview.state.store(0); }
    { std::lock_guard<std::mutex> lock(preparer.mutex); preparer.previews.clear(); }
    active = outgoing = nullptr; activeView.store(nullptr); pending.store(nullptr);
    owned.clear(); retireRead.store(0); retireWrite.store(0);
    transitionLeft = seekFadeLeft = 0; seeking.store(false); fadeFinished.store(true);
}
juce::String AudioEngine::initialise(const juce::XmlElement* settings)
{
    const auto result = devices.initialise(0, 2, settings, true);
    devices.addAudioCallback(this); if (result.isNotEmpty()) setError(result); return result;
}
void AudioEngine::configureOffline(double rateIn, int blockSize)
{
    if (!std::isfinite(rateIn) || rateIn <= 0 || rateIn > 1000000)
    { setError("Unsupported output sample rate."); return; }
    resetResources(); plugins.prepare(rateIn, blockSize); sampleRate.store(rateIn); quantum.store(std::max(1, blockSize)); ++configuration; clock.store(0); acknowledged.store(0);
    { std::lock_guard<std::mutex> lock(preparer.mutex); preparer.desired.configuration = configuration.load(); preparer.latest = preparer.desired; }
    startWorker();
}
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{ setError({}); linkState->outputLatency = std::max(0, device->getOutputLatencyInSamples()); linkState->hostTimeFilter.reset(); linkState->sampleCounter = 0; inputLatency = std::max(0, device->getInputLatencyInSamples()); configureOffline(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()); }
void AudioEngine::audioDeviceStopped() { if (recordWriter) recordFailed.store(true); resetResources(); }
void AudioEngine::audioDeviceError(const juce::String& message) { recordFailed.store(true); setError(message); running.store(false); }
void AudioEngine::setError(juce::String message) { std::lock_guard<std::mutex> lock(errorMutex); lastError = std::move(message); }
juce::String AudioEngine::error() const { std::lock_guard<std::mutex> lock(errorMutex); return lastError; }
void AudioEngine::submit(const Snapshot& snapshot, bool structural, bool allowLoop)
{
    const auto previousPosition = position(), previousEnd = loopEnd.load(); const auto wasLooping = looping.load();
    const auto nextLooping = snapshot.project.loopEnabled && allowLoop;
    const bool changedLoop = nextLooping != wasLooping || !std::equal_to<double>()(loopStart.load(), snapshot.project.loopStart) || !std::equal_to<double>()(previousEnd, snapshot.project.loopEnd);
    const bool rebase = (!wasLooping && nextLooping && previousPosition >= snapshot.project.loopEnd)
        || (wasLooping && changedLoop && static_cast<double>(clock.load()) / sampleRate.load() >= previousEnd);
    plugins.sync(snapshot.project, snapshot.generation, allowLoop);
    loopStart.store(snapshot.project.loopStart); loopEnd.store(snapshot.project.loopEnd); looping.store(snapshot.project.loopEnabled && allowLoop);
    cueTarget.store(static_cast<float>(snapshot.project.cueGain)); tempo.store(snapshot.project.bpm); numerator.store(snapshot.project.numerator); denominator.store(snapshot.project.denominator); masterTarget.store(static_cast<float>(snapshot.project.master.gain)); masterPanTarget.store(static_cast<float>(snapshot.project.master.pan));
    Request request; request.snapshot = snapshot; request.snapshot.project.loopEnabled = snapshot.project.loopEnabled && allowLoop; request.configuration = configuration.load();
    const bool anySolo = std::any_of(snapshot.project.tracks.begin(), snapshot.project.tracks.end(), [](const auto& track) { return track.solo; });
    for (const auto& track : snapshot.project.tracks)
    {
        auto& control = controls[track.id];
        if (!control) { control = std::make_shared<Control>(); control->bus = plugins.bus(track.id); control->current = (track.mute || (anySolo && !track.solo)) ? 0.0f : static_cast<float>(track.gain); }
        control->panTarget.store(static_cast<float>(track.pan));
        control->target.store((!track.outputEnabled || outputDestination(track).isNotEmpty() || track.mute || (anySolo && !track.solo)) ? 0.0f : static_cast<float>(track.gain));
        control->samplerEnd.store(track.sampleOffset + track.sampleDuration);
        if (track.sampler) control->samplerTarget.store(track.samplerBypass ? 0.0f : static_cast<float>(track.samplerGain));
        request.controls[track.id] = control;
    }
    for (auto it = controls.begin(); it != controls.end();)
        if (!findId(snapshot.project.tracks, it->first)) it = controls.erase(it); else ++it;
    {
    std::lock_guard<std::mutex> lock(preparer.mutex);
    if (seeking.load())
    { request.seek = true; request.seekSerial = seekSerial.load(); request.seekFrame = preparer.desired.seekFrame; }
    preparer.desired = request;
    if (structural || activeView.load() == nullptr || request.seek)
    { requested.store(snapshot.revision); preparer.latest = request; }
    // Gain/BPM revisions leave readers intact; structural base follows engine acknowledgement.
    }
    if (rebase) seek(nextLooping && previousPosition >= snapshot.project.loopEnd ? snapshot.project.loopStart : previousPosition);
}
void AudioEngine::replace(const Snapshot& snapshot)
{
    devices.removeAudioCallback(this); resetResources(); controls.clear();
    clock.store(0); acknowledged.store(0); ++configuration;
    { std::lock_guard<std::mutex> lock(preparer.mutex); preparer.latest.reset(); preparer.desired = {}; }
    startWorker(); submit(snapshot);
    if (devices.getCurrentAudioDevice()) devices.addAudioCallback(this);
}
bool AudioEngine::playing() const { return running.load() || linkState->waiting.load(); }
bool AudioEngine::linkEnabled() const { return linkState->link.isEnabled(); }
size_t AudioEngine::linkPeers() const { return linkState->link.numPeers(); }
double AudioEngine::linkTempo() const { return linkState->link.captureAppSessionState().tempo(); }
void AudioEngine::requestLinkTempo(double bpm)
{
    if (!std::isfinite(bpm) || bpm <= 0 || bpm > 999) return;
    if (!devices.getCurrentAudioDevice()) { auto state = linkState->link.captureAppSessionState(); state.setTempo(bpm, linkState->link.clock().micros()); linkState->link.commitAppSessionState(state); }
    else linkState->requestedTempo.store(bpm);
}
void AudioEngine::enableLink(bool enabled)
{
    linkState->resetFilter.store(true); linkState->link.enable(enabled);
    if (enabled && linkPeers() == 0) requestLinkTempo(tempo.load());
    if (!enabled) { linkState->waiting.store(false); linkState->transportRequest.store(0); if (linkState->wantedPlaying.load()) play(); }
}
void AudioEngine::setLinkQuantum(double value)
{ if (std::isfinite(value) && value > 0 && value <= 1584) linkState->quantum.store(value); }
double AudioEngine::linkQuantum() const { return linkState->quantum.load(); }
void AudioEngine::setLinkStartStopSync(bool enabled)
{ linkState->startStop.store(enabled); linkState->link.enableStartStopSync(enabled); }
bool AudioEngine::linkStartStopSync() const { return linkState->startStop.load(); }
int AudioEngine::linkRemoteTransport()
{
    if (!linkEnabled() || !linkStartStopSync() || linkState->transportRequest.load() != 0) return 0;
    const auto playing = linkState->link.captureAppSessionState().isPlaying();
    return playing == linkState->wantedPlaying.load() ? 0 : playing ? 1 : -1;
}
void AudioEngine::play(bool broadcast)
{
    resumeAfterSeek.store(true); linkState->wantedPlaying.store(true);
    if (linkEnabled()) { linkState->transportRequest.store(broadcast ? 1 : 2); linkState->waiting.store(true); }
    else if (!seeking.load()) running.store(true);
}
void AudioEngine::stop(bool broadcast)
{
    linkState->wantedPlaying.store(false); linkState->waiting.store(false);
    if (linkEnabled() && broadcast) linkState->transportRequest.store(-1);
    resumeAfterSeek.store(false); running.store(false); if (seeking.load()) fadeFinished.store(true);
}
int AudioEngine::syncLink(int samples) noexcept
{
    linkState->blockBeat = -1;
    if (!linkEnabled()) return 0;
    auto state = linkState->link.captureAudioSessionState();
    const auto hostTime = linkState->hostTime + std::chrono::microseconds(std::llround((liveInputOffset + linkState->outputLatency) * 1.0e6 / sampleRate.load()));
    const auto bpm = linkState->requestedTempo.exchange(0);
    if (bpm > 0) state.setTempo(bpm, hostTime);
    if (seeking.load() && linkState->transportRequest.load() > 0) { running.store(false); return samples; }
    const auto action = linkState->transportRequest.exchange(0);
    const auto quantum = linkQuantum();
    if (action > 0)
    {
        const auto beat = position() * state.tempo() / 60;
        if (action == 1) state.setIsPlayingAndRequestBeatAtTime(true, hostTime, beat, quantum);
        else state.requestBeatAtStartPlayingTime(beat, quantum);
        linkState->startTime = state.timeAtBeat(beat, quantum);
    }
    else if (action < 0) state.setIsPlaying(false, hostTime);
    if (action != 0 || bpm > 0) linkState->link.commitAudioSessionState(state);
    tempo.store(state.tempo()); linkState->blockBeat = state.beatAtTime(hostTime, quantum);
    if (linkState->waiting.load())
    {
        if (seeking.load()) { running.store(false); return samples; }
        const auto offset = static_cast<int>(juce::jlimit(0.0, static_cast<double>(samples),
            std::ceil(static_cast<double>((linkState->startTime - hostTime).count()) * sampleRate.load() / 1.0e6)));
        if (offset < samples) { running.store(true); linkState->waiting.store(false); }
        else running.store(false);
        return offset;
    }
    return 0;
}
bool AudioEngine::previewNote(const Track& track, const Asset& asset, const juce::File& file, int note, double velocity, bool held, bool replace)
{
    if (!track.midi || !track.sampler || track.samplerBypass || asset.missing || note < 0 || note > 127
        || !std::isfinite(velocity) || velocity <= 0 || velocity > 1 || !std::isfinite(track.sampleDuration)
        || track.sampleDuration <= 0 || !std::isfinite(track.sampleOffset) || track.sampleOffset < 0
        || track.rootNote < 0 || track.rootNote > 127) return false;
    if (!held && replace) releaseEditPreviews(track.id);
    for (size_t index = 0; index < previews.size(); ++index)
    {
        auto& slot = previews[index]; int idle = 0;
        if (!slot.state.compare_exchange_strong(idle, 1)) continue;
        slot.track = track.id; slot.note = note; slot.held = held; slot.release.store(false); slot.position.store(0);
        auto& control = controls[track.id];
        if (!control) { control = std::make_shared<Control>(); control->bus = plugins.bus(track.id); control->target.store(track.mute ? 0.0f : static_cast<float>(track.gain));
          control->samplerTarget.store(track.samplerBypass ? 0.0f : static_cast<float>(track.samplerGain)); }
        std::lock_guard<std::mutex> lock(preparer.mutex);
        control->samplerEnd.store(track.sampleOffset + track.sampleDuration);
        preparer.previews.push_back({ index, track, asset, file, note, velocity, held, control }); return true;
    }
    return false;
}
void AudioEngine::releaseEditPreviews(const juce::String& track)
{ plugins.release(track, -1, true); for (auto& slot : previews) if (slot.state.load() != 0 && slot.track == track && !slot.held) slot.release.store(true); }
void AudioEngine::releasePreview(const juce::String& track, int note)
{ plugins.release(track, note); for (auto& slot : previews) if (slot.state.load() != 0 && slot.track == track && (note < 0 || slot.note == note)) slot.release.store(true); }
double AudioEngine::samplerPosition(const juce::String& id) const
{ const auto found = controls.find(id); return found == controls.end() ? -1 : found->second->samplerCursor.load(); }
std::bitset<128> AudioEngine::activeNotes(const juce::String& track) const
{
    bool audible = running.load();
    if (!audible) for (const auto& slot : previews)
        if (slot.state.load() >= 1 && slot.state.load() <= 3 && slot.track == track && !slot.release.load()) audible = true;
    const auto found = controls.find(track); if (!audible || found == controls.end()) return plugins.activeNotes(track);
    return std::bitset<128>(found->second->notesLow.load()) | (std::bitset<128>(found->second->notesHigh.load()) << 64) | plugins.activeNotes(track);
}
void AudioEngine::seek(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0
        || seconds > static_cast<double>(std::numeric_limits<juce::int64>::max()) / sampleRate.load() - 1) return;
    const auto frame = static_cast<juce::int64>(std::llround(seconds * sampleRate.load()));
    std::lock_guard<std::mutex> lock(preparer.mutex);
    if (seeking.load() && preparer.desired.seekFrame == frame) return;
    // Keep the prepared audio when Play requests the current stopped position.
    if (!running.load() && !seeking.load() && clock.load() == frame
        && activeView.load() != nullptr && requested.load() == acknowledged.load()) return;
    const bool resume = running.load();
    resumeAfterSeek.store(resume); fadeFinished.store(!resume || activeView.load() == nullptr); seeking.store(true);
    auto request = preparer.desired; request.seek = true;
    request.seekSerial = ++seekSerial;
    request.seekFrame = frame;
    requested.store(request.snapshot.revision);
    preparer.desired = request; preparer.latest = request;
}
bool AudioEngine::retirementAvailable() const noexcept
{ return retireWrite.load(std::memory_order_relaxed) - retireRead.load(std::memory_order_acquire) < retireSize - 1; }
void AudioEngine::retire(Arrangement* arrangement) noexcept
{
    if (!arrangement) return;
    const auto write = retireWrite.load(std::memory_order_relaxed);
    retired[write % retireSize] = arrangement;
    retireWrite.store(write + 1, std::memory_order_release);
}
std::unique_ptr<AudioEngine::Arrangement> AudioEngine::prepare(const Request& request, Arrangement* base)
{
#if defined(OPENTONE_TESTING)
    while (pausePreparation.load() && !exiting.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->generation = request.snapshot.generation; arrangement->revision = request.snapshot.revision;
    arrangement->baseRevision = base ? base->revision : 0; arrangement->configuration = request.configuration;
    arrangement->seek = request.seek; arrangement->seekSerial = request.seekSerial;
    const auto rateIn = sampleRate.load();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    for (const auto& pair : request.controls) arrangement->controls.push_back(pair.second);
    const auto globalStart = request.snapshot.project.loopEnabled ? request.snapshot.project.loopStart : 0;
    const auto globalEnd = request.snapshot.project.loopEnabled ? request.snapshot.project.loopEnd : 0;
    const auto addVoice = [&](const Clip& clip, const Asset& asset, double pitch, double gain, bool midi, double repeat = 0, double gateFrom = -1, double gateTo = -1, const MidiNote* expression = nullptr)
    {
        if (asset.missing) return;
        const auto file = request.snapshot.directory.getChildFile(asset.path);
        auto control = request.controls.find(clip.track); if (control == request.controls.end()) return;
        std::shared_ptr<Voice> voice;
        if (!request.seek && base && base->generation == arrangement->generation)
            for (const auto& entry : base->voices) if (entry.voice->matches(clip, file, pitch, gain, midi, repeat, gateFrom, gateTo, globalStart, globalEnd, expression)) { voice = entry.voice; break; }
        const bool reused = voice != nullptr;
        if (!voice)
        {
            auto* reader = formats.createReaderFor(file);
            if (!reader) { setError("Could not prepare " + file.getFileName()); return; }
            auto* secondReader = globalEnd > globalStart ? formats.createReaderFor(file) : nullptr;
            if (globalEnd > globalStart && !secondReader) { delete reader; setError("Could not prepare loop audio"); return; }
            voice = std::make_shared<Voice>(clip, asset, file, rateIn, reader, pitch, gain, midi, repeat, gateFrom, gateTo, secondReader, globalStart, globalEnd, expression);
        }
        arrangement->voices.push_back({ std::move(voice), control->second, reused });
    };
    for (const auto& clip : request.snapshot.project.clips)
    {
        if (clip.scene >= 0) continue;
        const auto* track = findId(request.snapshot.project.tracks, clip.track); if (!track || !clip.enabled) continue;
        const auto* asset = findId(request.snapshot.project.assets, track->midi ? track->sample : clip.asset);
        if (!track->midi) { if (asset) addVoice(clip, *asset, clip.playbackRate, 1, false); continue; }
        const auto frameAt = [rateIn](double seconds) { return static_cast<juce::int64>(std::llround(seconds * rateIn)); };
        for (size_t index = 0; index < clip.notes.size(); ++index)
        {
            const auto& note = clip.notes[index]; auto pattern = midiPattern(clip, note); if (!pattern) continue;
            if (request.snapshot.project.grooveAmount > 0)
            {
                const auto sixteenth = 15.0 / request.snapshot.project.bpm;
                const auto division = std::floor((clip.start + note.start - clip.offset) / sixteenth + 0.5);
                if (std::isfinite(division) && std::abs(std::fmod(division, 2.0)) >= 0.5) pattern->onset += sixteenth * 0.5 * request.snapshot.project.grooveAmount;
            }
            const auto clipEnd = clip.start + clip.duration;
            const auto from = std::max(clip.start, pattern->onset), to = std::min(clipEnd, pattern->onset + pattern->duration);
            if (pattern->period <= 0 && to <= from) continue;
            const auto onset = pattern->period > 0 ? pattern->onset : from;
            const auto length = pattern->period > 0 ? pattern->duration : to - from;
            arrangement->notes.push_back({ request.controls.at(track->id), note.pitch, frameAt(onset),
                std::max<juce::int64>(1, frameAt(length)), pattern->period > 0 ? std::max<juce::int64>(1, frameAt(pattern->period)) : 0,
                frameAt(clip.start), frameAt(clipEnd), static_cast<float>(note.velocity), note, static_cast<juce::uint32>((clip.id + ":" + juce::String(static_cast<int>(index))).hashCode()), pattern->period > 0 ? 0 : from - pattern->onset });
            if (!asset || !track->sampler || track->samplerBypass) continue;
            const auto pitch = std::pow(2.0, (note.pitch - track->rootNote) / 12.0);
            const auto sampleEnd = pattern->onset + track->sampleDuration / pitch;
            if (pattern->period <= 0 && sampleEnd <= from) continue;
            Clip played { clip.id + ":" + juce::String(static_cast<int>(index)), clip.track, asset->id,
                onset, track->sampleOffset + (pattern->period > 0 ? 0 : (from - pattern->onset) * pitch),
                std::min(length, pattern->period > 0 ? track->sampleDuration / pitch : sampleEnd - from) };
            addVoice(played, *asset, pitch, 1, true, pattern->period, clip.start, clipEnd, &note);
        }
    }

    const auto lead = static_cast<juce::int64>(rateIn * 0.15);
    const auto blockSize = quantum.load();
    const auto frame = clock.load();
    arrangement->activation = request.seek ? request.seekFrame
        : frame + (base && running.load() ? ((lead + blockSize - 1) / blockSize) * blockSize : 0);
    for (const auto& entry : arrangement->voices)
        if (!entry.reused) { entry.voice->initialise(arrangement->activation); entry.voice->fill(arrangement->activation, startupBlocks()); }
    return arrangement;
}
void AudioEngine::workerLoop()
{
    std::optional<Request> work;
    std::optional<Request> building;
    std::future<std::unique_ptr<Arrangement>> future;
    std::unique_ptr<Arrangement> preparing;
    while (!exiting.load())
    {
        std::vector<PreviewRequest> previewRequests;
        { std::lock_guard<std::mutex> lock(preparer.mutex); previewRequests.swap(preparer.previews); }
        for (const auto& request : previewRequests)
        {
            auto& slot = previews[request.slot];
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            auto* reader = formats.createReaderFor(request.file);
            if (!reader) { slot.state.store(0, std::memory_order_release); continue; }
            const auto pitch = std::pow(2.0, (request.note - request.track.rootNote) / 12.0);
            Clip clip { "preview", request.track.id, request.asset.id, 0, request.track.sampleOffset,
                std::min(request.track.sampleDuration / pitch, request.held ? request.track.sampleDuration / pitch : 0.25) };
            slot.voice = std::make_unique<Voice>(clip, request.asset, request.file, sampleRate.load(), reader, pitch, request.velocity, true);
            slot.control = request.control;
            slot.voice->initialise(0); slot.level = 0; slot.releaseLeft = 0;
        }
        for (auto& slot : previews)
        {
            const auto state = slot.state.load(std::memory_order_acquire);
            if (state == 4 || (state == 1 && slot.voice && slot.release.load()))
            { slot.voice.reset(); slot.control.reset(); slot.state.store(0, std::memory_order_release); }
            else if (state >= 1 && state <= 3 && slot.voice)
            {
                slot.voice->fill(slot.position.load());
                if (state == 1 && slot.voice->ready()) slot.state.store(2, std::memory_order_release);
            }
        }
        auto read = retireRead.load(std::memory_order_relaxed), write = retireWrite.load(std::memory_order_acquire);
        while (read != write
#if defined(OPENTONE_TESTING)
               && !pauseCleanup.load()
#endif
               )
        {
            auto* value = retired[read % retireSize];
            owned.erase(std::remove_if(owned.begin(), owned.end(), [value](const auto& item) { return item.get() == value; }), owned.end());
            ++read;
        }
        retireRead.store(read, std::memory_order_release);
        auto* base = activeView.load(std::memory_order_acquire);
#if defined(OPENTONE_TESTING)
        if (!pauseStreaming.load())
#endif
        if (!(seeking.load() && fadeFinished.load()))
        for (const auto& arrangement : owned)
            for (const auto& entry : arrangement->voices) entry.voice->fill(clock.load());
        { std::lock_guard<std::mutex> lock(preparer.mutex);
          if (preparer.latest) { work = std::move(preparer.latest); preparer.latest.reset(); preparing.reset(); } }
        if (future.valid() && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            preparing = future.get();
            if (work) preparing.reset();
            else work = building;
            building.reset();
        }
        if (work && !preparing && !future.valid() && pending.load() == nullptr && (!work->seek || fadeFinished.load()))
        {
            auto baseCopy = base ? std::make_unique<Arrangement>(*base) : nullptr;
            if (work->seek && base && seeking.load() && base == activeView.load(std::memory_order_acquire)
                && !running.load() && base->revision == work->snapshot.revision
                && base->generation == work->snapshot.generation && base->configuration == work->configuration)
            {
                // The callback has finished its seek fade; the worker can rewind its readers safely.
                jassert(seeking.load() && fadeFinished.load() && !running.load());
                preparing = std::move(baseCopy); preparing->baseRevision = base->revision;
                preparing->seek = true; preparing->seekSerial = work->seekSerial; preparing->activation = work->seekFrame;
                for (auto& entry : preparing->voices)
                { entry.reused = false; entry.voice->initialise(preparing->activation); }
            }
            else
            {
                building = work; work.reset();
                future = std::async(std::launch::async, [this, request = *building, copy = std::move(baseCopy)]
                { return prepare(request, copy.get()); });
            }
        }
        if (preparing)
        {
            auto& result = preparing;
            for (const auto& entry : result->voices) if (!entry.reused) entry.voice->fill(result->activation, startupBlocks());
            bool ready = true;
            for (const auto& entry : result->voices) if (!entry.reused && !entry.voice->ready(startupBlocks())) ready = false;
            if (ready && (result->seek || !base || !running.load() || clock.load() < result->activation))
            {
                auto* pointer = result.get(); owned.push_back(std::move(result));
                pending.store(pointer, std::memory_order_release); work.reset();
            }
            else if (!result->seek && base && running.load() && clock.load() >= result->activation)
            {
                // Reposition only unowned new voices; active voices continue streaming.
                const auto blockSize = quantum.load();
                result->activation = clock.load() + ((static_cast<juce::int64>(sampleRate.load() * 0.15) + blockSize - 1) / blockSize) * blockSize;
                for (const auto& entry : result->voices) if (!entry.reused)
                { entry.voice->initialise(result->activation); }
            }
        }
        if (!work && !preparing && !future.valid() && pending.load() == nullptr)
        {
            std::lock_guard<std::mutex> lock(preparer.mutex);
            if (!preparer.latest && preparer.desired.snapshot.revision > 0
                && (!base || requested.load() > acknowledged.load()))
                work = preparer.desired;
        }
#if defined(OPENTONE_TESTING)
        ownedArrangements.store(owned.size());
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void AudioEngine::render(float* const* output, int channels, int samples) noexcept
{
    std::array<float*, 2> pointers {};
    const int capacity = std::max(1, quantum.load());
    for (int offset = 0; offset < samples;)
    {
        auto count = std::min(capacity, samples - offset);
        if (running.load() && looping.load())
        { const auto remaining = static_cast<juce::int64>(std::llround(loopEnd.load() * sampleRate.load())) - playbackFrame(clock.load());
          if (remaining > 0) count = static_cast<int>(std::min<juce::int64>(count, remaining)); }
        for (int channel = 0; channel < std::min(channels, 2); ++channel) pointers[static_cast<size_t>(channel)] = output[channel] ? output[channel] + offset : nullptr;
        liveInputOffset = offset; renderBlock(pointers.data(), std::min(channels, 2), count); offset += count;
    }
    for (int channel = 2; channel < channels; ++channel) if (output[channel]) std::fill_n(output[channel], samples, 0.0f);
}
void AudioEngine::renderBlock(float* const* output, int channels, int samples) noexcept
{
    struct CallbackScope { CallbackScope() { rendering = true; } ~CallbackScope() { rendering = false; } } scope;
    const juce::ScopedNoDenormals noDenormals;
    for (int channel = 0; channel < channels; ++channel) if (output[channel]) std::fill_n(output[channel], samples, 0.0f);
    for (auto& slot : previews)
    { const auto state = slot.state.load(std::memory_order_acquire); if (state == 2 || state == 3) { slot.control->clearNotes(); slot.control->samplerCursor.store(-1); } }
    const juce::CriticalSection::ScopedTryLockType pluginLock(plugins.mutex);
    pluginBlock = pluginLock.isLocked();
    if (active) for (const auto& control : active->controls) control->samplerCursor.store(-1);
    renderPlayback(output, channels, samples);
    renderPreviews(output, channels, samples);
    if (pluginBlock && liveInput) plugins.monitorInput(liveInput, liveInputChannels, liveInputOffset, samples);
    if (pluginBlock) plugins.finishBlock(output, channels, samples, masterTarget.load(), masterPanTarget.load());
    else for (int channel = 0; channel < std::min(channels, 2); ++channel) if (output[channel])
        juce::FloatVectorOperations::multiply(output[channel], panGain(masterPanTarget.load(), channel), samples);
    bool clippedOutput = false; std::array<float, 2> peaks {};
    for (int channel = 0; channel < channels; ++channel) if (output[channel])
        for (int sample = 0; sample < samples; ++sample)
        { const auto level = std::abs(output[channel][sample]); peaks[static_cast<size_t>(channel)] = std::max(peaks[static_cast<size_t>(channel)], level);
          clippedOutput = clippedOutput || level > 1; }
    const auto decay = static_cast<float>(std::exp(-samples / (sampleRate.load() * 0.15)));
    outputPeakLeft.store(std::max(peaks[0], outputPeakLeft.load() * decay)); outputPeakRight.store(std::max(peaks[1], outputPeakRight.load() * decay));
    clipping.store(clippedOutput);
}
void AudioEngine::renderPlayback(float* const* output, int channels, int samples) noexcept
{
    const auto rateIn = sampleRate.load();
    const auto ramp = std::max(1, static_cast<int>(rateIn * 0.005));
    const auto rampFloat = static_cast<float>(ramp);
    if (seeking.load() && !running.load()) fadeFinished.store(true);
    if (seeking.load() && !fadeFinished.load() && seekFadeLeft == 0) seekFadeLeft = ramp;
    auto* update = pending.load(std::memory_order_acquire);
    if (update && outgoing == nullptr && retirementAvailable())
    {
        const auto frame = clock.load();
        const bool compatible = update->configuration == configuration.load()
            && update->baseRevision == (active ? active->revision : 0)
            && (!active || update->generation == active->generation)
            && (!update->seek || update->seekSerial == seekSerial.load());
        if (!compatible || (!update->seek && running.load() && frame > update->activation))
        { pending.store(nullptr, std::memory_order_release); retire(update); }
        else if ((update->seek && fadeFinished.load()) || (!update->seek && (!running.load() || frame == update->activation
                       || frame + samples > update->activation)))
        {
            // Activation is always a callback boundary; a deadline inside a block is deferred.
            if (!update->seek && running.load() && frame < update->activation) {}
            else
            {
                if (update->seek) retire(active); else outgoing = active;
                active = update; transitionLeft = ramp;
                activeView.store(active, std::memory_order_release);
                acknowledged.store(active->revision);
                if (active->seek) { clock.store(active->activation); seekFadeLeft = 0; seeking.store(false); running.store(resumeAfterSeek.load()); }
                pending.store(nullptr, std::memory_order_release);
            }
        }
    }
    const auto linkOffset = syncLink(samples);
    if (pluginBlock) plugins.beginBlock(playbackFrame(clock.load()), tempo.load(), running.load(), samples);
    if (pluginBlock && active) for (const auto& control : active->controls) if (control->bus)
    { control->target.store(control->bus->outputEnabled && !control->bus->parent ? control->bus->target : 0.0f); control->panTarget.store(control->bus->pan); }
    const auto availableSamples = samples - linkOffset;
    if (active) for (const auto& control : active->controls) { control->clearNotes(); control->publishNotes(); }
    if (pluginBlock)
    {
        const auto start = playbackFrame(clock.load()); plugins.transport(start, tempo.load(), running.load());
        if (active) for (const auto& control : active->controls) if (control->bus && control->bus->chase)
        { auto& input = *control->bus; for (int channel = 1; channel <= 16; ++channel) input.midi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
          for (size_t pitch = 0; pitch < input.previews.size(); ++pitch) if (input.previews[pitch].on)
              input.midi.addEvent(juce::MidiMessage::noteOn(1, static_cast<int>(pitch), input.previews[pitch].velocity), 0); }
        const auto loopFrom = static_cast<juce::int64>(std::llround(loopStart.load() * rateIn)), loopTo = static_cast<juce::int64>(std::llround(loopEnd.load() * rateIn));
        const auto round = looping.load() && loopTo > loopFrom && clock.load() >= loopFrom ? static_cast<uint64_t>((clock.load() - loopFrom) / (loopTo - loopFrom)) : 0;
        if (active && running.load()) for (const auto& note : active->notes) if (note.control->bus)
        {
            auto& input = *note.control->bus;
            if (input.monitor == 0) continue;
            const auto limit = std::min(start + availableSamples, note.windowEnd);
            auto onset = note.onsetAt(std::max(start, note.windowStart));
            if (note.period > 0) onset -= note.period;
            while (onset < limit)
            {
                const auto from = std::max(onset, note.windowStart), to = std::min(onset + note.length, note.windowEnd);
                const auto velocity = note.velocityAt(onset, round);
                if (to > from && velocity > 0)
                {
                    const auto channel = note.expression.channel;
                    const auto starting = (from >= start && from < start + availableSamples) || (input.chase && from < start && to > start);
                    const auto at = std::min(samples - 1, linkOffset + static_cast<int>(std::max<juce::int64>(0, from - start)));
                    if (starting)
                    { if (channel > 1 && !note.expression.expression.empty()) for (const auto control : {std::pair<int,int>{101,0}, {100,0}, {6,note.expression.bendRange}, {38,0}, {101,127}, {100,127}}) input.midi.addEvent(juce::MidiMessage::controllerEvent(channel, control.first, control.second), at);
                      input.midi.addEvent(juce::MidiMessage::noteOn(channel, note.pitch, static_cast<float>(velocity)), at); }
                    if (channel > 1 && from < start + availableSamples && to > start)
                    { const auto eventTime = std::min(to - 1, start + availableSamples - 1); const auto elapsed = static_cast<double>(eventTime - onset) / rateIn + note.expressionOffset;
                      for (const auto& expression : note.expression.expression)
                      { const auto value = automationValue(expression.points, elapsed, 0); const auto offset = linkOffset + static_cast<int>(std::max<juce::int64>(0, eventTime - start));
                        if (expression.controller == -2) input.midi.addEvent(juce::MidiMessage::pitchWheel(channel, juce::jlimit(0, 16383, juce::roundToInt(8192 + value / (note.expression.bendRange * 100.0) * 8191))), offset);
                        else if (expression.controller == -1) input.midi.addEvent(juce::MidiMessage::channelPressureChange(channel, juce::jlimit(0, 127, juce::roundToInt(value))), offset);
                        else input.midi.addEvent(juce::MidiMessage::controllerEvent(channel, expression.controller, juce::jlimit(0, 127, juce::roundToInt(value))), offset); } }

                    if (to >= start && to < start + availableSamples)
                        input.midi.addEvent(juce::MidiMessage::noteOff(note.expression.channel, note.pitch, static_cast<float>(note.expression.releaseVelocity)), linkOffset + static_cast<int>(to - start));
                }
                if (note.period <= 0) break;
                onset += note.period;
            }
        }
    }
    if (!running.load() || !active)
    {
        if (outgoing && retirementAvailable()) { retire(outgoing); outgoing = nullptr; }
        return;
    }
    auto frame = clock.load(); bool starved = false;
    const bool clickEnabled = metronomeEnabled.load();
    const auto framesPerBeat = rateIn * 60 / tempo.load() * 4.0 / denominator.load();
    for (int sample = linkOffset; sample < samples; ++sample)
    {
        for (const auto& control : active->controls)
            control->current += juce::jlimit(-1.0f / rampFloat, 1.0f / rampFloat, control->target.load() - control->current);
        masterCurrent += juce::jlimit(-1.0f / rampFloat, 1.0f / rampFloat, masterTarget.load() - masterCurrent);
        float left = 0, right = 0;
        const float progress = 1.0f - static_cast<float>(transitionLeft) / rampFloat;
        for (const auto& entry : active->voices)
        {
            float l = 0, r = 0; if (!entry.voice->sample(frame, l, r)) starved = true;
            const float gain = entry.control->current * (entry.reused ? 1.0f : progress)
                * (entry.voice->midi ? entry.control->samplerTarget.load() : 1) * static_cast<float>(entry.voice->clip.gain)
                * (pluginBlock && entry.control->bus && (entry.control->bus->monitor == 0 || (entry.control->bus->inputEnabled && entry.control->bus->armed && entry.control->bus->monitor == 1)) ? 0.0f : 1.0f);
            if (pluginBlock && entry.control->bus)
            { const auto inputGain = (entry.reused ? 1.0f : progress) * (entry.voice->midi ? entry.control->samplerTarget.load() : 1) * static_cast<float>(entry.voice->clip.gain)
                * (pluginBlock && entry.control->bus && (entry.control->bus->monitor == 0 || (entry.control->bus->inputEnabled && entry.control->bus->armed && entry.control->bus->monitor == 1)) ? 0.0f : 1.0f);
              entry.control->bus->audio.addSample(0, sample, l * inputGain); entry.control->bus->audio.addSample(1, sample, r * inputGain); }
            else
            { left += l * gain * panGain(entry.control->panTarget.load(), 0); right += r * gain * panGain(entry.control->panTarget.load(), 1); }
        }
        if (outgoing)
            for (const auto& entry : outgoing->voices)
            {
                bool reused = false;
                for (const auto& next : active->voices) if (next.voice == entry.voice) { reused = true; break; }
                if (!reused)
                { float l = 0, r = 0; entry.voice->sample(frame, l, r);
                  const auto gain = entry.control->current * (1 - progress) * (entry.voice->midi ? entry.control->samplerTarget.load() : 1) * static_cast<float>(entry.voice->clip.gain)
                * (pluginBlock && entry.control->bus && (entry.control->bus->monitor == 0 || (entry.control->bus->inputEnabled && entry.control->bus->armed && entry.control->bus->monitor == 1)) ? 0.0f : 1.0f);
                  if (pluginBlock && entry.control->bus)
            { const auto inputGain = (1 - progress) * (entry.voice->midi ? entry.control->samplerTarget.load() : 1) * static_cast<float>(entry.voice->clip.gain)
                * (pluginBlock && entry.control->bus && (entry.control->bus->monitor == 0 || (entry.control->bus->inputEnabled && entry.control->bus->armed && entry.control->bus->monitor == 1)) ? 0.0f : 1.0f);
              entry.control->bus->audio.addSample(0, sample, l * inputGain); entry.control->bus->audio.addSample(1, sample, r * inputGain); }
            else
            { left += l * gain * panGain(entry.control->panTarget.load(), 0); right += r * gain * panGain(entry.control->panTarget.load(), 1); } }
            }
        if (clickEnabled)
        {
            const auto beatPosition = linkState->blockBeat >= 0 ? (linkState->blockBeat + sample * tempo.load() / 60 / rateIn) * denominator.load() / 4.0 : static_cast<double>(playbackFrame(frame)) / framesPerBeat;
            const auto beat = std::floor(beatPosition);
            const auto seconds = (beatPosition - beat) * framesPerBeat / rateIn;
            if (seconds < 0.02)
            {
                const bool accent = std::fmod(beat, static_cast<double>(numerator.load())) < 0.5;
                const auto envelope = std::min(1.0, seconds / 0.001) * (1 - seconds / 0.02);
                const auto click = static_cast<float>((accent ? 0.22 : 0.14) * envelope
                    * std::sin(juce::MathConstants<double>::twoPi * (accent ? 1800 : 1200) * seconds));
                left += click * cueTarget.load(); right += click * cueTarget.load();
            }
        }
        const float fade = seekFadeLeft > 0 ? static_cast<float>(seekFadeLeft) / rampFloat : 1;
        left *= (pluginBlock ? 1 : masterCurrent) * fade; right *= (pluginBlock ? 1 : masterCurrent) * fade;
        if (channels > 0 && output[0]) output[0][sample] = left;
        if (channels > 1 && output[1]) output[1][sample] = right;
        ++frame;
        if (transitionLeft > 0 && --transitionLeft == 0 && outgoing)
        { retire(outgoing); outgoing = nullptr; }
        if (seekFadeLeft > 0 && --seekFadeLeft == 0)
        { running.store(false); fadeFinished.store(true); break; }
    }
    for (const auto& entry : active->voices) if (entry.voice->midi && entry.control->samplerTarget.load() > 0)
    {
        const auto time = entry.voice->mappedFrame(frame - 1), onset = entry.voice->onsetAt(time);
        if (time >= entry.voice->windowStart && time >= onset && time < entry.voice->end && time < onset + entry.voice->noteLength && entry.voice->expressionVelocity(onset, frame - 1) > 0)
        { const auto position = entry.voice->clip.offset + static_cast<double>(time - onset) / entry.voice->rate * entry.voice->pitch;
          if (position < entry.control->samplerEnd.load()) entry.control->samplerCursor.store(position); }
    }
    clock.store(frame);
    if (running.load()) for (const auto& note : active->notes)
        if (note.contains(playbackFrame(frame), looping.load() && frame >= static_cast<juce::int64>(loopStart.load() * rateIn) ? static_cast<uint64_t>((frame - static_cast<juce::int64>(loopStart.load() * rateIn)) / std::max<juce::int64>(1, static_cast<juce::int64>((loopEnd.load() - loopStart.load()) * rateIn))) : 0)) note.control->markNote(note.pitch);
    for (const auto& control : active->controls) control->publishNotes();
    if (starved) ++underruns;
}
void AudioEngine::renderPreviews(float* const* output, int channels, int samples) noexcept
{
    const auto ramp = std::max(1, static_cast<int>(sampleRate.load() * 0.005));
    for (auto& slot : previews)
    {
        auto state = slot.state.load(std::memory_order_acquire); if (state != 2 && state != 3) continue;
        if (state == 2 && slot.release.load()) { slot.control->publishNotes(); slot.state.store(4, std::memory_order_release); continue; }
        if (state == 2) slot.state.store(3, std::memory_order_release);
        auto* voice = slot.voice.get(); auto frame = slot.position.load();
        bool done = false;
        for (int sample = 0; sample < samples; ++sample)
        {
            if (frame >= voice->end) { done = true; break; }
            if (slot.release.load() && slot.releaseLeft == 0) slot.releaseLeft = ramp;
            float left = 0, right = 0; voice->sample(frame++, left, right);
            slot.level += juce::jlimit(-1.0f / static_cast<float>(ramp), 1.0f / static_cast<float>(ramp), slot.control->target.load() - slot.level);
            const auto fade = slot.releaseLeft > 0 ? static_cast<float>(slot.releaseLeft) / static_cast<float>(ramp) : 1;
            const auto gain = slot.level * masterTarget.load() * slot.control->samplerTarget.load() * fade;
            if (pluginBlock && slot.control->bus)
            { const auto inputGain = slot.control->samplerTarget.load() * fade;
              slot.control->bus->audio.addSample(0, sample, left * inputGain); slot.control->bus->audio.addSample(1, sample, right * inputGain); }
            else
            { if (channels > 0 && output[0]) output[0][sample] += left * gain * panGain(slot.control->panTarget.load(), 0);
              if (channels > 1 && output[1]) output[1][sample] += right * gain * panGain(slot.control->panTarget.load(), 1); }
            if (slot.releaseLeft > 0 && --slot.releaseLeft == 0) { done = true; break; }
        }
        slot.position.store(frame);
        if (!done && slot.control->samplerTarget.load() > 0)
        { const auto position = voice->clip.offset + static_cast<double>(frame) / voice->rate * voice->pitch;
          if (position < slot.control->samplerEnd.load()) slot.control->samplerCursor.store(position); }
        if (!done && !slot.release.load()) slot.control->markNote(slot.note);
        slot.control->publishNotes();
        if (done) slot.state.store(4, std::memory_order_release);
    }
}
juce::Result AudioEngine::enableMicrophone()
{
    auto* device = devices.getCurrentAudioDevice();
    if (!device) return juce::Result::fail("No audio device is available. Choose one in Settings > Audio.");
    if (device->getActiveInputChannels().countNumberOfSetBits() > 0) return juce::Result::ok();
    auto setup = devices.getAudioDeviceSetup();
    if (setup.inputDeviceName.isEmpty())
    {
        auto* type = devices.getCurrentDeviceTypeObject();
        const auto names = type ? type->getDeviceNames(true) : juce::StringArray();
        if (names.isEmpty()) return juce::Result::fail("No microphone or audio input is available.");
        setup.inputDeviceName = names[juce::jlimit(0, names.size() - 1, type->getDefaultDeviceIndex(true))];
    }
    setup.useDefaultInputChannels = false; setup.inputChannels.clear(); setup.inputChannels.setBit(0);
    const auto previousPosition = position(); const auto wasPlaying = playing();
    const auto error = devices.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) return juce::Result::fail(error);
    if (wasPlaying) { seek(previousPosition); play(); }
    device = devices.getCurrentAudioDevice();
    return device && device->getActiveInputChannels().countNumberOfSetBits() > 0 ? juce::Result::ok()
        : juce::Result::fail("Microphone input could not be enabled. Check microphone permission and Settings > Audio.");
}
juce::Result AudioEngine::startAudioRecording(const juce::File& file, double start, double end)
{
    auto directory = file.getParentDirectory().createDirectory(); if (directory.failed()) return directory;
    if (file.exists()) return juce::Result::fail("Recording file already exists.");
    auto stream = file.createOutputStream();
    if (!stream || stream->failedToOpen()) return juce::Result::fail("Could not create the audio recording file.");
    juce::WavAudioFormat format;
    auto* writer = format.createWriterFor(stream.get(), rate(), 1, 24, {}, 0);
    if (!writer) return juce::Result::fail("Could not create the WAV recorder.");
    stream.release();
    if (!recordThread.isThreadRunning() && !recordThread.startThread()) { delete writer; return juce::Result::fail("Could not start the audio recording writer."); }
    auto next = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer, recordThread, 262144);
    next->setFlushInterval(static_cast<int>(rate()));
    const juce::ScopedLock lock(devices.getAudioCallbackLock());
    recordEnd = end >= 0 && std::isfinite(end) ? static_cast<juce::int64>(std::llround(end * sampleRate.load())) : std::numeric_limits<juce::int64>::max();
    recordStart = static_cast<juce::int64>(std::llround(start * rate()));
    recordRate.store(rate());
    recordFailed.store(false); recordFrames.store(0); recordFirstFrame.store(recordStart); recordWriter = std::move(next);
    return juce::Result::ok();
}
juce::Result AudioEngine::finishAudioRecording()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;
    { const juce::ScopedLock lock(devices.getAudioCallbackLock()); finished = std::move(recordWriter); }
    finished.reset();
    return recordFailed.load() ? juce::Result::fail("Audio recording was interrupted by an input change or a full recording buffer.") : juce::Result::ok();
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* input, int inputChannels, float* const* output,
                                                  int channels, int samples, const juce::AudioIODeviceCallbackContext&)
{
    if (linkEnabled())
    {
        if (linkState->resetFilter.exchange(false)) { linkState->hostTimeFilter.reset(); linkState->sampleCounter = 0; }
        // The SDK filter measures callback time; device output latency is added in syncLink.
        linkState->hostTime = linkState->hostTimeFilter.sampleTimeToHostTime(linkState->sampleCounter);
        linkState->sampleCounter += samples;
    }
    float peak = 0;
    const float* microphone = nullptr;
    for (int channel = 0; channel < inputChannels; ++channel) if (input[channel]) { microphone = input[channel]; break; }
    if (microphone) for (int sample = 0; sample < samples; ++sample) peak = std::max(peak, std::abs(microphone[sample]));
    inputPeak.store(std::max(peak, inputPeak.load() * 0.85f));
    if (recordWriter && running.load() && !seeking.load() && !recordFailed.load())
    {
        const auto frame = clock.load() - inputLatency;
        const auto skip = static_cast<int>(juce::jlimit<juce::int64>(0, samples, recordStart - frame));
        const float* source = microphone ? microphone + skip : nullptr;
        const auto count = static_cast<int>(juce::jlimit<juce::int64>(0, samples - skip, recordEnd - frame - skip));
        if (count > 0)
        {
            if (!source || !recordWriter->write(&source, count)) recordFailed.store(true);
            else
            { if (recordFrames.load() == 0) recordFirstFrame.store(frame + skip); recordFrames.fetch_add(count); }
        }
    }
    liveInput = input; liveInputChannels = inputChannels;
    render(output, channels, samples);
    liveInput = nullptr; liveInputChannels = liveInputOffset = 0;
}
}
