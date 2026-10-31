// Refills medicine the way the game's own restore path does: current and display
// uses become GetMedicineTotalMaxUsage (MaxTime + MedicineExtraUsage). MaxTime stays
// the base value, so ability bonuses and later upgrades are counted automatically.
        constexpr std::int32_t MedicineRowDefaultOffset = 0x150; // Read by PushMedicineToInventory as the initial MaxTime.
        constexpr std::int32_t MedicineRowUseTypeOffset = 0x159; // UseMedicineType, passed along with the count.
        constexpr std::int32_t MedicineCurrentOffset = 0x50, MedicineDisplayOffset = 0x54, MedicineMaximumOffset = 0x58;
        constexpr std::int32_t MedicineTopSlotOffset = 0x618, MedicineBottomSlotOffset = 0x610;
        constexpr std::int32_t MedicineRestoreDelegateOffset = 0x2b8;
        constexpr Address MedicineBroadcastRva = 0x65d0100;
        struct MedicineParameter { std::string_view type; std::int32_t size, offset; };
        struct MedicineEntry { NameValue id{}; Address info{}; };

        // FNames compare without case and keep the first registered spelling, so a
        // shared name such as "CurrentTime" may read back as "currentTime".
        Address MedicineProperty(Address type, std::string_view name) {
            for (unsigned depth = 0; type && depth < 64; ++depth) {
                auto property = Read<Address>(type + 0x50);
                for (unsigned index = 0; property && index < 2048; ++index) {
                    const auto* text = NameText(Read<std::uint32_t>(property + 0x20));
                    if (text && ItemSameSpelling(*text, name)) return property;
                    const auto next = Read<Address>(property + 0x10);
                    if (next == property) return 0;
                    property = next;
                }
                type = Read<Address>(type + 0x40);
            }
            return 0;
        }
        Address MedicineFunctionNamed(Address owner, std::string_view name) {
            for (unsigned depth = 0; owner && depth < 64; ++depth) {
                auto function = Read<Address>(owner + 0x48);
                for (unsigned index = 0; function && index < 2048; ++index) {
                    if (ItemSameSpelling(ObjectName(function), name)) return function;
                    const auto next = Read<Address>(function + 0x28);
                    if (next == function) return 0;
                    function = next;
                }
                owner = Read<Address>(owner + 0x40);
            }
            return 0;
        }
        std::string MedicineFunctionProblem(Address owner, std::string_view name, Address rva, std::uint32_t flags, int size,
            std::initializer_list<MedicineParameter> expected) {
            const auto function = MedicineFunctionNamed(owner, name);
            if (!ObjectValid(function)) return std::format("{} missing", name);
            const auto native = Read<Address>(function + 0x128) - base;
            const auto actualFlags = Read<std::uint32_t>(function + 0x100);
            const auto actualSize = Read<std::uint16_t>(function + 0x106);
            const auto actualCount = Read<std::uint8_t>(function + 0x104);
            if (native != rva || actualFlags != flags || actualSize != size || actualCount != expected.size())
                return std::format("{} native={:x} flags={:x} size={} count={}", name, native, actualFlags, actualSize, actualCount);
            auto field = Read<Address>(function + 0x50);
            for (std::size_t visited = 0; visited < expected.size(); ++visited) {
                if (!std::any_of(expected.begin(), expected.end(), [&](const auto& parameter) {
                    return PropertyLayout(field, parameter.type, parameter.size, parameter.offset);
                })) return std::format("{} parameter {} size={} offset={}", name, visited,
                    Read<std::int32_t>(field + 0x34), Read<std::int32_t>(field + 0x44));
                field = Read<Address>(field + 0x10);
            }
            return field ? std::format("{} has extra parameters", name) : std::string{};
        }
        std::string MedicinePropertyProblem(Address type, std::string_view name, std::string_view kind, std::int32_t size, std::int32_t offset) {
            const auto property = MedicineProperty(type, name);
            if (!property) return std::format("{} missing", name);
            return PropertyLayout(property, kind, size, offset) ? std::string{} :
                std::format("{} size={} offset={}", name, Read<std::int32_t>(property + 0x34), Read<std::int32_t>(property + 0x44));
        }
        // Empty when the backpack matches this build, otherwise the first mismatch.
        std::string MedicineContractProblem(Address backpack) {
            if (!ObjectValid(backpack)) return "backpack invalid";
            const auto type = Read<Address>(backpack + 0x10);
            auto problem = MedicinePropertyProblem(type, "MedicineInfos", "MapProperty", 80, 0x620);
            if (problem.empty()) problem = MedicinePropertyProblem(type, "BottomMedicineID", "NameProperty", 8, 0x610);
            if (problem.empty()) problem = MedicinePropertyProblem(type, "TopMedicineID", "NameProperty", 8, 0x618);
            if (!problem.empty()) return problem;
            const auto infoClass = Read<Address>(Read<Address>(MedicineProperty(type, "MedicineInfos") + 0x78) + 0x70);
            if (!ObjectValid(infoClass) || !ItemSameSpelling(ObjectName(infoClass), "PBZMedicineInfo"))
                return std::format("medicine info class is {}", ObjectName(infoClass));
            problem = MedicinePropertyProblem(infoClass, "CurrentTime", "IntProperty", 4, MedicineCurrentOffset);
            if (problem.empty()) problem = MedicinePropertyProblem(infoClass, "DisplayTime", "IntProperty", 4, MedicineDisplayOffset);
            if (problem.empty()) problem = MedicinePropertyProblem(infoClass, "MaxTime", "IntProperty", 4, MedicineMaximumOffset);
            if (problem.empty()) problem = MedicineFunctionProblem(type, "GetMedicineTotalMaxUsage", 0x65d50e0, 0x54020401, 12,
                {{"NameProperty", 8, 0}, {"IntProperty", 4, 8}});
            if (problem.empty()) problem = MedicineFunctionProblem(type, "UseMedicine", 0x65dac70, 0x04020401, 12,
                {{"NameProperty", 8, 0}, {"IntProperty", 4, 8}});
            if (problem.empty()) problem = MedicinePropertyProblem(type, "RestoreMedicineDelegate", "MulticastInlineDelegateProperty", 16, MedicineRestoreDelegateOffset);
            constexpr std::array<unsigned char, 13> BroadcastEntry{0x48,0x83,0xec,0x48,0x8b,0x44,0x24,0x70,0x48,0x89,0x54,0x24,0x20};
            std::array<unsigned char, BroadcastEntry.size()> entry{};
            if (problem.empty() && (!ReadBytes(base + MedicineBroadcastRva, entry.data(), entry.size()) || entry != BroadcastEntry))
                problem = "medicine broadcast entry changed";
            return problem;
        }
        bool ReadMedicineEntries(Address backpack, std::vector<MedicineEntry>& entries) {
            const auto infoClass = Read<Address>(Read<Address>(MedicineProperty(Read<Address>(backpack + 0x10), "MedicineInfos") + 0x78) + 0x70);
            const auto map = backpack + 0x620;
            ItemSparseMap layout;
            if (!ItemReadMap(map, layout)) return false;
            for (std::int32_t slot = 0; slot < layout.count; ++slot) {
                bool allocated{};
                if (!ItemSlotAllocated(map, layout, slot, allocated)) return false;
                if (!allocated) continue;
                ItemMapEntry entry;
                if (!ItemReadMapEntry(layout, slot, entry) || !ObjectValid(entry.value) ||
                    !IsClass(Read<Address>(entry.value + 0x10), infoClass)) return false;
                entries.push_back({entry.id, entry.value});
            }
            ItemSparseMap after;
            return ItemReadMap(map, after) && ItemSameMap(layout, after);
        }
        Address MedicineRow(const NameValue& id) {
            const auto& table = ItemTables[static_cast<std::size_t>(ItemKind::Medicine)];
            const auto map = base + table.tableRva + 8;
            ItemSparseMap layout;
            if (!ItemTableValid(table, layout)) return 0;
            for (std::int32_t slot = 0; slot < layout.count; ++slot) {
                bool allocated{};
                ItemMapEntry entry;
                if (!ItemSlotAllocated(map, layout, slot, allocated)) return 0;
                if (allocated && ItemReadMapEntry(layout, slot, entry) && ItemSameName(entry.id, id)) return entry.value;
            }
            return 0;
        }
        std::optional<std::int32_t> MedicineRowDefault(const NameValue& id) {
            const auto row = MedicineRow(id);
            std::int32_t uses{};
            if (!row || !ReadBytes(row + MedicineRowDefaultOffset, &uses, sizeof(uses))) return {};
            return uses;
        }
        // Tells the HUD the way the game's rest path does; the gourd redraws from this.
        bool BroadcastMedicineRestored(Address backpack, NameValue id, std::int32_t count, std::uint8_t useType, bool topSlot) noexcept {
            using Broadcast = void(__fastcall*)(Address, NameValue, std::int32_t, std::uint8_t, std::int32_t);
            __try {
                reinterpret_cast<Broadcast>(base + MedicineBroadcastRva)(backpack + MedicineRestoreDelegateOffset, id, count, useType, topSlot ? 1 : 0);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool WriteMedicineCount(Address address, std::int32_t value) noexcept {
            __try { *reinterpret_cast<std::int32_t*>(address) = value; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        // UseMedicine with zero clamps current and display to the total and tells the HUD.
        bool MedicineClamp(Address backpack, const NameValue& id) {
            struct Parameters { NameValue id; std::int32_t count; } parameters{id, 0};
            const auto called = CallProcessEvent(backpack, MedicineFunctionNamed(Read<Address>(backpack + 0x10), "UseMedicine"), &parameters);
            if (called) ++dispatchedCalls;
            return called;
        }
        ExecutionResult MedicineCallFault() {
            faulted.store(true);
            GetBackend().SetRuntimeState(false, "药品调用异常，适配器停止");
            return {RequestState::Indeterminate, "药品调用异常，结果无法确认"};
        }
        ExecutionResult RefillMedicineChecked(const Context& context) {
            const auto backpack = Backpack(context);
            std::vector<MedicineEntry> entries;
            const auto problem = backpack ? MedicineContractProblem(backpack) : std::string("backpack not found");
            if (!problem.empty()) {
                gloss::Log("PBZ refill medicine contract mismatch: " + problem);
                return {RequestState::Failed, "背包药品数据或函数契约未通过核对，未修改"};
            }
            if (!ReadMedicineEntries(backpack, entries)) {
                gloss::Log("PBZ refill medicine could not read the MedicineInfos map");
                return {RequestState::Failed, "背包药品数据或函数契约未通过核对，未修改"};
            }
            // Check every repair first, so a failure cannot leave some medicine half fixed.
            std::vector<std::pair<MedicineEntry, std::int32_t>> repairs;
            for (const auto& entry : entries) {
                if (!MedicineMaximumInflated(Read<std::int32_t>(entry.info + MedicineMaximumOffset))) continue;
                const auto uses = MedicineRowDefault(entry.id);
                if (!uses || !ValidMedicineDefault(*uses))
                    return {RequestState::Failed, "无法从药品表读取原始使用次数，未修改异常上限"};
                repairs.push_back({entry, *uses});
            }
            for (const auto& [entry, uses] : repairs) {
                if (!WriteMedicineCount(entry.info + MedicineMaximumOffset, uses))
                    return {RequestState::Indeterminate, "药品上限修复写入失败"};
                if (!MedicineClamp(backpack, entry.id)) return MedicineCallFault();
            }
            const auto type = Read<Address>(backpack + 0x10);
            const auto totalFunction = MedicineFunctionNamed(type, "GetMedicineTotalMaxUsage");
            std::string summary;
            NameValue previous{};
            for (const auto offset : {MedicineTopSlotOffset, MedicineBottomSlotOffset}) {
                const auto id = Read<NameValue>(backpack + offset);
                if (!id.comparison || ItemSameName(id, previous)) continue;
                previous = id;
                const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return ItemSameName(entry.id, id); });
                if (found == entries.end()) continue;
                const auto info = found->info;
                struct TotalParameters { NameValue id; std::int32_t result; } total{id, 0};
                if (!CallProcessEvent(backpack, totalFunction, &total)) return MedicineCallFault();
                if (!ValidMedicineTotal(total.result))
                    return {RequestState::Failed, std::format("药品总次数 {} 不在有效范围内，未补充", total.result)};
                if (!WriteMedicineCount(info + MedicineCurrentOffset, total.result) ||
                    !WriteMedicineCount(info + MedicineDisplayOffset, total.result))
                    return {RequestState::Indeterminate, "药品显示次数写入失败"};
                // Also when already full: this is how a stuck gourd gets redrawn.
                const auto row = MedicineRow(id);
                std::uint8_t useType{};
                if (!row || !ReadBytes(row + MedicineRowUseTypeOffset, &useType, sizeof(useType)))
                    gloss::Log("PBZ refill medicine could not read the use type; HUD not notified");
                else if (!BroadcastMedicineRestored(backpack, id, total.result, useType, offset == MedicineTopSlotOffset))
                    return MedicineCallFault();
                else ++dispatchedCalls;
                const auto current = Read<std::int32_t>(info + MedicineCurrentOffset);
                if (current != total.result || Read<std::int32_t>(info + MedicineDisplayOffset) != total.result)
                    return {RequestState::Dispatched, std::format("药品补充已调用，回读 {} / {}，未达到上限", current, total.result)};
                if (!summary.empty()) summary += "，";
                summary += std::format("{} / {}", current, total.result);
            }
            if (summary.empty()) {
                if (repairs.empty()) return {RequestState::Failed, "未装备药品，没有可补充的项目"};
                return {RequestState::Verified, std::format("已修复 {} 项药品的异常上限；未装备药品，无需补充", repairs.size())};
            }
            if (repairs.empty()) return {RequestState::Verified, std::format("已装备药品已补满：{}", summary)};
            return {RequestState::Verified, std::format("已装备药品已补满：{}；已修复 {} 项药品的异常上限", summary, repairs.size())};
        }
        ExecutionResult RefillMedicine(const Context& context) {
            auto result = RefillMedicineChecked(context);
            gloss::Log(std::format("PBZ refill medicine state={}: {}", RequestStateLabel(result.state), result.message));
            return result;
        }
