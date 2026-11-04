#include "runtime.hpp"
#include <chrono>
#include <fstream>
#include <format>
#include <condition_variable>
#include <deque>
#include <thread>
#include <string>
#include <utility>

namespace gloss
{
    namespace
    {
        HMODULE moduleHandle{};
        std::recursive_mutex overlayMutex;
        struct AsyncLogger {
            std::mutex mutex;
            std::condition_variable wake;
            std::deque<std::string> pending;
            bool started{};
            bool dropped{};

            void EnsureStarted() {
                if (started) return;
                started = true;
                std::thread([this] { Run(); }).detach();
            }
            void Enqueue(std::string line) {
                {
                    std::lock_guard lock(mutex);
                    EnsureStarted();
                    // Logging must never make the game thread wait for disk I/O.
                    // Keep a bounded queue; preserve a marker that the worker
                    // emits once capacity is available so dropped diagnostics
                    // remain visible without unbounded memory growth.
                    constexpr std::size_t QueueLimit = 4096;
                    if (pending.size() >= QueueLimit) {
                        pending.pop_front();
                        dropped = true;
                    }
                    pending.push_back(std::move(line));
                }
                wake.notify_one();
            }
            void Run() {
                for (;;) {
                    std::deque<std::string> batch;
                    bool reportDropped{};
                    {
                        std::unique_lock lock(mutex);
                        wake.wait(lock, [this] { return !pending.empty(); });
                        batch.swap(pending);
                        reportDropped = std::exchange(dropped, false);
                    }
                    try {
                        const auto directory = DataDirectory() / L"logs";
                        std::filesystem::create_directories(directory);
                        const auto file = directory / L"trainer.log";
                        std::error_code error;
                        if (std::filesystem::file_size(file, error) > 2 * 1024 * 1024 && !error) {
                            std::filesystem::remove(directory / L"trainer.previous.log", error);
                            std::filesystem::rename(file, directory / L"trainer.previous.log", error);
                        }
                        std::ofstream stream(file, std::ios::binary | std::ios::app);
                        if (reportDropped) stream << "GlossTrainer: log queue overflow; older diagnostics were dropped\n";
                        for (const auto& line : batch) {
                            stream << line;
                            OutputDebugStringA(line.c_str());
                        }
                    }
                    catch (...) {
                        OutputDebugStringA("GlossTrainer: log write failed\n");
                    }
                }
            }
        };
        // Intentionally leaked: the worker may still be draining at process
        // teardown, and DLL detach must not wait on a game-owned thread.
        AsyncLogger* logger{};
        std::once_flag loggerOnce;
    }
    void SetModule(HMODULE module) { moduleHandle = module; }
    HMODULE Module() { return moduleHandle; }
    std::recursive_mutex &OverlayMutex() { return overlayMutex; }
    const std::filesystem::path &DataDirectory()
    {
        static const auto directory = []
        {
            wchar_t path[32768]{};
            GetModuleFileNameW(moduleHandle, path, static_cast<DWORD>(std::size(path)));
            return std::filesystem::path(path).parent_path() / L"GlossTrainer" / L"PBZ";
        }();
        return directory;
    }
    void Log(std::string_view message)
    {
        try
        {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            const auto line = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] {}\n",
                                          time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                                          time.wSecond, time.wMilliseconds, GetCurrentThreadId(), message);
            std::call_once(loggerOnce, [] { logger = new AsyncLogger; });
            logger->Enqueue(line);
        }
        catch (...)
        {
            OutputDebugStringA("GlossTrainer: log write failed\n");
        }
    }
}
