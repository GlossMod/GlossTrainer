#pragma once
#include <Windows.h>
#include <cstdint>
namespace gloss::gameplay::aot
{
    // Mono AOT v180, BtlSvr.Main identity 65A46ED7-6F85-40F5-AD32-079A507252B6.
    // Tokens are looked up through the relocated call table, never treated as RVAs.
    bool Initialize(HMODULE module);
    std::uintptr_t Method(std::uint32_t methodDefRid);
    std::uintptr_t ScriptMethod(std::uint32_t methodDefRid);
    std::uintptr_t RuntimeMethod(std::uint32_t methodDefRid);
    std::uintptr_t ProtoMethod(std::uint32_t methodDefRid);
    bool Ready();
}
