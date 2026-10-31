#include "update.hpp"
#include "runtime.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gloss::update
{
    namespace
    {
        constexpr std::size_t MaxResponseBytes = 512 * 1024;
        constexpr std::size_t MaxScanBytes = 2 * 1024 * 1024;
        constexpr std::size_t ReleasesPerPage = 20;
        constexpr std::size_t MaxReleasePages = 10;
        constexpr ULONGLONG MaxRequestMilliseconds = 20000;
        constexpr wchar_t ApiHost[] = L"api.github.com";
        constexpr std::string_view GameTagPrefix = "PhantomBladeZero/";
        constexpr std::string_view ReleasePrefix = "https://github.com/GlossMod/GlossTrainer/releases/tag/";
        constexpr std::string_view DownloadPrefix = "https://github.com/GlossMod/GlossTrainer/releases/download/";
        using VersionParts = std::array<std::uint32_t, 3>;

        std::optional<VersionParts> ParseVersion(std::string_view text, bool requireV)
        {
            if (requireV)
            {
                if (text.empty() || text.front() != 'v')
                    return std::nullopt;
                text.remove_prefix(1);
            }
            VersionParts parts{};
            for (std::size_t index = 0; index != parts.size(); ++index)
            {
                const auto separator = text.find('.');
                if ((index != 2 && separator == std::string_view::npos) ||
                    (index == 2 && separator != std::string_view::npos))
                    return std::nullopt;
                const auto component = index == 2 ? text : text.substr(0, separator);
                if (component.empty() || (component.size() > 1 && component.front() == '0'))
                    return std::nullopt;
                for (const auto digit : component)
                    if (digit < '0' || digit > '9')
                        return std::nullopt;
                const auto [end, error] = std::from_chars(component.data(), component.data() + component.size(), parts[index]);
                if (error != std::errc{} || end != component.data() + component.size())
                    return std::nullopt;
                if (index != 2)
                    text.remove_prefix(separator + 1);
            }
            return parts;
        }

        std::optional<VersionParts> ParseGameTag(std::string_view tag)
        {
            if (!tag.starts_with(GameTagPrefix))
                return std::nullopt;
            return ParseVersion(tag.substr(GameTagPrefix.size()), true);
        }

        bool IsTagUrl(std::string_view url, std::string_view prefix, std::string_view tag, std::string_view suffix = {})
        {
            if (!ParseGameTag(tag))
                return false;
            if (url == std::string(prefix) + std::string(tag) + std::string(suffix))
                return true;
            for (const auto *separator : {"%2F", "%2f"})
            {
                const auto encoded = std::string("PhantomBladeZero") + separator + std::string(tag.substr(GameTagPrefix.size()));
                if (url == std::string(prefix) + encoded + std::string(suffix))
                    return true;
            }
            return false;
        }

        Snapshot Failure()
        {
            return {Status::Error, {}, "检查更新失败", {}};
        }

        detail::ReleaseResult Failed(std::string diagnostic)
        {
            return {Failure(), std::move(diagnostic)};
        }

        detail::ReleaseResult HttpFailure(const char *operation, DWORD error = GetLastError())
        {
            // Offline/proxy/TLS failures are expected results. Do not raise a C++
            // exception inside the game just because GitHub is unreachable.
            return Failed(std::format("{} failed ({})", operation, error));
        }

        void LogDiagnostic(std::string_view prefix, std::string_view message) noexcept
        {
            try
            {
                Log(std::string(prefix) + std::string(message));
            }
            catch (...)
            {
                // Logging must never turn an advisory update into a process failure.
            }
        }

        struct State
        {
            std::mutex mutex;
            Snapshot snapshot;
            bool checking{};
        };

        State &SharedState()
        {
            // The ASI has no runtime unload. Avoid destruction racing a bounded network
            // request during process teardown; no thread is joined from DllMain.
            static auto *state = new State;
            return *state;
        }

        void Publish(Snapshot result)
        {
            auto &state = SharedState();
            std::lock_guard lock(state.mutex);
            state.snapshot = std::move(result);
            state.checking = false;
        }

        void PublishFailure() noexcept
        {
            try
            {
                auto &state = SharedState();
                std::lock_guard lock(state.mutex);
                // Clear the duplicate guard and stale release before any potentially
                // allocating work. Even a second allocation failure must not terminate.
                state.checking = false;
                state.snapshot.status = Status::Error;
                state.snapshot.latestVersion.clear();
                state.snapshot.releaseUrl.clear();
                state.snapshot.message.clear();
                state.snapshot.message = "检查更新失败";
            }
            catch (...)
            {
            }
        }

        bool BeginCheck()
        {
            auto &state = SharedState();
            std::lock_guard lock(state.mutex);
            if (state.checking)
                return false;
            state.snapshot = {Status::Checking, {}, "正在检查更新…", {}};
            state.checking = true;
            return true;
        }

        class HttpHandle
        {
        public:
            explicit HttpHandle(HINTERNET value) noexcept : value(value) {}
            ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
            HttpHandle(const HttpHandle &) = delete;
            HttpHandle &operator=(const HttpHandle &) = delete;
            operator HINTERNET() const { return value; }

        private:
            HINTERNET value;
        };

        detail::ReleaseResult FetchRelease()
        {
            const auto started = GetTickCount64();
            HttpHandle session(WinHttpOpen(L"GlossTrainer-PhantomBladeZero-UpdateChecker/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session)
                return HttpFailure("WinHttpOpen");
            if (!WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000))
                return HttpFailure("WinHttpSetTimeouts");
            HttpHandle connection(WinHttpConnect(session, ApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!connection)
                return HttpFailure("WinHttpConnect");
            detail::ReleaseScan scan;
            for (std::size_t page = 1; page <= MaxReleasePages; ++page)
            {
                const auto path = std::format(L"/repos/GlossMod/GlossTrainer/releases?per_page={}&page={}", ReleasesPerPage, page);
                HttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
                if (!request)
                    return HttpFailure("WinHttpOpenRequest");
                std::string budgetError;
                auto applyBudget = [&]
                {
                    const auto elapsed = GetTickCount64() - started;
                    if (elapsed >= MaxRequestMilliseconds)
                    {
                        budgetError = "GitHub release scan exceeded its 20 second time limit";
                        return false;
                    }
                    const auto timeout = static_cast<int>(std::min<ULONGLONG>(5000, MaxRequestMilliseconds - elapsed));
                    if (!WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout))
                    {
                        budgetError = std::format("Set request budget failed ({})", GetLastError());
                        return false;
                    }
                    return true;
                };
                DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
                if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
                    return HttpFailure("Disable redirects/authentication");
                DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
                if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)))
                    return HttpFailure("Set redirect policy");
                constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
                if (!applyBudget())
                    return Failed(std::move(budgetError));
                if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                    return HttpFailure("WinHttpSendRequest");
                if (!applyBudget())
                    return Failed(std::move(budgetError));
                if (!WinHttpReceiveResponse(request, nullptr))
                    return HttpFailure("WinHttpReceiveResponse");
                if (!applyBudget())
                    return Failed(std::move(budgetError));
                DWORD status{}, size = sizeof(status);
                if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
                    return HttpFailure("Read HTTP status");
                if (status != 200)
                    return Failed(std::format("GitHub release list returned HTTP {}", status));
                DWORD contentLength{};
                size = sizeof(contentLength);
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &size, WINHTTP_NO_HEADER_INDEX) &&
                    (contentLength > MaxResponseBytes || contentLength > MaxScanBytes - scan.responseBytes))
                    return Failed("GitHub release scan exceeded its response byte limit");
                std::string body;
                std::array<char, 8192> buffer{};
                for (;;)
                {
                    if (!applyBudget())
                        return Failed(std::move(budgetError));
                    DWORD read{};
                    if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
                        return HttpFailure("WinHttpReadData");
                    if (!applyBudget())
                        return Failed(std::move(budgetError));
                    if (!read)
                        break;
                    if (body.size() + read > MaxResponseBytes || body.size() + read > MaxScanBytes - scan.responseBytes)
                        return Failed("GitHub release scan exceeded its response byte limit");
                    body.append(buffer.data(), read);
                }
                auto result = detail::EvaluateReleasePage(scan, body, Version);
                if (scan.complete || scan.failed)
                    return result;
            }
            return Failed("GitHub release scan reached its page limit before completion");
        }

        void RunCheck(detail::ReleaseResult (*fetch)() = FetchRelease) noexcept
        {
            try
            {
                auto result = fetch();
                if (!result.diagnostic.empty())
                    LogDiagnostic("Update check failed: ", result.diagnostic);
                else
                    LogDiagnostic("Update check: ", result.snapshot.message);
                Publish(std::move(result.snapshot));
            }
            catch (const std::exception &error)
            {
                PublishFailure();
                LogDiagnostic("Update check failed: ", error.what());
            }
            catch (...)
            {
                PublishFailure();
                LogDiagnostic("Update check failed: ", "unknown error");
            }
        }

        DWORD WINAPI Worker(void *parameter)
        {
            RunCheck();
            // Destroy worker locals before releasing the module reference. The process
            // does not wait for this thread on exit, and no DLL detach work is needed.
            FreeLibraryAndExitThread(static_cast<HMODULE>(parameter), 0);
        }
    }

    namespace detail
    {
        bool IsTrustedReleaseUrl(std::string_view url, std::string_view tag)
        {
            return IsTagUrl(url, ReleasePrefix, tag);
        }

        ReleaseResult EvaluateRelease(std::string_view payload, std::string_view currentVersion)
        {
            auto reject = [](std::string reason) -> ReleaseResult
            { return {Failure(), std::move(reason)}; };
            const auto current = ParseVersion(currentVersion, false);
            if (!current)
                return reject("Invalid compiled current version");
            if (payload.size() > MaxResponseBytes)
                return reject("GitHub response exceeded the 512 KiB limit");
            nlohmann::json json;
            try
            {
                json = nlohmann::json::parse(payload, [](int depth, nlohmann::json::parse_event_t, nlohmann::json &)
                                             {
            if (depth > 32) throw std::runtime_error("GitHub JSON nesting exceeded the depth limit");
            return true; }, false);
            }
            catch (const std::exception &error)
            {
                return reject(error.what());
            }
            if (json.is_discarded() || !json.is_object())
                return reject("GitHub response is not a JSON release object");
            for (const auto *name : {"draft", "prerelease"})
            {
                if (!json.contains(name) || !json[name].is_boolean())
                    return reject("Missing/invalid release stability field");
                if (json[name].get<bool>())
                    return reject("Release is a draft/prerelease");
            }
            if (!json.contains("tag_name") || !json["tag_name"].is_string() ||
                !json.contains("html_url") || !json["html_url"].is_string())
                return reject("Missing release tag/page");
            const auto tag = json["tag_name"].get<std::string>();
            const auto latest = ParseGameTag(tag);
            if (!latest)
                return reject("Release tag is not strict PhantomBladeZero/vX.Y.Z");
            const auto url = json["html_url"].get<std::string>();
            if (!IsTrustedReleaseUrl(url, tag))
                return reject("Release page does not match the fixed GitHub repository/tag");
            // Only a release with the matching game package can advertise an update.
            const auto archiveName = std::string("GlossTrainer-PhantomBladeZero-") + tag.substr(GameTagPrefix.size()) + "-win64.zip";
            if (!json.contains("assets") || !json["assets"].is_array())
                return reject("Release has no package assets");
            bool packageFound = false;
            for (const auto &asset : json["assets"])
            {
                if (!asset.is_object() || !asset.contains("name") || !asset["name"].is_string() ||
                    asset["name"].get<std::string>() != archiveName)
                    continue;
                if (packageFound || !asset.contains("browser_download_url") || !asset["browser_download_url"].is_string() ||
                    !asset.contains("size") || !asset["size"].is_number_unsigned() || asset["size"].get<std::uint64_t>() == 0 ||
                    !IsTagUrl(asset["browser_download_url"].get<std::string>(), DownloadPrefix, tag, "/" + archiveName))
                    return reject("Release package is duplicated or has invalid provenance");
                packageFound = true;
            }
            if (!packageFound)
                return reject("Release does not contain the matching PhantomBladeZero ZIP");
            if (*latest > *current)
                return {{Status::UpdateAvailable, tag, "发现新版本，可前往发布页下载", url}, {}};
            return {{Status::Current, tag, "当前已是最新版本", url}, {}};
        }

        ReleaseResult EvaluateReleasePage(ReleaseScan &scan, std::string_view payload, std::string_view currentVersion)
        {
            auto reject = [&](std::string reason) -> ReleaseResult
            {
                scan.failed = true;
                scan.newest = {};
                return {Failure(), std::move(reason)};
            };
            if (scan.complete || scan.failed)
                return reject("Release scan was already completed or failed");
            if (!ParseVersion(currentVersion, false) ||
                (!scan.currentVersion.empty() && scan.currentVersion != currentVersion))
                return reject("Invalid or changed compiled current version");
            scan.currentVersion = currentVersion;
            if (scan.pages >= MaxReleasePages)
                return reject("Release scan exceeded its page limit");
            if (payload.size() > MaxResponseBytes || payload.size() > MaxScanBytes - scan.responseBytes)
                return reject("Release scan exceeded its response byte limit");
            nlohmann::json releases;
            try
            {
                releases = nlohmann::json::parse(payload, [](int depth, nlohmann::json::parse_event_t, nlohmann::json &)
                                               {
                    if (depth > 32) throw std::runtime_error("GitHub JSON nesting exceeded the depth limit");
                    return true;
                }, false);
            }
            catch (const std::exception &error)
            {
                return reject(error.what());
            }
            if (releases.is_discarded() || !releases.is_array() || releases.size() > ReleasesPerPage)
                return reject("GitHub response is not a bounded release-list page");
            ++scan.pages;
            scan.responseBytes += payload.size();
            for (const auto &release : releases)
            {
                if (!release.is_object() || !release.contains("tag_name") || !release["tag_name"].is_string())
                    return reject("Release-list entry has no valid tag");
                const auto tag = release["tag_name"].get<std::string>();
                // The repository's global latest and legacy naked vX.Y.Z channel are unrelated.
                if (!tag.starts_with(GameTagPrefix))
                    continue;
                for (const auto *name : {"draft", "prerelease"})
                    if (!release.contains(name) || !release[name].is_boolean())
                        return reject("Missing/invalid release stability field");
                if (release["draft"].get<bool>() || release["prerelease"].get<bool>())
                    continue;
                auto candidate = EvaluateRelease(release.dump(), currentVersion);
                if (!candidate.diagnostic.empty())
                    return reject(std::move(candidate.diagnostic));
                if (scan.newest.latestVersion.empty() ||
                    *ParseGameTag(candidate.snapshot.latestVersion) > *ParseGameTag(scan.newest.latestVersion))
                    scan.newest = std::move(candidate.snapshot);
            }
            if (releases.size() == ReleasesPerPage)
            {
                if (scan.pages == MaxReleasePages)
                    return reject("Release scan reached its page limit before completion");
                return {{Status::Checking, {}, "正在检查更新…", {}}, {}};
            }
            scan.complete = true;
            if (scan.newest.latestVersion.empty())
                return {{Status::NoRelease, {}, "暂无正式发布版本", {}}, {}};
            return {scan.newest, {}};
        }
    }

    void CheckAsync() noexcept
    {
        try
        {
            if (!BeginCheck())
                return;
            HMODULE module{};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                    reinterpret_cast<LPCWSTR>(&Worker), &module))
            {
                const auto error = GetLastError();
                PublishFailure();
                LogDiagnostic("Update check: ", HttpFailure("could not retain module", error).diagnostic);
                return;
            }
            const auto thread = CreateThread(nullptr, 0, Worker, module, 0, nullptr);
            if (!thread)
            {
                const auto error = GetLastError();
                FreeLibrary(module);
                PublishFailure();
                LogDiagnostic("Update check: ", HttpFailure("could not create worker", error).diagnostic);
                return;
            }
            CloseHandle(thread);
        }
        catch (const std::exception &error)
        {
            PublishFailure();
            LogDiagnostic("Update check could not start: ", error.what());
        }
        catch (...)
        {
            PublishFailure();
            LogDiagnostic("Update check could not start: ", "unknown error");
        }
    }

    bool HasUpdate() noexcept
    {
        try
        {
            auto &state = SharedState();
            std::lock_guard lock(state.mutex);
            return state.snapshot.status == Status::UpdateAvailable;
        }
        catch (...)
        {
            return false;
        }
    }

    Snapshot GetSnapshot()
    {
        auto &state = SharedState();
        std::lock_guard lock(state.mutex);
        return state.snapshot;
    }

    bool OpenReleasePage()
    {
        const auto snapshot = GetSnapshot();
        if (!detail::IsTrustedReleaseUrl(snapshot.releaseUrl, snapshot.latestVersion))
            return false;
        const std::wstring url(snapshot.releaseUrl.begin(), snapshot.releaseUrl.end());
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result > 32)
            return true;
        Log(std::format("Update check: could not open GitHub release page ({})", result));
        return false;
    }
}
