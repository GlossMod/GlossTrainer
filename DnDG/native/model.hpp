#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gloss::dndg {

enum class Locale { Game, English, Chinese };
enum class Kind { Sleeve, Hand, UniqueChip, Charm };
enum class Edit { Health, Heal, Shield, Chips, Add, Discard, Replace };

struct Entry {
    std::uint64_t token{}; // ObjectID in the current encounter; 0 for catalog entries.
    std::string id;
    std::string name;
    std::string effect;
    std::string iconKey;
    bool concealed{};
    bool locked{};
};

struct Snapshot {
    std::uint64_t encounter{};
    Locale locale{Locale::English};
    bool canEditVitals{};
    bool canEditHand{};
    bool canEditItems{};
    int health{};
    int maxHealth{};
    int shield{};
    int chips{};
    std::vector<Entry> sleeve;
    std::vector<Entry> hand;
    std::vector<Entry> uniqueChips;
    std::vector<Entry> charms;
    std::optional<Entry> nextCard;
};

struct Catalog {
    std::vector<Entry> cards;
    std::vector<Entry> uniqueChips;
    std::vector<Entry> charms;
};

struct Command {
    Edit edit{};
    Kind kind{};
    // Snapshot coordinates are intentionally not vector indices: native gameplay
    // must find the same live ObjectID and re-check permission on the game thread.
    std::uint64_t encounter{};
    std::uint64_t token{};
    int value{};
    std::string id;
};

enum class Feedback { None, Applied, Stale, Unavailable, Invalid, Busy };

struct UiRequest {
    Locale locale{Locale::Game};
    bool panelOpen{};
    bool showNext{};
};

// Only plain value types cross the render / gameplay thread boundary. Neither
// thread waits for the other to process a command; engine objects remain local.
void PublishSnapshot(Snapshot value);
void PublishCatalog(Catalog value);
std::shared_ptr<const Snapshot> ReadSnapshot();
std::shared_ptr<const Catalog> ReadCatalog();
bool SubmitCommand(Command command);
std::vector<Command> TakeCommands();
void PublishFeedback(Feedback result);
Feedback TakeFeedback();
void PublishUiRequest(UiRequest value);
UiRequest ReadUiRequest();

} // namespace gloss::dndg
