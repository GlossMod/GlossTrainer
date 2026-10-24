#include "../src/gameplay/aot.hpp"
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string_view>

namespace {
int failures{};

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
        ++failures; \
    } \
} while (false)

class SyntheticImage {
public:
    static constexpr std::size_t ImageSize = 0x220000;
    static constexpr std::size_t NtRva = 0x80;
    static constexpr std::size_t TextRva = 0x1000;
    static constexpr std::size_t TextSize = 0x110000;
    static constexpr std::size_t DataRva = 0x120000;
    static constexpr std::size_t DataSize = ImageSize - DataRva;
    static constexpr std::size_t MainTableRva = 0x121000;
    static constexpr std::size_t ScriptTableRva = 0x1A4000;
    static constexpr std::size_t MainHeaderRva = 0x1C0000;
    static constexpr std::size_t ScriptHeaderRva = 0x1C1000;
    static constexpr std::size_t DuplicateMainHeaderRva = 0x1C1800;
    static constexpr std::size_t MainNameRva = 0x1C2000;
    static constexpr std::size_t MainGuidRva = 0x1C2040;
    static constexpr std::size_t ScriptNameRva = 0x1C2080;
    static constexpr std::size_t ScriptGuidRva = 0x1C20D0;
    static constexpr std::size_t BadGuidRva = 0x1C2120;
    static constexpr std::size_t RuntimeHeaderRva = 0x1C2300;
    static constexpr std::size_t ProtoHeaderRva = 0x1C2800;
    static constexpr std::size_t RuntimeNameRva = 0x1C3000;
    static constexpr std::size_t RuntimeGuidRva = 0x1C3040;
    static constexpr std::size_t ProtoNameRva = 0x1C3080;
    static constexpr std::size_t ProtoGuidRva = 0x1C30D0;
    static constexpr std::size_t RuntimeTableRva = 0x20000;
    static constexpr std::size_t ProtoTableRva = 0x40000;
    static constexpr std::size_t GetterRva = 0x4000;
    static constexpr std::size_t CostItemRva = 0x5000;
    static constexpr std::size_t AlternateTargetRva = 0x6000;
    static constexpr std::size_t RuntimeTargetRva = 0x7000;
    static constexpr std::size_t ProtoTargetRva = 0x8000;
    static constexpr std::uint32_t MainCount = 106230;
    static constexpr std::uint32_t ScriptCount = 22103;
    static constexpr std::uint32_t GetterRid = 0x8ca2;
    static constexpr std::uint32_t CostItemRid = 0x374c;
    static constexpr std::uint32_t RuntimeCount = 15630;
    static constexpr std::uint32_t ProtoCount = 36051;

    SyntheticImage() {
        base_ = static_cast<std::byte*>(VirtualAlloc(nullptr, ImageSize,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!base_) return;
        std::memset(base_, 0, ImageSize);
        WriteHeaders();
        WriteAssembly(MainHeaderRva, MainNameRva, MainGuidRva, MainTableRva,
            MainCount, TextRva, TextRva + TextSize);
        WriteAssembly(ScriptHeaderRva, ScriptNameRva, ScriptGuidRva, ScriptTableRva,
            ScriptCount, TextRva, TextRva + TextSize);
        WriteAssembly(RuntimeHeaderRva, RuntimeNameRva, RuntimeGuidRva, RuntimeTableRva,
            RuntimeCount, TextRva, TextRva + TextSize);
        WriteAssembly(ProtoHeaderRva, ProtoNameRva, ProtoGuidRva, ProtoTableRva,
            ProtoCount, TextRva, TextRva + TextSize);
        WriteString(MainNameRva, "BtlSvr.Main");
        WriteString(MainGuidRva, "65A46ED7-6F85-40F5-AD32-079A507252B6");
        WriteString(ScriptNameRva, "B1UI_GSE.Script");
        WriteString(ScriptGuidRva, "64AC5451-3E83-486D-B18A-128163A8971C");
        WriteString(BadGuidRva, "00000000-0000-0000-0000-000000000000");
        WriteString(RuntimeNameRva, "UnrealEngine.Runtime");
        WriteString(RuntimeGuidRva, "70421AB0-86DD-4BD4-AFD6-6C909E8B51A7");
        WriteString(ProtoNameRva, "Protobuf.RunTime");
        WriteString(ProtoGuidRva, "3CED0FE6-3455-4ADB-9501-636B0DB4FC87");
        WriteBytes(GetterRva, {
            0x55, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x30, 0x48,
            0x89, 0x4d, 0xf8, 0x48, 0x89, 0x55, 0xf0, 0xe8
        });
        WriteBytes(CostItemRva, {0x55, 0x48, 0x8b, 0xec});
        WriteBytes(AlternateTargetRva, {
            0x55, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x30, 0x48,
            0x89, 0x4d, 0xf8, 0x48, 0x89, 0x55, 0xf0, 0xe8
        });
        WriteBytes(RuntimeTargetRva, {0x90, 0xc3});
        WriteBytes(ProtoTargetRva, {0x90, 0xc3});
        WriteCall(MainTableRva, GetterRid, GetterRva);
        WriteCall(ScriptTableRva, CostItemRid, CostItemRva);
        WriteCall(RuntimeTableRva, 1, RuntimeTargetRva);
        WriteCall(ProtoTableRva, 1, ProtoTargetRva);
    }

    ~SyntheticImage() { if (base_) VirtualFree(base_, 0, MEM_RELEASE); }
    SyntheticImage(const SyntheticImage&) = delete;
    SyntheticImage& operator=(const SyntheticImage&) = delete;

    explicit operator bool() const { return base_ != nullptr; }
    HMODULE Module() const { return reinterpret_cast<HMODULE>(base_); }
    std::uintptr_t Address(std::size_t rva) const {
        return reinterpret_cast<std::uintptr_t>(base_) + rva;
    }

    bool Protect() {
        if (!base_) return false;
        DWORD previous{};
        if (!VirtualProtect(base_ + TextRva, TextSize, PAGE_EXECUTE_READWRITE, &previous)) return false;
        if (!VirtualProtect(base_ + DataRva, DataSize, PAGE_READONLY, &previous)) return false;
        return true;
    }

    bool MakeMainTargetNonExecutable() {
        DWORD previous{};
        return VirtualProtect(base_ + AlternateTargetRva, 0x1000, PAGE_READONLY, &previous) != FALSE;
    }

    void MainTarget(std::size_t targetRva) { WriteCall(MainTableRva, GetterRid, targetRva); }
    void MainCodeEnd(std::size_t endRva) { WriteAt(MainHeaderRva + 0x38, Address(endRva)); }
    void MainGuid(std::size_t guidRva) { WriteAt(MainHeaderRva + 0xc0, Address(guidRva)); }
    void ScriptGuid(std::size_t guidRva) { WriteAt(ScriptHeaderRva + 0xc0, Address(guidRva)); }
    void RuntimeGuid(std::size_t guidRva) { WriteAt(RuntimeHeaderRva + 0xc0, Address(guidRva)); }
    void ProtoGuid(std::size_t guidRva) { WriteAt(ProtoHeaderRva + 0xc0, Address(guidRva)); }
    void MainGetterOpcode(std::uint8_t opcode) {
        base_[MainTableRva + (GetterRid - 1) * 5] = static_cast<std::byte>(opcode);
    }
    void ScriptCostOpcode(std::uint8_t opcode) {
        base_[ScriptTableRva + (CostItemRid - 1) * 5] = static_cast<std::byte>(opcode);
    }
    void DuplicateMainHeader() {
        std::memcpy(base_ + DuplicateMainHeaderRva, base_ + MainHeaderRva, 0x190);
    }

private:
    std::byte* base_{};

    template<class T> void WriteAt(std::size_t rva, T value) {
        std::memcpy(base_ + rva, &value, sizeof(value));
    }

    void WriteString(std::size_t rva, std::string_view value) {
        std::memcpy(base_ + rva, value.data(), value.size());
        base_[rva + value.size()] = std::byte{};
    }

    void WriteBytes(std::size_t rva, std::initializer_list<std::uint8_t> bytes) {
        for (const auto byte : bytes) base_[rva++] = static_cast<std::byte>(byte);
    }

    void WriteCall(std::size_t tableRva, std::uint32_t rid, std::size_t targetRva) {
        const auto entryRva = tableRva + static_cast<std::size_t>(rid - 1) * 5;
        base_[entryRva] = std::byte{0xe8};
        const auto displacement = static_cast<std::int32_t>(
            static_cast<std::int64_t>(Address(targetRva)) -
            static_cast<std::int64_t>(Address(entryRva + 5)));
        std::memcpy(base_ + entryRva + 1, &displacement, sizeof(displacement));
    }

    void WriteAssembly(std::size_t headerRva, std::size_t nameRva, std::size_t guidRva,
        std::size_t tableRva, std::uint32_t count, std::size_t codeStartRva,
        std::size_t codeEndRva) {
        WriteAt(headerRva + 0x00, std::uint32_t{180});
        WriteAt(headerRva + 0x04, std::uint32_t{0});
        WriteAt(headerRva + 0x30, Address(codeStartRva));
        WriteAt(headerRva + 0x38, Address(codeEndRva));
        WriteAt(headerRva + 0x40, Address(tableRva));
        WriteAt(headerRva + 0xc0, Address(guidRva));
        WriteAt(headerRva + 0x108, Address(nameRva));
        WriteAt(headerRva + 0x154, count);
        WriteAt(headerRva + 0x188, std::uint32_t{5});
    }

    void WriteHeaders() {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base_);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = static_cast<LONG>(NtRva);

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base_ + NtRva);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.NumberOfSections = 2;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(ImageSize);

        auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(base_ + NtRva +
            offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + sizeof(IMAGE_OPTIONAL_HEADER64));
        std::memcpy(sections[0].Name, ".text", 5);
        sections[0].Misc.VirtualSize = static_cast<DWORD>(TextSize);
        sections[0].VirtualAddress = static_cast<DWORD>(TextRva);
        sections[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
        std::memcpy(sections[1].Name, ".rdata", 6);
        sections[1].Misc.VirtualSize = static_cast<DWORD>(DataSize);
        sections[1].VirtualAddress = static_cast<DWORD>(DataRva);
        sections[1].Characteristics = IMAGE_SCN_MEM_READ;
    }
};

void TestBothAssembliesAndRidBounds() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image || !image.Protect()) { CHECK(false); return; }
    CHECK(gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(gloss::gameplay::aot::Ready());
    CHECK(gloss::gameplay::aot::Method(SyntheticImage::GetterRid) == image.Address(SyntheticImage::GetterRva));
    CHECK(gloss::gameplay::aot::ScriptMethod(SyntheticImage::CostItemRid) == image.Address(SyntheticImage::CostItemRva));
    CHECK(gloss::gameplay::aot::Method(0) == 0);
    CHECK(gloss::gameplay::aot::ScriptMethod(0) == 0);
    CHECK(gloss::gameplay::aot::Method(SyntheticImage::MainCount + 1) == 0);
    CHECK(gloss::gameplay::aot::ScriptMethod(SyntheticImage::ScriptCount + 1) == 0);
}

void TestOptionalRuntimeAndProtoAssemblies() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image || !image.Protect()) { CHECK(false); return; }
    CHECK(gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(gloss::gameplay::aot::Ready());
    CHECK(gloss::gameplay::aot::RuntimeMethod(1) == image.Address(SyntheticImage::RuntimeTargetRva));
    CHECK(gloss::gameplay::aot::ProtoMethod(1) == image.Address(SyntheticImage::ProtoTargetRva));
    CHECK(gloss::gameplay::aot::RuntimeMethod(SyntheticImage::RuntimeCount + 1) == 0);
    CHECK(gloss::gameplay::aot::ProtoMethod(SyntheticImage::ProtoCount + 1) == 0);
}

void TestOptionalRuntimeIdentityFailureIsNonFatal() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.RuntimeGuid(SyntheticImage::BadGuidRva);
    CHECK(image.Protect());
    CHECK(gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(gloss::gameplay::aot::Ready());
    CHECK(gloss::gameplay::aot::RuntimeMethod(1) == 0);
    CHECK(gloss::gameplay::aot::ProtoMethod(1) == image.Address(SyntheticImage::ProtoTargetRva));
}

void TestOptionalProtoIdentityFailureIsNonFatal() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.ProtoGuid(SyntheticImage::BadGuidRva);
    CHECK(image.Protect());
    CHECK(gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(gloss::gameplay::aot::Ready());
    CHECK(gloss::gameplay::aot::RuntimeMethod(1) == image.Address(SyntheticImage::RuntimeTargetRva));
    CHECK(gloss::gameplay::aot::ProtoMethod(1) == 0);
}

void TestRejectsWrongAssemblyMvids() {
    {
        SyntheticImage image;
        CHECK(static_cast<bool>(image));
        if (!image) { CHECK(false); return; }
        image.MainGuid(SyntheticImage::BadGuidRva);
        CHECK(image.Protect());
        CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
        CHECK(!gloss::gameplay::aot::Ready());
    }
    {
        SyntheticImage image;
        CHECK(static_cast<bool>(image));
        if (!image) { CHECK(false); return; }
        image.ScriptGuid(SyntheticImage::BadGuidRva);
        CHECK(image.Protect());
        CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
        CHECK(!gloss::gameplay::aot::Ready());
    }
}

void TestRejectsNonCallScriptEntry() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.ScriptCostOpcode(0x90);
    CHECK(image.Protect());
    CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(!gloss::gameplay::aot::Ready());
}

void TestRejectsNonCallMethodEntry() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.MainGetterOpcode(0x90);
    CHECK(image.Protect());
    CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(!gloss::gameplay::aot::Ready());
}

void TestRejectsNonExecutableResolvedTarget() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.MainTarget(SyntheticImage::AlternateTargetRva);
    CHECK(image.Protect());
    CHECK(image.MakeMainTargetNonExecutable());
    CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(!gloss::gameplay::aot::Ready());
}

void TestRejectsTargetAtCodeEnd() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.MainCodeEnd(SyntheticImage::GetterRva);
    CHECK(image.Protect());
    CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(!gloss::gameplay::aot::Ready());
}

void TestRejectsDuplicateAssemblyHeader() {
    SyntheticImage image;
    CHECK(static_cast<bool>(image));
    if (!image) { CHECK(false); return; }
    image.DuplicateMainHeader();
    CHECK(image.Protect());
    CHECK(!gloss::gameplay::aot::Initialize(image.Module()));
    CHECK(!gloss::gameplay::aot::Ready());
}
}

namespace gloss {
void Log(std::string_view) {}
}

int main() {
    TestBothAssembliesAndRidBounds();
    TestOptionalRuntimeAndProtoAssemblies();
    TestOptionalRuntimeIdentityFailureIsNonFatal();
    TestOptionalProtoIdentityFailureIsNonFatal();
    TestRejectsWrongAssemblyMvids();
    TestRejectsNonCallMethodEntry();
    TestRejectsNonCallScriptEntry();
    TestRejectsNonExecutableResolvedTarget();
    TestRejectsTargetAtCodeEnd();
    TestRejectsDuplicateAssemblyHeader();

    if (failures) {
        std::cerr << failures << " AOT test assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer AOT tests passed\n";
    return 0;
}
