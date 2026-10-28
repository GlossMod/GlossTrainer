#pragma once
#include "transformation_model.hpp"

namespace gloss::gameplay::transformation::pak_catalog
{
    struct Record
    {
        const char *name, *category, *path;
    };
    // Asset Registry parent-class evidence and PAK presence establish candidates,
    // not current-build compatibility, initialization, or playable support.
    inline constexpr Record Records[] = {
#include "transformation_pak_catalog.inc"
    };
    inline std::size_t Seed(std::vector<CharacterEntry> &entries)
    {
        const auto before = entries.size();
        for (const auto &record : Records)
        {
            CharacterEntry entry;
            entry.name = record.name;
            entry.category = model::CatalogCategory(record.category);
            entry.path = record.path;
            entry.support = Support::Unverified;
            entry.discovered = false;
            entry.reason = "PAK 中存在角色配置；尚未验证变身与招式";
            // Reference names and any runtime support/diagnostics win when the
            // same canonical asset was already merged from another source.
            model::MergeCatalog(entries, std::move(entry));
        }
        return entries.size() - before;
    }
}
