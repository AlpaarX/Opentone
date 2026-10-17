// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "domain/Project.h"
#include <map>
namespace opentone
{
juce::Array<juce::PluginDescription> builtinPluginDescriptions();
void addDefaultReturns(Project&);
struct DrumRackBranch
{
    juce::String name; int receive = -1, send = 60; float gain = 1, pan = 0; bool mute = false;
    std::vector<Plugin> devices;
};
Plugin builtinDrumRackModel(const std::vector<DrumRackBranch>&);
void setBuiltinSidechain(juce::AudioProcessor&, const juce::AudioBuffer<float>*);
juce::StringArray samplerPresetNames();
juce::StringArray builtinPresetNames(const juce::String& identifier);
juce::Result writeSamplerPreset(int index, const juce::File& destination);
Plugin builtinPluginModel(const juce::String&, const std::map<juce::String, float>& parameters = {}, int preset = -1);
std::unique_ptr<juce::AudioProcessor> createBuiltinPlugin(const juce::String& identifier);
}
