#include "../src/gameplay/transformation_model.hpp"
#include "../src/gameplay/transformation_presets.hpp"
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace transform = gloss::gameplay::transformation;
namespace model = gloss::gameplay::transformation::model;

namespace gloss::gameplay::transformation
{
    const char *ActionName(Action action)
    {
        static constexpr const char *names[]{"Light", "Heavy", "Dodge", "Skill1", "Skill2", "Skill3", "Skill4", "Special"};
        const auto index = static_cast<std::size_t>(action);
        return index < 8 ? names[index] : "Unknown";
    }
}

namespace
{
    int failures{};

#define CHECK(...)                                                                                         \
    do                                                                                                     \
    {                                                                                                      \
        if (!(__VA_ARGS__))                                                                                \
        {                                                                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n";              \
            ++failures;                                                                                   \
        }                                                                                                  \
    } while (false)

    using transform::CharacterEntry;
    using transform::Phase;
    using transform::SkillEntry;
    using transform::SkillSlot;
    using transform::Support;
    using transform::model::Progress;

    model::ContextKey MakeContext(std::int32_t playerSerial = 1, std::uint64_t generation = 7)
    {
        return {{10, playerSerial}, {20, 2}, {30, 3}, generation};
    }

    CharacterEntry MakeEntry(std::string id = "bp'/game/role/foo.foo'")
    {
        CharacterEntry entry;
        entry.id = std::move(id);
        entry.path = "BP'/Game/Role/Foo.Foo'";
        entry.name = "Foo";
        entry.support = Support::Unverified;
        return entry;
    }

    void TestCatalogDeduplicatesByPathAndFormWithoutPromotingSupport()
    {
        std::vector<CharacterEntry> entries;
        auto base = MakeEntry();
        base.support = Support::Experimental;
        base.discovered = true;
        base.resourceId = 11;
        model::MergeCatalog(entries, base);

        auto duplicate = MakeEntry("ignored");
        duplicate.path = "bp'/GAME/ROLE/FOO.FOO'";
        duplicate.name = "";
        duplicate.discovered = true;
        duplicate.resourceId = 12;
        duplicate.support = Support::Verified;
        model::MergeCatalog(entries, duplicate);
        CHECK(entries.size() == 1);
        CHECK(entries.front().id == "/game/role/foo.foo#");
        CHECK(entries.front().name == "Foo");
        CHECK(entries.front().resourceId == 12);
        CHECK(entries.front().support == Support::Experimental);

        auto alternateForm = MakeEntry("ignored");
        alternateForm.form = "phase-2";
        model::MergeCatalog(entries, alternateForm);
        CHECK(entries.size() == 2);
        CHECK(entries.back().id == "/game/role/foo.foo#phase-2");
    }

    void TestSkillSlotsRequireAvailableStableSkillIds()
    {
        const std::vector<SkillEntry> skills{
            {41001, "Attack A", true, {}},
            {41002, "Attack B", true, {}},
            {41003, "Unavailable", false, "not initialized"}};
        const std::vector<SkillSlot> valid{{"Combo", transform::Action::Skill1, {41001, 41002}}};
        CHECK(model::ValidSlots(valid, skills));

        const std::vector<SkillSlot> badId{{"Bad ID", transform::Action::Skill1, {1}}};
        const std::vector<SkillSlot> unavailable{{"Unavailable", transform::Action::Skill1, {41003}}};
        const std::vector<SkillSlot> duplicateAction{
            {"One", transform::Action::Skill1, {41001}},
            {"Two", transform::Action::Skill1, {41002}}};
        CHECK(!model::ValidSlots(badId, skills));
        CHECK(!model::ValidSlots(unavailable, skills));
        CHECK(!model::ValidSlots(duplicateAction, skills));
    }

    void TestStarterPresetsResolveHintsToCurrentSkillIdsAndLeaveMissingSlotsEmpty()
    {
        const std::string yangjianPath = "BP'/Game/00Main/Design/Units/MGD/TAMER_mgd_yangjian_01.TAMER_mgd_yangjian_01_C'";
        std::vector<SkillEntry> full;
        for (int i = 1; i <= 96; ++i)
            full.push_back({50000 + i, "Skill", true, {}});

        const auto yangjian = model::StarterSlots(yangjianPath, full);
        CHECK(yangjian.size() == 8);
        CHECK(yangjian[0].skills == std::vector<int>{50008}); // Reference position 8 resolves to this runtime ID.
        CHECK(yangjian[2].skills == std::vector<int>{50096}); // Last reference position is included.
        CHECK(yangjian[0].name.find("参考：丝滑五连") != std::string::npos);

        const std::vector<SkillEntry> shortList{
            {61001, "First", true, {}}, {61002, "Second", true, {}}, {61003, "Third", true, {}},
            {61004, "Fourth", true, {}}, {61005, "Fifth", true, {}}, {61006, "Sixth", true, {}},
            {61007, "Seventh", true, {}}, {61008, "Eighth", true, {}}};
        const auto shortPreset = model::StarterSlots(yangjianPath, shortList);
        CHECK(shortPreset[0].skills == std::vector<int>{61008});
        CHECK(shortPreset[1].skills.empty());
        CHECK(shortPreset[2].skills.empty());
        CHECK(shortPreset[3].skills == std::vector<int>{61001});
        CHECK(shortPreset[4].skills == std::vector<int>{61002});
        for (std::size_t i = 5; i < shortPreset.size(); ++i)
            CHECK(shortPreset[i].skills.empty());

        auto unavailable = full;
        unavailable[95].available = false;
        const auto unavailablePreset = model::StarterSlots(yangjianPath, unavailable);
        CHECK(unavailablePreset[2].skills.empty());

        const auto dasheng = model::StarterSlots("/Game/00Main/Design/Units/MGD/TAMER_mgd_jsds.TAMER_mgd_jsds_C", full);
        CHECK(dasheng.size() == 8);
        CHECK(dasheng[0].skills == std::vector<int>{50001});
        CHECK(dasheng[1].skills == std::vector<int>{50033});

        const auto nonAdapter = model::StarterSlots("/Game/00Main/Design/Units/GYCY/TAMER_gycy_lang_01.TAMER_gycy_lang_01_C", full);
        CHECK(nonAdapter.size() == 8);
        CHECK(std::all_of(nonAdapter.begin(), nonAdapter.end(), [](const auto &slot) { return slot.skills.empty(); }));
    }

    void TestComboAdvancesThroughFinalSkillAndRejectsDuplicates()
    {
        model::Combo combo;
        const std::array<int, 3> skills{41001, 41002, 41003};
        CHECK(combo.Queue(skills, 100));
        CHECK(!combo.Queue(skills, 101));
        CHECK(combo.Next(0, 100) == 41001);

        combo.Result(true, 100);
        CHECK(!combo.Next(0, 101)); // A queued request does not advance before casting is observed.
        CHECK(combo.Next(41001, 102) == std::nullopt);
        CHECK(combo.Next(0, 103) == 41002);
        CHECK(combo.Position() == 1);

        combo.Result(true, 103);
        CHECK(combo.Next(41002, 104) == std::nullopt);
        CHECK(combo.Next(0, 105) == 41003);
        CHECK(combo.Position() == 2);

        combo.Result(true, 105);
        CHECK(combo.Next(41003, 106) == std::nullopt);
        CHECK(combo.Next(0, 107) == std::nullopt);
        CHECK(!combo.Busy());
        CHECK(!combo.Failed());
        CHECK(combo.Position() == 3);
    }

    void TestComboRejectsAndTimesOutUnobservedRequests()
    {
        model::Combo rejected;
        const std::array<int, 2> skills{41001, 41002};
        CHECK(rejected.Queue(skills, 100));
        CHECK(rejected.Next(0, 100) == 41001);
        rejected.Result(false, 100);
        CHECK(rejected.Failed());
        CHECK(!rejected.Busy());
        CHECK(!rejected.Next(0, 101));

        model::Combo noCastObserved;
        CHECK(noCastObserved.Queue(skills, 100));
        CHECK(noCastObserved.Next(0, 100) == 41001);
        noCastObserved.Result(true, 100);
        CHECK(!noCastObserved.Next(0, 30100));
        CHECK(noCastObserved.Failed());
        CHECK(!noCastObserved.Busy());
    }

    struct MockBackend final : model::SessionBackend
    {
        Progress load{Progress::Complete};
        Progress initialize{Progress::Complete};
        Progress activate{Progress::Complete};
        Progress update{Progress::Complete};
        std::vector<Progress> restores{Progress::Complete};
        int loads{}, initializations{}, activations{}, updates{}, restoreCalls{};
        std::vector<std::string> events;

        Progress Load(const CharacterEntry &) override
        {
            ++loads;
            events.emplace_back("load");
            return load;
        }
        Progress Initialize(const CharacterEntry &) override
        {
            ++initializations;
            events.emplace_back("initialize");
            return initialize;
        }
        Progress Activate() override
        {
            ++activations;
            events.emplace_back("activate");
            return activate;
        }
        Progress Update() override
        {
            ++updates;
            events.emplace_back("update");
            return update;
        }
        Progress Restore() override
        {
            ++restoreCalls;
            events.emplace_back("restore");
            const auto index = static_cast<std::size_t>(restoreCalls - 1);
            return index < restores.size() ? restores[index] : restores.back();
        }
    };

    void TestSessionHappyPathDeathAndIdempotentRestoreRetry()
    {
        model::Session session;
        MockBackend backend;
        const auto context = MakeContext();
        CHECK(session.Begin(MakeEntry(), context, 100));
        CHECK(!session.Begin(MakeEntry("another"), context, 101));
        CHECK(session.State() == Phase::Loading);
        CHECK(session.Exclusive());

        session.Tick(backend, context, 101, false);
        CHECK(backend.loads == 0);
        CHECK(session.State() == Phase::Loading);
        session.Tick(backend, context, 102);
        CHECK(session.State() == Phase::Initializing);
        session.Tick(backend, context, 103);
        CHECK(session.State() == Phase::Active);
        CHECK(backend.initializations == 1);
        CHECK(backend.activations == 1);
        session.Tick(backend, context, 104);
        CHECK(backend.updates == 1);

        backend.update = Progress::Failed; // Runtime death/invalid actor funnels into restoration.
        backend.restores = {Progress::Failed, Progress::Pending, Progress::Complete};
        session.Tick(backend, context, 105);
        CHECK(session.State() == Phase::Restoring);
        session.Tick(backend, context, 106, false);
        CHECK(session.State() == Phase::RecoveryRequired);
        CHECK(session.Exclusive());
        CHECK(session.Character().id == "bp'/game/role/foo.foo'");
        session.Tick(backend, context, 107, false);
        CHECK(session.State() == Phase::RecoveryRequired);
        CHECK(session.Exclusive());
        session.Tick(backend, context, 108, false);
        CHECK(session.State() == Phase::Idle);
        CHECK(!session.Exclusive());
        CHECK(session.Character().id.empty());
        CHECK(!session.Context().Valid());
        CHECK(backend.restoreCalls == 3);
        CHECK(session.Begin(MakeEntry(), context, 109));
        session.Tick(backend, context, 110);
        session.Tick(backend, context, 111);
        CHECK(session.State() == Phase::Active);
        CHECK(backend.initializations == 2);
        CHECK(backend.activations == 2);
    }

    void TestSessionRollsBackLoadInitializeAndActivationFailures()
    {
        {
            model::Session session;
            MockBackend backend;
            backend.load = Progress::Failed;
            CHECK(session.Begin(MakeEntry(), MakeContext(), 1));
            session.Tick(backend, MakeContext(), 2);
            CHECK(session.State() == Phase::Restoring);
            CHECK(backend.initializations == 0);
            session.Tick(backend, MakeContext(), 3);
            CHECK(session.State() == Phase::Idle);
            CHECK(backend.restoreCalls == 1);
        }
        {
            model::Session session;
            MockBackend backend;
            backend.initialize = Progress::Failed;
            const auto context = MakeContext();
            CHECK(session.Begin(MakeEntry(), context, 1));
            session.Tick(backend, context, 2);
            session.Tick(backend, context, 3);
            CHECK(session.State() == Phase::Restoring);
            CHECK(backend.activations == 0);
            session.Tick(backend, context, 4);
            CHECK(session.State() == Phase::Idle);
        }
        {
            model::Session session;
            MockBackend backend;
            backend.activate = Progress::Failed;
            const auto context = MakeContext();
            CHECK(session.Begin(MakeEntry(), context, 1));
            session.Tick(backend, context, 2);
            session.Tick(backend, context, 3);
            CHECK(session.State() == Phase::Restoring);
            CHECK(backend.initializations == 1);
            session.Tick(backend, context, 4);
            CHECK(session.State() == Phase::Idle);
        }
    }

    void TestSessionRetriesPendingActivationWithoutReinitializing()
    {
        model::Session session;
        MockBackend backend;
        backend.activate = Progress::Pending;
        const auto context = MakeContext();
        CHECK(session.Begin(MakeEntry(), context, 1));
        session.Tick(backend, context, 2); // Loading complete.
        session.Tick(backend, context, 3); // Initialize complete; Activate pending.
        CHECK(session.State() == Phase::Initializing);
        CHECK(backend.initializations == 1);
        backend.activate = Progress::Complete;
        session.Tick(backend, context, 4);
        CHECK(session.State() == Phase::Active);
        CHECK(backend.initializations == 1);
        CHECK(backend.activations == 2);
    }

    void TestSessionTimeoutAndContextGenerationOrSerialChangesRestore()
    {
        {
            model::Session session;
            MockBackend backend;
            const auto context = MakeContext();
            CHECK(session.Begin(MakeEntry(), context, 100));
            session.Tick(backend, context, 15099);
            CHECK(session.State() == Phase::Initializing);
            session.Tick(backend, context, 15100);
            CHECK(session.State() == Phase::Idle);
            CHECK(backend.loads == 1);
            CHECK(backend.initializations == 0);
            CHECK(backend.restoreCalls == 1);
        }
        {
            model::Session session;
            MockBackend backend;
            backend.load = Progress::Pending;
            const auto context = MakeContext();
            CHECK(session.Begin(MakeEntry(), context, 100));
            session.Tick(backend, context, 15099);
            CHECK(session.State() == Phase::Loading);
            session.Tick(backend, context, 15100);
            CHECK(session.State() == Phase::Idle);
            CHECK(backend.loads == 1); // The expired phase restores before another Load attempt.
            CHECK(backend.initializations == 0);
            CHECK(backend.restoreCalls == 1);
        }
        const std::array<model::ContextKey, 3> changedContexts{
            MakeContext(2),                        // Same object index, different serial.
            MakeContext(1, 8),                    // New game/session generation.
            {{10, 1}, {20, 2}, {31, 3}, 7}};       // Different world.
        for (const auto &changed : changedContexts)
        {
            model::Session session;
            MockBackend backend;
            const auto context = MakeContext();
            CHECK(session.Begin(MakeEntry(), context, 100));
            session.Tick(backend, changed, 101, false);
            CHECK(session.State() == Phase::Idle);
            CHECK(backend.loads == 0);
            CHECK(backend.restoreCalls == 1);
        }
    }

    void TestSessionManualRestoreCancelsBeforeFurtherLoading()
    {
        model::Session session;
        MockBackend backend;
        const auto context = MakeContext();
        CHECK(session.Begin(MakeEntry(), context, 1));
        session.RequestRestore();
        CHECK(!session.Begin(MakeEntry("queued"), context, 2));
        session.Tick(backend, context, 3, false);
        CHECK(session.State() == Phase::Idle);
        CHECK(backend.loads == 0);
        CHECK(backend.restoreCalls == 1);
    }
}

int main()
{
    TestCatalogDeduplicatesByPathAndFormWithoutPromotingSupport();
    TestSkillSlotsRequireAvailableStableSkillIds();
    TestStarterPresetsResolveHintsToCurrentSkillIdsAndLeaveMissingSlotsEmpty();
    TestComboAdvancesThroughFinalSkillAndRejectsDuplicates();
    TestComboRejectsAndTimesOutUnobservedRequests();
    TestSessionHappyPathDeathAndIdempotentRestoreRetry();
    TestSessionRollsBackLoadInitializeAndActivationFailures();
    TestSessionRetriesPendingActivationWithoutReinitializing();
    TestSessionTimeoutAndContextGenerationOrSerialChangesRestore();
    TestSessionManualRestoreCancelsBeforeFurtherLoading();
    if (failures)
    {
        std::cerr << failures << " transformation assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer transformation tests passed (0 failures)\n";
    return 0;
}
