#include "update.hpp"
#include "runtime.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gloss::update {
namespace {
constexpr std::size_t MaxResponseBytes = 512 * 1024;
constexpr ULONGLONG MaxRequestMilliseconds = 20000;
constexpr wchar_t ApiHost[] = L"api.github.com";
constexpr wchar_t ApiPath[] = L"/repos/GlossMod/GlossTrainer/releases/latest";
constexpr std::string_view ReleasePrefix = "https://github.com/GlossMod/GlossTrainer/releases/tag/";
using VersionParts = std::array<std::uint32_t, 3>;

std::optional<VersionParts> ParseVersion(std::string_view text, bool requireV) {
    if (requireV) {
        if (text.empty() || text.front() != 'v') return std::nullopt;
        text.remove_prefix(1);
    }
    VersionParts parts{};
    for (std::size_t index = 0; index != parts.size(); ++index) {
        const auto separator = text.find('.');
        if ((index != 2 && separator == std::string_view::npos) ||
            (index == 2 && separator != std::string_view::npos)) return std::nullopt;
        const auto component = index == 2 ? text : text.substr(0, separator);
        if (component.empty() || (component.size() > 1 && component.front() == '0')) return std::nullopt;
        for (const auto digit : component) if (digit < '0' || digit > '9') return std::nullopt;
        const auto [end, error] = std::from_chars(component.data(), component.data() + component.size(), parts[index]);
        if (error != std::errc{} || end != component.data() + component.size()) return std::nullopt;
        if (index != 2) text.remove_prefix(separator + 1);
    }
    return parts;
}

Snapshot Failure() {
    return {Status::Error, {}, "检查更新失败", {}};
}

struct State {
    std::mutex mutex;
    Snapshot snapshot;
    bool checking{};
};

State& SharedState() {
    // The ASI has no runtime unload. Avoid destruction racing a bounded network
    // request during process teardown; no thread is joined from DllMain.
    static auto* state = new State;
    return *state;
}

void Publish(Snapshot result) {
    auto& state = SharedState();
    std::lock_guard lock(state.mutex);
    state.snapshot = std::move(result);
    state.checking = false;
}

class HttpHandle {
public:
    explicit HttpHandle(HINTERNET value) : value(value) {
        if (!value) throw std::runtime_error(std::format("WinHTTP open failed ({})", GetLastError()));
    }
    ~HttpHandle() { WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    operator HINTERNET() const { return value; }
private:
    HINTERNET value;
};

void Require(BOOL succeeded, const char* operation) {
    if (!succeeded) throw std::runtime_error(std::format("{} failed ({})", operation, GetLastError()));
}

detail::ReleaseResult FetchRelease() {
    const auto started = GetTickCount64();
    HttpHandle session(WinHttpOpen(L"GlossTrainer-UpdateChecker/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    Require(WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000), "WinHttpSetTimeouts");
    HttpHandle connection(WinHttpConnect(session, ApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    HttpHandle request(WinHttpOpenRequest(connection, L"GET", ApiPath, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
    Require(WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)), "Disable redirects/authentication");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    Require(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)), "Set redirect policy");
    constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    Require(WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0), "WinHttpSendRequest");
    Require(WinHttpReceiveResponse(request, nullptr), "WinHttpReceiveResponse");
    DWORD status{}, size = sizeof(status);
    Require(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX), "Read HTTP status");
    if (status == 404) return {{Status::NoRelease, {}, "暂无正式发布版本", {}}, {}};
    if (status != 200) throw std::runtime_error(std::format("GitHub latest release returned HTTP {}", status));

    DWORD contentLength{};
    size = sizeof(contentLength);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &size, WINHTTP_NO_HEADER_INDEX) && contentLength > MaxResponseBytes) {
        throw std::runtime_error("GitHub response exceeded the 512 KiB limit");
    }
    std::string body;
    std::array<char, 8192> buffer{};
    for (;;) {
        if (GetTickCount64() - started >= MaxRequestMilliseconds) throw std::runtime_error("GitHub request exceeded its total time limit");
        DWORD read{};
        Require(WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read), "WinHttpReadData");
        if (!read) break;
        if (body.size() + read > MaxResponseBytes) throw std::runtime_error("GitHub response exceeded the 512 KiB limit");
        body.append(buffer.data(), read);
    }
    return detail::EvaluateRelease(body, Version);
}

void RunCheck() noexcept {
    try {
        auto result = FetchRelease();
        if (!result.diagnostic.empty()) Log("Update check: " + result.diagnostic);
        else Log("Update check: " + result.snapshot.message);
        Publish(std::move(result.snapshot));
    } catch (const std::exception& error) {
        Log(std::string("Update check failed: ") + error.what());
        Publish(Failure());
    } catch (...) {
        Log("Update check failed: unknown error");
        Publish(Failure());
    }
}

DWORD WINAPI Worker(void* parameter) {
    RunCheck();
    // Destroy worker locals before releasing the module reference. The process
    // does not wait for this thread on exit, and no DLL detach work is needed.
    FreeLibraryAndExitThread(static_cast<HMODULE>(parameter), 0);
}
}

namespace detail {
bool IsTrustedReleaseUrl(std::string_view url, std::string_view tag) {
    return ParseVersion(tag, true).has_value() && url == std::string(ReleasePrefix) + std::string(tag);
}

ReleaseResult EvaluateRelease(std::string_view payload, std::string_view currentVersion) {
    auto reject = [](std::string reason) -> ReleaseResult { return {Failure(), std::move(reason)}; };
    const auto current = ParseVersion(currentVersion, false);
    if (!current) return reject("Invalid compiled current version");
    if (payload.size() > MaxResponseBytes) return reject("GitHub response exceeded the 512 KiB limit");
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(payload, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if (depth > 32) throw std::runtime_error("GitHub JSON nesting exceeded the depth limit");
            return true;
        }, false);
    } catch (const std::exception& error) {
        return reject(error.what());
    }
    if (json.is_discarded() || !json.is_object()) return reject("GitHub response is not a JSON release object");
    for (const auto* name : {"draft", "prerelease"}) {
        if (!json.contains(name) || !json[name].is_boolean()) return reject("Missing/invalid release stability field");
        if (json[name].get<bool>()) return reject("GitHub latest endpoint returned a draft/prerelease");
    }
    if (!json.contains("tag_name") || !json["tag_name"].is_string() ||
        !json.contains("html_url") || !json["html_url"].is_string()) return reject("Missing release tag/page");
    const auto tag = json["tag_name"].get<std::string>();
    const auto latest = ParseVersion(tag, true);
    if (!latest) return reject("Latest release tag is not strict vX.Y.Z");
    const auto url = json["html_url"].get<std::string>();
    if (!IsTrustedReleaseUrl(url, tag)) return reject("Release page does not match the fixed GitHub repository/tag");
    if (*latest > *current) return {{Status::UpdateRequired, tag, "发现新版本，请下载更新后使用", url}, {}};
    return {{Status::Current, tag, "当前已是最新版本", url}, {}};
}
}

void CheckAsync() {
    auto& state = SharedState();
    {
        std::lock_guard lock(state.mutex);
        if (state.checking) return;
        state.checking = true;
        // A retry alone does not dismiss an already confirmed update. Its
        // completed failure does: users can continue when GitHub is unavailable.
        if (state.snapshot.status != Status::UpdateRequired) {
            state.snapshot = {Status::Checking, {}, "正在检查更新…", {}};
        }
    }
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&Worker), &module)) {
        Log(std::format("Update check: could not retain module ({})", GetLastError()));
        Publish(Failure());
        return;
    }
    const auto thread = CreateThread(nullptr, 0, Worker, module, 0, nullptr);
    if (!thread) {
        const auto error = GetLastError();
        FreeLibrary(module);
        Log(std::format("Update check: could not create worker ({})", error));
        Publish(Failure());
        return;
    }
    CloseHandle(thread);
}

Snapshot GetSnapshot() {
    auto& state = SharedState();
    std::lock_guard lock(state.mutex);
    return state.snapshot;
}

bool Required() {
    auto& state = SharedState();
    std::lock_guard lock(state.mutex);
    return state.snapshot.status == Status::UpdateRequired;
}

bool OpenReleasePage() {
    const auto snapshot = GetSnapshot();
    if (!detail::IsTrustedReleaseUrl(snapshot.releaseUrl, snapshot.latestVersion)) return false;
    const std::wstring url(snapshot.releaseUrl.begin(), snapshot.releaseUrl.end());
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) return true;
    Log(std::format("Update check: could not open GitHub release page ({})", result));
    return false;
}
}
