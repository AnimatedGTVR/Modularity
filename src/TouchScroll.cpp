#include "Engine.h"

// Finger drag-to-scroll for the editor/runtime ImGui UI on Android.
//
// ImGui has no built-in touch scrolling: a swipe just drags whatever widget is
// under your finger (or nothing). On a touchscreen that makes long inspectors,
// the hierarchy, the file browser, etc. basically unusable since you can't
// reach anything past the first screen. This recognizes a drag on a scrollable
// window and feeds that motion straight into the window's scroll offset, while
// swallowing the click so the same gesture doesn't also poke a button or grab a
// slider. A short stationary tap still clicks normally.
//
// Lives in its own translation unit because it needs imgui_internal.h (window
// scroll state, HoveredWindow, ClearActiveID) which the rest of Engine.cpp
// deliberately doesn't pull in.

#ifdef __ANDROID__

#include "ThirdParty/ModuGUI/imgui.h"
#include "ThirdParty/ModuGUI/imgui_internal.h"

#include <cmath>

bool Engine::updateAndroidTouchScroll(float px, float py, bool active) {
    ImGuiContext &g = *ImGui::GetCurrentContext();
    ImGuiIO &io = g.IO;

    // How long the finger must sit still before a hold becomes a "right-click" so
    // touch can reach the editor's many right-click context menus (hierarchy, etc).
    constexpr double kLongPressSeconds = 0.30;

    // Gesture state carried across frames for the single primary finger.
    static bool tracking = false;
    static bool decided = false;   // have we classified tap vs. scroll yet?
    static bool isScroll = false;  // classified as a scroll drag
    static ImVec2 startPos(0.0f, 0.0f);
    static ImVec2 lastPos(0.0f, 0.0f);
    static ImGuiWindow *target = nullptr;
    static double holdStart = 0.0;     // g.Time at first contact
    static int longPressPhase = 0;     // 0 none, 1 right-down emitted, 2 released
    static bool longPressDead = false; // drifted/scrolled: no context menu this touch

    if (!active) {
        // If a finger lifts mid-pulse (down emitted, up not yet), release it so the
        // synthetic right button never sticks.
        if (longPressPhase == 1) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        }
        tracking = false;
        decided = false;
        isScroll = false;
        target = nullptr;
        longPressPhase = 0;
        longPressDead = false;
        return false; // nothing held; let the normal (released) button feed run
    }

    const float dpi = (uiDpiScale > 0.0f) ? uiDpiScale : 1.0f;
    // Slop before we commit to "this is a drag" - keeps taps feeling like taps.
    const float slop = 16.0f * dpi;

    if (!tracking) {
        tracking = true;
        decided = false;
        isScroll = false;
        startPos = lastPos = ImVec2(px, py);
        target = g.HoveredWindow;
        holdStart = g.Time;
        longPressPhase = 0;
        longPressDead = false;
        return true; // first contact: allow the tap through until proven a drag
    }

    const ImVec2 delta(px - lastPos.x, py - lastPos.y);
    lastPos = ImVec2(px, py);

    // Keep the target fresh until we commit: HoveredWindow may lag the touch by a
    // frame right after contact, so re-read it as the finger settles.
    if (!decided && g.HoveredWindow != nullptr) {
        target = g.HoveredWindow;
    }

    // --- Long-press -> right-click ---------------------------------------------
    // A right-click "click" only registers if ImGui sees a press AND a release, so
    // emit the down on the frame the hold matures, then the up on the very next
    // frame (that release is what BeginPopupContextItem/Window waits on). After
    // that we just hold and swallow the left tap for the rest of the gesture.
    if (longPressPhase == 1) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        longPressPhase = 2;
        return false;
    }
    if (longPressPhase == 2) {
        return false;
    }
    if (!longPressDead) {
        const float ex = px - startPos.x;
        const float ey = py - startPos.y;
        if (ex * ex + ey * ey >= slop * slop) {
            longPressDead = true; // drifted before maturing: a normal tap/scroll
        } else if (!io.WantTextInput && g.HoveredWindow != nullptr &&
                   (g.Time - holdStart) >= kLongPressSeconds) {
            // The press may have grabbed a widget on the down frame; drop it so the
            // right-click lands cleanly instead of dragging that widget.
            if (g.ActiveId != 0) {
                ImGui::ClearActiveID();
            }
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
            longPressPhase = 1;
            return false; // swallow the left tap: this hold became a right-click
        }
    }

    if (!decided) {
        const float ex = px - startPos.x;
        const float ey = py - startPos.y;
        if (ex * ex + ey * ey >= slop * slop) {
            decided = true;
            ImGuiWindow *w = target;
            const bool canScroll =
                w != nullptr && (w->ScrollMax.y > 0.0f ||
                                 ((w->Flags & ImGuiWindowFlags_HorizontalScrollbar) &&
                                  w->ScrollMax.x > 0.0f));
            // Don't hijack drags meant for a text field caret/selection.
            isScroll = canScroll && !io.WantTextInput;
        }
    }

    if (decided && isScroll && target != nullptr) {
        // The press may have already grabbed a widget on the down frame; drop it
        // so the drag scrolls instead of dragging that widget.
        if (g.ActiveId != 0) {
            ImGui::ClearActiveID();
        }
        if (target->ScrollMax.y > 0.0f) {
            ImGui::SetScrollY(target, target->Scroll.y - delta.y);
        }
        // Only scroll sideways on windows that actually opt into a horizontal
        // scrollbar. Otherwise a finger drag on a panel whose text merely overflows
        // (e.g. the Inspector) would scroll it left/right, which feels broken - that
        // text is meant to wrap, not pan.
        if ((target->Flags & ImGuiWindowFlags_HorizontalScrollbar) &&
            target->ScrollMax.x > 0.0f) {
            ImGui::SetScrollX(target, target->Scroll.x - delta.x);
        }
        return false; // swallow the button: this gesture is a scroll, not a tap
    }

    // Still within slop, or classified as a tap: keep feeding the press so a
    // genuine tap clicks normally.
    return !decided || !isScroll;
}

#else

bool Engine::updateAndroidTouchScroll(float, float, bool) { return true; }

#endif // __ANDROID__
