// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/AudioEngine.h"
#include "domain/TimelineMath.h"
#include <chrono>
#include <future>

namespace opentone
{
thread_local bool AudioEngine::rendering = false;
struct AudioEngine::Voice
{
    struct Block { juce::int64 frame = 0; std::array<std::array<float, chunk>, 2> data {}; bool failed = false; };
    struct Source : juce::AudioSource
    {
        explicit Source(juce::AudioFormatReader* input, juce::TimeSliceThread& thread)
            : reader(input, thread, 65536) { reader.setReadTimeout(0); }
        void prepareToPlay(int, double) override {}
        void releaseResources() override {}
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
        {
            if (!reader.read(info.buffer, info.startSample, info.numSamples, position, true, true)) failed = true;
            position += info.numSamples;
        }
        juce::BufferingAudioReader reader;
        juce::int64 position = 0;
        bool failed = false;
    };
    Voice(const Clip& clipIn, const Asset& asset, const juce::File& file, double rateIn,
          juce::AudioFormatReader* reader, juce::TimeSliceThread& thread, double pitchIn = 1, double gainIn = 1, bool midiIn = false, double repeat = 0, double gateFrom = -1, double gateTo = -1, juce::AudioFormatReader* secondReader = nullptr, double globalStart = 0, double globalEnd = 0)
        : clip(clipIn), path(file), rate(rateIn), pitch(pitchIn), gain(gainIn), midi(midiIn), source(reader, thread), resampler(&source, false, 2), scratch(2, chunk)
    {
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
            alternate = std::make_unique<Source>(secondReader, thread);
            alternateResampler = std::make_unique<juce::ResamplingAudioSource>(alternate.get(), false, 2);
            alternateResampler->setResamplingRatio(sourceRate * pitch / rate); alternateResampler->prepareToPlay(chunk, rate);
        }
    }
    bool matches(const Clip& value, const juce::File& file, double pitchIn, double gainIn, bool midiIn, double repeat, double gateFrom, double gateTo, double globalStart, double globalEnd) const
    { return loopStart == static_cast<juce::int64>(std::llround(globalStart * rate)) && loopEnd == static_cast<juce::int64>(std::llround(globalEnd * rate)) && period == (repeat > 0 ? std::max<juce::int64>(1, static_cast<juce::int64>(std::llround(repeat * rate))) : 0)
          && windowStart == (gateFrom >= 0 ? static_cast<juce::int64>(std::llround(gateFrom * rate)) : start)
          && end == static_cast<juce::int64>(std::llround((gateTo >= 0 ? gateTo : value.start + value.duration) * rate))
          && clip.id == value.id && clip.track == value.track && clip.asset == value.asset && path == file
          && std::tie(clip.start, clip.offset, clip.duration, pitch, gain, midi)
               == std::tie(value.start, value.offset, value.duration, pitchIn, gainIn, midiIn); }
    juce::int64 onsetAt(juce::int64 frame) const
    { return period > 0 ? start + static_cast<juce::int64>(std::floor(static_cast<double>(frame - start) / static_cast<double>(period))) * period : start; }
    void initialise(juce::int64 frame) { nextFrame = frame; sourceOutput = -1; sourceOccurrence = std::numeric_limits<juce::int64>::min(); }
    void fill(juce::int64 current)
    {
        // SPSC slots are never overwritten until the callback has released them.
        for (int count = 0; count < ringSize - 1; ++count)
        {
            auto write = writeIndex.load(std::memory_order_relaxed);
            if (write - readIndex.load(std::memory_order_acquire) >= ringSize - 1) break;
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
    bool ready() const { return writeIndex.load(std::memory_order_acquire) - readIndex.load() >= 8; }
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
        left = block.data[0][offset] * static_cast<float>(level); right = block.data[1][offset] * static_cast<float>(level); return !block.failed;
    }
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
        juce::int64 start, length, period, windowStart, windowEnd; float velocity;
        juce::int64 onsetAt(juce::int64 frame) const
        { return period > 0 ? start + static_cast<juce::int64>(std::floor(static_cast<double>(frame - start) / static_cast<double>(period))) * period : start; }
        bool contains(juce::int64 frame) const
        { const auto onset = onsetAt(frame); return frame >= windowStart && frame < windowEnd && frame >= onset && frame < onset + length; }
    };
    std::vector<Note> notes;
    struct Entry { std::shared_ptr<Voice> voice; std::shared_ptr<Control> control; bool reused = false; };
    std::vector<Entry> voices;
    std::vector<std::shared_ptr<Control>> controls;
    uint64_t generation = 0, revision = 0, baseRevision = 0, configuration = 0, seekSerial = 0;
    juce::int64 activation = 0;
    bool seek = false;
};
AudioEngine::AudioEngine() { readAhead.startThread(); startWorker(); }
AudioEngine::~AudioEngine()
{
    devices.removeAudioCallback(this);
    finishAudioRecording(); recordThread.stopThread(-1);
    resetResources(); readAhead.stopThread(-1);
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
{ setError({}); inputLatency = std::max(0, device->getInputLatencyInSamples()); configureOffline(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()); }
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
    tempo.store(snapshot.project.bpm); masterTarget.store(static_cast<float>(snapshot.project.master.gain)); masterPanTarget.store(static_cast<float>(snapshot.project.master.pan));
    Request request; request.snapshot = snapshot; request.snapshot.project.loopEnabled = snapshot.project.loopEnabled && allowLoop; request.configuration = configuration.load();
    const bool anySolo = std::any_of(snapshot.project.tracks.begin(), snapshot.project.tracks.end(), [](const auto& track) { return track.solo; });
    for (const auto& track : snapshot.project.tracks)
    {
        auto& control = controls[track.id];
        if (!control) { control = std::make_shared<Control>(); control->bus = plugins.bus(track.id); control->current = (track.mute || (anySolo && !track.solo)) ? 0.0f : static_cast<float>(track.gain); }
        control->panTarget.store(static_cast<float>(track.pan));
        control->target.store((track.mute || (anySolo && !track.solo)) ? 0.0f : static_cast<float>(track.gain));
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
void AudioEngine::play() { resumeAfterSeek.store(true); if (!seeking.load()) running.store(true); }
void AudioEngine::stop() { resumeAfterSeek.store(false); running.store(false); if (seeking.load()) fadeFinished.store(true); }
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
        preparer.previews.push_back({ index, track, asset, file, note, velocity, held, control }); return true;
    }
    return false;
}
void AudioEngine::releaseEditPreviews(const juce::String& track)
{ plugins.release(track, -1, true); for (auto& slot : previews) if (slot.state.load() != 0 && slot.track == track && !slot.held) slot.release.store(true); }
void AudioEngine::releasePreview(const juce::String& track, int note)
{ plugins.release(track, note); for (auto& slot : previews) if (slot.state.load() != 0 && slot.track == track && (note < 0 || slot.note == note)) slot.release.store(true); }
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
    const bool resume = running.load();
    resumeAfterSeek.store(resume); fadeFinished.store(!resume || activeView.load() == nullptr); seeking.store(true);
    std::lock_guard<std::mutex> lock(preparer.mutex);
    auto request = preparer.desired; request.seek = true;
    request.seekSerial = ++seekSerial;
    request.seekFrame = static_cast<juce::int64>(std::llround(seconds * sampleRate.load()));
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
    const auto addVoice = [&](const Clip& clip, const Asset& asset, double pitch, double gain, bool midi, double repeat = 0, double gateFrom = -1, double gateTo = -1)
    {
        if (asset.missing) return;
        const auto file = request.snapshot.directory.getChildFile(asset.path);
        auto control = request.controls.find(clip.track); if (control == request.controls.end()) return;
        std::shared_ptr<Voice> voice;
        if (!request.seek && base && base->generation == arrangement->generation)
            for (const auto& entry : base->voices) if (entry.voice->matches(clip, file, pitch, gain, midi, repeat, gateFrom, gateTo, globalStart, globalEnd)) { voice = entry.voice; break; }
        const bool reused = voice != nullptr;
        if (!voice)
        {
            auto* reader = formats.createReaderFor(file);
            if (!reader) { setError("Could not prepare " + file.getFileName()); return; }
            auto* secondReader = globalEnd > globalStart ? formats.createReaderFor(file) : nullptr;
            if (globalEnd > globalStart && !secondReader) { delete reader; setError("Could not prepare loop audio"); return; }
            voice = std::make_shared<Voice>(clip, asset, file, rateIn, reader, readAhead, pitch, gain, midi, repeat, gateFrom, gateTo, secondReader, globalStart, globalEnd);
        }
        arrangement->voices.push_back({ std::move(voice), control->second, reused });
    };
    for (const auto& clip : request.snapshot.project.clips)
    {
        const auto* track = findId(request.snapshot.project.tracks, clip.track); if (!track) continue;
        const auto* asset = findId(request.snapshot.project.assets, track->midi ? track->sample : clip.asset);
        if (!track->midi) { if (asset) addVoice(clip, *asset, 1, 1, false); continue; }
        const auto frameAt = [rateIn](double seconds) { return static_cast<juce::int64>(std::llround(seconds * rateIn)); };
        for (size_t index = 0; index < clip.notes.size(); ++index)
        {
            const auto& note = clip.notes[index]; const auto pattern = midiPattern(clip, note); if (!pattern) continue;
            const auto clipEnd = clip.start + clip.duration;
            const auto from = std::max(clip.start, pattern->onset), to = std::min(clipEnd, pattern->onset + pattern->duration);
            if (pattern->period <= 0 && to <= from) continue;
            const auto onset = pattern->period > 0 ? pattern->onset : from;
            const auto length = pattern->period > 0 ? pattern->duration : to - from;
            arrangement->notes.push_back({ request.controls.at(track->id), note.pitch, frameAt(onset),
                std::max<juce::int64>(1, frameAt(length)), pattern->period > 0 ? std::max<juce::int64>(1, frameAt(pattern->period)) : 0,
                frameAt(clip.start), frameAt(clipEnd), static_cast<float>(note.velocity) });
            if (!asset || !track->sampler || track->samplerBypass) continue;
            const auto pitch = std::pow(2.0, (note.pitch - track->rootNote) / 12.0);
            const auto sampleEnd = pattern->onset + track->sampleDuration / pitch;
            if (pattern->period <= 0 && sampleEnd <= from) continue;
            Clip played { clip.id + ":" + juce::String(static_cast<int>(index)), clip.track, asset->id,
                onset, track->sampleOffset + (pattern->period > 0 ? 0 : (from - pattern->onset) * pitch),
                std::min(length, pattern->period > 0 ? track->sampleDuration / pitch : sampleEnd - from) };
            addVoice(played, *asset, pitch, note.velocity, true, pattern->period, clip.start, clipEnd);
        }
    }

    const auto lead = static_cast<juce::int64>(rateIn * 0.15);
    const auto blockSize = quantum.load();
    const auto frame = clock.load();
    arrangement->activation = request.seek ? request.seekFrame
        : frame + (base && running.load() ? ((lead + blockSize - 1) / blockSize) * blockSize : 0);
    for (const auto& entry : arrangement->voices)
        if (!entry.reused) { entry.voice->initialise(arrangement->activation); entry.voice->fill(arrangement->activation); }
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
            slot.voice = std::make_unique<Voice>(clip, request.asset, request.file, sampleRate.load(), reader, readAhead, pitch, request.velocity, true);
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
            building = work; work.reset();
            future = std::async(std::launch::async, [this, request = *building, copy = std::move(baseCopy)]
            { return prepare(request, copy.get()); });
        }
        if (preparing)
        {
            auto& result = preparing;
            for (const auto& entry : result->voices) if (!entry.reused) entry.voice->fill(result->activation);
            bool ready = true;
            for (const auto& entry : result->voices) if (!entry.reused && !entry.voice->ready()) ready = false;
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
                { entry.voice->readIndex.store(0); entry.voice->writeIndex.store(0); entry.voice->initialise(result->activation); }
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
        renderBlock(pointers.data(), std::min(channels, 2), count); offset += count;
    }
    for (int channel = 2; channel < channels; ++channel) if (output[channel]) std::fill_n(output[channel], samples, 0.0f);
}
void AudioEngine::renderBlock(float* const* output, int channels, int samples) noexcept
{
    struct CallbackScope { CallbackScope() { rendering = true; } ~CallbackScope() { rendering = false; } } scope;
    const juce::ScopedNoDenormals noDenormals;
    for (int channel = 0; channel < channels; ++channel) if (output[channel]) std::fill_n(output[channel], samples, 0.0f);
    for (auto& slot : previews)
    { const auto state = slot.state.load(std::memory_order_acquire); if (state == 2 || state == 3) slot.control->clearNotes(); }
    const juce::CriticalSection::ScopedTryLockType pluginLock(plugins.mutex);
    pluginBlock = pluginLock.isLocked();
    if (pluginBlock) plugins.beginBlock(playbackFrame(clock.load()), tempo.load(), running.load(), samples);
    if (pluginBlock && active) for (const auto& control : active->controls) if (control->bus)
    { control->target.store(control->bus->target); control->panTarget.store(control->bus->pan); }
    renderPlayback(output, channels, samples);
    renderPreviews(output, channels, samples);
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
                outgoing = active; active = update; transitionLeft = ramp;
                activeView.store(active, std::memory_order_release); pending.store(nullptr, std::memory_order_release);
                acknowledged.store(active->revision);
                if (active->seek) { clock.store(active->activation); seekFadeLeft = 0; seeking.store(false); running.store(resumeAfterSeek.load()); }
            }
        }
    }
    if (active) for (const auto& control : active->controls) { control->clearNotes(); control->publishNotes(); }
    if (pluginBlock)
    {
        const auto start = playbackFrame(clock.load()); plugins.transport(start, tempo.load(), running.load());
        if (active) for (const auto& control : active->controls) if (control->bus && control->bus->chase)
        { auto& input = *control->bus; input.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
          for (size_t pitch = 0; pitch < input.previews.size(); ++pitch) if (input.previews[pitch].on)
              input.midi.addEvent(juce::MidiMessage::noteOn(1, static_cast<int>(pitch), input.previews[pitch].velocity), 0); }
        if (active && running.load()) for (const auto& note : active->notes) if (note.control->bus)
        {
            auto& input = *note.control->bus;
            const auto limit = std::min(start + samples, note.windowEnd);
            auto onset = note.onsetAt(std::max(start, note.windowStart));
            if (note.period > 0) onset -= note.period;
            while (onset < limit)
            {
                const auto from = std::max(onset, note.windowStart), to = std::min(onset + note.length, note.windowEnd);
                if (to > from)
                {
                    if ((from >= start && from < start + samples) || (input.chase && from < start && to > start))
                        input.midi.addEvent(juce::MidiMessage::noteOn(1, note.pitch, note.velocity), static_cast<int>(std::max<juce::int64>(0, from - start)));
                    if (to >= start && to < start + samples)
                        input.midi.addEvent(juce::MidiMessage::noteOff(1, note.pitch), static_cast<int>(to - start));
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
    const auto framesPerBeat = rateIn * 60 / tempo.load();
    for (int sample = 0; sample < samples; ++sample)
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
                * (entry.voice->midi ? entry.control->samplerTarget.load() : 1);
            if (pluginBlock && entry.control->bus)
            { const auto inputGain = (entry.reused ? 1.0f : progress) * (entry.voice->midi ? entry.control->samplerTarget.load() : 1);
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
                  const auto gain = entry.control->current * (1 - progress) * (entry.voice->midi ? entry.control->samplerTarget.load() : 1);
                  if (pluginBlock && entry.control->bus)
            { const auto inputGain = (1 - progress) * (entry.voice->midi ? entry.control->samplerTarget.load() : 1);
              entry.control->bus->audio.addSample(0, sample, l * inputGain); entry.control->bus->audio.addSample(1, sample, r * inputGain); }
            else
            { left += l * gain * panGain(entry.control->panTarget.load(), 0); right += r * gain * panGain(entry.control->panTarget.load(), 1); } }
            }
        if (clickEnabled)
        {
            const auto beat = std::floor(static_cast<double>(playbackFrame(frame)) / framesPerBeat);
            const auto seconds = (static_cast<double>(playbackFrame(frame)) - beat * framesPerBeat) / rateIn;
            if (seconds < 0.02)
            {
                const bool accent = std::fmod(beat, 4.0) < 0.5;
                const auto envelope = std::min(1.0, seconds / 0.001) * (1 - seconds / 0.02);
                const auto click = static_cast<float>((accent ? 0.22 : 0.14) * envelope
                    * std::sin(juce::MathConstants<double>::twoPi * (accent ? 1800 : 1200) * seconds));
                left += click; right += click;
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
    clock.store(frame);
    if (running.load()) for (const auto& note : active->notes)
        if (note.contains(playbackFrame(frame))) note.control->markNote(note.pitch);
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
juce::Result AudioEngine::startAudioRecording(const juce::File& file, double start)
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
        if (skip < samples)
        {
            if (!source || !recordWriter->write(&source, samples - skip)) recordFailed.store(true);
            else
            { if (recordFrames.load() == 0) recordFirstFrame.store(frame + skip); recordFrames.fetch_add(samples - skip); }
        }
    }
    render(output, channels, samples);
}
}
