#include "game_write.hpp"
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
using namespace godot;
using namespace gloss::dndg;
class NativeWriteProbe final : public Node {
    GDCLASS(NativeWriteProbe, Node)
public:
    int edit(int verb, int kind, uint64_t encounter, uint64_t token, int value, const String& id) {
        const Command command{static_cast<Edit>(verb), static_cast<Kind>(kind), encounter, token, value, id.utf8().get_data()};
        return static_cast<int>(ApplyGameCommand(get_tree(), command));
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("edit", "verb", "kind", "encounter", "token", "value", "id"), &NativeWriteProbe::edit);
    }
};
void Init(ModuleInitializationLevel level) { if (level == MODULE_INITIALIZATION_LEVEL_SCENE) GDREGISTER_CLASS(NativeWriteProbe); }
void Deinit(ModuleInitializationLevel) {}
extern "C" __declspec(dllexport) GDExtensionBool native_write_init(GDExtensionInterfaceGetProcAddress proc,
    GDExtensionClassLibraryPtr library, GDExtensionInitialization *init) {
    GDExtensionBinding::InitObject object(proc, library, init);
    object.register_initializer(Init);
    object.register_terminator(Deinit);
    object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return object.init();
}
