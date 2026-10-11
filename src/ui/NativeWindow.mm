// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/NativeWindow.h"
#include <map>
#include <set>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
namespace opentone
{
namespace { std::map<int, unsigned short> physicalKeys; }
bool isKeyboardKeyDown(int key)
{
    key = static_cast<int>(juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key)));
    const auto found = physicalKeys.find(key);
    return found != physicalKeys.end() ? CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, found->second)
                                     : juce::KeyPress::isKeyCurrentlyDown(key);
}
void* installPluginKeyMonitor(juce::KeyListener& listener)
{
    auto* target = &listener;
    auto consumed = std::make_shared<std::set<unsigned short>>();
    id token = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp handler:^NSEvent*(NSEvent* event)
    {
        NSString* characters = event.charactersIgnoringModifiers;
        const bool space = event.keyCode == 49;
        if (!space && characters.length != 1) return event;
        const auto character = space ? static_cast<juce::juce_wchar>(' ') : [characters characterAtIndex:0];
        const auto physicalKey = static_cast<int>(juce::CharacterFunctions::toLowerCase(character));
        if (event.type == NSEventTypeKeyDown) physicalKeys[physicalKey] = event.keyCode;
        if (event.type == NSEventTypeKeyDown && event.isARepeat && consumed->count(event.keyCode)) return nil;
        juce::Component* source = nullptr;
        auto& desktop = juce::Desktop::getInstance();
        NSWindow* eventWindow = event.window ?: NSApp.keyWindow;
        for (NSWindow* candidate = eventWindow; candidate != nil && source == nullptr; candidate = candidate.parentWindow)
            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (auto* peer = desktop.getComponent(i)->getPeer())
                    if (static_cast<NSView*>(peer->getNativeHandle()).window == candidate) { source = desktop.getComponent(i); break; }
        if (!space && [eventWindow.firstResponder isKindOfClass:[NSTextView class]]) return event;
        int flags = 0;
        if (event.modifierFlags & NSEventModifierFlagShift) flags |= juce::ModifierKeys::shiftModifier;
        if (event.modifierFlags & NSEventModifierFlagCommand) flags |= juce::ModifierKeys::commandModifier;
        if (event.modifierFlags & NSEventModifierFlagControl) flags |= juce::ModifierKeys::ctrlModifier;
        if (event.modifierFlags & NSEventModifierFlagOption) flags |= juce::ModifierKeys::altModifier;
        const juce::KeyPress key(static_cast<int>(character), juce::ModifierKeys(flags), character);
        if (event.type == NSEventTypeKeyDown)
        { if (target->keyPressed(key, source)) { consumed->insert(event.keyCode); return nil; } }
        else
        { target->keyStateChanged(false, source); if (consumed->erase(event.keyCode)) return nil; }
        return event;
    }];
    return [token retain];
}
void removePluginKeyMonitor(void* token)
{ if (token) { [NSEvent removeMonitor:static_cast<id>(token)]; [static_cast<id>(token) release]; } }
void matchTitleBar(juce::Component& component, juce::Colour colour)
{
    if (auto* peer = component.getTopLevelComponent()->getPeer())
    {
        auto* view = static_cast<NSView*>(peer->getNativeHandle());
        NSWindow* window = view.window;
        if (window == nil) return;
        if (auto* document = dynamic_cast<juce::DocumentWindow*>(component.getTopLevelComponent()))
        {
            if (!document->isUsingNativeTitleBar())
            {
                const auto element = [](juce::Button* button) -> id
                {
                    if (button)
                        if (auto* handler = button->getAccessibilityHandler())
                            return static_cast<id>(handler->getNativeImplementation());
                    return nil;
                };
                // Window managers need the custom controls exposed as standard window actions.
                window.accessibilityCloseButton = element(document->getCloseButton());
                window.accessibilityMinimizeButton = element(document->getMinimiseButton());
                window.accessibilityZoomButton = element(document->getMaximiseButton());
                window.accessibilityFullScreenButton = element(document->getMaximiseButton());
                window.opaque = NO;
                window.backgroundColor = [NSColor clearColor];
                view.wantsLayer = YES;
                const CGFloat radius = document->isFullScreen() ? 0 : 10;
                const bool changed = std::abs(view.layer.cornerRadius - radius) > 0.01;
                [CATransaction begin];
                [CATransaction setDisableActions:YES];
                view.layer.opaque = NO;
                view.layer.masksToBounds = YES;
                view.layer.cornerRadius = radius;
                if (@available(macOS 10.15, *)) view.layer.cornerCurve = kCACornerCurveContinuous;
                [CATransaction commit];
                window.hasShadow = !document->isFullScreen();
                if (changed) [window invalidateShadow];
            }
            else
                window.backgroundColor = [NSColor colorWithSRGBRed:colour.getFloatRed() green:colour.getFloatGreen()
                                                             blue:colour.getFloatBlue() alpha:1.0];
        }
        window.appearance = [NSAppearance appearanceNamed:colour.getPerceivedBrightness() < 0.5f
                                                           ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    }
}
}
