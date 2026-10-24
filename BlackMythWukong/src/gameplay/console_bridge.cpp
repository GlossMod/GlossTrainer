#include "console_bridge.hpp"
#include "aot.hpp"
#include "engine_bridge.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <MinHook.h>
#include <atomic>
#include <format>

namespace gloss::gameplay::console
{
    namespace
    {
        std::atomic_bool requestRegistration{}, travelReady{}, shrinesReady{};
        using FlagFn = int (*)(int);
        using InputFn = int (*)();
        FlagFn originalFlag{};
        InputFn originalInput{};
        thread_local bool executing{};
        int Flag(int flag) { return flag == 4 && (requestRegistration.load(std::memory_order_relaxed) || executing) ? 1 : originalFlag(flag); }
        int Input() { return requestRegistration.load(std::memory_order_relaxed) || executing ? 1 : originalInput(); }
        bool Hook(std::uintptr_t target, void *replacement, void **original, const char *signature)
        {
            const auto pattern = Pattern::Parse(signature);
            if (!target || FindPattern(pattern, target, pattern.bytes.size()).size() != 1)
                return false;
            const auto created = MH_CreateHook(reinterpret_cast<void *>(target), replacement, original);
            if (created != MH_OK)
                return false;
            if (MH_EnableHook(reinterpret_cast<void *>(target)) != MH_OK)
            {
                MH_RemoveHook(reinterpret_cast<void *>(target));
                return false;
            }
            return true;
        }
    }
    void Initialize()
    {
        if (!aot::Ready())
            return;
        const bool flag = Hook(aot::Method(0x1399f), reinterpret_cast<void *>(&Flag), reinterpret_cast<void **>(&originalFlag), "55 48 8B EC 48 83 EC 30 48 89 4D F8 49 8B 05");
        const bool input = Hook(aot::Method(0x139a0), reinterpret_cast<void *>(&Input), reinterpret_cast<void **>(&originalInput), "55 48 8B EC 48 83 EC 30 48 89 75 F0 48 89 7D F8 49 8B 05");
        requestRegistration = flag && input;
        Log(std::format("Console registration bridges flag={} input={}", flag, input));
    }
    void Tick()
    {
        if (!engine::IsGameThread())
            return;
        const bool travel = engine::ConsoleExists(L"b.teleportToLevelPosition"), shrines = engine::ConsoleExists(L"b.ActiveAllRebirthPoint");
        const auto changedTravel = travelReady.exchange(travel) != travel, changedShrines = shrinesReady.exchange(shrines) != shrines;
        if (changedTravel || changedShrines)
            Log(std::format("Console commands registered: travel={} shrines={}", travel, shrines));
        if (travel && shrines)
            requestRegistration = false;
    }
    bool TravelReady() { return travelReady.load(); }
    bool ShrinesReady() { return shrinesReady.load(); }
    features::Result Execute(std::wstring_view command, std::uintptr_t world)
    {
        const auto name = command.substr(0, command.find(L' '));
        if (name != L"b.teleportToLevelPosition" && name != L"b.ActiveAllRebirthPoint")
            return {false, "指令不在功能白名单"};
        executing = true;
        const auto result = engine::ExecuteConsole(command, world);
        executing = false;
        return {result.success, result.message};
    }
}
