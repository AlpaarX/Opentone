// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ui/UiTheme.h"
namespace opentone
{
// Read only the visible source range; painting never opens or decodes a file.
class WaveformDetail final : public juce::ChangeBroadcaster
{
public:
    ~WaveformDetail() override { worker.removeAllJobs(true, -1); }
    bool draw(juce::Graphics& graphics, const juce::String& file, juce::Rectangle<float> area,
              double from, double to, double sampleRate, juce::Colour colour)
    {
        if (area.isEmpty() || to <= from || sampleRate <= 0 || (to - from) * sampleRate / area.getWidth() >= 512) return false;
        auto& entry = entries[file];
        if (!entry) entry = std::make_shared<Entry>();
        std::shared_ptr<Data> data;
        {
            const juce::ScopedLock guard(lock);
            data = entry->ready;
            if (!covers(data, from, to, sampleRate) && !covers(entry->pending, from, to, sampleRate))
            {
                auto request = std::make_shared<Data>();
                request->rate = sampleRate;
                request->first = static_cast<juce::int64>(std::max(0.0, std::floor((from - (to - from) * 0.25) * sampleRate) - 2));
                request->count = static_cast<int>(std::min(2000000.0, std::ceil(to * sampleRate) - static_cast<double>(request->first) + (to - from) * sampleRate * 0.25 + 3));
                entry->pending = request;
                worker.addJob([this, target = entry, request, file]
                {
                    { const juce::ScopedLock check(lock); if (target->pending != request) return; }
                    juce::AudioFormatManager formats; formats.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(file)));
                    if (reader && reader->numChannels > 0 && reader->sampleRate == request->rate)
                    {
                        request->samples.setSize(static_cast<int>(std::min(2u, reader->numChannels)), request->count);
                        if (!reader->read(&request->samples, 0, request->count, request->first, true, true)) request->samples.setSize(0, 0);
                    }
                    { const juce::ScopedLock check(lock);
                      if (target->pending != request) return;
                      target->ready = request; }
                    sendChangeMessage();
                });
            }
        }
        // Bound memory when navigating many source files; active jobs own their entries.
        if (entries.size() > 16) for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->first != file) { entries.erase(it); break; }
        if (!covers(data, from, to, sampleRate) || data->samples.getNumSamples() == 0) return false;
        juce::Graphics::ScopedSaveState saved(graphics); graphics.reduceClipRegion(area.toNearestInt());
        const auto first = std::max(0, static_cast<int>(std::floor(from * sampleRate - static_cast<double>(data->first))) - 1);
        const auto last = std::min(data->count - 1, static_cast<int>(std::ceil(to * sampleRate - static_cast<double>(data->first))) + 1);
        const auto pixelsPerSample = static_cast<double>(area.getWidth()) / ((to - from) * sampleRate);
        const auto channelHeight = area.getHeight() / static_cast<float>(data->samples.getNumChannels());
        for (int channel = 0; channel < data->samples.getNumChannels(); ++channel)
        {
            const auto centre = area.getY() + (static_cast<float>(channel) + 0.5f) * channelHeight;
            const auto amplitude = channelHeight * 0.46f;
            graphics.setColour(colour.withMultipliedAlpha(0.22f));
            graphics.drawHorizontalLine(juce::roundToInt(centre), area.getX(), area.getRight());
            graphics.setColour(colour);
            const auto* samples = data->samples.getReadPointer(channel);
            const auto xAt = [&](int i) { return area.getX() + static_cast<float>((static_cast<double>(data->first) + i - from * sampleRate) * pixelsPerSample); };
            const auto yAt = [&](int i) { return centre - juce::jlimit(-1.0f, 1.0f, samples[i]) * amplitude; };
            juce::Path curve;
            if (pixelsPerSample >= 0.5)
            {
                curve.startNewSubPath(xAt(first), yAt(first));
                for (int i = first; i < last; ++i)
                {
                    const auto before = yAt(std::max(0, i - 1)), after = yAt(std::min(data->count - 1, i + 2));
                    const auto dx = static_cast<float>(pixelsPerSample / 3);
                    curve.cubicTo(xAt(i) + dx, yAt(i) + (yAt(i + 1) - before) / 6,
                                  xAt(i + 1) - dx, yAt(i + 1) - (after - yAt(i)) / 6, xAt(i + 1), yAt(i + 1));
                }
                graphics.strokePath(curve, juce::PathStrokeType(1.0f));
                if (pixelsPerSample >= 4) for (int i = first; i <= last; ++i) graphics.fillEllipse(xAt(i) - 2, yAt(i) - 2, 4, 4);
            }
            else
            {
                for (int pixel = 0; pixel < juce::roundToInt(area.getWidth()); ++pixel)
                {
                    const int begin = std::max(first, static_cast<int>(std::floor(from * sampleRate - static_cast<double>(data->first) + pixel / pixelsPerSample)));
                    const int end = std::min(last, static_cast<int>(std::ceil(from * sampleRate - static_cast<double>(data->first) + (pixel + 1) / pixelsPerSample)));
                    float low = 1, high = -1;
                    for (int i = begin; i <= end; ++i) { low = std::min(low, samples[i]); high = std::max(high, samples[i]); }
                    if (low <= high) graphics.drawVerticalLine(juce::roundToInt(area.getX()) + pixel, centre - high * amplitude, centre - low * amplitude + 1);
                }
            }
        }
        return true;
    }
private:
    struct Data { juce::AudioBuffer<float> samples; double rate = 0; juce::int64 first = 0; int count = 0; };
    struct Entry { std::shared_ptr<Data> ready, pending; };
    static bool covers(const std::shared_ptr<Data>& data, double from, double to, double rate)
    { return data && data->rate == rate && static_cast<double>(data->first) <= from * rate
        && static_cast<double>(data->first) + data->count >= std::ceil(to * rate) + 1; }
    juce::CriticalSection lock;
    std::map<juce::String, std::shared_ptr<Entry>> entries;
    juce::ThreadPool worker {1};
};
}
