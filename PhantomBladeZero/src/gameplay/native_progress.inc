// Persistent progress actions use the game's own setter and getter. They are
// dispatched once on the verified game thread and never replayed as toggles.
        Address ExtendedWorldSubsystem(const Context& context, std::string_view className);
        bool ProgressFeature(std::string_view id) { return id == "unlock_66_days"; }
        Address ProgressSystem(const Context& context) {
            const auto object = ExtendedWorldSubsystem(context, "PBZGameSystem");
            const auto type = Read<Address>(object + 0x10);
            if (!LocalGameThread() || !ObjectValid(object) || Read<Address>(object + 0x20) != context.world ||
                !PropertyLayout(FindProperty(type, "bCanSelectNightMare"), "BoolProperty", 1, 0x294) ||
                Read<std::uint32_t>(FindProperty(type, "bCanSelectNightMare") + 0x70) != 0xff010001 ||
                !CombatSignature(FindFunction(type, "MarkSelectNightMare"), "MarkSelectNightMare", 0x65eef60, 0x04020401, 0, {}) ||
                !CombatSignature(FindFunction(type, "CanSelectNightMare"), "CanSelectNightMare", 0x65ed7e0, 0x54020401, 1, {
                    {"ReturnValue", "BoolProperty", 0, 1, 0x18001040000780ull, ""}})) return 0;
            return object;
        }
        bool ProgressCapability(const Context& context, std::string_view id, std::string& reason) {
            if (!ProgressFeature(id)) return false;
            const bool ready = ProgressSystem(context) != 0;
            reason = ready ? "" : "噩梦难度解锁与查询接口尚未通过核对";
            return ready;
        }
        ExecutionResult ExecuteProgress(const Context& context, const Request& request) {
            if (request.arguments.size() != 1 || !ProgressFeature(request.arguments[0]))
                return {RequestState::Failed, "难度解锁参数无效"};
            const auto object = ProgressSystem(context);
            if (!object) return {RequestState::Failed, "噩梦难度解锁与查询接口尚未通过核对"};
            const auto index = Read<std::uint32_t>(object + 0xc);
            const auto type = Read<Address>(object + 0x10);
            std::uint8_t unlocked{2};
            if (!CallProcessEvent(object, FindFunction(type, "CanSelectNightMare"), &unlocked) || unlocked > 1)
                return {RequestState::Failed, "无法读取当前难度解锁状态"};
            if (unlocked) return {RequestState::Verified, "六十六天难度已经解锁；当前难度保持原值"};
            const auto activeDifficulty = Read<std::int32_t>(object + 0x298);
            if (!CallProcessEvent(object, FindFunction(type, "MarkSelectNightMare"), nullptr))
                return {RequestState::Indeterminate, "难度解锁调用异常，未重复提交"};
            ++dispatchedCalls;
            const auto after = CurrentContext();
            unlocked = 2;
            if (!after || after->Key() != context.Key() || ProgressSystem(*after) != object ||
                Read<std::uint32_t>(object + 0xc) != index ||
                !CallProcessEvent(object, FindFunction(type, "CanSelectNightMare"), &unlocked) || unlocked != 1 ||
                Read<std::int32_t>(object + 0x298) != activeDifficulty)
                return {RequestState::Indeterminate, "难度解锁调用已返回，但查询未匹配，未重复提交"};
            return {RequestState::Verified, "六十六天难度解锁已由游戏接口回读；由游戏正常保存，当前难度保持原值"};
        }
