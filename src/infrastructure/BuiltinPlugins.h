// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "domain/Project.h"
#include <map>
namespace opentone
{
juce::Array<juce::PluginDescription> builtinPluginDescriptions();
void addDefaultReturns(Project&);
Plugin builtinPluginModel(const juce::String&, const std::map<juce::String, float>& parameters = {});
std::unique_ptr<juce::AudioProcessor> createBuiltinPlugin(const juce::String& identifier);
}
