#include "../src/item_icons.cpp"
#include "../src/gameplay/item_images.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace gloss { void Log(std::string_view text) { std::cout << text << '\n'; } }
namespace {
    int checks{}, uploads{};
    void Check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
    using namespace gloss::ui;
    using gloss::gameplay::ItemImageResult;
    using Format = gloss::gameplay::ItemImageFormat;
    void Frame(const std::vector<std::string>& visible = {}) {
        ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
        ImGui::GetIO().DeltaTime = 1.f / 60;
        ImGui::NewFrame();
        BeginItemIconFrame();
        for (const auto& key : visible) FindItemIcon(key);
        ImGui::EndFrame();
        int created{};
        for (auto* texture : ImGui::GetCurrentContext()->UserTextures)
            if (texture->Status == ImTextureStatus_WantCreate) {
                ++created;
                texture->SetTexID(17);
                texture->BackendUserData = reinterpret_cast<void*>(1);
                texture->SetStatus(ImTextureStatus_OK);
            }
        uploads += created;
        Check(created <= 1, "more than one texture upload per frame");
    }
    void ResetGpu() {
        for (auto& [key, entry] : State().entries)
            if (entry.texture) {
                entry.texture->BackendUserData = nullptr;
                entry.texture->SetTexID(ImTextureID_Invalid);
                entry.texture->SetStatus(ImTextureStatus_Destroyed);
            }
    }
    void Context() {
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ItemImageResult Solid(std::string key, Format format = Format::Rgba8) {
        return {1, std::move(key), 4, 4, format, std::vector<std::uint8_t>(64, 255), {}};
    }
}
int main(int argc, char** argv) {
    try {
        Check(argc <= 2, "usage: PBZItemIconsTests [runtime evidence directory]");
        auto rgba = Solid("/Game/Test");
        rgba.data[0] = 31; rgba.data[2] = 69;
        auto decoded = Decode(rgba);
        Check(decoded.pixels == rgba.data, "RGBA bytes changed");
        rgba.format = Format::Bgra8;
        decoded = Decode(rgba);
        Check(decoded.pixels[0] == 69 && decoded.pixels[2] == 31, "BGRA channels not swapped");
        const std::array<std::uint8_t, 8> red{0, 248, 0, 0, 0, 0, 0, 0};
        ItemImageResult bc{1, "/Game/Crop", 3, 5, Format::Bc1, {}, {}};
        bc.data.insert(bc.data.end(), red.begin(), red.end());
        bc.data.insert(bc.data.end(), red.begin(), red.end());
        decoded = Decode(bc);
        Check(decoded.pixels.size() == 60, "BC cropped dimensions wrong");
        for (std::size_t i = 0; i < decoded.pixels.size(); i += 4)
            Check(decoded.pixels[i] == 255 && decoded.pixels[i + 1] == 0 && decoded.pixels[i + 2] == 0 && decoded.pixels[i + 3] == 255, "BC1 border crop changed color");
        for (const auto format : {Format::Bc2, Format::Bc3}) {
            bc.format = format;
            bc.data.clear();
            std::array<std::uint8_t, 8> alpha{};
            if (format == Format::Bc2) alpha.fill(255); else alpha[0] = 255;
            for (int i = 0; i < 2; ++i) { bc.data.insert(bc.data.end(), alpha.begin(), alpha.end()); bc.data.insert(bc.data.end(), red.begin(), red.end()); }
            decoded = Decode(bc);
            Check(decoded.pixels.size() == 60 && decoded.pixels[0] == 255 && decoded.pixels.back() == 255, "BC2/3 alpha or border decode failed");
        }
        bc.format = Format::Bc7; bc.data.assign(32, 0);
        Check(Decode(bc).pixels.size() == 60, "BC7 border decode failed");
        bc.data.pop_back(); Check(Decode(bc).pixels.empty(), "truncated block accepted");
        bc.width = 2049; Check(Decode(bc).pixels.empty(), "oversize image accepted");
        auto edge = Solid("/Game/Edge"); edge.width = 512; edge.height = 2; edge.data.resize(512 * 2 * 4);
        for (std::size_t i = 0; i < edge.data.size(); i += 4) {
            const bool opaque = ((i / 4) % 512) % 2 == 0;
            edge.data[i] = opaque ? 255 : 0; edge.data[i + 1] = 0; edge.data[i + 2] = opaque ? 0 : 255; edge.data[i + 3] = opaque ? 255 : 0;
        }
        decoded = Decode(edge);
        Check(decoded.width == 256 && decoded.height == 1 && decoded.pixels[0] == 255 && decoded.pixels[2] == 0 && decoded.pixels[3] == 128, "downsample caused transparent color fringe");
        ItemImageResult largeBc3{1, "/Game/Synthetic512", 512, 512, Format::Bc3, {}, {}};
        const std::array<std::uint8_t, 8> opaqueAlpha{255, 0, 0, 0, 0, 0, 0, 0};
        for (int block = 0; block < 128 * 128; ++block) {
            largeBc3.data.insert(largeBc3.data.end(), opaqueAlpha.begin(), opaqueAlpha.end());
            largeBc3.data.insert(largeBc3.data.end(), red.begin(), red.end());
        }
        decoded = Decode(largeBc3);
        Check(decoded.width == 256 && decoded.height == 256 && decoded.pixels.size() == 256 * 256 * 4,
            "512x512 BC3 downsample dimensions wrong");
        bool solidRed = true;
        for (std::size_t i = 0; i < decoded.pixels.size(); i += 4) {
            solidRed = solidRed && decoded.pixels[i] == 255 && decoded.pixels[i + 1] == 0 &&
                decoded.pixels[i + 2] == 0 && decoded.pixels[i + 3] == 255;
        }
        Check(solidRed, "512x512 BC3 downsample changed opaque red pixels");
        // Real game captures are optional local evidence, never required by CI.
        if (argc == 2) {
            const auto root = std::filesystem::canonical(argv[1]);
            std::ifstream input(root / "heishang.dxt5", std::ios::binary);
            Check(input.good(), "missing real game BC3 payload");
            ItemImageResult real{1, "/Game/Heishang", 512, 512, Format::Bc3,
                std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {}), {}};
            decoded = Decode(std::move(real));
            Check(decoded.width == 256 && decoded.height == 256 && decoded.pixels.size() == 256 * 256 * 4, "real game image failed");
            std::ofstream output(root / "heishang-cache256.rgba", std::ios::binary);
            output.write(reinterpret_cast<const char*>(decoded.pixels.data()), decoded.pixels.size()); output.close();
            Check(output.good(), "failed to write real game decoded evidence");
        }

        gloss::gameplay::ResetItemImages(1);
        Context(); Frame();
        Check(!FindItemIcon("/Game/Async"), "pending image returned a texture");
        const auto request = gloss::gameplay::TakeItemImageRequest();
        Check(request && request->key == "/Game/Async" && request->epoch == 1, "native image request not queued");
        FindItemIcon("/Game/Async"); Check(!gloss::gameplay::TakeItemImageRequest(), "duplicate pending request");
        gloss::gameplay::PublishItemImage(Solid("/Game/Async"));
        for (int i = 0; i < 200 && !FindItemIcon("/Game/Async"); ++i) { Frame(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        Check(FindItemIcon("/Game/Async").has_value(), "asynchronous result not uploaded");
        auto& cache = State();
        std::vector<std::string> visible;
        for (int i = 0; i < 64; ++i) {
            Entry entry; entry.epoch = 1; entry.phase = Phase::Ready; entry.width = entry.height = 4; entry.pixels.assign(64, 255);
            const auto key = "/Game/Seed" + std::to_string(i);
            cache.entries.emplace(key, std::move(entry)); cache.cpuBytes += 64; visible.push_back(key);
        }
        for (int i = 0; i < 65; ++i) Frame(visible);
        Check(cache.textureCount == 64, "GPU budget not enforced");
        int retiredCalls{};
        SetItemIconRetirementCallback([&](std::span<ImTextureData* const>) { ++retiredCalls; return false; });
        Frame(visible); Check(retiredCalls == 1 && cache.textureCount == 64, "failed retirement modified cache");
        Frame(visible); Check(retiredCalls == 1, "failed retirement retried without backoff");
        SetItemIconRetirementCallback([&](std::span<ImTextureData* const> textures) {
            ++retiredCalls; Check(textures.size() <= 8, "retirement batch too large");
            for (auto* texture : textures) {
                texture->WantDestroyNextFrame = true;
                texture->BackendUserData = nullptr;
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
            return true;
        });
        cache.nextRetirement = {};
        Frame(visible); Check(cache.textureCount == 64 && !cache.entries.at("/Game/Async").texture,
            "completed retirement not reclaimed before visible upload");
        visible.resize(20);
        const auto settledUploads = uploads, settledRetires = retiredCalls;
        for (int i = 0; i < 100; ++i) { cache.nextRetirement = {}; Frame(visible); }
        Check(uploads == settledUploads && retiredCalls == settledRetires,
            "offscreen CPU entries caused upload/retirement churn on a fixed page");
        for (auto* texture : ImGui::GetPlatformIO().Textures) {
            bool known = texture == ImGui::GetIO().Fonts->TexData;
            for (const auto& [key, entry] : cache.entries) known = known || (entry.texture.get() == texture);
            Check(known, "dangling PlatformIO texture after retirement");
        }
        ResetGpu();
        gloss::gameplay::ResetItemImages(2); Frame();
        Check(!FindItemIcon("/Game/Async"), "old epoch image remained visible");
        Check(cache.textureCount == 0, "obsolete textures without GPU allocations not reclaimed");
        ImGui::DestroyContext();
        Check(cache.context == nullptr, "context hook did not unbind");
        for (int i = 0; i < 200; ++i) {
            bool running; { std::lock_guard lock(cache.workerMutex); running = cache.workerRunning; }
            if (!running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        { std::lock_guard lock(cache.workerMutex); Check(!cache.workerRunning, "decoder did not stop"); }
        Context(); Frame(); Check(cache.workerStarted, "decoder did not restart with new context");
        StopItemIconWorker(); SetItemIconRetirementCallback({}); ImGui::DestroyContext();
        std::cout << "Dynamic item icon tests passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
