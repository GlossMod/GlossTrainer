#pragma once
#include <cstddef>
#include <cstdint>

namespace gloss::gameplay {
    // The catalog is private until every table and parent link has been checked.
    // Keeping the cursors separate from engine pointers makes a reset discard
    // every unfinished phase, including a previously completed table prefix.
    struct ItemCatalogProgress {
        enum class Phase { Tables, ParentIndex, ParentLinks, VerifyTables, Complete };
        Phase phase{Phase::Tables};
        std::size_t table{}, row{};
        std::int32_t slot{}, allocated{};
        bool tableReady{};

        void FinishTable(std::size_t tableCount) {
            ++table;
            slot = allocated = 0;
            tableReady = false;
            if (table == tableCount) { phase = Phase::ParentIndex; row = 0; }
        }
        void FinishRows(std::size_t rowCount) {
            if (row != rowCount) return;
            row = 0;
            if (phase == Phase::ParentIndex) phase = Phase::ParentLinks;
            else if (phase == Phase::ParentLinks) { phase = Phase::VerifyTables; table = 0; }
        }
        void FinishVerification(std::size_t tableCount) {
            if (++table == tableCount) phase = Phase::Complete;
        }
        bool Complete() const { return phase == Phase::Complete; }
    };
}
