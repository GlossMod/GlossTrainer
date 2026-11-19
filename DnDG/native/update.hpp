#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace gloss::dndg {

using SemVersion = std::array<unsigned, 3>;
enum class UpdateState { Unchecked, Running, Unavailable, Current, Available, NoRelease };
struct UpdateResult {
    UpdateState state{};
    SemVersion version{};
};

std::optional<SemVersion> LatestDnDGRelease(std::string_view json);
UpdateResult ReadUpdate() noexcept;
void CheckForUpdate() noexcept;

} // namespace gloss::dndg
