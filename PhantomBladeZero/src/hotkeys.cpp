#include "hotkeys.hpp"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <fstream>

namespace gloss::hotkeys {
    namespace {
        constexpr std::size_t MaxActions = 256, MaxParameters = 16, MaxParameterBytes = 1024, MaxFileBytes = 1024 * 1024;
        bool ValidId(std::string_view id) {
            return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '-';
            });
        }
        bool ParametersValid(const std::vector<std::string>& values, std::size_t count) {
            return values.size() == count && values.size() <= MaxParameters && std::all_of(values.begin(), values.end(), [](const auto& s) { return s.size() <= MaxParameterBytes; });
        }
        bool ModifierKey(unsigned key) { return key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || (key >= VK_LSHIFT && key <= VK_RMENU) || key == VK_LWIN || key == VK_RWIN; }
        bool KeyboardKey(unsigned key) { return key >= VK_BACK && key < 255; }
        std::string Hex(std::string_view value) {
            constexpr char digits[] = "0123456789abcdef";
            std::string out; out.reserve(value.size() * 2);
            for (unsigned char c : value) { out.push_back(digits[c >> 4]); out.push_back(digits[c & 15]); }
            return out;
        }
        std::optional<std::string> Unhex(std::string_view value) {
            if (value.size() % 2 || value.size() > MaxParameterBytes * 2) return {};
            std::string out;
            auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
            for (std::size_t i = 0; i < value.size(); i += 2) {
                const auto a = digit(value[i]), b = digit(value[i + 1]);
                if (a < 0 || b < 0) return {};
                out.push_back(static_cast<char>((a << 4) | b));
            }
            return out;
        }
        bool Number(std::string_view value, unsigned& result) {
            const auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
            return converted.ec == std::errc{} && converted.ptr == value.data() + value.size();
        }
    }

    std::string ChordError(Chord chord) {
        if (!KeyboardKey(chord.key) || ModifierKey(chord.key) || chord.key == VK_PROCESSKEY || chord.key == VK_PACKET) return "请选择一个键盘按键，可组合 Ctrl / Shift / Alt。";
        if (chord.modifiers & ~unsigned(Control | Shift | Alt)) return "Windows 组合键由系统保留。";
        if (chord.key == VK_INSERT) return "Insert 由显示或隐藏菜单保留。";
        if (chord.key == VK_ESCAPE || chord.key == VK_DELETE || chord.key == VK_TAB) return "Escape、Delete 和 Tab 为界面保留键。";
        if (chord.key == VK_F4 && (chord.modifiers & Alt)) return "Alt + F4 由系统保留。";
        if (chord.key == VK_SPACE && (chord.modifiers & Alt)) return "Alt + Space 由系统保留。";
        return {};
    }
    std::string ChordLabel(Chord chord) {
        if (!chord.key) return "未绑定";
        std::string label;
        if (chord.modifiers & Control) label += "Ctrl+";
        if (chord.modifiers & Shift) label += "Shift+";
        if (chord.modifiers & Alt) label += "Alt+";
        const auto key = chord.key;
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) label.push_back(static_cast<char>(key));
        else if (key >= VK_F1 && key <= VK_F24) label += "F" + std::to_string(key - VK_F1 + 1);
        else {
            wchar_t name[64]{};
            auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16;
            if (key == VK_PRIOR || key == VK_NEXT || key == VK_END || key == VK_HOME || key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN || key == VK_DIVIDE || key == VK_NUMLOCK) scan |= 1u << 24;
            if (GetKeyNameTextW(static_cast<LONG>(scan), name, static_cast<int>(std::size(name)))) {
                char utf8[256]{};
                const auto count = WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
                if (count > 1) label.append(utf8, count - 1);
            }
            if (label.empty() || label.back() == '+') label += "VK " + std::to_string(key);
        }
        return label;
    }

    void Registry::Configure(std::vector<Action> actions) {
        std::lock_guard lock(mutex_);
        actions_.clear();
        for (auto& action : actions) if (actions_.size() < MaxActions && ValidId(action.id) && ParametersValid(action.defaults, action.defaults.size())) actions_.emplace(action.id, std::move(action));
        for (auto it = bindings_.begin(); it != bindings_.end();) {
            const auto found = actions_.find(it->first);
            if (found == actions_.end() || !ParametersValid(it->second.parameters, found->second.defaults.size())) { it = bindings_.erase(it); dirty_ = true; }
            else ++it;
        }
        queue_.clear(); pending_.clear(); capture_ = {}; captureKeys_.clear();
    }
    std::optional<Binding> Registry::Get(std::string_view action) const {
        std::lock_guard lock(mutex_); const auto found = bindings_.find(action);
        return found == bindings_.end() ? std::nullopt : std::optional(found->second);
    }
    bool Registry::SetLocked(std::string_view action, Chord chord, std::vector<std::string> parameters, std::string& error) {
        error.clear();
        const auto definition = actions_.find(action);
        if (definition == actions_.end()) { error = "此功能已不再提供。"; return false; }
        if (!chord.key) { if (bindings_.erase(std::string(action))) dirty_ = true; return true; }
        if (!(error = ChordError(chord)).empty()) return false;
        if (!ParametersValid(parameters, definition->second.defaults.size())) { error = "绑定参数无效。"; return false; }
        for (const auto& [id, binding] : bindings_) if (id != action && binding.chord == chord) {
            const auto other = actions_.find(id);
            error = "此快捷键已绑定：" + (other != actions_.end() && !other->second.title.empty() ? other->second.title : std::string("其他功能"));
            return false;
        }
        bindings_[std::string(action)] = {chord, std::move(parameters)}; dirty_ = true; return true;
    }
    bool Registry::Set(std::string_view action, Chord chord, std::vector<std::string> parameters, std::string& error) {
        std::lock_guard lock(mutex_); return SetLocked(action, chord, std::move(parameters), error);
    }
    void Registry::SetParameters(std::string_view action, std::vector<std::string> parameters) {
        std::lock_guard lock(mutex_);
        const auto found = bindings_.find(action);
        const auto spec = actions_.find(action);
        if (found != bindings_.end() && spec != actions_.end() && ParametersValid(parameters, spec->second.defaults.size()) && found->second.parameters != parameters) { found->second.parameters = std::move(parameters); dirty_ = true; }
    }
    void Registry::BeginCapture(std::string_view action, std::vector<std::string> parameters) {
        std::lock_guard lock(mutex_);
        const auto found = actions_.find(action);
        if (found == actions_.end() || !ParametersValid(parameters, found->second.defaults.size())) return;
        capture_ = {std::string(action), "按下快捷键；Escape 取消，Delete 清除。"}; captureParameters_ = std::move(parameters); captureKeys_.clear(); queue_.clear(); pending_.clear();
    }
    void Registry::CancelCapture() { std::lock_guard lock(mutex_); capture_ = {}; captureKeys_.clear(); }
    CaptureState Registry::Capture() const { std::lock_guard lock(mutex_); return capture_; }
    bool Registry::Matches(Chord chord, bool foreground, bool menuVisible) const {
        std::lock_guard lock(mutex_);
        if (!foreground || !KeyboardKey(chord.key)) return false;
        if (!capture_.action.empty()) return true;
        if (menuVisible) return false;
        return std::any_of(bindings_.begin(), bindings_.end(), [&](const auto& pair) { return pair.second.chord == chord; });
    }
    bool Registry::QueueKey(Chord chord, bool foreground, bool menuVisible) {
        std::lock_guard lock(mutex_);
        if (!foreground || !KeyboardKey(chord.key)) return false;
        if (!capture_.action.empty()) {
            if (!ModifierKey(chord.key) && captureKeys_.size() < 8) captureKeys_.push_back(chord);
            return true;
        }
        if (menuVisible) return false;
        for (const auto& [id, binding] : bindings_) if (binding.chord == chord) {
            if (queue_.size() < 32 && pending_.insert(id).second) queue_.push_back({id, binding.parameters});
            return true;
        }
        return false;
    }
    void Registry::PumpCapture() {
        std::lock_guard lock(mutex_);
        while (!captureKeys_.empty() && !capture_.action.empty()) {
            const auto chord = captureKeys_.front(); captureKeys_.pop_front();
            if (chord.key == VK_ESCAPE) { capture_ = {}; break; }
            std::string error;
            if (SetLocked(capture_.action, chord.key == VK_DELETE ? Chord{} : chord, captureParameters_, error)) { capture_ = {}; break; }
            capture_.message = std::move(error);
        }
        captureKeys_.clear();
    }
    std::vector<Invocation> Registry::Drain() {
        std::lock_guard lock(mutex_);
        std::vector<Invocation> result;
        while (!queue_.empty()) { result.push_back(std::move(queue_.front())); queue_.pop_front(); }
        return result;
    }
    void Registry::Complete(std::string_view action) { std::lock_guard lock(mutex_); pending_.erase(std::string(action)); }
    void Registry::OnFocusLost() { std::lock_guard lock(mutex_); capture_ = {}; captureKeys_.clear(); queue_.clear(); pending_.clear(); }
    bool Registry::Dirty() const { std::lock_guard lock(mutex_); return dirty_; }

    bool Registry::Load(const std::filesystem::path& path, std::string& error) {
        std::lock_guard lock(mutex_); error.clear();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) && !ec) return true;
        const auto bytes = std::filesystem::file_size(path, ec);
        if (ec || bytes > MaxFileBytes) { error = "快捷键文件无法读取或过大。"; return false; }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) { error = "快捷键文件无法读取。"; return false; }
        std::map<std::string, std::map<std::string, std::string>, std::less<>> sections;
        std::string line, section;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == ';') continue;
            if (line.size() > 4096) { error = "快捷键文件行过长。"; return false; }
            if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); if (!ValidId(section) || sections.size() >= MaxActions) { error = "快捷键文件节无效。"; return false; } }
            else { const auto equals = line.find('='); if (!section.empty() && equals != std::string::npos) sections[section][line.substr(0, equals)] = line.substr(equals + 1); }
        }
        if (!stream.eof()) { error = "快捷键文件读取未完成。"; return false; }
        bindings_.clear();
        for (const auto& [id, values] : sections) {
            const auto spec = actions_.find(id);
            if (spec == actions_.end()) continue;
            const auto key = values.find("key"), mods = values.find("modifiers"), count = values.find("count");
            unsigned vk{}, modifiers{}, parameters{};
            if (key == values.end() || mods == values.end() || count == values.end() || !Number(key->second, vk) || !Number(mods->second, modifiers) || !Number(count->second, parameters) || parameters != spec->second.defaults.size() || parameters > MaxParameters) continue;
            std::vector<std::string> decoded;
            for (unsigned i = 0; i < parameters; ++i) {
                const auto found = values.find("p" + std::to_string(i));
                auto value = found == values.end() ? std::nullopt : Unhex(found->second);
                if (!value) break;
                decoded.push_back(std::move(*value));
            }
            std::string ignored;
            SetLocked(id, {vk, modifiers}, std::move(decoded), ignored);
        }
        dirty_ = false; queue_.clear(); pending_.clear(); capture_ = {}; captureKeys_.clear();
        return true;
    }
    bool Registry::Save(const std::filesystem::path& path, std::string& error) {
        std::lock_guard lock(mutex_); error.clear();
        try {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            auto temporary = path; temporary += L".tmp";
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                stream << "; GlossTrainer PBZ hotkeys v1; parameters are UTF-8 hex\n";
                for (const auto& [id, binding] : bindings_) {
                    stream << '[' << id << "]\nkey=" << binding.chord.key << "\nmodifiers=" << binding.chord.modifiers << "\ncount=" << binding.parameters.size() << '\n';
                    for (std::size_t i = 0; i < binding.parameters.size(); ++i) stream << 'p' << i << '=' << Hex(binding.parameters[i]) << '\n';
                    stream << '\n';
                }
                stream.flush(); if (!stream) { error = "保存快捷键失败。"; return false; }
            }
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { error = "替换快捷键文件失败。"; return false; }
            dirty_ = false; return true;
        } catch (...) { error = "保存快捷键失败。"; return false; }
    }
    Registry& GetRegistry() { static Registry registry; return registry; }
}
