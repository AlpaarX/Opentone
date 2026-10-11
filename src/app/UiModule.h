// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "application/EditorPresenter.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace opentone
{
// Change this ABI only together with the host; Editor.h is private to the UI module.
struct UiApi
{
    uint32_t version;
    juce::Component* (*create)(EditorPresenter&);
    bool (*canReload)(juce::Component&);
    void (*requestQuit)(juce::Component&, void (*done)());
};
constexpr uint32_t uiAbiVersion = 22;
}

#if defined(_WIN32) && defined(OPENTONE_UI_MODULE_BUILD)
 #define OPENTONE_UI_EXPORT __declspec(dllexport)
#else
 #define OPENTONE_UI_EXPORT
#endif
extern "C" OPENTONE_UI_EXPORT const opentone::UiApi* opentoneGetUiApi();
