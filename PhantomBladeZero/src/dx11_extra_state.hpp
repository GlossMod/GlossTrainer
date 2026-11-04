#pragma once
#include <array>

namespace gloss::graphics::detail {
    template<class Shader, class Instance>
    struct ShaderBinding {
        Shader* shader{};
        std::array<Instance*, 256> instances{};
        unsigned count{256};

        template<class Get> void Capture(Get get) { get(&shader, instances.data(), &count); }
        template<class Set> void Restore(Set set) {
            set(shader, instances.data(), count);
            if (shader) shader->Release();
            for (unsigned i = 0; i < count; ++i)
                if (instances[i]) instances[i]->Release();
        }
    };

    // ImGui's DX11 backend restores VS/PS/GS itself, but clears HS/DS/CS without
    // restoring them. These bindings include dynamic shader class instances.
    template<class Context, class Instance, class Hull, class Domain, class Compute>
    class ExtraShaderState {
    public:
        explicit ExtraShaderState(Context* context) : context_(context) {
            hull_.Capture([&](auto shader, auto instances, auto count) { context_->HSGetShader(shader, instances, count); });
            domain_.Capture([&](auto shader, auto instances, auto count) { context_->DSGetShader(shader, instances, count); });
            compute_.Capture([&](auto shader, auto instances, auto count) { context_->CSGetShader(shader, instances, count); });
        }
        ~ExtraShaderState() {
            hull_.Restore([&](auto shader, auto instances, auto count) { context_->HSSetShader(shader, instances, count); });
            domain_.Restore([&](auto shader, auto instances, auto count) { context_->DSSetShader(shader, instances, count); });
            compute_.Restore([&](auto shader, auto instances, auto count) { context_->CSSetShader(shader, instances, count); });
        }
        ExtraShaderState(const ExtraShaderState&) = delete;
        ExtraShaderState& operator=(const ExtraShaderState&) = delete;

    private:
        Context* context_;
        ShaderBinding<Hull, Instance> hull_;
        ShaderBinding<Domain, Instance> domain_;
        ShaderBinding<Compute, Instance> compute_;
    };
}
