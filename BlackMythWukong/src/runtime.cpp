#include "runtime.hpp"
#include <chrono>
#include <fstream>
#include <format>
#include <string>

namespace gloss {
namespace {
HMODULE moduleHandle{};
std::recursive_mutex overlayMutex;
std::mutex logMutex;
}
void SetModule(HMODULE module) { moduleHandle = module; }
HMODULE Module() { return moduleHandle; }
std::recursive_mutex& OverlayMutex() { return overlayMutex; }
const std::filesystem::path& DataDirectory() {
    static const auto directory = [] {
        wchar_t path[32768]{};
        GetModuleFileNameW(moduleHandle, path, static_cast<DWORD>(std::size(path)));
        return std::filesystem::path(path).parent_path() / L"GlossTrainer";
    }();
    return directory;
}
void Log(std::string_view message) {
    try {
        std::lock_guard lock(logMutex);
        const auto directory = DataDirectory() / L"logs";
        std::filesystem::create_directories(directory);
        const auto file = directory / L"trainer.log";
        std::error_code error;
        if (std::filesystem::file_size(file, error) > 2 * 1024 * 1024 && !error) {
            std::filesystem::remove(directory / L"trainer.previous.log", error);
            std::filesystem::rename(file, directory / L"trainer.previous.log", error);
        }
        SYSTEMTIME time{};
        GetLocalTime(&time);
        const auto line = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] {}\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, time.wMilliseconds, GetCurrentThreadId(), message);
        std::ofstream stream(file, std::ios::binary | std::ios::app);
        stream << line;
        OutputDebugStringA(line.c_str());
    } catch (...) {
        OutputDebugStringA("GlossTrainer: log write failed\n");
    }
}
}
