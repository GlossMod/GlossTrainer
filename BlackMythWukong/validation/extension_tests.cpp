#include "../src/gameplay/engine_bridge.hpp"
#include "../src/gameplay/extensions.hpp"
#include "../src/runtime.hpp"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace gloss::gameplay::extensions::catalog_validation
{
#include "../src/gameplay/catalog_data.inc"
}

namespace
{
    int failures{};

#define CHECK(expression)                                                                 \
    do                                                                                    \
    {                                                                                     \
        if (!(expression))                                                                \
        {                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
            ++failures;                                                                   \
        }                                                                                 \
    } while (false)

    using gloss::gameplay::engine::DiscoverBindings;

    struct SyntheticPe
    {
        static constexpr std::size_t ImageSize = 0x3000;
        static constexpr std::size_t NtOffset = 0x80;
        static constexpr std::size_t TextRva = 0x400;
        static constexpr std::size_t TextSize = 0x400;
        static constexpr std::size_t DataRva = 0x1000;
        static constexpr std::size_t DataSize = 0x1000;

        std::vector<std::byte> bytes = std::vector<std::byte>(ImageSize);

        SyntheticPe() { InitializeHeaders(); }

        IMAGE_DOS_HEADER *Dos() { return reinterpret_cast<IMAGE_DOS_HEADER *>(bytes.data()); }
        IMAGE_NT_HEADERS64 *Nt() { return reinterpret_cast<IMAGE_NT_HEADERS64 *>(bytes.data() + NtOffset); }
        IMAGE_SECTION_HEADER *Sections()
        {
            return reinterpret_cast<IMAGE_SECTION_HEADER *>(bytes.data() + SectionOffset());
        }
        std::size_t SectionOffset() const
        {
            return NtOffset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + sizeof(IMAGE_OPTIONAL_HEADER64);
        }

        void InitializeHeaders()
        {
            auto *dos = Dos();
            dos->e_magic = IMAGE_DOS_SIGNATURE;
            dos->e_lfanew = static_cast<LONG>(NtOffset);

            auto *nt = Nt();
            nt->Signature = IMAGE_NT_SIGNATURE;
            nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
            nt->FileHeader.NumberOfSections = 2;
            nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
            nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
            nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(ImageSize);

            auto *sections = Sections();
            std::memcpy(sections[0].Name, ".text", 5);
            sections[0].Misc.VirtualSize = static_cast<DWORD>(TextSize);
            sections[0].VirtualAddress = static_cast<DWORD>(TextRva);
            sections[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

            std::memcpy(sections[1].Name, ".rdata", 6);
            sections[1].Misc.VirtualSize = static_cast<DWORD>(DataSize);
            sections[1].VirtualAddress = static_cast<DWORD>(DataRva);
            sections[1].Characteristics = IMAGE_SCN_MEM_READ;
        }

        void WriteString(std::size_t rva, std::string_view value, bool terminate = true)
        {
            std::memcpy(bytes.data() + rva, value.data(), value.size());
            if (terminate)
                bytes[rva + value.size()] = std::byte{};
        }

        void WriteBinding(std::size_t codeRva, std::size_t nameRva, std::size_t targetRva)
        {
            std::array<unsigned char, 16> code{
                0x48, 0x8d, 0x15, 0, 0, 0, 0, // lea rdx,[rip+name]
                0x48, 0x8d, 0x0d, 0, 0, 0, 0, // lea rcx,[rip+function]
                0xff, 0xd3                    // call rbx
            };
            const auto nameDisplacement = static_cast<std::int32_t>(
                static_cast<std::int64_t>(nameRva) - static_cast<std::int64_t>(codeRva + 7));
            const auto targetDisplacement = static_cast<std::int32_t>(
                static_cast<std::int64_t>(targetRva) - static_cast<std::int64_t>(codeRva + 14));
            std::memcpy(code.data() + 3, &nameDisplacement, sizeof(nameDisplacement));
            std::memcpy(code.data() + 10, &targetDisplacement, sizeof(targetDisplacement));
            std::memcpy(bytes.data() + codeRva, code.data(), code.size());
        }

        std::uintptr_t Address(std::size_t rva) const
        {
            return reinterpret_cast<std::uintptr_t>(bytes.data()) + rva;
        }
    };

    void TestDiscoverCompleteAndAdjacentBindings()
    {
        SyntheticPe image;
        constexpr std::size_t firstName = 0x1100;
        constexpr std::string_view first = "Export_First";
        constexpr auto secondName = firstName + first.size() + 1;
        constexpr std::string_view second = "Export_Second";
        image.WriteString(firstName, first);
        image.WriteString(secondName, second);
        image.WriteBinding(0x500, firstName, 0x600);
        image.WriteBinding(0x520, secondName, 0x640);

        // A near match with one wrong opcode must not count as an engine binding.
        std::array<unsigned char, 16> decoy{};
        decoy[0] = 0x48;
        decoy[1] = 0x8d;
        decoy[2] = 0x15;
        decoy[7] = 0x48;
        decoy[8] = 0x89;
        decoy[9] = 0x0d;
        decoy[14] = 0xff;
        decoy[15] = 0xd3;
        std::memcpy(image.bytes.data() + 0x540, decoy.data(), decoy.size());

        const auto found = DiscoverBindings(std::span<const std::byte>(image.bytes));
        CHECK(found.size() == 2);
        CHECK(found.contains(std::string(first)));
        CHECK(found.contains(std::string(second)));
        if (found.contains(std::string(first)))
            CHECK(found.at(std::string(first)) == image.Address(0x600));
        if (found.contains(std::string(second)))
            CHECK(found.at(std::string(second)) == image.Address(0x640));
    }

    void TestDiscoverRejectsDuplicateNames()
    {
        SyntheticPe image;
        image.WriteString(0x1100, "Export_Duplicate");
        image.WriteString(0x1140, "Export_Duplicate");
        image.WriteBinding(0x500, 0x1100, 0x600);
        image.WriteBinding(0x520, 0x1140, 0x640);
        const auto found = DiscoverBindings(std::span<const std::byte>(image.bytes));
        CHECK(!found.contains("Export_Duplicate"));
        CHECK(found.empty());
    }

    void TestDiscoverRejectsNonExecutableAndOutOfBoundsTargets()
    {
        {
            SyntheticPe image;
            image.WriteString(0x1100, "Export_DataTarget");
            image.WriteBinding(0x500, 0x1100, 0x1200); // The target is in .rdata, not executable.
            CHECK(DiscoverBindings(std::span<const std::byte>(image.bytes)).empty());
        }
        {
            SyntheticPe image;
            image.WriteString(0x1100, "Export_OutsideImage");
            image.WriteBinding(0x500, 0x1100, SyntheticPe::ImageSize + 0x100);
            CHECK(DiscoverBindings(std::span<const std::byte>(image.bytes)).empty());
        }
        {
            SyntheticPe image;
            constexpr std::size_t edgeName = SyntheticPe::DataRva + SyntheticPe::DataSize - 8;
            image.WriteString(edgeName, "Export_X", false); // No room for the required NUL terminator.
            image.WriteBinding(0x500, edgeName, 0x600);
            CHECK(DiscoverBindings(std::span<const std::byte>(image.bytes)).empty());
        }
    }

    void TestDiscoverRejectsTruncatedPeHeaders()
    {
        std::vector<std::byte> tooSmallDos(sizeof(IMAGE_DOS_HEADER) - 1);
        CHECK(DiscoverBindings(std::span<const std::byte>(tooSmallDos)).empty());

        // e_lfanew points at a readable but incomplete NT header. The supplied
        // span is the true boundary; the backing vector only avoids allocator noise.
        std::vector<std::byte> truncatedNt(0x400);
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(truncatedNt.data());
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = static_cast<LONG>(SyntheticPe::NtOffset);
        const auto ntSpanSize = SyntheticPe::NtOffset + sizeof(IMAGE_NT_HEADERS64) - 1;
        CHECK(DiscoverBindings(std::span<const std::byte>(truncatedNt.data(), ntSpanSize)).empty());

        SyntheticPe image;
        const auto oneSectionOnly = image.SectionOffset() + sizeof(IMAGE_SECTION_HEADER);
        CHECK(DiscoverBindings(std::span<const std::byte>(image.bytes.data(), oneSectionOnly)).empty());
    }

    void TestStaticCatalog()
    {
        using namespace gloss::gameplay::extensions::catalog_validation;

        constexpr std::array<int, 47> removedSceneIds{
            -1, 0, 1, 4, 5, 6, 7, 90, 91, 99, 100, 101, 102, 103, 104, 105, 106, 107,
            108, 109, 110, 111, 112, 113, 1000, 3615, 3616, 3617, 3618, 3619, 3620,
            3621, 3622, 3623, 3624, 3625, 3626, 3627, 3630, 3631, 3632, 3633, 3634,
            3635, 3636, 4444, 4445};
        constexpr std::array<int, 15> retainedChapterAndHiddenSceneIds{
            9, 13, 14, 21, 22, 23, 26, 27, 32, 33, 51, 52, 60, 63, 97};
        constexpr std::array<std::pair<int, std::string_view>, 6> removedLandmarks{
            std::pair{10, std::string_view{"测试拾取"}},
            std::pair{10, std::string_view{"测试菜单"}},
            std::pair{30, std::string_view{"测试拾取种子"}},
            std::pair{62, std::string_view{"触发通关（测试）"}},
            std::pair{62, std::string_view{"123"}},
            std::pair{52, std::string_view{"此处输入位置描述52"}}};

        std::set<int> itemIds;
        std::size_t itemCount{};
        for (const auto &item : itemEntries)
        {
            ++itemCount;
            CHECK(item.id > 0);
            CHECK(itemIds.insert(item.id).second);
            CHECK(item.name && *item.name);
            CHECK(item.category && *item.category);
            CHECK(item.list >= 0 && item.list <= 16);
        }
        CHECK(itemCount == 1160);
        CHECK(itemIds.size() == 1160);

        std::set<int> sceneIds;
        std::size_t sceneCount{};
        for (const auto &scene : sceneEntries)
        {
            ++sceneCount;
            CHECK(sceneIds.insert(scene.id).second);
            CHECK(scene.name && *scene.name);
        }
        CHECK(sceneCount == 32);
        CHECK(sceneIds.size() == 32);
        for (const auto id : removedSceneIds)
            CHECK(!sceneIds.contains(id));
        for (const auto id : retainedChapterAndHiddenSceneIds)
            CHECK(sceneIds.contains(id));

        std::size_t landmarkCount{};
        for (const auto &landmark : landmarkEntries)
        {
            ++landmarkCount;
            CHECK(sceneIds.contains(landmark.scene));
            CHECK(landmark.name && *landmark.name);
            CHECK(std::isfinite(landmark.position.x));
            CHECK(std::isfinite(landmark.position.y));
            CHECK(std::isfinite(landmark.position.z));
        }
        CHECK(landmarkCount == 284);
        for (const auto &[scene, name] : removedLandmarks)
        {
            CHECK(std::none_of(std::begin(landmarkEntries), std::end(landmarkEntries), [&](const auto &landmark)
                               { return landmark.scene == scene && name == landmark.name; }));
        }
    }
}

namespace gloss
{
    void Log(std::string_view) {}
    const std::filesystem::path &DataDirectory()
    {
        static const std::filesystem::path directory = L".";
        return directory;
    }
}

int main()
{
    TestDiscoverCompleteAndAdjacentBindings();
    TestDiscoverRejectsDuplicateNames();
    TestDiscoverRejectsNonExecutableAndOutOfBoundsTargets();
    TestDiscoverRejectsTruncatedPeHeaders();
    TestStaticCatalog();

    if (failures)
    {
        std::cerr << failures << " extension test assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer extension tests passed\n";
    return 0;
}
