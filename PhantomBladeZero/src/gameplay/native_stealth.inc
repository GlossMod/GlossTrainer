// Local-player perception suppression through the game's loose gameplay tags.
// Tag read-back proves the applied state; enemy behavior needs separate testing.
        constexpr std::array<std::string_view, 5> StealthTags{
            "AI.Perception.IgnoreSightSense", "AI.Perception.IgnoreHearingSense",
            "AI.Perception.IgnoreTouchSense", "AI.Perception.IgnoreTeamSense", "AI.Hatred.IgnoreByNPC"};
        struct StealthBinding {
            Context context;
            TagTargetIdentity identity;
            std::array<LooseTagContribution, StealthTags.size()> contributions;
            bool enabled{}, uncertain{};
            std::string error;
        } stealthBinding;

        bool StealthFeature(std::string_view id) { return id == "invisible"; }
        TagTargetIdentity StealthIdentity(const Context& context) {
            const auto component = Read<Address>(context.pawn + 0x750);
            return {context.world, context.pawn, component, context.worldIndex,
                Read<std::uint32_t>(context.pawn + 0xc), Read<std::uint32_t>(component + 0xc)};
        }
        TagTargetStatus StealthTargetStatus(const StealthBinding& binding) {
            if (!binding.identity.Valid()) return TagTargetStatus::Expired;
            const auto& identity = binding.identity;
            if (!ObjectValid(identity.world) || Read<std::uint32_t>(identity.world + 0xc) != identity.worldIndex ||
                !ObjectValid(identity.pawn) || Read<std::uint32_t>(identity.pawn + 0xc) != identity.pawnIndex ||
                !ObjectValid(identity.component) || Read<std::uint32_t>(identity.component + 0xc) != identity.componentIndex)
                return TagTargetStatus::Expired;
            if (!LocalGameThread() || stopping.load() || faulted.load() ||
                !PropertyLayout(FindProperty(Read<Address>(identity.pawn + 0x10), "AbilitySystemComponent"), "ObjectProperty", 8, 0x750) ||
                Read<Address>(identity.pawn + 0x750) != identity.component || Read<Address>(identity.component + 0x20) != identity.pawn ||
                !ClassNamed(Read<Address>(identity.component + 0x10), "PBZAbilitySystemComponent")) return TagTargetStatus::Unavailable;
            return TagTargetStatus::Current;
        }
        bool StealthHasContributions() {
            return std::any_of(stealthBinding.contributions.begin(), stealthBinding.contributions.end(),
                [](const auto& contribution) { return contribution.owned; });
        }
        bool StealthCapability(const Context& context, std::string_view id, std::string& reason) {
            reason.clear();
            if (!StealthFeature(id) || !MovementContextCurrent(context) || !PlayerAttributesReady(context) ||
                !PerfectTagFunction(true) || !PerfectTagFunction(false)) {
                reason = "本地玩家能力系统或感知标签 Add/Remove 接口契约未通过";
                return false;
            }
            for (const auto tag : StealthTags) {
                if (!FindExistingName(tag)) { reason = "游戏中尚未找到所需感知忽略标签"; return false; }
                const auto count = ReadGameplayTagCount(context, tag);
                if (!count || *count > 100000) { reason = "感知忽略标签计数未通过实际回读"; return false; }
            }
            return true;
        }
        // A call can return false after dispatch; the ledger is recorded before
        // entering ProcessEvent so indeterminate contributions remain cleanable.
        bool StealthTagCall(const StealthBinding& binding, std::string_view tag, bool add, bool& dispatched) {
            dispatched = false;
            if (StealthTargetStatus(binding) != TagTargetStatus::Current) return false;
            const auto function = PerfectTagFunction(add);
            const auto name = FindExistingName(tag);
            const auto owner = Read<Address>(function + 0x20), cdo = Read<Address>(owner + 0x160);
            if (!function || !name || !ObjectValid(cdo)) return false;
            struct Parameters { Address actor; NameValue tag; std::uint8_t replicate{}, result{}; }
                parameters{binding.identity.pawn, *name};
            if (StealthTargetStatus(binding) != TagTargetStatus::Current) return false;
            dispatched = true;
            const bool called = CallProcessEvent(cdo, function, &parameters);
            ++dispatchedCalls;
            return called && parameters.result != 0;
        }
        bool RemoveStealthContributions() {
            auto& binding = stealthBinding;
            if (!StealthHasContributions()) { binding = {}; return true; }
            const auto target = StealthTargetStatus(binding);
            if (target == TagTargetStatus::Expired) { binding = {}; return true; }
            if (target != TagTargetStatus::Current) {
                binding.enabled = false;
                binding.uncertain = true;
                binding.error = "旧玩家感知标签仍有未确认贡献，但原能力系统暂不可用；未写入新玩家";
                return false;
            }
            bool complete = true;
            for (std::size_t i = StealthTags.size(); i-- > 0;) {
                auto& contribution = binding.contributions[i];
                if (!contribution.owned) continue;
                if (StealthTargetStatus(binding) != TagTargetStatus::Current) { complete = false; break; }
                const auto before = ReadGameplayTagCount(binding.context, StealthTags[i]);
                if (!before || *before > 100001) { complete = false; continue; }
                if (contribution.removalPending) {
                    complete &= contribution.ObserveRemoval(before);
                    continue;
                }
                if (contribution.ObserveNoContribution(*before)) continue;
                if (!contribution.BeginRemove(*before)) { complete = false; continue; }
                bool dispatched{};
                StealthTagCall(binding, StealthTags[i], false, dispatched);
                if (!dispatched) {
                    contribution.CancelUnexecutedRemoval();
                    complete = false;
                    continue;
                }
                if (StealthTargetStatus(binding) != TagTargetStatus::Current) { complete = false; continue; }
                const auto after = ReadGameplayTagCount(binding.context, StealthTags[i]);
                complete &= contribution.ObserveRemoval(after);
            }
            if (complete && !StealthHasContributions()) { binding = {}; return true; }
            binding.enabled = false;
            binding.uncertain = true;
            binding.error = "部分感知标签贡献未能确认移除；不会重复减计数或写入新玩家";
            return false;
        }
        bool StealthTagsMatch(const Context& context) {
            const auto& binding = stealthBinding;
            if (!binding.enabled || binding.uncertain || !MovementContextCurrent(context) ||
                binding.identity != StealthIdentity(context) || StealthTargetStatus(binding) != TagTargetStatus::Current) return false;
            for (std::size_t i = 0; i < StealthTags.size(); ++i) {
                const auto& contribution = binding.contributions[i];
                if (!contribution.owned || !contribution.confirmed || contribution.removalPending) return false;
                const auto count = ReadGameplayTagCount(binding.context, StealthTags[i]);
                if (!count || *count < contribution.baseline + 1) return false;
            }
            return true;
        }
        ExecutionResult ExecuteStealth(const Context& context, const Request& request) {
            if (request.arguments.size() != 2 || request.arguments[0] != "invisible" ||
                (request.arguments[1] != "on" && request.arguments[1] != "off"))
                return {RequestState::Failed, "隐身开关参数无效"};
            if (request.arguments[1] == "off") {
                const bool restored = RemoveStealthContributions();
                return {restored ? RequestState::Verified : RequestState::Indeterminate,
                    restored ? "已移除本修改器添加的感知标签贡献，保留游戏原有标签；敌人感知需实测" : stealthBinding.error};
            }
            std::string reason;
            if (!StealthCapability(context, "invisible", reason)) return {RequestState::Failed, reason};
            if (StealthTagsMatch(context))
                return {RequestState::Verified, "感知忽略标签仍在游戏能力系统中；敌人实战效果需另行验证"};
            if (StealthHasContributions() && !RemoveStealthContributions())
                return {RequestState::Indeterminate, "前次感知标签贡献尚未清理，未重复添加"};
            auto& binding = stealthBinding;
            binding = {};
            binding.context = context;
            binding.identity = StealthIdentity(context);
            if (StealthTargetStatus(binding) != TagTargetStatus::Current)
                return {RequestState::Failed, "玩家能力系统身份已变化，未添加感知标签"};
            for (std::size_t i = 0; i < StealthTags.size(); ++i) {
                auto& contribution = binding.contributions[i];
                const auto before = ReadGameplayTagCount(binding.context, StealthTags[i]);
                if (!before || !contribution.BeginAdd(*before)) {
                    const bool restored = RemoveStealthContributions();
                    return {restored ? RequestState::Failed : RequestState::Indeterminate, "感知标签基础计数无法确认，已回滚本次新增贡献"};
                }
                bool dispatched{};
                const bool called = StealthTagCall(binding, StealthTags[i], true, dispatched);
                if (!dispatched) contribution.Clear();
                const auto after = dispatched && StealthTargetStatus(binding) == TagTargetStatus::Current ?
                    ReadGameplayTagCount(binding.context, StealthTags[i]) : std::nullopt;
                const bool matched = contribution.ObserveAdd(after);
                if (!called || !matched) {
                    const bool restored = RemoveStealthContributions();
                    return {restored ? RequestState::Failed : RequestState::Indeterminate,
                        restored ? "感知标签新增未全部通过回读，已回滚本次新增贡献" : "感知标签部分新增未通过回读，回滚尚未确认；不会自动重试"};
                }
            }
            binding.enabled = true;
            binding.uncertain = false;
            binding.error.clear();
            return {RequestState::Verified, "五项感知忽略标签各新增一份并已回读；敌人识别和仇恨效果需实战验证"};
        }
        std::vector<ToggleState> StealthToggleStates(const Context& context) {
            std::string reason;
            bool known = StealthCapability(context, "invisible", reason);
            const bool enabled = stealthBinding.enabled;
            if (known && (enabled || stealthBinding.uncertain || StealthHasContributions())) {
                known = StealthTagsMatch(context);
                if (!known) reason = stealthBinding.error.empty() ? "感知忽略标签未全部回读，或玩家能力系统身份已变化" : stealthBinding.error;
            }
            if (known) reason = enabled ? "游戏感知忽略标签已回读；敌人实战效果仍需验证" : "本修改器未添加感知忽略标签";
            return {{"invisible", known, known && enabled, std::move(reason)}};
        }
        bool StealthSessionChanged(const Context&) { return RemoveStealthContributions(); }
        bool StealthCleanup() {
            if (!LocalGameThread()) return false;
            return RemoveStealthContributions();
        }
