'use strict';
const contracts=require('./script_test_contracts');
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const file=fs.readFileSync(path.join(__dirname,'../src/gameplay/script_features.inc'),'utf8');
const source=Array.from(file.matchAll(/R"GLOSSJS\(([\s\S]*?)\)GLOSSJS"/g),m=>m[1]).join('');
let checks=0;
function check(value,why){++checks;assert.ok(value,why);}
function harness(){
    const slots=new Map(),calls=[],replies=[],queries=[];let now=10000,reads=0,classifications=0;
    const pawn={path:'pawn',CharacterMovement:{MovementMode:1},K2_GetActorLocation:()=>({X:0,Y:0,Z:0})},gi={};
    const valid=object=>!!object&&object.valid!==false;
    const component={BlockTag:'block',SprintTag:'sprint',InAirTag:'air',GetOwner:()=>pawn,GetInteractingActor:()=>null,CurrentInteractActor:null,
        GetValidDistance:actor=>actor.inRange!==false,CanTriggerInteract:actor=>actor.can!==false,
        IsValidInteract:(actor,block,sprint,air)=>!block&&!sprint&&!air,
        DoInteract:actor=>{calls.push(actor.path);return true;},InteractActors:{GetMaxIndex:()=>32,
            IsValidIndex:index=>slots.has(index),GetKey:index=>index,Get:index=>slots.get(index)}};
    const targetActor=actor=>actor;
    const targetSubsystem={GetTargetsByTagInRange:(tag,exact,center,range,out)=>{
        queries.push({tag,exact,center,range});const values=targetSubsystem.values||[];
        out.value=Array.isArray(values)?{Num:()=>values.length,Get:index=>{++reads;return values[index];}}:values;
    }};
    const UE={KismetSystemLibrary:{IsValid:valid,GetPathName:object=>object.path||''},
        PBZBlueprintFunctionLibrary:{GetPBZLocalCharacter:()=>pawn},PBZInteractComponent:{GetInteractComponent:()=>component},
        PBZPickupActor:{StaticClass:()=>'pickup'},EMovementMode:{MOVE_Walking:1},
        AbilitySystemBlueprintLibrary:{GetAbilitySystemComponent:()=>({HasMatchingGameplayTag:()=>false,HasAnyMatchingGameplayTags:()=>false})},Vector2D:function(){},
        GameplayStatics:{GetPlayerController:()=>({}),ProjectWorldToScreen:(pc,point,out)=>{out.value={X:10,Y:20};return true;},},
        NewArray:()=>[],SubsystemBlueprintLibrary:{GetWorldSubsystem:()=>targetSubsystem},TargetingWorldSubsystem:{StaticClass:()=> 'TargetingWorldSubsystem'}};
    contracts.install(UE,component);
    const puerts={$ref:value=>({value}),$unref:ref=>ref.value,argv:{getByName:()=>gi}};
    const sandbox={Date:{now:()=>now},console:{log:message=>replies.push(message)},require:name=>name==='ue'?UE:
        name==='puerts'?puerts:name==='Library/TsHelper'?{default:{constructGameplayTag:name=>name}}:null};
    const context=vm.createContext(sandbox,{codeGeneration:{strings:false,wasm:false}});
    const run=(command,id='',enabled=false,range=enabled?(id==='auto_collect'?10:id==='highlight_loot'?50:0):0)=>{
        replies.length=0;
        // Feed the lexical value directly, including NaN/Infinity regression cases.
        sandbox.input={command,id,enabled,range};
        vm.runInContext(`(()=>{const {command,id,enabled,range}=input,ack='ACK:';${source}})();`,context);
        return replies.find(reply=>reply.startsWith('ACK:'));
    };
    const actor=(metres,name='loot')=>({path:name,location:{X:metres*100,Y:0,Z:0},IsA:()=>{++classifications;return true;},
        IsValidPickup:()=>true,IsValidInteractOutOverlap:()=>true,K2_GetActorLocation(){return this.location;}});
    return {run,actor,slots,calls,queries,component,UE,targetSubsystem,state:()=>sandbox.__glossPBZ302256,
        advance:ms=>now+=ms,reads:()=>reads,classifications:()=>classifications,resetCounts:()=>{reads=classifications=0;}};
}
{
    const h=harness(),a=h.actor(20);h.targetSubsystem.values=[a];
    h.run('toggle','highlight_loot',true,50);const markers=JSON.parse(h.run('tick').slice(7));
    check(h.queries.length===1&&h.queries[0].range===5000,'targeting query uses configured highlight range');
    check(markers.length===1,'targeting result extends highlight beyond short local map');
    h.run('toggle','highlight_loot',false);h.targetSubsystem.values=[a];h.run('toggle','auto_collect',true,10);h.run('tick');
    check(h.queries.at(-1).range===1000&&h.calls.length===0,'auto query uses its independent 10 metre range');
    h.targetSubsystem.values={Num:()=>1,Get:()=>a};h.run('toggle','highlight_loot',true,50);
    check(JSON.parse(h.run('tick').slice(7)).length===1,'TArray-like targeting output is consumed safely');
}
{
    const h=harness();h.run('tick');
    check(h.state().autoDistanceM===10&&h.state().highlightDistanceM===50,'independent defaults in metres');
    const a=h.actor(50),b=h.actor(50.01,'far');h.slots.set(0,a);h.slots.set(1,b);
    check(h.run('toggle','highlight_loot',true)==='ACK:ok');
    const markers=JSON.parse(h.run('tick').slice(7));check(markers.length===1&&markers[0].distance===50,'default highlight boundary is 50 metres');
    check(h.calls.length===0,'highlights do not collect');
}
for(const id of ['auto_collect','highlight_loot']){
    const maximum=id==='auto_collect'?50:100,key=id==='auto_collect'?'autoDistanceM':'highlightDistanceM';
    for(const bad of [0,-1,maximum+0.01,NaN,Infinity,-Infinity,'10',null,undefined]){
        const h=harness();let result;
        if(bad===undefined){h.run('tick');h.state()[key]=17;result=h.run('toggle',id,true,null);}
        else result=h.run('toggle',id,true,bad);
        check(result.startsWith('ACK:error:'),'reject invalid '+id+' range '+String(bad));
        check(h.state()[id]===false,'invalid range never enables '+id);
    }
    for(const value of [1,1.25,maximum]){
        const h=harness();check(h.run('toggle',id,true,value)==='ACK:ok','valid boundary '+id+' '+value);
        check(h.state()[key]===value,'exact range stored after success');
    }
    const h=harness();h.run('toggle',id,true,7.5);
    check(h.run('toggle',id,false,7.5).startsWith('ACK:error:'),'off rejects unexpected range');
    check(h.state()[id]===true,'malformed off preserves on state');
    check(h.run('toggle',id,false)==='ACK:ok','off is parameter free');
    check(h.state()[key]===7.5,'off does not mutate remembered successful value');
}
{
    const h=harness();h.run('toggle','auto_collect',true,8);h.run('toggle','highlight_loot',true,72.5);
    check(h.state().autoDistanceM===8&&h.state().highlightDistanceM===72.5,'editing highlight cannot alter collection');
    h.run('toggle','auto_collect',true,3.25);
    check(h.state().autoDistanceM===3.25&&h.state().highlightDistanceM===72.5,'editing collection cannot alter highlight');
    const a=h.actor(3.26);h.slots.set(0,a);h.run('tick');check(h.calls.length===0,'collection applies its independent exact limit');
    a.location.X=325;h.run('tick');check(h.calls.length===1,'collection includes its boundary');
}
{
    const h=harness();h.run('toggle','highlight_loot',true,40);
    h.UE.GameplayStatics.ProjectWorldToScreen=null;
    check(h.run('toggle','highlight_loot',true,80).startsWith('ACK:error:'),'failed reconfiguration reports error');
    check(h.state().highlightDistanceM===40&&h.state().highlight_loot,'failed reconfiguration restores prior distance and state');
}
{
    const h=harness(),a=h.actor(9);h.slots.set(0,a);h.run('toggle','auto_collect',true);
    h.component.IsValidInteract=()=>{a.location.X=1001;return true;};
    h.run('tick');check(h.calls.length===0,'collection rechecks configured distance after candidate predicate');
}
{
    const h=harness(),a=h.actor(80);h.slots.set(0,a);h.run('toggle','highlight_loot',true,100);
    check(JSON.parse(h.run('tick').slice(7)).length===1,'retained candidate initially visible');
    h.run('toggle','highlight_loot',true,50);
    check(JSON.parse(h.run('tick').slice(7)).length===0,'decreasing range removes retained marker on next tick');
}
{
    const h=harness();h.targetSubsystem.values=Array.from({length:160},(_,i)=>h.actor(20,'target'+i));
    h.run('toggle','highlight_loot',true,50);h.run('tick');
    check(h.reads()<=64,'refresh consumes no full output array: at most new32 plus retained32');
    check(h.state().targetingCursor===32,'empty local map gives all 32 slots to targeting');
    for(let round=1;round<5;++round){h.advance(250);h.run('tick');}
    check(h.queries.length===2&&h.state().targetingCursor===0,'1Hz refresh preserves progress to the final 32 targets');
    check(h.state().lootEntries.size===32,'retained candidates remain bounded');
    h.run('cleanup');const before=h.queries.length;h.advance(2000);h.run('tick');
    check(h.queries.length===before&&h.state().targetingPool===null,'all-off releases pool and performs no query');
}
{
    const h=harness(),near=h.actor(2,'near');
    h.targetSubsystem.values=[near,...Array.from({length:159},(_,i)=>h.actor(40,'far'+i))];
    h.run('toggle','auto_collect',true,10);h.run('toggle','highlight_loot',true,50);
    for(let round=0;round<12;++round){h.run('tick');h.advance(250);}
    check(h.calls.length===1&&h.calls[0]==='near','short native sweeps never clear accepted targets outside the current target batch');
    check(h.state().lootEntries.has('near'),'far highlight results cannot evict a nearby collection candidate');
}
{
    const h=harness(),a=h.actor(5);h.targetSubsystem.values=[a];h.run('toggle','auto_collect',true,10);
    h.component.CanTriggerInteract=()=>{throw Error('short overlap gate must not run');};
    h.component.GetValidDistance=()=>{throw Error('short radius gate must not run');};
    h.run('tick');check(h.calls.length===1,'range-source loot interacts without original short-range gates');
}
for(const change of ['range','invalid','owner','state','path']){
    const h=harness(),a=h.actor(5);h.targetSubsystem.values=[a];h.run('toggle','auto_collect',true,10);let predicates=0;
    a.IsValidInteractOutOverlap=()=>{
        if(++predicates===2){
            if(change==='range')a.location.X=1001;
            if(change==='invalid')a.valid=false;
            if(change==='owner')h.component.GetOwner=()=>({});
            if(change==='state')h.component.GetInteractingActor=()=>({});
            if(change==='path')a.path='other';
        }
        return true;
    };
    check(h.run('tick').startsWith('ACK:ok|'),'last OutOverlap callback completes: '+change);
    check(h.calls.length===0,'fresh final guard rejects last callback change: '+change);
}
{
    const h=harness(),a=h.actor(5),b=h.actor(6,'supported');delete a.IsValidInteractOutOverlap;
    h.targetSubsystem.values=[a,b];h.run('toggle','auto_collect',true,10);
    check(h.run('tick').startsWith('ACK:ok|'),'single missing actor binding does not fail the script group');
    check(h.calls.length===1&&h.calls[0]==='supported'&&h.state().lootDiagnostics.size===1,'unsupported actor is diagnosed and skipped');
}
{
    const h=harness();h.run('toggle','highlight_loot',true);h.targetSubsystem.GetTargetsByTagInRange=()=>{throw Error('query failed');};
    check(h.run('tick').startsWith('ACK:error:'),'query failure never silently falls back to short range');
}
{
    const h=harness();h.targetSubsystem.values={Num:()=>4097,Get:()=>{throw Error('must not read oversized output');}};
    h.run('toggle','highlight_loot',true);check(h.run('tick').includes('targeting output exceeds supported bounds'),'oversized result rejects before any element read');
}
{
    const h=harness(),near=h.actor(2,'accepted');
    const old=Array.from({length:160},(_,i)=>h.actor(35,'old'+i));old[100]=near;
    h.targetSubsystem.values=old;h.run('toggle','auto_collect',true,10);h.run('toggle','highlight_loot',true,50);
    for(let round=0;round<6;++round){h.run('tick');h.advance(250);}
    check(h.calls.length===1&&h.state().targetingCursor===32,'accepted target is outside the next unfinished batch');
    const shorter=Array.from({length:64},(_,i)=>h.actor(35,'new'+i));shorter[0]=near;
    h.targetSubsystem.values=shorter;h.run('toggle','highlight_loot',true,40);
    h.run('tick');h.advance(250);h.run('tick');
    check(h.calls.length===1&&h.state().attempted.has('accepted'),'range shrink and reordered source cannot replay an accepted actor');
}
check(!source.includes('GetAllActorsOfClass(')&&!source.includes('component.DistanceA='),'no world scan or search-distance mutation');
{
    const h=harness(),a=h.actor(2,'reenter');h.targetSubsystem.values=[a];
    h.run('toggle','auto_collect',true,10);h.run('tick');check(h.calls.length===1,'first target request is accepted');
    h.targetSubsystem.values=[];h.advance(1000);h.run('tick');
    check(!h.state().attempted.has('reenter'),'complete empty current-generation sources prove target departure');
    h.targetSubsystem.values=[a];h.advance(1000);h.run('tick');
    check(h.calls.length===2,'confirmed departure permits a later return');
}
console.log(`${checks} checks, 0 failures`);
