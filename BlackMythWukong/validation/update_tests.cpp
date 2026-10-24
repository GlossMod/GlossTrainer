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

    std::string Release(std::string tag, std::string url = {})
    {
        if (url.empty())
            url = "https://github.com/GlossMod/GlossTrainer/releases/tag/" + tag;
        return nlohmann::json{{"tag_name", tag}, {"html_url", url}, {"draft", false}, {"prerelease", false}}.dump();
    }

    void TestVersionComparison()
    {
        for (const auto *tag : {"v0.3.5", "v0.4.0", "v1.0.0", "v0.10.0", "v4294967295.0.0"})
        {
            const auto result = update::detail::EvaluateRelease(Release(tag), "0.3.4");
            CHECK(result.snapshot.status == Status::UpdateRequired);
            CHECK(result.snapshot.latestVersion == tag);
            CHECK(result.diagnostic.empty());
        }
        for (const auto *tag : {"v0.3.4", "v0.3.3", "v0.2.99"})
        {
            const auto result = update::detail::EvaluateRelease(Release(tag), "0.3.4");
            CHECK(result.snapshot.status == Status::Current);
            CHECK(result.diagnostic.empty());
        }
        for (const auto *tag : {"0.3.5", "V0.3.5", "v0.03.5", "v00.3.5", "v0.3.05", "v0.3", "v0.3.5.0",
                                "v0.3.5-beta", "v0.3.5+build", "v-1.3.5", "v+1.3.5", "v0.3.5 ", "v0.3.4294967296", "v.3.5", "v0..5"})
        {
            const auto result = update::detail::EvaluateRelease(Release(tag), "0.3.4");
            CHECK(result.snapshot.status == Status::Error);
            CHECK(result.snapshot.message == "检查更新失败");
            CHECK(!result.diagnostic.empty());
            CHECK(result.snapshot.releaseUrl.empty());
        }
        CHECK(update::detail::EvaluateRelease(Release("v0.3.5"), "v0.3.4").snapshot.status == Status::Error);
    }

    void TestStableReleaseValidation()
    {
        const auto valid = nlohmann::json::parse(Release("v0.3.5"));
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
        for (const auto *invalid : {"", "{", "null", "[]", "{}", "\"v0.3.5\""})
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
        const std::string valid = "https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.5";
        CHECK(update::detail::IsTrustedReleaseUrl(valid, "v0.3.5"));
        for (const auto *url : {
                 "http://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.5",
                 "https://github.com.evil.invalid/GlossMod/GlossTrainer/releases/tag/v0.3.5",
                 "https://github.com@evil.invalid/GlossMod/GlossTrainer/releases/tag/v0.3.5",
                 "https://github.com/Other/GlossTrainer/releases/tag/v0.3.5",
                 "https://github.com/GlossMod/Other/releases/tag/v0.3.5",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.6",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.5?download=1",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.5#fragment",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.5/../v0.3.6",
                 "https://github.com/GlossMod/GlossTrainer/releases/tag/%760.3.5",
                 "file:///C:/trainer.exe", "javascript:alert(1)"})
        {
            CHECK(!update::detail::IsTrustedReleaseUrl(url, "v0.3.5"));
            CHECK(update::detail::EvaluateRelease(Release("v0.3.5", url), "0.3.4").snapshot.status == Status::Error);
        }
        CHECK(!update::detail::IsTrustedReleaseUrl(valid, "v0.3.5/anything"));
    }

    void TestFailedRetryAllowsUse()
    {
        update::Publish({Status::UpdateRequired, "v0.3.5", "发现新版本", "https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.5"});
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
        update::Publish(update::detail::EvaluateRelease(Release("v0.3.4"), "0.3.4").snapshot);
        CHECK(!update::Required());
        CHECK(update::GetSnapshot().status == Status::Current);
    }
}

int main()
{
    TestVersionComparison();
    TestStableReleaseValidation();
    TestTrustedReleaseUrls();
    TestFailedRetryAllowsUse();
    if (failures)
    {
        std::cerr << failures << " update assertions failed\n";
        return 1;
    }
    std::cout << "PASS: version ordering, stable release validation, trusted URLs, bounded JSON, failed retry recovery\n";
    return 0;
}
