// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "app/UiModule.h"

namespace opentone
{
class UiLoader
{
public:
    explicit UiLoader(juce::File module);
    ~UiLoader();
    bool changed() const;
    const UiApi* load();
    const juce::String& error() const { return lastError; }
private:
    juce::File source, ready;
    juce::String loadedToken, lastError;
    std::vector<juce::File> copies;
};
}
