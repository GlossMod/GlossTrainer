#include "model.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>

using namespace gloss::dndg;

void Check(bool result, const char* label) {
    if (!result) {
        std::cerr << "FAIL: " << label << '\n';
        std::exit(1);
    }
}

int main() {
    Check(ReadSnapshot()->encounter == 0, "defaults to no encounter");
    Snapshot snapshot;
    snapshot.encounter = 42;
    snapshot.health = 17;
    snapshot.nextCard = Entry{21, "01", "Top", "Rules"};
    PublishSnapshot(std::move(snapshot));
    const auto previous = ReadSnapshot();
    PublishSnapshot({});
    Check(previous->nextCard && previous->health == 17, "reader owns immutable generation");
    Check(!ReadSnapshot()->nextCard, "clearing snapshot also clears preview");
    std::jthread game([] {
        Catalog catalog;
        catalog.cards.push_back({0, "CARD", "Card"});
        PublishCatalog(std::move(catalog));
    });
    game.join();
    Check(ReadCatalog()->cards.front().id == "CARD", "catalog published across threads");
    Check(!SubmitCommand({Edit::Heal}), "cannot edit without encounter token");
    Check(TakeFeedback() == Feedback::Invalid, "failed submission publishes feedback");
    Check(TakeFeedback() == Feedback::None, "feedback read consumes value");
    Check(!SubmitCommand({Edit::Replace, Kind::Hand, 42, 0, 0, "CARD"}), "replacement requires object token");
    Check(!SubmitCommand({Edit::Add, Kind::Charm, 42}), "add requires catalog id");
    for (int i = 0; i < 16; ++i)
        Check(SubmitCommand({Edit::Health, Kind::Hand, 42, 0, i}), "queue accepts bounded commands");
    Check(!SubmitCommand({Edit::Health, Kind::Hand, 42, 0, 16}), "queue rejects overflow");
    Check(TakeFeedback() == Feedback::Busy, "overflow is reported");
    const auto commands = TakeCommands();
    Check(commands.size() == 16 && commands[15].value == 15 && commands[0].encounter == 42, "commands keep order and identity");
    Check(TakeCommands().empty(), "draining is one-shot");
    PublishUiRequest({Locale::Chinese, true, true});
    const auto request = ReadUiRequest();
    Check(request.panelOpen && request.showNext && request.locale == Locale::Chinese, "UI flags travel together");
    std::cout << "DnDG native model tests passed\n";
}
