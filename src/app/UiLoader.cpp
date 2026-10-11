// SPDX-License-Identifier: AGPL-3.0-only
#include "app/UiLoader.h"
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace opentone
{
UiLoader::UiLoader(juce::File module) : source(std::move(module)), ready(source.getSiblingFile("ui-ready")) {}
UiLoader::~UiLoader() { for (const auto& file : copies) file.deleteFile(); }
bool UiLoader::changed() const { return ready.existsAsFile() && ready.loadFileAsString() != loadedToken; }
const UiApi* UiLoader::load()
{
    loadedToken = ready.loadFileAsString();
    const auto copy = source.getSiblingFile("reload-" + juce::Uuid().toString() + source.getFileExtension());
    void* handle = nullptr;
    const auto fail = [&](const juce::String& reason) -> const UiApi*
    { lastError = reason;
#if JUCE_WINDOWS
      if (handle) FreeLibrary(static_cast<HMODULE>(handle));
#else
      if (handle) dlclose(handle);
#endif
      copy.deleteFile(); juce::Logger::writeToLog(reason); return nullptr; };
    if (!source.copyFileTo(copy)) return fail("UI module could not be copied: " + source.getFullPathName());
    using Entry = const UiApi* (*)();
#if JUCE_WINDOWS
    handle = LoadLibraryW(copy.getFullPathName().toWideCharPointer());
    auto entry = handle ? reinterpret_cast<Entry>(GetProcAddress(static_cast<HMODULE>(handle), "opentoneGetUiApi")) : nullptr;
#else
    handle = dlopen(copy.getFullPathName().toRawUTF8(), RTLD_LOCAL | RTLD_NOW);
    auto entry = handle ? reinterpret_cast<Entry>(dlsym(handle, "opentoneGetUiApi")) : nullptr;
#endif
    if (!handle) return fail("UI module could not be loaded: " + source.getFullPathName());
    if (!entry) return fail("UI module is missing its entry point.");
    const auto* api = entry();
    if (!api || api->version != uiAbiVersion || !api->create || !api->canReload || !api->requestQuit)
        return fail("UI module ABI is incompatible; rebuild and restart the development host.");
    copies.push_back(copy);
    // shortcut: modules stay loaded until process exit for undo/async callbacks, add an ownership drain before unloading.
    lastError.clear();
    return api;
}
}
