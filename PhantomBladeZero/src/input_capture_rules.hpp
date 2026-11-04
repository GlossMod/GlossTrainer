#pragma once
#include <utility>

namespace gloss::input {
    // GetCapture identifies a window, not the plugin which acquired it. Claim only
    // a transition caused by our Win32 backend; never claim an existing capture.
    struct MenuMouseCapture {
        bool owned{};
        void ObserveBackend(bool capturedBefore, bool capturedAfter) {
            if (!capturedAfter) owned = false;
            else if (!capturedBefore) owned = true;
        }
        void Lost() { owned = false; }
        bool ReleaseIfOwned(bool targetCaptured) {
            return std::exchange(owned, false) && targetCaptured;
        }
    };
}
