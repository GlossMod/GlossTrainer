#include "item_images.hpp"
#include <deque>
#include <mutex>
#include <unordered_set>

namespace gloss::gameplay {
    namespace {
        std::mutex imageMutex;
        std::uint64_t imageEpoch{};
        std::deque<ItemImageRequest> imageRequests;
        std::unordered_set<std::string> pendingImageKeys;
        std::vector<ItemImageResult> imageResults;
        std::size_t resultBytes{};
        constexpr std::size_t QueueLimit = 48, ResultLimit = 16 * 1024 * 1024;
    }
    void ResetItemImages(std::uint64_t epoch) {
        std::lock_guard lock(imageMutex);
        if (imageEpoch == epoch) return;
        imageEpoch = epoch;
        imageRequests.clear(); pendingImageKeys.clear(); imageResults.clear(); resultBytes = 0;
    }
    std::uint64_t ItemImageEpoch() { std::lock_guard lock(imageMutex); return imageEpoch; }
    bool RequestItemImage(std::string_view key) {
        if (key.empty() || key.size() > 1024) return false;
        std::lock_guard lock(imageMutex);
        if (!imageEpoch) return false;
        if (pendingImageKeys.contains(std::string(key))) return true;
        if (imageRequests.size() >= QueueLimit) return false;
        pendingImageKeys.emplace(key);
        imageRequests.push_back({imageEpoch, std::string(key)});
        return true;
    }
    std::optional<ItemImageRequest> TakeItemImageRequest() {
        std::lock_guard lock(imageMutex);
        if (imageRequests.empty() || resultBytes >= ResultLimit || imageResults.size() >= QueueLimit) return {};
        auto request = std::move(imageRequests.front()); imageRequests.pop_front(); return request;
    }
    void PublishItemImage(ItemImageResult result) {
        std::lock_guard lock(imageMutex);
        if (result.epoch != imageEpoch) return;
        pendingImageKeys.erase(result.key);
        if (result.data.size() > ResultLimit - resultBytes) {
            result.data.clear(); result.error = "图片响应队列已满";
        }
        resultBytes += result.data.size(); imageResults.push_back(std::move(result));
    }
    std::vector<ItemImageResult> DrainItemImageResults() {
        std::lock_guard lock(imageMutex);
        resultBytes = 0;
        auto result = std::move(imageResults); imageResults.clear(); return result;
    }
}
