// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "domain/Project.h"
namespace opentone
{
inline std::optional<Clip> clipInTimeSelection(Clip clip, const TimeSelection& selection)
{
    if (!selection.tracks.contains(clip.track)) return {};
    const auto from = std::max(clip.start, selection.start), to = std::min(clip.start + clip.duration, selection.end);
    if (to <= from) return {};
    clip.offset += from - clip.start; clip.start = from; clip.duration = to - from;
    return clip;
}
inline double timelineGridStep(double bpm, double pixelsPerSecond)
{
    auto step = 60.0 / bpm;
    if (!std::isfinite(step) || step <= 0 || !std::isfinite(pixelsPerSecond) || pixelsPerSecond <= 0) return 1;
    while (step * pixelsPerSecond > 32) step /= 2;
    while (step * pixelsPerSecond < 16) step *= 2;
    return step;
}
inline double snapToGrid(double seconds, double step, bool snap)
{
    const auto value = std::max(0.0, seconds);
    if (!snap || !std::isfinite(step) || step <= 0) return value;
    const auto remainder = std::fmod(value, step);
    return value - remainder + (remainder >= step / 2 ? step : 0);
}
inline double snapTime(double seconds, double bpm, bool snap)
{ return snapToGrid(seconds, 60.0 / bpm, snap); }
inline double midiSourceTime(const Clip& clip, double elapsed = 0)
{
    const auto source = clip.offset + elapsed;
    if (!clip.loop || clip.loopLength <= 0) return source;
    const auto phase = std::fmod(source - clip.loopStart, clip.loopLength);
    return clip.loopStart + (phase < 0 ? phase + clip.loopLength : phase);
}
struct MidiPattern
{
    double onset, duration, period;
};
inline std::optional<MidiPattern> midiPattern(const Clip& clip, const MidiNote& note)
{
    if (note.velocity <= 0) return {};
    if (clip.loop && clip.loopLength > 0)
    {
        if (note.start < clip.loopStart || note.start >= clip.loopStart + clip.loopLength) return {};
        auto onset = clip.start + note.start - midiSourceTime(clip);
        if (onset > clip.start) onset -= clip.loopLength;
        return MidiPattern { onset, std::min(note.duration, clip.loopStart + clip.loopLength - note.start), clip.loopLength };
    }
    return MidiPattern { clip.start + note.start - clip.offset, note.duration, 0 };
}
template <typename Draw> void forEachMidiOccurrence(const Clip& clip, const MidiNote& note, double from, double to, Draw draw)
{
    const auto pattern = midiPattern(clip, note); if (!pattern) return;
    from = std::max(from, clip.start); to = std::min(to, clip.start + clip.duration); if (to <= from) return;
    auto onset = pattern->onset;
    if (pattern->period > 0) onset += std::floor((from - onset) / pattern->period) * pattern->period;
    while (onset < to)
    {
        const auto begin = std::max({ onset, clip.start, from }), end = std::min({ onset + pattern->duration, clip.start + clip.duration, to });
        if (end > begin) draw(begin, end);
        if (pattern->period <= 0) break;
        onset += pattern->period;
    }
}
inline Clip trimLeft(Clip clip, double start)
{
    if (clip.loop && clip.loopLength > 0)
    {
        const auto delta = juce::jlimit(-clip.start, clip.duration - 1.0e-6, start - clip.start);
        clip.offset = midiSourceTime(clip, delta); clip.start += delta; clip.duration -= delta; return clip;
    }
    const auto delta = juce::jlimit(-clip.offset, clip.duration - 1.0e-6, start - clip.start);
    const auto allowed = std::max(-clip.start, delta);
    clip.start += allowed; clip.offset += allowed; clip.duration -= allowed; return clip;
}
inline Clip trimRight(Clip clip, double end, double sourceDuration)
{ clip.duration = juce::jlimit(1.0e-6, sourceDuration - clip.offset, end - clip.start); return clip; }
}
