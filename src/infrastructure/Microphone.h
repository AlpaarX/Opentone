// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
namespace opentone
{
#if JUCE_MAC
void requestMicrophonePermission(std::function<void(bool)>);
#else
inline void requestMicrophonePermission(std::function<void(bool)> reply)
{ juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, std::move(reply)); }
#endif
}
