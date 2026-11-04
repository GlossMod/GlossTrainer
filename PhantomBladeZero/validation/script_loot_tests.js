'use strict';
const contracts=require('./script_test_contracts');
// Reproduce the v1.1.2 purple world-loot carrier without a game process.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const embedded=fs.readFileSync(path.join(__dirname,'../src/gameplay/script_features.inc'),'utf8');
const source=Array.from(embedded.matchAll(/R"GLOSSJS\(([\s\S]*?)\)GLOSSJS"/g),m=>m[1]).join('');
const rubbishClass='/Game/PBZContent/Blueprint/Item/Device/BP_Rubbish.BP_Rubbish_C';
let checks=0;
function check(value,message){++checks;assert.ok(value,message);}
function harness(){
    let now=10000, liveComponent;
    const replies=[],calls=[],slots=new Map(),rangeCalls=[];
    const valid=x=>!!x&&x.valid!==false;
    const gi={},pawn={path:'pawn',CharacterMovement:{MovementMode:1},K2_GetActorLocation:()=>({X:0,Y:0,Z:0})};
    const asc={tags:new Set(),HasMatchingGameplayTag(name){return this.tags.has(name);}};
    const component={DistanceA:0,DistanceB:0,DistanceC:0,CurrentInteractActor:null,
        GetOwner:()=>pawn,GetInteractingActor:()=>component.busy,
        GetValidDistance:(actor,player)=>{rangeCalls.push([actor,player]);return actor.inRange!==false;},
        CanTriggerInteract:actor=>actor.can!==false,
        DoInteract:actor=>{calls.push(actor.path);return actor.result!==false;},
        InteractActors:{GetMaxIndex:()=>32,IsValidIndex:index=>slots.has(index),GetKey:index=>index+100,
            Get:key=>slots.get(key-100)}};
    liveComponent=component;
    const UE={KismetSystemLibrary:{IsValid:valid,GetPathName:object=>object.path||''},
        PBZBlueprintFunctionLibrary:{GetPBZLocalCharacter:object=>object.foreign?{}:pawn},
        PBZInteractComponent:{GetInteractComponent:()=>liveComponent},PBZPickupActor:{StaticClass:()=>'pickup'},
        AbilitySystemBlueprintLibrary:{GetAbilitySystemComponent:()=>asc},EMovementMode:{MOVE_Walking:1},
        GameplayStatics:{GetPlayerController:()=>({}),ProjectWorldToScreen:(pc,point,out)=>{out.value={X:10,Y:20};return true;}},
        Vector2D:function(){},NewArray:()=>[],SubsystemBlueprintLibrary:{GetWorldSubsystem:()=>({GetTargetsByTagInRange:(tag,exact,center,range,out)=>{out.value=contracts.array();}})},TargetingWorldSubsystem:{StaticClass:()=> 'TargetingWorldSubsystem'}};
    const native=contracts.install(UE,component,asc);
    const puerts={$ref:value=>({value}),$unref:ref=>ref.value,argv:{getByName:()=>gi}};
    const sandbox={console:{log:reply=>replies.push(reply)},Date:{now:()=>now},require:name=>
        name==='ue'?UE:name==='puerts'?puerts:name==='Library/TsHelper'?{default:{constructGameplayTag:name=>name}}:null};
    contracts.installSandbox(sandbox,native);
    const context=vm.createContext(sandbox,{codeGeneration:{strings:false,wasm:false}});
    const run=(command,id='',enabled=false)=>{
        replies.length=0;
        vm.runInContext(`(()=>{const command=${JSON.stringify(command)},id=${JSON.stringify(id)},enabled=${enabled},range=${enabled?(id==='auto_collect'?10:id==='highlight_loot'?50:0):0},ack='ACK:';${source}})();`,context);
        return replies[0];
    };
    const actor=(name='world-loot')=>({path:name,'Loot Item ID':'Zhengdaodaying.wmA.008','Item ID':'None',
        'Not Interact':false,Pickups:[{ItemId:'Weapon_Material_05',Quantity:4}],PickupMap:new Map(),
        IsA:()=>false,GetClass:()=>({path:rubbishClass}),location:{X:600,Y:0,Z:0},
        K2_GetActorLocation(){return this.location;},IsValidInteractOutOverlap(){return this.inRange!==false;}});
    return {run,actor,slots,calls,rangeCalls,component,pawn,asc,UE,native,state:()=>sandbox.__glossPBZ302256,
        advance:milliseconds=>now+=milliseconds,replaceComponent:()=>liveComponent={...component}};
}
{
    const h=harness(),actor=h.actor();h.slots.set(3,actor);h.component.CurrentInteractActor=actor;
    check(h.run('toggle','auto_collect',true)==='ACK:ok');
    check(h.run('toggle','highlight_loot',true)==='ACK:ok');
    const markers=JSON.parse(h.run('tick').slice('ACK:ok|'.length));
    check(markers.length===0&&h.state().lootEntries.get(actor.path).label==='Loot','scene drop key is classified as Loot without overlay output');
    check(h.calls.length===1,'real 600 cm actor with zero tier distances uses native predicate');
    check(h.rangeCalls.length===0,'custom range does not call the original short-radius predicate');
    check(typeof actor.IsValidPickup==='undefined','BP_Rubbish does not inherit the native pickup method');
    check(h.state().attempted.has(actor.path)&&h.state().retryAfter.size===0,'accepted request is not retried while present');
    h.advance(5000);h.run('tick');check(h.calls.length===1,'accepted actor does not trigger every cooldown');
}
for(const condition of ['none','empty','missing','notInteract','missingFlag','wrongFlag','fakeClass','subclass','whitespace','invalid','foreign']){
    const h=harness(),a=h.actor();h.slots.set(3,a);
    if(condition==='none')a['Loot Item ID']='None';
    if(condition==='empty')a['Loot Item ID']='';
    if(condition==='missing')delete a['Loot Item ID'];
    if(condition==='notInteract')a['Not Interact']=true;
    if(condition==='missingFlag')delete a['Not Interact'];
    if(condition==='wrongFlag')a['Not Interact']=0;
    if(condition==='fakeClass')a.GetClass=()=>({path:'/Game/Other/BP_Rubbish.BP_Rubbish_C'});
    if(condition==='subclass')a.GetClass=()=>({path:rubbishClass+'_Child'});
    if(condition==='whitespace')a['Loot Item ID']='Area. bad';
    if(condition==='invalid')a.valid=false;
    if(condition==='foreign')a.foreign=true;
    h.run('toggle','auto_collect',true);h.run('toggle','highlight_loot',true);
    check(h.run('tick')==='ACK:ok|[]','reject unsupported carrier: '+condition);
    check(h.calls.length===0,'no interaction: '+condition);
}
for(const condition of ['nativeRange','busy','air','combat','predicate','staleMap','nativePickup']){
    const h=harness(),a=h.actor();h.slots.set(3,a);h.run('toggle','auto_collect',true);
    if(condition==='nativeRange')a.inRange=false;
    if(condition==='busy')h.component.busy={};
    if(condition==='air')h.pawn.CharacterMovement.MovementMode=3;
    if(condition==='combat')h.asc.tags.add('CharacterState.InCombat');
    if(condition==='predicate')a.can=false;
    if(condition==='staleMap')h.component.CanTriggerInteract=()=>{h.slots.clear();return true;};
    if(condition==='nativePickup'){a.IsA=()=>true;a.IsValidPickup=()=>false;}
    check(h.run('tick').startsWith('ACK:ok|'),'normal predicate completed: '+condition);
    check(h.calls.length===0,'final boundary retained: '+condition);
}
for(const change of ['membership','mapReplacement','component','pawn','notInteract','path','class','invalid']){
    const h=harness(),a=h.actor();h.slots.set(3,a);h.component.CurrentInteractActor=a;
    h.run('toggle','auto_collect',true);let predicates=0;
    h.component.CanTriggerInteract=()=>{
        if(++predicates===2){
            if(change==='membership'){h.slots.clear();h.component.CurrentInteractActor=null;}
            if(change==='mapReplacement'){
                h.component.CurrentInteractActor=null;
                h.component.InteractActors={GetMaxIndex:()=>0,IsValidIndex:()=>false,GetKey:()=>0,Get:()=>null};
            }
            if(change==='component')h.replaceComponent();
            if(change==='pawn')h.UE.PBZBlueprintFunctionLibrary.GetPBZLocalCharacter=()=>({});
            if(change==='notInteract')a['Not Interact']=true;
            if(change==='path')a.path='replaced-world-loot';
            if(change==='class')a.GetClass=()=>({path:'/Game/Other.Other_C'});
            if(change==='invalid')a.valid=false;
        }
        return true;
    };
    check(h.run('tick').startsWith('ACK:ok|'),'last predicate completes: '+change);
    check(predicates===2&&h.calls.length===0,'fresh identity after the last callback rejects '+change);
}
{
    const h=harness(),a=h.actor();h.slots.set(3,a);h.run('toggle','auto_collect',true);
    let invocation=0;a.IsValidInteractOutOverlap=()=>++invocation===1;
    h.run('tick');check(invocation===2&&h.calls.length===0,'native range is checked again immediately before interaction');
}
{
    const h=harness(),a=h.actor();a.result=false;h.slots.set(3,a);h.run('toggle','auto_collect',true);
    h.run('tick');check(h.calls.length===1&&!h.state().attempted.has(a.path),'false result is not recorded as accepted');
    h.advance(1499);h.run('tick');check(h.calls.length===1,'failed request is rate limited');
    h.advance(1);a.result=true;h.run('tick');check(h.calls.length===2&&h.state().attempted.has(a.path),'failure retries after 1.5 seconds');
    h.advance(1500);h.run('tick');check(h.calls.length===2,'success ends failure retry');
    h.run('cleanup');check(h.state().retryAfter.size===0&&h.state().attempted.size===0,'cleanup clears both ledgers');
}
{
    const h=harness(),a=h.actor('first'),b=h.actor('second');a.result=false;b.result=false;
    a.location.X=100;h.slots.set(3,a);h.slots.set(4,b);h.run('toggle','auto_collect',true);
    h.run('tick');check(h.calls.length===1,'at most one interaction in a round');
    h.run('tick');check(h.calls.length===2&&h.calls[1]==='second','failed nearest candidate does not starve other loot');
    h.run('toggle','auto_collect',false);check(h.state().retryAfter.size===0,'off clears retry ownership');
}
{
    const h=harness(),a=h.actor();a.result=false;h.slots.set(3,a);h.run('toggle','auto_collect',true);h.run('tick');
    h.slots.clear();h.replaceComponent();h.run('tick');check(h.state().retryAfter.size===0,'component replacement clears retry ledger');
}
{
    const h=harness();delete h.UE.TargetingWorldSubsystem;
    check(h.run('toggle','auto_collect',true).includes('targeting range query unavailable'),'missing native distance contract fails explicitly');
    check(h.state().auto_collect===false,'failed toggle rolls back');
}
console.log(`${checks} checks, 0 failures`);
