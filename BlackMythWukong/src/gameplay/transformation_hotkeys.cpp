#include "transformation_hotkeys.hpp"
#include "transformation_bindings.hpp"
#include "../runtime.hpp"
#include <Windows.h>
#include <format>
#include <mutex>

namespace gloss::gameplay::transformation
{
    features::Result TriggerSkillHotkey(std::string_view characterId, int skillId);
}

namespace gloss::gameplay::transformation::hotkeys
{
    namespace
    {
        std::mutex captureMutex;
        std::optional<CapturedBinding> recording, captured;
        features::Result captureResult{true, {}};
    }
    void BeginCapture(std::string_view characterId, int skillId)
    {
        features::CancelHotkeyCapture();
        std::lock_guard lock(captureMutex);
        captured.reset();
        if (characterId.empty() || skillId <= 0)
        {
            recording.reset();
            captureResult = {false, "请先选择有效角色技能"};
            return;
        }
        recording = CapturedBinding{std::string(characterId), skillId};
        captureResult = {true, "按下单键或 Ctrl / Shift / Alt 组合键；Esc 取消，Delete / Backspace 清除"};
    }
    void CancelCapture()
    {
        std::lock_guard lock(captureMutex);
        recording.reset();
        captured.reset();
        captureResult = {true, {}};
    }
    bool AnyCapture()
    {
        // Starting either global recorder takes ownership of the next press.
        if (features::AnyHotkeyCapture()) { CancelCapture(); return false; }
        std::lock_guard lock(captureMutex);
        return recording.has_value();
    }
    bool Capturing(std::string_view characterId, int skillId)
    {
        if (!AnyCapture()) return false;
        std::lock_guard lock(captureMutex);
        return recording && recording->characterId == characterId && recording->skillId == skillId;
    }
    std::optional<CapturedBinding> TakeCaptured()
    {
        std::lock_guard lock(captureMutex);
        auto result = std::move(captured);
        captured.reset();
        return result;
    }
    features::Result GetCaptureResult()
    {
        std::lock_guard lock(captureMutex);
        return captureResult;
    }
    features::Result Conflicts(unsigned key, unsigned modifiers)
    {
        if (!bindings::Bindable(key, modifiers)) return {false, "按键或组合键无效，或由系统保留"};
        return features::CheckHotkeyConflict(key, modifiers);
    }
    std::string Label(unsigned key, unsigned modifiers)
    {
        if (!key) return "未绑定";
        std::string result;
        if (modifiers & features::Ctrl) result += "Ctrl+";
        if (modifiers & features::Shift) result += "Shift+";
        if (modifiers & features::Alt) result += "Alt+";
        wchar_t name[64]{};
        auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_DELETE || key == VK_INSERT ||
            key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT || key == VK_DIVIDE || key == VK_NUMLOCK)
            scan |= 0x100;
        if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0)
        {
            char text[256]{};
            WideCharToMultiByte(CP_UTF8, 0, name, -1, text, 256, nullptr, nullptr);
            result += text;
        }
        else result += std::format("VK {}", key);
        return result;
    }
    bool HandleHotkey(unsigned key, unsigned modifiers)
    {
        if (AnyCapture())
        {
            std::lock_guard lock(captureMutex);
            if (!recording) return true;
            if (key == VK_ESCAPE)
            {
                recording.reset();
                captureResult = {true, "已取消技能按键录制"};
            }
            else if (key == VK_DELETE || key == VK_BACK)
            {
                captured = *recording;
                captured->key = captured->modifiers = 0;
                recording.reset();
                captureResult = {true, "按键已清除；保存配置后生效"};
            }
            else if (key && !bindings::ModifierKey(key))
            {
                captureResult = Conflicts(key, modifiers);
                if (captureResult.success)
                {
                    captured = *recording;
                    captured->key = key;
                    captured->modifiers = modifiers;
                    recording.reset();
                    captureResult.message = "按键已录制；保存配置后生效";
                }
            }
            return true;
        }
        if (!IsHotkey(key, modifiers)) return false;
        const auto status = GetStatus();
        for (const auto &binding : Bindings(status.activeId))
            if (bindings::Matches(binding, key, modifiers))
            {
                TriggerSkillHotkey(status.activeId, binding.skillId);
                return true;
            }
        return false;
    }
    bool IsHotkey(unsigned key, unsigned modifiers)
    {
        if (!key || !features::HotkeysEnabled() || features::UpdateRequired() || overlay::Visible() || !Conflicts(key, modifiers).success) return false;
        const auto status = GetStatus();
        if (status.phase != Phase::Active) return false;
        const auto values = Bindings(status.activeId);
        return std::any_of(values.begin(), values.end(), [&](const auto &value) { return bindings::Matches(value, key, modifiers); });
    }
}
