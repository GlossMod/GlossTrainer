#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gloss::gameplay {
    enum class ParamType { Float, Int, Name, String, Bool };

    struct ParamDef {
        std::string name;
        std::string label;
        ParamType type{ParamType::String};
        std::string defaultValue;
        double minimum{};
        double maximum{};
        std::size_t maxLength{128};
    };

    struct FeatureDef {
        std::string id;
        std::string title;
        std::string description;
        int category{}; // 1 survival, 2 combat, 3 resources, 4 movement, 5 world.
        int page{1}; // 1 features, 2 items, 3 travel.
        std::vector<ParamDef> params;
        std::string functionName;
        std::vector<std::string> fixedArguments;
        bool persistent{}; // May change saved inventory/progression; never auto-retried.
        std::string unavailableReason;
    };

    struct ToggleDef {
        std::string id;
        std::string title;
        std::string description;
        int category{};
        int page{1};
        std::string onFeatureId;
        std::string offFeatureId;
    };

    const std::vector<FeatureDef>& Catalog();
    const std::vector<ToggleDef>& ToggleCatalog();
    const FeatureDef* FindFeature(std::string_view id);
    // Validates user parameters only. Name accepts an existing FName comparison ID
    // in decimal or 0x hex form, optionally followed by :number (both uint32).
    bool ValidateParameters(const FeatureDef& feature, const std::vector<std::string>& values,
        std::string& error);
}
