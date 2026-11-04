// Exact-build item tables and inventory calls. Included inside native_adapter's
// anonymous namespace; all state and all engine calls belong to the game thread.
        enum class ItemKind : std::size_t { Prop, Medicine, Accessory, MainWeapon, ShaqiWeapon };
        struct ItemTableContract {
            ItemKind kind;
            Address tableRva;
            std::string_view path, prefix, category, getter, rowType, nameProperty;
            std::int32_t rowSize, nameOffset, expectedSlots;
            std::string_view inventoryProperty, valueClass;
            std::int32_t inventoryOffset;
            ExpectedFunction grant;
            std::uint32_t grantFlags;
        };
        constexpr ItemTableContract ItemTables[] = {
            {ItemKind::Prop, 0xe091940, "/Game/PBZContent/DataTable/Item/DT_InventoryData", "prop", "道具",
                "GetPropData", "PBZItemDataStruct", "ItemName", 856, 0x28, 640,
                "Inventories", "PBZInventoryInfo", 0x510,
                {"AddPropsToBackPack", 0x6619b60, 13, 3, {{{"Num", ParamType::Int, 0},
                    {"ItemId", ParamType::Name, 4}, {"bPushWidget", ParamType::Bool, 12}}}}, 0x04040401},
            {ItemKind::Medicine, 0xe0978b0, "/Game/PBZContent/DataTable/UI/DT_MedicineTable", "medicine", "药品",
                "GetMedicineData", "PBZMedicineDataStruct", "Name", 688, 0x28, 17,
                "MedicineInfos", "PBZMedicineInfo", 0x620,
                {"PushMedicineToInventory", 0x6621970, 10, 3, {{{"MedicineID", ParamType::Name, 0},
                    {"bAutoEquip", ParamType::Bool, 8}, {"bShowPickupUI", ParamType::Bool, 9}}}}, 0x04020401},
            {ItemKind::Accessory, 0xe097940, "/Game/PBZContent/DataTable/UI/DT_Accessory", "accessory", "饰品",
                "GetAccessoryData", "AccessoryData", "AccessoryName", 536, 0x28, 73,
                "Accessories", "PBZAccessoryInfo", 0x570,
                {"PushAccessoryToInventory", 0x66216e0, 9, 2, {{{"AccessoryID", ParamType::Name, 0},
                    {"bAutoEquip", ParamType::Bool, 8}}}}, 0x04020401},
            {ItemKind::MainWeapon, 0xe097b80, "/Game/PBZContent/DataTable/UI/DT_WeaponData", "weapon", "主武器",
                "GetMainWeaponTableData", "WeaponData", "WeaponName", 1880, 0x28, 33,
                "MainWeaponInfos", "MainWeaponInfo", 0x718,
                {"AddWeaponItem", 0x6619fc0, 12, 5, {{{"WeaponID", ParamType::Name, 0},
                    {"bIsNormalWeapon", ParamType::Bool, 8}, {"bPushWidget", ParamType::Bool, 9},
                    {"bIsAddByLoadData", ParamType::Bool, 10}, {"bAutoEquipment", ParamType::Bool, 11}}}}, 0x04020401},
            {ItemKind::ShaqiWeapon, 0xe097a60, "/Game/PBZContent/DataTable/UI/DT_ShaqiWeaponData", "shaqi", "煞兵",
                "GetShaqiWeaponTableData", "ShaqiWeaponData", "WeaponName", 2096, 0x30, 28,
                "ShaqiWeaponInfos", "ShaqiWeaponInfo", 0x780,
                {"AddWeaponItem", 0x6619fc0, 12, 5, {{{"WeaponID", ParamType::Name, 0},
                    {"bIsNormalWeapon", ParamType::Bool, 8}, {"bPushWidget", ParamType::Bool, 9},
                    {"bIsAddByLoadData", ParamType::Bool, 10}, {"bAutoEquipment", ParamType::Bool, 11}}}}, 0x04020401}
        };
        struct ItemGetterContract { Address nativeRva; std::uint32_t flags; std::uint16_t size; };
        constexpr ItemGetterContract ItemGetters[] = {
            {0x661e150, 0x14022401, 864}, {0x661db30, 0x14022401, 696},
            {0x661b440, 0x04022401, 544}, {0x661d6d0, 0x04020401, 1888}, {0x661e840, 0x04020401, 2104}
        };
        constexpr std::array<unsigned char, 15> ItemResolveEntry{
            0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x30};
        constexpr std::array<unsigned char, 14> ItemToStringEntry{
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x92, 0xc1, 0x00, 0x00};
        struct ItemSparseMap {
            Address data{};
            std::int32_t count{}, capacity{};
            std::array<std::uint32_t, 4> inlineBits{};
            Address heapBits{};
            std::int32_t numBits{}, maxBits{};
        };
        static_assert(sizeof(ItemSparseMap) == 48);
        struct ItemMapEntry { NameValue id{}; Address value{}; };
        static_assert(sizeof(ItemMapEntry) == 16);
        struct ItemTextSource {
            std::int32_t offset{}, arrayIndex{-1}, stride{}, innerOffset{};
        };
        struct ItemDescriptionLayout {
            std::vector<ItemTextSource> fields;
            int arrayOffset{-1}, stride{};
        };
        struct ItemCatalogBuild {
            ItemCatalogProgress progress;
            std::unordered_map<std::string, bool> keys;
            std::unordered_map<std::uint64_t, std::size_t> propRows;
            ItemDescriptionLayout descriptions;
            int iconOffset{-1}, fallbackOffset{-1};
            bool collectibleLayout{};
        };
        struct NativeItem {
            ItemEntry entry;
            NameValue id{};
            ItemKind kind{};
            Address row{};
            std::int32_t slot{};
            std::int32_t iconOffset{-1}, iconFallbackOffset{-1};
            std::string nameKey;
            CollectibleType collectibleType{CollectibleType::Unknown};
            NameValue integrationId{};
            std::int32_t sequentialIndex{};
            std::size_t nameParent{static_cast<std::size_t>(-1)};
            std::string parentName, subheadKey;
            bool collectibleSchema{}, nameKeyEmpty{}, subheadInputValid{}, subheadKeyEmpty{}, parentNameResolved{};
            std::vector<ItemTextSource> descriptionFields;
            std::string rawDescription;
            bool nameInputValid{}, named{}, descriptionComplete{};
        };
        struct NativeItemCatalog {
            std::string session;
            Address backpack{};
            std::uint32_t backpackIndex{};
            std::vector<NativeItem> rows;
            std::array<ItemSparseMap, 5> tables{};
            std::unordered_map<std::string, std::size_t> iconRows;
            ItemCatalogBuild build;
            std::size_t parsed{}, described{}, descriptionField{}, cacheHits{}, reformatted{};
            bool built{}, complete{}, reformatting{};
            Clock::time_point nextRefresh{}, nextProgress{}, started{};
        };
        NativeItemCatalog itemCatalog;
        std::uint64_t itemCatalogRevision{1};
        void ItemResetCatalog() {
            itemCatalog = {};
            ++itemCatalogRevision;
        }

        struct CachedItemText { ItemEntry entry; std::string raw; };
        std::unordered_map<std::string, CachedItemText> cachedItemText;
        bool itemCacheLoaded{};
        std::string itemCacheCulture;
        std::mutex itemCultureMutex;
        std::string observedItemCulture;
        void UpdateItemCulture() {
            static Clock::time_point next;
            if (Clock::now() < next) return;
            next = Clock::now() + std::chrono::seconds(1);
            wchar_t local[32768]{};
            std::string culture;
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) {
                const auto config = std::filesystem::path(local) / L"ProjectPBZ/Saved/Config/Windows/GameUserSettings.ini";
                std::ifstream input(config);
                culture = ReadItemCulture(input);
            }
            std::lock_guard lock(itemCultureMutex);
            observedItemCulture = std::move(culture);
        }
        std::string ItemGameCulture() {
            std::lock_guard lock(itemCultureMutex);
            return observedItemCulture;
        }
        std::filesystem::path ItemCachePath() {
            return gloss::DataDirectory() / ("item_catalog_" + itemCacheCulture + ".cache");
        }
        void LoadItemCache() {
            if (itemCacheLoaded) return;
            itemCacheLoaded = true;
            if (itemCacheCulture.empty()) itemCacheCulture = ItemGameCulture();
            if (itemCacheCulture.empty()) return;
            const auto path = ItemCachePath();
            std::error_code error;
            if (std::filesystem::file_size(path, error) > 16 * 1024 * 1024 || error) return;
            std::ifstream stream(path, std::ios::binary);
            ReadItemTextCache(stream, SupportedHash, cachedItemText);

        }
        void SaveItemCache() {
            if (itemCacheCulture.empty()) return;
            bool dirty{};
            for (const auto& item : itemCatalog.rows) if (item.named) {
                const auto found = cachedItemText.find(item.entry.key);
                if (found != cachedItemText.end() && found->second.entry.name == item.entry.name &&
                    found->second.entry.category == item.entry.category && found->second.raw == item.rawDescription) continue;
                cachedItemText[item.entry.key] = {item.entry, item.rawDescription};
                dirty = true;
            }
            if (!dirty) return;
            if (itemCacheCulture.empty()) return;
            // Serialize and write off the game thread: a write-through replace can
            // block for far longer than a frame. The module is pinned until exit.
            static std::mutex saveMutex;
            static std::atomic<std::uint64_t> latestSave{};
            const auto save = ++latestSave;
            struct Row { std::string key, name, category, raw; };
            std::vector<Row> rows;
            rows.reserve(cachedItemText.size());
            for (const auto& [key, value] : cachedItemText)
                rows.push_back({key, value.entry.name, value.entry.category, value.raw});
            try {
                std::thread([path = ItemCachePath(), rows = std::move(rows), save] {
                    try {
                        std::lock_guard lock(saveMutex);
                        if (save != latestSave.load()) return; // A newer save has every row of this one.
                        auto temporary = path; temporary += ".tmp";
                        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                        if (!stream) return;
                        stream << SupportedHash << '\n';
                        for (const auto& row : rows)
                            stream << std::quoted(row.key) << ' ' << std::quoted(row.name) << ' '
                                << std::quoted(row.category) << ' ' << std::quoted(row.raw) << '\n';
                        stream.close();
                        if (stream) MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                    } catch (...) { /* The cache is optional; the next session rebuilds it. */ }
                }).detach();
            } catch (const std::system_error&) { /* Same: no thread means no cache this time. */ }
        }

        std::uint64_t ItemNameIdentity(const NameValue& id) {
            return static_cast<std::uint64_t>(id.comparison) | (static_cast<std::uint64_t>(id.number) << 32);
        }
        bool ItemSameName(const NameValue& left, const NameValue& right) {
            return left.comparison == right.comparison && left.number == right.number;
        }
        bool ItemSameSpelling(std::string_view left, std::string_view right) {
            return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
                [](unsigned char a, unsigned char b) {
                    const auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
                    return fold(a) == fold(b);
                });
        }
        std::optional<std::string> ItemUtf8(const wchar_t* data, std::size_t count) {
            if (!count) return std::string{};
            if (count > 8191 || std::find(data, data + count, L'\0') != data + count) return {};
            const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data,
                static_cast<int>(count), nullptr, 0, nullptr, nullptr);
            if (size <= 0) return {};
            std::string result(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data, static_cast<int>(count),
                result.data(), size, nullptr, nullptr) != size) return {};
            return result;
        }
        std::optional<std::string> ItemString(Address address) {
            StringValue value{};
            if (!ReadBytes(address, &value, sizeof(value)) || value.count < 0 || value.count > 8192 ||
                value.capacity < value.count || value.capacity > 1024 * 1024) return {};
            if (!value.count) return std::string{};
            if (reinterpret_cast<Address>(value.data) < 0x10000) return {};
            std::vector<wchar_t> buffer(static_cast<std::size_t>(value.count));
            if (!ReadBytes(reinterpret_cast<Address>(value.data), buffer.data(), buffer.size() * sizeof(wchar_t)) ||
                buffer.back() != L'\0') return {};
            return ItemUtf8(buffer.data(), buffer.size() - 1);
        }
        bool ItemFieldType(Address property, std::string_view expected, std::int32_t size) {
            return property && Read<std::int32_t>(property + 0x34) == size &&
                Name(Read<std::uint32_t>(Read<Address>(property + 8) + 0x30)).value_or("") == expected;
        }
        bool ItemFunctionSignature(Address function, const ItemTableContract& table) {
            const auto& expected = table.grant;
            if (!ObjectValid(function) || !ClassNamed(Read<Address>(function + 0x10), "Function") ||
                ObjectName(function) != expected.name || Read<std::uint32_t>(function + 0x100) != table.grantFlags ||
                Read<std::uint8_t>(function + 0x104) != expected.count ||
                Read<std::uint16_t>(function + 0x106) != expected.size ||
                Read<Address>(function + 0x128) != base + expected.nativeRva) return false;
            std::array<bool, 8> found{};
            auto property = Read<Address>(function + 0x50);
            unsigned visited{};
            for (; property && visited < 16; ++visited) {
                if (Read<std::uint64_t>(property + 0x38) != 0x18001040000280ull) return false;
                const auto name = Name(Read<std::uint32_t>(property + 0x20)).value_or("");
                bool matched{};
                for (unsigned index = 0; index < expected.count; ++index) {
                    const auto& parameter = expected.parameters[index];
                    if (!ItemSameSpelling(name, parameter.name)) continue;
                    if (found[index] || !PropertyLayout(property, PropertyType(parameter.type),
                        static_cast<std::int32_t>(SizeOf(parameter.type)), parameter.offset)) return false;
                    // Each bool is a complete native byte, never a packed bitfield.
                    if (parameter.type == ParamType::Bool && Read<std::uint32_t>(property + 0x70) != 0xff010001)
                        return false;
                    found[index] = true;
                    matched = true;
                    break;
                }
                if (!matched) return false;
                const auto next = Read<Address>(property + 0x10);
                if (next == property) return false;
                property = next;
            }
            return !property && std::all_of(found.begin(), found.begin() + expected.count,
                [](bool value) { return value; });
        }
        bool ItemTextField(Address type, std::string_view name, std::int32_t offset) {
            const auto property = FindProperty(type, name);
            if (!PropertyLayout(property, "StructProperty", 48, offset)) return false;
            const auto structure = Read<Address>(property + 0x70);
            return ObjectValid(structure) && ObjectName(structure) == "FlowLocText" &&
                Read<std::int32_t>(structure + 0xa8) == 48;
        }
        bool ItemRowLayout(Address backpackType, const ItemTableContract& table) {
            const auto getter = FindFunction(backpackType, table.getter);
            const auto& expected = ItemGetters[static_cast<std::size_t>(table.kind)];
            if (!ObjectValid(getter) || ObjectName(getter) != table.getter ||
                !ClassNamed(Read<Address>(getter + 0x10), "Function") ||
                Read<Address>(getter + 0x128) != base + expected.nativeRva ||
                Read<std::uint32_t>(getter + 0x100) != expected.flags ||
                Read<std::uint8_t>(getter + 0x104) != 2 || Read<std::uint16_t>(getter + 0x106) != expected.size)
                return false;
            const auto result = FindProperty(getter, "ReturnValue");
            if (!PropertyLayout(result, "StructProperty", table.rowSize, 8) ||
                (Read<std::uint64_t>(result + 0x38) & 0x580) != 0x580) return false;
            bool inputFound{}, resultFound{};
            auto parameter = Read<Address>(getter + 0x50);
            for (unsigned visited = 0; parameter && visited < 4; ++visited) {
                if (parameter == result && !resultFound) resultFound = true;
                else if (!inputFound && PropertyLayout(parameter, "NameProperty", 8, 0) &&
                    (Read<std::uint64_t>(parameter + 0x38) & 0x580) == 0x80) inputFound = true;
                else return false;
                const auto next = Read<Address>(parameter + 0x10);
                if (next == parameter) return false;
                parameter = next;
            }
            if (parameter || !inputFound || !resultFound) return false;
            const auto structure = Read<Address>(result + 0x70);
            return ObjectValid(structure) && ObjectName(structure) == table.rowType &&
                Read<std::int32_t>(structure + 0xa8) == table.rowSize &&
                PropertyLayout(FindProperty(structure, "ItemId"), "NameProperty", 8, 8) &&
                ItemTextField(structure, table.nameProperty, table.nameOffset);
        }
        bool ItemCollectibleEnum(Address enumeration) {
            if (!ObjectValid(enumeration) || ObjectName(enumeration) != "EItemTypes" ||
                Read<std::int32_t>(enumeration + 0x50) != 7) return false;
            // These frozen arrays use tagged pointers in this exact build.
            const auto names = Read<Address>(enumeration + 0x40) & ~Address{7};
            const auto values = Read<Address>(enumeration + 0x48) & ~Address{7};
            if (names < 0x10000 || values < 0x10000) return false;
            constexpr std::array<std::string_view, 7> expected{"EItemTypes::Item", "EItemTypes::Book", "EItemTypes::BookPage",
                "EItemTypes::Letter", "EItemTypes::MemoryFlower", "EItemTypes::MemoryFlowerPetal", "EItemTypes::EItemTypes_MAX"};
            for (std::size_t index = 0; index < expected.size(); ++index) {
                NameValue name{};
                std::int64_t value{};
                if (!ReadBytes(names + index * 8, &name, sizeof(name)) || name.number ||
                    Name(name.comparison).value_or("") != expected[index] ||
                    !ReadBytes(values + index * 8, &value, sizeof(value)) || value != static_cast<std::int64_t>(index)) return false;
            }
            return true;
        }
        bool ItemCollectibleLayout(Address structure) {
            const auto type = FindProperty(structure, "ItemType");
            const auto enumeration = Read<Address>(type + 0x78);
            const auto flow = Read<Address>(FindProperty(structure, "Subhead") + 0x70);
            return ObjectValid(structure) && ObjectName(structure) == "PBZItemDataStruct" &&
                Read<std::int32_t>(structure + 0xa8) == 856 &&
                PropertyLayout(type, "EnumProperty", 1, 0x24c) &&
                ItemCollectibleEnum(enumeration) &&
                ItemFieldType(Read<Address>(type + 0x70), "ByteProperty", 1) &&
                PropertyLayout(FindProperty(structure, "IntegrationItemID"), "NameProperty", 8, 0x244) &&
                PropertyLayout(FindProperty(structure, "SequentialIndex"), "IntProperty", 4, 0x240) &&
                ItemTextField(structure, "Subhead", 0xe8) &&
                PropertyLayout(FindProperty(flow, "Namespace"), "StrProperty", 16, 0) &&
                PropertyLayout(FindProperty(flow, "Key"), "StrProperty", 16, 16) &&
                PropertyLayout(FindProperty(flow, "ValueCache"), "TextProperty", 16, 32);
        }
        bool ItemReadMap(Address map, ItemSparseMap& result) {
            if (!ReadBytes(map, &result, sizeof(result)) || result.count < 0 || result.count > 8192 ||
                result.capacity < result.count || result.capacity > 16384 || result.numBits != result.count ||
                result.maxBits < result.numBits || result.maxBits > 16384 ||
                (result.capacity && result.data < 0x10000) ||
                (result.heapBits && (result.heapBits < 0x10000 || (result.heapBits & 3))) ||
                (!result.heapBits && result.numBits > 128)) return false;
            return !result.data || !(result.data & 7);
        }
        bool ItemSameMap(const ItemSparseMap& left, const ItemSparseMap& right) {
            return left.data == right.data && left.count == right.count && left.capacity == right.capacity &&
                left.heapBits == right.heapBits && left.numBits == right.numBits && left.maxBits == right.maxBits &&
                left.inlineBits == right.inlineBits;
        }
        bool ItemSlotAllocated(Address map, const ItemSparseMap& layout, std::int32_t slot, bool& allocated) {
            if (slot < 0 || slot >= layout.count) return false;
            const auto bits = layout.heapBits ? layout.heapBits : map + 16;
            std::uint32_t word{};
            if (!ReadBytes(bits + static_cast<Address>(slot / 32) * 4, &word, sizeof(word))) return false;
            allocated = (word & (1u << (slot % 32))) != 0;
            return true;
        }
        bool ItemReadMapEntry(const ItemSparseMap& layout, std::int32_t slot, ItemMapEntry& entry) {
            return slot >= 0 && slot < layout.count &&
                ReadBytes(layout.data + static_cast<Address>(slot) * 24, &entry, sizeof(entry));
        }
        bool ItemTableValid(const ItemTableContract& table, ItemSparseMap& layout) {
            // The table RVAs identify inline singleton objects, not pointer globals.
            const auto object = base + table.tableRva;
            const auto path = ItemString(object + 0x78);
            return path && *path == table.path && Executable(Read<Address>(Read<Address>(object))) &&
                ItemReadMap(object + 8, layout);
        }
        bool ItemCurrentRow(const NativeItem& item) {
            const auto index = static_cast<std::size_t>(item.kind);
            const auto& table = ItemTables[index];
            ItemSparseMap current;
            bool allocated{};
            ItemMapEntry entry;
            NameValue rowId{};
            return ItemTableValid(table, current) && ItemSameMap(current, itemCatalog.tables[index]) &&
                ItemSlotAllocated(base + table.tableRva + 8, current, item.slot, allocated) && allocated &&
                ItemReadMapEntry(current, item.slot, entry) && ItemSameName(entry.id, item.id) && entry.value == item.row &&
                ReadBytes(item.row + 8, &rowId, sizeof(rowId)) && ItemSameName(rowId, item.id);
        }
        bool ItemTextEntriesValid() {
            std::array<unsigned char, ItemResolveEntry.size()> resolve{};
            std::array<unsigned char, ItemToStringEntry.size()> toString{};
            return ReadBytes(base + 0x66c27a0, resolve.data(), resolve.size()) &&
                resolve == ItemResolveEntry &&
                ReadBytes(base + 0x131e1e0, toString.data(), toString.size()) && toString == ItemToStringEntry;
        }
        // No C++ destructor-bearing objects in this SEH leaf. The returned FText
        // reference is released even when ToString or its memory read raises SEH.
        enum class ItemTextResult { Read, Unavailable, Fault };
        ItemTextResult ItemResolveTextLeaf(Address flow, wchar_t* buffer, std::int32_t capacity, std::int32_t* length) noexcept {
            Address text[2]{};
            bool success{};
            *length = 0;
            __try {
                __try {
                    using Resolve = void*(__fastcall*)(Address, void*);
                    using ToString = const StringValue*(__fastcall*)(const void*);
                    reinterpret_cast<Resolve>(base + 0x66c27a0)(flow, text);
                    const auto string = reinterpret_cast<ToString>(base + 0x131e1e0)(text);
                    if (string && string->count >= 0 && string->count <= capacity &&
                        string->capacity >= string->count && string->capacity <= 1024 * 1024) {
                        if (!string->count) success = true;
                        else if (string->data && string->data[string->count - 1] == L'\0') {
                            std::memcpy(buffer, string->data, static_cast<std::size_t>(string->count) * sizeof(wchar_t));
                            *length = string->count - 1;
                            success = true;
                        }
                    }
                } __finally {
                    if (text[0]) {
                        using Release = void(__fastcall*)(void*);
                        const auto data = text[0];
                        const auto vtable = *reinterpret_cast<Address*>(data);
                        reinterpret_cast<Release>(*reinterpret_cast<Address*>(vtable + 0x10))(reinterpret_cast<void*>(data));
                        text[0] = 0;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { return ItemTextResult::Fault; }
            return success ? ItemTextResult::Read : ItemTextResult::Unavailable;
        }
        std::optional<std::string> ItemResolveText(Address flow) {
            if (stopping.load() || faulted.load() || !LocalGameThread()) return {};
            std::array<wchar_t, 8192> buffer{};
            std::int32_t length{};
            const auto result = ItemResolveTextLeaf(flow, buffer.data(), static_cast<std::int32_t>(buffer.size()), &length);
            if (result == ItemTextResult::Fault) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "物品文本解析异常，适配器已停用；不会自动重试发放");
                return {};
            }
            if (result != ItemTextResult::Read) return {};
            return ItemUtf8(buffer.data(), static_cast<std::size_t>(length));
        }
        bool ItemFlowInput(Address flow, std::string& key) {
            const auto nameSpace = ItemString(flow);
            const auto value = ItemString(flow + 16);
            if (!nameSpace || !value || value->empty()) return false;
            key = *value;
            return true;
        }
        int ItemSoftTextureOffset(Address rowType, std::string_view propertyName) {
            const auto property = FindProperty(rowType, propertyName);
            if (!ItemFieldType(property, "SoftObjectProperty", 40)) return -1;
            const auto offset = Read<std::int32_t>(property + 0x44);
            if (offset < 0 || offset + 40 > Read<std::int32_t>(rowType + 0xa8) ||
                !ClassNamed(Read<Address>(property + 0x70), "Texture2D")) return -1;
            return offset;
        }
        std::string ItemSoftTextureKey(Address row, int offset) {
            if (offset < 0) return {};
            const auto soft = row + offset;
            const auto subPath = ItemString(soft + 24);
            if (!subPath || !subPath->empty()) return {};
            const auto decode = [](Address address) {
                NameValue id{};
                if (!ReadBytes(address, &id, sizeof(id))) return std::string{};
                auto result = Name(id.comparison).value_or("");
                if (id.number) result += std::format("_{}", id.number - 1);
                return result;
            };
            const auto package = decode(soft + 8), asset = decode(soft + 16);
            if (!package.starts_with("/Game/") || asset.empty() || asset == "None") return {};
            return package + '.' + asset;
        }
        ItemDescriptionLayout ItemDescriptionFields(ItemKind kind, Address rowType) {
            ItemDescriptionLayout layout;
            const auto field = [&](std::string_view name, int offset) {
                if (ItemTextField(rowType, name, offset)) layout.fields.push_back({offset});
            };
            std::string_view arrayName;
            int arrayOffset{}, stride{};
            if (kind == ItemKind::Prop) {
                field("ItemDescription", 0x58); field("IPDescription", 0x88);
                field("FunctionalDescription", 0xb8); field("Subhead", 0xe8); field("ItemSignature", 0x118);
            } else if (kind == ItemKind::Accessory) {
                field("AccessoryDesc", 0xf8); field("IPDesc", 0x128); field("ShortIntroduction", 0x1e8);
            } else if (kind == ItemKind::Medicine) {
                field("Desc", 0x58); field("ShortIntroduction", 0x280);
                arrayName = "AbilityDesc"; arrayOffset = 0x88; stride = 48;
            } else if (kind == ItemKind::MainWeapon) {
                field("EquipDescription", 0x2b8); field("Simple", 0x2e8); field("ShortIntroduction", 0x6d8);
                arrayName = "WeaponAbilityDesc"; arrayOffset = 0x318; stride = 96;
            } else {
                field("EquipDescription", 0x340); field("SimpleDesc", 0x370); field("ShortIntroduction", 0x7b0);
                arrayName = "WeaponAbilityDesc"; arrayOffset = 0x3a0; stride = 96;
            }
            if (arrayName.empty()) return layout;
            const auto property = FindProperty(rowType, arrayName);
            if (!PropertyLayout(property, "ArrayProperty", 16, arrayOffset)) return layout;
            const auto inner = Read<Address>(property + 0x78);
            if (!ItemFieldType(inner, "StructProperty", stride)) return layout;
            const auto structure = Read<Address>(inner + 0x70);
            if (!ObjectValid(structure) || Read<std::int32_t>(structure + 0xa8) != stride) return layout;
            if (stride == 48) {
                if (ObjectName(structure) != "FlowLocText") return layout;
            } else if (ObjectName(structure) != "WeaponAbilityDescEntry" ||
                !ItemTextField(structure, "WeaponTitle", 0) || !ItemTextField(structure, "WeaponDesc", 48)) return layout;
            layout.arrayOffset = arrayOffset;
            layout.stride = stride;
            return layout;
        }
        void ItemDescriptionSources(NativeItem& item, const ItemDescriptionLayout& layout) {
            item.descriptionFields = layout.fields;
            if (layout.arrayOffset < 0) return;
            const auto count = Read<std::int32_t>(item.row + layout.arrayOffset + 8);
            const auto capacity = Read<std::int32_t>(item.row + layout.arrayOffset + 12);
            if (count < 0 || count > 32 || capacity < count || capacity > 128) return;
            for (int i = 0; i < std::min(count, 8); ++i) {
                item.descriptionFields.push_back({layout.arrayOffset, i, layout.stride, 0});
                if (layout.stride == 96) item.descriptionFields.push_back({layout.arrayOffset, i, layout.stride, 48});
            }
        }
        Address ItemDescriptionAddress(const NativeItem& item, const ItemTextSource& source) {
            if (source.arrayIndex < 0) return item.row + source.offset;
            const auto array = item.row + source.offset;
            const auto count = Read<std::int32_t>(array + 8), capacity = Read<std::int32_t>(array + 12);
            const auto data = Read<Address>(array);
            if (count <= source.arrayIndex || count > 32 || capacity < count || capacity > 128 ||
                data < 0x10000 || (data & 7)) return 0;
            return data + source.arrayIndex * source.stride + source.innerOffset;
        }
        #include "native_item_input.inc"
        bool ItemBackpackMatches(const Context& context, Address backpack) {
            const auto current = CurrentContext();
            return !stopping.load() && !faulted.load() && LocalGameThread() && current &&
                current->SameKey(context) && activeBound && current->SameKey(activeContext) &&
                Backpack(*current) == backpack && ObjectValid(backpack) &&
                Read<std::uint32_t>(backpack + 0xc) == itemCatalog.backpackIndex;
        }
        #include "native_dust.inc"
        void ItemPublish(bool ready, std::string status) {
            LoadItemCache();
            std::vector<ItemEntry> entries;
            if (itemCatalog.complete) {
                entries.reserve(itemCatalog.rows.size());
                for (const auto& item : itemCatalog.rows) entries.push_back(item.entry);
                if (dustItem) entries.push_back(*dustItem);
            }
            if (!itemCatalog.complete) {
                std::unordered_map<std::string, bool> published;
                const auto preview = [&](ItemEntry entry) {
                    entry.available = false;
                    entry.owned = -1;
                    entry.reason = "等待当前游戏目录验证";
                    published.emplace(entry.key, true);
                    entries.push_back(std::move(entry));
                };
                // Cold starts can show names as they arrive; a cache is only a
                // browsing preview until this session's full catalog is checked.
                for (const auto& item : itemCatalog.rows) if (item.named) preview(item.entry);
                for (const auto& [key, value] : cachedItemText)
                    if (!published.contains(key)) preview(value.entry);
            }
            GetBackend().PublishItems(std::move(entries), ready, std::move(status));
        }
        void ItemInvalidate(std::string reason) {
            dustItem.reset();
            const auto session = itemCatalog.session;
            const auto backpack = itemCatalog.backpack;
            const auto backpackIndex = itemCatalog.backpackIndex;
            ItemResetCatalog();
            itemCatalog.session = session;
            itemCatalog.backpack = backpack;
            itemCatalog.backpackIndex = backpackIndex;
            itemCatalog.nextRefresh = Clock::now() + std::chrono::milliseconds(1500);
            ItemPublish(false, std::move(reason));
        }
        bool ItemBuildCatalog(Address backpack, std::string& reason) {
            const auto backpackType = Read<Address>(backpack + 0x10);
            if (!ItemTextEntriesValid()) { reason = "本地化入口指令与版本契约不符"; return false; }
            auto& build = itemCatalog.build;
            auto& progress = build.progress;
            if (itemCatalog.started == Clock::time_point{}) {
                itemCatalog.started = Clock::now();
                itemCatalog.rows.reserve(791);
            }
            // Table setup, sparse slots (including holes), joins and the final
            // identity pass all share one budget. No name engine calls start in
            // the Tick that completes this phase.
            ItemLoadingBudget budget(Clock::now(), std::chrono::milliseconds(1), 32);
            std::size_t checkedTable = std::size(ItemTables);
            while (!progress.Complete() && budget.Continue(Clock::now())) {
                budget.Consume();
                if (progress.phase == ItemCatalogProgress::Phase::Tables) {
                    const auto& table = ItemTables[progress.table];
                    auto& layout = itemCatalog.tables[progress.table];
                    if (!progress.tableReady) {
                        if (!ItemRowLayout(backpackType, table) || !ItemTableValid(table, layout) ||
                            layout.count != table.expectedSlots) {
                            reason = std::format("{}表路径、反射行结构或稀疏映射未通过核对", table.category);
                            return false;
                        }
                        const auto getter = FindFunction(backpackType, table.getter);
                        const auto rowType = Read<Address>(FindProperty(getter, "ReturnValue") + 0x70);
                        build.collectibleLayout = table.kind == ItemKind::Prop && ItemCollectibleLayout(rowType);
                        const auto iconProperty = table.kind == ItemKind::Prop ? "ItemIcon" :
                            table.kind == ItemKind::Medicine || table.kind == ItemKind::Accessory ? "HighPrecisionTexture" : "RenderIcon";
                        build.iconOffset = ItemSoftTextureOffset(rowType, iconProperty);
                        build.fallbackOffset = ItemSoftTextureOffset(rowType,
                            table.kind == ItemKind::Medicine ? "SoftTexture" : "Icon");
                        build.descriptions = ItemDescriptionFields(table.kind, rowType);
                        progress.tableReady = true;
                        checkedTable = progress.table;
                        continue;
                    }
                    // A table can unload while this builder is yielded. Never
                    // resume a saved sparse pointer before checking its owner.
                    if (checkedTable != progress.table || progress.slot == layout.count) {
                        ItemSparseMap current;
                        if (!ItemTableValid(table, current) || !ItemSameMap(layout, current)) {
                            reason = std::format("{}表加载状态发生变化，等待稳定后重建", table.category);
                            return false;
                        }
                        checkedTable = progress.table;
                    }
                    if (progress.slot == layout.count) {
                        if (progress.allocated < 1) { reason = "物品表没有已分配行"; return false; }
                        progress.FinishTable(std::size(ItemTables));
                        continue;
                    }
                    const auto slot = progress.slot++;
                    bool allocated{};
                    if (!ItemSlotAllocated(base + table.tableRva + 8, layout, slot, allocated)) {
                        reason = "物品表分配位读取失败";
                        return false;
                    }
                    if (!allocated) continue;
                    ++progress.allocated;
                    ItemMapEntry mapEntry;
                    NameValue rowId{};
                    if (!ItemReadMapEntry(layout, slot, mapEntry) || mapEntry.value < 0x10000 || (mapEntry.value & 7) ||
                        !ReadBytes(mapEntry.value + 8, &rowId, sizeof(rowId)) || !ItemSameName(rowId, mapEntry.id)) {
                        reason = "物品表行指针或行标识与映射键不一致";
                        return false;
                    }
                    const auto symbolic = Name(mapEntry.id.comparison);
                    if (!symbolic || symbolic->empty() || *symbolic == "None") {
                        reason = "物品表存在无法解码的标识";
                        return false;
                    }
                    auto key = std::string(table.prefix) + ':' + *symbolic;
                    if (mapEntry.id.number) key += std::format("_{}", mapEntry.id.number - 1);
                    if (!build.keys.emplace(key, true).second) { reason = "物品符号键发生冲突"; return false; }
                    NativeItem item;
                    item.entry.key = std::move(key);
                    item.entry.name = "未命名";
                    item.entry.category = table.category;
                    item.entry.maxQuantity = table.kind == ItemKind::Prop ? 999 : 1;
                    item.id = mapEntry.id;
                    item.kind = table.kind;
                    item.row = mapEntry.value;
                    item.slot = slot;
                    item.nameInputValid = ItemFlowInput(item.row + table.nameOffset, item.nameKey);
                    if (build.collectibleLayout) {
                        std::uint8_t type{};
                        if (ReadBytes(item.row + 0x24c, &type, sizeof(type))) {
                            item.collectibleSchema = true;
                            item.collectibleType = static_cast<CollectibleType>(type);
                            const auto category = CollectibleCategory(item.collectibleSchema, item.collectibleType);
                            if (!category.empty()) item.entry.category = category;
                            if (CollectibleIsUnique(item.collectibleSchema, item.collectibleType)) item.entry.maxQuantity = 1;
                            // Only the two proven child types need parent/name
                            // metadata; ordinary prop rows retain their old path.
                            if ((item.collectibleType == CollectibleType::BookPage ||
                                item.collectibleType == CollectibleType::MemoryFlowerPetal) && !item.nameInputValid &&
                                ReadBytes(item.row + 0x244, &item.integrationId, sizeof(item.integrationId)) &&
                                ReadBytes(item.row + 0x240, &item.sequentialIndex, sizeof(item.sequentialIndex))) {
                                const auto nameSpace = ItemString(item.row + 0x28);
                                const auto nameKey = ItemString(item.row + 0x38);
                                item.nameKeyEmpty = nameSpace && nameKey && nameKey->empty();
                                if (item.nameKeyEmpty) {
                                    const auto subheadNamespace = ItemString(item.row + 0xe8);
                                    const auto subhead = ItemString(item.row + 0xf8);
                                    item.subheadInputValid = subheadNamespace && subhead;
                                    if (item.subheadInputValid) {
                                        item.subheadKey = *subhead;
                                        item.subheadKeyEmpty = subhead->empty();
                                    }
                                }
                            }
                        }
                    }
                    item.iconOffset = build.iconOffset;
                    item.iconFallbackOffset = build.fallbackOffset;
                    item.entry.iconKey = ItemSoftTextureKey(item.row, build.iconOffset);
                    auto fallbackKey = ItemSoftTextureKey(item.row, build.fallbackOffset);
                    if (item.entry.iconKey.empty()) {
                        item.entry.iconKey = std::move(fallbackKey);
                        item.iconOffset = item.iconFallbackOffset;
                        item.iconFallbackOffset = -1;
                    } else if (fallbackKey.empty() || fallbackKey == item.entry.iconKey) item.iconFallbackOffset = -1;
                    ItemDescriptionSources(item, build.descriptions);
                    itemCatalog.rows.push_back(std::move(item));
                } else if (progress.phase == ItemCatalogProgress::Phase::ParentIndex) {
                    if (progress.row < itemCatalog.rows.size()) {
                        const auto index = progress.row++;
                        const auto& item = itemCatalog.rows[index];
                        if (item.kind == ItemKind::Prop) build.propRows.emplace(ItemNameIdentity(item.id), index);
                        if (!item.entry.iconKey.empty() && item.iconOffset >= 0)
                            itemCatalog.iconRows.try_emplace(item.entry.iconKey, index);
                    }
                    progress.FinishRows(itemCatalog.rows.size());
                } else if (progress.phase == ItemCatalogProgress::Phase::ParentLinks) {
                    if (progress.row < itemCatalog.rows.size()) {
                        auto& item = itemCatalog.rows[progress.row++];
                        if (item.kind == ItemKind::Prop && item.nameKeyEmpty && item.subheadInputValid && item.sequentialIndex > 0) {
                            const auto parent = build.propRows.find(ItemNameIdentity(item.integrationId));
                            if (parent != build.propRows.end()) {
                                const auto& source = itemCatalog.rows[parent->second];
                                if (source.nameInputValid && source.collectibleSchema &&
                                    CollectibleFallbackAllowed(item.collectibleSchema, item.collectibleType,
                                        item.nameKeyEmpty, source.collectibleType)) item.nameParent = parent->second;
                            }
                        }
                    }
                    progress.FinishRows(itemCatalog.rows.size());
                } else if (progress.phase == ItemCatalogProgress::Phase::VerifyTables) {
                    ItemSparseMap current;
                    if (!ItemTableValid(ItemTables[progress.table], current) ||
                        !ItemSameMap(current, itemCatalog.tables[progress.table])) {
                        reason = "物品表在增量构建期间重新加载，等待重建";
                        return false;
                    }
                    progress.FinishVerification(std::size(ItemTables));
                }
            }
            if (!progress.Complete()) return true;
            itemCatalog.parsed = itemCatalog.described = itemCatalog.descriptionField = itemCatalog.cacheHits = 0;
            itemCatalog.built = true;
            build.keys.clear();
            build.propRows.clear();
            gloss::Log(std::format("PBZ item catalog layout built incrementally: rows={} elapsed={:.3f}ms", itemCatalog.rows.size(),
                std::chrono::duration<double, std::milli>(Clock::now() - itemCatalog.started).count()));
            return true;
        }
        using ItemAmounts = std::unordered_map<std::uint64_t, std::int64_t>;
        bool ItemReadInventory(Address backpack, const ItemTableContract& table, ItemAmounts& amounts) {
            const auto property = FindProperty(Read<Address>(backpack + 0x10), table.inventoryProperty);
            if (!PropertyLayout(property, "MapProperty", 80, table.inventoryOffset)) return false;
            const auto keyProperty = Read<Address>(property + 0x70);
            const auto valueProperty = Read<Address>(property + 0x78);
            if (!ItemFieldType(keyProperty, "NameProperty", 8) || !ItemFieldType(valueProperty, "ObjectProperty", 8))
                return false;
            const auto valueClass = Read<Address>(valueProperty + 0x70);
            if (!ObjectValid(valueClass) || ObjectName(valueClass) != table.valueClass) return false;
            if (table.kind == ItemKind::Prop &&
                (!PropertyLayout(FindProperty(valueClass, "CurrentAmount"), "IntProperty", 4, 0x380) ||
                    !PropertyLayout(FindProperty(valueClass, "HasMaxAmount"), "IntProperty", 4, 0x384) ||
                    !PropertyLayout(FindProperty(valueClass, "ItemId"), "NameProperty", 8, 0x388))) return false;
            const auto map = backpack + table.inventoryOffset;
            ItemSparseMap layout;
            if (!ItemReadMap(map, layout)) return false;
            amounts.clear();
            for (std::int32_t slot = 0; slot < layout.count; ++slot) {
                bool allocated{};
                if (!ItemSlotAllocated(map, layout, slot, allocated)) return false;
                if (!allocated) continue;
                ItemMapEntry entry;
                if (!ItemReadMapEntry(layout, slot, entry) || !ObjectValid(entry.value) ||
                    !IsClass(Read<Address>(entry.value + 0x10), valueClass)) return false;
                std::int64_t owned = 1;
                if (table.kind == ItemKind::Prop) {
                    NameValue rowId{};
                    std::int32_t current{};
                    if (!ReadBytes(entry.value + 0x380, &current, sizeof(current)) || current < 0 ||
                        !ReadBytes(entry.value + 0x388, &rowId, sizeof(rowId)) || !ItemSameName(rowId, entry.id)) return false;
                    // HasMaxAmount is not a proven capacity contract. Do not use it
                    // as a limit or alter it while granting ordinary items.
                    owned = current;
                }
                if (!amounts.emplace(ItemNameIdentity(entry.id), owned).second) return false;
            }
            ItemSparseMap after;
            return ItemReadMap(map, after) && ItemSameMap(layout, after);
        }
        std::optional<std::int64_t> ItemOwned(Address backpack, const NativeItem& item) {
            ItemAmounts amounts;
            if (!ItemReadInventory(backpack, ItemTables[static_cast<std::size_t>(item.kind)], amounts)) return {};
            const auto found = amounts.find(ItemNameIdentity(item.id));
            return found == amounts.end() ? 0 : found->second;
        }
        bool ItemRefreshAmounts(Address backpack) {
            PerformanceScope performance(2);
            bool changed{};
            for (const auto& table : ItemTables) {
                ItemAmounts amounts;
                const bool readable = ItemReadInventory(backpack, table, amounts);
                const auto function = FindFunction(Read<Address>(backpack + 0x10), table.grant.name);
                const bool callable = ItemFunctionSignature(function, table);
                for (auto& item : itemCatalog.rows) {
                    if (item.kind != table.kind) continue;
                    const auto found = amounts.find(ItemNameIdentity(item.id));
                    const std::int64_t owned = !readable ? -1 : (found == amounts.end() ? 0 : found->second);
                    const bool overflow = table.kind == ItemKind::Prop && owned > std::numeric_limits<std::int32_t>::max() - 1;
                    const bool duplicate = (table.kind != ItemKind::Prop || CollectibleIsUnique(item.collectibleSchema, item.collectibleType)) && owned > 0;
                    const bool available = item.named && readable && callable && !overflow && !duplicate;
                    const std::string reason = !item.named ? "游戏本地化名称为空或未能解析，禁止发放" :
                        !callable ? "发放函数反射契约不匹配" : !readable ? "背包映射或数量字段未通过核对" :
                        duplicate ? "已拥有，禁止重复发放" : overflow ? "当前数量已达到整数上限" : "";
                    changed = changed || item.entry.owned != owned || item.entry.available != available || item.entry.reason != reason;
                    item.entry.owned = owned;
                    item.entry.available = available;
                    item.entry.reason = reason;
                }
            }
            return changed;
        }
        bool ItemRefreshDescriptions(const Context& context, Address backpack) {
            if (itemCatalog.described == itemCatalog.rows.size()) return true;
            ItemLoadingBudget budget(Clock::now(), std::chrono::milliseconds(1), 8);
            bool changed{};
            std::size_t rowsVisited{};
            while (itemCatalog.described < itemCatalog.rows.size() && rowsVisited < 64 && budget.Continue(Clock::now())) {
                auto& item = itemCatalog.rows[itemCatalog.described];
                if (!item.named || item.descriptionComplete || itemCatalog.descriptionField == item.descriptionFields.size()) {
                    if (item.named && !item.descriptionComplete) {
                        item.entry.description = ItemFormattedDescription(item.rawDescription);
                        changed = true;
                    }
                    item.descriptionComplete = true;
                    ++itemCatalog.described;
                    itemCatalog.descriptionField = 0;
                    ++rowsVisited;
                    continue;
                }
                if (!ItemBackpackMatches(context, backpack) || !ItemCurrentRow(item)) {
                    ItemInvalidate("文本解析期间会话或物品表已变化");
                    return false;
                }
                const auto address = ItemDescriptionAddress(item, item.descriptionFields[itemCatalog.descriptionField]);
                std::string key;
                if (address && ItemFlowInput(address, key)) {
                    const auto description = ItemResolveText(address);
                    if (faulted.load()) { ItemInvalidate("物品文本解析异常，目录已停用"); return false; }
                    if (description && !description->empty() && *description != key &&
                        item.rawDescription.find(*description) == std::string::npos) {
                        if (!item.rawDescription.empty()) item.rawDescription += "\n\n";
                        item.rawDescription += *description;
                    }
                }
                ++itemCatalog.descriptionField;
                budget.Consume();
            }
            if (!ItemBackpackMatches(context, backpack)) { ItemInvalidate("文本解析期间会话或物品表已变化"); return false; }
            const auto now = Clock::now();
            const bool complete = itemCatalog.described == itemCatalog.rows.size();
            if (complete) {
                SaveItemCache();
                gloss::Log(std::format("PBZ item catalog descriptions complete: rows={} elapsed={:.3f}ms", itemCatalog.rows.size(),
                    std::chrono::duration<double, std::milli>(now - itemCatalog.started).count()));
            }
            if (complete || (changed && now >= itemCatalog.nextProgress)) {
                ItemPublish(true, std::format("已从游戏加载 {} 件物品；数量定期回读", itemCatalog.rows.size()));
                itemCatalog.nextProgress = now + std::chrono::milliseconds(250);
            }
            return true;
        }
        void RefreshItems(const Context& context, bool force = false) {
            PerformanceScope performance(5);
            if (stopping.load() || faulted.load() || !LocalGameThread()) return;
            if (!force && !GetBackend().ItemPageActive()) return;
            static Clock::time_point nextCultureCheck;
            if (Clock::now() >= nextCultureCheck) {
                nextCultureCheck = Clock::now() + std::chrono::seconds(1);
                const auto culture = ItemGameCulture();
                if (culture != itemCacheCulture) {
                    itemCacheCulture = culture;
                    cachedItemText.clear();
                    itemCacheLoaded = false;
                    ItemResetCatalog();
                    gloss::Log("PBZ item culture changed: " + culture);
                }
            }
            const auto session = context.Key();
            const auto backpack = Backpack(context);
            if (!backpack || session != activeSession) {
                ItemInvalidate("等待当前玩家背包子系统");
                return;
            }
            const auto backpackIndex = Read<std::uint32_t>(backpack + 0xc);
            if (itemCatalog.session != session || itemCatalog.backpack != backpack || itemCatalog.backpackIndex != backpackIndex) {
                ItemResetCatalog();
                itemCatalog.session = session;
                itemCatalog.backpack = backpack;
                itemCatalog.backpackIndex = backpackIndex;
                itemInput.scan = {};
                ItemPublish(false, "正在核对当前场景的物品表与本地化布局");
            }
            const auto now = Clock::now();
            const bool inputChanged = ItemRefreshInput(context);
            if (inputChanged && itemCatalog.complete) { itemCatalog.reformatting = true; itemCatalog.reformatted = 0; }
            if (itemCatalog.reformatting) {
                // Rich text for every row can take far longer than a frame.
                ItemLoadingBudget budget(Clock::now(), std::chrono::milliseconds(1), 64);
                while (itemCatalog.reformatted < itemCatalog.rows.size() && budget.Continue(Clock::now())) {
                    auto& item = itemCatalog.rows[itemCatalog.reformatted++];
                    item.entry.description = ItemFormattedDescription(item.rawDescription);
                    budget.Consume();
                }
                if (itemCatalog.reformatted == itemCatalog.rows.size()) {
                    itemCatalog.reformatting = false;
                    ItemPublish(true, std::format("已从游戏加载物品；连招按键：{}", itemInput.device));
                }
            }
            if (!itemCatalog.built) {
                if (!force && now < itemCatalog.nextRefresh) return;
                std::string reason;
                if (!ItemBuildCatalog(backpack, reason)) { ItemInvalidate(std::move(reason)); return; }
                // Even a cheap cached name must wait until the next Tick, so
                // separate bounded phases cannot accumulate in one game frame.
                return;
            }
            if (!itemCatalog.complete) {
                if (!ItemTextEntriesValid()) { ItemInvalidate("本地化入口已变化，目录解析停止"); return; }
                // Names make the catalog usable first. Cached/empty rows do not
                // consume one entire Tick each, and long descriptions run later.
                ItemLoadingBudget budget(Clock::now(), std::chrono::milliseconds(2), 32);
                LoadItemCache();
                while (itemCatalog.parsed < itemCatalog.rows.size() && budget.Continue(Clock::now())) {
                    auto& item = itemCatalog.rows[itemCatalog.parsed];
                    if (!ItemBackpackMatches(context, backpack) || !ItemCurrentRow(item)) {
                        ItemInvalidate("解析期间会话或物品表已变化，等待重新建立目录");
                        return;
                    }
                    const auto& table = ItemTables[static_cast<std::size_t>(item.kind)];
                    const auto cached = cachedItemText.find(item.entry.key);
                    if (cached != cachedItemText.end() && (item.nameInputValid || item.nameParent < itemCatalog.rows.size())) {
                        item.entry.name = cached->second.entry.name;
                        item.rawDescription = cached->second.raw;
                        item.entry.description = ItemFormattedDescription(item.rawDescription);
                        item.named = true;
                        ++itemCatalog.cacheHits;
                        // An empty cached description may be from the early
                        // name-only checkpoint. Revisit it under the lazy budget.
                        item.descriptionComplete = !item.rawDescription.empty();
                    } else if (item.nameInputValid) {
                        const auto name = ItemResolveText(item.row + table.nameOffset);
                        if (faulted.load()) { ItemInvalidate("物品文本解析异常，目录已停用"); return; }
                        if (name && !name->empty() && *name != item.nameKey) {
                            item.entry.name = *name;
                            item.named = true;
                        }
                    } else if (item.nameParent < itemCatalog.rows.size()) {
                        const auto& parent = itemCatalog.rows[item.nameParent];
                        if (!ItemCurrentRow(parent)) {
                            ItemInvalidate("收藏品父项已变化，正在重建目录"); return;
                        }
                        // Each pass starts at most one localization call. The
                        // existing deadline is checked again before the subhead.
                        if (!item.parentNameResolved) {
                            item.parentNameResolved = true;
                            if (parent.named) item.parentName = parent.entry.name;
                            else {
                                const auto name = ItemResolveText(parent.row + ItemTables[0].nameOffset);
                                if (faulted.load()) { ItemInvalidate("收藏品父名称解析异常，目录已停用"); return; }
                                if (name && !name->empty() && *name != parent.nameKey) item.parentName = *name;
                            }
                            budget.Consume();
                            continue;
                        }
                        std::optional<std::string> subhead;
                        if (item.subheadKeyEmpty) subhead = std::string{};
                        else if (!item.parentName.empty()) {
                            subhead = ItemResolveText(item.row + 0xe8);
                            if (faulted.load()) { ItemInvalidate("收藏品副标题解析异常，目录已停用"); return; }
                            if (subhead && *subhead == item.subheadKey) subhead.reset();
                        }
                        const auto name = ComposeCollectibleName(item.collectibleType, item.parentName,
                            subhead ? std::optional<std::string_view>(*subhead) : std::nullopt, item.sequentialIndex);
                        if (name) { item.entry.name = *name; item.named = true; }
                    }
                    ++itemCatalog.parsed;
                    budget.Consume();
                }
                if (!ItemBackpackMatches(context, backpack)) { ItemInvalidate("目录解析后玩家会话变化"); return; }
                if (itemCatalog.parsed != itemCatalog.rows.size()) {
                    if (now >= itemCatalog.nextProgress) {
                        ItemPublish(false, std::format("正在解析游戏物品名称：{} / {}", itemCatalog.parsed, itemCatalog.rows.size()));
                        itemCatalog.nextProgress = now + std::chrono::milliseconds(250);
                    }
                    return;
                }
                itemCatalog.complete = true;
                SaveItemCache();
                ItemRefreshAmounts(backpack);
                RefreshDust(context);
                if (!ItemBackpackMatches(context, backpack)) { ItemInvalidate("目录库存回读后玩家会话变化"); return; }
                ItemPublish(true, std::format("已从游戏加载 {} 件物品；数量定期回读", itemCatalog.rows.size()));
                gloss::Log(std::format("PBZ item catalog names ready: rows={} cacheHits={} elapsed={:.3f}ms", itemCatalog.rows.size(), itemCatalog.cacheHits,
                    std::chrono::duration<double, std::milli>(Clock::now() - itemCatalog.started).count()));
                itemCatalog.nextRefresh = now + std::chrono::milliseconds(1500);
                return;
            }
            if (!ItemRefreshDescriptions(context, backpack)) return;
            if (!force && now < itemCatalog.nextRefresh) return;
            // Recheck table identities before reusing any FName from the catalog.
            for (std::size_t index = 0; index < std::size(ItemTables); ++index) {
                ItemSparseMap layout;
                if (!ItemTableValid(ItemTables[index], layout) || !ItemSameMap(layout, itemCatalog.tables[index])) {
                    ItemInvalidate("物品表已重新加载，正在重建目录");
                    return;
                }
            }
            if (!ItemBackpackMatches(context, backpack)) { ItemInvalidate("库存刷新前玩家会话变化"); return; }
            const bool changed = ItemRefreshAmounts(backpack);
            const bool resourceChanged = RefreshDust(context);
            if (!ItemBackpackMatches(context, backpack)) { ItemInvalidate("库存刷新后玩家会话变化"); return; }
            if (changed || resourceChanged || force || !GetBackend().ItemsReady())
                ItemPublish(true, std::format("已从游戏加载 {} 件物品及角色资源；数量已回读", itemCatalog.rows.size()));
            itemCatalog.nextRefresh = now + std::chrono::milliseconds(1500);
        }
        bool ItemImageLoadSignature(Address function) {
            if (!ObjectValid(function) || ObjectName(function) != "LoadAsset_Blocking" ||
                Read<Address>(function + 0x128) != base + 0x40c0800 ||
                Read<std::uint32_t>(function + 0x100) != 0x04022403 ||
                Read<std::uint8_t>(function + 0x104) != 2 || Read<std::uint16_t>(function + 0x106) != 48) return false;
            auto parameter = Read<Address>(function + 0x50);
            if (ObjectName(Read<Address>(function + 0x20)) != "KismetSystemLibrary" ||
                Name(Read<std::uint32_t>(parameter + 0x20)) != "Asset" ||
                !PropertyLayout(parameter, "SoftObjectProperty", 40, 0) ||
                Read<std::uint64_t>(parameter + 0x38) != 0x1c000000000080ull) return false;
            parameter = Read<Address>(parameter + 0x10);
            return Name(Read<std::uint32_t>(parameter + 0x20)) == "ReturnValue" &&
                PropertyLayout(parameter, "ObjectProperty", 8, 40) &&
                Read<std::uint64_t>(parameter + 0x38) == 0x18001000000780ull && !Read<Address>(parameter + 0x10);
        }
        bool ItemCopyBulkLeaf(Address bulk, void* destination) noexcept {
            __try {
                void* pointer = destination;
                using Copy = void(__fastcall*)(void*, void**, bool);
                reinterpret_cast<Copy>(base + 0x15144c0)(reinterpret_cast<void*>(bulk), &pointer, false);
                return pointer == destination;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        ItemImageObjectIdentity ItemImageIdentify(Address object) {
            ItemImageObjectIdentity result;
            if (!ObjectValid(object)) return result;
            std::int32_t count{}, index{}, serial{};
            std::uint32_t flags{};
            Address chunks{}, chunk{}, pointer{};
            if (!ReadBytes(object + 0xc, &index, sizeof(index)) || index < 0 ||
                !ReadBytes(base + 0xde28c70, &count, sizeof(count)) || count < 0 || count > 2000000 || index >= count ||
                !ReadBytes(base + 0xde28c68, &chunks, sizeof(chunks)) || chunks < 0x10000 ||
                !ReadBytes(chunks + static_cast<Address>(index >> 16) * 8, &chunk, sizeof(chunk)) || chunk < 0x10000) return {};
            const auto slot = chunk + static_cast<Address>(index & 65535) * 24;
            if (!ReadBytes(slot + 8, &pointer, sizeof(pointer)) || pointer != object ||
                !ReadBytes(slot + 16, &serial, sizeof(serial)) ||
                !ReadBytes(slot + 4, &flags, sizeof(flags)) || (flags & 0x10200000) ||
                !ReadBytes(object + 0x10, &result.type, sizeof(result.type)) ||
                !ReadBytes(object + 0x18, &result.name, sizeof(result.name)) ||
                !ReadBytes(object + 0x20, &result.outer, sizeof(result.outer))) return {};
            result.address = object;
            result.index = index;
            result.serial = serial;
            return result;
        }
        bool ItemImagePathMatches(Address texture, std::string_view source) {
            const auto outer = Read<Address>(texture + 0x20);
            if (!ObjectValid(texture) || !ObjectValid(outer)) return false;
            const auto decode = [](Address object) {
                NameValue id{};
                if (!ReadBytes(object + 0x18, &id, sizeof(id))) return std::string{};
                auto name = Name(id.comparison).value_or("");
                if (id.number) name += std::format("_{}", id.number - 1);
                return name;
            };
            const auto package = decode(outer), asset = decode(texture);
            return package.starts_with("/Game/") && !asset.empty() && source == package + '.' + asset;
        }
        void ProcessItemImage(const Context& context) {
            PerformanceScope performance(3);
            if (!GetBackend().ItemPageActive()) return;
            static Clock::time_point nextImage;
            static std::uint64_t lastEpoch{};
            static bool imageFault{};
            static std::optional<ItemImageRequest> delayedImage;
            static unsigned imageWaits{};
            static std::int32_t imageAttemptOffset{-1};
            static std::uint64_t lastRevision{};
            static ItemImageObservation observedImage;
            static ItemImageObjectIdentity observedClass, observedSizeX;
            const auto clearObservation = [&] {
                observedImage.Clear(); observedClass = {}; observedSizeX = {};
            };
            const auto epoch = ItemImageEpoch();
            if (lastEpoch != epoch) {
                lastEpoch = epoch; imageFault = false; nextImage = {};
                delayedImage.reset(); imageWaits = 0; imageAttemptOffset = -1;
                clearObservation();
            }
            if (lastRevision != itemCatalogRevision) {
                lastRevision = itemCatalogRevision;
                clearObservation(); imageWaits = 0; imageAttemptOffset = -1;
                // Keep a pending bridge key so it is eventually completed; only
                // the weak engine observation belongs to the old catalog.
            }
            if (!epoch || stopping.load() || faulted.load() || !LocalGameThread() ||
                !itemCatalog.complete || Clock::now() < nextImage) return;
            std::optional<ItemImageRequest> request;
            if (delayedImage) { request = std::move(delayedImage); delayedImage.reset(); }
            else {
                request = TakeItemImageRequest(); imageWaits = 0; imageAttemptOffset = -1;
                clearObservation();
            }
            if (!request) return;
            nextImage = Clock::now() + std::chrono::milliseconds(100);
            ItemImageResult result;
            result.key = request->key; result.epoch = request->epoch;
            const auto fail = [&](std::string reason) {
                clearObservation();
                result.data.clear(); result.error = std::move(reason);
                gloss::Log("PBZ item image unavailable: " + result.key + ": " + result.error);
                PublishItemImage(std::move(result));
            };
            const auto defer = [&] {
                if (imageWaits >= 20) return false;
                ++imageWaits;
                delayedImage = request;
                return true;
            };
            if (imageFault) { fail("本次会话的图片读取发生异常，停止重试"); return; }
            const auto indexed = itemCatalog.iconRows.find(request->key);
            const auto found = indexed != itemCatalog.iconRows.end() && indexed->second < itemCatalog.rows.size() ?
                itemCatalog.rows.begin() + indexed->second : itemCatalog.rows.end();
            if (request->epoch != epoch || found == itemCatalog.rows.end() || found->iconOffset < 0 ||
                found->entry.iconKey != request->key ||
                !ItemBackpackMatches(context, itemCatalog.backpack) || !ItemCurrentRow(*found)) {
                fail("图片不在当前游戏定义中，或玩家会话已变化"); return;
            }
            if (imageAttemptOffset < 0) imageAttemptOffset = found->iconOffset;
            if (imageAttemptOffset != found->iconOffset && imageAttemptOffset != found->iconFallbackOffset) {
                fail("图片软引用字段已变化"); return;
            }
            const auto unavailable = [&](std::string reason) {
                if (imageAttemptOffset == found->iconOffset && found->iconFallbackOffset >= 0 &&
                    found->iconFallbackOffset != found->iconOffset &&
                    ItemBackpackMatches(context, itemCatalog.backpack) && ItemCurrentRow(*found)) {
                    gloss::Log("PBZ item image fallback: " + result.key + ": " + reason);
                    // A different soft path must get its own load/defer cycle.
                    clearObservation();
                    imageAttemptOffset = found->iconFallbackOffset;
                    imageWaits = 0;
                    delayedImage = request;
                    return;
                }
                fail(std::move(reason));
            };
            constexpr std::array<unsigned char, 16> CopyEntry{
                0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x56,0x48,0x83};
            std::array<unsigned char, 16> copyEntry{};
            if (!ReadBytes(base + 0x15144c0, copyEntry.data(), copyEntry.size()) || copyEntry != CopyEntry) {
                fail("图片复制函数版本契约不匹配"); return;
            }
            // Re-read the exact selected soft path on every yield. A reused row
            // address or a fallback must never inherit another texture's wait.
            const auto sourceKey = ItemSoftTextureKey(found->row, imageAttemptOffset);
            const auto source = found->row + imageAttemptOffset;
            const auto subpath = ItemString(source + 24);
            if (sourceKey.empty() || !subpath || !subpath->empty()) {
                unavailable("图片软引用布局已变化"); return;
            }
            Address texture{};
            bool mustYieldAfterLoad = !imageWaits;
            if (observedImage.texture.Valid()) {
                const auto current = ItemImageIdentify(observedImage.texture.address);
                bool textureContract{};
                if (observedImage.Matches(epoch, itemCatalogRevision, sourceKey, current) &&
                    ItemImagePathMatches(current.address, sourceKey)) {
                    if (observedClass.Valid() && observedSizeX.Valid()) {
                        const auto currentClass = ItemImageIdentify(observedClass.address);
                        const auto currentSizeX = ItemImageIdentify(observedSizeX.address);
                        textureContract = currentClass.Valid() && currentClass == observedClass &&
                            current.type == currentClass.address && currentSizeX.Valid() && currentSizeX == observedSizeX &&
                            Read<Address>(currentSizeX.address + 0x128) == base + 0x4636e10;
                    } else {
                        // UClass/UFunction serials can remain unallocated. This
                        // only disables their reflection cache, not reuse of a
                        // texture whose weak serial is valid.
                        const auto sizeX = FindFunction(current.type, "Blueprint_GetSizeX");
                        textureContract = ClassNamed(current.type, "Texture2D") && ObjectValid(sizeX) &&
                            Read<Address>(sizeX + 0x128) == base + 0x4636e10;
                    }
                }
                if (textureContract) texture = current.address;
                else {
                    clearObservation();
                    mustYieldAfterLoad = true;
                }
            }
            if (!texture) {
                const auto function = Read<Address>(base + 0xdfc2270);
                const auto owner = Read<Address>(function + 0x20);
                const auto cdo = Read<Address>(owner + 0x160);
                if (!ItemImageLoadSignature(function) || !ObjectValid(cdo)) {
                    fail("图片读取函数版本契约不匹配"); return;
                }
                // No arbitrary paths: the source belongs to the checked row.
                alignas(8) std::array<std::byte, 48> parameters{};
                if (!ReadBytes(source, parameters.data(), 40)) {
                    unavailable("图片软引用布局已变化"); return;
                }
                // Never borrow the definition row's empty FString allocation.
                std::fill(parameters.begin() + 24, parameters.begin() + 40, std::byte{});
                if (!CallProcessEvent(cdo, function, parameters.data())) {
                    imageFault = true; fail("图片资源加载发生原生异常"); return;
                }
                std::memcpy(&texture, parameters.data() + 40, sizeof(texture));
                if (!ItemBackpackMatches(context, itemCatalog.backpack) || !ItemCurrentRow(*found) ||
                    ItemSoftTextureKey(found->row, imageAttemptOffset) != sourceKey) {
                    fail("图片加载后玩家会话或定义行已变化"); return;
                }
                if (!ObjectValid(texture) || !ClassNamed(Read<Address>(texture + 0x10), "Texture2D") ||
                    !ItemImagePathMatches(texture, sourceKey)) {
                    unavailable("游戏没有返回路径一致的 Texture2D 图片"); return;
                }
                const auto type = Read<Address>(texture + 0x10);
                const auto sizeX = FindFunction(type, "Blueprint_GetSizeX");
                if (!ObjectValid(sizeX) || Read<Address>(sizeX + 0x128) != base + 0x4636e10) {
                    unavailable("纹理平台数据布局未通过核对"); return;
                }
                const auto identity = ItemImageIdentify(texture);
                observedClass = ItemImageIdentify(type);
                observedSizeX = ItemImageIdentify(sizeX);
                if (identity.Valid()) observedImage = {epoch, itemCatalogRevision, sourceKey, identity};
                else clearObservation(); // Serial-less assets retain the original reload path.
                // Loading can enqueue render initialization. Always return to
                // the game loop once before inspecting a new object's payload.
                if (mustYieldAfterLoad) {
                    if (!defer()) unavailable("图片对象反复变化，停止等待");
                    return;
                }
            }
            const auto platform = Read<Address>(texture + 0x160);
            const auto mipCount = Read<std::int32_t>(platform + 0x40), mipCapacity = Read<std::int32_t>(platform + 0x44);
            if (platform < 0x10000 || mipCount < 1 || mipCount > 16 || mipCapacity < mipCount || mipCapacity > 32) {
                unavailable("图片顶层 mip 不可用"); return;
            }
            const auto mip = Read<Address>(Read<Address>(platform + 0x38));
            result.width = Read<std::uint16_t>(mip + 0x48);
            result.height = Read<std::uint16_t>(mip + 0x4a);
            if (mip < 0x10000 || !result.width || !result.height || result.width > 2048 || result.height > 2048 ||
                Read<std::uint16_t>(mip + 0x4c) != 1) { unavailable("图片尺寸不在支持范围内"); return; }
            const auto format = Read<std::uint8_t>(platform + 12);
            std::size_t bytes{};
            const auto blocks = static_cast<std::size_t>((result.width + 3) / 4) * ((result.height + 3) / 4);
            switch (format) {
            case 2: result.format = ItemImageFormat::Bgra8; bytes = std::size_t(result.width) * result.height * 4; break;
            case 37: result.format = ItemImageFormat::Rgba8; bytes = std::size_t(result.width) * result.height * 4; break;
            case 5: result.format = ItemImageFormat::Bc1; bytes = blocks * 8; break;
            case 6: result.format = ItemImageFormat::Bc2; bytes = blocks * 16; break;
            case 7: result.format = ItemImageFormat::Bc3; bytes = blocks * 16; break;
            case 56: result.format = ItemImageFormat::Bc7; bytes = blocks * 16; break;
            default: unavailable(std::format("暂不支持游戏纹理格式 {}", format)); return;
            }
            const auto bulk = mip + 0x20;
            std::array<unsigned char, 5> bulkSize{};
            if (!ReadBytes(bulk + 8, bulkSize.data(), bulkSize.size())) { unavailable("图片数据长度不可读"); return; }
            std::uint64_t size{};
            for (unsigned i = 0; i < 5; ++i) size |= std::uint64_t(bulkSize[i]) << (i * 8);
            if (size != bytes || bytes > 16 * 1024 * 1024 || Read<std::uint8_t>(bulk + 0x13) != 0) {
                unavailable("图片数据长度或锁状态不匹配"); return;
            }
            // Only the unloaded-payload IO path has been validated. Do not race
            // the render thread's upload/discard of a freshly loaded CPU payload.
            if (Read<Address>(bulk)) {
                if (defer()) return;
                unavailable("图片仍由游戏渲染线程持有，暂不读取"); return;
            }
            const auto bulkFlags = Read<std::uint32_t>(bulk + 0x14);
            result.data.assign(bytes, 0xcd);
            if (!ItemCopyBulkLeaf(bulk, result.data.data())) {
                imageFault = true; fail("图片数据复制发生原生异常"); return;
            }
            if (!ItemBackpackMatches(context, itemCatalog.backpack) || !ItemCurrentRow(*found) ||
                ItemSoftTextureKey(found->row, imageAttemptOffset) != sourceKey || !ItemImagePathMatches(texture, sourceKey) ||
                (observedImage.texture.Valid() && !observedImage.Matches(epoch, itemCatalogRevision,
                    sourceKey, ItemImageIdentify(texture)))) {
                fail("图片读取后玩家会话、对象或定义行已变化"); return;
            }
            if (Read<std::uint8_t>(bulk + 0x13) != 0 ||
                Read<Address>(bulk) || Read<std::uint32_t>(bulk + 0x14) != bulkFlags ||
                std::all_of(result.data.begin(), result.data.end(), [](auto value) { return value == 0xcd; })) {
                unavailable("图片数据未完成读取"); return;
            }
            gloss::Log(std::format("PBZ item image read: {} {}x{} format={} bytes={} bulkFlags={:x} fallback={}",
                result.key, result.width, result.height, format, bytes, bulkFlags, imageAttemptOffset != found->iconOffset));
            clearObservation();
            PublishItemImage(std::move(result));
        }
        ExecutionResult GrantItem(const Context& context, const Request& request) {
            if (request.featureId != "grant_item" || request.arguments.size() != 2)
                return {RequestState::Failed, "物品发放请求格式错误"};
            int quantity{};
            const auto& text = request.arguments[1];
            if (!ParseItemQuantity(text, quantity))
                return {RequestState::Failed, "发放数量必须为有效的正整数"};
            if (!itemCatalog.complete || itemCatalog.session != context.Key() ||
                !ItemBackpackMatches(context, itemCatalog.backpack))
                return {RequestState::Failed, "当前会话的物品目录尚未就绪"};
            if (request.arguments[0] == DustItemKey) {
                auto result = GrantDust(context, quantity);
                if (!faulted.load() && ItemBackpackMatches(context, itemCatalog.backpack))
                    ItemPublish(true, "物品及角色资源数量已回读");
                return result;
            }
            const auto found = std::find_if(itemCatalog.rows.begin(), itemCatalog.rows.end(),
                [&](const NativeItem& item) { return item.entry.key == request.arguments[0]; });
            if (found == itemCatalog.rows.end()) return {RequestState::Failed, "物品不在当前游戏目录中，拒绝任意标识"};
            const auto& item = *found;
            if (!item.named || quantity > item.entry.maxQuantity)
                return {RequestState::Failed, "该物品没有有效游戏名称，或数量超过可发放上限"};
            if (!ItemCurrentRow(item)) { ItemInvalidate("物品行已变化，正在重建目录"); return {RequestState::Failed, "物品行已变化，未执行"}; }
            const auto backpack = itemCatalog.backpack;
            const auto& table = ItemTables[static_cast<std::size_t>(item.kind)];
            const auto function = FindFunction(Read<Address>(backpack + 0x10), table.grant.name);
            if (!ItemFunctionSignature(function, table)) return {RequestState::Failed, "发放函数契约不符，未执行"};
            const auto before = ItemOwned(backpack, item);
            if (!before) return {RequestState::Failed, "无法读取发放前库存，未执行"};
            if ((item.kind != ItemKind::Prop || CollectibleIsUnique(item.collectibleSchema, item.collectibleType)) && *before != 0)
                return {RequestState::Failed, "已拥有该物品，未重复发放"};
            if (!ItemQuantityFitsInventory(*before, quantity))
                return {RequestState::Failed, "当前数量加请求数量会溢出，未执行"};
            alignas(8) std::array<std::byte, 16> parameters{};
            if (item.kind == ItemKind::Prop) {
                std::memcpy(parameters.data(), &quantity, sizeof(quantity));
                std::memcpy(parameters.data() + 4, &item.id, sizeof(item.id));
            } else {
                std::memcpy(parameters.data(), &item.id, sizeof(item.id));
                if (item.kind == ItemKind::MainWeapon) parameters[8] = std::byte{1};
            }
            // All other bools remain false: no auto-equip, load-data bypass, or
            // pickup UI side effects. There is exactly one dispatch and no retry.
            if (!ItemBackpackMatches(context, backpack) || !ItemCurrentRow(item) ||
                !ItemFunctionSignature(function, table)) return {RequestState::Failed, "调用前会话或契约变化，未执行"};
            if (!CallProcessEvent(backpack, function, parameters.data())) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "物品发放调用异常，适配器已停用；不会自动重试");
                ItemInvalidate("发放发生原生异常，物品目录已停用");
                return {RequestState::Indeterminate, "原生调用异常，库存变化无法确认；不会重试"};
            }
            ++dispatchedCalls;
            if (!ItemBackpackMatches(context, backpack)) {
                ItemInvalidate("发放后玩家会话变化，等待重建目录");
                return {RequestState::Indeterminate, "调用后世界或玩家变化，结果无法确认；不会重试"};
            }
            const auto after = ItemOwned(backpack, item);
            const auto name = item.entry.name;
            if (!ItemBackpackMatches(context, backpack)) {
                ItemInvalidate("发放库存回读期间玩家会话变化");
                return {RequestState::Indeterminate, "库存回读期间玩家会话变化，结果无法确认；不会重试"};
            }
            if (!after) {
                RefreshItems(context, true);
                return {RequestState::Indeterminate, "调用已返回，发放后库存无法读取；不会重试"};
            }
            const auto delta = *after - *before;
            RefreshItems(context, true);
            if (delta == quantity)
                return {RequestState::Verified, std::format("{}：库存 {} → {}，已核对增加 {}", name, *before, *after, quantity)};
            if (delta == 0) return {RequestState::Failed, "调用已返回，库存没有增加；不会重试"};
            return {RequestState::Indeterminate, std::format("库存增量 {} 与请求数量 {} 不一致；不会重试", delta, quantity)};
        }
