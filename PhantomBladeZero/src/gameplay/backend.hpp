#pragma once

#include "catalog.hpp"
#include <chrono>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

namespace gloss::gameplay {
    using Clock = std::chrono::steady_clock;
    enum class RequestState { Pending, Running, Dispatched, Verified, Failed, Expired, Cancelled, Indeterminate };

    struct Capability {
        std::string featureId;
        bool ready{};
        std::string reason;
    };
    struct Statistic {
        std::string id;
        std::string label;
        std::string value;
    };
    struct ToggleState {
        std::string id;
        bool known{};
        bool enabled{};
        std::string reason;
    };
    struct TogglePreference {
        std::string id;
        bool enabled{}; // Saved user intent, separate from observed game state.
        bool restorePending{};
    };
    struct ItemEntry {
        std::string key, name, description, category;
        int maxQuantity{999};
        std::int64_t owned{-1};
        bool available{};
        std::string reason;
        std::string iconKey;
    };
    struct Request {
        std::uint64_t requestId{};
        std::uint64_t generation{};
        std::string featureId;
        // Validated fixed arguments followed by validated user parameters.
        std::vector<std::string> arguments;
        Clock::time_point expiresAt;
        bool restoration{};
        std::uint64_t preferenceRevision{};
    };
    struct RequestRecord {
        Request request;
        RequestState state{RequestState::Pending};
        std::string message;
    };
    struct ExecutionResult {
        // Dispatched only means the call returned. Verified requires read-back evidence.
        RequestState state{RequestState::Indeterminate};
        std::string message;
    };
    struct SubmitResult {
        bool accepted{};
        std::uint64_t requestId{};
        std::string message;
    };
    // Publications own immutable storage; frame snapshots only share ownership.
    class ItemList {
    public:
        ItemList() : entries_(std::make_shared<const std::vector<ItemEntry>>()) {}
        ItemList(std::vector<ItemEntry> entries)
            : entries_(std::make_shared<const std::vector<ItemEntry>>(std::move(entries))) {}
        auto begin() const { return entries_->begin(); }
        auto end() const { return entries_->end(); }
        bool empty() const { return entries_->empty(); }
        std::size_t size() const { return entries_->size(); }
        const ItemEntry& operator[](std::size_t index) const { return (*entries_)[index]; }
        const ItemEntry* data() const { return entries_->data(); }
    private:
        std::shared_ptr<const std::vector<ItemEntry>> entries_;
    };
    struct Snapshot {
        bool ready{};
        std::uint64_t generation{};
        std::string status;
        std::vector<Capability> capabilities;
        std::vector<Statistic> statistics;
        std::vector<RequestRecord> requests; // Oldest first; bounded history.
        std::size_t pendingCount{};
        std::vector<ToggleState> toggleStates;
        ItemList items;
        bool itemsReady{};
        std::string itemCatalogStatus;
        std::vector<TogglePreference> togglePreferences;
    };

    class Backend {
        std::atomic<bool> itemPageActive_{};
    public:
        explicit Backend(std::size_t queueCapacity = 32, std::size_t historyCapacity = 96);
        // Initialize before starting the native adapter. Missing files use defaults.
        // File errors are contained and never prevent runtime initialization.
        bool InitializeSettings(const std::filesystem::path& file, std::string* error = nullptr);
        // Call periodically outside the game thread. Failed saves stay dirty.
        bool FlushSettings(std::string* error = nullptr);
        Snapshot GetSnapshot() const;
        bool IsReady() const;
        void SetItemPageActive(bool active) { itemPageActive_.store(active, std::memory_order_relaxed); }
        bool ItemPageActive() const { return itemPageActive_.load(std::memory_order_relaxed); }
        // Session key identifies the player/world context. Any key/readiness change
        // invalidates queued work; dispatch must occur only on the game thread.
        void SetRuntimeState(bool ready, std::string status, std::string sessionKey = {});
        void SetCapability(std::string_view featureId, bool ready, std::string reason = {});
        void PublishStatistics(std::vector<Statistic> statistics);
        void PublishToggleStates(std::vector<ToggleState> states);
        void PublishItems(std::vector<ItemEntry> items, bool ready, std::string status);
        SubmitResult SubmitItem(std::string_view key, int quantity);
        SubmitResult Submit(std::string_view featureId, std::vector<std::string> values = {},
            std::chrono::milliseconds lifetime = std::chrono::seconds(5), Clock::time_point now = Clock::now());
        // Callback runs without the model mutex. Reentrant/concurrent Pump calls
        // return zero. Executed requests never retry within the same session.
        // Saved toggles restore after a fresh session and valid state read-back.
        std::size_t Pump(const std::function<ExecutionResult(const Request&)>& execute,
            std::size_t budget = 8, Clock::time_point now = Clock::now());
    private:
        RequestRecord* FindRecord(std::uint64_t requestId);
        void ExpirePending(Clock::time_point now);
        void TrimHistory();
        void QueueRestoration(Clock::time_point now);
        void FinishToggleRequest(const Request& request, RequestState state, bool executed);

        struct ToggleIntent {
            bool enabled{};
            bool restorePending{};
            bool restoreEnabled{true};
            std::uint64_t revision{};
        };

        mutable std::mutex mutex_;
        bool ready_{};
        bool pumping_{};
        std::uint64_t generation_{1};
        std::uint64_t nextRequestId_{1};
        std::string status_{"等待游戏运行时适配器"};
        std::string sessionKey_;
        std::vector<Capability> capabilities_;
        std::vector<Statistic> statistics_;
        std::vector<ToggleState> toggleStates_;
        ItemList items_;
        bool itemsReady_{};
        std::string itemCatalogStatus_{"进入角色场景后加载物品列表"};
        std::deque<Request> queue_;
        std::deque<RequestRecord> records_;
        std::size_t queueCapacity_;
        std::size_t historyCapacity_;
        // File lock precedes model lock; disk I/O never holds the model lock.
        std::mutex settingsFileMutex_;
        bool settingsInitialized_{};
        std::filesystem::path settingsFile_;
        std::uint64_t settingsRevision_{}, persistedSettingsRevision_{};
        std::vector<ToggleIntent> toggleIntents_;
    };

    Backend& GetBackend();
    const char* RequestStateLabel(RequestState state);
}
