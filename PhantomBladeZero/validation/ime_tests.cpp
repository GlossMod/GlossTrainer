#include "../src/ime.hpp"
#include <imm.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace gloss
{
    void Log(std::string_view) {}
}

namespace
{
    namespace ime = gloss::input::ime;
    constexpr wchar_t WindowClassName[] = L"GlossTrainerImeFixtureWindow";
    int failures{};
    bool candidateReadbackSkipped{}, compositionReadbackSkipped{};

#define CHECK(expression)                                                                 \
    do                                                                                    \
    {                                                                                     \
        if (!(expression))                                                                \
        {                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
            ++failures;                                                                   \
        }                                                                                 \
    } while (false)

    void ReportPassIfClean(int startingFailures, std::string_view message)
    {
        if (failures == startingFailures)
            std::cout << "PASS " << message << '\n';
    }

    bool SameRawRegistration(const RAWINPUTDEVICE &left, const RAWINPUTDEVICE &right)
    {
        return left.usUsagePage == right.usUsagePage && left.usUsage == right.usUsage &&
               left.dwFlags == right.dwFlags && left.hwndTarget == right.hwndTarget;
    }

    bool ReadCandidateForm(HIMC context, CANDIDATEFORM &form)
    {
        if (ImmGetCandidateWindow(context, 0, &form))
            return true;
        if (!candidateReadbackSkipped)
            std::cout << "SKIP candidate-form readback: this IMM context/layout does not expose ImmGetCandidateWindow\n";
        candidateReadbackSkipped = true;
        return false;
    }

    bool ReadCompositionForm(HIMC context, COMPOSITIONFORM &form)
    {
        if (ImmGetCompositionWindow(context, &form))
            return true;
        if (!compositionReadbackSkipped)
            std::cout << "SKIP composition-form readback: this IMM context/layout does not expose ImmGetCompositionWindow\n";
        compositionReadbackSkipped = true;
        return false;
    }

    enum class RawQuery
    {
        Error,
        Missing,
        Found
    };
    RawQuery QueryKeyboardRegistration(RAWINPUTDEVICE &registration)
    {
        UINT count{};
        if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != 0 || count > 256)
            return RawQuery::Error;
        std::vector<RAWINPUTDEVICE> devices(count);
        if (count && GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
            return RawQuery::Error;
        for (const auto &device : devices)
        {
            if (device.usUsagePage == 1 && device.usUsage == 6)
            {
                registration = device;
                return RawQuery::Found;
            }
        }
        return RawQuery::Missing;
    }

    struct RawRegistrationSnapshot
    {
        RAWINPUTDEVICE original{};
        RawQuery result = QueryKeyboardRegistration(original);
        bool hadOriginal = result == RawQuery::Found;

        void Restore() const
        {
            if (result == RawQuery::Error)
                return;
            if (hadOriginal)
            {
                RegisterRawInputDevices(&original, 1, sizeof(original));
            }
            else
            {
                RAWINPUTDEVICE remove{1, 6, RIDEV_REMOVE, nullptr};
                RegisterRawInputDevices(&remove, 1, sizeof(remove));
            }
        }
        ~RawRegistrationSnapshot() { Restore(); }
    };

    struct KeyboardLayoutScope
    {
        HKL original = GetKeyboardLayout(0);
        HKL activeIme{};
        HKL loadedByTest{};

        KeyboardLayoutScope()
        {
            if (original && ImmIsIME(original))
            {
                activeIme = original;
                return;
            }
            const int count = GetKeyboardLayoutList(0, nullptr);
            if (count > 0 && count <= 256)
            {
                std::vector<HKL> layouts(count);
                const int actual = GetKeyboardLayoutList(count, layouts.data());
                for (int i = 0; i < actual; ++i)
                {
                    if (!ImmIsIME(layouts[i]))
                        continue;
                    if (ActivateKeyboardLayout(layouts[i], KLF_NOTELLSHELL))
                    {
                        activeIme = layouts[i];
                        return;
                    }
                }
            }

            // Try the Windows Simplified Chinese layout in this test process's
            // current thread only. If absent, the following IMM position checks
            // are reported as SKIP rather than treated as passed.
            loadedByTest = LoadKeyboardLayoutW(L"00000804", KLF_ACTIVATE | KLF_NOTELLSHELL);
            if (loadedByTest && ImmIsIME(loadedByTest))
                activeIme = loadedByTest;
            else if (loadedByTest)
            {
                ActivateKeyboardLayout(original, KLF_NOTELLSHELL);
                UnloadKeyboardLayout(loadedByTest);
                loadedByTest = nullptr;
            }
        }

        ~KeyboardLayoutScope()
        {
            if (original && GetKeyboardLayout(0) != original)
                ActivateKeyboardLayout(original, KLF_NOTELLSHELL);
            if (loadedByTest)
                UnloadKeyboardLayout(loadedByTest);
        }
        bool HasIme() const { return activeIme != nullptr; }
    };

    struct ImGuiFixture
    {
        ImGuiContext *context{};
        char text[64]{};
        ImGuiPlatformImeData platformIme{};
        ImVec2 viewportPosition{};
        ImVec2 inputMinimum{}, inputMaximum{};
        bool hasPlatformImeData{}, inputItemActive{};
        inline static ImGuiFixture *current{};

        static void CapturePlatformImeData(ImGuiContext *, ImGuiViewport *viewport, ImGuiPlatformImeData *data)
        {
            if (!current)
                return;
            current->platformIme = *data;
            current->viewportPosition = viewport->Pos;
            current->hasPlatformImeData = true;
        }

        void Frame()
        {
            auto &io = ImGui::GetIO();
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(340, 140), ImGuiCond_Always);
            ImGui::Begin("IME test fixture", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::InputText("Search", text, sizeof(text));
            inputItemActive = ImGui::IsItemActive();
            inputMinimum = ImGui::GetItemRectMin();
            inputMaximum = ImGui::GetItemRectMax();
            ImGui::End();
            ImGui::Render();
        }

        ImGuiFixture()
        {
            IMGUI_CHECKVERSION();
            context = ImGui::CreateContext();
            auto &io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(640, 480);
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
            current = this;
            ImGui::GetPlatformIO().Platform_SetImeDataFn = CapturePlatformImeData;
            io.AddFocusEvent(true);

            Frame();
            const ImVec2 click((inputMinimum.x + inputMaximum.x) * 0.5f,
                               (inputMinimum.y + inputMaximum.y) * 0.5f);
            io.AddMousePosEvent(click.x, click.y);
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
            // io.WantTextInput is an output for the next frame in 1.92.9.
            Frame();
        }
        ~ImGuiFixture()
        {
            if (context)
            {
                ImGui::SetCurrentContext(context);
                ImGui::DestroyContext(context);
            }
            if (current == this)
                current = nullptr;
        }
        bool Ready() const
        {
            ImGui::SetCurrentContext(context);
            return inputItemActive && ImGui::GetIO().WantTextInput && hasPlatformImeData &&
                   platformIme.WantTextInput;
        }
        ime::TextInput TextRequest() const
        {
            const POINT position{
                static_cast<LONG>(platformIme.InputPos.x - viewportPosition.x),
                static_cast<LONG>(platformIme.InputPos.y - viewportPosition.y)};
            return {platformIme.WantTextInput, platformIme.WantVisible, position,
                    static_cast<LONG>(platformIme.InputLineHeight)};
        }
    };

    struct WindowFixture
    {
        HWND window{};
        bool menuActive = true;
        int updateMessages{};
        int imeMessages{};
        int compositionMessages{};
        int backendCharMessages{};
        bool attached{};

        WindowFixture()
        {
            window = CreateWindowExW(0, WindowClassName, L"GlossTrainer IME test",
                                     WS_OVERLAPPED, 0, 0, 640, 480, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), this);
            CHECK(window != nullptr);
        }
        ~WindowFixture()
        {
            if (window)
            {
                // Detach is window-thread-only and restores the exact original HIMC.
                ime::Detach(window);
                attached = false;
                DestroyWindow(window);
            }
        }
        void Attach()
        {
            ime::Attach(window);
            attached = true;
        }
        void PumpUpdates()
        {
            MSG message{};
            while (PeekMessageW(&message, window, ime::UpdateMessage, ime::UpdateMessage, PM_REMOVE))
                DispatchMessageW(&message);
        }
        HIMC CurrentContext() const
        {
            const auto context = ImmGetContext(window);
            if (context)
                ImmReleaseContext(window, context);
            return context;
        }
    };

    LRESULT CALLBACK FixtureWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            const auto create = reinterpret_cast<CREATESTRUCTW *>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *fixture = reinterpret_cast<WindowFixture *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == ime::UpdateMessage && fixture)
        {
            ++fixture->updateMessages;
            ime::Sync(window, fixture->menuActive);
            return 0;
        }
        if (ime::IsImeMessage(message) && fixture)
        {
            ++fixture->imeMessages;
            if (message == WM_IME_COMPOSITION)
                ++fixture->compositionMessages;
            // This is the production routing contract: IME messages take one
            // default-processing path and bypass ImGui's Win32 handler.
            return ime::ProcessMessage(window, message, wparam, lparam);
        }
        if (message == WM_CHAR && fixture)
        {
            ++fixture->backendCharMessages;
            return ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    bool RegisterTestWindowClass()
    {
        WNDCLASSEXW klass{sizeof(klass)};
        klass.style = CS_IME;
        klass.lpfnWndProc = FixtureWindowProc;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = WindowClassName;
        if (RegisterClassExW(&klass))
            return true;
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    void TestDefaultContextAndOwnerThreadRequests(ImGuiFixture &imgui)
    {
        const int startingFailures = failures;
        WindowFixture fixture;
        if (!fixture.window)
            return;
        const HIMC original = fixture.CurrentContext();
        CHECK(original != nullptr);
        if (!original)
            return;
        fixture.Attach();

        ime::TextInput request = imgui.TextRequest();
        const POINT position = request.position;
        CHECK(request.wantTextInput);
        CHECK(request.wantVisible);

        // The request originates on a worker, while Sync runs only from the HWND
        // thread's posted-message handler.
        std::atomic_bool renderThreadSync{true};
        std::thread renderThread([window = fixture.window, request, &renderThreadSync]
                                 {
        ime::Request(window, request);
        renderThreadSync = ime::Sync(window, true); });
        renderThread.join();
        CHECK(!renderThreadSync);
        CHECK(fixture.CurrentContext() == original);
        fixture.PumpUpdates();
        CHECK(fixture.updateMessages == 1);
        const HIMC privateContext = fixture.CurrentContext();
        CHECK(privateContext != nullptr);
        CHECK(privateContext != original);

        if (privateContext)
        {
            CANDIDATEFORM candidate{};
            if (ReadCandidateForm(privateContext, candidate))
            {
                CHECK(candidate.dwStyle == CFS_EXCLUDE);
                CHECK(candidate.ptCurrentPos.x == position.x);
                CHECK(candidate.ptCurrentPos.y == position.y + request.lineHeight);
                CHECK(candidate.rcArea.left == position.x);
                CHECK(candidate.rcArea.top == position.y);
                CHECK(candidate.rcArea.right == position.x + 1);
                CHECK(candidate.rcArea.bottom == position.y + request.lineHeight);
            }

            COMPOSITIONFORM composition{};
            if (ReadCompositionForm(privateContext, composition))
            {
                CHECK(composition.dwStyle == CFS_FORCE_POSITION);
                CHECK(composition.ptCurrentPos.x == position.x);
                CHECK(composition.ptCurrentPos.y == position.y);
            }
        }

        // Losing text focus while the menu stays open restores the original
        // default context. Repeated focus transitions must reuse a stable context.
        for (int i = 0; i != 3; ++i)
        {
            ime::Request(fixture.window, {});
            fixture.PumpUpdates();
            CHECK(fixture.CurrentContext() == original);
            request.position.x += 17;
            request.position.y += 9;
            ime::Request(fixture.window, request);
            fixture.PumpUpdates();
            CHECK(fixture.CurrentContext() == privateContext);
            if (privateContext && !candidateReadbackSkipped)
            {
                CANDIDATEFORM moved{};
                if (ReadCandidateForm(privateContext, moved))
                {
                    CHECK(moved.ptCurrentPos.x == request.position.x);
                    CHECK(moved.ptCurrentPos.y == request.position.y + request.lineHeight);
                }
            }
        }

        fixture.menuActive = false;
        ime::Clear(fixture.window);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() == original);
        fixture.menuActive = true;
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() == privateContext);
        ime::Detach(fixture.window);
        fixture.attached = false;
        CHECK(fixture.CurrentContext() == original);
        ReportPassIfClean(startingFailures, "default HIMC, cross-thread request, repeated focus and hide restore");
        if (failures == startingFailures && !candidateReadbackSkipped)
            std::cout << "PASS candidate position follows updated ImGui requests\n";
        if (failures == startingFailures && !compositionReadbackSkipped)
            std::cout << "PASS composition position follows ImGui request\n";
    }

    void TestNullOriginalContext(ImGuiFixture &imgui)
    {
        const int startingFailures = failures;
        WindowFixture fixture;
        if (!fixture.window)
            return;
        const HIMC defaultContext = fixture.CurrentContext();
        CHECK(defaultContext != nullptr);
        const HIMC detached = ImmAssociateContext(fixture.window, nullptr);
        CHECK(detached == defaultContext);
        CHECK(fixture.CurrentContext() == nullptr);

        fixture.Attach();
        const ime::TextInput request = imgui.TextRequest();
        CHECK(request.wantTextInput);
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() != nullptr);

        fixture.menuActive = false;
        ime::Clear(fixture.window);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() == nullptr);
        fixture.menuActive = true;
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() != nullptr);
        ime::Detach(fixture.window);
        fixture.attached = false;
        CHECK(fixture.CurrentContext() == nullptr);

        // Restore the system-created default association for fixture teardown.
        ImmAssociateContext(fixture.window, defaultContext);
        CHECK(fixture.CurrentContext() == defaultContext);
        ReportPassIfClean(startingFailures, "NULL original HIMC is restored exactly after focus loss and detach");
    }

    void TestIndependentOriginalContext(ImGuiFixture &imgui)
    {
        const int startingFailures = failures;
        WindowFixture fixture;
        if (!fixture.window)
            return;
        const HIMC defaultContext = fixture.CurrentContext();
        HIMC independent = ImmCreateContext();
        CHECK(independent != nullptr);
        if (!independent)
            return;
        const HIMC displaced = ImmAssociateContext(fixture.window, independent);
        CHECK(displaced == defaultContext);
        CHECK(fixture.CurrentContext() == independent);

        fixture.Attach();
        const ime::TextInput request = imgui.TextRequest();
        CHECK(request.wantTextInput);
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() != independent);
        fixture.menuActive = false;
        ime::Clear(fixture.window);
        fixture.PumpUpdates();
        CHECK(fixture.CurrentContext() == independent);
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        ime::Detach(fixture.window);
        fixture.attached = false;
        CHECK(fixture.CurrentContext() == independent);

        ImmAssociateContext(fixture.window, defaultContext);
        ImmDestroyContext(independent);
        CHECK(fixture.CurrentContext() == defaultContext);
        ReportPassIfClean(startingFailures, "independent pre-associated HIMC is preserved");
    }

    void TestRawKeyboardRegistration(ImGuiFixture &imgui)
    {
        const int startingFailures = failures;
        RawRegistrationSnapshot snapshot;
        CHECK(snapshot.result != RawQuery::Error);
        if (snapshot.result == RawQuery::Error)
            return;
        WindowFixture fixture;
        if (!fixture.window)
            return;
        WindowFixture externalTarget;
        if (!externalTarget.window)
            return;

        RAWINPUTDEVICE noLegacy{1, 6, RIDEV_NOLEGACY | RIDEV_INPUTSINK, fixture.window};
        CHECK(RegisterRawInputDevices(&noLegacy, 1, sizeof(noLegacy)) != FALSE);
        RAWINPUTDEVICE registeredNoLegacy{};
        CHECK(QueryKeyboardRegistration(registeredNoLegacy) == RawQuery::Found);
        CHECK((registeredNoLegacy.dwFlags & RIDEV_NOLEGACY) != 0);
        CHECK(registeredNoLegacy.hwndTarget == fixture.window);
        fixture.Attach();

        const ime::TextInput request = imgui.TextRequest();
        CHECK(request.wantTextInput);
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        RAWINPUTDEVICE temporary{};
        CHECK(QueryKeyboardRegistration(temporary) == RawQuery::Found);
        CHECK((temporary.dwFlags & RIDEV_NOLEGACY) == 0);
        CHECK(temporary.hwndTarget == fixture.window);

        ime::Request(fixture.window, {});
        fixture.PumpUpdates();
        RAWINPUTDEVICE restored{};
        CHECK(QueryKeyboardRegistration(restored) == RawQuery::Found);
        CHECK(SameRawRegistration(restored, registeredNoLegacy));

        // Re-enter text mode, then emulate another component replacing the
        // registration. Detach must keep that newer registration untouched.
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();
        RAWINPUTDEVICE external{1, 6, RIDEV_NOLEGACY | RIDEV_INPUTSINK, externalTarget.window};
        CHECK(RegisterRawInputDevices(&external, 1, sizeof(external)) != FALSE);
        RAWINPUTDEVICE registeredExternal{};
        CHECK(QueryKeyboardRegistration(registeredExternal) == RawQuery::Found);
        CHECK(registeredExternal.hwndTarget == externalTarget.window);
        fixture.menuActive = false;
        ime::Clear(fixture.window);
        fixture.PumpUpdates();
        RAWINPUTDEVICE retained{};
        CHECK(QueryKeyboardRegistration(retained) == RawQuery::Found);
        CHECK(SameRawRegistration(retained, registeredExternal));
        ime::Detach(fixture.window);
        fixture.attached = false;
        CHECK(QueryKeyboardRegistration(retained) == RawQuery::Found);
        CHECK(SameRawRegistration(retained, registeredExternal));
        snapshot.Restore();

        ReportPassIfClean(startingFailures, "NOLEGACY keyboard registration restores conditionally and preserves external changes");
    }

    void TestImeMessageSingleRouteAndTextCommit(ImGuiFixture &imgui)
    {
        const int startingFailures = failures;
        WindowFixture fixture;
        if (!fixture.window)
            return;
        if (!ImGui_ImplWin32_Init(fixture.window))
        {
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__
                      << ": ImGui Win32 backend initialization failed in IME test fixture\n";
            ++failures;
            return;
        }
        // Keep observing the same platform request shape used by the overlay while
        // retaining backend state for its real WM_CHAR event submission path.
        ImGui::GetPlatformIO().Platform_SetImeDataFn = ImGuiFixture::CapturePlatformImeData;
        fixture.Attach();
        const ime::TextInput request = imgui.TextRequest();
        CHECK(request.wantTextInput);
        ime::Request(fixture.window, request);
        fixture.PumpUpdates();

        const int compositionBefore = fixture.compositionMessages;
        SendMessageW(fixture.window, WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
        CHECK(fixture.compositionMessages == compositionBefore + 1);
        imgui.Frame();
        CHECK(imgui.text[0] == '\0');

        // A synthetic WM_CHAR checks the committed-text delivery endpoint only.
        // It does not simulate Sogou composition, candidate selection or a real IME.
        const int charsBefore = fixture.backendCharMessages;
        SendMessageW(fixture.window, WM_CHAR, L'中', 1);
        CHECK(fixture.backendCharMessages == charsBefore + 1);
        imgui.Frame();
        CHECK(std::string_view(imgui.text) == "\xe4\xb8\xad");

        ime::Detach(fixture.window);
        fixture.attached = false;
        ImGui_ImplWin32_Shutdown();
        ReportPassIfClean(startingFailures, "WM_IME_COMPOSITION takes one default path; synthetic WM_CHAR reaches ImGui once");
    }
}

int main()
{
    KeyboardLayoutScope keyboardLayout;
    if (!RegisterTestWindowClass())
    {
        std::cerr << "Could not register IME test window class: " << GetLastError() << '\n';
        return 2;
    }
    if (!keyboardLayout.HasIme())
        std::cout << "SKIP IMM form readback may be unavailable: no IME HKL could be activated on this test thread\n";
    ImGuiFixture imgui;
    if (!imgui.Ready())
    {
        std::cerr << "FAIL Dear ImGui InputText fixture did not become active or publish IME data\n";
        return 1;
    }
    TestDefaultContextAndOwnerThreadRequests(imgui);
    TestNullOriginalContext(imgui);
    TestIndependentOriginalContext(imgui);
    TestRawKeyboardRegistration(imgui);
    TestImeMessageSingleRouteAndTextCommit(imgui);
    UnregisterClassW(WindowClassName, GetModuleHandleW(nullptr));

    if (failures)
    {
        std::cerr << failures << " IME test assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer IME integration tests passed\n";
    return 0;
}
