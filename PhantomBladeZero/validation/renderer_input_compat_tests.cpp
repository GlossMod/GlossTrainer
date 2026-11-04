#include "../src/dx11_extra_state.hpp"
#include "../src/input_capture_rules.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    struct Object {
        int references{1};
        void AddRef() { ++references; }
        void Release() { --references; }
    };
    struct Context {
        struct Stage { Object* shader{}; std::array<Object*, 256> instances{}; unsigned count{}; };
        Stage hull, domain, compute;
        unsigned restores{};
        static void Get(const Stage& stage, Object** shader, Object** instances, unsigned* count) {
            *shader = stage.shader;
            if (*shader) (*shader)->AddRef();
            const auto copied = (std::min)(*count, stage.count);
            std::copy_n(stage.instances.data(), copied, instances);
            for (unsigned i = 0; i < copied; ++i)
                if (instances[i]) instances[i]->AddRef();
            *count = copied;
        }
        void Set(Stage& stage, Object* shader, Object* const* instances, unsigned count) {
            stage = {}; stage.shader = shader; stage.count = count;
            std::copy_n(instances, count, stage.instances.data());
            ++restores;
        }
        void HSGetShader(Object** shader, Object** instances, unsigned* count) { Get(hull, shader, instances, count); }
        void DSGetShader(Object** shader, Object** instances, unsigned* count) { Get(domain, shader, instances, count); }
        void CSGetShader(Object** shader, Object** instances, unsigned* count) { Get(compute, shader, instances, count); }
        void HSSetShader(Object* shader, Object* const* instances, unsigned count) { Set(hull, shader, instances, count); }
        void DSSetShader(Object* shader, Object* const* instances, unsigned count) { Set(domain, shader, instances, count); }
        void CSSetShader(Object* shader, Object* const* instances, unsigned count) { Set(compute, shader, instances, count); }
    };
    using ExtraShaderState = gloss::graphics::detail::ExtraShaderState<Context, Object, Object, Object, Object>;

    void TestShaderBindingsSurviveOverlayAndException() {
        Object hull, compute, sharedInstance, lastInstance;
        Context context;
        context.hull = {&hull, {}, 256};
        context.hull.instances[0] = &sharedInstance;
        context.hull.instances[255] = &lastInstance;
        context.compute = {&compute, {}, 1};
        context.compute.instances[0] = &sharedInstance;
        try {
            ExtraShaderState state(&context);
            CHECK(hull.references == 2 && compute.references == 2);
            CHECK(sharedInstance.references == 3 && lastInstance.references == 2);
            // Model the exact upstream backend behavior: clear every extra stage.
            context.hull = {}; context.domain = {}; context.compute = {};
            throw std::runtime_error("draw callback failed");
        } catch (const std::runtime_error&) {}
        CHECK(context.restores == 3);
        CHECK(context.hull.shader == &hull && context.hull.count == 256);
        CHECK(context.hull.instances[0] == &sharedInstance && context.hull.instances[255] == &lastInstance);
        CHECK(!context.domain.shader && context.domain.count == 0);
        CHECK(context.compute.shader == &compute && context.compute.count == 1);
        CHECK(context.compute.instances[0] == &sharedInstance);
        CHECK(hull.references == 1 && compute.references == 1);
        CHECK(sharedInstance.references == 1 && lastInstance.references == 1);
    }
    void TestMouseCleanupLeavesForeignCaptureAndReleasesOwnOnce() {
        gloss::input::MenuMouseCapture state;
        state.ObserveBackend(true, true); // Another plugin already captured HWND.
        CHECK(!state.ReleaseIfOwned(true));
        state.ObserveBackend(false, true); // Trainer backend acquired capture.
        CHECK(state.owned);
        state.ObserveBackend(true, true); // Subsequent motion retains ownership.
        CHECK(state.ReleaseIfOwned(true));
        CHECK(!state.ReleaseIfOwned(true)); // A queued second sync cannot release twice.
        state.ObserveBackend(false, true);
        state.Lost(); // Another window/plugin receives WM_CAPTURECHANGED.
        CHECK(!state.ReleaseIfOwned(true));
        state.ObserveBackend(false, true);
        state.ObserveBackend(true, false); // Backend releases on physical button up.
        CHECK(!state.ReleaseIfOwned(true));
        state.ObserveBackend(false, true);
        CHECK(!state.ReleaseIfOwned(false)); // Never release capture on another HWND.
        CHECK(!state.owned);
    }
}

int main() {
    TestShaderBindingsSurviveOverlayAndException();
    TestMouseCleanupLeavesForeignCaptureAndReleasesOwnOnce();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
