#pragma once
#include "transformation_model.hpp"

namespace gloss::gameplay::transformation::model
{
    // Source-mod positions are one-time adaptation hints, never stored bindings.
    // Resolve against this build's enumerated IDs; missing hints leave a slot
    // empty. Names describe the reference, not a verified in-game move.
    inline std::vector<SkillSlot> StarterSlots(std::string_view path, std::span<const SkillEntry> skills)
    {
        std::vector<SkillSlot> slots;
        for (int i = 0; i != 8; ++i)
            slots.push_back({ActionName(static_cast<Action>(i)), static_cast<Action>(i), {}});
        struct Hint { std::size_t position; const char *name; };
        constexpr std::array<Hint, 8> yangjian{{
            {8, "丝滑五连"}, {10, "斧子下劈"}, {96, "侧闪"}, {1, "点射激光"},
            {2, "横扫"}, {17, "撑杆跳"}, {41, "斧与激光"}, {45, "巨斧横扫"}
        }};
        constexpr std::array<Hint, 8> dasheng{{
            {1, "横扫与戳"}, {33, "拳与棍"}, {59, "后翻滚"}, {12, "二连戳"},
            {15, "四段劈"}, {35, "弹簧棍"}, {73, "前突刺"}, {4, "棍花劈棍"}
        }};
        const auto id = StableId(path);
        const auto *hints = id.find("tamer_mgd_yangjian_01.") != std::string::npos ? &yangjian :
                            id.find("tamer_mgd_jsds.") != std::string::npos ? &dasheng : nullptr;
        if (!hints) return slots;
        for (std::size_t i = 0; i != hints->size(); ++i)
        {
            const auto &hint = (*hints)[i];
            if (hint.position == 0 || hint.position > skills.size()) continue;
            const auto &skill = skills[hint.position - 1];
            if (skill.id <= 0 || !skill.available) continue;
            slots[i].skills = {skill.id};
            slots[i].name += std::string(" · 参考：") + hint.name;
        }
        return slots;
    }
}
