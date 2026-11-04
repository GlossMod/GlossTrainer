'use strict';
const contracts=require('./script_test_contracts');
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const embedded=fs.readFileSync(path.join(__dirname,'../src/gameplay/script_features.inc'),'utf8');
const source=Array.from(embedded.matchAll(/R"GLOSSJS\(([\s\S]*?)\)GLOSSJS"/g),m=>m[1]).join('');
let checks=0;function check(value,why){++checks;assert.ok(value,why);}
function harness(maximum=4096){
    const counts={},slots=new Map(),interactions=[],replies=[];
    const call=(name,fn)=>(...args)=>{counts[name]=(counts[name]||0)+1;return fn(...args);};
    const valid=x=>!!x&&x.valid!==false,gi={},pawn={path:'pawn',CharacterMovement:{MovementMode:1},K2_GetActorLocation:()=>({X:0,Y:0,Z:0})};
    const asc={HasMatchingGameplayTag:call('tagCheck',()=>false)};
    const classObjects=new Map();
    const component={DistanceA:300,DistanceB:500,DistanceC:1000,CurrentInteractActor:null,
        GetOwner:()=>pawn,GetInteractingActor:call('busy',()=>component.busy),
        GetValidDistance:call('range',a=>a.inRange!==false),
        CanTriggerInteract:call('can',a=>a.can!==false),DoInteract:a=>{interactions.push(a.path);return true;},
        InteractActors:{GetMaxIndex:call('maximum',()=>maximum),IsValidIndex:call('slot',i=>slots.has(i)),
            GetKey:call('key',i=>i+100),Get:call('get',key=>slots.get(key-100))}};
    let liveComponent=component;
    const UE={KismetSystemLibrary:{IsValid:call('valid',valid),GetPathName:call('path',x=>{
        if(x.isType)counts.classPath=(counts.classPath||0)+1;return x.path||'';})},
        PBZBlueprintFunctionLibrary:{GetPBZLocalCharacter:call('owner',()=>pawn)},
        PBZInteractComponent:{GetInteractComponent:()=>liveComponent},PBZPickupActor:{StaticClass:call('staticClass',()=>'pickup')},
        EMovementMode:{MOVE_Walking:1},AbilitySystemBlueprintLibrary:{GetAbilitySystemComponent:call('asc',()=>asc)},
        GameplayStatics:{GetPlayerController:()=>({}),ProjectWorldToScreen:call('project',(pc,loc,out)=>{out.value={X:loc.X,Y:loc.Y};return true;})},
        Vector2D:function(){},NewArray:()=>[],SubsystemBlueprintLibrary:{GetWorldSubsystem:()=>({GetTargetsByTagInRange:(tag,exact,center,range,out)=>{out.value=contracts.array();}})},TargetingWorldSubsystem:{StaticClass:()=> 'TargetingWorldSubsystem'}};
    contracts.install(UE,component,asc);
    const puerts={$ref:value=>({value}),$unref:ref=>ref.value,argv:{getByName:()=>gi}};
    const sandbox={console:{log:text=>replies.push(text)},Date:{now:()=>10000},require:name=>
        name==='ue'?UE:name==='puerts'?puerts:name==='Library/TsHelper'?{default:{constructGameplayTag:call('makeTag',x=>x)}}:null};
    const context=vm.createContext(sandbox,{codeGeneration:{strings:false,wasm:false}});
    const run=(command,id='',enabled=false)=>{
        replies.length=0;vm.runInContext(`(()=>{const command=${JSON.stringify(command)},id=${JSON.stringify(id)},enabled=${enabled},range=${enabled?(id==='auto_collect'?10:id==='highlight_loot'?50:0):0},ack='ACK:';${source}})();`,context);
        return replies[0];
    };
    const actor=(name,type='Loot',x=100)=>{
        if(!classObjects.has(type))classObjects.set(type,{isType:true,path:type==='Chest'?'/Game/PBZContent/Blueprint/Item/Device/BP_Chest.BP_Chest_C':'/Game/Unsupported.Type_C'});
        return {path:name,location:{X:x,Y:100,Z:0},IsA:call('isA',()=>type==='Loot'),GetClass:call('class',()=>classObjects.get(type)),
            K2_GetActorLocation:call('location',function(){return this.location;}),IsValidPickup:()=>true,IsValidInteractOutOverlap:()=>true};
    };
    // Arrow wrappers do not preserve this; actor location is an explicit closure.
    const makeActor=(...args)=>{const a=actor(...args);a.K2_GetActorLocation=call('location',()=>a.location);return a;};
    return {counts,slots,component,pawn,interactions,run,actor:makeActor,state:()=>sandbox.__glossPBZ302256,
        reset:()=>{for(const key of Object.keys(counts))delete counts[key];},setMaximum:value=>maximum=value,
        replaceComponent:()=>{liveComponent={...component};return liveComponent;}};
}
{
    const h=harness();h.run('toggle','highlight_loot',true);h.run('cleanup');h.reset();
    check(h.run('tick')==='ACK:ok|[]');check(Object.keys(h.counts).length===0,'all-off has no engine reads');
}
{
    const h=harness();for(let i=0;i<4096;i++)h.slots.set(i,h.actor('unsupported'+i,'Unsupported'));
    h.run('toggle','highlight_loot',true);h.reset();check(h.run('tick')==='ACK:ok|[]');
    check(h.counts.slot===32&&h.counts.isA===32,'all examined actors count against the budget');
    check(h.counts.classPath===1&&h.counts.staticClass===1,'same-round class paths and static class reused');
    check(h.state().scanCursor===32);h.run('tick');check(h.state().scanCursor===64,'round robin progresses');
}
{
    const h=harness();h.run('toggle','highlight_loot',true);h.reset();h.run('tick');
    check(h.counts.slot===128&&!h.counts.isA,'raw sparse holes are bounded');check(h.state().scanCursor===128);
}
for(const reason of ['busy','air']){
    const h=harness();h.slots.set(0,h.actor('loot'));h.run('toggle','auto_collect',true);
    if(reason==='busy')h.component.busy={};else h.pawn.CharacterMovement.MovementMode=3;
    h.reset();check(h.run('tick')==='ACK:ok|[]');check(!h.counts.maximum&&!h.counts.slot&&!h.counts.isA,'blocked auto-only avoids scanning '+reason);
    h.run('toggle','highlight_loot',true);h.reset();h.run('tick');check(h.counts.slot>0&&h.counts.project===1,'highlight still runs while collection blocked');
}
{
    const h=harness();const current=h.actor('current');h.slots.set(4000,current);h.component.CurrentInteractActor=current;
    h.run('toggle','auto_collect',true);h.reset();h.run('tick');
    check(h.interactions.length===1,'current target is not delayed by sparse cursor');
    check(h.counts.makeTag===7&&h.counts.tagCheck===22,'construct tags once, re-read all final gates');
    h.run('tick');check(h.interactions.length===1,'partial sweep must not clear attempted current actor');
}
{
    const h=harness();const a=h.actor('remembered');h.slots.set(0,a);
    h.run('toggle','highlight_loot',true);h.run('toggle','auto_collect',true);h.run('tick');
    check(h.interactions.length===1);check(h.state().lootEntries.size===1);
    a.location.X=140;const markers=JSON.parse(h.run('tick').slice(7));
    check(markers.length===1&&markers[0].x===140,'retained entries reproject during sparse batches');
    check(h.interactions.length===1&&h.state().attempted.has('remembered'),'one attempt survives partial sweeps');
    a.valid=false;check(h.run('tick')==='ACK:ok|[]','cached actor validity is re-read');
    check(h.state().lootEntries.size===0);a.valid=true;
    h.setMaximum(8);h.run('tick');check(h.state().scanCursor===0&&h.state().lootEntries.size===1,'shrinking map restarts bounded sweep');
    h.slots.delete(0);h.run('tick');check(!h.state().attempted.has('remembered'),'complete absent sweep releases attempt');
    h.run('cleanup');check(h.state().lootEntries.size===0&&h.state().scanSeen.size===0&&h.state().scanCursor===0,'cleanup releases caches');
}
{
    const h=harness();h.slots.set(0,h.actor('old'));h.run('toggle','highlight_loot',true);h.run('tick');
    h.slots.clear();h.replaceComponent();h.run('tick');
    check(h.state().lootEntries.size===0&&h.state().attempted.size===0,'component identity change resets candidates and attempts');
}
{
    const h=harness();for(let i=0;i<32;++i)h.slots.set(i,h.actor('loot'+i,'Loot',400-i*10));
    h.run('toggle','auto_collect',true);h.reset();h.run('tick');
    check(h.interactions.length===1&&h.interactions[0]==='loot31','nearest candidate selected independently of engine ordering');
    check(h.counts.can===2,'far-to-near ordering calls only nearest predicate plus final revalidation');
}
{
    const h=harness();for(let i=0;i<32;++i){const a=h.actor('blocked'+i,'Loot',400-i*10);a.can=false;h.slots.set(i,a);}
    h.run('toggle','auto_collect',true);h.reset();h.run('tick');
    check(h.interactions.length===0&&h.counts.can===32,'all blocked candidates retain the normal predicate without interaction');
}
{
    const h=harness(),near=h.actor('blocked-near','Loot',100),far=h.actor('valid-next','Loot',150);near.can=false;
    h.slots.set(0,far);h.slots.set(1,near);h.run('toggle','auto_collect',true);h.reset();h.run('tick');
    check(h.interactions.length===1&&h.interactions[0]==='valid-next'&&h.counts.can===3,'blocked nearest does not starve the next valid candidate');
}
console.log(`${checks} checks, 0 failures`);
