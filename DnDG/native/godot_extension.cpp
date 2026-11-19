#include "game_icon_pixels.hpp"
#include "game_read.hpp"
#include "game_write.hpp"
#include "model.hpp"
#include "icons.hpp"
#include "runtime.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <utility>

// Match the toolset used for godot-cpp. Mixed MSVC 19.38 / 19.44
// objects disagree on the inline thread-local Godot construction context.
static_assert(_MSC_VER == 1938, "Build DnDG and the pinned godot-cpp SDK with MSVC 14.38");

namespace gloss::dndg {

class GlossTrainerBridge : public godot::Node {
    GDCLASS(GlossTrainerBridge, godot::Node)

public:
    void _ready() override {
        const auto instance = get_instance_id();
        if (activeBridge_ && activeBridge_ != instance) {
            set_process(false);
            return;
        }
        activeBridge_ = instance;
        set_process_mode(PROCESS_MODE_ALWAYS);
        set_process(true);
        Log("GDExtension game-thread bridge ready.");
        godot::UtilityFunctions::print("[GlossTrainer DnDG] Native game-thread bridge ready");
    }

    void _process(double delta) override {
        auto* tree = get_tree();
        if (!tree || activeBridge_ != get_instance_id())
            return;
        const auto request = ReadUiRequest();
        if (request.panelOpen && !ownPause_ && !tree->is_paused()) {
            tree->set_pause(true);
            ownPause_ = true;
        } else if (!request.panelOpen) {
            RestorePause(tree);
        }

        auto commands = TakeCommands();
        for (const auto& command : commands) {
            // Clicks can race menu dismissal by one frame. Reject them unless our
            // menu is still open and the game thread sees the encounter as editable.
            PublishFeedback(request.panelOpen ? ApplyGameCommand(tree, command) : Feedback::Unavailable);
        }

        if (request.panelOpen) {
            auto key = TakeIconRequest();
            if (!key.empty())
                PublishIconPixels(ReadGameIconPixels(std::move(key)));
        }
        sinceRefresh_ += delta;
        const bool refresh = sinceRefresh_ >= .15 || !commands.empty() ||
            request.panelOpen != previous_.panelOpen || request.showNext != previous_.showNext || request.locale != previous_.locale;
        if (!refresh)
            return;
        sinceRefresh_ = 0;
        previous_ = request;

        // Only snapshot the expensive lists while relevant. Keep a cheap locale
        // snapshot when closed so the first UI frame can follow the game.
        auto state = ReadGameSnapshot(tree, request);
        const auto effectiveLocale = request.locale == Locale::Game ? state.locale : request.locale;
        if (request.panelOpen && (!catalogReady_ || catalogLocale_ != effectiveLocale)) {
            auto catalog = ReadGameCatalog(tree, effectiveLocale);
            catalogReady_ = !catalog.cards.empty();
            catalogLocale_ = effectiveLocale;
            PublishCatalog(std::move(catalog));
        }
        PublishSnapshot(std::move(state));
    }

    void _exit_tree() override {
        if (activeBridge_ != get_instance_id())
            return;
        RestorePause(get_tree());
        activeBridge_ = 0;
        TakeCommands();
        PublishSnapshot({});
        PublishCatalog({});
    }

protected:
    static void _bind_methods() {}

private:
    void RestorePause(godot::SceneTree* tree) {
        if (ownPause_ && tree)
            tree->set_pause(false);
        ownPause_ = false;
    }

    inline static std::uint64_t activeBridge_{};
    bool ownPause_{};
    bool catalogReady_{};
    double sinceRefresh_{1.};
    Locale catalogLocale_{Locale::Game};
    UiRequest previous_{};
};

void RegisterExtension(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE)
        GDREGISTER_CLASS(GlossTrainerBridge);
}

void UnregisterExtension(godot::ModuleInitializationLevel) {}

} // namespace gloss::dndg

extern "C" __declspec(dllexport) GDExtensionBool GlossTrainerGodotInit(
        GDExtensionInterfaceGetProcAddress getProc, GDExtensionClassLibraryPtr library,
        GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject binding(getProc, library, initialization);
    binding.register_initializer(gloss::dndg::RegisterExtension);
    binding.register_terminator(gloss::dndg::UnregisterExtension);
    binding.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return binding.init();
}
