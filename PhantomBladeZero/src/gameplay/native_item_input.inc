// Rich-text action IDs are resolved through the game's decorator DataTable, then
// the active Enhanced Input mappings. No action has a hard-coded default key.
        struct ItemInputState {
            Address table{};
            std::uint32_t tableIndex{};
            std::uint32_t scanCursor{};
            Clock::time_point nextRefresh{};
            std::unordered_map<std::string, std::string> labels;
            std::string device;
        };
        ItemInputState itemInput;
        Address ItemCommonInput(const Context& context) {
            ItemSparseMap map;
            const auto address = context.player + 0x140;
            if (!ReadBytes(address, &map, sizeof(map)) || map.count < 1 || map.count > 128 ||
                map.capacity < map.count || map.numBits != map.count || map.maxBits < map.numBits || map.maxBits > 4096) return 0;
            const auto bits = map.heapBits ? map.heapBits : address + 16;
            if (!map.heapBits && map.numBits > 128) return 0;
            for (int i = 0; i < map.count; ++i) {
                if (!(Read<std::uint32_t>(bits + (i / 32) * 4) & (1u << (i % 32)))) continue;
                const auto object = Read<Address>(map.data + i * 24 + 8);
                if (ObjectValid(object) && ObjectName(Read<Address>(object + 0x10)) == "CommonInputSubsystem" &&
                    Read<Address>(object + 0x20) == context.player) return object;
            }
            return 0;
        }
        bool ItemInputTableValid() {
            return ObjectValid(itemInput.table) && Read<std::uint32_t>(itemInput.table + 0xc) == itemInput.tableIndex &&
                ObjectName(itemInput.table) == "DT_RichTextInputAction" &&
                ObjectName(Read<Address>(itemInput.table + 0x20)) == "/Game/PBZContent/UI/Foundation/Text/Decorator/DT_RichTextInputAction" &&
                ObjectName(Read<Address>(itemInput.table + 0x10)) == "DataTable" &&
                ObjectName(Read<Address>(itemInput.table + 0x28)) == "RichTextInputActionMapping" &&
                Read<std::int32_t>(Read<Address>(itemInput.table + 0x28) + 0xa8) == 264 &&
                PropertyLayout(FindProperty(Read<Address>(itemInput.table + 0x28), "InputAction"), "SoftObjectProperty", 40, 8);
        }
        void ItemDiscoverInputTable() {
            if (ItemInputTableValid()) return;
            itemInput.table = 0;
            const auto chunks = Read<Address>(base + 0xddb3c58);
            const auto count = Read<std::uint32_t>(base + 0xddb3c60);
            if (!chunks || count > 2000000) return;
            if (itemInput.scanCursor >= count) itemInput.scanCursor = 0;
            const auto end = (std::min)(count, itemInput.scanCursor + 4096);
            for (; itemInput.scanCursor < end; ++itemInput.scanCursor) {
                const auto index = itemInput.scanCursor;
                const auto chunk = Read<Address>(chunks + (index / 65536) * 8);
                const auto object = Read<Address>(chunk + (index % 65536) * 24 + 8);
                if (Read<std::uint32_t>(object + 0xc) != index || ObjectName(object) != "DT_RichTextInputAction") continue;
                itemInput.table = object; itemInput.tableIndex = index;
                if (ItemInputTableValid()) return;
                itemInput.table = 0;
            }
        }
        std::string ItemKeyLabel(std::string key, std::string_view gamepad) {
            const bool ps = gamepad == "PS4" || gamepad == "PS5" || gamepad.find("Dual") != gamepad.npos;
            const bool xbox = gamepad.find("Xbox") != gamepad.npos || gamepad == "XInput";
            const std::pair<std::string_view, std::string_view> labels[] = {
                {"LeftMouseButton", "鼠标左键"}, {"RightMouseButton", "鼠标右键"}, {"MiddleMouseButton", "鼠标中键"},
                {"ThumbMouseButton", "鼠标侧键 1"}, {"ThumbMouseButton2", "鼠标侧键 2"},
                {"SpaceBar", "空格"}, {"LeftShift", "左 Shift"}, {"RightShift", "右 Shift"},
                {"LeftControl", "左 Ctrl"}, {"RightControl", "右 Ctrl"}, {"LeftAlt", "左 Alt"},
                {"Gamepad_FaceButton_Bottom", ps ? "叉键" : xbox ? "A" : "面键（下）"},
                {"Gamepad_FaceButton_Right", ps ? "圆圈键" : xbox ? "B" : "面键（右）"},
                {"Gamepad_FaceButton_Left", ps ? "方块键" : xbox ? "X" : "面键（左）"},
                {"Gamepad_FaceButton_Top", ps ? "三角键" : xbox ? "Y" : "面键（上）"},
                {"Gamepad_LeftShoulder", ps ? "L1" : "LB"}, {"Gamepad_RightShoulder", ps ? "R1" : "RB"},
                {"Gamepad_LeftTrigger", ps ? "L2" : "LT"}, {"Gamepad_RightTrigger", ps ? "R2" : "RT"},
                {"Gamepad_LeftTriggerAxis", ps ? "L2" : "LT"}, {"Gamepad_RightTriggerAxis", ps ? "R2" : "RT"},
                {"Gamepad_LeftThumbstick", ps ? "L3" : "左摇杆按下"}, {"Gamepad_RightThumbstick", ps ? "R3" : "右摇杆按下"},
                {"Gamepad_DPad_Up", "方向键上"}, {"Gamepad_DPad_Down", "方向键下"},
                {"Gamepad_DPad_Left", "方向键左"}, {"Gamepad_DPad_Right", "方向键右"}
            };
            for (const auto& [name, label] : labels) if (key == name) return std::string(label);
            return key;
        }
        bool ItemRefreshInput(const Context& context) {
            ItemDiscoverInputTable();
            const auto now = Clock::now();
            if (now < itemInput.nextRefresh) return false;
            itemInput.nextRefresh = now + std::chrono::milliseconds(250);
            std::unordered_map<std::string, std::string> labels;
            const auto common = ItemCommonInput(context);
            const auto commonType = Read<Address>(common + 0x10);
            const bool deviceLayout = common && PropertyLayout(FindProperty(commonType, "CurrentInputType"), "EnumProperty", 1, 0x91) &&
                PropertyLayout(FindProperty(commonType, "GamepadInputType"), "NameProperty", 8, 0x94);
            const auto device = deviceLayout ? Read<std::uint8_t>(common + 0x91) : 255;
            const auto gamepad = deviceLayout ? Name(Read<std::uint32_t>(common + 0x94)).value_or("") : "";
            const auto playerInput = Read<Address>(context.controller + 0x428);
            const auto mappings = FindProperty(Read<Address>(playerInput + 0x10), "EnhancedActionMappings");
            const auto inner = Read<Address>(mappings + 0x78);
            const auto mappingType = Read<Address>(inner + 0x70);
            const bool mappingLayout = ObjectValid(playerInput) && ClassNamed(Read<Address>(playerInput + 0x10), "EnhancedPlayerInput") &&
                PropertyLayout(mappings, "ArrayProperty", 16, 0x628) && ItemFieldType(inner, "StructProperty", 80) &&
                ObjectName(mappingType) == "EnhancedActionKeyMapping" &&
                PropertyLayout(FindProperty(mappingType, "Action"), "ObjectProperty", 8, 0x20) &&
                PropertyLayout(FindProperty(mappingType, "Key"), "StructProperty", 24, 0x28);
            const auto data = Read<Address>(playerInput + 0x628);
            const auto count = Read<std::int32_t>(playerInput + 0x630), capacity = Read<std::int32_t>(playerInput + 0x634);
            ItemSparseMap rows;
            if (device < 2 && mappingLayout && data && count >= 0 && count <= 2048 && capacity >= count && capacity <= 4096 &&
                ItemInputTableValid() && ReadBytes(itemInput.table + 0x30, &rows, sizeof(rows)) && rows.count >= 0 &&
                rows.count <= 256 && rows.capacity >= rows.count && rows.numBits == rows.count && rows.maxBits >= rows.count) {
                const auto bits = rows.heapBits ? rows.heapBits : itemInput.table + 0x40;
                for (int i = 0; i < rows.count && (rows.heapBits || rows.count <= 128); ++i) {
                    if (!(Read<std::uint32_t>(bits + (i / 32) * 4) & (1u << (i % 32)))) continue;
                    const auto entry = rows.data + i * 24;
                    const auto id = Name(Read<std::uint32_t>(entry));
                    const auto row = Read<Address>(entry + 8);
                    if (!id || !row) continue;
                    // Rows with device-specific overrides require the game's full brush
                    // resolver. Never substitute the default for an unhandled override.
                    if (Read<std::int32_t>(row + 0x38) || Read<std::int32_t>(row + 0x88) ||
                        Read<std::int32_t>(row + 0x100) || Read<std::uint32_t>(row + 0xd8)) continue;
                    const auto package = Read<NameValue>(row + 0x10), asset = Read<NameValue>(row + 0x18);
                    if (!package.comparison || !asset.comparison || package.number || asset.number) continue;
                    std::vector<std::string> keys;
                    for (int j = 0; j < count; ++j) {
                        const auto mapping = data + j * 80;
                        const auto action = Read<Address>(mapping + 0x20);
                        if (Read<std::uint32_t>(action + 0x18) != asset.comparison ||
                            Read<std::uint32_t>(Read<Address>(action + 0x20) + 0x18) != package.comparison ||
                            !ObjectValid(action)) continue;
                        const auto key = Name(Read<std::uint32_t>(mapping + 0x28));
                        if (!key || *key == "None" || key->starts_with("Gamepad_") != (device == 1)) continue;
                        const auto label = ItemKeyLabel(*key, gamepad);
                        if (std::find(keys.begin(), keys.end(), label) == keys.end()) keys.push_back(label);
                    }
                    std::string joined;
                    for (const auto& key : keys) { if (!joined.empty()) joined += " / "; joined += key; }
                    labels.emplace(*id, joined.empty() ? "未绑定" : std::move(joined));
                }
            }
            const auto deviceName = device == 0 ? "键鼠" : device == 1 ? "手柄 " + gamepad : "等待输入设备";
            const bool changed = itemInput.labels != labels || itemInput.device != deviceName;
            itemInput.labels = std::move(labels); itemInput.device = deviceName;
            return changed;
        }
        std::string ItemFormattedDescription(std::string_view raw) {
            return FormatItemRichText(raw, [](const ItemRichTextReference& reference) -> std::optional<std::string> {
                if (reference.kind == ItemRichTextKind::InputAction) {
                    const auto found = itemInput.labels.find(reference.id);
                    return found != itemInput.labels.end() ? found->second : "按键待读取";
                }
                if (reference.kind == ItemRichTextKind::InlineImage && reference.tag == "img") {
                    if (reference.id == "Fire") return "火";
                    if (reference.id == "Thunder") return "雷";
                    if (reference.id == "Ice") return "冰";
                }
                if (reference.kind == ItemRichTextKind::InlineImage && reference.tag == "Niagara" && reference.id == "Fire") return "技能能量";
                return reference.kind == ItemRichTextKind::DynamicValue ? "（数值待读取）" : "图示";
            }).text;
        }
