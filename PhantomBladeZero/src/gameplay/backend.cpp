#include "backend.hpp"
#include "toggle_settings.hpp"
#include <algorithm>
#include <exception>
#include <utility>

namespace gloss::gameplay {
    namespace {
        bool Active(RequestState state) {
            return state == RequestState::Pending || state == RequestState::Running;
        }
        bool ExecutionState(RequestState state) {
            return state == RequestState::Dispatched || state == RequestState::Verified ||
                state == RequestState::Failed || state == RequestState::Indeterminate;
        }
        std::size_t ToggleIndex(std::string_view featureId, bool& enabled) {
            const auto& toggles = ToggleCatalog();
            for (std::size_t i = 0; i < toggles.size(); ++i) {
                if (featureId == toggles[i].onFeatureId) { enabled = true; return i; }
                if (featureId == toggles[i].offFeatureId) { enabled = false; return i; }
            }
            return toggles.size();
        }
    }

    Backend::Backend(std::size_t queueCapacity, std::size_t historyCapacity)
        : queueCapacity_(std::clamp<std::size_t>(queueCapacity, 1, 256)),
          historyCapacity_(std::max(historyCapacity, queueCapacity_ + 1)) {
        for (const auto& feature : Catalog())
            capabilities_.push_back({feature.id, false, feature.unavailableReason});
        toggleIntents_.resize(ToggleCatalog().size());
    }

    bool Backend::InitializeSettings(const std::filesystem::path& file, std::string* error) {
        std::lock_guard fileLock(settingsFileMutex_);
        if (error) error->clear();
        {
            std::lock_guard lock(mutex_);
            if (settingsInitialized_) return true;
        }
        auto data = toggle_settings::Defaults();
        std::string message;
        bool loaded{};
        try {
            if (file.empty()) message = "功能设置文件路径为空";
            else loaded = toggle_settings::Load(file, data, message);
        } catch (...) { message = "功能设置读取异常，使用默认关闭状态"; }
        {
            std::lock_guard lock(mutex_);
            settingsFile_ = file;
            settingsInitialized_ = true;
            for (std::size_t i = 0; i < toggleIntents_.size(); ++i) {
                auto& intent = toggleIntents_[i];
                intent.enabled = data.at(ToggleCatalog()[i].id);
                intent.restorePending = intent.enabled;
            }
        }
        if (error) *error = std::move(message);
        return loaded;
    }

    bool Backend::FlushSettings(std::string* error) {
        std::lock_guard fileLock(settingsFileMutex_);
        if (error) error->clear();
        toggle_settings::Data data;
        std::filesystem::path file;
        std::uint64_t revision{};
        {
            std::lock_guard lock(mutex_);
            if (!settingsInitialized_ || settingsRevision_ == persistedSettingsRevision_) return true;
            file = settingsFile_;
            revision = settingsRevision_;
            for (std::size_t i = 0; i < toggleIntents_.size(); ++i)
                data.emplace(ToggleCatalog()[i].id, toggleIntents_[i].enabled);
        }
        std::string message;
        bool saved{};
        try { saved = !file.empty() && toggle_settings::Save(file, data, message); }
        catch (...) { message = "功能设置保存异常，保留原文件并等待下次保存"; }
        if (saved) {
            std::lock_guard lock(mutex_);
            persistedSettingsRevision_ = revision;
        } else if (message.empty()) message = "功能设置文件路径为空，无法保存";
        if (error) *error = std::move(message);
        return saved;
    }

    bool Backend::IsReady() const {
        std::lock_guard lock(mutex_);
        return ready_;
    }

    Snapshot Backend::GetSnapshot() const {
        std::lock_guard lock(mutex_);
        Snapshot result{ready_, generation_, status_, capabilities_, statistics_,
            {records_.begin(), records_.end()}, queue_.size(), toggleStates_, items_, itemsReady_, itemCatalogStatus_};
        if (settingsInitialized_) {
            for (std::size_t i = 0; i < toggleIntents_.size(); ++i)
                result.togglePreferences.push_back({ToggleCatalog()[i].id, toggleIntents_[i].enabled,
                    toggleIntents_[i].restorePending});
        }
        return result;
    }

    void Backend::FinishToggleRequest(const Request& request, RequestState state, bool executed) {
        if (!settingsInitialized_) return;
        bool enabled{};
        const auto index = ToggleIndex(request.featureId, enabled);
        if (index == toggleIntents_.size()) return;
        auto& intent = toggleIntents_[index];
        if (request.preferenceRevision != intent.revision) return;
        if (!executed) {
            // A request which never called the game may be submitted once ready.
            if (request.restoration) intent.restorePending = true;
            return;
        }
        // No automatic retry after failure or unknown effects in this session.
        intent.restorePending = false;
        if (!request.restoration && state == RequestState::Verified && intent.enabled != enabled) {
            intent.enabled = enabled;
            ++settingsRevision_;
        }
    }

    void Backend::QueueRestoration(Clock::time_point now) {
        if (!settingsInitialized_ || !ready_ || queue_.size() >= queueCapacity_) return;
        for (std::size_t i = 0; i < toggleIntents_.size(); ++i) {
            auto& intent = toggleIntents_[i];
            if (!intent.restorePending) continue;
            const auto& toggle = ToggleCatalog()[i];
            const auto observed = std::find_if(toggleStates_.begin(), toggleStates_.end(),
                [&](const auto& state) { return state.id == toggle.id && state.known; });
            if (observed == toggleStates_.end()) continue;
            if (std::any_of(records_.begin(), records_.end(), [&](const auto& record) {
                return Active(record.state) && (record.request.featureId == toggle.onFeatureId ||
                    record.request.featureId == toggle.offFeatureId);
            })) continue;
            if (observed->enabled == intent.restoreEnabled) {
                intent.restorePending = false;
                continue;
            }
            const auto& featureId = intent.restoreEnabled ? toggle.onFeatureId : toggle.offFeatureId;
            const auto capability = std::find_if(capabilities_.begin(), capabilities_.end(),
                [&](const auto& item) { return item.featureId == featureId && item.ready; });
            if (capability == capabilities_.end()) continue;
            const auto* feature = FindFeature(featureId);
            Request request{nextRequestId_++, generation_, featureId, feature->fixedArguments,
                now + std::chrono::seconds(5), true, intent.revision};
            queue_.push_back(request);
            records_.push_back({request, RequestState::Pending, "正在恢复记住的开关状态"});
            TrimHistory();
            break; // One restore per pump; preserve queue space for explicit actions.
        }
    }

    RequestRecord* Backend::FindRecord(std::uint64_t requestId) {
        const auto found = std::find_if(records_.begin(), records_.end(), [&](const auto& record) {
            return record.request.requestId == requestId;
        });
        return found == records_.end() ? nullptr : &*found;
    }

    void Backend::TrimHistory() {
        while (records_.size() > historyCapacity_) {
            const auto removable = std::find_if(records_.begin(), records_.end(), [](const auto& record) {
                return !Active(record.state);
            });
            if (removable == records_.end()) break;
            records_.erase(removable);
        }
    }

    void Backend::ExpirePending(Clock::time_point now) {
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (now >= it->expiresAt) {
                if (auto* record = FindRecord(it->requestId)) {
                    record->state = RequestState::Expired;
                    record->message = "请求已过期，未调用游戏功能";
                }
                FinishToggleRequest(*it, RequestState::Expired, false);
                it = queue_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Backend::SetRuntimeState(bool ready, std::string status, std::string sessionKey) {
        std::lock_guard lock(mutex_);
        if (ready_ != ready || sessionKey_ != sessionKey) {
            ++generation_;
            for (auto& record : records_) {
                if (record.state == RequestState::Pending) {
                    record.state = RequestState::Cancelled;
                    record.message = "玩家或世界会话已变化，未执行旧请求";
                } else if (record.state == RequestState::Running) {
                    record.state = RequestState::Indeterminate;
                    record.message = "执行时会话发生变化，结果无法确认；不会自动重试";
                }
            }
            queue_.clear();
            statistics_.clear();
            toggleStates_.clear();
            for (auto& intent : toggleIntents_) {
                // A local close or a queued close cancelled by loading must still
                // turn off native switches which survive the readiness change.
                const bool pendingOff = intent.restorePending && !intent.restoreEnabled;
                intent.restorePending = pendingOff || (settingsInitialized_ && intent.enabled);
                intent.restoreEnabled = !pendingOff;
            }
            items_ = ItemList{};
            itemsReady_ = false;
            itemCatalogStatus_ = "进入角色场景后加载物品列表";
            for (auto& capability : capabilities_) {
                capability.ready = false;
                capability.reason = FindFeature(capability.featureId)->unavailableReason;
            }
        }
        ready_ = ready;
        status_ = std::move(status);
        sessionKey_ = std::move(sessionKey);
        TrimHistory();
    }

    void Backend::SetCapability(std::string_view featureId, bool ready, std::string reason) {
        std::lock_guard lock(mutex_);
        const auto* feature = FindFeature(featureId);
        if (!feature) return;
        const auto found = std::find_if(capabilities_.begin(), capabilities_.end(), [&](const auto& item) {
            return item.featureId == featureId;
        });
        found->ready = ready && !feature->functionName.empty();
        found->reason = feature->functionName.empty() ? feature->unavailableReason :
            (reason.empty() && !found->ready ? feature->unavailableReason : std::move(reason));
    }

    void Backend::PublishStatistics(std::vector<Statistic> statistics) {
        std::lock_guard lock(mutex_);
        statistics_ = std::move(statistics);
    }

    void Backend::PublishToggleStates(std::vector<ToggleState> states) {
        std::lock_guard lock(mutex_);
        toggleStates_ = std::move(states);
    }

    void Backend::PublishItems(std::vector<ItemEntry> items, bool ready, std::string status) {
        std::lock_guard lock(mutex_);
        if (!ready_) return;
        items_ = std::move(items);
        itemsReady_ = ready;
        itemCatalogStatus_ = std::move(status);
    }

    SubmitResult Backend::SubmitItem(std::string_view key, int quantity) {
        return Submit("grant_item", {std::string(key), std::to_string(quantity)});
    }

    SubmitResult Backend::Submit(std::string_view featureId, std::vector<std::string> values,
        std::chrono::milliseconds lifetime, Clock::time_point now) {
        const auto* feature = FindFeature(featureId);
        if (!feature) return {false, 0, "未知功能"};
        if (values.empty()) {
            for (const auto& param : feature->params) values.push_back(param.defaultValue);
        }
        std::string error;
        if (!ValidateParameters(*feature, values, error)) return {false, 0, std::move(error)};
        if (lifetime <= std::chrono::milliseconds::zero() || lifetime > std::chrono::seconds(60))
            return {false, 0, "请求有效期必须大于 0 且不超过 60 秒"};

        std::lock_guard lock(mutex_);
        ExpirePending(now);
        bool toggleEnabled{};
        const auto toggleIndex = ToggleIndex(featureId, toggleEnabled);
        const bool rememberToggle = settingsInitialized_ && toggleIndex < toggleIntents_.size();
        if (rememberToggle && !toggleEnabled) {
            auto& intent = toggleIntents_[toggleIndex];
            ++intent.revision; // Invalidates a running enable's eventual completion.
            if (intent.enabled) { intent.enabled = false; ++settingsRevision_; }
            intent.restorePending = true;
            intent.restoreEnabled = false;
            const auto& toggle = ToggleCatalog()[toggleIndex];
            for (auto it = queue_.begin(); it != queue_.end();) {
                if (it->featureId != toggle.onFeatureId && it->featureId != toggle.offFeatureId) { ++it; continue; }
                if (auto* record = FindRecord(it->requestId)) {
                    record->state = RequestState::Cancelled;
                    record->message = "用户已关闭开关，取消旧请求";
                }
                it = queue_.erase(it);
            }
        }
        const auto rememberOff = [&]() -> SubmitResult {
            return {true, 0, "已记住关闭状态，游戏就绪后核对并关闭"};
        };
        if (!ready_) return rememberToggle && !toggleEnabled ? rememberOff() : SubmitResult{false, 0, status_};
        if (featureId == "grant_item") {
            if (!itemsReady_) return {false, 0, "物品列表尚未准备好"};
            const auto item = std::find_if(items_.begin(), items_.end(), [&](const auto& entry) { return entry.key == values[0]; });
            if (item == items_.end()) return {false, 0, "所选物品已失效，请重新选择"};
            if (!item->available) return {false, 0, item->reason.empty() ? "该物品暂时无法获取" : item->reason};
            const int quantity = std::stoi(values[1]); // The Int parameter was validated above.
            if (quantity > item->maxQuantity) return {false, 0, "数量超过该物品单次获取上限"};
        }
        const auto capability = std::find_if(capabilities_.begin(), capabilities_.end(), [&](const auto& item) {
            return item.featureId == featureId;
        });
        if (capability == capabilities_.end() || !capability->ready) {
            if (rememberToggle && !toggleEnabled) return rememberOff();
            return {false, 0, capability == capabilities_.end() ? feature->unavailableReason : capability->reason};
        }
        if (rememberToggle && toggleEnabled) {
            const auto& toggle = ToggleCatalog()[toggleIndex];
            if (std::any_of(records_.begin(), records_.end(), [&](const auto& record) {
                return Active(record.state) && (record.request.featureId == toggle.onFeatureId ||
                    record.request.featureId == toggle.offFeatureId);
            })) return {false, 0, "上一项开关操作尚未完成"};
        }
        if (queue_.size() >= queueCapacity_) {
            if (rememberToggle && !toggleEnabled) return rememberOff();
            return {false, 0, "请求队列已满，请等待当前请求处理"};
        }

        if (rememberToggle && toggleEnabled) {
            auto& intent = toggleIntents_[toggleIndex];
            ++intent.revision;
            intent.restorePending = false;
        }

        Request request{nextRequestId_++, generation_, feature->id, feature->fixedArguments, now + lifetime};
        if (rememberToggle) request.preferenceRevision = toggleIntents_[toggleIndex].revision;
        request.arguments.insert(request.arguments.end(), values.begin(), values.end());
        queue_.push_back(request);
        records_.push_back({request, RequestState::Pending, "已排队，尚未调用游戏功能"});
        TrimHistory();
        return {true, request.requestId, "已排队，尚未调用游戏功能"};
    }

    std::size_t Backend::Pump(const std::function<ExecutionResult(const Request&)>& execute,
        std::size_t budget, Clock::time_point now) {
        if (!execute || budget == 0) return 0;
        std::uint64_t lastEligibleId{};
        {
            std::lock_guard lock(mutex_);
            if (pumping_) return 0;
            ExpirePending(now);
            QueueRestoration(now);
            if (!ready_ || queue_.empty()) return 0;
            pumping_ = true;
            lastEligibleId = queue_.back().requestId;
        }
        // Restores the guard even if an allocation while formatting a result fails.
        struct PumpGuard {
            Backend& backend;
            ~PumpGuard() {
                std::lock_guard lock(backend.mutex_);
                backend.pumping_ = false;
            }
        } guard{*this};
        const auto started = Clock::now();
        std::size_t dispatched{};
        for (std::size_t considered = 0; considered < budget; ++considered) {
            Request request;
            {
                std::lock_guard lock(mutex_);
                ExpirePending(now + (Clock::now() - started));
                if (!ready_ || queue_.empty() || queue_.front().requestId > lastEligibleId) break;
                request = std::move(queue_.front());
                queue_.pop_front();
                auto* record = FindRecord(request.requestId);
                const auto capability = std::find_if(capabilities_.begin(), capabilities_.end(), [&](const auto& item) {
                    return item.featureId == request.featureId;
                });
                if (request.generation != generation_ || capability == capabilities_.end() || !capability->ready) {
                    if (record) {
                        record->state = RequestState::Cancelled;
                        record->message = "会话或功能就绪状态已变化，未执行请求";
                    }
                    FinishToggleRequest(request, RequestState::Cancelled, false);
                    continue;
                }
                if (record) {
                    record->state = RequestState::Running;
                    record->message = "正在游戏线程调用，结果待确认";
                }
            }
            ExecutionResult result;
            try {
                ++dispatched;
                result = execute(request);
                if (!ExecutionState(result.state))
                    result = {RequestState::Indeterminate, "适配器返回了无效结果状态；不会自动重试"};
            } catch (const std::exception&) {
                result = {RequestState::Indeterminate, "游戏调用发生异常，结果无法确认；不会自动重试"};
            } catch (...) {
                result = {RequestState::Indeterminate, "游戏调用发生未知异常，结果无法确认；不会自动重试"};
            }
            {
                std::lock_guard lock(mutex_);
                auto* record = FindRecord(request.requestId);
                if (record && request.generation == generation_ && record->state == RequestState::Running) {
                    record->state = result.state;
                    record->message = result.message.empty() ? RequestStateLabel(result.state) : std::move(result.message);
                    FinishToggleRequest(request, result.state, true);
                }
                TrimHistory();
            }
        }
        return dispatched;
    }

    Backend& GetBackend() {
        static Backend backend;
        return backend;
    }

    const char* RequestStateLabel(RequestState state) {
        switch (state) {
        case RequestState::Pending: return "已排队";
        case RequestState::Running: return "调用中";
        case RequestState::Dispatched: return "已调用，效果未验证";
        case RequestState::Verified: return "已回读验证";
        case RequestState::Failed: return "调用失败";
        case RequestState::Expired: return "已过期，未执行";
        case RequestState::Cancelled: return "已取消，未执行";
        case RequestState::Indeterminate: return "结果无法确认";
        }
        return "未知状态";
    }
}
