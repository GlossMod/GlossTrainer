#pragma once
#include "transformation.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <span>
#include <unordered_set>

namespace gloss::gameplay::transformation::model
{
    // Unreal object paths are case-insensitive. Form remains part of the key so
    // stage/ally variants sharing a character class are not silently collapsed.
    inline std::string StableId(std::string_view path, std::string_view form = {})
    {
        const auto quote = path.find('\'');
        if (quote != std::string_view::npos && path.back() == '\'')
            path = path.substr(quote + 1, path.size() - quote - 2);
        std::string value(path);
        value += '#';
        value += form;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    inline void MergeCatalog(std::vector<CharacterEntry> &entries, CharacterEntry entry)
    {
        entry.id = StableId(entry.path, entry.form);
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto &v) { return v.id == entry.id; });
        if (it == entries.end())
            entries.push_back(std::move(entry));
        else
        {
            it->discovered |= entry.discovered;
            if (entry.resourceId > 0) it->resourceId = entry.resourceId;
            if (entry.overrideId > 0) it->overrideId = entry.overrideId;
            if (it->name.empty()) it->name = std::move(entry.name);
            // Discovery proves presence, never playable/verified status.
        }
    }
    inline bool ValidSlots(std::span<const SkillSlot> slots, std::span<const SkillEntry> skills)
    {
        if (slots.size() > 8) return false;
        std::unordered_set<int> actions;
        for (const auto &slot : slots)
        {
            const auto action = static_cast<int>(slot.action);
            if (action < 0 || action >= 8 || !actions.insert(action).second || slot.name.size() > 128 || slot.skills.size() > 32)
                return false;
            for (const auto id : slot.skills)
                if (id <= 0 || std::none_of(skills.begin(), skills.end(), [&](const auto &skill) { return skill.id == id && skill.available; }))
                    return false;
        }
        return true;
    }
    struct ObjectKey
    {
        std::int32_t index{-1}, serial{};
        bool operator==(const ObjectKey &) const = default;
        bool Valid() const { return index >= 0 && serial > 0; }
    };
    struct ContextKey
    {
        ObjectKey player, controller, world;
        std::uint64_t generation{};
        bool operator==(const ContextKey &) const = default;
        bool Valid() const { return player.Valid() && controller.Valid() && world.Valid(); }
    };
    inline bool Expired(std::uint64_t now, std::uint64_t since, std::uint64_t timeout)
    {
        return now < since || now - since >= timeout;
    }
    class Combo
    {
    public:
        bool Queue(std::span<const int> skills, std::uint64_t now)
        {
            if (Busy() || skills.empty() || skills.size() > 32 || std::any_of(skills.begin(), skills.end(), [](int id) { return id <= 0; }))
                return false;
            sequence.assign(skills.begin(), skills.end());
            index = 0;
            accepted = observed = failed = false;
            changed = now;
            return true;
        }
        std::optional<int> Next(int casting, std::uint64_t now)
        {
            if (!Busy()) return {};
            if (Expired(now, changed, accepted ? 30000 : 5000))
            {
                failed = true;
                sequence.clear();
                return {};
            }
            if (accepted)
            {
                if (casting == sequence[index]) observed = true;
                if (!observed || casting > 0) return {};
                accepted = observed = false;
                changed = now;
                if (++index == sequence.size())
                {
                    sequence.clear();
                    return {};
                }
            }
            if (casting > 0) return {};
            return sequence[index];
        }
        void Result(bool result, std::uint64_t now)
        {
            if (!Busy()) return;
            if (!result)
            {
                sequence.clear();
                failed = true;
                return;
            }
            accepted = true;
            changed = now;
        }
        void Reset() { sequence.clear(); index = 0; accepted = observed = failed = false; }
        bool Busy() const { return !sequence.empty(); }
        bool Failed() const { return failed; }
        std::size_t Position() const { return index; }
    private:
        std::vector<int> sequence;
        std::size_t index{};
        bool accepted{}, observed{}, failed{};
        std::uint64_t changed{};
    };

    enum class Progress { Pending, Complete, Failed };
    // The driver owns weak references and restoration baselines. Failed progress
    // can still mean partial work; Restore must therefore be idempotent.
    struct SessionBackend
    {
        virtual ~SessionBackend() = default;
        virtual Progress Load(const CharacterEntry &) = 0;
        virtual Progress Initialize(const CharacterEntry &) = 0;
        virtual Progress Activate() = 0;
        virtual Progress Update() = 0;
        virtual Progress Restore() = 0;
    };
    class Session
    {
    public:
        bool Begin(CharacterEntry entry, ContextKey context, std::uint64_t now)
        {
            if (phase != Phase::Idle || !context.Valid() || entry.id.empty()) return false;
            character = std::move(entry);
            key = context;
            phase = Phase::Loading;
            changed = now;
            initialized = false;
            return true;
        }
        void RequestRestore()
        {
            if (phase != Phase::Idle) phase = Phase::Restoring;
        }
        void Tick(SessionBackend &backend, ContextKey context, std::uint64_t now, bool controlsAvailable = true)
        {
            if (phase == Phase::Idle) return;
            if (context != key || !context.Valid()) phase = Phase::Restoring;
            if ((phase == Phase::Loading || phase == Phase::Initializing) && Expired(now, changed, 15000))
                phase = Phase::Restoring;
            if (phase == Phase::Restoring || phase == Phase::RecoveryRequired)
            {
                const auto result = backend.Restore();
                if (result == Progress::Complete)
                {
                    phase = Phase::Idle;
                    character = {};
                    key = {};
                }
                else if (result == Progress::Failed) phase = Phase::RecoveryRequired;
                return;
            }
            if (!controlsAvailable) return;
            if (phase == Phase::Loading)
            {
                const auto result = backend.Load(character);
                if (result == Progress::Failed) phase = Phase::Restoring;
                else if (result == Progress::Complete) phase = Phase::Initializing;
            }
            else if (phase == Phase::Initializing)
            {
                const auto result = initialized ? Progress::Complete : backend.Initialize(character);
                if (result == Progress::Failed) phase = Phase::Restoring;
                else if (result == Progress::Complete)
                {
                    initialized = true;
                    const auto active = backend.Activate();
                    if (active == Progress::Complete) phase = Phase::Active;
                    else if (active == Progress::Failed) phase = Phase::Restoring;
                }
            }
            else if (phase == Phase::Active && backend.Update() == Progress::Failed)
                phase = Phase::Restoring;
        }
        Phase State() const { return phase; }
        bool Exclusive() const { return phase != Phase::Idle; }
        const CharacterEntry &Character() const { return character; }
        ContextKey Context() const { return key; }
    private:
        Phase phase{Phase::Idle};
        CharacterEntry character;
        ContextKey key;
        std::uint64_t changed{};
        bool initialized{};
    };
}
