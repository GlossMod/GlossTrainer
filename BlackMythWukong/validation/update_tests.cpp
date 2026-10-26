#include "../src/runtime.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

// Compile the production implementation once so state transitions can be
// exercised without making a network request or opening the user's browser.
#include "../src/update.cpp"

namespace gloss
{
    void Log(std::string_view) {}
}

namespace
{
    int failures{};
#define CHECK(condition)                                                      \
    do                                                                        \
    {                                                                         \
        if (!(condition))                                                     \
        {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; \
            ++failures;                                                       \
        }                                                                     \
    } while (false)
    namespace update = gloss::update;
    using update::Status;
    using Json = nlohmann::json;

    Json ReleaseObject(std::string tag, std::string url = {})
    {
        if (url.empty())
            url = "https://github.com/GlossMod/GlossTrainer/releases/tag/" + tag;
        const auto slash = tag.find('/');
        const auto version = slash == std::string::npos ? tag : tag.substr(slash + 1);
        const auto game = slash == std::string::npos ? "BlackMythWukong" : tag.substr(0, slash);
        const auto name = "GlossTrainer-" + game + "-" + version + "-win64.zip";
        return Json{{"tag_name", tag}, {"html_url", url}, {"draft", false}, {"prerelease", false},
                    {"assets", Json::array({Json{{"name", name}, {"size", 1024u},
                        {"browser_download_url", "https://github.com/GlossMod/GlossTrainer/releases/download/" + tag + "/" + name}}})}};
    }

    std::string Release(std::string tag, std::string url = {})
    {
        return ReleaseObject(std::move(tag), std::move(url)).dump();
    }

    update::detail::ReleaseResult Page(const Json &releases, std::string_view current = "0.3.4")
    {
        update::detail::ReleaseScan scan;
        return update::detail::EvaluateReleasePage(scan, releases.dump(), current);
    }

    void TestVersionComparison()
    {
        for (const auto *tag : {"BlackMythWukong/v0.3.5", "BlackMythWukong/v0.4.0", "BlackMythWukong/v1.0.0", "BlackMythWukong/v0.10.0", "BlackMythWukong/v4294967295.0.0"})
        {
            const auto result = update::detail::EvaluateRelease(Release(tag), "0.3.4");
            CHECK(result.snapshot.status == Status::UpdateRequired);
            CHECK(result.snapshot.latestVersion == tag);
            CHECK(result.diagnostic.empty());
        }
        for (const auto *tag : {"BlackMythWukong/v0.3.4", "BlackMythWukong/v0.3.3", "BlackMythWukong/v0.2.99"})
        {
            const auto result = update::detail::EvaluateRelease(Release(tag), "0.3.4");
            CHECK(result.snapshot.status == Status::Current);
            CHECK(result.diagnostic.empty());
        }
        for (const auto *tag : {"0.3.5", "V0.3.5", "BlackMythWukong/v0.03.5", "BlackMythWukong/v00.3.5", "BlackMythWukong/v0.3.05", "BlackMythWukong/v0.3", "BlackMythWukong/v0.3.5.0",
                                "BlackMythWukong/v0.3.5-beta", "BlackMythWukong/v0.3.5+build", "BlackMythWukong/v-1.3.5", "BlackMythWukong/v+1.3.5", "BlackMythWukong/v0.3.5 ", "BlackMythWukong/v0.3.4294967296", "BlackMythWukong/v.3.5", "BlackMythWukong/v0..5"})
        {
            const auto result = update::detail::EvaluateRelease(Release(tag), "0.3.4");
            CHECK(result.snapshot.status == Status::Error);
            CHECK(result.snapshot.message == "检查更新失败");
            CHECK(!result.diagnostic.empty());
            CHECK(result.snapshot.releaseUrl.empty());
        }
        CHECK(update::detail::EvaluateRelease(Release("BlackMythWukong/v0.3.5"), "BlackMythWukong/v0.3.4").snapshot.status == Status::Error);
    }

    void TestStableReleaseValidation()
    {
        const auto valid = nlohmann::json::parse(Release("BlackMythWukong/v0.3.5"));
        for (const auto *field : {"draft", "prerelease"})
        {
            for (const auto &invalid : {nlohmann::json(true), nlohmann::json(0), nlohmann::json("false"), nlohmann::json(nullptr)})
            {
                auto release = valid;
                release[field] = invalid;
                CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
            }
            auto release = valid;
            release.erase(field);
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
        }
        for (const auto *field : {"tag_name", "html_url"})
        {
            auto release = valid;
            release[field] = 12;
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
            release.erase(field);
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
        }
        for (const auto *invalid : {"", "{", "null", "[]", "{}", "\"BlackMythWukong/v0.3.5\""})
        {
            const auto result = update::detail::EvaluateRelease(invalid, "0.3.4");
            CHECK(result.snapshot.status == Status::Error);
            CHECK(result.snapshot.message == "检查更新失败");
            CHECK(!result.diagnostic.empty());
        }
        CHECK(update::detail::EvaluateRelease(std::string(512 * 1024 + 1, ' '), "0.3.4").snapshot.status == Status::Error);
        std::string nested(40, '[');
        nested += '0';
        nested += std::string(40, ']');
        const auto excessiveDepth = update::detail::EvaluateRelease(nested, "0.3.4");
        CHECK(excessiveDepth.snapshot.status == Status::Error);
        CHECK(excessiveDepth.diagnostic.find("depth limit") != std::string::npos);
    }

    void TestTrustedReleaseUrls()
    {
        const std::string valid = "https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5";
        CHECK(update::detail::IsTrustedReleaseUrl(valid, "BlackMythWukong/v0.3.5"));
        for (const auto *url : {
                 "http://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5",
                 "https://github.com.evil.invalid/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5",
                 "https://github.com@evil.invalid/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5",
                 "https://github.com/Other/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5",
                 "https://github.com/GlossMod/Other/releases/tag/BlackMythWukong/v0.3.5",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.6",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5?download=1",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5#fragment",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5/../v0.3.6",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/%760.3.5",
                 "file:///C:/trainer.exe", "javascript:alert(1)"})
        {
            CHECK(!update::detail::IsTrustedReleaseUrl(url, "BlackMythWukong/v0.3.5"));
            CHECK(update::detail::EvaluateRelease(Release("BlackMythWukong/v0.3.5", url), "0.3.4").snapshot.status == Status::Error);
        }
        CHECK(!update::detail::IsTrustedReleaseUrl(valid, "BlackMythWukong/v0.3.5/anything"));
    }

    void TestPackageProvenance()
    {
        const auto valid = ReleaseObject("BlackMythWukong/v0.3.5");
        for (const auto &assets : {Json(nullptr), Json::object(), Json::array(), Json::array({ReleaseObject("OtherGame/v99.0.0")["assets"][0]})})
        {
            auto release = valid;
            release["assets"] = assets;
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
        }
        for (const auto *field : {"name", "size", "browser_download_url"})
        {
            auto release = valid;
            release["assets"][0].erase(field);
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
        }
        for (const auto &size : {Json(0), Json(-1), Json("1024"), Json(nullptr), Json(1.5)})
        {
            auto release = valid;
            release["assets"][0]["size"] = size;
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
        }
        for (const auto *url : {
                 "https://evil.invalid/GlossTrainer-BlackMythWukong-v0.3.5-win64.zip",
                 "https://github.com/GlossMod/GlossTrainer/releases/download/OtherGame/v0.3.5/GlossTrainer-BlackMythWukong-v0.3.5-win64.zip",
                 "https://github.com/GlossMod/GlossTrainer/releases/download/BlackMythWukong/v0.3.6/GlossTrainer-BlackMythWukong-v0.3.5-win64.zip"})
        {
            auto release = valid;
            release["assets"][0]["browser_download_url"] = url;
            CHECK(update::detail::EvaluateRelease(release.dump(), "0.3.4").snapshot.status == Status::Error);
        }
        auto duplicate = valid;
        duplicate["assets"].push_back(duplicate["assets"][0]);
        CHECK(update::detail::EvaluateRelease(duplicate.dump(), "0.3.4").snapshot.status == Status::Error);
        for (const auto *separator : {"%2F", "%2f"})
        {
            auto encoded = valid;
            encoded["assets"][0]["browser_download_url"] = std::string("https://github.com/GlossMod/GlossTrainer/releases/download/BlackMythWukong") +
                separator + "v0.3.5/GlossTrainer-BlackMythWukong-v0.3.5-win64.zip";
            const auto pageUrl = std::string("https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong") + separator + "v0.3.5";
            encoded["html_url"] = pageUrl;
            CHECK(update::detail::IsTrustedReleaseUrl(pageUrl, "BlackMythWukong/v0.3.5"));
            CHECK(update::detail::EvaluateRelease(encoded.dump(), "0.3.4").snapshot.status == Status::UpdateRequired);
        }
        CHECK(!update::detail::IsTrustedReleaseUrl("https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong%252Fv0.3.5", "BlackMythWukong/v0.3.5"));
        CHECK(!update::detail::IsTrustedReleaseUrl("https://github.com/GlossMod/GlossTrainer/releases/tag/OtherGame/v0.3.5", "BlackMythWukong/v0.3.5"));
    }

    void TestGameFilteringAndOrdering()
    {
        auto draft = ReleaseObject("BlackMythWukong/v999.0.0");
        draft["draft"] = true;
        auto prerelease = ReleaseObject("BlackMythWukong/v998.0.0");
        prerelease["prerelease"] = true;
        const auto mixed = Json::array({ReleaseObject("OtherGame/v999.0.0"), ReleaseObject("BlackMythWukong/v0.4.0"),
            draft, ReleaseObject("BlackMythWukong/v0.10.0"), ReleaseObject("v1000.0.0"), prerelease, ReleaseObject("BlackMythWukong/v0.3.5")});
        auto result = Page(mixed);
        CHECK(result.snapshot.status == Status::UpdateRequired);
        CHECK(result.snapshot.latestVersion == "BlackMythWukong/v0.10.0");
        CHECK(result.diagnostic.empty());
        result = Page(mixed, "0.10.0");
        CHECK(result.snapshot.status == Status::Current);
        CHECK(Page(Json::array()).snapshot.status == Status::NoRelease);
        CHECK(Page(Json::array({ReleaseObject("OtherGame/v999.0.0"), ReleaseObject("v99.0.0"), draft, prerelease})).snapshot.status == Status::NoRelease);
        for (const auto &invalid : {Json(), Json::object(), Json::array({1}), Json::array({Json::object()})})
            CHECK(Page(invalid).snapshot.status == Status::Error);
        auto invalid = ReleaseObject("BlackMythWukong/v0.3.5");
        invalid["prerelease"] = "false";
        CHECK(Page(Json::array({invalid})).snapshot.status == Status::Error);
        for (const auto *tag : {"v99.0.0", "OtherGame/v99.0.0", "blackmythwukong/v0.3.5", "BlackMythWukong2/v0.3.5"})
            CHECK(update::detail::EvaluateRelease(Release(tag), "0.3.4").snapshot.status == Status::Error);
    }

    void TestBoundedCompletePagination()
    {
        Json full = Json::array();
        for (std::size_t index = 0; index < update::ReleasesPerPage; ++index)
            full.push_back(ReleaseObject("OtherGame/v" + std::to_string(index) + ".0.0"));
        full[2] = ReleaseObject("BlackMythWukong/v0.3.5");
        update::detail::ReleaseScan scan;
        auto result = update::detail::EvaluateReleasePage(scan, full.dump(), "0.3.4");
        CHECK(result.snapshot.status == Status::Checking);
        CHECK(result.snapshot.releaseUrl.empty());
        CHECK(!scan.complete && !scan.failed);
        CHECK(scan.pages == 1);
        result = update::detail::EvaluateReleasePage(scan, Json::array({ReleaseObject("BlackMythWukong/v0.9.0")}).dump(), "0.3.4");
        CHECK(result.snapshot.status == Status::UpdateRequired);
        CHECK(result.snapshot.latestVersion == "BlackMythWukong/v0.9.0");
        CHECK(scan.complete && !scan.failed);
        CHECK(update::detail::EvaluateReleasePage(scan, "[]", "0.3.4").snapshot.status == Status::Error);
        scan = {};
        for (std::size_t page = 0; page < update::MaxReleasePages; ++page)
            result = update::detail::EvaluateReleasePage(scan, full.dump(), "0.3.4");
        CHECK(result.snapshot.status == Status::Error);
        CHECK(scan.failed && !scan.complete);
        CHECK(result.snapshot.releaseUrl.empty());
        CHECK(result.diagnostic.find("page limit") != std::string::npos);
        scan = {};
        update::detail::EvaluateReleasePage(scan, full.dump(), "0.3.4");
        CHECK(update::detail::EvaluateReleasePage(scan, "[]", "0.3.5").snapshot.status == Status::Error);
        scan = {};
        CHECK(update::detail::EvaluateReleasePage(scan, std::string(update::MaxResponseBytes + 1, ' '), "0.3.4").snapshot.status == Status::Error);
        scan = {};
        auto oversizedPage = full;
        oversizedPage[0]["body"] = std::string(450 * 1024, 'x');
        for (int index = 0; index < 5; ++index)
            result = update::detail::EvaluateReleasePage(scan, oversizedPage.dump(), "0.3.4");
        CHECK(result.snapshot.status == Status::Error);
        CHECK(result.diagnostic.find("byte limit") != std::string::npos);
        scan = {};
        full.push_back(ReleaseObject("OtherGame/v999.0.0"));
        CHECK(update::detail::EvaluateReleasePage(scan, full.dump(), "0.3.4").snapshot.status == Status::Error);
        scan = {};
        std::string nested(40, '[');
        nested += '0';
        nested += std::string(40, ']');
        CHECK(update::detail::EvaluateReleasePage(scan, nested, "0.3.4").diagnostic.find("depth limit") != std::string::npos);
    }

    void TestFailedRetryAllowsUse()
    {
        update::Publish({Status::UpdateRequired, "BlackMythWukong/v0.3.5", "发现新版本", "https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5"});
        CHECK(update::Required());
        CHECK(update::GetSnapshot().status == Status::UpdateRequired);
        update::Publish(update::detail::EvaluateRelease("malformed response", "0.3.4").snapshot);
        CHECK(!update::Required());
        CHECK(update::GetSnapshot().status == Status::Error);
        CHECK(update::GetSnapshot().message == "检查更新失败");
        CHECK(update::GetSnapshot().releaseUrl.empty());
        update::Publish({Status::NoRelease, {}, "暂无正式发布版本", {}});
        CHECK(!update::Required());
        CHECK(!update::OpenReleasePage());
        update::Publish({Status::Checking, {}, "正在检查更新…", {}});
        CHECK(!update::Required());
        update::Publish(update::detail::EvaluateRelease(Release("BlackMythWukong/v0.3.4"), "0.3.4").snapshot);
        CHECK(!update::Required());
        CHECK(update::GetSnapshot().status == Status::Current);
    }
}

int main()
{
    TestVersionComparison();
    TestStableReleaseValidation();
    TestTrustedReleaseUrls();
    TestPackageProvenance();
    TestGameFilteringAndOrdering();
    TestBoundedCompletePagination();
    TestFailedRetryAllowsUse();
    if (failures)
    {
        std::cerr << failures << " update assertions failed\n";
        return 1;
    }
    std::cout << "PASS: per-game version ordering, package provenance, trusted URLs, bounded complete pagination, failed retry recovery\n";
    return 0;
}
