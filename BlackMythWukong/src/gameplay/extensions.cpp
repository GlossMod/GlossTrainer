#include "extensions.hpp"
#include "engine_bridge.hpp"
#include "midhook.hpp"
#include "aot.hpp"
#include "console_bridge.hpp"
#include "additional_hooks.hpp"
#include "../runtime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <format>
#include <iomanip>
#include <mutex>
#include <optional>

namespace gloss::gameplay::extensions {
namespace {
#include "catalog_data.inc"
using features::Id;
using engine::Arg;
constexpr auto First=static_cast<std::size_t>(Id::InfiniteItems);
struct Control { bool supported{},enabled{};std::string message="等待游戏接口";double value{}; };
std::array<Control,features::Count> controls;
std::mutex mutex;
Status status;
std::vector<Bookmark> bookmarks;
struct Request {ULONGLONG at;std::uint64_t generation;std::function<features::Result()> execute;std::optional<Id> feature;std::uint64_t updateGeneration{};bool restoring{};};
std::optional<Request> request;
std::uint64_t observedUpdateGeneration{};
std::array<bool,features::Count> updateRestores{};
ULONGLONG lastUpdateRestoreAttempt{};
std::atomic<std::uintptr_t> candidate{};
std::uintptr_t player{},controller{},worldContext{};
std::uint64_t generation{};
std::atomic_bool registered{};
ULONGLONG captureAt{},lastProbe{};
thread_local bool inFrame{};
bool spaceDown{};
struct Recovery {std::uintptr_t world;bool dilation,pause;float time;bool paused;};
std::vector<Recovery> recoveries;
std::atomic_bool recovering{};
constexpr auto Managed=L"/Script/b1-Managed.BGUFunctionLibraryManaged:RunScriptGM";
constexpr auto HasState=L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUHasUnitSimpleState";
constexpr auto SetState=L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUSetUnitSimpleState";
std::wstring Wide(std::string_view text) {
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(n,L'\0');if(n) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),n);return result;
}
bool Finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<1e8&&std::abs(p.y)<1e8&&std::abs(p.z)<1e8;}
template<std::size_t N> engine::CallResult Call(std::wstring_view path,std::array<Arg,N>& args,std::uintptr_t object=0) {return engine::Call(path,args,object);}
engine::CallResult EmptyCall(std::wstring_view path,std::uintptr_t object=0) {return engine::Call(path,{},object);}
features::Result GM(std::string command) {
    auto args=std::array{Arg::String("GMCommand",Wide(command)),Arg::Object("WorldContext",player)};
    const auto result=Call(Managed,args);
    Log(std::format("Extension GM {}: {}",command,result.message));
    return {result.success,result.success?"已发送到游戏接口":result.message};
}
std::uintptr_t Movement() {
    auto value=Arg::Object("CharacterMovement",0);
    return engine::Property(player,value)?value.Get<std::uintptr_t>():0;
}
struct WidgetBaseline {engine::WeakObject weak;std::uint8_t original;};
std::vector<WidgetBaseline> hiddenWidgets;
ULONGLONG lastWidgetDiscovery{};
bool WidgetVisibility(std::uintptr_t widget,std::uint8_t& value) {
    auto args=std::array{Arg::Data("ReturnValue",engine::Type::Enum,std::uint8_t{},true)};
    if(!Call(L"/Script/UMG.Widget:GetVisibility",args,widget).success)return false;
    value=args[0].Get<std::uint8_t>();return value<=4;
}
bool SetWidgetVisibility(std::uintptr_t widget,std::uint8_t value) {
    auto args=std::array{Arg::Enum("InVisibility",value)};return Call(L"/Script/UMG.Widget:SetVisibility",args,widget).success;
}
bool HideGameUi(bool enabled,bool discover=true) {
    if(!engine::WeakReady())return false;
    bool ok=true;
    if(enabled&&discover) {
        auto args=std::array{Arg::Object("WorldContextObject",worldContext),Arg::Objects("FoundWidgets"),
            Arg::Object("WidgetClass",engine::Find(L"/Script/UMG.UserWidget")),Arg::Boolean("TopLevelOnly",true)};
        if(!args[2].Get<std::uintptr_t>()||!Call(L"/Script/UMG.WidgetBlueprintLibrary:GetAllWidgetsOfClass",args).success)return false;
        for(std::size_t i=0;i+8<=args[1].bytes.size();i+=8) {
            std::uintptr_t widget{};std::memcpy(&widget,args[1].bytes.data()+i,8);
            if(!engine::IsA(widget,L"/Script/UMG.UserWidget"))continue;
            if(std::any_of(hiddenWidgets.begin(),hiddenWidgets.end(),[&](const auto& b){return engine::ResolveWeak(b.weak)==widget;}))continue;
            std::uint8_t value{};
            if(!WidgetVisibility(widget,value)){ok=false;continue;}
            if(value==1||value==2)continue; // Respect widgets the game already hid.
            const auto weak=engine::MakeWeak(widget);
            if(engine::ResolveWeak(weak)!=widget){ok=false;continue;}
            if(hiddenWidgets.size()>=256){ok=false;break;}
            hiddenWidgets.push_back({weak,value});
        }
        lastWidgetDiscovery=GetTickCount64();
    }
    for(auto it=hiddenWidgets.begin();it!=hiddenWidgets.end();) {
        const auto object=engine::ResolveWeak(it->weak);
        if(!object){it=hiddenWidgets.erase(it);continue;}
        std::uint8_t current{};
        if(!WidgetVisibility(object,current)){ok=false;++it;continue;}
        if(enabled) {
            if(current!=2) {
                it->original=current;
                // Collapsed widgets must retain their own layout state.
                if(current!=1&&!SetWidgetVisibility(object,2))ok=false;
            }
            ++it;
        } else {
            if(current==2&&!SetWidgetVisibility(object,it->original)){ok=false;++it;continue;}
            it=hiddenWidgets.erase(it);
        }
    }
    return ok;
}
bool Location(std::uintptr_t actor,Vec3& p) {
    auto args=std::array{Arg::Object("Actor",actor),Arg::Vector("OutPos",{},true)};
    if (!Call(L"/Script/UnrealExtent.GSE_ActorFuncLib:GetActorLocationSimple",args).success) return false;
    p=args[1].Get<Vec3>();return Finite(p);
}
features::Result SetLocation(Vec3 p) {
    if (!Finite(p)) return {false,"坐标无效"};
    auto args=std::array{Arg::Object("Actor",player),Arg::Vector("Pos",p),Arg::Boolean("bSweep",false),Arg::Int("iTeleportType",1)};
    const auto result=Call(L"/Script/UnrealExtent.GSE_ActorFuncLib:SetActorLocationSimple",args);
    Vec3 after{};const bool verified=result.success && Location(player,after) && std::abs(after.x-p.x)<2 && std::abs(after.y-p.y)<2 && std::abs(after.z-p.z)<2;
    if (verified) {std::lock_guard lock(mutex);status.position=after;status.positionValid=true;}
    return {verified,verified?"坐标已更新":result.success?"游戏未移动到指定坐标":result.message};
}
bool GetState(std::uint8_t state,bool& value) {
    auto args=std::array{Arg::Object("Unit",player),Arg::Enum("SimpleState",state),Arg::Data("ReturnValue",engine::Type::Bool,std::int32_t{},true)};
    const auto r=Call(HasState,args);value=args[2].Get<std::int32_t>()!=0;return r.success;
}
bool ChangeState(std::uint8_t state,bool remove) {
    auto args=std::array{Arg::Object("Unit",player),Arg::Enum("SimpleState",state),Arg::Boolean("IsRemove",remove)};
    return Call(SetState,args).success;
}
struct StateBaseline {Id owner;std::uint8_t state;bool present;std::uintptr_t player;};
std::vector<StateBaseline> states;
bool StateToggle(Id id,bool enabled,std::initializer_list<std::uint8_t> values) {
    if (enabled) {
        const auto before=states.size();
        const auto rollback=[&] {for(auto i=states.size();i>before;--i)if(!states[i-1].present)ChangeState(states[i-1].state,true);states.resize(before);};
        for (auto state:values) {
            auto it=std::find_if(states.begin(),states.end(),[&](const auto& b){return b.owner==id&&b.state==state&&b.player==player;});
            if (it==states.end()) {bool present{};if(!GetState(state,present)){rollback();return false;}states.push_back({id,state,present,player});}
            if (!ChangeState(state,false)) {rollback();return false;}
        }
    } else {
        for (auto it=states.begin();it!=states.end();) {
            if (it->owner==id) {
                if (it->player==player && !it->present && !ChangeState(it->state,true)) return false;
                it=states.erase(it);
            } else ++it;
        }
    }return true;
}
struct FieldBaseline {Id owner;std::uintptr_t player,object;Arg original,imposed;};
std::vector<FieldBaseline> fields;
bool FieldToggle(Id id,bool enabled,std::uintptr_t object,Arg value) {
    if(!enabled) {
        for(auto it=fields.begin();it!=fields.end();) {
            if(it->owner!=id){++it;continue;}
            if(engine::IsA(it->object,L"/Script/CoreUObject.Object")) {
                auto current=it->original;
                if(!engine::Property(it->object,current))return false;
                if(current.bytes==it->imposed.bytes&&!engine::Property(it->object,it->original,true))return false;
            }
            it=fields.erase(it);
        }
        return true;
    }
    if (!object) return false;
    auto it=std::find_if(fields.begin(),fields.end(),[&](const auto& b){return b.owner==id&&b.player==player&&b.object==object&&std::string_view(b.original.name)==value.name;});
    if (it==fields.end()) {auto original=value;if(!engine::Property(object,original))return false;fields.push_back({id,player,object,std::move(original),value});it=std::prev(fields.end());}
    else {auto current=value;if(!engine::Property(object,current))return false;if(current.bytes!=it->imposed.bytes)it->original=std::move(current);}
    if(!engine::Property(object,value,true))return false;it->imposed=value;return true;
}
bool pausedOwned{},pausedBaseline{};
std::uintptr_t pausedPlayer{};
bool ReadPaused(bool& value) {
    auto args=std::array{Arg::Object("WorldContextObject",worldContext),Arg::Data("ReturnValue",engine::Type::Bool,std::int32_t{},true)};
    if(!Call(L"/Script/Engine.GameplayStatics:IsGamePaused",args).success)return false;value=args[1].Get<std::int32_t>()!=0;return true;
}
bool Pause(bool value) {
    auto args=std::array{Arg::Object("WorldContextObject",worldContext),Arg::Boolean("bPaused",value),Arg::Data("ReturnValue",engine::Type::Bool,std::int32_t{},true)};
    return Call(L"/Script/Engine.GameplayStatics:SetGamePaused",args).success&&args[2].Get<std::int32_t>()!=0;
}
bool Foreground() {DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
bool PauseTick(bool freeze,bool background) {
    const bool desired=freeze||(background&&!Foreground());
    if(desired&&!pausedOwned) {if(!ReadPaused(pausedBaseline))return false;pausedPlayer=player;if(!pausedBaseline&&!Pause(true))return false;pausedOwned=true;}
    if(!desired&&pausedOwned) {if(pausedPlayer==player&&!Pause(pausedBaseline))return false;pausedOwned=false;}
    return true;
}
bool dilationOwned{};float dilationBaseline=1;
bool Dilation(bool enabled,double value) {
    if(enabled&&!dilationOwned) {
        auto args=std::array{Arg::Object("WorldContextObject",worldContext),Arg::Data("ReturnValue",engine::Type::Float,0.f,true)};
        if(!Call(L"/Script/Engine.GameplayStatics:GetGlobalTimeDilation",args).success)return false;
        dilationBaseline=args[1].Get<float>();if(!std::isfinite(dilationBaseline)||dilationBaseline<=0)return false;dilationOwned=true;
    }
    if(!enabled&&!dilationOwned)return true;
    auto args=std::array{Arg::Object("WorldContextObject",worldContext),Arg::Float("TimeDilation",enabled?static_cast<float>(value):dilationBaseline)};
    const bool ok=Call(L"/Script/Engine.GameplayStatics:SetGlobalTimeDilation",args).success;if(ok&&!enabled)dilationOwned=false;return ok;
}
std::uintptr_t cheatManager{},originalCheatManager{},cameraController{};
std::uintptr_t debugController{},debugManager{},originalDebugManager{};
bool cameraOwned{};
std::uintptr_t FirstController(std::uintptr_t context) {
    auto args=std::array{Arg::Object("WorldCtx",context),Arg::Data("ReturnValue",engine::Type::Object,std::uintptr_t{},true)};
    return Call(L"/Script/UnrealExtent.GSE_EngineFuncLib:GetFirstLocalPlayerController",args).success?args[1].Get<std::uintptr_t>():0;
}
bool RestoreManager(std::uintptr_t pc,std::uintptr_t expected,std::uintptr_t original) {
    auto current=Arg::Object("CheatManager",0);
    if(!engine::Property(pc,current))return false;
    if(current.Get<std::uintptr_t>()!=expected)return true; // Do not replace another owner's manager.
    current=Arg::Object("CheatManager",original);return engine::Property(pc,current,true);
}
bool FreeCamera(bool enabled) {
    if(enabled==cameraOwned)return true;
    if(enabled) {
        // Reflection must expose the reference before the state-changing call.
        auto value=Arg::Object("CheatManager",0);if(!engine::Property(controller,value))return false;
        originalCheatManager=value.Get<std::uintptr_t>();cheatManager=originalCheatManager;cameraController=controller;
        if(!cheatManager) {
            cheatManager=engine::Construct(engine::Find(L"/Script/Engine.CheatManager"),controller);if(!cheatManager)return false;
            value=Arg::Object("CheatManager",cheatManager);if(!engine::Property(controller,value,true))return false;
        }
        auto debug=Arg::Object("DebugCameraControllerRef",0);
        if(!engine::Property(cheatManager,debug)||debug.Get<std::uintptr_t>()) {
            RestoreManager(cameraController,cheatManager,originalCheatManager);return false;
        }
        const bool called=EmptyCall(L"/Script/Engine.CheatManager:EnableDebugCamera",cheatManager).success;
        if(!engine::Property(cheatManager,debug)||!engine::IsA(debug.Get<std::uintptr_t>(),L"/Script/Engine.DebugCameraController")) {
            RestoreManager(cameraController,cheatManager,originalCheatManager);return false;
        }
        debugController=debug.Get<std::uintptr_t>();
        auto original=Arg::Object("OriginalControllerRef",0);
        auto localPlayer=Arg::Object("OriginalPlayer",0);
        cameraOwned=true;
        if(called&&engine::Property(debugController,original)&&original.Get<std::uintptr_t>()==cameraController&&
            engine::Property(debugController,localPlayer)&&engine::IsA(localPlayer.Get<std::uintptr_t>(),L"/Script/Engine.Player")&&
            FirstController(worldContext)==debugController)return true;
        // An incomplete activation still owns state and must use the debug
        // controller's manager for rollback, just like a normal close.
        Log("Debug camera activation did not complete; restoring original controller");
        FreeCamera(false);return false;
    }
    // DisableDebugCamera casts its Outer to DebugCameraController. Calling it
    // on the original player's manager returns without deactivating the view.
    if(!debugController||!engine::World(debugController))return false;
    auto original=Arg::Object("OriginalControllerRef",0);
    if(!engine::Property(debugController,original)||original.Get<std::uintptr_t>()!=cameraController)return false;
    if(!debugManager) {
        auto value=Arg::Object("CheatManager",0);if(!engine::Property(debugController,value))return false;
        originalDebugManager=value.Get<std::uintptr_t>();debugManager=originalDebugManager;
        if(!debugManager) {
            debugManager=engine::Construct(engine::Find(L"/Script/Engine.CheatManager"),debugController);if(!debugManager)return false;
            value=Arg::Object("CheatManager",debugManager);if(!engine::Property(debugController,value,true))return false;
        }
    }
    const bool called=EmptyCall(L"/Script/Engine.CheatManager:DisableDebugCamera",debugManager).success;
    if(FirstController(worldContext)!=cameraController)return false;
    auto cleared=Arg::Object("OriginalControllerRef",0);
    if(!engine::Property(debugController,cleared)||cleared.Get<std::uintptr_t>())return false;
    cameraOwned=false;
    const bool restored=RestoreManager(cameraController,cheatManager,originalCheatManager);
    // The temporary controller may already have been destroyed by the engine.
    if(engine::World(debugController))RestoreManager(debugController,debugManager,originalDebugManager);
    if(engine::IsA(debugController,L"/Script/Engine.DebugCameraController")) {
        const auto destroyed=EmptyCall(L"/Script/Engine.Actor:K2_DestroyActor",debugController);
        if(!destroyed.success)Log("Temporary debug camera cleanup failed: "+destroyed.message);
    }
    auto debugRef=Arg::Object("DebugCameraControllerRef",0);engine::Property(cheatManager,debugRef,true);
    cheatManager=originalCheatManager=cameraController=0;
    debugController=debugManager=originalDebugManager=0;
    return called&&restored;
}
std::uintptr_t SpringArm() {
    auto args=std::array{Arg::Object("ComponentClass",engine::Find(L"/Script/Engine.SpringArmComponent")),Arg::Data("ReturnValue",engine::Type::Object,std::uintptr_t{},true)};
    return args[0].Get<std::uintptr_t>()&&Call(L"/Script/Engine.Actor:GetComponentByClass",args,player).success?args[1].Get<std::uintptr_t>():0;
}
bool CameraDistance(bool enabled,double distance) {
    const auto object=enabled?SpringArm():0;
    for(std::size_t i=0;i<fields.size();) {
        if(fields[i].owner!=Id::FixedCameraDistance||fields[i].object==object){++i;continue;}
        const auto previous=fields[i].object;
        if(!engine::IsA(previous,L"/Script/Engine.SpringArmComponent")){fields.erase(fields.begin()+i);continue;}
        if(!FieldToggle(Id::FixedCameraDistance,false,previous,Arg::Float("TargetArmLength",0)))return false;
    }
    return !enabled||FieldToggle(Id::FixedCameraDistance,true,object,Arg::Float("TargetArmLength",static_cast<float>(distance)));
}
features::Result Toggle(Id id,bool enabled,double value) {
    if(id==Id::InfiniteItems||id==Id::InfiniteBuffDuration||id==Id::InfiniteVesselDuration) {
        const auto result=extra::SetEnabled(id,enabled);
        if(result.success){std::lock_guard lock(mutex);auto& c=controls[static_cast<std::size_t>(id)];c.enabled=enabled;c.message=result.message;}
        return result;
    }
    bool ok{};
    switch(id) {
    case Id::Stealth:ok=StateToggle(id,enabled,{66});break;
    case Id::BossStealth:ok=StateToggle(id,enabled,{38});break;
    case Id::NoHitCollision:ok=StateToggle(id,enabled,{17,19});break;
    case Id::NoGravity:ok=FieldToggle(id,enabled,Movement(),Arg::Float("GravityScale",0));break;
    case Id::HidePlayer:ok=FieldToggle(id,enabled,player,Arg::Boolean("bHidden",true));break;
    case Id::HideHud:ok=HideGameUi(enabled);if(!ok&&enabled)HideGameUi(false);break;
    case Id::BulletTime:ok=Dilation(enabled,value);break;
    case Id::FreeCamera:ok=FreeCamera(enabled);break;
    case Id::FixedCameraDistance:ok=CameraDistance(enabled,value);break;
    case Id::ContinuousJump:ok=true;break;
    case Id::PauseBackground:case Id::FreezeWorld:{
        bool freeze{},background{};{std::lock_guard lock(mutex);freeze=controls[static_cast<std::size_t>(Id::FreezeWorld)].enabled;background=controls[static_cast<std::size_t>(Id::PauseBackground)].enabled;}
        if(id==Id::FreezeWorld)freeze=enabled;else background=enabled;ok=PauseTick(freeze,background);break;
    }
    default:return {false,"当前版本尚无可用调用入口"};
    }
    const bool pendingUiRestore=id==Id::HideHud&&!enabled&&!ok;
    if(ok||id==Id::FreeCamera||pendingUiRestore) {std::lock_guard lock(mutex);auto& c=controls[static_cast<std::size_t>(id)];c.enabled=id==Id::FreeCamera?cameraOwned:enabled;c.value=value;}
    if(pendingUiRestore)return {true,"已关闭，正在恢复界面"};
    return {ok,ok?(enabled?"已启用":"已关闭并恢复"):"游戏接口未完成操作"};
}
bool HasUpdateRestores() {
    return std::any_of(updateRestores.begin(),updateRestores.end(),[](bool pending){return pending;});
}
void RefreshUpdateBarrier() {
    std::optional<Id> cancelled;
    bool changed{};
    {
        std::lock_guard lock(mutex);
        const auto current=features::UpdateGeneration();
        changed=current!=observedUpdateGeneration;
        if(changed) {observedUpdateGeneration=current;lastUpdateRestoreAttempt=0;}
        if(changed||features::UpdateRequired())
            for(std::size_t i=First;i<controls.size();++i)
                if(controls[i].enabled)updateRestores[i]=true;
        if(request&&!request->restoring&&(features::UpdateRequired()||request->updateGeneration!=current)) {
            cancelled=request->feature;request.reset();status.pending=false;status.message="操作已取消，请更新后使用";
        }
    }
    if(cancelled)features::CompleteAction(*cancelled,{false,"操作已取消，请更新后使用"});
    if(changed)extra::CaptureNativePlayer(0,0); // Cancel old native deferred allowances/cycle requests too.
}
void RestoreForUpdate() {
    RefreshUpdateBarrier();
    std::vector<std::pair<Id,double>> active;
    {
        std::lock_guard lock(mutex);
        for(std::size_t i=First;i<controls.size();++i) {
            if(!controls[i].enabled)updateRestores[i]=false;
            else if(updateRestores[i])active.emplace_back(static_cast<Id>(i),controls[i].value);
        }
        const auto now=GetTickCount64();
        if(active.empty()||(lastUpdateRestoreAttempt&&now-lastUpdateRestoreAttempt<100))return;
        lastUpdateRestoreAttempt=now;
    }
    for(const auto& [id,value]:active) {
        features::Result result;
        try {result=Toggle(id,false,value);}
        catch(...) {result={false,"关闭功能失败，等待重试"};}
        bool report{};
        {
            std::lock_guard lock(mutex);auto& control=controls[static_cast<std::size_t>(id)];
            if(result.success&&!control.enabled)updateRestores[static_cast<std::size_t>(id)]=false;
            report=control.message!=result.message;control.message=result.message;
        }
        if(report)Log(std::format("Update restoration {}: {}",static_cast<unsigned>(id),result.message));
    }
}
void Probe() {
    const auto function=[](const wchar_t* path){return engine::Find(path)!=0;};
    const bool state=function(HasState)&&function(SetState);
    const bool pause=function(L"/Script/Engine.GameplayStatics:IsGamePaused")&&function(L"/Script/Engine.GameplayStatics:SetGamePaused");
    const bool gm=function(Managed);
    auto gravity=Arg::Float("GravityScale",0);const bool canGravity=engine::Property(Movement(),gravity);
    auto hidden=Arg::Boolean("bHidden",false);const bool canHidden=engine::Property(player,hidden);
    const bool canHud=engine::WeakReady()&&function(L"/Script/UMG.WidgetBlueprintLibrary:GetAllWidgetsOfClass")&&
        function(L"/Script/UMG.Widget:GetVisibility")&&function(L"/Script/UMG.Widget:SetVisibility");
    auto arm=Arg::Float("TargetArmLength",0);const bool canArm=engine::Property(SpringArm(),arm);
    std::lock_guard lock(mutex);
    auto support=[&](Id id,bool yes,const char* missing="当前版本未提供此接口") {auto& c=controls[static_cast<std::size_t>(id)];c.supported=yes;c.message=yes?"已就绪":missing;};
    support(Id::Stealth,state);support(Id::BossStealth,state);support(Id::NoHitCollision,state);
    support(Id::NoGravity,canGravity);support(Id::HidePlayer,canHidden);support(Id::HideHud,canHud);
    support(Id::BulletTime,function(L"/Script/Engine.GameplayStatics:GetGlobalTimeDilation")&&function(L"/Script/Engine.GameplayStatics:SetGlobalTimeDilation"));
    support(Id::FreezeWorld,pause);support(Id::PauseBackground,pause);
    support(Id::ContinuousJump,function(L"/Script/Engine.Character:LaunchCharacter")&&canGravity);
    support(Id::FixedCameraDistance,canArm,"角色未使用弹簧臂镜头组件");
    support(Id::FreeCamera,function(L"/Script/Engine.CheatManager:EnableDebugCamera")&&function(L"/Script/Engine.CheatManager:DisableDebugCamera"));
    support(Id::SummonCloud,function(L"/Script/b1-Managed.BGUFunctionLibraryCS:ToggleCloudMove"));
    support(Id::UnlockBossRush,gm);
    support(Id::UnlockShrines,console::ShrinesReady(),"等待游戏注册土地庙指令");
    support(Id::UnlockCloud,gm&&function(L"/Script/b1-Managed.BGWDataAsset_CloudMoveConfig"));
    for(auto id:{Id::InfiniteItems,Id::InfiniteBuffDuration,Id::InfiniteVesselDuration,Id::NewGameCycle})support(id,extra::Supported(id),"当前版本的功能特征未匹配");
    status.ready=gm;status.message=gm?"游戏接口已就绪":"等待游戏功能库";
}
void JumpTick() {
    const bool down=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
    bool enabled{};{std::lock_guard lock(mutex);enabled=controls[static_cast<std::size_t>(Id::ContinuousJump)].enabled;}
    if(down&&!spaceDown&&enabled&&!overlay::Visible()&&Foreground()) {
        const auto movement=Movement();
        auto falling=std::array{Arg::Data("ReturnValue",engine::Type::Bool,std::int32_t{},true)};
        auto z=Arg::Float("JumpZVelocity",0);
        if(movement&&Call(L"/Script/Engine.CharacterMovementComponent:IsFalling",falling,movement).success&&falling[0].Get<std::int32_t>()&&engine::Property(movement,z)) {
            auto args=std::array{Arg::Vector("LaunchVelocity",{0,0,z.Get<float>()}),Arg::Boolean("bXYOverride",false),Arg::Boolean("bZOverride",true)};
            const auto result=Call(L"/Script/Engine.Character:LaunchCharacter",args,player);
            if(!result.success) Log("Continuous jump rejected: "+result.message);
        }
    }spaceDown=down;
}
void CancelPending(const char* reason) {
    extra::CaptureNativePlayer(0,0);
    lastProbe=0;
    std::optional<Id> cancelled;
    {
        std::lock_guard lock(mutex);status.ready=false;
        if(status.pending) {
            if(request&&request->feature){cancelled=request->feature;controls[static_cast<std::size_t>(*cancelled)].message=reason;}
            request.reset();status.pending=false;status.message=reason;
        }
    }
    if(cancelled)features::CompleteAction(*cancelled,{false,reason});
}
void RestoreSession() {
    std::vector<std::pair<Id,double>> active;
    {std::lock_guard lock(mutex);for(std::size_t i=First;i<controls.size();++i)if(controls[i].enabled)active.emplace_back(static_cast<Id>(i),controls[i].value);}
    for(const auto& [id,value]:active) {
        const auto result=Toggle(id,false,value);
        if(!result.success)Log(std::format("Extension {} restore skipped: old object unavailable",static_cast<unsigned>(id)));
    }
    // Global effects belong to the world, not to the lifetime of its pawn.
    if(dilationOwned&&!Dilation(false,1))Log("Old-world time dilation could not be restored");
    if(pausedOwned) {if(!Pause(pausedBaseline))Log("Old-world pause could not be restored");else pausedOwned=false;}
    if(dilationOwned||pausedOwned){recoveries.push_back({worldContext,dilationOwned,pausedOwned,dilationBaseline,pausedBaseline});recovering=true;}
}
void LoseSession(const char* reason) {
    CancelPending(reason);
    if(!player&&!worldContext)return;
    RestoreSession();
    states.clear();fields.clear();player=controller=worldContext=0;dilationOwned=pausedOwned=false;
    std::lock_guard lock(mutex);++generation;for(auto& c:controls)c.enabled=false;status.positionValid=false;status.objects.clear();
}
void RecoverGlobals() {
    for(auto it=recoveries.begin();it!=recoveries.end();) {
        if(!engine::World(it->world)){Log("Old world was destroyed; no global state remains to restore");it=recoveries.erase(it);continue;}
        if(it->dilation) {
            auto args=std::array{Arg::Object("WorldContextObject",it->world),Arg::Float("TimeDilation",it->time)};
            if(Call(L"/Script/Engine.GameplayStatics:SetGlobalTimeDilation",args).success)it->dilation=false;
        }
        if(it->pause) {
            auto args=std::array{Arg::Object("WorldContextObject",it->world),Arg::Boolean("bPaused",it->paused),Arg::Data("ReturnValue",engine::Type::Bool,std::int32_t{},true)};
            if(Call(L"/Script/Engine.GameplayStatics:SetGamePaused",args).success&&args[2].Get<std::int32_t>())it->pause=false;
        }
        if(!it->dilation&&!it->pause)it=recoveries.erase(it);else ++it;
    }
    recovering=!recoveries.empty();
}
void Frame() {
    if(inFrame||!engine::IsGameThread())return;
    inFrame=true;
    struct Exit{~Exit(){inFrame=false;}}exit;
    try {
        RefreshUpdateBarrier();
        RecoverGlobals();
        const auto now=GetTickCount64();
        bool hideUi{};{std::lock_guard lock(mutex);hideUi=controls[static_cast<std::size_t>(Id::HideHud)].enabled;}
        // Also retry an old session's live UMG restoration after GC/transition.
        if(!hideUi&&!hiddenWidgets.empty()) {
            const bool restored=HideGameUi(false);
            std::lock_guard lock(mutex);
            controls[static_cast<std::size_t>(Id::HideHud)].message=restored?"已关闭并恢复":"已关闭，正在恢复界面";
        }
        {std::lock_guard lock(mutex);if(request&&now-request->at>=30000) {
            if(request->feature)controls[static_cast<std::size_t>(*request->feature)].message="请求已过期";
            request.reset();status.pending=false;status.message="请求已过期，请重新操作";
        }}
        const auto context=candidate.load();
        if(cameraOwned&&!engine::World(cameraController)){cameraOwned=false;cheatManager=originalCheatManager=cameraController=debugController=debugManager=originalDebugManager=0;}
        if(!cameraOwned&&!engine::IsPawn(context)) {LoseSession("当前角色已离开，操作已取消");return;}
        std::uintptr_t nextController=controller;
        if(!cameraOwned) {
            auto pc=std::array{Arg::Object("WorldCtx",context),Arg::Data("ReturnValue",engine::Type::Object,std::uintptr_t{},true)};
            if(!Call(L"/Script/UnrealExtent.GSE_EngineFuncLib:GetFirstLocalPlayerController",pc).success){LoseSession("本地控制器未就绪，操作已取消");return;}
            nextController=pc[1].Get<std::uintptr_t>();
        }
        auto pawn=std::array{Arg::Data("ReturnValue",engine::Type::Object,std::uintptr_t{},true)};
        if(!nextController||(!cameraOwned&&!Call(L"/Script/Engine.Controller:K2_GetPawn",pawn,nextController).success)){LoseSession("角色未就绪，操作已取消");return;}
        const auto actual=cameraOwned?player:pawn[0].Get<std::uintptr_t>();
        const auto world=engine::World(cameraOwned?cameraController:actual);
        if((!cameraOwned&&!engine::IsPawn(actual))||!world){LoseSession("角色未就绪，操作已取消");return;}
        if(player!=actual||worldContext!=world||controller!=nextController) {
            CancelPending("角色或场景已经变化，操作已取消");RestoreSession();
            player=actual;controller=nextController;worldContext=world;
            states.clear();fields.clear();dilationOwned=pausedOwned=cameraOwned=false;lastProbe=0;
            std::lock_guard lock(mutex);++generation;for(auto& c:controls)c.enabled=false;status.positionValid=false;status.objects.clear();
        }
        if(engine::IsPawn(player))extra::CaptureNativePlayer(player,controller);else extra::CaptureNativePlayer(0,0);
        console::Tick();
        if(!lastProbe||now-lastProbe>10000) {lastProbe=now;Probe();}
        RestoreForUpdate();
        std::optional<Request> work;
        {std::lock_guard lock(mutex);if(request) {work=std::move(request);request.reset();}}
        if(work) {
            const bool allowed=work->restoring||(!features::UpdateRequired()&&work->updateGeneration==features::UpdateGeneration());
            const auto result=allowed&&now-work->at<30000&&work->generation==generation?work->execute():features::Result{false,"请求已过期、需要更新或游戏场景已变化"};
            Log("Extension action: "+result.message);
            if(work->feature)features::CompleteAction(*work->feature,result);
            std::lock_guard lock(mutex);status.pending=false;status.message=result.message;
            if(work->feature)controls[static_cast<std::size_t>(*work->feature)].message=result.message;
        }
        // An update may arrive while a game-thread call was already executing.
        // Recheck before continuous effects, and preserve restoration across unlocks.
        RestoreForUpdate();
        bool freeze{},background{},fixedDistance{};double distance{};
        {std::lock_guard lock(mutex);freeze=controls[static_cast<std::size_t>(Id::FreezeWorld)].enabled;background=controls[static_cast<std::size_t>(Id::PauseBackground)].enabled;const auto& camera=controls[static_cast<std::size_t>(Id::FixedCameraDistance)];fixedDistance=camera.enabled;distance=camera.value;}
        if(fixedDistance&&!CameraDistance(true,distance)){std::lock_guard lock(mutex);controls[static_cast<std::size_t>(Id::FixedCameraDistance)].message="等待镜头组件恢复";}
        {std::lock_guard lock(mutex);hideUi=controls[static_cast<std::size_t>(Id::HideHud)].enabled;}
        if(hideUi&&!HideGameUi(true,now-lastWidgetDiscovery>=500)){std::lock_guard lock(mutex);controls[static_cast<std::size_t>(Id::HideHud)].message="部分界面正在切换";}
        if(!PauseTick(freeze,background)) {
            std::lock_guard lock(mutex);
            for(auto id:{Id::FreezeWorld,Id::PauseBackground})if(controls[static_cast<std::size_t>(id)].enabled)controls[static_cast<std::size_t>(id)].message="暂停状态更新失败";
        }
        JumpTick();
    }catch(const std::exception& e){Log(std::format("Extension frame failed: {}",e.what()));std::lock_guard lock(mutex);status.pending=false;status.message="操作失败，请查看日志";}
}
features::Result Queue(std::function<features::Result()> execute,std::optional<Id> id={},bool resetPosition=false,bool restoring=false) {
    std::lock_guard lock(mutex);
    const auto updateGeneration=features::UpdateGeneration();
    if(!restoring&&(features::UpdateRequired()||updateGeneration!=observedUpdateGeneration||HasUpdateRestores()))
        return {false,features::UpdateRequired()?"请下载最新版本后使用":"正在恢复此前功能状态，请稍后重试"};
    if(recovering)return {false,"正在恢复此前场景的临时状态"};
    if(!status.ready)return {false,"请进入存档，等待游戏接口就绪"};
    if(status.pending)return {false,"上一项操作尚未完成"};
    status.pending=true;status.message="等待游戏线程处理";
    if(resetPosition)status.positionValid=false;
    request=Request{GetTickCount64(),generation,std::move(execute),id,updateGeneration,restoring};return {true,"已排队"};
}
void LoadBookmarks() {
    std::ifstream file(DataDirectory()/L"bookmarks.txt");std::string name;Vec3 p;
    while(bookmarks.size()<200&&file>>std::quoted(name)>>p.x>>p.y>>p.z)if(Finite(p)&&!name.empty()&&name.size()<=128)bookmarks.push_back({std::move(name),p});
}
bool WriteBookmarks() {
    try {
        std::filesystem::create_directories(DataDirectory());
        const auto file=DataDirectory()/L"bookmarks.txt",temporary=DataDirectory()/L"bookmarks.tmp";
        {std::ofstream stream(temporary,std::ios::trunc);stream<<std::setprecision(17);for(const auto& b:bookmarks)stream<<std::quoted(b.name)<<' '<<b.position.x<<' '<<b.position.y<<' '<<b.position.z<<'\n';stream.flush();if(!stream)return false;}
        return MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    }catch(...){return false;}
}
}
std::span<const ItemEntry> Items(){return itemEntries;}
std::span<const SceneEntry> Scenes(){return sceneEntries;}
std::span<const LandmarkEntry> Landmarks(){return landmarkEntries;}
Status GetStatus(){std::lock_guard lock(mutex);return status;}
features::Result ChangeItem(std::size_t index,int count,bool remove) {
    if(index>=std::size(itemEntries)||count<1||count>999999)return {false,"物品或数量无效"};
    const auto entry=itemEntries[index];
    if(remove&&(entry.list==13||entry.list==15||entry.list==16))return {false,"此类型尚未提供单项删除接口"};
    if(entry.list==16)return {false,"请选择影神图的故事与画像阶段"};
    std::string command;
    if(entry.list==15)command=std::format("AddSpell {}",entry.id);
    else command=std::format("{} {} {}",remove?"CostItem":"AddItem",entry.id,count);
    return Queue([command,remove,entry,count]{if(remove)extra::PermitRemoval(entry.id,count);return GM(command);});
}
features::Result SetCard(std::size_t index,int storyStage,int portraitStage) {
    if(index>=std::size(itemEntries)||itemEntries[index].list!=16||storyStage<0||storyStage>5||portraitStage<0||portraitStage>1)return {false,"图鉴阶段无效"};
    const auto id=itemEntries[index].id;
    return Queue([id,storyStage,portraitStage]{return GM(std::format("Card {} {} {}",id,storyStage,portraitStage));});
}
features::Result RefreshPosition() {
    return Queue([] {Vec3 p{};if(!Location(player,p))return features::Result{false,"读取坐标失败"};std::lock_guard lock(mutex);status.position=p;status.positionValid=true;return features::Result{true,"已读取当前位置"};},{},true);
}
features::Result Teleport(Vec3 p){if(!Finite(p))return {false,"坐标无效"};return Queue([p]{return SetLocation(p);});}
features::Result Travel(int sceneId,Vec3 p) {
    if(!Finite(p)||std::none_of(std::begin(sceneEntries),std::end(sceneEntries),[&](const auto& s){return s.id==sceneId;}))return {false,"场景或坐标无效"};
    return Queue([sceneId,p] {return console::Execute(Wide(std::format("b.teleportToLevelPosition {} {:.9f} {:.9f} {:.9f} 0 0 0",sceneId,p.x,p.y,p.z)),worldContext);});
}
features::Result TravelDefault(int sceneId) {
    if(std::none_of(std::begin(sceneEntries),std::end(sceneEntries),[&](const auto& s){return s.id==sceneId;}))return {false,"场景无效"};
    return Queue([sceneId]{return console::Execute(Wide(std::format("b.teleportToLevelPosition {}",sceneId)),worldContext);});
}
features::Result RefreshObjects() {
    return Queue([] {
        auto args=std::array{Arg::Object("WorldContextObject",player),Arg::Object("ActorClass",engine::Find(L"/Script/Engine.Actor")),Arg::Objects("OutActors")};
        const auto r=Call(L"/Script/Engine.GameplayStatics:GetAllActorsOfClass",args);if(!r.success)return features::Result{false,r.message};
        std::vector<ObjectLocation> result;
        for(std::size_t i=0;i+8<=args[2].bytes.size();i+=8) {
            std::uintptr_t address{};std::memcpy(&address,args[2].bytes.data()+i,8);if(address==player||!engine::IsActor(address))continue;
            auto name=engine::ObjectName(address);
            std::string lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            bool match=false;for(auto word:{"interact","treasure","chest","rebirth","collect","herb","pick","npc","drop"})if(lower.find(word)!=std::string::npos)match=true;
            Vec3 p{};if(match&&Location(address,p))result.push_back({address,std::move(name),p});
            if(result.size()>=1000)break;
        }
        const auto count=result.size();std::lock_guard lock(mutex);status.objects=std::move(result);
        return features::Result{true,std::format("已找到 {} 个场景对象",count)};
    });
}
features::Result TeleportToObject(std::uintptr_t address) {
    {std::lock_guard lock(mutex);if(std::none_of(status.objects.begin(),status.objects.end(),[&](const auto& o){return o.address==address;}))return {false,"对象不在当前列表"};}
    return Queue([address] {Vec3 p{};if(!engine::IsActor(address)||!Location(address,p))return features::Result{false,"对象已经离开当前场景，请刷新"};p.z+=100;return SetLocation(p);});
}
std::vector<Bookmark> Bookmarks(){std::lock_guard lock(mutex);return bookmarks;}
features::Result SaveBookmark(std::string name,Vec3 p) {
    if(name.empty()||name.size()>128||name.find_first_of("\r\n")!=std::string::npos||!Finite(p))return {false,"名称或坐标无效"};
    std::lock_guard lock(mutex);if(bookmarks.size()>=200)return {false,"最多保存200个位置"};
    bookmarks.push_back({std::move(name),p});if(!WriteBookmarks()){bookmarks.pop_back();return {false,"位置保存失败"};}return {true,"位置已保存"};
}
features::Result RemoveBookmark(std::size_t index) {
    std::lock_guard lock(mutex);if(index>=bookmarks.size())return {false,"位置不存在"};const auto old=bookmarks[index];bookmarks.erase(bookmarks.begin()+index);
    if(!WriteBookmarks()){bookmarks.insert(bookmarks.begin()+index,old);return {false,"位置删除失败"};}return {true,"位置已删除"};
}
void Initialize() {
    {std::lock_guard lock(mutex);LoadBookmarks();for(const auto& d:features::Definitions())controls[static_cast<std::size_t>(d.id)].value=d.initialValue;}
    engine::Initialize(GetModuleHandleW(nullptr));
    aot::Initialize(GetModuleHandleW(nullptr));
    console::Initialize();
    extra::Initialize();
}
void CapturePlayer(std::uintptr_t object) noexcept {
    if(!engine::Ready()||!engine::IsGameThread())return;
    candidate=object;
    if(!registered&&engine::IsPawn(object))registered=engine::RegisterFrame(&Frame);
}
bool Owns(Id id){return static_cast<std::size_t>(id)>=First&&id<Id::Count;}
features::BackendState Query(Id id) {
    std::lock_guard lock(mutex);const auto& c=controls[static_cast<std::size_t>(id)];
    if(!engine::Ready())return {features::Availability::Unavailable,c.enabled,"当前游戏未找到完整引擎接口"};
    if(!status.ready)return {features::Availability::Waiting,c.enabled,"等待进入存档并捕获本地玩家"};
    return {c.supported?features::Availability::Ready:features::Availability::Unavailable,c.enabled,id==Id::NewGameCycle&&c.supported?std::string{}:c.message};
}
features::Result SetEnabled(Id id,bool enabled,double value) {
    if(!std::isfinite(value))return {false,"参数无效"};
    if(enabled&&Query(id).availability!=features::Availability::Ready)return {false,"此功能尚未就绪"};
    if(!enabled) {
        std::lock_guard lock(mutex);
        if(features::UpdateRequired()||features::UpdateGeneration()!=observedUpdateGeneration||HasUpdateRestores()) {
            updateRestores[static_cast<std::size_t>(id)]=true;
            return {true,"已请求关闭，等待游戏线程恢复"};
        }
    }
    return Queue([id,enabled,value]{return Toggle(id,enabled,value);},id,false,!enabled);
}
features::Result Apply(Id id,double value) {
    if(Query(id).availability!=features::Availability::Ready)return {false,"此功能尚未就绪"};
    return Queue([id,value] {
        if(id==Id::NewGameCycle)return extra::SetCycle(static_cast<int>(value));
        if(id==Id::UnlockBossRush)return GM("AllBossRush");
        if(id==Id::UnlockShrines)return console::Execute(L"b.ActiveAllRebirthPoint",worldContext);
        if(id==Id::UnlockCloud) {
            const auto config=engine::Load(L"/Game/00Main/DataAsset/DA_CloudMoveConfig.DA_CloudMoveConfig",L"/Script/b1-Managed.BGWDataAsset_CloudMoveConfig");
            auto item=Arg::Int("CloudMoveItemId",0);
            if(!config||!engine::Property(config,item)||item.Get<int>()<=0)return features::Result{false,"未读取到当前版本的筋斗云物品配置"};
            return GM(std::format("AddItem {} 1",item.Get<int>()));
        }
        if(id==Id::SummonCloud){auto args=std::array{Arg::Object("Unit",player)};const auto r=Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:ToggleCloudMove",args);return features::Result{r.success,r.success?"已请求切换筋斗云":r.message};}
        return features::Result{false,"此操作的游戏入口尚未就绪"};
    },id);
}
}
