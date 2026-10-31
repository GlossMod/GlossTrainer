#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace gloss::update
{
    enum class Status
    {
        Idle,
        Checking,
        Current,
        UpdateAvailable,
        Error,
        NoRelease
    };

    struct Snapshot
    {
        Status status = Status::Idle;
        std::string latestVersion;
        std::string message = "尚未检查更新";
        std::string releaseUrl;
    };

    // Advisory only: a newer release never restricts gameplay functionality.
    // At most one request is active. This never waits for network I/O on the caller.
    void CheckAsync() noexcept;
    bool HasUpdate() noexcept;
    Snapshot GetSnapshot();
    bool OpenReleasePage();

    namespace detail
    {
        struct ReleaseResult
        {
            Snapshot snapshot;
            std::string diagnostic;
        };

        // A bounded scan must reach the final page before a version is published.
        struct ReleaseScan
        {
            std::size_t pages{};
            std::size_t responseBytes{};
            std::string currentVersion;
            Snapshot newest;
            bool complete{};
            bool failed{};
        };

        // Only PhantomBladeZero/vX.Y.Z releases with a matching
        // GlossTrainer-PhantomBladeZero-vX.Y.Z-win64.zip package are accepted.
        // Pure response validation, also used by the offline regression target.
        ReleaseResult EvaluateReleasePage(ReleaseScan &scan, std::string_view payload, std::string_view currentVersion);
        ReleaseResult EvaluateRelease(std::string_view payload, std::string_view currentVersion);
        bool IsTrustedReleaseUrl(std::string_view url, std::string_view tag);
    }
}
