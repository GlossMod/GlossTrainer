#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

namespace gloss::gameplay
{
    // UI submission and the worker's expiry are one transaction. A captured game
    // callback never waits for either: contention leaves the action for its next
    // genuine game event. Completion cannot overwrite a newer submission.
    class PendingAction
    {
    public:
        enum Status
        {
            Idle,
            Queued,
            Completed,
            Expired
        };
        static constexpr std::uint64_t Lifetime = 30000;

        void Queue(int value, std::uint64_t now)
        {
            std::lock_guard lock(mutex_);
            value_ = value;
            queuedAt_ = now;
            status_.store(Queued, std::memory_order_release);
        }
        int State() const noexcept { return status_.load(std::memory_order_acquire); }
        void Expire(std::uint64_t now)
        {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (lock && value_ >= 0 && TimedOut(now))
                Cancel();
        }
        template <class Writer>
        bool Consume(std::uint64_t now, Writer &&writer)
        {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (!lock || value_ < 0)
                return false;
            if (TimedOut(now))
            {
                Cancel();
                return false;
            }
            const auto value = value_;
            value_ = -1;
            const bool written = writer(value);
            status_.store(written ? Completed : Expired, std::memory_order_release);
            return written;
        }

    private:
        bool TimedOut(std::uint64_t now) const noexcept
        {
            return now >= queuedAt_ && now - queuedAt_ > Lifetime;
        }
        void Cancel() noexcept
        {
            value_ = -1;
            status_.store(Expired, std::memory_order_release);
        }
        std::mutex mutex_;
        std::atomic<int> status_{Idle};
        int value_ = -1;
        std::uint64_t queuedAt_{};
    };
}
