R"GLOSSJS(
try {
    const key = '__glossPBZ302256';
    let state = globalThis[key];
    if (state && state.version !== 3) throw Error('private script state contract changed');
    if (!state) {
        const UE = require('ue'), puerts = require('puerts');
        // Puerts rejects a released proxy before UFUNCTION IsValid can return
        // false. Match the game's isNativeObjectAlive guard for this exact error.
        // Other binding/engine errors must still reach the diagnostic receipt.
        const valid = function(object) {
            if (!object) return false;
            try { return UE.KismetSystemLibrary.IsValid(object); }
            catch (error) {
                if (error && error.message==='passing a invalid object') return false;
                throw error;
            }
        };
        state = {version:3,free_teleport:false,unrestricted_teleport:false,auto_collect:false,highlight_loot:false,
            leaseUntil:0,map:null,autoDistanceM:10,highlightDistanceM:50,lootPawn:'',lootComponent:null,targetingAt:0,targetingPool:null,targetingCount:0,targetingCursor:0,targetingRevision:0,mapVisited:0,targetVisited:0,mapSweep:false,targetSweep:false,lootDiagnostics:new Set(),attempted:new Set(),retryAfter:new Map(),
            scanCursor:0,scanMaximum:0,scanSeen:new Set(),lootEntries:new Map(),nativeHighlights:new Map(),nativeHighlightAssets:null,nativeHighlightTimer:0};
        const active = id => state[id] && Date.now() <= state.leaseUntil;
        state.ensureMap = function() {
            if (state.map) return;
            const prototype = require('UI/Widget/Map').MapInterface.prototype;
            const helperModule = require('Library/TsHelper');
            const methods = ['runTeleportGates','confirmAndConsumeCharm','isTeleportAllowed',
                'isTeleportBlockedByStory','isTeleportBlockedByFlowCriticalSection'];
            const originals = Object.create(null), installed = Object.create(null);
            for (const method of methods) {
                if (typeof prototype[method] !== 'function') throw Error('missing map method '+method);
                originals[method] = prototype[method];
            }
            // Generated from the captured v1.1.2 runTeleportGates; only quantity changes.
            // map-source.js SHA256: 818ce07aa6fe661509c89459d5fb28134adb797c11b7ec3b27a973b464eba9f5
            // Normalize only CRLF from source capture; all method text remains exact.
            const expectedGate =
                "runTeleportGates(targetBellID) {\n" +
                "        // gate 1\uff1a\u5148\u5224\u65ad\u7a7f\u884c\u7b26\u6570\u91cf\u3002\u8eab\u4e0a\u6ca1\u6709\u7a7f\u884c\u7b26\u76f4\u63a5\u62e6\uff0c\u540e\u9762\u7684\u72b6\u6001/\u4f4d\u7f6e\u68c0\u67e5\u90fd\u4e0d\u8dd1\uff08\u907f\u514d\"\u6ca1\u7b26\u5374\u5148\u62a5\u6218\u6597\u4e2d/\u9644\u8fd1\u6709\u94c3\u94db\"\u7b49\u8bef\u5bfc\uff09\u3002\n" +
                "        let backpack = UE.PBZBackPackSubsystem.GetBackPackSubsytemByContextObject(this);\n" +
                "        if (!backpack)\n" +
                "            return false;\n" +
                "        let ownedRef = (0, puerts_1.$ref)(0);\n" +
                "        let maxRef = (0, puerts_1.$ref)(0);\n" +
                "        backpack.GetItemNumInfo(TeleportCharmConfig.ItemID, ownedRef, maxRef);\n" +
                "        if ((0, puerts_1.$unref)(ownedRef) <= 0) {\n" +
                "            this.showTeleportTip(\"UI_Map_37\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        let character = UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(this);\n" +
                "        if (!character)\n" +
                "            return false;\n" +
                "        let asc = UE.AbilitySystemBlueprintLibrary.GetAbilitySystemComponent(character);\n" +
                "        if (!asc)\n" +
                "            return false;\n" +
                "        // gate 2\uff1a\u5fc5\u987b\u662f Walking \u6216\u884c\u8239\u4e2d\u3002Falling/Flying/Swimming/NavWalking \u7b49\u90fd\u62d2\u7edd\u3002\n" +
                "        // \u884c\u8239\u7279\u4f8b\uff1a\u73a9\u5bb6\u88ab attach \u5230 boat\uff0cMovementMode \u4e0d\u662f Walking\uff1bdoCharmTeleport \u4f1a\u8c03 ForceEndShipNavigation \u5f3a\u5236\u4e0b\u8239\u518d\u4f20\uff0c\u6240\u4ee5\u8fd9\u91cc\u653e\u884c\u3002\n" +
                "        let movement = character.CharacterMovement;\n" +
                "        let isWalking = movement && movement.MovementMode === UE.EMovementMode.MOVE_Walking;\n" +
                "        let isShipNav = asc.HasMatchingGameplayTag(TsHelper_1.default.constructGameplayTag(\"CharacterState.ShipNavigation\"));\n" +
                "        if (!isWalking && !isShipNav) {\n" +
                "            this.showTeleportTip(\"UI_Map_33\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        if (asc.HasMatchingGameplayTag(TsHelper_1.default.constructGameplayTag(\"CharacterState.InCombat\"))) {\n" +
                "            this.showTeleportTip(\"UI_Map_34\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // \u884c\u8239\u72b6\u6001\u4e0d\u518d\u62e6\u622a\uff1adoCharmTeleport \u91cc\u8c03 ForceEndShipNavigation \u5f3a\u5236\u4e0b\u8239\u518d\u4f20\u3002\n" +
                "        // \u4e0d\u80fd\u76f4\u63a5\u590d\u7528 UIHelper.canOpenSystemUI\uff08\u5b83\u5185\u90e8\u628a ShipNavigation \u89c6\u4e3a false\uff09\uff0c\n" +
                "        // \u7528\u672c\u5730 canTeleportBaseState \u590d\u523b\u540c\u4e00\u7ec4\u62e6\u622a tag \u4f46\u53bb\u6389 ShipNavigation\u3002\n" +
                "        if (!this.canTeleportBaseState()) {\n" +
                "            this.showTeleportTip(\"UI_Map_34\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        let bellManager = TsHelper_1.default.getWorldSubsystem(this, UE.BellManager.StaticClass());\n" +
                "        if (!bellManager)\n" +
                "            return false;\n" +
                "        if (bellManager.IsInBrokenBellState()) {\n" +
                "            this.showTeleportTip(\"UI_Map_35\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // \u955c\u82b1\u6c34\u6708\uff08jhsy\uff09\u4e5f\u7b97\u5f02\u7a7a\u95f4\uff0c\u590d\u7528 UI_Map_35\n" +
                "        if (TsHelper_1.default.isInJhsy(this)) {\n" +
                "            this.showTeleportTip(\"UI_Map_35\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // \u73a9\u5bb6\u6700\u540e\u5b58\u6863\u6765\u81ea SaveTriggerBox\uff08\u5373 checkpoint\uff09\u4f46\u8fd8\u6ca1\u6fc0\u6d3b\u4efb\u4f55\u94c3\u94db \u2192 \u6ca1\"\u5b89\u5168\u951a\u70b9\"\uff0c\u7981\u6b62\u4f20\u9001\n" +
                "        // \u6fc0\u6d3b\u4efb\u610f\u94c3\u94db\u540e\u7531 BellManager.AddActiveBell \u81ea\u52a8\u6e05\u6b64 flag\n" +
                "        // as any\uff1aIsSavedFromTriggerBoxOnly \u662f\u65b0\u52a0\u65b9\u6cd5\uff0cPuerTS \u53cd\u5c04 .d.ts \u9700 C++ \u91cd\u7f16 + Generator \u624d\u66f4\u65b0\n" +
                "        if (bellManager.IsSavedFromTriggerBoxOnly()) {\n" +
                "            this.showTeleportTip(\"UI_Map_33\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // \u7981\u4f20 Volume \u8d70 PBZLevelActorManager \u7edf\u4e00\u7ba1\u7406\uff08Volume \u5728 BeginPlay \u8c03 AddActor \u6ce8\u518c\u81ea\u5df1\uff09\n" +
                "        let levelActorManager = TsHelper_1.default.getWorldSubsystem(this, UE.PBZLevelActorManager.StaticClass());\n" +
                "        let playerLoc = character.K2_GetActorLocation();\n" +
                "        // as any\uff1aPBZLevelActorManager.IsTeleportBlockedAt \u662f CL 188631 \u52a0\u7684\u65b9\u6cd5\uff0cPuerTS \u53cd\u5c04 .d.ts \u9700\u8981 C++ \u91cd\u7f16 + Generator \u624d\u66f4\u65b0\n" +
                "        if (levelActorManager && levelActorManager.IsTeleportBlockedAt(playerLoc)) {\n" +
                "            this.showTeleportTip(\"UI_Map_39\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // flow \u5173\u952e\u6bb5\uff1a\u5206\u652f\u91cc\u8fd8\u6709\u300c\u6709\u540e\u679c\u300d\u7684\u4e8b\u6ca1\u505a\u5b8c\u65f6\u7981\u4f20\uff0c\u63d0\u793a\u540c\u7981\u4f20 Volume\uff1b\u6263\u7b26\u524d\u5728 confirmAndConsumeCharm \u91cc\u518d\u5224\u4e00\u6b21\n" +
                "        if (this.isTeleportBlockedByFlowCriticalSection(FlowCriticalSectionSource.Charm, false)) {\n" +
                "            this.showTeleportTip(\"UI_Map_39\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // \u9644\u8fd1\u94c3\u94db\u68c0\u67e5\uff1aRule #1 \u5148\u7269\u5316 BellID \u5217\u8868\uff0c\u907f\u514d\u5faa\u73af\u5185 GetSaveActorByID \u89e6\u53d1 GC \u65f6\u8de8\u8fb9\u754c\u6301 FName\n" +
                "        let gameSystem = TsHelper_1.default.getWorldSubsystem(this, UE.PBZGameSystem.StaticClass());\n" +
                "        if (gameSystem) {\n" +
                "            let activeBellIDs = bellManager.ActivateBellIDs;\n" +
                "            let bellNum = activeBellIDs.Num();\n" +
                "            let bellIDList = [];\n" +
                "            for (let i = 0; i < bellNum; i++) {\n" +
                "                if (activeBellIDs.IsValidIndex(i))\n" +
                "                    bellIDList.push(activeBellIDs.Get(i).toString());\n" +
                "            }\n" +
                "            for (const bellID of bellIDList) {\n" +
                "                let bellActor = gameSystem.GetSaveActorByID(bellID);\n" +
                "                if (!bellActor)\n" +
                "                    continue;\n" +
                "                let bellLoc = bellActor.K2_GetActorLocation();\n" +
                "                if (playerLoc.op_Subtraction(bellLoc).Size() <= TeleportCharmConfig.NearbyBellRadius) {\n" +
                "                    this.showTeleportTip(\"UI_Map_36\");\n" +
                "                    return false;\n" +
                "                }\n" +
                "            }\n" +
                "        }\n" +
                "        // gate 10\uff1a\u89e3\u6790\u76ee\u6807\u94c3\u94db\u6240\u5c5e mapID\uff08\u4f9b\u4e0b\u9762\u767d\u540d\u5355 / \u5267\u60c5\u9501\u4f7f\u7528\uff09\u3002\n" +
                "        //   \u8de8\u5927\u533a\u9650\u5236\u5df2\u53d6\u6d88\uff1a\u7a7f\u884c\u7b26\u53ef\u4f20\u5230\u4efb\u610f\u5927\u533a\u7684\u5df2\u6fc0\u6d3b\u94c3\u94db\uff08\u8de8\u56fe\u7531 doCharmTeleport\n" +
                "        //   \u67e5 DT_MapData[targetMapID].LevelName \u540e\u4ea4\u7ed9 TeleportToTargetPoint \u7684\u8de8\u56fe\u5206\u652f\u5904\u7406\uff09\u3002\n" +
                "        //   mapID \u4f18\u5148\u7528 BellActor.location \u67e5\uff08actor \u5728\u573a\u666f\u91cc\u66f4\u51c6\uff09\uff0cactor \u4e0d\u5b58\u5728\u518d fallback \u67e5 DT_BellData.MapID\u3002\n" +
)GLOSSJS"
R"GLOSSJS(                "        //   \u4e24\u8def\u90fd\u89e3\u6790\u4e0d\u51fa\u5c31\u62e6\uff1alevelName \u4f1a\u4e3a\u7a7a\uff0cTeleportToTargetPoint \u628a\u7a7a levelName \u5f53\u8de8\u56fe\u5904\u7406\u4f1a\u4f20\u5230\u4e0d\u5b58\u5728\u7684\u5173\u5361\u3002\n" +
                "        let mapSubsystem = TsHelper_1.default.getWorldSubsystem(this, UE.MapSubsystem.StaticClass());\n" +
                "        if (!mapSubsystem)\n" +
                "            return false;\n" +
                "        let targetMapID = \"\";\n" +
                "        if (gameSystem) {\n" +
                "            let targetBellActor = gameSystem.GetSaveActorByID(targetBellID);\n" +
                "            if (targetBellActor) {\n" +
                "                targetMapID = mapSubsystem.GetMapIDByLocation(targetBellActor.K2_GetActorLocation()).toString().toLowerCase();\n" +
                "            }\n" +
                "        }\n" +
                "        if (!targetMapID) {\n" +
                "            let targetBellData = UE.BellManager.GetBellData(targetBellID);\n" +
                "            targetMapID = targetBellData ? targetBellData.MapID.toString().toLowerCase() : \"\";\n" +
                "        }\n" +
                "        if (!targetMapID) {\n" +
                "            this.showTeleportTip(\"UI_Map_31\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        if (!this.isTeleportAllowed(targetMapID)) {\n" +
                "            this.showTeleportTip(\"UI_Map_31\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        // \u5267\u60c5\u4f20\u9001\u9501\uff08flow tag\uff09\uff1a\u547d\u4e2d\u5219\u7981\u4f20\n" +
                "        if (this.isTeleportBlockedByStory()) {\n" +
                "            this.showTeleportTip(\"UI_Map_31\");\n" +
                "            return false;\n" +
                "        }\n" +
                "        return true;\n" +
                "    }";
            if (originals.runTeleportGates.toString().replace(/\r\n/g, "\n") !== expectedGate)
                throw Error('teleport gate source contract changed');
            const puerts_1=puerts, TsHelper_1=helperModule;
            const TeleportCharmConfig={NearbyBellRadius:1000,ItemID:'Item_Traversing'};
            const FlowCriticalSectionSource={Charm:0,Bell:1};
            const freeGates = function runTeleportGates(targetBellID) {
        // gate 1：先判断穿行符数量。身上没有穿行符直接拦，后面的状态/位置检查都不跑（避免"没符却先报战斗中/附近有铃铛"等误导）。
        let backpack = UE.PBZBackPackSubsystem.GetBackPackSubsytemByContextObject(this);
        if (!backpack)
            return false;
        let ownedRef = (0, puerts_1.$ref)(0);
        let maxRef = (0, puerts_1.$ref)(0);
        backpack.GetItemNumInfo(TeleportCharmConfig.ItemID, ownedRef, maxRef);
        if (!active("free_teleport") && (0, puerts_1.$unref)(ownedRef) <= 0) {
            this.showTeleportTip("UI_Map_37");
            return false;
        }
        let character = UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(this);
        if (!character)
            return false;
        let asc = UE.AbilitySystemBlueprintLibrary.GetAbilitySystemComponent(character);
        if (!asc)
            return false;
        // gate 2：必须是 Walking 或行船中。Falling/Flying/Swimming/NavWalking 等都拒绝。
        // 行船特例：玩家被 attach 到 boat，MovementMode 不是 Walking；doCharmTeleport 会调 ForceEndShipNavigation 强制下船再传，所以这里放行。
        let movement = character.CharacterMovement;
        let isWalking = movement && movement.MovementMode === UE.EMovementMode.MOVE_Walking;
        let isShipNav = asc.HasMatchingGameplayTag(TsHelper_1.default.constructGameplayTag("CharacterState.ShipNavigation"));
        if (!isWalking && !isShipNav) {
            this.showTeleportTip("UI_Map_33");
            return false;
        }
        if (asc.HasMatchingGameplayTag(TsHelper_1.default.constructGameplayTag("CharacterState.InCombat"))) {
            this.showTeleportTip("UI_Map_34");
            return false;
        }
        // 行船状态不再拦截：doCharmTeleport 里调 ForceEndShipNavigation 强制下船再传。
        // 不能直接复用 UIHelper.canOpenSystemUI（它内部把 ShipNavigation 视为 false），
        // 用本地 canTeleportBaseState 复刻同一组拦截 tag 但去掉 ShipNavigation。
        if (!this.canTeleportBaseState()) {
            this.showTeleportTip("UI_Map_34");
            return false;
        }
        let bellManager = TsHelper_1.default.getWorldSubsystem(this, UE.BellManager.StaticClass());
        if (!bellManager)
            return false;
        if (bellManager.IsInBrokenBellState()) {
            this.showTeleportTip("UI_Map_35");
            return false;
        }
        // 镜花水月（jhsy）也算异空间，复用 UI_Map_35
        if (TsHelper_1.default.isInJhsy(this)) {
            this.showTeleportTip("UI_Map_35");
            return false;
        }
        // 玩家最后存档来自 SaveTriggerBox（即 checkpoint）但还没激活任何铃铛 → 没"安全锚点"，禁止传送
        // 激活任意铃铛后由 BellManager.AddActiveBell 自动清此 flag
        // as any：IsSavedFromTriggerBoxOnly 是新加方法，PuerTS 反射 .d.ts 需 C++ 重编 + Generator 才更新
        if (bellManager.IsSavedFromTriggerBoxOnly()) {
            this.showTeleportTip("UI_Map_33");
            return false;
        }
        // 禁传 Volume 走 PBZLevelActorManager 统一管理（Volume 在 BeginPlay 调 AddActor 注册自己）
        let levelActorManager = TsHelper_1.default.getWorldSubsystem(this, UE.PBZLevelActorManager.StaticClass());
        let playerLoc = character.K2_GetActorLocation();
        // as any：PBZLevelActorManager.IsTeleportBlockedAt 是 CL 188631 加的方法，PuerTS 反射 .d.ts 需要 C++ 重编 + Generator 才更新
        if (levelActorManager && levelActorManager.IsTeleportBlockedAt(playerLoc)) {
            this.showTeleportTip("UI_Map_39");
            return false;
        }
        // flow 关键段：分支里还有「有后果」的事没做完时禁传，提示同禁传 Volume；扣符前在 confirmAndConsumeCharm 里再判一次
        if (this.isTeleportBlockedByFlowCriticalSection(FlowCriticalSectionSource.Charm, false)) {
            this.showTeleportTip("UI_Map_39");
            return false;
        }
        // 附近铃铛检查：Rule #1 先物化 BellID 列表，避免循环内 GetSaveActorByID 触发 GC 时跨边界持 FName
        let gameSystem = TsHelper_1.default.getWorldSubsystem(this, UE.PBZGameSystem.StaticClass());
        if (gameSystem) {
            let activeBellIDs = bellManager.ActivateBellIDs;
            let bellNum = activeBellIDs.Num();
            let bellIDList = [];
            for (let i = 0; i < bellNum; i++) {
                if (activeBellIDs.IsValidIndex(i))
                    bellIDList.push(activeBellIDs.Get(i).toString());
            }
            for (const bellID of bellIDList) {
                let bellActor = gameSystem.GetSaveActorByID(bellID);
                if (!bellActor)
                    continue;
                let bellLoc = bellActor.K2_GetActorLocation();
                if (playerLoc.op_Subtraction(bellLoc).Size() <= TeleportCharmConfig.NearbyBellRadius) {
                    this.showTeleportTip("UI_Map_36");
                    return false;
                }
            }
        }
        // gate 10：解析目标铃铛所属 mapID（供下面白名单 / 剧情锁使用）。
        //   跨大区限制已取消：穿行符可传到任意大区的已激活铃铛（跨图由 doCharmTeleport
        //   查 DT_MapData[targetMapID].LevelName 后交给 TeleportToTargetPoint 的跨图分支处理）。
        //   mapID 优先用 BellActor.location 查（actor 在场景里更准），actor 不存在再 fallback 查 DT_BellData.MapID。
        //   两路都解析不出就拦：levelName 会为空，TeleportToTargetPoint 把空 levelName 当跨图处理会传到不存在的关卡。
        let mapSubsystem = TsHelper_1.default.getWorldSubsystem(this, UE.MapSubsystem.StaticClass());
        if (!mapSubsystem)
            return false;
        let targetMapID = "";
        if (gameSystem) {
            let targetBellActor = gameSystem.GetSaveActorByID(targetBellID);
            if (targetBellActor) {
                targetMapID = mapSubsystem.GetMapIDByLocation(targetBellActor.K2_GetActorLocation()).toString().toLowerCase();
            }
        }
        if (!targetMapID) {
            let targetBellData = UE.BellManager.GetBellData(targetBellID);
            targetMapID = targetBellData ? targetBellData.MapID.toString().toLowerCase() : "";
        }
        if (!targetMapID) {
            this.showTeleportTip("UI_Map_31");
            return false;
        }
        if (!this.isTeleportAllowed(targetMapID)) {
            this.showTeleportTip("UI_Map_31");
            return false;
        }
        // 剧情传送锁（flow tag）：命中则禁传
        if (this.isTeleportBlockedByStory()) {
            this.showTeleportTip("UI_Map_31");
            return false;
        }
        return true;
    };
            const targetValid = function(owner,target) {
                if (!valid(UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(owner))) return false;
                const data = UE.BellManager.GetBellData(target);
                if (!data || !data.MapID || !data.MapID.toString()) return false;
                const bells = helperModule.default.getWorldSubsystem(owner,UE.BellManager.StaticClass());
                if (!valid(bells)) return false;
                const ids=bells.ActivateBellIDs, count=ids.Num();
                if (!Number.isInteger(count) || count<0 || count>4096) return false;
                for (let i=0;i<count;++i) if (ids.IsValidIndex(i) && ids.Get(i).toString()===target.toString()) return true;
                return false;
            };
            installed.runTeleportGates = function(target) {
                if (!active('unrestricted_teleport')) return active('free_teleport') ?
                    freeGates.call(this,target) : originals.runTeleportGates.call(this,target);
                if (!targetValid(this,target)) return false;
                const pack = UE.PBZBackPackSubsystem.GetBackPackSubsytemByContextObject(this);
                if (!valid(pack)) return false;
                if (!active('free_teleport')) {
                    const count=puerts.$ref(0), max=puerts.$ref(0);
                    pack.GetItemNumInfo('Item_Traversing',count,max);
                    if (puerts.$unref(count)<=0) { this.showTeleportTip('UI_Map_37'); return false; }
)GLOSSJS"
R"GLOSSJS(                }
                return true;
            };
            installed.confirmAndConsumeCharm = function(target) {
                if (!active('free_teleport')) return originals.confirmAndConsumeCharm.call(this,target);
                if (!targetValid(this,target) || !this.runTeleportGates(target)) return;
                if (!active('unrestricted_teleport') && this.isTeleportBlockedByFlowCriticalSection(0,true)) {
                    this.showTeleportTip('UI_Map_39'); return;
                }
                this.doCharmTeleport(target);
            };
            installed.isTeleportAllowed = function(...args) {
                return active('unrestricted_teleport') ? true : originals.isTeleportAllowed.apply(this,args);
            };
            installed.isTeleportBlockedByStory = function(...args) {
                return active('unrestricted_teleport') ? false : originals.isTeleportBlockedByStory.apply(this,args);
            };
            installed.isTeleportBlockedByFlowCriticalSection = function(...args) {
                return active('unrestricted_teleport') ? false : originals.isTeleportBlockedByFlowCriticalSection.apply(this,args);
            };
            state.map={prototype,methods,originals,installed};
        };
        state.syncMap = function() {
            const wanted=state.free_teleport || state.unrestricted_teleport;
            if (wanted) state.ensureMap();
            if (!state.map) return;
            const {prototype,methods,originals,installed}=state.map;
            // Preflight every slot; never replace a method another plugin owns.
            for (const method of methods) if (prototype[method]!==originals[method] && prototype[method]!==installed[method])
                throw Error('map method replaced externally '+method);
            const changed=[];
            try {
                for (const method of methods) {
                    const before=prototype[method], next=wanted?installed[method]:originals[method];
                    if (before===next) continue;
                    prototype[method]=next; changed.push({method,before,next});
                    if (prototype[method]!==next) throw Error('map method assignment refused '+method);
                }
            } catch(error) {
                for (let i=changed.length;i-- > 0;) {
                    const c=changed[i];
                    if (prototype[c.method]===c.next) prototype[c.method]=c.before;
                }
                throw error;
            }
        };
        const gameInstance = function() {
            const gi=puerts.argv && puerts.argv.getByName('GameInstance');
            if (!valid(gi)) throw Error('GameInstance argv unavailable');
            return gi;
        };
        const lootContext = function() {
            const gi=gameInstance(), pawn=UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(gi);
            if (!valid(pawn)) return null;
            const component=UE.PBZInteractComponent.GetInteractComponent(pawn);
            if (!valid(component) || component.GetOwner()!==pawn) return null;
            return {gi,pawn,component};
        };
)GLOSSJS"
R"GLOSSJS(        // The game's V-key collection entry and its original art. Each entry is
        // hosted by an owned screen-space component: Unreal updates projection
        // every frame without trainer overlay geometry or per-frame JS calls.
        // Same-build runtime evidence: output/pbz-native-highlight-20261105/native-art.json.
        state.releaseNativeHighlight = function(effect) {
            const component=effect.component;
            if (!valid(component)) return true;
            // Never destroy a replacement component or another widget's host.
            const boundWidget=component.GetWidget();
            if (component.GetOwner()!==effect.actor ||
                (effect.componentName!==null && UE.KismetSystemLibrary.GetPathName(component)!==effect.componentName) ||
                (boundWidget!==effect.widget && (effect.bound || boundWidget!=null))) return true;
            // Hide first, including on a refused destruction. Failed owned
            // hosts remain in the ledger and are retried by the lease guard.
            effect.retiring=true;
            try { if (valid(effect.widget)) effect.widget.SetVisibility(UE.ESlateVisibility.Collapsed); } catch (_) {}
            try { component.SetVisibility(false,false); } catch (_) {}
            try { component.K2_DestroyComponent(effect.actor); } catch (_) {}
            return !valid(component);
        };
        state.clearNativeHighlights = function() {
            if (state.nativeHighlightTimer) {
                clearTimeout(state.nativeHighlightTimer); state.nativeHighlightTimer=0;
            }
            for (const [name,effect] of state.nativeHighlights) {
                try { if (state.releaseNativeHighlight(effect)) state.nativeHighlights.delete(name); }
                catch (_) { /* Other owned entries still need cleanup. Retry below. */ }
            }
            if (state.nativeHighlights.size) state.armNativeHighlightGuard();
        };
        state.armNativeHighlightGuard = function() {
            if (state.nativeHighlightTimer || !state.nativeHighlights.size) return;
            // Puerts timers run on the game thread, including when the C++
            // maintenance lease stops renewing. No worker calls Unreal here.
            state.nativeHighlightTimer=setTimeout(function() {
                state.nativeHighlightTimer=0;
                if (!active('highlight_loot')) state.clearNativeHighlights();
                else state.armNativeHighlightGuard();
            },500);
        };
        state.ensureNativeHighlight = function(context) {
            if (typeof setTimeout!=='function' || typeof clearTimeout!=='function' ||
                !UE.WidgetComponent || typeof UE.WidgetComponent.StaticClass!=='function' ||
                !UE.WidgetBlueprintLibrary || typeof UE.WidgetBlueprintLibrary.Create!=='function' ||
                !UE.Class || typeof UE.Class.Load!=='function' ||
                !UE.Texture2D || typeof UE.Texture2D.Load!=='function' ||
                !UE.MaterialInstanceConstant || typeof UE.MaterialInstanceConstant.Load!=='function' ||
                !UE.EWidgetSpace || !Number.isInteger(UE.EWidgetSpace.Screen) ||
                !UE.ECollisionEnabled || !Number.isInteger(UE.ECollisionEnabled.NoCollision) ||
                typeof context.pawn.AddComponentByClass!=='function' || typeof context.pawn.FinishAddComponent!=='function')
                throw Error('native sense widget contract unavailable');
            const cached=state.nativeHighlightAssets;
            if (cached && valid(cached.widgetClass) && valid(cached.icon) && valid(cached.glow)) return cached;
            const widgetClass=UE.Class.Load('/Game/PBZContent/UI/FrontEnd/Sense/W_Sense_Quest.W_Sense_Quest_C');
            const icon=UE.Texture2D.Load('/Game/PBZContent/UI/InGame/Quest/T_Track_Collection.T_Track_Collection');
            const glow=UE.MaterialInstanceConstant.Load('/Game/PBZContent/VFX/SceneFX/UI/WorldMap/Assets/MI_UI_ChengXinDongZhao_001_BG_Green.MI_UI_ChengXinDongZhao_001_BG_Green');
            if (!valid(widgetClass) || !valid(icon) || !valid(glow)) throw Error('native sense collection art unavailable');
            state.nativeHighlightAssets={widgetClass,icon,glow};
            return state.nativeHighlightAssets;
        };
)GLOSSJS"
R"GLOSSJS(        // PickupInfo is the same-build user-defined struct. Its full reflected
        // names and authored aliases are accepted; scene Loot Item ID is not an inventory ID.
        // Read existing rewards only: never generate/roll loot to choose an icon.
        // Evidence: output/pbz-native-icons-20261105/loot-state-opened-chest.json.
        state.nativeHighlightItemId = function(entry) {
            const asId=function(value) {
                if (value==null || typeof value.toString!=='function') return '';
                const id=value.toString();
                return typeof id==='string' && id && id!=='None' && id.length<=256 && !/[\s\x00]/.test(id) ? id : '';
            };
            const actor=entry.actor;
            if (actor.IsA(UE.PBZPickupActor.StaticClass())) return asId(actor.ItemId);
            const pickups=actor.Pickups;
            if (!pickups || typeof pickups.Num!=='function' || typeof pickups.Get!=='function') return '';
            const count=pickups.Num();
            if (!Number.isInteger(count) || count<1 || count>16) return '';
            let itemId='';
            for (let i=0;i<count;++i) {
                const reward=pickups.Get(i);
                if (!reward) return '';
                const id=asId(reward.ItemID_3_AA0CF7F84C2C8B7CDA8E3D8BF2772009 ?? reward.ItemID);
                const amount=reward.Amount_5_93BDB8B545F957E43232B49EA76A6DFE ?? reward.Amount;
                if (!id || !Number.isInteger(amount) || amount<=0 || (itemId && id!==itemId)) return '';
                itemId=id;
            }
            return itemId;
        };
        state.resolveNativeHighlightArt = function(entry,context) {
            // Icon selection is optional and isolated from loot maintenance.
            // Hold the returned row with its soft-reference child for Puerts.
            // Resolving once per owned effect bounds both reads and retained rows.
            try {
                const itemId=state.nativeHighlightItemId(entry);
                if (!itemId || !UE.PBZBackPackSubsystem ||
                    typeof UE.KismetSystemLibrary.IsValidSoftObjectReference!=='function') return null;
                const read=function(owner,getter,fields) {
                    try {
                        if (!owner || typeof owner[getter]!=='function') return undefined;
                        const row=owner[getter](itemId);
                        // All five native item rows inherit ItemId (Name at 8).
                        // The game's pickup UI also uses the ItemID spelling.
                        // Never accept a valid default texture from another row.
                        const id=row && (row.ItemId ?? row.ItemID);
                        if (id==null || id.toString()!==itemId) return undefined;
                        for (const field of fields) {
                            const texture=row[field];
                            if (texture && UE.KismetSystemLibrary.IsValidSoftObjectReference(texture)) return {row,texture};
                        }
                        return null;
                    } catch (_) { return undefined; }
                };
                const subsystem=UE.PBZBackPackSubsystem;
                // Exact row identity, rather than guessed ID prefixes or a
                // different ItemType enum, selects the original small HUD art.
                // Evidence: native_items.inc ItemRowLayout and icon-contract-findings.json.
                for (const [getter,fields] of [['GetPropData',['WhiteIcon','ItemIcon']],
                    ['GetAccessoryData',['Icon','WhiteIcon']],['GetMedicineData',['HighPrecisionTexture']]]) {
                    const result=read(subsystem,getter,fields);
                    if (result!==undefined) return result;
                }
                if (typeof subsystem.GetBackPackSubsytemByContextObject!=='function') return null;
                const backpack=subsystem.GetBackPackSubsytemByContextObject(context.gi);
                if (!valid(backpack)) return null;
                for (const getter of ['GetMainWeaponTableData','GetShaqiWeaponTableData']) {
                    const result=read(backpack,getter,['RenderIcon']);
                    if (result!==undefined) return result;
                }
                return null;
            } catch (_) { return null; }
        };
        state.createNativeHighlight = function(entry,context) {
            state.nativeHighlightStage='assets';
            const assets=state.ensureNativeHighlight(context), actor=entry.actor;
            const pc=UE.GameplayStatics.GetPlayerController(context.gi,0);
            if (!valid(pc) || typeof actor.AddComponentByClass!=='function' || typeof actor.FinishAddComponent!=='function')
                throw Error('native sense widget owner unavailable');
            state.nativeHighlightStage='create-widget';
            const widget=UE.WidgetBlueprintLibrary.Create(context.gi,assets.widgetClass,pc);
            if (!valid(widget)) throw Error('native sense widget creation failed');
            let component=null, effect=null;
            try {
                state.nativeHighlightStage='widget-layout';
                const icon=widget.icon || widget.Icon;
                if (!valid(icon) || !valid(widget.Glow) || !valid(widget.DistanceText) ||
                    !valid(widget.DIstanceBox) || !valid(widget.Arrow)) throw Error('native sense entry layout changed');
                widget.StopAnimationsAndLatentActions();
                widget.SetVisibility(UE.ESlateVisibility.SelfHitTestInvisible);
                widget.SetRenderOpacity(1);
                state.nativeHighlightStage='native-art';
                icon.SetBrushFromTexture(assets.icon,false);
                let itemArt=state.resolveNativeHighlightArt(entry,context);
                if (itemArt && typeof icon.SetBrushFromSoftTexture==='function') {
                    try { icon.SetBrushFromSoftTexture(itemArt.texture,false); }
                    catch (_) {
                        itemArt=null;
                        // Keep a usable original collection icon on a refused
                        // optional item binding; do not stop automatic collection.
                        try { icon.SetBrushFromTexture(assets.icon,false); } catch (_) {}
                    }
                } else itemArt=null;
                widget.Glow.SetBrushFromMaterial(assets.glow);
                icon.SetVisibility(UE.ESlateVisibility.SelfHitTestInvisible);
                widget.Glow.SetVisibility(UE.ESlateVisibility.SelfHitTestInvisible);
                widget.Arrow.SetVisibility(UE.ESlateVisibility.Collapsed);
                widget.DIstanceBox.SetVisibility(UE.ESlateVisibility.SelfHitTestInvisible);
                const transform=new UE.Transform();
                // Deferred registration prevents allocating a world-space
                // render target before switching the component to screen space.
                state.nativeHighlightStage='create-host';
                component=actor.AddComponentByClass(UE.WidgetComponent.StaticClass(),false,transform,true);
                if (!valid(component)) throw Error('native sense host creation failed');
                effect={actor,name:entry.name,component,componentName:null,widget,itemArt,meters:-1,bound:false,retiring:false};
                // Record ownership before any fallible configuration call.
                state.nativeHighlights.set(entry.name,effect);
                effect.componentName=UE.KismetSystemLibrary.GetPathName(component);
                state.nativeHighlightStage='configure-host';
                component.SetWidgetSpace(UE.EWidgetSpace.Screen);
                component.SetCollisionEnabled(UE.ECollisionEnabled.NoCollision);
                component.SetDrawAtDesiredSize(false);
                component.SetDrawSize(new UE.Vector2D(256,256));
                component.SetPivot(new UE.Vector2D(0.5,0.5));
                component.SetWindowFocusable(false);
                component.SetTickWhenOffscreen(false);
                state.nativeHighlightStage='bind-widget';
                component.SetWidget(widget);
                effect.bound=true;
                state.nativeHighlightStage='finish-host';
                actor.FinishAddComponent(component,false,transform);
                component.SetVisibility(true,false);
                state.nativeHighlightStage='fade-in';
                const fadeIn=widget.AI_SenseQuestFadeIn || widget.FadeIn;
                if (valid(fadeIn)) widget.PlayAnimationForward(fadeIn,1,false);
                state.armNativeHighlightGuard();
                return effect;
            } catch(error) {
                if (effect) {
                    // Cleanup must never replace the original creation failure.
                    try { if (state.releaseNativeHighlight(effect)) state.nativeHighlights.delete(entry.name); } catch (_) {}
                    try { state.armNativeHighlightGuard(); } catch (_) {}
                }
                try { if (valid(widget)) widget.SetVisibility(UE.ESlateVisibility.Collapsed); } catch (_) {}
                throw error;
            }
        };
        state.updateNativeHighlights = function(entries,context) {
            const wanted=new Map(entries.map(entry=>[entry.name,entry]));
            for (const [name,effect] of state.nativeHighlights) {
                const entry=wanted.get(name);
                if (!entry || effect.retiring || entry.actor!==effect.actor || !valid(effect.component) || !valid(effect.widget) ||
                    effect.component.GetOwner()!==entry.actor || effect.component.GetWidget()!==effect.widget ||
                    UE.KismetSystemLibrary.GetPathName(effect.component)!==effect.componentName) {
                    if (state.releaseNativeHighlight(effect)) state.nativeHighlights.delete(name);
                }
            }
            // At most two new native widget trees per maintenance tick. Actor
            // hosts project smoothly at engine frame rate after creation.
            entries.sort((a,b)=>a.cm-b.cm);
            let created=0;
            for (const entry of entries) {
                let effect=state.nativeHighlights.get(entry.name);
                if (!effect && created<2 && state.nativeHighlights.size<32) {
                    effect=state.createNativeHighlight(entry,context); ++created;
                }
                if (!effect || effect.retiring || effect.actor!==entry.actor || !valid(effect.component) || !valid(effect.widget)) continue;
                const meters=Math.round(entry.cm/100);
                if (meters!==effect.meters) {
                    const text=UE.KismetTextLibrary.Conv_IntToText(meters,false,true,1,324);
                    effect.widget.DistanceText.SetPBZFormatText(text); effect.meters=meters;
                }
            }
            if (state.nativeHighlights.size) state.armNativeHighlightGuard();
        };
)GLOSSJS"
R"GLOSSJS(        state.syncLoot = function() {
            if (!state.auto_collect && !state.highlight_loot) { state.stopLoot(); return; }
            const context=lootContext();
            if (!context) throw Error('local interaction component unavailable');
            if (!UE.SubsystemBlueprintLibrary || !UE.TargetingWorldSubsystem ||
                typeof UE.TargetingWorldSubsystem.StaticClass!=='function' || typeof UE.NewArray!=='function')
                throw Error('targeting range query unavailable');
            if (state.auto_collect && !require('Library/TsHelper').default) throw Error('gameplay tag helper unavailable');
            if (state.highlight_loot) state.ensureNativeHighlight(context);
            else state.clearNativeHighlights();
            if (!state.auto_collect) { state.attempted.clear(); state.retryAfter.clear(); }
        };
        state.resetLootProof = function() {
            state.mapVisited=state.targetVisited=0;
            state.mapSweep=state.targetSweep=false; state.scanSeen.clear();
        };
        state.stopLoot = function() {
            state.clearNativeHighlights();
            state.attempted.clear(); state.retryAfter.clear(); state.lootPawn=''; state.lootComponent=null;
            state.scanCursor=0; state.scanMaximum=0; state.scanSeen.clear(); state.targetingAt=0; state.targetingPool=null; state.targetingCount=state.targetingCursor=0; state.resetLootProof(); state.lootDiagnostics.clear(); state.lootEntries.clear();
        };
        // Exact v1.1.2 BookPage full FNames from DT_InventoryData; never use comparison/base names.
        // Source: output/pbz-features-20261104/collectibles/inventory-name-audit.json
        // Source SHA256: e8da54e6f60661b30b24b02d8c23ac1e9823bde626e6c7792cb70be129bc02d4
        // Sorted IDs (newline joined) SHA256: 496894e31b63e8ecae46c8d5861e740d8431e343ca7574324764630ef4882def
        const notePageIds = new Set([
            "Page_CS_01","Page_CS_02","Page_CS_03","Page_CS_04","Page_CS_05","Page_CS_06","Page_CS_07","Page_CS_08","Page_CS_10","Page_CS_11",
            "Page_CS_12","Page_CS_13","Page_CS_14","Page_CS_15","Page_CS_16","Page_CS_17","Page_CS_18","Page_CS_19","Page_XC_01","Page_XC_02",
            "Page_XC_03","Page_XC_04","Page_XC_05","Page_XC_06","Page_XC_07","Page_XC_08","R_01001","R_01002","R_01003","R_01004",
            "R_01005","R_01006","R_01007","R_01008","R_01009","R_01010","R_01011","R_01012","R_01013","R_01014",
            "R_01015","R_01016","R_01017","R_01018","R_01019","R_01020","R_01021","R_01022","R_01023","R_01024",
            "R_01025","R_01026","R_01027","R_01028","R_01029","R_01030","R_01031","R_01032","R_01033","R_01034",
            "R_02001","R_02002","R_02003","R_02004","R_02005","R_02006","R_02007","R_02008","R_02009","R_02010",
            "R_02011","R_02012","R_03001","R_03002","R_03003","R_03004","R_03005","R_03006","R_03007","R_03008",
            "R_03011","R_03012","R_03013","R_03014","R_03015","R_03016","R_03017","R_03018","R_03019","R_03020",
            "R_03021","R_03022","R_03023","R_03025","R_03026","R_03027","R_03028","R_03029","R_03030","R_04001",
            "R_040018","R_040019","R_04002","R_04003","R_04004","R_04005","R_04006","R_04007","R_04008","R_04009",
            "R_04010","R_04902","R_05001","R_05002","R_05003","R_05005","R_05006","R_05007","R_05008","R_05009",
            "R_05010","R_05011","R_05012","R_05013","R_05014","R_05015","R_05016","R_05017","R_05018","R_05019",
            "R_05020","R_05021","R_06003","R_06005","R_06006","R_06007","R_06008","R_06009","R_06014","R_06015",
            "R_06016","R_06017","R_06018","R_06019","R_06020","R_06021","R_06022","R_06024","R_06025","R_06028",
            "R_06029","R_06030","R_06031","R_06032","R_06033","R_06034","R_06035","R_070001","R_070002","R_070003",
            "R_070004","R_070005","R_070006","R_070007","R_070008","R_070009","R_07001","R_070010","R_070011","R_070012",
            "R_070013","R_070014","R_070015","R_070016","R_070017","R_070018","R_07002","R_07004","R_07005","R_07007",
            "R_07008","R_07009","R_07010","R_07011","R_07013","R_07014","R_07015","R_07016","R_07017","R_07018",
            "R_07019","R_07020","R_08001","R_11001","R_11002","R_11003","R_12001","R_12003","R_12004","R_12005",
            "R_12006","R_12007","R_12009","R_12010","R_12011","R_12012","R_12013","R_12014","R_12015","R_12016",
            "R_12017","R_12019","R_12020","R_13001","R_14001","R_14004","R_14005","R_14006",
        ]);
        const kind = function(actor, scan = null) {
            if (!valid(actor)) return '';
            if (actor.IsA(scan ? scan.pickupClass : UE.PBZPickupActor.StaticClass())) return 'Loot';
            const type=actor.GetClass();
            if (!valid(type)) return '';
            let path=scan && scan.classPaths.get(type);
            if (path===undefined || path===null) {
                path=UE.KismetSystemLibrary.GetPathName(type);
                if (scan) scan.classPaths.set(type,path);
            }
            if (path==='/Game/PBZContent/Blueprint/Item/Device/BP_Chest.BP_Chest_C') return 'Chest';
            if (path!=='/Game/PBZContent/Blueprint/Item/Device/BP_Rubbish.BP_Rubbish_C') return '';
            try {
                // Verified BP_Rubbish is the scene loot carrier: Pickups is
                // PickupInfo[], PickupMap is Name->Int, with Pickup/Collect Items
                // and OnInteract. Its Loot Item ID is a scene key, not InventoryID.
                // Runtime schema: performance/live-rubbish-schema.json (v1.1.2).
                if (actor['Not Interact']!==false) return '';
                const value=actor['Loot Item ID'];
                if (value==null || typeof value.toString!=='function') return '';
                const text=value.toString();
                if (typeof text!=='string' || !text || text==='None' || text.length>256 || /[\s\x00]/.test(text)) return '';
                const separator=text.lastIndexOf('.');
                if (separator<=0 || separator===text.length-1) return '';
                return notePageIds.has(text.slice(separator+1)) ? 'Note' : 'Loot';
            } catch (_) { return ''; }
        };
        // Only native PBZPickupActor exposes IsValidPickup. Scene loot carriers
        // use the normal CanTriggerInteract predicate and never call that method.
        const pickupValid = (actor,scan) => !actor.IsA(scan.pickupClass) || actor.IsValidPickup()===true;
        // BP interaction classes expose the same pawn,bool-returning overlap
        // contract used by native CheckInteractActors. Do not fall back to the
        // old short-range GetValidDistance path when the contract is absent.
        const distance = function(a,b) {
            const x=a.X-b.X,y=a.Y-b.Y,z=a.Z-b.Z;
            return Math.sqrt(x*x+y*y+z*z);
        };
        state.lootTick = function() {
            state.nativeHighlightStage='context';
            const now=Date.now(); state.leaseUntil=now+1000;
            for (const [name,until] of state.retryAfter) if (until<=now) state.retryAfter.delete(name);
            if (!state.auto_collect && !state.highlight_loot) return [];
            const context=lootContext();
            if (!context) { state.stopLoot(); return []; }
            const {gi,pawn,component}=context;
            const pawnName=UE.KismetSystemLibrary.GetPathName(pawn);
            if (pawnName!==state.lootPawn || component!==state.lootComponent) {
                state.stopLoot(); state.lootPawn=pawnName; state.lootComponent=component;
            }
            let interactionTags=null;
            const interactionReady=function() {
                if (!state.auto_collect || valid(component.GetInteractingActor())) return false;
                const movement=pawn.CharacterMovement;
                const asc=UE.AbilitySystemBlueprintLibrary.GetAbilitySystemComponent(pawn);
)GLOSSJS"
R"GLOSSJS(                if (!valid(movement) || movement.MovementMode!==UE.EMovementMode.MOVE_Walking || !valid(asc)) return false;
                if (!interactionTags) interactionTags=['CharacterState.InCombat','CharacterState.Space.Air',
                    'CharacterState.StatusInfliction','CharacterState.Action','CharacterState.Crouching','CharacterState.ShipNavigation']
                    .map(name=>require('Library/TsHelper').default.constructGameplayTag(name));
                for (const tag of interactionTags) if (asc.HasMatchingGameplayTag(tag)) return false;
                return true;
            };
            const mayInteract=interactionReady();
            if (!state.highlight_loot && !mayInteract) return [];
            const origin=pawn.K2_GetActorLocation();
            const maxRangeM=Math.max(state.auto_collect?state.autoDistanceM:0,state.highlight_loot?state.highlightDistanceM:0);
            // Preserve the engine-owned output wrapper and consume it incrementally.
            // The native tag query itself is O(tag count), limited to once/second.
            state.nativeHighlightStage='target-query';
            if (now>=state.targetingAt) {
                const subsystem=UE.SubsystemBlueprintLibrary.GetWorldSubsystem(pawn,UE.TargetingWorldSubsystem.StaticClass());
                if (!valid(subsystem) || typeof subsystem.GetTargetsByTagInRange!=='function') throw Error('targeting range query unavailable');
                const output=puerts.$ref(UE.NewArray(UE.Actor));
                const tag=require('Library/TsHelper').default.constructGameplayTag('Targeting.Target.Interact');
                subsystem.GetTargetsByTagInRange(tag,true,origin,maxRangeM*100,output);
                const values=puerts.$unref(output);
                if (!values || typeof values.Num!=='function' || typeof values.Get!=='function') throw Error('targeting output array contract changed');
                const count=values.Num();
                if (!Number.isInteger(count) || count<0 || count>4096) throw Error('targeting output exceeds supported bounds');
                state.targetingPool=values; state.targetingCount=count;
                state.targetingCursor=count?state.targetingCursor%count:0;
                ++state.targetingRevision; state.targetingAt=now+1000;
                // A cursor wrap is not proof that a refreshed/reordered array
                // was fully visited. Retain the cursor, restart only the proof.
                state.resetLootProof();
            }
            state.nativeHighlightStage='candidate-scan';
            const map=component.InteractActors, maximum=map.GetMaxIndex();
            if (!Number.isInteger(maximum) || maximum<0 || maximum>4096) throw Error('interaction map bounds changed');
            if (maximum!==state.scanMaximum || state.scanCursor>maximum) {
                state.scanCursor=0; state.scanMaximum=maximum; state.resetLootProof();
            }
            const current=component.CurrentInteractActor;
            const scan={pickupClass:UE.PBZPickupActor.StaticClass(),classPaths:new Map()}, classified=new Map();
            const classify=function(actor) {
                if (!actor) return null;
                if (classified.has(actor)) return classified.get(actor);
                let result=null;
                const label=kind(actor,scan);
                // Same-build BP_Chest.IsValidInteractOutOverlap is exactly
                // !CurSwitchState. bInteracted is a Bool array, not this state.
                if (label && (label!=='Chest' || actor.CurSwitchState===false) &&
                    UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(actor)===pawn) {
                    const name=UE.KismetSystemLibrary.GetPathName(actor);
                    if (name) result={actor,label,name};
                }
                classified.set(actor,result); return result;
            };
            // Retain the nearest 32: distant highlight entries cannot evict
            // nearer collection candidates just because they appear later.
            const remember=function(actor,index=-1,mapKey=0,source='map',targetIndex=-1) {
                const entry=classify(actor);
                if (!entry) return;
                state.scanSeen.add(entry.name);
                const cm=distance(origin,actor.K2_GetActorLocation());
                if (!Number.isFinite(cm) || cm>maxRangeM*100) return;
                const previous=state.lootEntries.get(entry.name);
                if (previous && previous.actor===actor && previous.label===entry.label && source==='current') {
                    index=previous.index; mapKey=previous.mapKey; targetIndex=previous.targetIndex; source=previous.source;
                }
                state.lootEntries.delete(entry.name);
                if (state.lootEntries.size>=32) {
                    let furthest=null;
                    for (const cached of state.lootEntries.values()) if (!furthest || cached.cm>furthest.cm) furthest=cached;
                    if (furthest.cm<=cm) return;
                    state.lootEntries.delete(furthest.name);
                }
                state.lootEntries.set(entry.name,{...entry,index,mapKey,source,targetIndex,cm});
            };
            let inspected=0, examined=0;
            if (current) { remember(current,-1,0,'current'); ++examined; }
            const localBudget=state.targetingCount?16:32;
            while (state.scanCursor<maximum && inspected<128 && examined<localBudget) {
                const index=state.scanCursor++; ++inspected;
                state.mapVisited=Math.min(maximum,state.mapVisited+1);
                if (map.IsValidIndex(index)) {
                    ++examined; const mapKey=map.GetKey(index); remember(map.Get(mapKey),index,mapKey);
                }
            }
            if (state.scanCursor>=maximum) state.scanCursor=0;
            state.mapSweep=state.mapVisited>=maximum;
            const targetBatch=Math.min(32-examined,state.targetingCount);
            for (let i=0;i<targetBatch;++i) {
                const index=state.targetingCursor++;
                state.targetVisited=Math.min(state.targetingCount,state.targetVisited+1);
                remember(state.targetingPool.Get(index),-1,0,'targeting',index);
                if (state.targetingCursor>=state.targetingCount) state.targetingCursor=0;
            }
            state.targetSweep=state.targetVisited>=state.targetingCount;
            if (state.mapSweep && state.targetSweep) {
                for (const name of state.attempted) if (!state.scanSeen.has(name)) state.attempted.delete(name);
                for (const name of state.retryAfter.keys()) if (!state.scanSeen.has(name)) state.retryAfter.delete(name);
                state.resetLootProof();
            }
            const member=function(entry) {
                if (component.CurrentInteractActor===entry.actor) return true;
                if (entry.source==='targeting') return state.targetingPool && entry.targetIndex>=0 &&
                    entry.targetIndex<state.targetingCount && state.targetingPool.Get(entry.targetIndex)===entry.actor;
                const liveMap=component.InteractActors, count=liveMap.GetMaxIndex();
                return Number.isInteger(count) && count>=0 && count<=4096 && entry.index>=0 && entry.index<count &&
                    liveMap.IsValidIndex(entry.index) && liveMap.GetKey(entry.index)===entry.mapKey && liveMap.Get(entry.mapKey)===entry.actor;
            };
            const candidates=[];
            for (const [name,entry] of state.lootEntries) {
                const identity=member(entry) && classify(entry.actor);
                const cm=identity?distance(origin,entry.actor.K2_GetActorLocation()):NaN;
                if (!identity || identity.name!==name || identity.label!==entry.label || !Number.isFinite(cm) || cm>maxRangeM*100) {
                    state.lootEntries.delete(name); continue;
                }
                entry.cm=cm; candidates.push(entry);
            }
            const reportBinding=function(label,method) {
                const text=label+': '+method+' unavailable';
                if (!state.lootDiagnostics.has(text)) {
                    state.lootDiagnostics.add(text); console.log('__GLOSS_PBZ_LOOT_DIAG__:'+text);
                }
            };
            // Replace only the trainer's short-range overlap/radius gates. The
            // game's non-overlap Blueprint predicate and real state flags remain.
            const interactionValid=function(entry) {
                const {actor,label}=entry;
                if (typeof actor.IsValidInteractOutOverlap!=='function') { reportBinding(label,'IsValidInteractOutOverlap'); return false; }
                if (typeof component.IsValidInteract!=='function') throw Error('component interaction state predicate unavailable');
                const asc=UE.AbilitySystemBlueprintLibrary.GetAbilitySystemComponent(pawn);
                if (!valid(asc) || typeof asc.HasAnyMatchingGameplayTags!=='function' ||
                    component.BlockTag==null || component.SprintTag==null || component.InAirTag==null)
                    throw Error('component interaction tag contract unavailable');
                if (actor.IsValidInteractOutOverlap(pawn)!==true) return false;
                if (typeof actor.IsInteractionFailed==='function' && actor.IsInteractionFailed()!==false) return false;
                const blocked=asc.HasAnyMatchingGameplayTags(component.BlockTag)===true;
                const sprint=asc.HasMatchingGameplayTag(component.SprintTag)===true;
                const air=asc.HasMatchingGameplayTag(component.InAirTag)===true;
                return component.IsValidInteract(actor,blocked,sprint,air)===true;
            };
            const highlightCandidates=[],collectCandidates=[];
            for (const entry of candidates) {
                const {actor,label,name,cm}=entry;
                if (!valid(actor) || !pickupValid(actor,scan) || (label==='Chest' && actor.CurSwitchState!==false)) continue;
                if (state.highlight_loot && cm<=state.highlightDistanceM*100) highlightCandidates.push(entry);
                if (mayInteract && cm<=state.autoDistanceM*100 && !state.attempted.has(name) && !state.retryAfter.has(name))
                    collectCandidates.push(entry);
            }
            state.nativeHighlightStage='native-update';
            state.updateNativeHighlights(highlightCandidates,context);
            state.nativeHighlightStage='auto-interact';
            // Sorting at most 32 JS values avoids calling Blueprint predicates
            // for every new distance minimum when engine order is far-to-near.
            collectCandidates.sort((a,b)=>a.cm-b.cm);
            let selected=null;
            for (const entry of collectCandidates) {
                if (valid(entry.actor) && member(entry) && interactionValid(entry)) { selected=entry; break; }
            }
            if (selected) {
)GLOSSJS"
R"GLOSSJS(                const {actor,name,label}=selected;
                // Complete all Blueprint-capable predicates before the fresh
                // object/session/source/distance guard. No collision is modified.
                if (valid(actor) && member(selected) && interactionReady() && pickupValid(actor,scan) && interactionValid(selected) &&
                    interactionReady() && valid(actor) && valid(pawn) && valid(component) && component.GetOwner()===pawn &&
                    UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(gi)===pawn &&
                    UE.PBZInteractComponent.GetInteractComponent(pawn)===component &&
                    UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter(actor)===pawn && kind(actor)===label &&
                    UE.KismetSystemLibrary.GetPathName(actor)===name && member(selected) &&
                    distance(pawn.K2_GetActorLocation(),actor.K2_GetActorLocation())<=state.autoDistanceM*100) {
                    state.retryAfter.set(name,now+1500);
                    if (component.DoInteract(actor)===true) {
                        state.retryAfter.delete(name); state.attempted.add(name);
                    }
                }
            }
            return [];
        };
        state.tick = function() {
            try { return state.lootTick(); }
            catch (error) {
                const message=String(error && error.message || error);
                const stack=String(error && error.stack || '').split('\n').slice(1,4).join(' | ').slice(0,360);
                const stage=state.nativeHighlightStage || 'unknown';
                try { state.clearNativeHighlights(); } catch (_) {}
                throw Error(message+' [loot stage='+stage+']'+(stack?' '+stack:''));
            }
        };
        const dispatch=function(command,id,enabled,range) {
            if (command==='cleanup') {
                state.leaseUntil=0;
                state.free_teleport=false; state.unrestricted_teleport=false;
                state.auto_collect=false; state.highlight_loot=false;
                state.stopLoot(); state.syncMap();
            } else if (command==='toggle') {
                if (!['free_teleport','unrestricted_teleport','auto_collect','highlight_loot'].includes(id)) throw Error('unknown toggle');
                const distanceKey=id==='auto_collect'?'autoDistanceM':id==='highlight_loot'?'highlightDistanceM':'';
                if (enabled && distanceKey && (!Number.isFinite(range) || range<1 || range>(id==='auto_collect'?50:100)))
                    throw Error('loot distance is outside the supported range');
                if ((!enabled || !distanceKey) && range!==0) throw Error('unexpected loot distance parameter');
                const before=state[id], beforeDistance=distanceKey?state[distanceKey]:0;
                state[id]=enabled; if (enabled && distanceKey) state[distanceKey]=range;
                state.leaseUntil=Date.now()+1000;
                try { state.syncMap(); state.syncLoot(); if (distanceKey) state.targetingAt=0; }
                catch(error) {
                    state[id]=before; if (distanceKey) state[distanceKey]=beforeDistance;
                    try { state.syncMap(); state.syncLoot(); if (distanceKey) state.targetingAt=0; } catch(restoreError) { state.leaseUntil=0; }
                    throw error;
                }
            } else if (command!=='tick') throw Error('unknown command');
            return command==='tick'?state.tick():undefined;
        };
        Object.defineProperty(state,'dispatch',{value:dispatch,writable:false,configurable:false});
        globalThis[key]=state;
    }
    let result;
    try { result=state.dispatch(command,id,enabled,range); }
    catch (error) { state.clearNativeHighlights(); throw error; }
    console.log(ack+'ok'+(command==='tick'?'|'+JSON.stringify(result):''));
} catch (error) { console.log(ack+'error:'+String(error && error.message || error)); }
)GLOSSJS"
