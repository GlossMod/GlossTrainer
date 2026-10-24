#pragma once
#include "../features.hpp"
#include <cstdint>
#include <string_view>
namespace gloss::gameplay::console
{
    void Initialize();
    void Tick();
    bool TravelReady();
    bool ShrinesReady();
    features::Result Execute(std::wstring_view command, std::uintptr_t world);
}
