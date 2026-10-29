// Engine-owned FGameplayAttribute construction and lifetime, verified against
// GM_SetHealth / GM_FullHeal for the supported executable hash.
        bool AttributeEntriesValid() {
            constexpr std::array<unsigned char, 10> ctor{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
            constexpr std::array<unsigned char, 9> setter{0x40,0x53,0x48,0x83,0xec,0x70,0x48,0x8b,0xd9};
            constexpr std::array<unsigned char, 10> release{0x48,0x85,0xc9,0x74,0x2e,0x53,0x48,0x83,0xec,0x20};
            return std::memcmp(reinterpret_cast<void*>(base + 0x4c4fb60), ctor.data(), ctor.size()) == 0 &&
                std::memcmp(reinterpret_cast<void*>(base + 0x4c7a520), setter.data(), setter.size()) == 0 &&
                std::memcmp(reinterpret_cast<void*>(base + 0x12e1930), release.data(), release.size()) == 0;
        }
        bool SetEngineAttribute(Address component, Address property, float value) noexcept {
            alignas(8) std::array<std::byte, 56> descriptor{};
            bool success{};
            __try {
                reinterpret_cast<void*(__fastcall*)(void*, void*)>(base + 0x4c4fb60)(
                    descriptor.data(), reinterpret_cast<void*>(property));
                if (Read<Address>(reinterpret_cast<Address>(descriptor.data()) + 0x10) == property) {
                    reinterpret_cast<void(__fastcall*)(void*, const void*, float)>(base + 0x4c7a520)(
                        reinterpret_cast<void*>(component), descriptor.data(), value);
                    success = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { success = false; }
            // The game frees FFieldPath storage at +0x20 and FString at +0x00.
            // No copied/made-up descriptors or CRT frees may cross this boundary.
            __try {
                for (const auto offset : {0x20u, 0u}) {
                    const auto allocation = Read<Address>(reinterpret_cast<Address>(descriptor.data()) + offset);
                    if (allocation) reinterpret_cast<void(__fastcall*)(void*)>(base + 0x12e1930)(reinterpret_cast<void*>(allocation));
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { success = false; }
            return success;
        }
        bool PlayerAttributesReady(const Context& context) {
            const auto type = Read<Address>(context.pawn + 0x10);
            const auto component = Read<Address>(context.pawn + 0x750);
            const auto attributes = Read<Address>(context.pawn + 0x758);
            return LocalGameThread() && !stopping.load() && !faulted.load() && AttributeEntriesValid() &&
                PropertyLayout(FindProperty(type, "AbilitySystemComponent"), "ObjectProperty", 8, 0x750) &&
                PropertyLayout(FindProperty(type, "AttributeSetBase"), "ObjectProperty", 8, 0x758) &&
                ObjectValid(component) && ClassNamed(Read<Address>(component + 0x10), "PBZAbilitySystemComponent") &&
                Read<Address>(component + 0x20) == context.pawn && ObjectValid(attributes) &&
                ObjectName(Read<Address>(attributes + 0x10)) == "BasicAttributeSet" &&
                Read<Address>(attributes + 0x20) == context.pawn;
        }
        bool MaxHealthReady(const Context& context) {
            return PlayerAttributesReady(context) && ReadHealth(context).has_value();
        }
        struct AttributeIdentity {
            Address pawn{}, component{}, attributes{};
            std::uint32_t pawnIndex{}, componentIndex{}, attributeIndex{};
        };
        AttributeIdentity CaptureAttributeIdentity(const Context& context) {
            const auto component = Read<Address>(context.pawn + 0x750), attributes = Read<Address>(context.pawn + 0x758);
            return {context.pawn, component, attributes, Read<std::uint32_t>(context.pawn + 0xc),
                Read<std::uint32_t>(component + 0xc), Read<std::uint32_t>(attributes + 0xc)};
        }
        bool AttributeIdentityCurrent(const Context& context, const AttributeIdentity& identity) {
            const auto current = CurrentContext();
            if (!current || current->Key() != context.Key() || !PlayerAttributesReady(*current)) return false;
            const auto observed = CaptureAttributeIdentity(*current);
            return observed.pawn == identity.pawn && observed.pawnIndex == identity.pawnIndex &&
                observed.component == identity.component && observed.componentIndex == identity.componentIndex &&
                observed.attributes == identity.attributes && observed.attributeIndex == identity.attributeIndex;
        }
        ExecutionResult SetMaxHealth(const Context& context, const Request& request) {
            float target{};
            if (request.arguments.size() != 1) return {RequestState::Failed, "最大生命参数数量错误"};
            const auto& text = request.arguments.front();
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), target);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                !std::isfinite(target) || target < 1 || target > 100000000 || !MaxHealthReady(context))
                return {RequestState::Failed, "最大生命参数或玩家属性契约无效，未执行"};
            const auto attributes = Read<Address>(context.pawn + 0x758);
            const auto type = Read<Address>(attributes + 0x10);
            const auto component = Read<Address>(context.pawn + 0x750);
            const auto identity = CaptureAttributeIdentity(context);
            if (!SetEngineAttribute(component, FindProperty(type, "Health_Max"), target)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "最大生命调用异常，适配器停止");
                return {RequestState::Indeterminate, "最大生命调用异常，结果无法确认"};
            }
            if (!AttributeIdentityCurrent(context, identity))
                return {RequestState::Indeterminate, "上限调用后玩家属性对象变化，未继续回血"};
            auto afterMaximum = ReadHealth(context);
            if (!afterMaximum || afterMaximum->maximum < 1)
                return {RequestState::Indeterminate, "上限调用已返回，但玩家或生命读回失效，未继续回血"};
            const auto close = [](float a, float b) { return std::abs(a - b) <= (std::max)(0.01f, std::abs(b) * 1e-5f); };
            std::optional<float> previousBase, previousActual;
            for (int correction = 0; correction < 2 && !close(afterMaximum->maximum, target); ++correction) {
                const auto currentBase = Read<float>(attributes + 0x70);
                const auto adjusted = CorrectedAttributeBase(currentBase, afterMaximum->maximum, target, previousBase, previousActual);
                if (!adjusted) break;
                previousBase = currentBase;
                previousActual = afterMaximum->maximum;
                if (!SetEngineAttribute(component, FindProperty(type, "Health_Max"), *adjusted)) {
                    faulted.store(true);
                    GetBackend().SetRuntimeState(false, "最大生命修正调用异常，适配器停止");
                    return {RequestState::Indeterminate, "上限修正调用异常，未继续回血"};
                }
                ++dispatchedCalls;
                if (!AttributeIdentityCurrent(context, identity))
                    return {RequestState::Indeterminate, "上限修正期间玩家属性对象变化，未继续回血"};
                afterMaximum = ReadHealth(context);
                if (!afterMaximum || afterMaximum->maximum < 1)
                    return {RequestState::Indeterminate, "上限修正后的生命属性不可读，未继续回血"};
            }
            if (!SetEngineAttribute(component, FindProperty(type, "Health"), afterMaximum->maximum)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "恢复生命调用异常，适配器停止");
                return {RequestState::Indeterminate, "已设置上限，恢复生命调用异常"};
            }
            dispatchedCalls += 2;
            if (!AttributeIdentityCurrent(context, identity))
                return {RequestState::Indeterminate, "生命调用后玩家属性对象变化，结果无法确认"};
            const auto finalContext = CurrentContext();
            if (!finalContext || finalContext->Key() != context.Key())
                return {RequestState::Indeterminate, "生命调用后玩家变化，结果无法确认"};
            const auto health = ReadHealth(*finalContext);
            PublishReadback(health, &*finalContext);
            if (!health) return {RequestState::Indeterminate, "生命调用已返回，实际属性未读回"};
            if (close(health->maximum, target) && close(health->current, health->maximum))
                return {RequestState::Verified, std::format("最大生命已设为 {:.1f}，生命已恢复 {:.1f} / {:.1f}", target, health->current, health->maximum)};
            return {RequestState::Dispatched, std::format("生命已读回 {:.1f} / {:.1f}；请求上限 {:.1f}，可能受游戏属性修正影响", health->current, health->maximum, target)};
        }
