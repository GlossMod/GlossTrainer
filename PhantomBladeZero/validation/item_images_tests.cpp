#include "../src/gameplay/item_images.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace gloss::gameplay;
    int failures{};

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)

    constexpr std::size_t QueueLimit = 48;
    constexpr std::size_t ResultLimit = 16 * 1024 * 1024;

    ItemImageResult Image(std::uint64_t epoch, std::string key, std::size_t bytes = 4)
    {
        ItemImageResult result;
        result.epoch = epoch;
        result.key = std::move(key);
        result.width = 1;
        result.height = 1;
        result.format = ItemImageFormat::Rgba8;
        result.data.assign(bytes, 0x7f);
        return result;
    }

    void TestRequestValidationAndDuplicateCoalescing()
    {
        ResetItemImages(0);
        CHECK(!RequestItemImage("before-epoch"));

        constexpr std::uint64_t epoch = 101;
        ResetItemImages(epoch);
        CHECK(ItemImageEpoch() == epoch);
        CHECK(!RequestItemImage(""));
        CHECK(!RequestItemImage(std::string(1025, 'k')));

        CHECK(RequestItemImage("prop:one"));
        CHECK(RequestItemImage("prop:one")); // A duplicate is acknowledged without enqueuing twice.
        auto request = TakeItemImageRequest();
        CHECK(request.has_value());
        if (request)
        {
            CHECK(request->epoch == epoch);
            CHECK(request->key == "prop:one");
        }
        CHECK(!TakeItemImageRequest().has_value());

        // The key remains pending while work is in flight, so repeated UI frames
        // cannot enqueue another image request before the adapter publishes.
        CHECK(RequestItemImage("prop:one"));
        CHECK(!TakeItemImageRequest().has_value());
        auto failure = Image(epoch, "prop:one", 0);
        failure.error = "decode failed";
        PublishItemImage(std::move(failure));
        const auto results = DrainItemImageResults();
        CHECK(results.size() == 1);
        if (results.size() == 1)
        {
            CHECK(results[0].epoch == epoch);
            CHECK(results[0].key == "prop:one");
            CHECK(results[0].data.empty());
            CHECK(results[0].error == "decode failed");
        }
        CHECK(RequestItemImage("prop:one")); // Publishing a failure releases the dedupe key.
        request = TakeItemImageRequest();
        CHECK(request && request->key == "prop:one");
    }

    void TestEpochResetClearsQueuedAndPublishedWorkAndRejectsLateResults()
    {
        constexpr std::uint64_t oldEpoch = 202;
        constexpr std::uint64_t newEpoch = 203;
        ResetItemImages(oldEpoch);
        CHECK(RequestItemImage("old-queued"));
        auto inFlight = RequestItemImage("old-in-flight");
        CHECK(inFlight);
        CHECK(RequestItemImage("old-still-queued"));
        const auto oldRequest = TakeItemImageRequest();
        CHECK(oldRequest && oldRequest->key == "old-queued");
        const auto oldInFlight = TakeItemImageRequest();
        CHECK(oldInFlight && oldInFlight->key == "old-in-flight");

        PublishItemImage(Image(oldEpoch, "old-already-published"));
        ResetItemImages(newEpoch);
        CHECK(ItemImageEpoch() == newEpoch);
        CHECK(!TakeItemImageRequest()); // The remaining queued work is discarded.
        CHECK(DrainItemImageResults().empty()); // Results from the old epoch are discarded too.

        PublishItemImage(Image(oldEpoch, "old-in-flight")); // A late worker result must be rejected.
        CHECK(DrainItemImageResults().empty());
        CHECK(RequestItemImage("new-request"));
        const auto current = TakeItemImageRequest();
        CHECK(current && current->epoch == newEpoch && current->key == "new-request");

        // Resetting to the same epoch is intentionally a no-op.
        CHECK(RequestItemImage("same-epoch-queued"));
        ResetItemImages(newEpoch);
        const auto retained = TakeItemImageRequest();
        CHECK(retained && retained->key == "same-epoch-queued");
    }

    void TestQueueLimitAndCapacityRecovery()
    {
        constexpr std::uint64_t epoch = 303;
        ResetItemImages(epoch);
        for (std::size_t index = 0; index < QueueLimit; ++index)
            CHECK(RequestItemImage("item-" + std::to_string(index)));
        CHECK(!RequestItemImage("queue-overflow"));

        const auto first = TakeItemImageRequest();
        CHECK(first && first->key == "item-0");
        CHECK(RequestItemImage("queue-slot-reused"));
        std::size_t drained{};
        while (const auto request = TakeItemImageRequest())
        {
            CHECK(request->epoch == epoch);
            ++drained;
        }
        CHECK(drained == QueueLimit);
    }

    void TestDuplicateAtFullQueueDoesNotConsumeCapacity()
    {
        constexpr std::uint64_t epoch = 404;
        ResetItemImages(epoch);
        for (std::size_t index = 0; index < QueueLimit; ++index)
            CHECK(RequestItemImage("full-" + std::to_string(index)));

        // Idempotent duplicate requests should still be acknowledged at capacity;
        // they do not need another queue slot. This currently catches the bridge's
        // queue-full check running before its duplicate-key check.
        CHECK(RequestItemImage("full-0"));
        CHECK(!RequestItemImage("full-new"));
        std::size_t count{};
        while (TakeItemImageRequest())
            ++count;
        CHECK(count == QueueLimit);
    }

    void TestResultCountBudgetAndDrainRecovery()
    {
        constexpr std::uint64_t epoch = 505;
        ResetItemImages(epoch);
        for (std::size_t index = 0; index < QueueLimit; ++index)
        {
            const auto key = "result-" + std::to_string(index);
            CHECK(RequestItemImage(key));
            CHECK(TakeItemImageRequest().has_value());
            auto failure = Image(epoch, key, 0);
            failure.error = "not found";
            PublishItemImage(std::move(failure));
        }
        CHECK(RequestItemImage("after-result-budget"));
        CHECK(!TakeItemImageRequest()); // Result count budget applies until the consumer drains.

        const auto results = DrainItemImageResults();
        CHECK(results.size() == QueueLimit);
        CHECK(!results.empty() && results.front().error == "not found");
        const auto recovered = TakeItemImageRequest();
        CHECK(recovered && recovered->key == "after-result-budget");
    }

    void TestResultByteBudgetAndOversizeFailure()
    {
        constexpr std::uint64_t epoch = 606;
        ResetItemImages(epoch);
        CHECK(RequestItemImage("exact-budget"));
        CHECK(TakeItemImageRequest().has_value());
        PublishItemImage(Image(epoch, "exact-budget", ResultLimit));
        CHECK(RequestItemImage("blocked-by-bytes"));
        CHECK(!TakeItemImageRequest());
        const auto full = DrainItemImageResults();
        CHECK(full.size() == 1);
        CHECK(full.size() == 1 && full[0].data.size() == ResultLimit);
        const auto resumed = TakeItemImageRequest();
        CHECK(resumed && resumed->key == "blocked-by-bytes");

        ResetItemImages(epoch + 1);
        CHECK(RequestItemImage("oversize"));
        CHECK(TakeItemImageRequest().has_value());
        PublishItemImage(Image(epoch + 1, "oversize", ResultLimit + 1));
        const auto rejected = DrainItemImageResults();
        CHECK(rejected.size() == 1);
        if (rejected.size() == 1)
        {
            CHECK(rejected[0].data.empty());
            CHECK(rejected[0].error == "图片响应队列已满");
        }
    }
}

int main()
{
    TestRequestValidationAndDuplicateCoalescing();
    TestEpochResetClearsQueuedAndPublishedWorkAndRejectsLateResults();
    TestQueueLimitAndCapacityRecovery();
    TestDuplicateAtFullQueueDoesNotConsumeCapacity();
    TestResultCountBudgetAndDrainRecovery();
    TestResultByteBudgetAndOversizeFailure();
    if (failures)
        return 1;
    std::cout << "GlossTrainer PBZ item image bridge tests completed with " << failures << " failures\n";
    return 0;
}
