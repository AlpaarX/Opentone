// SPDX-License-Identifier: AGPL-3.0-only
#include "app/UiModule.h"
#include "ui/Editor.h"

extern "C" OPENTONE_UI_EXPORT const opentone::UiApi* opentoneGetUiApi()
{
    static const opentone::UiApi api {
        opentone::uiAbiVersion,
        [](opentone::EditorPresenter& presenter) -> juce::Component*
        { return new opentone::Editor(presenter); },
        [](juce::Component& component) { return static_cast<opentone::Editor&>(component).canReload(); },
        [](juce::Component& component, void (*done)()) { static_cast<opentone::Editor&>(component).requestQuit(done); }
    };
    return &api;
}
