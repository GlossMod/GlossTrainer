#include "ime.hpp"
#include "runtime.hpp"
#include <imm.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace gloss::input::ime {
namespace {
struct State {
    HWND window{};
    DWORD thread{};
    std::mutex requestMutex;
    TextInput requested;
    // All fields below this line belong exclusively to the window thread.
    HIMC context{}, previous{};
    bool associated{}, transitioning{}, syncing{}, initialModeSet{};
    bool rawAttempted{};
    std::optional<RAWINPUTDEVICE> previousRaw, temporaryRaw;
};
std::mutex statesMutex;
std::map<HWND, std::shared_ptr<State>> states;

std::shared_ptr<State> Find(HWND window) {
    std::lock_guard lock(statesMutex);
    const auto found = states.find(window);
    return found == states.end() ? nullptr : found->second;
}
bool WindowThread(const State& state) { return GetCurrentThreadId() == state.thread; }
HIMC CurrentContext(HWND window) {
    const auto context = ImmGetContext(window);
    if (context) ImmReleaseContext(window, context);
    return context;
}
enum class RegistrationResult { Failed, Missing, Found };
RegistrationResult KeyboardRegistration(RAWINPUTDEVICE& result) {
    UINT count{};
    if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != 0 || count > 256)
        return RegistrationResult::Failed;
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count && GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
        return RegistrationResult::Failed;
    for (const auto& device : devices) {
        if (device.usUsagePage == 1 && device.usUsage == 6) {
            result = device;
            return RegistrationResult::Found;
        }
    }
    return RegistrationResult::Missing;
}
bool SameRegistration(const RAWINPUTDEVICE& left, const RAWINPUTDEVICE& right) {
    return left.usUsagePage == right.usUsagePage && left.usUsage == right.usUsage &&
        left.dwFlags == right.dwFlags && left.hwndTarget == right.hwndTarget;
}
bool SyncRawKeyboard(State& state, bool textActive) {
    if (textActive) {
        if (state.rawAttempted) return false;
        state.rawAttempted = true;
        RAWINPUTDEVICE original{};
        if (KeyboardRegistration(original) != RegistrationResult::Found ||
            (original.dwFlags & RIDEV_NOLEGACY) != RIDEV_NOLEGACY) return false;
        auto temporary = original;
        // APPKEYS requires NOLEGACY. Keep the target and every other flag, and
        // never modify mouse registration or another usage's registration.
        temporary.dwFlags &= ~(RIDEV_NOLEGACY | RIDEV_APPKEYS);
        if (!RegisterRawInputDevices(&temporary, 1, sizeof(temporary))) {
            Log("IME: could not enable legacy keyboard messages for text input");
            return false;
        }
        state.previousRaw = original;
        state.temporaryRaw = temporary;
        Log("IME: legacy keyboard messages enabled while editing text");
        return true;
    }
    state.rawAttempted = false;
    if (!state.previousRaw || !state.temporaryRaw) return false;
    RAWINPUTDEVICE current{};
    const auto status = KeyboardRegistration(current);
    if (status == RegistrationResult::Failed) return false;
    if (status == RegistrationResult::Found && SameRegistration(current, *state.temporaryRaw)) {
        if (!RegisterRawInputDevices(&*state.previousRaw, 1, sizeof(RAWINPUTDEVICE))) {
            Log("IME: could not restore the previous keyboard registration");
            return false;
        }
        Log("IME: previous keyboard registration restored");
    } else {
        // Another plugin/game changed its registration after ours. Its newer
        // choice takes precedence over our saved snapshot.
        Log("IME: keyboard registration changed externally; keeping the newer registration");
    }
    state.previousRaw.reset();
    state.temporaryRaw.reset();
    return true;
}
void InitializeMode(State& state, HIMC original) {
    if (state.initialModeSet) return;
    // A game may have disassociated its default context with NULL. Briefly
    // retrieve the thread's default context to inherit the user's current
    // Chinese/English mode, then put the exact prior association back.
    auto source = original;
    if (!source && ImmAssociateContextEx(state.window, nullptr, IACE_DEFAULT)) {
        source = CurrentContext(state.window);
        if (source) {
            DWORD conversion{}, sentence{};
            const auto open = ImmGetOpenStatus(source);
            const auto hasConversion = ImmGetConversionStatus(source, &conversion, &sentence);
            ImmAssociateContext(state.window, original);
            ImmSetOpenStatus(state.context, open);
            if (hasConversion) ImmSetConversionStatus(state.context, conversion, sentence);
            state.initialModeSet = true;
            return;
        }
        ImmAssociateContext(state.window, original);
    }
    if (source) {
        DWORD conversion{}, sentence{};
        const auto open = ImmGetOpenStatus(source);
        const auto hasConversion = ImmGetConversionStatus(source, &conversion, &sentence);
        ImmSetOpenStatus(state.context, open);
        if (hasConversion) ImmSetConversionStatus(state.context, conversion, sentence);
    }
    state.initialModeSet = true;
}
void Associate(State& state) {
    if (state.associated && CurrentContext(state.window) == state.context) return;
    if (!state.context) state.context = ImmCreateContext();
    if (!state.context) {
        Log("IME: input context creation failed");
        return;
    }
    const auto previous = CurrentContext(state.window);
    InitializeMode(state, previous);
    state.previous = ImmAssociateContext(state.window, state.context);
    state.associated = CurrentContext(state.window) == state.context;
    if (state.associated) Log("IME: menu input context associated on the window thread");
    else Log("IME: menu input context association failed");
}
void Restore(State& state) {
    if (!state.associated) return;
    if (CurrentContext(state.window) == state.context) {
        // Cancel, never commit unfinished text to either the next field or the
        // game. These calls may synchronously reenter our window procedure.
        ImmNotifyIME(state.context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmAssociateContext(state.window, state.previous);
        if (CurrentContext(state.window) == state.context) {
            // Keep ownership and the saved association intact for a later
            // retry. Destroying a still-associated context would leave the
            // game's HWND holding an invalid handle.
            Log("IME: context restoration failed; retaining the owned context");
            return;
        }
        Log("IME: previous window input context restored");
    }
    state.associated = false;
    state.previous = nullptr;
}
void Position(State& state, const TextInput& input) {
    if (!state.associated || !input.wantVisible) return;
    const LONG height = std::max<LONG>(1, input.lineHeight);
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = input.position;
    ImmSetCompositionWindow(state.context, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = {input.position.x, input.position.y + height};
    candidate.rcArea = {input.position.x, input.position.y, input.position.x + 1, input.position.y + height};
    ImmSetCandidateWindow(state.context, &candidate);
}
}
void Attach(HWND window) {
    if (!window) return;
    std::lock_guard lock(statesMutex);
    if (states.contains(window)) return;
    auto state = std::make_shared<State>();
    state->window = window;
    state->thread = GetWindowThreadProcessId(window, nullptr);
    states.emplace(window, std::move(state));
}
void Request(HWND window, const TextInput& input) {
    if (const auto state = Find(window)) {
        {
            std::lock_guard lock(state->requestMutex);
            state->requested = input;
        }
        PostMessageW(window, UpdateMessage, 0, 0);
    }
}
void Clear(HWND window) { Request(window, {}); }
bool Sync(HWND window, bool menuActive) {
    const auto state = Find(window);
    if (!state || !WindowThread(*state) || state->syncing) return false;
    TextInput input;
    {
        std::lock_guard lock(state->requestMutex);
        input = state->requested;
    }
    state->syncing = state->transitioning = true;
    const bool textActive = menuActive && input.wantTextInput;
    if (textActive) {
        Associate(*state);
        Position(*state, input);
    } else Restore(*state);
    const bool rawChanged = SyncRawKeyboard(*state, textActive && state->associated);
    state->transitioning = state->syncing = false;
    return rawChanged;
}
bool OwnsMessages(HWND window) {
    const auto state = Find(window);
    return state && WindowThread(*state) && (state->associated || state->transitioning);
}
bool IsImeMessage(UINT message) {
    switch (message) {
    case WM_IME_SETCONTEXT: case WM_IME_STARTCOMPOSITION: case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION: case WM_IME_CHAR: case WM_IME_NOTIFY: case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL: case WM_IME_SELECT: case WM_IME_REQUEST:
    case WM_IME_KEYDOWN: case WM_IME_KEYUP:
    case WM_INPUTLANGCHANGEREQUEST: case WM_INPUTLANGCHANGE:
        return true;
    default: return false;
    }
}
LRESULT ProcessMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // In particular, WM_IME_COMPOSITION and WM_IME_CHAR must each take this
    // default path once. ImGui 1.92.9's backend also calls DefWindowProc for
    // composition, so the input router intercepts these before that backend.
    return DefWindowProcW(window, message, wparam, lparam);
}
void Detach(HWND window) {
    const auto state = Find(window);
    if (!state || !WindowThread(*state)) return;
    state->syncing = state->transitioning = true;
    Restore(*state);
    SyncRawKeyboard(*state, false);
    if (state->associated || (state->context && CurrentContext(window) == state->context)) {
        state->transitioning = state->syncing = false;
        Log("IME: cleanup deferred because the owned context is still associated");
        return;
    }
    if (state->context && !ImmDestroyContext(state->context)) {
        state->transitioning = state->syncing = false;
        Log("IME: owned context destruction failed; retaining its handle");
        return;
    }
    state->context = nullptr;
    state->transitioning = state->syncing = false;
    if (state->previousRaw || state->temporaryRaw) {
        // A failed restore still needs its original registration snapshot.
        // Keep it even after the owned context has been safely destroyed so a
        // later Detach can retry without losing or guessing the game settings.
        Log("IME: keyboard registration restoration deferred; retaining the saved registration");
        return;
    }
    std::lock_guard lock(statesMutex);
    states.erase(window);
}
}
