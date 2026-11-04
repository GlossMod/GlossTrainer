// Exact-build Puerts entry. Only our private module is served, synchronously on
// the game thread. Ordinary loads/log messages always call their original slot.
        constexpr std::array<std::string_view, 4> ScriptToggleIds{
            "free_teleport", "unrestricted_teleport", "auto_collect", "highlight_loot"};
        constexpr wchar_t ScriptModuleName[] = L"__gloss_pbz_private_302256.js";
        struct ScriptBytes { Address data{}; std::int32_t count{}, capacity{}; };
        using ScriptSearch = bool(__fastcall*)(Address, const StringValue*, const StringValue*, StringValue*, StringValue*);
        using ScriptLoad = bool(__fastcall*)(Address, const StringValue*, ScriptBytes*);
        using ScriptLog = void(__fastcall*)(Address, const StringValue*);
        // PBZ adds an integrity manifest after IJSModuleLoader::Load. Its ABI is
        // bool Verify(const FString& Path, const TArray<uint8>& Data), with no this.
        using ScriptVerify = bool(__fastcall*)(const StringValue*, const ScriptBytes*);
        constexpr Address ScriptVerifyRva = 0x62f65a0;
        constexpr std::array<unsigned char, 23> ScriptVerifyEntry{
            0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0xe8,0x32,0x3b,0xff,0xff,0x48,0x85,0xc0,0x74,0x22,0x48,0x63,0x53,0x08};
        std::atomic<ScriptVerify> originalScriptVerify{};
        bool scriptVerifyCreated{}, scriptVerifyPatched{}, scriptVerifyPatchCaptured{};
        std::array<unsigned char, 16> scriptVerifyOwnedEntry{};
        Address scriptEnv{}, scriptLoader{}, scriptLogger{};
        Context scriptContext;
        std::array<bool, ScriptToggleIds.size()> scriptEnabled{};
        // Only acknowledged on-requests publish a distance parameter. Indexes
        // share ScriptToggleIds; the two teleport toggles have no distance.
        std::array<double, ScriptToggleIds.size()> scriptDistances{0, 0, 10, 50};
        std::string scriptLootDiagnostic;
        std::array<std::atomic<Address>, 6> scriptSavedSlots{};
        std::array<bool, 6> scriptPatched{};
        thread_local bool scriptExecuting{};
        std::string scriptSource, scriptReply;
        std::uint64_t scriptSequence{}, scriptNextTick{};
        std::string scriptAck;
        bool scriptAckSeen{}, scriptStateOwned{}, scriptUncertain{};
        std::uint32_t scriptInstanceIndex{};
        std::string scriptError;
        Address scriptDataDescriptor{};
        ScriptBytes scriptLoadedBytes;
        bool scriptPrivateVerified{}, scriptProbeConfirmed{};
        Context scriptProbeContext;
        Address scriptProbeEnv{}, scriptProbeLoader{}, scriptProbeLogger{};
        std::uint32_t scriptProbeInstanceIndex{};
        struct ScriptModuleReceipt {
            bool confirmed{};
            Context context;
            Address env{}, loader{}, logger{};
            std::uint32_t instanceIndex{};
        } scriptModuleReceipt;
        const char* scriptStage{"idle"};
        std::string scriptLastError;
        struct ScriptExceptionRecord {
            DWORD code{};
            Address instruction{}, accessAddress{}, rcx{}, rdx{}, r8{}, r9{};
            ULONG_PTR accessType{};
            const char* boundary{};
            const char* stage{};
        } scriptException;
        int ScriptExceptionFilter(EXCEPTION_POINTERS* exception, const char* boundary) noexcept {
            // Preserve the earliest leaf fault if the game then throws again
            // while unwinding/reporting its failed module load.
            if (scriptException.code) { faulted.store(true); return EXCEPTION_EXECUTE_HANDLER; }
            scriptException = {};
            scriptException.boundary = boundary; scriptException.stage = scriptStage;
            if (exception && exception->ExceptionRecord) {
                const auto& record = *exception->ExceptionRecord;
                scriptException.code = record.ExceptionCode;
                scriptException.instruction = reinterpret_cast<Address>(record.ExceptionAddress);
                if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && record.NumberParameters >= 2) {
                    scriptException.accessType = record.ExceptionInformation[0];
                    scriptException.accessAddress = record.ExceptionInformation[1];
                }
            }
            if (exception && exception->ContextRecord) {
                const auto& registers = *exception->ContextRecord;
                scriptException.rcx = registers.Rcx; scriptException.rdx = registers.Rdx;
                scriptException.r8 = registers.R8; scriptException.r9 = registers.R9;
            }
            faulted.store(true);
            return EXCEPTION_EXECUTE_HANDLER;
        }
        bool ScriptThreadReady() { return !stopping.load() && !faulted.load() && LocalGameThread(); }
        bool ScriptFeature(std::string_view id) {
            return std::find(ScriptToggleIds.begin(), ScriptToggleIds.end(), id) != ScriptToggleIds.end();
        }
        bool ScriptDistanceFeature(std::string_view id) { return id == "auto_collect" || id == "highlight_loot"; }
        bool ScriptDistanceValid(std::string_view id, double value) {
            return ScriptDistanceFeature(id) && std::isfinite(value) && value >= 1 && value <= (id == "auto_collect" ? 50 : 100);
        }
        bool ScriptStringEquals(const StringValue* value, const wchar_t* expected, std::size_t length) {
            StringValue string;
            if (!value || !ReadBytes(reinterpret_cast<Address>(value), &string, sizeof(string)) ||
                string.count != length + 1 || string.capacity < string.count) return false;
            std::array<wchar_t, 128> text{};
            return length < text.size() && ReadBytes(reinterpret_cast<Address>(string.data), text.data(), (length + 1) * 2) &&
                std::char_traits<wchar_t>::compare(text.data(), expected, length + 1) == 0;
        }
        bool ScriptMakeString(StringValue* out) noexcept {
            __try {
                if (out->data || out->count || out->capacity) return false;
                reinterpret_cast<void(__fastcall*)(StringValue*, const wchar_t*)>(base + 0x127c240)(out, ScriptModuleName);
                return out->data && out->count == std::size(ScriptModuleName);
            } __except (ScriptExceptionFilter(GetExceptionInformation(), "FString-constructor")) { return false; }
        }
        bool __fastcall HookScriptSearch(Address loader, const StringValue* directory, const StringValue* module,
            StringValue* path, StringValue* debugPath) {
            if (scriptExecuting && !faulted.load() && loader == scriptLoader && LocalGameThread() &&
                ScriptStringEquals(module, ScriptModuleName, std::size(ScriptModuleName) - 1))
                {
                    scriptStage = "private-search";
                    return ScriptMakeString(path) && ScriptMakeString(debugPath);
                }
            return reinterpret_cast<ScriptSearch>(scriptSavedSlots[0].load())(loader, directory, module, path, debugPath);
        }
        bool ScriptFillBytes(ScriptBytes* out, const char* data, std::size_t size) noexcept {
            __try {
                if (out->data || out->count || out->capacity || size > 128 * 1024) return false;
                const auto memory = reinterpret_cast<Address(__fastcall*)(std::size_t)>(base + 0x66b6a50)(size + 2);
                if (!memory) return false;
                // Transfer ownership before copying so the engine's local TArray
                // destructor also owns the allocation on an interrupted copy.
                *out = {memory, static_cast<std::int32_t>(size), static_cast<std::int32_t>(size + 2)};
                std::memcpy(reinterpret_cast<void*>(memory), data, size);
                *reinterpret_cast<std::uint16_t*>(memory + size) = 0;
                return true;
            } __except (ScriptExceptionFilter(GetExceptionInformation(), "module-allocation-copy")) { return false; }
        }
        bool __fastcall HookScriptLoad(Address loader, const StringValue* path, ScriptBytes* out) {
            if (scriptExecuting && !faulted.load() && loader == scriptLoader && LocalGameThread() &&
                ScriptStringEquals(path, ScriptModuleName, std::size(ScriptModuleName) - 1))
                {
                    scriptStage = "private-load";
                    const bool loaded = ScriptFillBytes(out, scriptSource.data(), scriptSource.size());
                    if (loaded) { scriptDataDescriptor = reinterpret_cast<Address>(out); scriptLoadedBytes = *out; }
                    return loaded;
                }
            return reinterpret_cast<ScriptLoad>(scriptSavedSlots[1].load())(loader, path, out);
        }
        bool CaptureScriptLog(Address logger, const StringValue* input, bool errorLog = false) {
            if (!scriptExecuting || faulted.load() || logger != scriptLogger || !LocalGameThread()) return false;
            StringValue value;
            if (!ReadBytes(reinterpret_cast<Address>(input), &value, sizeof(value)) || value.count < 1 || value.count > 8192 ||
                value.capacity < value.count || !value.data || Read<wchar_t>(reinterpret_cast<Address>(value.data) + (value.count - 1) * 2) != 0) return false;
            std::wstring wide(value.count - 1, L'\0');
            if (!ReadBytes(reinterpret_cast<Address>(value.data), wide.data(), wide.size() * 2)) return false;
            const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) return false;
            std::string text(bytes, '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), text.data(), bytes, nullptr, nullptr);
            constexpr std::string_view diagnosticPrefix = "__GLOSS_PBZ_LOOT_DIAG__:";
            if (text.starts_with(diagnosticPrefix)) {
                scriptLootDiagnostic = text.substr(diagnosticPrefix.size(), 256);
                gloss::Log("PBZ loot candidate skipped: " + scriptLootDiagnostic);
                return true;
            }
            if (!text.starts_with(scriptAck)) {
                if (errorLog) scriptLastError = text.substr(0, 1024);
                return false;
            }
            if (scriptAckSeen) { scriptReply = "duplicate acknowledgement"; return true; }
            scriptAckSeen = true;
            scriptStage = "acknowledgement";
            scriptReply = text.substr(scriptAck.size());
            return true;
        }
        template<std::size_t Slot> void __fastcall HookScriptLog(Address logger, const StringValue* value) {
            try { if (CaptureScriptLog(logger, value, Slot == 3)) return; } catch (...) {}
            reinterpret_cast<ScriptLog>(scriptSavedSlots[Slot + 2].load())(logger, value);
        }
        bool ScriptPrivateBytesMatch(const ScriptBytes* input) noexcept {
            if (!input || reinterpret_cast<Address>(input) != scriptDataDescriptor || !scriptLoadedBytes.data) return false;
            ScriptBytes value;
            if (!ReadBytes(scriptDataDescriptor, &value, sizeof(value)) || value.data != scriptLoadedBytes.data ||
                value.count != scriptLoadedBytes.count || value.capacity != scriptLoadedBytes.capacity ||
                value.count < 1 || static_cast<std::size_t>(value.count) != scriptSource.size() || value.capacity != value.count + 2) return false;
            std::array<char, 256> bytes{};
            for (std::size_t offset = 0; offset < scriptSource.size();) {
                const auto count = (std::min)(bytes.size(), scriptSource.size() - offset);
                if (!ReadBytes(value.data + offset, bytes.data(), count) ||
                    std::memcmp(bytes.data(), scriptSource.data() + offset, count) != 0) return false;
                offset += count;
            }
            std::uint16_t terminator{1};
            return ReadBytes(value.data + value.count, &terminator, sizeof(terminator)) && !terminator;
        }
        bool __fastcall HookScriptVerify(const StringValue* path, const ScriptBytes* data) {
            ++callbacksInFlight;
            __try {
                if (scriptExecuting && ScriptThreadReady() &&
                    ScriptStringEquals(path, ScriptModuleName, std::size(ScriptModuleName) - 1) && ScriptPrivateBytesMatch(data)) {
                    scriptStage = "private-verified";
                    scriptPrivateVerified = true;
                    return true;
                }
                // All shipped modules, other threads and non-owned buffers retain
                // the game's original manifest, length and digest checks.
                return originalScriptVerify.load()(path, data);
            } __finally { --callbacksInFlight; }
        }
        bool ScriptVerifyOriginalEntry() {
            std::array<unsigned char, ScriptVerifyEntry.size()> bytes{};
            return ReadBytes(base + ScriptVerifyRva, bytes.data(), bytes.size()) && bytes == ScriptVerifyEntry;
        }
        bool ScriptVerifyOwnership() {
            std::array<unsigned char, ScriptVerifyEntry.size()> bytes{};
            return scriptVerifyPatched && scriptVerifyPatchCaptured &&
                ReadBytes(base + ScriptVerifyRva, bytes.data(), bytes.size()) &&
                std::equal(scriptVerifyOwnedEntry.begin(), scriptVerifyOwnedEntry.end(), bytes.begin()) &&
                std::equal(bytes.begin() + scriptVerifyOwnedEntry.size(), bytes.end(), ScriptVerifyEntry.begin() + scriptVerifyOwnedEntry.size());
        }
        bool PrepareScriptVerifyHook() {
            if (!modulePinned.load() || !ScriptVerifyOriginalEntry()) return false;
            if (!scriptVerifyCreated) {
                ScriptVerify original{};
                if (MH_CreateHook(reinterpret_cast<void*>(base + ScriptVerifyRva), reinterpret_cast<void*>(&HookScriptVerify),
                    reinterpret_cast<void**>(&original)) != MH_OK) return false;
                originalScriptVerify.store(original);
                scriptVerifyCreated = true;
            }
            return true;
        }
        bool InstallScriptVerifyHook() {
            if (scriptVerifyPatched) return ScriptVerifyOwnership();
            if (!PrepareScriptVerifyHook()) return false;
            if (MH_EnableHook(reinterpret_cast<void*>(base + ScriptVerifyRva)) != MH_OK) {
                // A failed hook operation must not erase an unknown live patch.
                if (!ScriptVerifyOriginalEntry()) scriptVerifyPatched = true;
                return false;
            }
            scriptVerifyPatched = true;
            std::array<unsigned char, 16> patch{};
            if (!ReadBytes(base + ScriptVerifyRva, patch.data(), patch.size()) ||
                patch[0] != 0xe9 || !std::equal(patch.begin() + 5, patch.end(), ScriptVerifyEntry.begin() + 5) ||
                (scriptVerifyPatchCaptured && patch != scriptVerifyOwnedEntry)) return false;
            // MinHook retains this trampoline for the process lifetime. Never
            // replace the original ownership receipt with a later foreign patch.
            if (!scriptVerifyPatchCaptured) { scriptVerifyOwnedEntry = patch; scriptVerifyPatchCaptured = true; }
            return ScriptVerifyOwnership();
        }
        bool RestoreScriptVerifyHook() {
            if (!scriptVerifyPatched) return true;
            if (!ScriptVerifyOwnership()) return false;
            const auto status = MH_DisableHook(reinterpret_cast<void*>(base + ScriptVerifyRva));
            if ((status != MH_OK && status != MH_ERROR_DISABLED) || !ScriptVerifyOriginalEntry()) return false;
            scriptVerifyPatched = false;
            return true;
        }
        std::array<Address, 6> ScriptHookSlots() {
            return {base + 0xbe93e10, base + 0xbe93e18, base + 0xbe92b48,
                base + 0xbe92b50, base + 0xbe92b58, base + 0xbe92b60};
        }
        std::array<Address, 6> ScriptHookFunctions() {
            return {reinterpret_cast<Address>(&HookScriptSearch), reinterpret_cast<Address>(&HookScriptLoad),
                reinterpret_cast<Address>(&HookScriptLog<0>), reinterpret_cast<Address>(&HookScriptLog<1>),
                reinterpret_cast<Address>(&HookScriptLog<2>), reinterpret_cast<Address>(&HookScriptLog<3>)};
        }
        bool ScriptVerifierWanted(std::string_view command, std::string_view id = {}, bool enabled = false) {
            if (command != "tick" && command != "toggle") return false;
            if (command == "toggle" && !ScriptFeature(id)) return false;
            for (std::size_t i = 0; i < scriptEnabled.size(); ++i)
                if ((command == "toggle" && ScriptToggleIds[i] == id) ? enabled : scriptEnabled[i]) return true;
            return false;
        }
        bool RestoreScriptHooks(bool keepVerifier = false) {
            const auto slots = ScriptHookSlots(), hooks = ScriptHookFunctions();
            bool restored = keepVerifier ? ScriptVerifyOwnership() : RestoreScriptVerifyHook();
            for (std::size_t i = scriptPatched.size(); i-- > 0;) {
                if (!scriptPatched[i]) continue;
                if (Read<Address>(slots[i]) == scriptSavedSlots[i].load() ||
                    ReplaceSlot(slots[i], hooks[i], scriptSavedSlots[i].load())) scriptPatched[i] = false;
                else restored = false;
            }
            // A failed callback restore must not leave a feature-group lease
            // active. A foreign patch or failed disable retains its own receipt.
            if (!restored && keepVerifier) RestoreScriptVerifyHook();
            return restored;
        }
        void AbortScriptHooks() {
            if (!scriptExecuting && LocalGameThread()) {
                scriptModuleReceipt = {};
                if (!RestoreScriptHooks()) {
                    faulted.store(true);
                    GetBackend().SetRuntimeState(false, "脚本租约恢复未完成；适配器已停止");
                }
            }
        }
        bool ScriptIdentity(const Context& context, Address& env, Address& loader, Address& logger) {
            if (!ObjectValid(context.instance)) return false;
            bool gameInstance{};
            auto type = Read<Address>(context.instance + 0x10);
            for (unsigned depth = 0; type && depth < 64; ++depth, type = Read<Address>(type + 0x40)) {
                if (ObjectName(type) == "PBZGameInstance") { gameInstance = true; break; }
            }
            if (!gameInstance) return false;
            const auto wrapper = Read<Address>(context.instance + 0x670);
            env = Read<Address>(wrapper);
            if (Read<Address>(env) != base + 0xbec30c0 || Read<Address>(env + 8) != base + 0xbec3140 ||
                Read<Address>(env + 16) != base + 0xbec3240) return false;
            loader = Read<Address>(env + 0x170); logger = Read<Address>(env + 0x180);
            return Read<Address>(loader) == base + 0xbe93e10 && Read<Address>(logger) == base + 0xbe92b48 &&
                Read<Address>(env + 0x238) && Read<Address>(env + 0x240);
        }
        bool ScriptProbeCurrent(const Context& context) {
            if (!scriptProbeConfirmed || !scriptProbeContext.SameKey(context) ||
                Read<std::uint32_t>(context.instance + 0xc) != scriptProbeInstanceIndex) return false;
            Address env{}, loader{}, logger{};
            return ScriptIdentity(context, env, loader, logger) && env == scriptProbeEnv && loader == scriptProbeLoader && logger == scriptProbeLogger;
        }
        bool ScriptOwnerMatches(const Context& context, Address env, Address loader, Address logger) {
            return !scriptStateOwned || (scriptContext.SameKey(context) &&
                Read<std::uint32_t>(context.instance + 0xc) == scriptInstanceIndex &&
                env == scriptEnv && loader == scriptLoader && logger == scriptLogger);
        }
        bool ScriptModuleCurrent(const Context& context, Address env, Address loader, Address logger) {
            return scriptModuleReceipt.confirmed && scriptModuleReceipt.context.SameKey(context) &&
                Read<std::uint32_t>(context.instance + 0xc) == scriptModuleReceipt.instanceIndex &&
                env == scriptModuleReceipt.env && loader == scriptModuleReceipt.loader && logger == scriptModuleReceipt.logger;
        }
        void RecordScriptModule(const Context& context, Address env, Address loader, Address logger, bool acknowledged) {
            if (!acknowledged) { scriptModuleReceipt = {}; return; }
            scriptModuleReceipt = {true, context, env, loader, logger, Read<std::uint32_t>(context.instance + 0xc)};
        }
        template<std::size_t N> bool ScriptBodyMatches(Address rva, const std::array<unsigned char, N>& expected) {
            std::array<unsigned char, N> observed{};
            return ReadBytes(base + rva, observed.data(), observed.size()) && observed == expected;
        }
        bool ScriptBodiesValid() {
            constexpr std::array<unsigned char, 21> verifierDispatch{0x84,0xc0,0x74,0x16,0x48,0x8d,0x55,0x08,0x48,0x8d,0x4d,0xf8,0xe8,0xc1,0xa0,0x10,0x00,0x84,0xc0,0x74,0x05};
            constexpr std::array<unsigned char, 31> executeEntry{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x00,0xfe,0xff,0xff,0x48,0x81,0xec,0x00,0x03,0x00,0x00};
            constexpr std::array<unsigned char, 57> loaderDispatch{0x48,0x8b,0x8e,0x70,0x01,0x00,0x00,0x48,0x8b,0x01,0x48,0x8d,0x55,0x48,0x48,0x89,0x54,0x24,0x20,0x4c,0x8d,0x4d,0xf8,0x4c,0x8b,0xc7,0x48,0x8d,0x55,0x80,0xff,0x10,0x84,0xc0,0x74,0x6d,0x48,0x8b,0x8e,0x70,0x01,0x00,0x00,0x48,0x8b,0x01,0x4c,0x8d,0x45,0x08,0x48,0x8d,0x55,0xf8,0xff,0x50,0x08};
            constexpr std::array<unsigned char, 62> wideConstructor{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x33,0xed,0x4c,0x8d,0x79,0x08,0x48,0x89,0x29,0x4c,0x8d,0x71,0x0c,0x41,0x89,0x2f,0x48,0x8b,0xf2,0x41,0x89,0x2e,0x48,0x8b,0xd9,0x48,0x85,0xd2,0x74,0x5e,0x66,0x39,0x2a,0x74,0x59,0x48,0x8b,0xca};
            constexpr std::array<unsigned char, 64> engineAllocate{0x48,0x85,0xc9,0xb8,0x08,0x00,0x00,0x00,0x41,0xb9,0x01,0x00,0x00,0x00,0x41,0xb8,0x10,0x00,0x00,0x00,0x4c,0x0f,0x45,0xc9,0x48,0x3b,0xc8,0x48,0x8b,0x0d,0xde,0x8d,0x69,0x07,0x44,0x0f,0x46,0xc0,0x48,0x85,0xc9,0x75,0x0b,0x41,0x8b,0xd0,0x49,0x8b,0xc9,0xe9,0x2a,0x49,0xc3,0xfa,0x48,0x8b,0x01,0x49,0x8b,0xd1,0x48,0xff,0x60,0x28};
            constexpr std::array<unsigned char, 38> loaderSearch{0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe1,0x48,0x81,0xec,0x90,0x00,0x00,0x00};
            constexpr std::array<unsigned char, 36> loaderLoad{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x80,0x3d,0x06,0xf6,0xe7,0x07,0x03,0x48,0x8d,0x3d,0x47,0xf6,0x4a,0x04,0x4d,0x8b,0xf8};
            constexpr std::array<unsigned char, 50> loggerLog{0x80,0x3d,0xc9,0xe0,0xd8,0x07,0x05,0x72,0x28,0x83,0x7a,0x08,0x00,0x74,0x05,0x4c,0x8b,0x0a,0xeb,0x07,0x4c,0x8d,0x0d,0x5d,0xdd,0x3b,0x04,0x4c,0x8b,0xc1,0x48,0x8d,0x15,0xab,0xe0,0xd8,0x07,0x48,0x8d,0x0d,0x44,0x0d,0xbe,0x05,0xe9,0x5f,0xa8,0x04,0xfb,0xc3};
            constexpr std::array<unsigned char, 5> loggerInfo{0xe9,0x6b,0xb6,0x36,0x12};
            constexpr std::array<unsigned char, 16> loggerWarn{0x80,0x3d,0x99,0x66,0xd8,0x07,0x03,0x72,0x28,0x83,0x7a,0x08,0x00,0x74,0x05,0x4c};
            constexpr std::array<unsigned char, 16> loggerError{0x80,0x3d,0x89,0x32,0xd9,0x07,0x02,0x72,0x28,0x83,0x7a,0x08,0x00,0x74,0x05,0x4c};
            return (scriptVerifyPatched ? ScriptVerifyOwnership() : ScriptVerifyOriginalEntry()) && ScriptBodyMatches(0x61ec420, executeEntry) &&
                ScriptBodyMatches(0x61ec495, loaderDispatch) &&
                ScriptBodyMatches(0x61ec4ce, verifierDispatch) &&
                ScriptBodyMatches(0x127c240, wideConstructor) &&
                ScriptBodyMatches(0x66b6a50, engineAllocate) &&
                ScriptBodyMatches(0x62c6490, loaderSearch) &&
                ScriptBodyMatches(0x61fd510, loaderLoad) &&
                ScriptBodyMatches(0x62eee00, loggerLog) &&
                ScriptBodyMatches(0x62eb100, loggerInfo) &&
                ScriptBodyMatches(0x62f6830, loggerWarn) &&
                ScriptBodyMatches(0x62e9c40, loggerError);
        }
        bool ScriptCapability(const Context& context, std::string& reason) {
            reason.clear();
            if (!ScriptThreadReady()) { reason = "等待脚本游戏线程，或适配器已停止"; return false; }
            Address env{}, loader{}, logger{};
            if (!ScriptIdentity(context, env, loader, logger)) { reason = "脚本环境身份未通过本版本核对"; return false; }
            if (!ScriptBodiesValid()) { reason = "脚本桥本机函数字节契约变化"; return false; }
            constexpr std::array<Address, 6> original{0x62c6490,0x61fd510,0x62eee00,0x62eb100,0x62f6830,0x62e9c40};
            const auto slots = ScriptHookSlots();
            for (std::size_t i = 0; i < slots.size(); ++i)
                if (scriptPatched[i] || Read<Address>(slots[i]) != base + original[i]) {
                    reason = "脚本加载器已由其他模块修改"; return false;
                }
            return true;
        }
        bool CallScriptModule(Address env) noexcept {
            StringValue module{};
            module.data = const_cast<wchar_t*>(ScriptModuleName);
            module.count = module.capacity = static_cast<int>(std::size(ScriptModuleName));
            __try { reinterpret_cast<void(__fastcall*)(Address, const StringValue*)>(base + 0x61ec420)(env, &module); return true; }
            __except (ScriptExceptionFilter(GetExceptionInformation(), "ExecuteModule")) { return false; }
        }
        constexpr const char* ScriptFeatureSource =
        #include "script_features.inc"
        ;
        constexpr const char* ScriptDispatchSource = R"GLOSSDISPATCH(try{
const s=globalThis.__glossPBZ302256;
const d=s&&Object.getOwnPropertyDescriptor(s,'dispatch');
if(!s||s.version!==3||!d||d.writable||d.configurable||typeof d.value!=='function')throw Error('script module receipt missing');
const value=d.value(command,id,enabled,range);
console.log(ack+'ok'+(command==='tick'?'|'+JSON.stringify(value):''));
}catch(e){console.log(ack+'error:'+String(e&&e.message||e));})GLOSSDISPATCH";
        const char* ScriptCommandBody(std::string_view command, bool moduleCurrent) {
            if (command == "probe") return "console.log(ack+'ok');";
            if (command == "tick" && !moduleCurrent) return nullptr;
            if (command != "tick" && command != "toggle" && command != "cleanup") return nullptr;
            return moduleCurrent ? ScriptDispatchSource : ScriptFeatureSource;
        }
        bool RunScript(const Context& context, std::string_view command, std::string_view id = {}, bool enabled = false, double distanceM = 0) {
            PerformanceScope performance(10);
            std::string reason;
            if (scriptExecuting) { scriptReply = "recursive script dispatch rejected"; return false; }
            if (!ScriptThreadReady() || !ScriptCapability(context, reason)) { scriptReply = reason; AbortScriptHooks(); return false; }
            if (command != "probe" && command != "toggle" && command != "cleanup" && command != "tick") {
                scriptReply = "unknown internal command"; AbortScriptHooks(); return false;
            }
            if (command == "toggle" && !ScriptFeature(id)) { scriptReply = "unknown internal toggle"; AbortScriptHooks(); return false; }
            const bool distanceRequest = command == "toggle" && enabled && ScriptDistanceFeature(id);
            if ((distanceRequest && !ScriptDistanceValid(id, distanceM)) || (!distanceRequest && distanceM != 0)) {
                scriptReply = "invalid internal distance parameter"; return false;
            }
            if (scriptStateOwned && !scriptContext.SameKey(context)) {
                scriptReply = "previous script session requires cleanup"; AbortScriptHooks(); return false;
            }
            Address env{}, loader{}, logger{};
            if (!ScriptIdentity(context, env, loader, logger)) { AbortScriptHooks(); return false; }
            // A new Puerts environment can appear without changing Context's
            // world/controller key. A probe must not overwrite the old JS owner.
            if (!ScriptOwnerMatches(context, env, loader, logger)) {
                scriptReply = "previous script environment requires cleanup"; AbortScriptHooks(); return false;
            }
            const bool moduleCurrent = ScriptModuleCurrent(context, env, loader, logger);
            const auto commandBody = ScriptCommandBody(command, moduleCurrent);
            if (!commandBody) {
                scriptReply = "script module acknowledgement requires recovery"; AbortScriptHooks(); return false;
            }
            scriptEnv = env; scriptLoader = loader; scriptLogger = logger;
            if (command != "probe") {
                scriptContext = context;
                scriptInstanceIndex = Read<std::uint32_t>(context.instance + 0xc);
                scriptStateOwned = true;
            }
            scriptAck = std::format("__GLOSS_PBZ_ACK__:{}:", ++scriptSequence);
            scriptReply.clear();
            scriptAckSeen = false;
            scriptDataDescriptor = 0; scriptLoadedBytes = {}; scriptPrivateVerified = false;
            scriptException = {}; scriptStage = "prepare"; scriptLastError.clear();
            if (command == "probe") scriptProbeConfirmed = false;
            // All strings are internal constants. Distance is a bounded finite
            // parsed number; no player-provided source text is interpolated.
            scriptSource = std::format("(()=>{{'use strict';const command='{}',id='{}',enabled={},range={:.9g},ack='{}';\n", command, id, enabled ? "true" : "false", distanceM, scriptAck);
            scriptSource += commandBody;
            scriptSource += "\n})();";
            const auto slots = ScriptHookSlots(), hooks = ScriptHookFunctions();
            for (std::size_t i = 0; i < slots.size(); ++i) {
                constexpr std::array<Address, 6> expected{0x62c6490,0x61fd510,0x62eee00,0x62eb100,0x62f6830,0x62e9c40};
                scriptSavedSlots[i].store(base + expected[i]);
                if (!ReplaceSlot(slots[i], base + expected[i], hooks[i])) {
                    // A page-protection restore failure can report false after
                    // publishing our pointer. Retain it in the cleanup ledger.
                    scriptPatched[i] = Read<Address>(slots[i]) == hooks[i];
                    if (!RestoreScriptHooks()) {
                        faulted.store(true);
                        GetBackend().SetRuntimeState(false, "脚本回调安装中断且恢复未完成；适配器已停止");
                    }
                    scriptReply = "脚本回调未能安装"; return false;
                }
                scriptPatched[i] = true;
            }
            if (!InstallScriptVerifyHook()) {
                const bool restored = RestoreScriptHooks();
                scriptReply = "私有模块验证入口未能安装";
                gloss::Log(std::format("PBZ script bridge install failed: sequence={} verifierRestored={} callbacksRestored={}",
                    scriptSequence, !scriptVerifyPatched, restored));
                if (!restored) {
                    faulted.store(true);
                    GetBackend().SetRuntimeState(false, "脚本验证入口安装中断且恢复未完成；适配器已停止");
                }
                return false;
            }
            scriptExecuting = true;
            struct Guard {
                bool finished{};
                ~Guard() {
                    scriptExecuting = false;
                    if (!finished && !RestoreScriptHooks()) {
                        faulted.store(true);
                        GetBackend().SetRuntimeState(false, "脚本异常退出且回调恢复未完成；适配器已停止");
                    }
                }
            } guard;
            scriptStage = "execute-module";
            const bool called = CallScriptModule(scriptEnv);
            scriptExecuting = false;
            const bool acknowledged = called && !faulted.load() && scriptPrivateVerified && scriptAckSeen &&
                (scriptReply == "ok" || scriptReply.starts_with("ok|"));
            // Keep only the narrow verifier between successful feature calls.
            // TLS is false while idle, so every ordinary load still verifies.
            // Probe/cleanup/final-off/errors always restore all seven changes.
            const bool restored = RestoreScriptHooks(acknowledged && ScriptVerifierWanted(command, id, enabled));
            guard.finished = restored;
            if (!restored) { faulted.store(true); scriptReply = "脚本回调恢复未完成"; }
            if (scriptException.code) {
                MEMORY_BASIC_INFORMATION memory{};
                VirtualQuery(reinterpret_cast<void*>(scriptException.instruction), &memory, sizeof(memory));
                const auto allocation = reinterpret_cast<Address>(memory.AllocationBase);
                gloss::Log(std::format("PBZ script SEH: sequence={} command={} id={} boundary={} stage={} code=0x{:08X} ip=0x{:X} module=0x{:X} rva=0x{:X} access={} address=0x{:X} rcx=0x{:X} rdx=0x{:X} r8=0x{:X} r9=0x{:X} privateVerified={} restored={}",
                    scriptSequence, command, id, scriptException.boundary ? scriptException.boundary : "unknown",
                    scriptException.stage ? scriptException.stage : "unknown", scriptException.code, scriptException.instruction,
                    allocation, allocation ? scriptException.instruction - allocation : 0, scriptException.accessType,
                    scriptException.accessAddress, scriptException.rcx, scriptException.rdx, scriptException.r8, scriptException.r9,
                    scriptPrivateVerified, restored));
                scriptReply = std::format("脚本桥异常 0x{:08X}，阶段 {}，地址 0x{:X}；详细信息已写入日志",
                    scriptException.code, scriptException.stage ? scriptException.stage : "unknown", scriptException.instruction);
            }
            if (!called || faulted.load()) GetBackend().SetRuntimeState(false, "脚本桥异常，适配器已停用；异常地址与回调恢复状态已记录");
            const bool success = called && restored && !faulted.load() && scriptPrivateVerified && scriptAckSeen &&
                (scriptReply == "ok" || scriptReply.starts_with("ok|"));
            if (!success || command != "probe") RecordScriptModule(context, env, loader, logger, success);
            if (!success) {
                if (scriptReply.empty()) scriptReply = scriptLastError.empty() ? "脚本未返回匹配确认" : scriptLastError;
                gloss::Log(std::format("PBZ script failed: sequence={} command={} id={} called={} privateVerified={} ack={} restored={} stage={} reply={}",
                    scriptSequence, command, id, called, scriptPrivateVerified, scriptAckSeen, restored, scriptStage, scriptReply));
            } else if (command == "probe") {
                scriptProbeContext = context; scriptProbeInstanceIndex = Read<std::uint32_t>(context.instance + 0xc);
                scriptProbeEnv = scriptEnv; scriptProbeLoader = scriptLoader; scriptProbeLogger = scriptLogger;
                scriptProbeConfirmed = true;
                gloss::Log(std::format("PBZ script probe verified: sequence={} env=0x{:X} callbacks=6 verifierRestored=true", scriptSequence, scriptEnv));
            }
            return success;
        }
        ExecutionResult ScriptProbe(const Context& context) {
            return RunScript(context, "probe") ? ExecutionResult{RequestState::Verified, "脚本桥探针收到匹配 ACK，六个临时回调与验证入口已恢复；未加载功能模块"} :
                ExecutionResult{RequestState::Indeterminate, "脚本桥探针未确认：" + scriptReply};
        }
        bool ScriptCleanup();
        ExecutionResult ExecuteScript(const Context& context, const Request& request) {
            if (request.arguments.size() < 2 || !ScriptFeature(request.arguments[0]) ||
                (request.arguments[1] != "on" && request.arguments[1] != "off")) return {RequestState::Failed, "脚本开关参数无效"};
            const auto& id = request.arguments[0];
            const bool on = request.arguments[1] == "on";
            const bool distanceRequest = on && ScriptDistanceFeature(id);
            if (request.arguments.size() != (distanceRequest ? 3 : 2)) return {RequestState::Failed, "脚本距离参数数量无效"};
            double distanceM{};
            if (distanceRequest) {
                const auto& text = request.arguments[2];
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), distanceM);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !ScriptDistanceValid(id, distanceM))
                    return {RequestState::Failed, "自动拾取距离为 1 至 50 米，高亮距离为 1 至 100 米"};
            }
            // A missing acknowledgement can leave any JS flag uncertain. Clean
            // the entire old ledger before accepting another individual toggle.
            if (scriptUncertain && !ScriptCleanup())
                return {RequestState::Indeterminate, "上次脚本状态未确认且清理未完成：" + scriptReply};
            if (!ScriptProbeCurrent(context) && !RunScript(context, "probe"))
                return {RequestState::Indeterminate, "脚本前置探针未通过，未启用功能：" + scriptReply};
            const auto index = std::find(ScriptToggleIds.begin(), ScriptToggleIds.end(), request.arguments[0]) - ScriptToggleIds.begin();
            if (!RunScript(context, "toggle", id, on, distanceM)) {
                ClearLootMarkers();
                scriptUncertain = true; scriptError = scriptReply;
                return {RequestState::Indeterminate, "脚本未确认完成：" + scriptReply};
            }
            scriptEnabled[index] = on;
            if (distanceRequest) scriptDistances[index] = distanceM;
            if (distanceRequest) scriptLootDiagnostic.clear();
            scriptNextTick = 0;
            scriptUncertain = false; scriptError.clear();
            if (request.arguments[0] == "highlight_loot" && !on) ClearLootMarkers();
            return {RequestState::Verified, on ? "游戏脚本已确认开启；实际场景效果需游戏内验证" : "游戏脚本已确认关闭并恢复原方法"};
        }
        ToggleState ScriptToggleState(const Context& context, std::string_view id) {
            std::string reason;
            const bool known = !scriptUncertain && ScriptCapability(context, reason);
            if (scriptUncertain) reason = scriptError;
            const auto index = std::find(ScriptToggleIds.begin(), ScriptToggleIds.end(), id) - ScriptToggleIds.begin();
            const bool enabled = known && index < scriptEnabled.size() && scriptEnabled[index];
            if (known && enabled && id == "auto_collect" && !scriptLootDiagnostic.empty()) reason = "部分目标未通过接口契约：" + scriptLootDiagnostic;
            return {std::string(id), known, enabled, reason.empty() ? "脚本开关已确认" : reason,
                enabled && ScriptDistanceFeature(id) ? std::vector<std::string>{std::format("{:.9g}", scriptDistances[index])} : std::vector<std::string>{}};
        }
        void ScriptTick(const Context& context) {
            // All-off performs no identity reads, name scans, or script execution.
            if (std::none_of(scriptEnabled.begin(), scriptEnabled.end(), [](bool enabled) { return enabled; })) return;
            if (scriptUncertain) return;
            if (!ScriptThreadReady()) { ClearLootMarkers(); AbortScriptHooks(); return; }
            const auto now = GetTickCount64();
            if (now < scriptNextTick) return;
            scriptNextTick = now + 250;
            if (!scriptContext.SameKey(context) || !RunScript(context, "tick")) {
                ClearLootMarkers();
                AbortScriptHooks();
                scriptUncertain = true;
                scriptError = scriptReply.empty() ? "脚本会话变化，等待清理" : scriptReply;
                return;
            }
            // World loot uses game-owned screen-space widgets. No projected
            // coordinates or per-frame overlay snapshot crosses this bridge.
            if (scriptReply != "ok|[]") {
                scriptUncertain = true;
                AbortScriptHooks();
                scriptError = "原生战利品提示维护回执未通过校验";
            }
        }
        bool ScriptCleanup() {
            ClearLootMarkers();
            if (!scriptStateOwned) return RestoreScriptHooks();
            if (!ScriptThreadReady()) { AbortScriptHooks(); return false; }
            Address env{}, loader{}, logger{};
            if (!ObjectValid(scriptContext.instance) || Read<std::uint32_t>(scriptContext.instance + 0xc) != scriptInstanceIndex) {
                scriptModuleReceipt = {};
                scriptEnabled.fill(false); scriptNextTick = 0;
                scriptStateOwned = scriptUncertain = false; return RestoreScriptHooks();
            }
            if (!ScriptIdentity(scriptContext, env, loader, logger) || env != scriptEnv) { AbortScriptHooks(); return false; }
            if (!RunScript(scriptContext, "cleanup")) return false;
            scriptEnabled.fill(false); scriptNextTick = 0;
            scriptStateOwned = scriptUncertain = false; scriptError.clear();
            return true;
        }
        bool ScriptSessionChanged(const Context&) {
            const bool restored = ScriptCleanup();
            if (restored) { scriptProbeConfirmed = false; scriptModuleReceipt = {}; }
            return restored;
        }
