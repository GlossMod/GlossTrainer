#include "model.hpp"

#include <atomic>
#include <mutex>
#include <utility>

namespace gloss::dndg {
namespace {

constexpr std::size_t MaxPendingCommands = 16;
constexpr std::size_t MaxIdBytes = 128;

struct Channel {
    std::atomic<std::shared_ptr<const Snapshot>> snapshot{std::make_shared<const Snapshot>()};
    std::atomic<std::shared_ptr<const Catalog>> catalog{std::make_shared<const Catalog>()};
    std::atomic<Feedback> feedback{Feedback::None};
    std::atomic<unsigned int> uiFlags{};
    std::mutex queueMutex;
    std::vector<Command> commands;
};

Channel& State() {
    static Channel channel;
    return channel;
}

} // namespace

void PublishSnapshot(Snapshot value) {
    State().snapshot.store(std::make_shared<const Snapshot>(std::move(value)), std::memory_order_release);
}

void PublishCatalog(Catalog value) {
    State().catalog.store(std::make_shared<const Catalog>(std::move(value)), std::memory_order_release);
}

std::shared_ptr<const Snapshot> ReadSnapshot() {
    return State().snapshot.load(std::memory_order_acquire);
}

std::shared_ptr<const Catalog> ReadCatalog() {
    return State().catalog.load(std::memory_order_acquire);
}

bool SubmitCommand(Command command) {
    if (!command.encounter || command.id.size() > MaxIdBytes) {
        PublishFeedback(Feedback::Invalid);
        return false;
    }
    if ((command.edit == Edit::Replace || command.edit == Edit::Discard) && !command.token) {
        PublishFeedback(Feedback::Invalid);
        return false;
    }
    if ((command.edit == Edit::Replace || command.edit == Edit::Add) && command.id.empty()) {
        PublishFeedback(Feedback::Invalid);
        return false;
    }
    auto& state = State();
    std::lock_guard lock(state.queueMutex);
    if (state.commands.size() >= MaxPendingCommands) {
        PublishFeedback(Feedback::Busy);
        return false;
    }
    state.commands.push_back(std::move(command));
    return true;
}

std::vector<Command> TakeCommands() {
    std::vector<Command> result;
    auto& state = State();
    std::lock_guard lock(state.queueMutex);
    result.swap(state.commands);
    return result;
}

void PublishFeedback(Feedback result) {
    State().feedback.store(result, std::memory_order_release);
}

Feedback TakeFeedback() {
    return State().feedback.exchange(Feedback::None, std::memory_order_acq_rel);
}

void PublishUiRequest(UiRequest value) {
    const auto flags = static_cast<unsigned int>(value.locale) | (value.panelOpen ? 4u : 0u) |
        (value.showNext ? 8u : 0u);
    State().uiFlags.store(flags, std::memory_order_release);
}

UiRequest ReadUiRequest() {
    const auto flags = State().uiFlags.load(std::memory_order_acquire);
    return {static_cast<Locale>(flags & 3u), (flags & 4u) != 0, (flags & 8u) != 0};
}

} // namespace gloss::dndg
