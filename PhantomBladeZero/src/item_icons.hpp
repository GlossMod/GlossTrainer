#pragma once
#include <imgui.h>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace gloss::ui {
    struct ItemIconView {
        ImTextureRef texture;
        ImVec2 uv0{0, 0}, uv1{1, 1};
        ImVec2 size;
    };

    // Call after ImGui::NewFrame(), under OverlayMutex, on the render thread.
    // Game image payloads are decoded in the background. At most one texture
    // upload is requested per frame, with up to 64 GPU textures resident.
    void BeginItemIconFrame();

    // Renderer-owned callback, bound/cleared under OverlayMutex. It must return
    // false without modifying any texture if GPU completion cannot be proved.
    // On true every supplied texture must have been destroyed by the backend,
    // after actual GPU completion. Called only before this frame's icon draws.
    void SetItemIconRetirementCallback(std::function<bool(std::span<ImTextureData* const>)> callback);

    // Signal the decoder to finish, without waiting or releasing GPU resources.
    // Call under OverlayMutex during ordinary shutdown, never from DllMain.
    // Context destruction also signals it; a new context can restart it.
    void StopItemIconWorker();

    // Render-thread only. Missing, invalid, or not-yet-uploadable icons have no
    // view. The caller should draw a neutral placeholder in that case.
    std::optional<ItemIconView> FindItemIcon(std::string_view iconKey);
}
