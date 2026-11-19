#include "icons.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace gloss::dndg {
namespace {

constexpr int PageSide = 1024;
constexpr int SlotSide = 64;
constexpr int GridSide = PageSide / SlotSide;
constexpr int Slots = GridSide * GridSide;
constexpr int Pages = 4;
constexpr int QueueLimit = 48;

struct Mailbox {
    std::mutex mutex;
    std::deque<std::string> requests;
    std::deque<IconPixels> replies;
};

Mailbox& Exchange() { static Mailbox value; return value; }

struct Region {
    bool available{};
    int page{};
    int x{};
    int y{};
    int w{};
    int h{};
};

struct Atlas {
    std::array<ImTextureData, Pages> textures;
    std::unordered_map<std::string, Region> regions;
    int allocated{};
};

// Renderer and its ImGui context are process lifetime. No static destructor
// frees textures while an ImGui backend may still own their addresses.
Atlas& Cache() { static auto* value = new Atlas; return *value; }

} // namespace

std::string TakeIconRequest() {
    auto& exchange = Exchange();
    std::lock_guard lock(exchange.mutex);
    if (exchange.requests.empty() || exchange.replies.size() >= QueueLimit)
        return {};
    auto next = std::move(exchange.requests.front());
    exchange.requests.pop_front();
    return next;
}

void PublishIconPixels(IconPixels value) {
    auto& exchange = Exchange();
    std::lock_guard lock(exchange.mutex);
    if (exchange.replies.size() < QueueLimit)
        exchange.replies.push_back(std::move(value));
}

void PumpIconAtlas() {
    if (!(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures))
        return;
    auto& cache = Cache();
    IconPixels image;
    {
        auto& exchange = Exchange();
        std::lock_guard lock(exchange.mutex);
        if (exchange.replies.empty())
            return;
        if (cache.allocated < Pages * Slots &&
                cache.textures[cache.allocated / Slots].Status == ImTextureStatus_WantUpdates)
            return; // The previous partial upload has not been consumed.
        image = std::move(exchange.replies.front());
        exchange.replies.pop_front();
    }
    const auto region = cache.regions.find(image.key);
    if (region == cache.regions.end() || region->second.available || cache.allocated == Pages * Slots ||
            image.width <= 0 || image.height <= 0 || image.width > IconMaxSide || image.height > IconMaxSide ||
            image.rgba.size() != static_cast<std::size_t>(image.width * image.height * 4))
        return;
    const int slot = cache.allocated++;
    const int page = slot / Slots;
    const int x = (slot % GridSide) * SlotSide + 1;
    const int y = ((slot % Slots) / GridSide) * SlotSide + 1;
    auto& texture = cache.textures[page];
    if (!texture.Pixels) {
        texture.Create(ImTextureFormat_RGBA32, PageSide, PageSide);
        std::memset(texture.GetPixels(), 0, texture.GetSizeInBytes());
        texture.UseColors = true;
        ImGui::RegisterUserTexture(&texture);
    }
    for (int row = 0; row < image.height; ++row)
        std::memcpy(texture.GetPixelsAt(x, y + row), image.rgba.data() + row * image.width * 4, image.width * 4);
    if (texture.Status == ImTextureStatus_OK) {
        const ImTextureRect rect{static_cast<unsigned short>(x), static_cast<unsigned short>(y),
            static_cast<unsigned short>(image.width), static_cast<unsigned short>(image.height)};
        texture.UpdateRect = rect;
        texture.Updates.push_back(rect);
        texture.SetStatus(ImTextureStatus_WantUpdates);
    }
    region->second = {true, page, x, y, image.width, image.height};
}

bool DrawItemIcon(const std::string& key, float left, float top, float right, float bottom) {
    if (key.empty() || key.size() > 512 || !ImGui::IsItemVisible())
        return false;
    auto& cache = Cache();
    const auto existing = cache.regions.find(key);
    if (existing == cache.regions.end()) {
        if (cache.regions.size() >= Pages * Slots * 2 || cache.allocated == Pages * Slots)
            return false;
        auto& exchange = Exchange();
        std::lock_guard lock(exchange.mutex);
        if (exchange.requests.size() < QueueLimit) {
            cache.regions.emplace(key, Region{});
            exchange.requests.push_back(key);
        }
        return false;
    }
    const auto& region = existing->second;
    if (!region.available)
        return false;
    auto& texture = cache.textures[region.page];
    if (texture.GetTexID() == ImTextureID_Invalid || right <= left || bottom <= top)
        return false;
    const float scale = std::min((right - left) / region.w, (bottom - top) / region.h);
    const float width = region.w * scale, height = region.h * scale;
    const ImVec2 start{(right + left - width) * .5f, (bottom + top - height) * .5f};
    const ImVec2 end{start.x + width, start.y + height};
    ImGui::GetWindowDrawList()->AddImage(texture.GetTexRef(), start, end,
        {(region.x + .5f) / PageSide, (region.y + .5f) / PageSide},
        {(region.x + region.w - .5f) / PageSide, (region.y + region.h - .5f) / PageSide});
    return true;
}

} // namespace gloss::dndg
