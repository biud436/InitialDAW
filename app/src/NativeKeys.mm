#include "NativeKeys.h"
#include "TypingKeyboard.h"

#import <AppKit/AppKit.h>

namespace daw
{

bool NativeKeys::available() { return true; }

NativeKeys::NativeKeys (juce::Component& owner, Handler handler) : owner_ (owner), handler_ (std::move (handler))
{
    auto* self = this;
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask: (NSEventMaskKeyDown | NSEventMaskKeyUp)
                                                       handler: ^NSEvent* (NSEvent* ev)
    {
        const int code = TypingKeyboard::asciiForVirtualKey ((int) [ev keyCode]);
        if (code == 0 || self->handler_ == nullptr)
            return ev;
        const bool down = [ev type] == NSEventTypeKeyDown;
        const auto flags = [ev modifierFlags];
        if (down && (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption)) != 0)
            return ev;   // 단축키
        auto* peer = self->owner_.getPeer();
        auto* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;
        if (view == nil || [ev window] != [view window])
            return ev;   // 다른 창
        return self->handler_ (code, down, down && [ev isARepeat]) ? nil : ev;
    }];
    monitor_ = [monitor retain];
}

NativeKeys::~NativeKeys()
{
    if (monitor_ != nullptr)
    {
        [NSEvent removeMonitor: (id) monitor_];
        [(id) monitor_ release];
    }
}

} // namespace daw
