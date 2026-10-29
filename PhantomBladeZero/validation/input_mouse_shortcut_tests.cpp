#include "../src/input_mouse_shortcuts.hpp"
#include <iostream>

namespace
{
    using gloss::input::MouseShortcutState;
    int failures{};
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)

    void TestLegacyAndRawCopiesDispatchOnlyOnceInBothOrders()
    {
        MouseShortcutState state;
        // Default Windows ordering: raw predicts consumption; legacy dispatches.
        auto edge = state.Raw(1, 0, true, false, true, true);
        CHECK(!edge.dispatch && edge.consumed);
        edge = state.Legacy(0, true, false, true);
        CHECK(edge.dispatch && !edge.consumed);
        state.CompleteLegacy(0, true);
        CHECK(!state.Legacy(0, true, false, true).dispatch); // Held/double down.
        CHECK(state.Raw(1, 0, false, false, true, false).consumed);
        CHECK(state.Legacy(0, false, false, true).consumed);
        CHECK(state.devices.empty());

        // Legacy first: a capture may have already stopped before the raw copy.
        edge = state.Legacy(1, true, false, true);
        CHECK(edge.dispatch);
        state.CompleteLegacy(1, true);
        edge = state.Raw(1, 1, true, false, true, false);
        CHECK(!edge.dispatch && edge.consumed);
        CHECK(state.Legacy(1, false, false, true).consumed);
        CHECK(state.Raw(1, 1, false, false, true, false).consumed);
        CHECK(!state.Legacy(1, false, false, true).consumed);
    }

    void TestRawOnlyAndMultipleDevicesKeepThePressOwnedUntilRelease()
    {
        MouseShortcutState state;
        auto edge = state.Raw(1, 2, true, true, true, false);
        CHECK(edge.dispatch && !edge.consumed);
        state.CompleteRaw(1, 2, true);
        edge = state.Raw(1, 2, true, true, true, false);
        CHECK(!edge.dispatch && edge.consumed);
        edge = state.Raw(2, 2, true, true, true, false);
        CHECK(!edge.dispatch && edge.consumed);
        // A spurious legacy copy of a raw-only event must not execute again.
        edge = state.Legacy(2, true, true, true);
        CHECK(!edge.dispatch && edge.consumed);
        CHECK(state.Raw(1, 2, false, true, true, false).consumed);
        CHECK(state.Raw(2, 2, false, true, true, false).consumed);
        CHECK(state.Legacy(2, false, true, true).consumed);
        CHECK(state.devices.empty());
        CHECK(state.Raw(2, 2, true, true, true, false).dispatch);
    }

    void TestUnboundMenuClickAndRecordingActivationDoNotCaptureTheirOwnRelease()
    {
        MouseShortcutState state;
        auto edge = state.Raw(1, 0, true, false, true, false);
        CHECK(!edge.dispatch && !edge.consumed);
        edge = state.Legacy(0, true, false, true);
        CHECK(edge.dispatch);
        state.CompleteLegacy(0, false); // Normal menu click, no recorder armed yet.
        CHECK(!state.Raw(1, 0, false, false, true, false).consumed);
        edge = state.Legacy(0, false, false, true);
        CHECK(!edge.dispatch && !edge.consumed);
        // ImGui activates BeginCapture on that release; it takes the next down.
        CHECK(state.Raw(1, 0, true, false, true, true).consumed);
        edge = state.Legacy(0, true, false, true);
        CHECK(edge.dispatch);
        state.CompleteLegacy(0, true);
        CHECK(state.Legacy(0, false, false, true).consumed);
    }

    void TestFocusResetAndMessageMapping()
    {
        MouseShortcutState state;
        CHECK(!state.Legacy(4, true, false, false).dispatch);
        state.Reset();
        CHECK(state.Legacy(4, true, false, true).dispatch);
        state.CompleteLegacy(4, true);
        state.Reset();
        CHECK(!state.Legacy(4, false, false, true).consumed);
        std::size_t index{};
        bool down{};
        CHECK(gloss::input::MouseButtonMessage(WM_LBUTTONDBLCLK, 0, index, down) && index == 0 && down);
        CHECK(gloss::input::MouseButtonMessage(WM_RBUTTONUP, 0, index, down) && index == 1 && !down);
        CHECK(gloss::input::MouseButtonMessage(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), index, down) && index == 4 && down);
        CHECK(!gloss::input::MouseButtonMessage(WM_MOUSEMOVE, 0, index, down));
        CHECK(!gloss::input::MouseButtonMessage(WM_XBUTTONDOWN, 0, index, down));
    }

    void TestRawFilteringKeepsOtherButtonsAxesAndWheel()
    {
        RAWMOUSE raw{};
        raw.usFlags = MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP;
        raw.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_WHEEL;
        raw.usButtonData = WHEEL_DELTA;
        raw.ulRawButtons = 3;
        raw.lLastX = 81;
        raw.lLastY = -34;
        gloss::input::FilterMouseButtons(raw, RI_MOUSE_LEFT_BUTTON_DOWN);
        CHECK(raw.usButtonFlags == (RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_WHEEL));
        CHECK(raw.ulRawButtons == 2);
        CHECK(raw.usFlags == (MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP));
        CHECK(raw.usButtonData == WHEEL_DELTA && raw.lLastX == 81 && raw.lLastY == -34);
    }
}

int main()
{
    TestLegacyAndRawCopiesDispatchOnlyOnceInBothOrders();
    TestRawOnlyAndMultipleDevicesKeepThePressOwnedUntilRelease();
    TestUnboundMenuClickAndRecordingActivationDoNotCaptureTheirOwnRelease();
    TestFocusResetAndMessageMapping();
    CHECK(!gloss::input::ForwardOverlayMessage(false, WM_MOUSEMOVE));
    CHECK(!gloss::input::ForwardOverlayMessage(false, WM_SETCURSOR));
    CHECK(!gloss::input::ForwardOverlayMessage(false, WM_KEYUP));
    CHECK(gloss::input::ForwardOverlayMessage(false, WM_SETFOCUS));
    CHECK(gloss::input::ForwardOverlayMessage(false, WM_KILLFOCUS));
    CHECK(gloss::input::ForwardOverlayMessage(true, WM_MOUSEMOVE));
    CHECK(gloss::input::ForwardOverlayMessage(true, WM_KEYUP));
    TestRawFilteringKeepsOtherButtonsAxesAndWheel();
    if (failures) return 1;
    std::cout << "GlossTrainer mouse shortcut tests passed (0 failures)\n";
    return 0;
}
