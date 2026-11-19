#include "native_dx12_readback.hpp"
#include "icons.hpp"
#include "model.hpp"
#include "runtime.hpp"

#include <backends/imgui_impl_dx12.h>
#include <imgui_internal.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gloss::dndg {
void Log(const char* message) noexcept { OutputDebugStringA(message); }
}

namespace {
using namespace gloss::dndg;
using gloss::dndg::validation::Dx12Readback;
using Microsoft::WRL::ComPtr;
using Pixels = std::vector<std::uint8_t>;
using Rgba = std::array<std::uint8_t, 4>;
constexpr Rgba Background{28, 44, 36, 255};
constexpr Rgba RedIcon{230, 52, 37, 255};
constexpr Rgba BlueIcon{27, 94, 214, 255};
constexpr Rgba GreenIcon{35, 172, 95, 255};
constexpr Rgba PurpleIcon{154, 48, 200, 255};

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct ComApartment {
    ComApartment() { Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "initialize COM for PNG encoder"); }
    ~ComApartment() { CoUninitialize(); }
};

void WritePng(const std::filesystem::path& path, unsigned width, unsigned height, const Pixels& pixels) {
    ComPtr<IWICImagingFactory> factory;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))), "create WIC factory");
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height)), "initialize PNG output");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA),
        "PNG encoder accepts BGRA8");
    Pixels bgra = pixels;
    for (std::size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    Check(SUCCEEDED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "write PNG pixels");
}

void ReplyToIconRequests() {
    while (true) {
        auto key = TakeIconRequest();
        if (key.empty()) return;
        const auto* color = key == "fixture/red" ? &RedIcon : key == "fixture/blue" ? &BlueIcon :
            key == "fixture/green" ? &GreenIcon : key == "fixture/purple" ? &PurpleIcon : nullptr;
        Check(color != nullptr, "unexpected icon request");
        IconPixels icon{std::move(key), 39, 62, {}};
        for (int row = 0; row < icon.height; ++row) {
            for (int col = 0; col < icon.width; ++col) {
                const auto& pixel = (row < 6 || col < 3 || row > 55 || col > 35) ? Rgba{237, 220, 188, 255} : *color;
                icon.rgba.insert(icon.rgba.end(), pixel.begin(), pixel.end());
            }
        }
        PublishIconPixels(std::move(icon));
    }
}

Pixels DrawFrame(Dx12Readback& gpu) {
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    DrawNativeOverlay(nullptr);
    ImGui::Render();
    const auto* draw = ImGui::GetDrawData();
    Check(draw && draw->DisplaySize.x == gpu.Width() && draw->DisplaySize.y == gpu.Height() &&
        draw->FramebufferScale.x == 1 && draw->FramebufferScale.y == 1, "draw data must match final output at 1:1");
    auto pixels = gpu.Render(ImGui::GetDrawData());
    Check(pixels.size() == static_cast<std::size_t>(gpu.Width()) * gpu.Height() * 4, "RGBA readback byte count");
    ReplyToIconRequests();
    return pixels;
}

Pixels Settle(Dx12Readback& gpu, int frames) {
    Pixels result;
    for (int i = 0; i < frames; ++i) result = DrawFrame(gpu);
    return result;
}

Rgba Sample(const Pixels& pixels, unsigned width, unsigned x, unsigned y) {
    const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

std::size_t CountColor(const Pixels& pixels, const Rgba& target) {
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
        count += std::equal(target.begin(), target.end(), pixels.data() + offset);
    return count;
}

ImVec2 FindIconPoint(const Pixels& pixels, unsigned width, const Rgba& color) {
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
        if (!std::equal(color.begin(), color.end(), pixels.data() + offset)) continue;
        const auto pixel = offset / 4;
        return {static_cast<float>(pixel % width) + 4.f, static_cast<float>(pixel / width) + 4.f};
    }
    throw std::runtime_error("find icon in rasterized grid");
}

void Click(Dx12Readback& gpu, ImVec2 point, ImGuiMouseButton button = ImGuiMouseButton_Left) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(point.x, point.y);
    DrawFrame(gpu);
    io.AddMouseButtonEvent(button, true);
    DrawFrame(gpu);
    io.AddMouseButtonEvent(button, false);
    DrawFrame(gpu);
}

Rgba WidgetColor(ImGuiCol slot) {
    const auto color = ImGui::GetColorU32(slot);
    return {static_cast<std::uint8_t>(color >> IM_COL32_R_SHIFT), static_cast<std::uint8_t>(color >> IM_COL32_G_SHIFT),
        static_cast<std::uint8_t>(color >> IM_COL32_B_SHIFT), static_cast<std::uint8_t>(color >> IM_COL32_A_SHIFT)};
}

bool IsWidgetPixel(const Rgba& pixel, ImGuiCol slot) {
    const auto foreground = WidgetColor(slot), background = WidgetColor(ImGuiCol_WindowBg);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const int expected = (foreground[channel] * foreground[3] + background[channel] * (255 - foreground[3]) + 127) / 255;
        if (std::abs(expected - pixel[channel]) > 1) return false;
    }
    return pixel[3] == 255;
}

void TestVitalCommands(Dx12Readback& gpu, const ImGuiWindow& panel, const Pixels& player, Snapshot match) {
    // In the unhovered output, find the three rectangular integer controls
    // by their actual rasterized background and find Apply on each same row.
    const float fontSize = ImGui::GetStyle().FontSizeBase * ImGui::GetStyle().FontScaleDpi;
    const auto inputX = static_cast<unsigned>(panel.Pos.x + fontSize * 9.f);
    const auto right = static_cast<unsigned>(panel.Pos.x + panel.Size.x - 8.f);
    const auto bottom = static_cast<unsigned>(panel.Pos.y + panel.Size.y - 8.f);
    std::vector<unsigned> rows;
    for (auto y = static_cast<unsigned>(panel.Pos.y); y < bottom;) {
        if (!IsWidgetPixel(Sample(player, gpu.Width(), inputX, y), ImGuiCol_FrameBg)) { ++y; continue; }
        const auto start = y++;
        while (y < bottom && IsWidgetPixel(Sample(player, gpu.Width(), inputX, y), ImGuiCol_FrameBg)) ++y;
        if (y - start >= fontSize * .7f) rows.push_back((start + y) / 2);
    }
    Check(rows.size() == 3, "find the three integer fields in the player panel");

    constexpr std::array operations{Edit::Health, Edit::Shield, Edit::Chips};
    constexpr std::array values{64, 19, 512};
    const auto applyPoint = [&](unsigned y) {
        auto x = inputX;
        while (x + 4 < right && (!IsWidgetPixel(Sample(player, gpu.Width(), x, y), ImGuiCol_Button) ||
            !IsWidgetPixel(Sample(player, gpu.Width(), x + 4, y), ImGuiCol_Button))) ++x;
        Check(x + 4 < right, "find same-row Apply button");
        return ImVec2(static_cast<float>(x + 3), static_cast<float>(y));
    };
    auto& io = ImGui::GetIO();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto y = rows[i];
        Click(gpu, {static_cast<float>(inputX), static_cast<float>(y)});
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        DrawFrame(gpu);
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        DrawFrame(gpu);
        io.AddInputCharactersUTF8(std::to_string(values[i]).c_str());
        DrawFrame(gpu);
        Check(TakeCommands().empty(), "typing an integer does not submit a write");
        // Refresh while editing to catch live synchronization clobbering drafts.
        ++match.chips;
        PublishSnapshot(match);
        DrawFrame(gpu);
        Click(gpu, applyPoint(y));
        const auto commands = TakeCommands();
        Check(commands.size() == 1 && commands[0].encounter == match.encounter &&
            commands[0].edit == operations[i] && commands[0].value == values[i],
            "Apply emits one command using the typed value and correct encounter");
    }

    match.canEditVitals = false;
    PublishSnapshot(match);
    DrawFrame(gpu);
    Click(gpu, applyPoint(rows.front()));
    Check(TakeCommands().empty(), "disabled Apply cannot enqueue a write");
}

void SelectTab(ImGuiWindow& panel, const char* id) {
    ImGuiTabBar* tabs = nullptr;
    auto& pool = ImGui::GetCurrentContext()->TabBars;
    for (int i = 0; i < pool.GetMapSize(); ++i) {
        auto* candidate = pool.TryGetMapData(i);
        if (candidate && panel.Rect().Contains(candidate->BarRect.Min)) { tabs = candidate; break; }
    }
    Check(tabs != nullptr, "find production tab bar");
    for (auto& entry : tabs->Tabs) {
        if (std::string_view(ImGui::TabBarGetTabName(tabs, &entry)).ends_with(id)) {
            tabs->NextSelectedTabId = entry.ID;
            return;
        }
    }
    throw std::runtime_error("find requested production tab");
}

ImVec2 MenuRow(unsigned row) {
    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    Check(popups.Size == 1 && popups.back().Window, "find context menu");
    const auto* popup = popups.back().Window;
    if (row == 2 && ImGui::GetCurrentContext()->LastItemData.Rect.GetHeight() > 0.f)
        return {ImGui::GetCurrentContext()->LastItemData.Rect.Min.x + 15.f, ImGui::GetCurrentContext()->LastItemData.Rect.GetCenter().y};
    const auto start = popup->DC.CursorStartPos;
    const float step = ImGui::GetTextLineHeightWithSpacing();
    return {start.x + 15.f, start.y + ImGui::GetFontSize() * .45f + step * row};
}

ImVec2 FindAddTile(const Pixels& pixels, unsigned width, Kind kind) {
    const auto* panel = ImGui::FindWindowByName("GlossTrainer | DnDG");
    const float font = ImGui::GetFontSize();
    const unsigned minY = panel ? static_cast<unsigned>(panel->Pos.y + 4.8f * font) : 0;
    const unsigned maxY = panel ? static_cast<unsigned>(panel->Pos.y + panel->Size.y - 4.f * font) : static_cast<unsigned>(ImGui::GetIO().DisplaySize.y);
    struct Row { unsigned firstY{}, lastY{}, start{}, end{}; };
    std::vector<Row> rows;
    for (unsigned y = minY; y < maxY; ++y) {
        bool hasRun = false;
        unsigned rightStart = 0, rightEnd = 0;
        for (unsigned x = 0; x < width;) {
            if (!IsWidgetPixel(Sample(pixels, width, x, y), ImGuiCol_Button)) {
                ++x;
                continue;
            }
            const unsigned start = x++;
            while (x < width && IsWidgetPixel(Sample(pixels, width, x, y), ImGuiCol_Button)) ++x;
            if (x - start >= 20 && (!hasRun || x > rightEnd)) {
                hasRun = true;
                rightStart = start;
                rightEnd = x;
            }
        }
        if (!hasRun)
            continue;
        if (rows.empty() || y > rows.back().lastY + 1)
            rows.push_back({y, y, rightStart, rightEnd});
        else {
            rows.back().lastY = y;
            if (rightEnd > rows.back().end)
                rows.back().start = rightStart, rows.back().end = rightEnd;
        }
    }
    Check(!rows.empty(), "find an add tile row in the current grid");
    const auto pick = kind == Kind::UniqueChip ? std::max_element(rows.begin(), rows.end(),
        [](const Row& a, const Row& b) { return a.lastY < b.lastY; }) :
        std::min_element(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.firstY < b.firstY; });
    Check(pick != rows.end(), "select the requested add tile row");
    return {static_cast<float>((pick->start + pick->end) / 2), static_cast<float>((pick->firstY + pick->lastY) / 2)};
}
void TestPicker(Dx12Readback& gpu, const Pixels& grid, const Rgba& ownedIcon, Edit verb, Kind kind,
        std::uint64_t encounter, std::uint64_t token, std::string_view selectedId,
        const std::filesystem::path& image = {}) {
    if (verb == Edit::Add) {
        Click(gpu, FindAddTile(grid, gpu.Width(), kind));
    } else {
        Click(gpu, FindIconPoint(grid, gpu.Width(), ownedIcon), ImGuiMouseButton_Right);
        Settle(gpu, 2);
        Click(gpu, MenuRow(1));
    }
    Check(TakeCommands().empty(), "opening a picker does not apply a command");
    auto picker = Settle(gpu, 9);
    const auto first = FindIconPoint(picker, gpu.Width(), GreenIcon);
    const auto second = FindIconPoint(picker, gpu.Width(), PurpleIcon);
    Check(first.y == second.y && first.x < second.x, "catalog icons run left to right in the same grid row");
    if (!image.empty()) WritePng(image, gpu.Width(), gpu.Height(), picker);
    Click(gpu, second);
    const auto commands = TakeCommands();
    if (!(commands.size() == 1 && commands[0].encounter == encounter && commands[0].edit == verb &&
        commands[0].kind == kind && commands[0].token == token && commands[0].id == selectedId)) {
        std::cerr << "picker mismatch: expected edit=" << static_cast<int>(verb) << " kind=" << static_cast<int>(kind)
            << " token=" << token << " id=" << selectedId << " got count=" << commands.size();
        if (!commands.empty()) std::cerr << " edit=" << static_cast<int>(commands[0].edit) << " kind=" << static_cast<int>(commands[0].kind)
            << " token=" << commands[0].token << " id=" << commands[0].id << " encounter=" << commands[0].encounter;
        std::cerr << '\n';
        throw std::runtime_error("click on the catalog icon selects the expected ID and target");
    }
}

void Capture(Dx12Readback& gpu, const std::filesystem::path& out, bool dark) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(gpu.Width()), static_cast<float>(gpu.Height())};
    io.DisplayFramebufferScale = {1.f, 1.f};
    io.DeltaTime = 1.f / 30.f;
    io.AddMousePosEvent(600.f, 550.f);

    Snapshot match;
    match.encounter = 11;
    match.locale = Locale::Chinese;
    match.canEditVitals = match.canEditHand = match.canEditItems = true;
    match.health = 78;
    match.maxHealth = 100;
    match.shield = 6;
    match.chips = 250;
    match.sleeve = {{1, "126", "红心牌", "出牌时：获得护盾。", "fixture/red"},
                    {2, "140", "黑桃牌", "弃牌时：获得筹码。", "fixture/blue"}};
    match.hand = {{3, "141", "桌面牌", "护盾加一。", "fixture/red"}};
    match.charms = {{11, "13", "幸运护符", "回合开始时获得护盾。", "fixture/red"}};
    match.uniqueChips = {{12, "004", "独特筹码", "使用时获得筹码。", "fixture/blue"}};
    Catalog catalog;
    catalog.cards = {{0, "4", "红心四", {}, "fixture/green"}, {0, "8", "红心八", {}, "fixture/purple"}};
    catalog.charms = {{0, "15", "白猫", {}, "fixture/green"}, {0, "48", "招财猫", {}, "fixture/purple"}};
    catalog.uniqueChips = {{0, "002", "蓝筹码", {}, "fixture/green"}, {0, "013", "金筹码", {}, "fixture/purple"}};
    PublishCatalog(std::move(catalog));
    match.nextCard = Entry{4, "144", "四条", "出牌时：随机选择一个点数。"};
    PublishSnapshot(match);

    const auto beforeHover = Settle(gpu, 3);
    const auto* preview = ImGui::FindWindowByName("##next-card");
    Check(preview && ReadUiRequest().showNext, "next-card window visible");
    io.AddMousePosEvent(preview->Pos.x + preview->WindowPadding.x + 10.f,
        preview->Pos.y + preview->WindowPadding.y + 5.f);
    auto hovered = Settle(gpu, 30);
    Check(hovered != beforeHover && CountColor(hovered, Background) < CountColor(beforeHover, Background),
        "hovered preview adds visible tooltip pixels");
    WritePng(out / "preview.png", gpu.Width(), gpu.Height(), hovered);

    io.AddMousePosEvent(600.f, 550.f);
    Check(HandleNativeKey(VK_INSERT, false), "default insert opens panel");
    auto player = Settle(gpu, 4);
    auto* window = ImGui::FindWindowByName("GlossTrainer | DnDG");
    Check(window && window->Size.x < gpu.Width() * .4f && window->Size.y < gpu.Height() * .5f,
        "panel remains compact at both output resolutions");
    Check(Sample(player, gpu.Width(), 12, 12) == Background &&
        Sample(player, gpu.Width(), gpu.Width() / 2, gpu.Height() / 2) == Background,
        "panel does not cover the preview origin or the center of the game");
    const auto background = Sample(player, gpu.Width(), static_cast<unsigned>(window->Pos.x + window->Size.x - 26.f),
        static_cast<unsigned>(window->Pos.y + window->Size.y - 55.f));
    Check(dark ? background[0] < 80 : background[0] > 180, "dark/light theme is visible on the framebuffer");
    auto* font = io.FontDefault->GetFontBaked(16.f * ImGui::GetStyle().FontScaleDpi);
    Check(font && font->FindGlyphNoFallback(0x751f), "loaded UI font supplies the Chinese health glyph");
    WritePng(out / "player.png", gpu.Width(), gpu.Height(), player);
    TestVitalCommands(gpu, *window, player, match);
    PublishSnapshot(match);

    SelectTab(*window, "###cards");
    auto cards = Settle(gpu, 8);
    const auto red = CountColor(cards, RedIcon), blue = CountColor(cards, BlueIcon);
    Check(red > 250 && blue > 250, "first and incremental atlas uploads rasterize both colored card icons");
    WritePng(out / "cards.png", gpu.Width(), gpu.Height(), cards);

    // Drive only ImGui's input queue, not Win32. Keep the pointer on the actual
    // rasterized icon until its tooltip appears, then open its right-click menu.
    const auto cursor = FindIconPoint(cards, gpu.Width(), RedIcon);
    io.AddMousePosEvent(cursor.x, cursor.y);
    const auto cardTooltip = Settle(gpu, 25);
    bool hoveredCard = false;
    for (const auto* w : ImGui::GetCurrentContext()->Windows) {
        if ((w->Flags & ImGuiWindowFlags_Tooltip) && w->Active && !w->Hidden) hoveredCard = true;
    }
    Check(hoveredCard && cardTooltip != cards, "hovering a card opens its tooltip");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    DrawFrame(gpu);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    const auto contextMenu = Settle(gpu, 3);
    auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    Check(popups.Size == 1 && popups.back().Window && contextMenu != cardTooltip,
        "right clicking the hovered card opens a visible context menu");
    WritePng(out / "card-menu.png", gpu.Width(), gpu.Height(), contextMenu);
    const auto menuId = popups.back().PopupId;

    // Change grid order while the popup is open. Only the original ObjectID
    // may be sent to the game thread when the first action (Discard) is chosen.
    std::swap(match.sleeve[0], match.sleeve[1]);
    PublishSnapshot(match);
    Settle(gpu, 3);
    Check(popups.Size == 1 && popups.back().PopupId == menuId && popups.back().Window,
        "card context menu persists across reordering");
    Click(gpu, MenuRow(0));
    const auto commands = TakeCommands();
    Check(commands.size() == 1 && commands[0].edit == Edit::Discard && commands[0].kind == Kind::Sleeve &&
        commands[0].encounter == match.encounter && commands[0].token == 1,
        "discard action targets the original card, not the reordered row");

    TestPicker(gpu, Settle(gpu, 2), RedIcon, Edit::Replace, Kind::Sleeve, match.encounter, 1, "8");
    TestPicker(gpu, Settle(gpu, 2), BlueIcon, Edit::Add, Kind::Sleeve, match.encounter, 0, "8");
    SelectTab(*window, "###items");
    TestPicker(gpu, Settle(gpu, 4), RedIcon, Edit::Add, Kind::Charm, match.encounter, 0, "48", out / "items.png");
    TestPicker(gpu, Settle(gpu, 4), BlueIcon, Edit::Add, Kind::UniqueChip, match.encounter, 0, "013");

    std::cout << "D3D12 WARP readback " << gpu.Width() << 'x' << gpu.Height() << ' ' << (dark ? "dark" : "light")
        << " panel=" << window->Size.x << 'x' << window->Size.y << " red=" << red << " blue=" << blue << " card_menu_token=" << commands[0].token
        << " vitals=3/3 card_picker=2/2 item_picker=2/2" << '\n';
}

struct Context {
    Context() { ImGui::CreateContext(); }
    ~Context() { ImGui::DestroyContext(); }
};
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        Check(argc == 5, "usage: GlossTrainerDnDGDx12CaptureTests <fixture.ini> <new output dir> <720|1440> <light|dark>");
        const std::wstring_view resolution(argv[3]), theme(argv[4]);
        Check(resolution == L"720" || resolution == L"1440", "resolution must be 720 or 1440");
        Check(theme == L"light" || theme == L"dark", "theme must be light or dark");
        Check(std::filesystem::is_regular_file(argv[1]), "fixture INI must exist");
        const std::filesystem::path out(argv[2]);
        Check(std::filesystem::create_directory(out), "output directory must not already exist");
        ComApartment com;
        Context context;
        ConfigureNativeImGui(argv[1]);
        const unsigned multiplier = resolution == L"1440" ? 2 : 1;
        Dx12Readback gpu(1280 * multiplier, 720 * multiplier);
        Capture(gpu, out, theme == L"dark");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
