'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const contracts=require('./script_test_contracts');
const featureFile=fs.readFileSync(path.join(__dirname,'../src/gameplay/script_features.inc'),'utf8');
const full=Array.from(featureFile.matchAll(/R"GLOSSJS\(([\s\S]*?)\)GLOSSJS"/g),m=>m[1]).join('');
const native=fs.readFileSync(path.join(__dirname,'../src/gameplay/native_scripts.inc'),'utf8');
const short=/R"GLOSSDISPATCH\(([\s\S]*?)\)GLOSSDISPATCH"/.exec(native)[1];
let checks=0;function check(value,why){++checks;assert.ok(value,why);}
function harness(){
    const replies=[],loads={};let available=true;
    const pawn={path:'pawn',CharacterMovement:{MovementMode:1},K2_GetActorLocation:()=>({X:0,Y:0,Z:0})},gi={};
    const asc={HasMatchingGameplayTag:()=>false,HasAnyMatchingGameplayTags:()=>false};
    const component={GetOwner:()=>pawn,GetInteractingActor:()=>null,CurrentInteractActor:null,
        CanTriggerInteract:()=>true,IsValidInteract:()=>true,DoInteract:()=>true,
        InteractActors:{GetMaxIndex:()=>0,IsValidIndex:()=>false,GetKey:()=>0,Get:()=>null}};
    const UE={KismetSystemLibrary:{IsValid:object=>!!object,GetPathName:object=>object.path||''},
        PBZBlueprintFunctionLibrary:{GetPBZLocalCharacter:()=>pawn},PBZInteractComponent:{GetInteractComponent:()=>available?component:null},
        PBZPickupActor:{StaticClass:()=>'pickup'},EMovementMode:{MOVE_Walking:1},
        AbilitySystemBlueprintLibrary:{GetAbilitySystemComponent:()=>asc},Vector2D:function(){},
        GameplayStatics:{GetPlayerController:()=>({}),ProjectWorldToScreen:()=>true}};
    contracts.install(UE,component,asc);
    const puerts={$ref:value=>({value}),$unref:ref=>ref.value,argv:{getByName:()=>gi}};
    const sandbox={console:{log:message=>replies.push(message)},Date:{now:()=>10000},require:name=>{
        loads[name]=(loads[name]||0)+1;
        return name==='ue'?UE:name==='puerts'?puerts:name==='Library/TsHelper'?{default:{constructGameplayTag:name=>name}}:null;
    }};
    const context=vm.createContext(sandbox,{codeGeneration:{strings:false,wasm:false}});
    const run=(source,command,id='',enabled=false,range=0)=>{
        replies.length=0;sandbox.args={command,id,enabled,range};
        vm.runInContext(`(()=>{const {command,id,enabled,range}=args,ack='ACK:';${source}})();`,context);
        return replies.find(message=>message.startsWith('ACK:'));
    };
    return {run,context,sandbox,loads,state:()=>sandbox.__glossPBZ302256,unavailable:()=>available=false};
}
check(Buffer.byteLength(full)>40000&&Buffer.byteLength(short)<512,'warm requests serve only a small fixed dispatcher');
check(!short.includes('require(')&&!short.includes('new Function')&&!short.includes('eval('),'short entry cannot load or compile caller input');
{
    const h=harness();check(h.run(short,'toggle','auto_collect',true,10).includes('receipt missing'),'no short dispatch before initialization');
    check(Object.keys(h.loads).length===0,'missing state does not attempt feature initialization');
    check(h.run(full,'toggle','auto_collect',true,10)==='ACK:ok','first full module acknowledged');
    const state=h.state(),dispatch=state.dispatch,descriptor=Object.getOwnPropertyDescriptor(state,'dispatch');
    check(descriptor.value===dispatch&&!descriptor.writable&&!descriptor.configurable,'dispatcher ownership is immutable');
    check(h.run(short,'toggle','highlight_loot',true,50)==='ACK:ok','second restored toggle uses short entry');
    check(state.auto_collect&&state.highlight_loot&&state.autoDistanceM===10&&state.highlightDistanceM===50,'dispatcher uses current arguments, not bootstrap closure values');
    check(h.run(short,'toggle','auto_collect',true,7.25)==='ACK:ok');
    check(state.autoDistanceM===7.25&&state.highlightDistanceM===50,'warm parameter edit preserves the other feature');
    check(h.run(short,'toggle','auto_collect',true,51).startsWith('ACK:error:'),'warm invalid range rejected');
    check(state.auto_collect&&state.autoDistanceM===7.25,'failed request leaves confirmed state intact');
    check(h.run(short,'tick')==='ACK:ok|[]','same short dispatcher handles tick output');
    check(h.run(short,'toggle','auto_collect',false)==='ACK:ok'&&!state.auto_collect&&state.highlight_loot,'warm off uses latest toggle ID');
    check(h.run(short,'cleanup')==='ACK:ok'&&!state.highlight_loot,'warm cleanup restores all state');
    check(state.targetingPool===null&&state.attempted.size===0&&state.retryAfter.size===0,'cleanup still clears owned candidates and effects');
    check(h.loads.ue===1&&h.loads.puerts===1&&state.dispatch===dispatch,'warm requests never initialize another module');
    check(h.run(short,'toggle','highlight_loot',true,30)==='ACK:ok'&&state.highlightDistanceM===30,'same environment reuses dispatcher after cleanup');
    check(h.run(short,'unknown').startsWith('ACK:error:unknown command'),'fixed command surface remains closed');
}
{
    const h=harness();h.run(full,'toggle','highlight_loot',true,50);h.unavailable();
    check(h.run(short,'toggle','highlight_loot',true,70).startsWith('ACK:error:'),'warm failed reconfiguration propagates');
    check(h.state().highlightDistanceM===50&&h.state().highlight_loot,'warm failure rolls back range and flag');
    check(h.run(short,'cleanup')==='ACK:ok','cleanup does not need live interaction component');
}
for(const replacement of [null,{version:2},{version:3,dispatch(){throw Error('must not call');}}]){
    const h=harness();h.run(full,'toggle','highlight_loot',true,50);h.sandbox.__glossPBZ302256=replacement;
    check(h.run(short,'tick').includes('receipt missing'),'missing or mutable foreign state is rejected');
}
console.log(`${checks} checks, 0 failures; bootstrap ${Buffer.byteLength(full)} bytes, dispatch ${Buffer.byteLength(short)} bytes`);
