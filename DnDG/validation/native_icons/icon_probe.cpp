#include "game_icon_pixels.hpp"
#include "game_read.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <cstddef>

static_assert(_MSC_VER == 1938, "Match the pinned Godot SDK toolset");

namespace {
using namespace godot;
using namespace gloss::dndg;

class NativeIconProbe : public Node {
    GDCLASS(NativeIconProbe, Node)

public:
    Vector2i loaded_size(const String& path) {
        const auto texture = LoadGameIcon(path.utf8().get_data());
        return texture.is_valid() ? Vector2i(texture->get_width(), texture->get_height()) : Vector2i();
    }

    Dictionary game_pixels(const String& path) {
        const auto icon = ReadGameIconPixels(path.utf8().get_data());
        bool visible = false, varied = false;
        for (std::size_t i = 0; i + 3 < icon.rgba.size(); i += 4) {
            visible |= icon.rgba[i + 3] != 0;
            varied |= icon.rgba[i] != icon.rgba[0] || icon.rgba[i + 1] != icon.rgba[1] ||
                icon.rgba[i + 2] != icon.rgba[2] || icon.rgba[i + 3] != icon.rgba[3];
        }
        Dictionary result;
        result["size"] = Vector2i(icon.width, icon.height);
        result["bytes"] = static_cast<std::int64_t>(icon.rgba.size());
        result["visible"] = visible;
        result["varied"] = varied;
        return result;
    }

    Dictionary pixels(const Ref<Image>& source, Rect2i region, bool crop) {
        const auto icon = EncodeIconPixels("fixture", source, crop ? std::optional(region) : std::nullopt);
        Dictionary result;
        result["size"] = Vector2i(icon.width, icon.height);
        result["bytes"] = static_cast<std::int64_t>(icon.rgba.size());
        if (icon.rgba.size() >= 4)
            result["first"] = Color::from_rgba8(icon.rgba[0], icon.rgba[1], icon.rgba[2], icon.rgba[3]);
        return result;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("loaded_size", "path"), &NativeIconProbe::loaded_size);
        ClassDB::bind_method(D_METHOD("game_pixels", "path"), &NativeIconProbe::game_pixels);
        ClassDB::bind_method(D_METHOD("pixels", "source", "region", "crop"), &NativeIconProbe::pixels);
    }
};

void Register(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE)
        GDREGISTER_CLASS(NativeIconProbe);
}

void Unregister(ModuleInitializationLevel) {}
} // namespace

extern "C" __declspec(dllexport) GDExtensionBool dndg_native_icon_probe(
    GDExtensionInterfaceGetProcAddress getProc, GDExtensionClassLibraryPtr library,
    GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject binding(getProc, library, initialization);
    binding.register_initializer(Register);
    binding.register_terminator(Unregister);
    binding.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return binding.init();
}
