#include "update.hpp"
#include "runtime.hpp"

#include <nlohmann/json.hpp>
#include <winhttp.h>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace gloss::dndg {
namespace {

using Json = nlohmann::json;
constexpr std::string_view Prefix = "DnDG/v";
constexpr DWORD TimeoutMs = 4000;
constexpr std::size_t MaxResponse = 2u * 1024 * 1024;
std::atomic<std::uint64_t> result{};

struct HttpHandle {
    explicit HttpHandle(HINTERNET value) : handle(value) {}
    ~HttpHandle() { if (handle) WinHttpCloseHandle(handle); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    HINTERNET handle{};
};

std::optional<SemVersion> ParseVersion(std::string_view input) {
    SemVersion parts{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (input.empty())
            return std::nullopt;
        const auto dot = input.find('.');
        if ((i == parts.size() - 1) != (dot == std::string_view::npos))
            return std::nullopt;
        const auto token = input.substr(0, dot);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || parts[i] > 65535 ||
            (token.size() > 1 && token.front() == '0'))
            return std::nullopt;
        if (dot != std::string_view::npos)
            input.remove_prefix(dot + 1);
    }
    return parts;
}

std::uint64_t Encode(UpdateResult value) {
    return static_cast<std::uint64_t>(value.state) | (static_cast<std::uint64_t>(value.version[0]) << 8) |
        (static_cast<std::uint64_t>(value.version[1]) << 24) | (static_cast<std::uint64_t>(value.version[2]) << 40);
}

std::optional<SemVersion> Candidate(const Json& release) {
    if (!release.is_object()) return std::nullopt;
    const auto tag = release.find("tag_name");
    const auto draft = release.find("draft");
    const auto prerelease = release.find("prerelease");
    if (tag == release.end() || !tag->is_string() || draft == release.end() || !draft->is_boolean() || draft->get<bool>() ||
        prerelease == release.end() || !prerelease->is_boolean() || prerelease->get<bool>())
        return std::nullopt;
    const std::string_view value = tag->get_ref<const std::string&>();
    return value.starts_with(Prefix) ? ParseVersion(value.substr(Prefix.size())) : std::nullopt;
}

UpdateResult FetchUpdate() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(18);
    const HttpHandle session(WinHttpOpen(L"GlossTrainer-DnDG", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.handle || !WinHttpSetTimeouts(session.handle, TimeoutMs, TimeoutMs, TimeoutMs, TimeoutMs))
        return {UpdateState::Unavailable};
    const HttpHandle connection(WinHttpConnect(session.handle, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.handle)
        return {UpdateState::Unavailable};
    std::optional<SemVersion> best;
    for (int page = 1; page <= 4 && std::chrono::steady_clock::now() < deadline; ++page) {
        const std::wstring path = L"/repos/GlossMod/GlossTrainer/releases?per_page=100&page=" + std::to_wstring(page);
        const HttpHandle request(WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.handle)
            return {UpdateState::Unavailable};
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
            !WinHttpSendRequest(request.handle, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.handle, nullptr))
            return {UpdateState::Unavailable};
        DWORD status{}, statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX) || status != 200)
            return {UpdateState::Unavailable};
        std::string body;
        char block[8192]{};
        while (true) {
            DWORD copied{};
            if (std::chrono::steady_clock::now() >= deadline ||
                !WinHttpReadData(request.handle, block, sizeof(block), &copied) || body.size() + copied > MaxResponse)
                return {UpdateState::Unavailable};
            if (!copied) break;
            body.append(block, copied);
        }
        const auto json = Json::parse(body, nullptr, false);
        if (!json.is_array())
            return {UpdateState::Unavailable};
        for (const auto& release : json) {
            const auto candidate = Candidate(release);
            if (candidate && (!best || *candidate > *best)) best = candidate;
        }
        if (json.size() < 100) {
            if (!best) return {UpdateState::NoRelease};
            const std::string_view build = Version;
            const auto current = ParseVersion(build.substr(0, build.find('-')));
            if (!current) return {UpdateState::Unavailable};
            return {*best > *current ? UpdateState::Available : UpdateState::Current, *best};
        }
    }
    return {UpdateState::Unavailable};
}

} // namespace

std::optional<SemVersion> LatestDnDGRelease(std::string_view json) {
    const auto releases = Json::parse(json, nullptr, false);
    std::optional<SemVersion> latest;
    if (releases.is_array()) {
        for (const auto& row : releases) {
            const auto version = Candidate(row);
            if (version && (!latest || *version > *latest)) latest = version;
        }
    }
    return latest;
}

UpdateResult ReadUpdate() noexcept {
    const auto word = result.load();
    return {static_cast<UpdateState>(word & 0xff), {static_cast<unsigned>((word >> 8) & 0xffff),
        static_cast<unsigned>((word >> 24) & 0xffff), static_cast<unsigned>((word >> 40) & 0xffff)}};
}

void CheckForUpdate() noexcept {
    auto previous = result.load();
    if ((previous & 0xff) == static_cast<unsigned>(UpdateState::Running) ||
        !result.compare_exchange_strong(previous, Encode({UpdateState::Running})))
        return;
    // No DLL teardown join: the ASI is pinned for the process lifetime.
    try {
        std::thread([] {
            try { result.store(Encode(FetchUpdate())); }
            catch (...) { result.store(Encode({UpdateState::Unavailable})); }
        }).detach();
    } catch (...) {
        result.store(Encode({UpdateState::Unavailable}));
    }
}

} // namespace gloss::dndg
