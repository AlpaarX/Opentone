// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace opentone
{
#if JUCE_MAC
void matchTitleBar(juce::Component&, juce::Colour);
void* installPluginKeyMonitor(juce::KeyListener&);
void removePluginKeyMonitor(void*);
bool isKeyboardKeyDown(int key);
#else
inline void matchTitleBar(juce::Component&, juce::Colour) {}
inline void* installPluginKeyMonitor(juce::KeyListener&) { return nullptr; }
inline void removePluginKeyMonitor(void*) {}
inline bool isKeyboardKeyDown(int key) { return juce::KeyPress::isKeyCurrentlyDown(key); }
#endif
}
