#include "../native/update.hpp"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

using namespace gloss::dndg;

namespace {

void Check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        std::exit(1);
    }
}

const char* StateName(UpdateState state) {
    switch (state) {
    case UpdateState::Unchecked: return "Unchecked";
    case UpdateState::Running: return "Running";
    case UpdateState::Unavailable: return "Unavailable";
    case UpdateState::Current: return "Current";
    case UpdateState::Available: return "Available";
    case UpdateState::NoRelease: return "NoRelease";
    }
    return "Unknown";
}

bool ParseTimeout(std::string_view text, unsigned& timeoutMs) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), timeoutMs);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && timeoutMs > 0 && timeoutMs <= 60000;
}

int RunLiveCheck(unsigned timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(timeoutMs);
    CheckForUpdate();

    auto update = ReadUpdate();
    while ((update.state == UpdateState::Unchecked || update.state == UpdateState::Running) &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        update = ReadUpdate();
    }

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "live state=" << StateName(update.state) << " version="
        << update.version[0] << '.' << update.version[1] << '.' << update.version[2]
        << " elapsed_ms=" << elapsedMs << " timeout_ms=" << timeoutMs << '\n';

    if (update.state == UpdateState::Unchecked || update.state == UpdateState::Running) {
        std::cerr << "TIMEOUT: update check did not reach a terminal state\n";
        return 2;
    }
    if (update.state == UpdateState::Unavailable) {
        std::cerr << "UNAVAILABLE: live check could not verify the release channel\n";
        return 3;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool live = false;
    bool timeoutProvided = false;
    unsigned timeoutMs = 25000;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--live") {
            live = true;
        } else if (argument == "--timeout-ms" && i + 1 < argc) {
            timeoutProvided = true;
            if (!ParseTimeout(argv[++i], timeoutMs)) {
                std::cerr << "--timeout-ms must be an integer from 1 to 60000\n";
                return 64;
            }
        } else {
            std::cerr << "Usage: GlossTrainerDnDGUpdateTests [--live] [--timeout-ms 1..60000]\n";
            return 64;
        }
    }
    if (timeoutProvided && !live) {
        std::cerr << "--timeout-ms requires --live\n";
        return 64;
    }

    const auto version = LatestDnDGRelease(R"json([
        {"tag_name":"DnDG/v1.2.3","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v1.2","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v01.2.3","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v1.2.65536","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v1.2.3-extra","draft":false,"prerelease":false}
    ])json");
    Check(version == std::optional<SemVersion>{SemVersion{1, 2, 3}}, "accepts exact DnDG/vX.Y.Z stable tags only");

    const auto filtered = LatestDnDGRelease(R"json([
        {"tag_name":"OtherGame/v99.0.0","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v98.0.0","draft":true,"prerelease":false},
        {"tag_name":"DnDG/v97.0.0","draft":false,"prerelease":true},
        {"tag_name":"DnDG/v1.4.0","draft":false,"prerelease":false}
    ])json");
    Check(filtered == std::optional<SemVersion>{SemVersion{1, 4, 0}}, "excludes other games, drafts, and prereleases");

    Check(!LatestDnDGRelease("[]"), "empty release list has no stable release");
    Check(!LatestDnDGRelease(R"json([
        {"tag_name":"OtherGame/v2.0.0","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v3.0.0","draft":true,"prerelease":false},
        {"tag_name":"DnDG/v4.0.0","draft":false,"prerelease":true}
    ])json"), "all-filtered release list has no stable release");
    Check(!LatestDnDGRelease("{not valid json"), "malformed JSON has no release result");

    const auto sortedMajor = LatestDnDGRelease(R"json([
        {"tag_name":"DnDG/v9.99.99","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v10.0.0","draft":false,"prerelease":false}
    ])json");
    Check(sortedMajor == std::optional<SemVersion>{SemVersion{10, 0, 0}}, "sorts major versions numerically");

    const auto sortedMinor = LatestDnDGRelease(R"json([
        {"tag_name":"DnDG/v2.9.99","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v2.100.0","draft":false,"prerelease":false}
    ])json");
    Check(sortedMinor == std::optional<SemVersion>{SemVersion{2, 100, 0}}, "sorts minor versions numerically");

    const auto sortedPatch = LatestDnDGRelease(R"json([
        {"tag_name":"DnDG/v2.4.9","draft":false,"prerelease":false},
        {"tag_name":"DnDG/v2.4.10","draft":false,"prerelease":false}
    ])json");
    Check(sortedPatch == std::optional<SemVersion>{SemVersion{2, 4, 10}}, "sorts patch versions numerically");

    std::cout << "DnDG native update tests passed (offline)\n";
    return live ? RunLiveCheck(timeoutMs) : 0;
}
