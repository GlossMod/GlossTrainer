#include "item_icons.hpp"
#include "runtime.hpp"
#include "gameplay/item_images.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4244 4267 4505)
#include "third_party/bcdec.h"
#pragma warning(pop)

namespace gloss::ui {
    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t MaxGpuTextures = 64, RetireBatch = 8, MaxEntries = 1024;
        constexpr std::size_t MaxWorkerJobs = 48;
        constexpr std::uint64_t MaxCpuBytes = 32ull * 1024 * 1024;
        constexpr std::uint64_t MaxQueuedBytes = 16ull * 1024 * 1024;
        constexpr std::uint32_t MaxInputDimension = 2048, MaxOutputDimension = 256;

        struct StringHash {
            using is_transparent = void;
            std::size_t operator()(std::string_view value) const noexcept {
                return std::hash<std::string_view>{}(value);
            }
        };
        enum class Phase { Empty, Requested, Decoding, Ready, Failed };
        struct Entry {
            std::uint64_t epoch{}, lastTouch{};
            Phase phase{};
            std::uint32_t width{}, height{};
            std::vector<std::uint8_t> pixels;
            std::unique_ptr<ImTextureData> texture;
            Clock::time_point retryAfter{};
            bool obsolete{}, retiring{};
        };
        struct Decoded {
            std::uint64_t epoch{};
            std::string key;
            std::uint32_t width{}, height{};
            std::vector<std::uint8_t> pixels;
            std::string error;
        };
        struct Cache {
            std::mutex workerMutex;
            std::condition_variable wake;
            std::deque<gameplay::ItemImageResult> jobs;
            std::deque<Decoded> completed;
            std::uint64_t queuedBytes{}, completedBytes{};
            std::thread worker;
            bool workerRunning{}, stopRequested{}, workerError{};
            bool workerStarted{}, workerFailed{}, suspended{};
            // The remaining fields belong to the render thread/OverlayMutex.
            std::unordered_map<std::string, Entry, StringHash, std::equal_to<>> entries;
            std::uint64_t epoch{}, frame{}, cpuBytes{};
            std::size_t textureCount{};
            ImGuiContext* context{};
            int lastFrame{-1};
            bool otherContextLogged{}, retirementFailureLogged{};
            unsigned retirementFailures{};
            Clock::time_point nextRetirement{};
            std::function<bool(std::span<ImTextureData* const>)> retire;
        };
        Cache& State() {
            // ASI hot unloading is unsupported. Never join workers or release GPU
            // objects from a CRT static destructor beneath the loader lock.
            static auto* cache = new Cache;
            return *cache;
        }
        bool ValidKey(std::string_view key) {
            return !key.empty() && key.size() <= 1024 && key.front() == '/' &&
                key.find_first_of("\r\n\t\0", 0, 4) == key.npos &&
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key.data(),
                    static_cast<int>(key.size()), nullptr, 0) > 0;
        }
        Decoded Decode(gameplay::ItemImageResult source) {
            Decoded result;
            result.epoch = source.epoch;
            result.key = std::move(source.key);
            result.error = std::move(source.error);
            if (source.data.empty()) {
                if (result.error.empty()) result.error = "游戏未返回图片数据";
                return result;
            }
            if (!source.width || !source.height || source.width > MaxInputDimension || source.height > MaxInputDimension) {
                result.error = "图片尺寸超出解码限制";
                return result;
            }
            const auto width = source.width, height = source.height;
            std::uint64_t expected{};
            unsigned blockBytes{};
            using Format = gameplay::ItemImageFormat;
            switch (source.format) {
            case Format::Rgba8: case Format::Bgra8:
                expected = static_cast<std::uint64_t>(width) * height * 4;
                break;
            case Format::Bc1: blockBytes = BCDEC_BC1_BLOCK_SIZE; break;
            case Format::Bc2: case Format::Bc3: case Format::Bc7: blockBytes = BCDEC_BC3_BLOCK_SIZE; break;
            default: result.error = "图片压缩格式不受支持"; return result;
            }
            if (blockBytes) expected = static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
            if (source.data.size() != expected) {
                result.error = "图片数据长度与格式不匹配";
                return result;
            }
            std::vector<std::uint8_t> rgba;
            if (!blockBytes) {
                rgba = std::move(source.data);
                if (source.format == Format::Bgra8)
                    for (std::size_t index = 0; index < rgba.size(); index += 4) std::swap(rgba[index], rgba[index + 2]);
            } else {
                rgba.resize(static_cast<std::size_t>(width) * height * 4);
                const auto blocksX = (width + 3) / 4;
                for (std::uint32_t y = 0; y < height; y += 4)
                    for (std::uint32_t x = 0; x < width; x += 4) {
                        std::array<std::uint8_t, 64> block{};
                        const auto* compressed = source.data.data() + ((y / 4) * blocksX + x / 4) * blockBytes;
                        switch (source.format) {
                        case Format::Bc1: bcdec_bc1(compressed, block.data(), 16); break;
                        case Format::Bc2: bcdec_bc2(compressed, block.data(), 16); break;
                        case Format::Bc3: bcdec_bc3(compressed, block.data(), 16); break;
                        case Format::Bc7: bcdec_bc7(compressed, block.data(), 16); break;
                        default: break;
                        }
                        const auto copyWidth = std::min<std::uint32_t>(4, width - x);
                        const auto copyHeight = std::min<std::uint32_t>(4, height - y);
                        for (std::uint32_t row = 0; row < copyHeight; ++row)
                            std::memcpy(rgba.data() + ((y + row) * width + x) * 4,
                                block.data() + row * 16, copyWidth * 4);
                    }
            }
            const auto longest = std::max(width, height);
            if (longest <= MaxOutputDimension) {
                result.width = width;
                result.height = height;
                result.pixels = std::move(rgba);
                return result;
            }
            result.width = (width * MaxOutputDimension + longest - 1) / longest;
            result.height = (height * MaxOutputDimension + longest - 1) / longest;
            result.pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4);
            // Average premultiplied colors, then return straight RGBA. Transparent
            // texels cannot darken the edge of an otherwise opaque weapon icon.
            for (std::uint32_t y = 0; y < result.height; ++y) {
                const auto y0 = y * height / result.height, y1 = (y + 1) * height / result.height;
                for (std::uint32_t x = 0; x < result.width; ++x) {
                    const auto x0 = x * width / result.width, x1 = (x + 1) * width / result.width;
                    std::uint64_t alpha{}, red{}, green{}, blue{};
                    for (auto sourceY = y0; sourceY < y1; ++sourceY)
                        for (auto sourceX = x0; sourceX < x1; ++sourceX) {
                            const auto* pixel = rgba.data() + (sourceY * width + sourceX) * 4;
                            alpha += pixel[3];
                            red += static_cast<std::uint64_t>(pixel[0]) * pixel[3];
                            green += static_cast<std::uint64_t>(pixel[1]) * pixel[3];
                            blue += static_cast<std::uint64_t>(pixel[2]) * pixel[3];
                        }
                    auto* pixel = result.pixels.data() + (y * result.width + x) * 4;
                    const auto count = static_cast<std::uint64_t>(x1 - x0) * (y1 - y0);
                    pixel[0] = alpha ? static_cast<std::uint8_t>((red + alpha / 2) / alpha) : 0;
                    pixel[1] = alpha ? static_cast<std::uint8_t>((green + alpha / 2) / alpha) : 0;
                    pixel[2] = alpha ? static_cast<std::uint8_t>((blue + alpha / 2) / alpha) : 0;
                    pixel[3] = static_cast<std::uint8_t>((alpha + count / 2) / count);
                }
            }
            return result;
        }
        void Worker(Cache& cache) {
            struct Exit {
                Cache& cache;
                ~Exit() { std::lock_guard lock(cache.workerMutex); cache.workerRunning = false; }
            } exit{cache};
            for (;;) {
                gameplay::ItemImageResult job;
                {
                    std::unique_lock lock(cache.workerMutex);
                    cache.wake.wait(lock, [&] {
                        return cache.stopRequested || (!cache.jobs.empty() && cache.completed.size() < MaxWorkerJobs &&
                            cache.completedBytes < MaxQueuedBytes);
                    });
                    if (cache.stopRequested) return;
                    job = std::move(cache.jobs.front());
                    cache.jobs.pop_front();
                    cache.queuedBytes -= job.data.size();
                }
                Decoded result;
                const auto epoch = job.epoch;
                const auto key = job.key;
                try { result = Decode(std::move(job)); }
                catch (...) { result.epoch = epoch; result.key = key; result.error = "图片解码失败"; }
                {
                    std::lock_guard lock(cache.workerMutex);
                    if (cache.stopRequested) return;
                    cache.completedBytes += result.pixels.size();
                    cache.completed.push_back(std::move(result));
                }
            }
        }
        void StartWorker(Cache& cache) {
            if (cache.workerStarted || cache.workerFailed || cache.suspended) return;
            {
                std::lock_guard lock(cache.workerMutex);
                if (cache.workerRunning) return;
                if (cache.workerError) { cache.workerFailed = true; return; }
            }
            // A previously signalled worker has already left its decode loop.
            // Joining here cannot wait for game/GPU work or run under DllMain.
            if (cache.worker.joinable()) cache.worker.join();
            try {
                {
                    std::lock_guard lock(cache.workerMutex);
                    cache.stopRequested = false;
                    cache.workerRunning = true;
                }
                cache.worker = std::thread([&cache] {
                    try { Worker(cache); }
                    catch (...) {
                        std::lock_guard lock(cache.workerMutex);
                        cache.workerError = true;
                        cache.workerRunning = false;
                    }
                });
                cache.workerStarted = true;
            } catch (...) {
                { std::lock_guard lock(cache.workerMutex); cache.workerRunning = false; }
                cache.workerFailed = true;
                Log("Item icons: background decoder could not start; using placeholders");
            }
        }
        void StopWorker(Cache& cache) {
            {
                std::lock_guard lock(cache.workerMutex);
                cache.stopRequested = true;
                cache.jobs.clear(); cache.completed.clear();
                cache.queuedBytes = cache.completedBytes = 0;
            }
            cache.workerStarted = false;
            for (auto& [key, entry] : cache.entries)
                if (entry.phase == Phase::Requested || entry.phase == Phase::Decoding) entry.phase = Phase::Empty;
            cache.wake.notify_one();
        }
        bool Registered(const ImGuiContext& context, const ImTextureData* texture) {
            return std::find(context.UserTextures.begin(), context.UserTextures.end(), texture) != context.UserTextures.end();
        }
        void Unregister(ImGuiContext& context, ImTextureData* texture) {
            if (Registered(context, texture)) ImGui::UnregisterUserTexture(texture);
            // UnregisterUserTexture does not update the last EndFrame's list.
            auto& platform = ImGui::GetPlatformIO().Textures;
            platform.find_erase(texture);
        }
        bool Destroyed(const ImTextureData& texture) {
            return texture.Status == ImTextureStatus_Destroyed && texture.BackendUserData == nullptr &&
                texture.GetTexID() == ImTextureID_Invalid;
        }
        bool NoGpuAllocation(const ImTextureData& texture) {
            return texture.BackendUserData == nullptr && texture.GetTexID() == ImTextureID_Invalid &&
                (texture.Status == ImTextureStatus_WantCreate || texture.Status == ImTextureStatus_Destroyed);
        }
        std::uint64_t PixelBytes(const Entry& entry) {
            return entry.texture ? static_cast<std::uint64_t>(entry.texture->GetSizeInBytes()) : entry.pixels.size();
        }
        void ReleaseTexture(Cache& cache, ImGuiContext& context, Entry& entry, bool preservePixels) {
            auto* texture = entry.texture.get();
            if (preservePixels) {
                try { entry.pixels.assign(texture->Pixels, texture->Pixels + texture->GetSizeInBytes()); }
                catch (...) { preservePixels = false; entry.phase = Phase::Empty; }
            }
            if (!preservePixels) cache.cpuBytes -= PixelBytes(entry);
            Unregister(context, texture);
            entry.texture.reset();
            --cache.textureCount;
            entry.retiring = false;
        }
        void CollectRetired(Cache& cache, ImGuiContext& context) {
            for (auto it = cache.entries.begin(); it != cache.entries.end();) {
                auto& entry = it->second;
                if (entry.texture && ((entry.retiring && Destroyed(*entry.texture)) ||
                    (entry.obsolete && NoGpuAllocation(*entry.texture)))) {
                    ReleaseTexture(cache, context, entry, !entry.obsolete);
                    if (entry.obsolete) { it = cache.entries.erase(it); continue; }
                }
                ++it;
            }
        }
        void ContextShutdown(ImGuiContext* context, ImGuiContextHook* hook) {
            auto& cache = *static_cast<Cache*>(hook->UserData);
            const auto previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context);
            for (auto& [key, entry] : cache.entries)
                if (entry.texture) Unregister(*context, entry.texture.get());
            ImGui::SetCurrentContext(previous);
            if (cache.context == context) {
                StopWorker(cache);
                cache.context = nullptr;
                cache.lastFrame = -1;
            }
        }
        bool BindContext(Cache& cache, ImGuiContext* context) {
            if (cache.context == context) return true;
            if (cache.context) {
                if (!cache.otherContextLogged) {
                    cache.otherContextLogged = true;
                    Log("Item icons: concurrent ImGui contexts are unsupported");
                }
                return false;
            }
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_Shutdown;
            hook.Callback = ContextShutdown;
            hook.UserData = &cache;
            ImGui::AddContextHook(context, &hook);
            cache.context = context;
            cache.lastFrame = -1;
            cache.suspended = false;
            return true;
        }
        void ResetEpoch(Cache& cache, std::uint64_t epoch) {
            if (cache.epoch == epoch) return;
            cache.epoch = epoch;
            {
                std::lock_guard lock(cache.workerMutex);
                cache.jobs.clear();
                cache.completed.clear();
                cache.queuedBytes = cache.completedBytes = 0;
            }
            cache.wake.notify_one();
            for (auto it = cache.entries.begin(); it != cache.entries.end();) {
                if (it->second.texture) { it->second.obsolete = true; ++it; }
                else { cache.cpuBytes -= it->second.pixels.size(); it = cache.entries.erase(it); }
            }
            cache.nextRetirement = {};
        }
        bool ReservePixels(Cache& cache, std::uint64_t bytes, std::string_view exclude) {
            while (cache.cpuBytes + bytes > MaxCpuBytes) {
                auto oldest = cache.entries.end();
                for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it)
                    if (it->first != exclude && !it->second.texture && !it->second.pixels.empty() &&
                        (oldest == cache.entries.end() || it->second.lastTouch < oldest->second.lastTouch)) oldest = it;
                if (oldest == cache.entries.end()) return false;
                cache.cpuBytes -= oldest->second.pixels.size();
                std::vector<std::uint8_t>().swap(oldest->second.pixels);
                oldest->second.phase = Phase::Empty;
            }
            return true;
        }
        void AdoptCompleted(Cache& cache) {
            std::deque<Decoded> completed;
            {
                std::lock_guard lock(cache.workerMutex);
                completed.swap(cache.completed);
                cache.completedBytes = 0;
            }
            cache.wake.notify_one();
            for (auto& result : completed) {
                const auto found = cache.entries.find(result.key);
                if (result.epoch != cache.epoch || found == cache.entries.end() || found->second.obsolete ||
                    found->second.epoch != result.epoch || found->second.texture || found->second.phase != Phase::Decoding) continue;
                auto& entry = found->second;
                if (result.pixels.empty()) {
                    entry.phase = Phase::Failed;
                    Log("Item icons: " + result.key + ": " + result.error);
                    continue;
                }
                if (!ReservePixels(cache, result.pixels.size(), result.key)) {
                    entry.phase = Phase::Empty;
                    entry.retryAfter = Clock::now() + std::chrono::seconds(1);
                    continue;
                }
                cache.cpuBytes += result.pixels.size();
                entry.width = result.width;
                entry.height = result.height;
                entry.pixels = std::move(result.pixels);
                entry.phase = Phase::Ready;
            }
        }
        void QueueResults(Cache& cache) {
            if (!cache.workerStarted) return;
            // The bridge has its own 16 MiB bound. Drain only when the previous
            // batch is consumed, so compressed input cannot accumulate here.
            {
                std::lock_guard lock(cache.workerMutex);
                if (!cache.jobs.empty()) return;
            }
            auto results = gameplay::DrainItemImageResults();
            std::lock_guard lock(cache.workerMutex);
            for (auto& result : results) {
                auto found = cache.entries.find(result.key);
                if (result.epoch != cache.epoch || found == cache.entries.end() || found->second.obsolete ||
                    found->second.epoch != result.epoch || found->second.phase != Phase::Requested) continue;
                auto& entry = found->second;
                if (cache.jobs.size() >= MaxWorkerJobs || result.data.size() > MaxQueuedBytes - cache.queuedBytes) {
                    entry.phase = Phase::Empty;
                    entry.retryAfter = Clock::now() + std::chrono::milliseconds(500);
                    continue;
                }
                entry.phase = Phase::Decoding;
                cache.queuedBytes += result.data.size();
                cache.jobs.push_back(std::move(result));
            }
            cache.wake.notify_one();
        }
        void Retire(Cache& cache, ImGuiContext& context, bool needRoom) {
            if (!cache.retire || Clock::now() < cache.nextRetirement) return;
            std::vector<Entry*> candidates;
            for (auto& [key, entry] : cache.entries) {
                if (!entry.texture) continue;
                if (entry.obsolete || entry.retiring || (needRoom && entry.lastTouch + 1 < cache.frame)) candidates.push_back(&entry);
            }
            std::sort(candidates.begin(), candidates.end(), [](const Entry* left, const Entry* right) {
                if (left->obsolete != right->obsolete) return left->obsolete;
                return left->lastTouch < right->lastTouch;
            });
            if (candidates.size() > RetireBatch) candidates.resize(RetireBatch);
            if (candidates.empty()) return;
            std::array<ImTextureData*, RetireBatch> textures{};
            for (std::size_t index = 0; index < candidates.size(); ++index) textures[index] = candidates[index]->texture.get();
            bool retired{};
            try { retired = cache.retire(std::span<ImTextureData* const>(textures.data(), candidates.size())); }
            catch (...) { Log("Item icons: renderer retirement callback failed"); }
            if (!retired) {
                const auto delay = std::chrono::milliseconds(250 * (1u << std::min(cache.retirementFailures++, 3u)));
                cache.nextRetirement = Clock::now() + delay;
                if (!cache.retirementFailureLogged) {
                    cache.retirementFailureLogged = true;
                    Log("Item icons: GPU completion not yet confirmed; retirement deferred");
                }
                return;
            }
            cache.retirementFailures = 0;
            cache.retirementFailureLogged = false;
            cache.nextRetirement = Clock::now() + std::chrono::milliseconds(100);
            for (auto* entry : candidates) entry->retiring = true;
            // Never trust callback success alone: backend fields must also prove
            // destruction before an ImTextureData pointer is unregistered/deleted.
            CollectRetired(cache, context);
        }
        Entry* UploadCandidate(Cache& cache) {
            Entry* candidate{};
            for (auto& [key, entry] : cache.entries) {
                // Begin runs before this frame's Find calls. Only restore/upload
                // icons requested during the preceding frame; offscreen CPU
                // entries must not cycle through the 64 GPU slots indefinitely.
                if (entry.obsolete || entry.retiring || entry.phase != Phase::Ready || entry.lastTouch + 1 < cache.frame) continue;
                if (entry.texture && entry.texture->Status != ImTextureStatus_WantCreate) continue;
                if (!candidate || entry.lastTouch > candidate->lastTouch) candidate = &entry;
            }
            return candidate;
        }
    }

    void SetItemIconRetirementCallback(std::function<bool(std::span<ImTextureData* const>)> callback) {
        State().retire = std::move(callback);
    }

    void StopItemIconWorker() {
        auto& cache = State();
        cache.suspended = true;
        StopWorker(cache);
    }

    void BeginItemIconFrame() {
        auto* context = ImGui::GetCurrentContext();
        if (!context || !(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures)) return;
        auto& cache = State();
        if (!BindContext(cache, context) || cache.lastFrame == ImGui::GetFrameCount()) return;
        cache.lastFrame = ImGui::GetFrameCount();
        ++cache.frame;
        ResetEpoch(cache, gameplay::ItemImageEpoch());
        StartWorker(cache);
        CollectRetired(cache, *context);
        AdoptCompleted(cache);
        QueueResults(cache);
        auto* candidate = UploadCandidate(cache);
        Retire(cache, *context, candidate && !candidate->texture && cache.textureCount >= MaxGpuTextures);
        // Retire may erase obsolete entries and invalidate a candidate pointer.
        candidate = UploadCandidate(cache);
        // Surviving CPU textures become WantCreate after renderer shutdown. Keep
        // all but this frame's selected upload out of the backend's texture list.
        for (auto& [key, entry] : cache.entries)
            if (entry.texture && entry.texture->Status == ImTextureStatus_WantCreate && !entry.retiring &&
                Registered(*context, entry.texture.get())) Unregister(*context, entry.texture.get());
        if (!candidate || (candidate->texture == nullptr && cache.textureCount >= MaxGpuTextures)) return;
        if (!candidate->texture) {
            candidate->texture = std::make_unique<ImTextureData>();
            candidate->texture->Create(ImTextureFormat_RGBA32, static_cast<int>(candidate->width), static_cast<int>(candidate->height));
            std::memcpy(candidate->texture->Pixels, candidate->pixels.data(), candidate->pixels.size());
            candidate->texture->UseColors = true;
            std::vector<std::uint8_t>().swap(candidate->pixels);
            ++cache.textureCount;
        }
        if (!Registered(*context, candidate->texture.get())) ImGui::RegisterUserTexture(candidate->texture.get());
    }

    std::optional<ItemIconView> FindItemIcon(std::string_view iconKey) {
        if (!ValidKey(iconKey)) return {};
        auto* context = ImGui::GetCurrentContext();
        auto& cache = State();
        if (!context || cache.context != context || !cache.epoch || cache.workerFailed) return {};
        auto found = cache.entries.find(iconKey);
        if (found == cache.entries.end()) {
            if (cache.entries.size() >= MaxEntries) {
                auto oldest = cache.entries.end();
                for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it)
                    if (!it->second.texture && it->second.phase != Phase::Requested && it->second.phase != Phase::Decoding &&
                        (oldest == cache.entries.end() || it->second.lastTouch < oldest->second.lastTouch)) oldest = it;
                if (oldest == cache.entries.end()) return {};
                cache.cpuBytes -= oldest->second.pixels.size();
                cache.entries.erase(oldest);
            }
            found = cache.entries.try_emplace(std::string(iconKey)).first;
            found->second.epoch = cache.epoch;
        }
        auto& entry = found->second;
        entry.lastTouch = cache.frame;
        if (entry.obsolete || entry.retiring || entry.epoch != cache.epoch) return {};
        if (entry.phase == Phase::Empty && Clock::now() >= entry.retryAfter) {
            if (gameplay::RequestItemImage(iconKey)) entry.phase = Phase::Requested;
            else entry.retryAfter = Clock::now() + std::chrono::milliseconds(100);
        }
        if (!entry.texture || !Registered(*context, entry.texture.get()) ||
            (entry.texture->Status != ImTextureStatus_OK && entry.texture->Status != ImTextureStatus_WantCreate)) return {};
        return ItemIconView{entry.texture->GetTexRef(), ImVec2(0, 0), ImVec2(1, 1),
            ImVec2(static_cast<float>(entry.width), static_cast<float>(entry.height))};
    }
}
