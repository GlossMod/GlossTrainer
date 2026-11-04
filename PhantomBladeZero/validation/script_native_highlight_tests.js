'use strict';
// Execute the shipped embedded script against owned UObject/widget contracts.
// No game process, actual timers, rendering, or native calls are used here.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const contracts=require('./script_test_contracts');
const embedded=fs.readFileSync(path.join(__dirname,'../src/gameplay/script_features.inc'),'utf8');
const source=Array.from(embedded.matchAll(/R"GLOSSJS\(([\s\S]*?)\)GLOSSJS"/g),m=>m[1]).join('');
let checks=0;function check(value,why){++checks;assert.ok(value,why);}
function harness(){
    const slots=new Map(),replies=[],interactions=[],itemRows=new Map(),itemLookups=[];let now=10000,livePawn,liveComponent;
    const pawn={path:'pawn',CharacterMovement:{MovementMode:1},K2_GetActorLocation:()=>({X:0,Y:0,Z:0})},gi={};livePawn=pawn;
    const asc={HasMatchingGameplayTag:()=>false,HasAnyMatchingGameplayTags:()=>false};
    const component={GetOwner:()=>livePawn,GetInteractingActor:()=>null,CurrentInteractActor:null,
        CanTriggerInteract:()=>true,IsValidInteract:()=>true,DoInteract:actor=>{interactions.push(actor);return true;},
        InteractActors:{GetMaxIndex:()=>64,IsValidIndex:i=>slots.has(i),GetKey:i=>i,Get:i=>slots.get(i)}};liveComponent=component;
    const forbidden=()=>{throw Error('trainer must not touch genuine V skill/session');};
    const UE={KismetSystemLibrary:{IsValid:object=>!!object&&object.valid!==false,GetPathName:object=>object.path||'',
            IsValidSoftObjectReference:object=>!!object&&object.soft===true&&!!object.path},
        PBZBackPackSubsystem:{GetPropData:id=>{itemLookups.push(id);return itemRows.get(id)||{ItemID:'None'};}},
        PBZBlueprintFunctionLibrary:{GetPBZLocalCharacter:()=>livePawn},PBZInteractComponent:{GetInteractComponent:()=>liveComponent},
        PBZPickupActor:{StaticClass:()=>'pickup'},EMovementMode:{MOVE_Walking:1},
        AbilitySystemBlueprintLibrary:{GetAbilitySystemComponent:()=>asc,SendGameplayEventToActor:forbidden},
        PBZAbilityBlueprintLibrary:{AddLooseGameplayTag:forbidden,RemoveLooseGameplayTag:forbidden},
        UIExtensionSubsystem:{StaticClass:forbidden},GameplayStatics:{GetPlayerController:()=>({path:'pc'}),ProjectWorldToScreen:forbidden}};
    const native=contracts.install(UE,component,asc);
    const sandbox={Date:{now:()=>now},console:{log:reply=>replies.push(reply)},require:name=>name==='ue'?UE:
        name==='puerts'?{$ref:value=>({value}),$unref:ref=>ref.value,argv:{getByName:()=>gi}}:
        name==='Library/TsHelper'?{default:{constructGameplayTag:name=>name}}:null};
    contracts.installSandbox(sandbox,native);const context=vm.createContext(sandbox,{codeGeneration:{strings:false,wasm:false}});
    function run(command,id='',enabled=false,range=enabled?(id==='auto_collect'?10:id==='highlight_loot'?50:0):0){
        replies.length=0;sandbox.input={command,id,enabled,range};
        vm.runInContext(`(()=>{const {command,id,enabled,range}=input,ack='ACK:';${source}})();`,context);
        return replies.find(reply=>reply.startsWith('ACK:'));
    }
    function actor(name='loot',metres=5,type='Loot'){
        const a={path:name,location:{X:metres*100,Y:0,Z:0},IsA:()=>type==='Loot',
            GetClass:()=>({path:type==='Chest'?'/Game/PBZContent/Blueprint/Item/Device/BP_Chest.BP_Chest_C':
                '/Game/PBZContent/Blueprint/Item/Device/BP_Rubbish.BP_Rubbish_C'}),
            CurSwitchState:false,bInteracted:contracts.array([false,true]),'Not Interact':false,'Loot Item ID':'Map.wmA.001',
            IsValidPickup(){return this.collected!==true;},IsValidInteractOutOverlap:()=>true,
            K2_GetActorLocation(){return this.location;}};return native.decorate(a);
    }
    return {run,actor,slots,pawn,component,UE,native,interactions,itemRows,itemLookups,state:()=>sandbox.__glossPBZ302256,
        advance:ms=>now+=ms,runTimers:()=>native.timers.runDue(),
        replaceComponent:()=>liveComponent={...component},
        replacePawn:()=>livePawn=native.decorate({...pawn,path:'pawn-new'}),unavailable:()=>liveComponent=null};
}
function enable(h){check(h.run('toggle','highlight_loot',true)==='ACK:ok','native contract enables highlight');}
function tick(h){check(h.run('tick')==='ACK:ok|[]','tick acknowledges without overlay labels');}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);tick(h);
    const effect=h.state().nativeHighlights.get(a.path),host=effect.component,widget=effect.widget;
    check(effect.actor===a&&host.owner===a&&host.widget===widget&&host.finished,'each leaf uses its own actor-owned finished host');
    const add=h.native.events.find(e=>e.name==='addComponent');
    check(add.args[1].deferred===true&&h.native.events.findIndex(e=>e.name==='SetWidgetSpace')<h.native.events.findIndex(e=>e.name==='finishComponent'),'screen space is configured before deferred registration');
    check(host.SetWidgetSpaceValue===h.UE.EWidgetSpace.Screen&&host.SetCollisionEnabledValue===0,'native screen projection with no collision');
    check(host.SetDrawSizeValue.X===256&&host.SetDrawSizeValue.Y===256&&host.SetPivotValue.X===0.5,'native leaf size and center pivot');
    check(host.SetWindowFocusableValue===false&&widget.Arrow.visibility===h.UE.ESlateVisibility.Collapsed,'visual does not capture input or show an unpositioned arrow');
    check(widget.icon.brush.path.endsWith('T_Track_Collection.T_Track_Collection')&&widget.Glow.brush.path.includes('BG_Green.MI_'),'uses real lowercase icon field and original collection icon and green glow');
    const fade=h.native.events.find(e=>e.name==='animation').args[1];
    check(fade.animation===widget.AI_SenseQuestFadeIn&&fade.speed===1&&fade.restore===false,'native forward fade receives all reflected inputs');
    check(effect.meters===5&&widget.DistanceText.text==='5','PBZ formatted distance receives metres');
    const original=effect,loads=h.native.count('load'),animations=h.native.count('animation'),writes=h.native.count('distance');
    a.location.X=549;tick(h);check(h.state().nativeHighlights.get(a.path)===original&&h.native.count('distance')===writes,'stable rounded distance reuses widget without invalidating text');
    a.location.X=551;tick(h);check(widget.DistanceText.text==='6'&&h.native.count('distance')===writes+1,'rounded distance change updates only text');
    check(h.native.count('load')===loads&&h.native.count('animation')===animations&&h.native.count('createWidget')===1,'steady updates do not reload assets/replay fades/recreate trees');
    check(h.native.timers.pending.size===1,'only one lease guard is armed');
    check(h.run('toggle','highlight_loot',false)==='ACK:ok'&&!host.valid&&h.state().nativeHighlights.size===0,'off destroys owned host');
    check(h.native.timers.pending.size===0,'off cancels guard after complete cleanup');
    tick(h);check(h.native.count('createWidget')===1,'all-off tick does not recreate effects');
    enable(h);tick(h);check(h.native.count('createWidget')===2,'on after off creates a fresh independent leaf');
    h.run('cleanup');check(h.state().nativeHighlights.size===0&&h.native.timers.pending.size===0,'full cleanup releases native ledger and timer');
}
{
    const h=harness();for(let i=0;i<40;++i)h.slots.set(i,h.actor('loot'+i,40-i*0.5));enable(h);
    let before=0;for(let i=0;i<20;++i){tick(h);const total=h.native.count('createWidget');check(total-before<=2,'per maintenance tick creates at most two native trees');before=total;}
    check(h.state().nativeHighlights.size<=32&&h.native.hosts.filter(host=>host.valid).length<=32,'dense scene retains at most 32 hosts');
    check(h.native.hosts.every(host=>host.SetWidgetSpaceValue===1&&host.finished),'every dense-scene host uses deferred screen projection');
    h.run('cleanup');check(h.native.hosts.every(host=>!host.valid),'cleanup destroys every dense-scene host');
}
for(const change of ['range','collected','invalid','chestOpened']){
    const h=harness(),a=h.actor('target',5,change==='chestOpened'?'Chest':'Loot');h.slots.set(0,a);enable(h);tick(h);
    const old=h.state().nativeHighlights.get(a.path).component;
    if(change==='range')a.location.X=5001;
    if(change==='collected')a.collected=true;
    if(change==='invalid')a.valid=false;
    if(change==='chestOpened')a.CurSwitchState=true;
    tick(h);check(h.state().nativeHighlights.size===0&&!old.valid,'native effect clears after '+change);
}
for(const change of ['component','pawn','unavailable']){
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);tick(h);const old=h.state().nativeHighlights.get(a.path).component;
    if(change==='component')h.replaceComponent();
    if(change==='pawn')h.replacePawn();
    if(change==='unavailable')h.unavailable();
    tick(h);check(!old.valid,'session change releases old owned host: '+change);
    check(change==='unavailable'?h.state().nativeHighlights.size===0:h.state().nativeHighlights.get(a.path).component!==old,'session must not retain old effect: '+change);
}
{
    const h=harness(),oldActor=h.actor('same-path');h.slots.set(0,oldActor);enable(h);tick(h);
    const old=h.state().nativeHighlights.get(oldActor.path).component,newActor=h.actor('same-path');h.slots.set(0,newActor);tick(h);
    check(!old.valid&&h.state().nativeHighlights.get(newActor.path).actor===newActor,'same-name actor replacement cannot reuse old object identity');
}
for(const replacement of ['owner','widget','path']){
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);tick(h);const old=h.state().nativeHighlights.get(a.path).component;
    if(replacement==='owner')old.owner={path:'foreign-owner'};
    if(replacement==='widget')old.widget={path:'manual-v-widget'};
    if(replacement==='path')old.path='replacement-host';
    tick(h);check(old.valid&&!h.native.events.some(e=>e.name==='destroy'&&e.args[0]===old),'foreign host identity is never destroyed: '+replacement);
    check(h.state().nativeHighlights.get(a.path).component!==old,'replacement identity receives a fresh owned host: '+replacement);
    h.run('cleanup');check(old.valid,'cleanup still leaves foreign host untouched: '+replacement);
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);tick(h);const host=h.state().nativeHighlights.get(a.path).component;
    h.native.control.destroyFailures=1;check(h.run('toggle','highlight_loot',false)==='ACK:ok','failed destroy still acknowledges off');
    check(host.valid&&host.visibility===false&&h.state().nativeHighlights.get(a.path).retiring,'failed destroy hides and retains cleanup ownership');
    check(h.native.timers.pending.size===1,'failed cleanup retains one retry guard');
    h.advance(500);h.runTimers();check(!host.valid&&h.state().nativeHighlights.size===0&&h.native.timers.pending.size===0,'guard retries and fully releases failed owned host');
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);tick(h);const host=h.state().nativeHighlights.get(a.path).component;
    h.advance(500);h.runTimers();check(host.valid&&h.native.timers.pending.size===1,'valid lease retains effect and rearms only one guard');
    h.advance(501);h.runTimers();check(!host.valid&&h.state().nativeHighlights.size===0&&h.native.timers.pending.size===0,'expired lease removes effects without any maintenance tick');
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);h.native.control.configurationFailures=1;
    const reply=h.run('tick');check(reply.startsWith('ACK:error:mock configure failed'),'fallible deferred host configuration is reported');
    check(reply.includes('[loot stage=configure-host]')&&reply.includes('at host.'),'creation diagnostic retains stage and original stack');
    check(h.native.hosts.length===1&&!h.native.hosts[0].valid&&h.state().nativeHighlights.size===0,'pre-bind failure destroys already-owned deferred host');
    check(h.native.widgets[0].visibility===h.UE.ESlateVisibility.Collapsed&&h.native.timers.pending.size===0,'failed tree is hidden without orphaned retry timers');
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);
    h.native.control.configurationFailures=1;h.native.control.identityReadFailures=10;
    const reply=h.run('tick');
    check(reply.startsWith('ACK:error:mock configure failed')&&reply.includes('[loot stage=configure-host]'),'cleanup exception never replaces original native creation failure');
    check(h.state().nativeHighlights.size===1&&h.native.widgets[0].visibility===h.UE.ESlateVisibility.Collapsed,'temporarily unreadable host retains owned cleanup ledger and hides leaf');
    h.native.control.identityReadFailures=0;
    h.advance(1001);h.runTimers();check(h.state().nativeHighlights.size===0&&!h.native.hosts[0].valid,'lease guard recovers once host identity can be read again');
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);h.native.control.missingControl='Glow';
    check(h.run('tick').includes('layout changed'),'missing native leaf control fails visibly');
    check(h.native.hosts.length===0&&h.state().nativeHighlights.size===0,'layout failure creates no host');
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);enable(h);tick(h);const widget=h.state().nativeHighlights.get(a.path).widget;
    const original=h.UE.SubsystemBlueprintLibrary.GetWorldSubsystem;h.UE.SubsystemBlueprintLibrary.GetWorldSubsystem=()=>{throw Error('query unavailable');};
    h.advance(1000);check(h.run('tick').includes('query unavailable'),'refresh failure is reported');
    check(widget.visibility===h.UE.ESlateVisibility.Collapsed&&h.state().nativeHighlights.size===0,'refresh failure clears native visuals');
    h.UE.SubsystemBlueprintLibrary.GetWorldSubsystem=original;
}
{
    const h=harness(),a=h.actor();h.slots.set(0,a);h.run('toggle','auto_collect',true);enable(h);tick(h);
    check(h.interactions.length===1&&h.state().attempted.has(a.path),'native visuals retain normal one-attempt collection');
    a.collected=true;tick(h);check(h.state().nativeHighlights.size===0,'confirmed collected state removes native visual');
    h.run('cleanup');check(h.native.events.every(e=>!e.name.includes('GameplayEvent')),'native ownership never mutates genuine V ability state');
}
for(const change of ['off','range','collectedDestroyed','lease']){
    const h=harness(),a=h.actor();h.native.control.throwOnInvalidObject=true;h.slots.set(0,a);enable(h);tick(h);
    const host=h.state().nativeHighlights.get(a.path).component;
    if(change==='off')check(h.run('toggle','highlight_loot',false)==='ACK:ok','released host proxy does not poison off acknowledgement');
    if(change==='range'){a.location.X=5001;tick(h);}
    if(change==='collectedDestroyed'){a.collected=true;a.valid=false;tick(h);}
    if(change==='lease'){h.advance(1001);h.runTimers();}
    // A guard already queued by the preceding live tick may retire once more.
    if(change==='range'||change==='collectedDestroyed'){h.advance(500);h.runTimers();}
    check(!host.valid&&h.state().nativeHighlights.size===0&&h.native.timers.pending.size===0,'released proxy is recognized during '+change+' cleanup');
    let released=false;try{h.UE.KismetSystemLibrary.IsValid(host);}catch(error){released=error.message==='passing a invalid object';}
    check(released,'fixture models released proxy throwing before native IsValid can return false');
    a.valid=true;a.collected=false;a.location.X=500;enable(h);tick(h);
    check(h.state().nativeHighlights.size===1&&h.state().nativeHighlights.get(a.path).component!==host,'released proxy cleanup permits a fresh host after '+change);
    check(h.run('cleanup')==='ACK:ok'&&h.state().nativeHighlights.size===0,'strict proxy cleanup completes after '+change);
}
{
    const h=harness(),a=h.actor();h.native.control.iconField='Icon';h.slots.set(0,a);enable(h);tick(h);
    check(h.state().nativeHighlights.get(a.path).widget.Icon.brush===h.state().nativeHighlightAssets.icon,'uppercase field remains a supported native layout fallback');
}
{
    const h=harness();h.pawn.validityError='native validity service unavailable';
    check(h.run('toggle','highlight_loot',true).includes('native validity service unavailable'),'non-release IsValid exception remains visible to the caller');
    check(!h.state().highlight_loot,'unrelated native failure does not silently enable highlight');
}
// The shipping chest has a bool-array named bInteracted. Its contents are the
// same on open and closed instances; only the proven CurSwitchState is a gate.
for(const opened of [false,true,undefined,null,0,1,'false']){
    const h=harness(),a=h.actor('chest',5,'Chest');a.CurSwitchState=opened;h.slots.set(0,a);
    h.run('toggle','auto_collect',true);enable(h);tick(h);
    check(a.bInteracted.Num()===2&&a.bInteracted.Get(1)===true,'fixture preserves real chest bool array');
    check(h.state().nativeHighlights.has(a.path)===(opened===false),'only strict unopened chest gets a marker');
    check(h.interactions.length===(opened===false?1:0),'opened or unknown chest cannot be auto-interacted');
}
{
    const h=harness(),a=h.actor('chest',5,'Chest');h.slots.set(0,a);enable(h);tick(h);
    const host=h.state().nativeHighlights.get(a.path).component;
    a.CurSwitchState=true;tick(h);
    check(!host.valid&&!h.state().lootEntries.has(a.path),'opening retires existing marker and candidate immediately');
}
const itemField='ItemID_3_AA0CF7F84C2C8B7CDA8E3D8BF2772009';
const amountField='Amount_5_93BDB8B545F957E43232B49EA76A6DFE';
const reward=(id,amount=1)=>({[itemField]:id,[amountField]:amount});
const soft=path=>({path,soft:true});
for(const kind of ['pickup','scene']){
    const h=harness(),a=h.actor('real-item',5,kind==='pickup'?'Loot':'Scene');
    if(kind==='pickup')a.ItemId='I_03001';else a.Pickups=contracts.array([reward('I_03001',3)]);
    const row={ItemID:'I_03001',WhiteIcon:soft('/Game/Icons/real-small'),ItemIcon:soft('/Game/Icons/fallback-small')};
    h.itemRows.set(row.ItemID,row);h.slots.set(0,a);enable(h);tick(h);
    const effect=h.state().nativeHighlights.get(a.path),event=h.native.events.find(e=>e.name==='softTexture');
    check(effect.itemArt&&effect.itemArt.row===row&&effect.itemArt.texture===row.WhiteIcon,'effect retains owning row and matching item soft-reference: '+kind);
    check(effect.widget.icon.brush===row.WhiteIcon&&event.args[2]===false,'actual small icon uses native asynchronous setter without resize: '+kind);
    check(h.itemLookups.length===1&&h.itemLookups[0]===row.ItemID,'lookup uses real inventory ID rather than scene key: '+kind);
    const loads=h.native.count('load');tick(h);a.location.X+=150;tick(h);
    check(h.itemLookups.length===1&&h.native.count('softTexture')===1&&h.native.count('load')===loads,'steady maintenance does not query/reload/rewrite item art: '+kind);
    h.run('cleanup');check(!h.state().nativeHighlights.size,'cleanup releases retained item art: '+kind);
}
{
    const h=harness(),a=h.actor('fallback-small');a.ItemId='I_03001';h.slots.set(0,a);
    const row={ItemID:a.ItemId,WhiteIcon:null,ItemIcon:soft('/Game/Icons/actual-small')};h.itemRows.set(a.ItemId,row);
    enable(h);tick(h);check(h.state().nativeHighlights.get(a.path).widget.icon.brush===row.ItemIcon,'absent white icon uses actual item icon');
}
for(const testCase of ['unknown','wrong-row','no-icon','lookup-error','soft-binding-error','different-rewards','empty','bad-amount','large']){
    const h=harness(),a=h.actor(testCase,5,'Scene');a.Pickups=contracts.array([reward('I_03001')]);
    let row={ItemID:'I_03001',WhiteIcon:soft('/Game/Icons/item')};
    if(testCase==='unknown')row={ItemID:'None',WhiteIcon:soft('/Game/Icons/default')};
    if(testCase==='wrong-row')row.ItemID='OtherItem';
    if(testCase==='no-icon')row.WhiteIcon=null;
    if(testCase==='lookup-error')h.UE.PBZBackPackSubsystem.GetPropData=()=>{throw Error('optional item unavailable');};
    if(testCase==='soft-binding-error')h.native.control.softTextureFailures=1;
    if(testCase==='different-rewards')a.Pickups=contracts.array([reward('I_03001'),reward('I_03002')]);
    if(testCase==='empty')a.Pickups=contracts.array();
    if(testCase==='bad-amount')a.Pickups=contracts.array([reward('I_03001',0)]);
    if(testCase==='large')a.Pickups=contracts.array(Array.from({length:17},()=>reward('I_03001')));
    h.itemRows.set('I_03001',row);h.slots.set(0,a);h.run('toggle','auto_collect',true);enable(h);tick(h);
    const effect=h.state().nativeHighlights.get(a.path);
    check(effect&&effect.itemArt===null&&effect.widget.icon.brush===h.state().nativeHighlightAssets.icon,'optional icon safely falls back: '+testCase);
    check(h.interactions.length===1,'optional icon failure preserves auto-collection: '+testCase);
}
{
    const h=harness(),a=h.actor('same-reward',5,'Scene');a.Pickups=contracts.array([reward('I_03001',2),reward('I_03001',1)]);
    h.itemRows.set('I_03001',{ItemID:'I_03001',ItemIcon:soft('/Game/Icons/item')});h.slots.set(0,a);enable(h);tick(h);
    check(!!h.state().nativeHighlights.get(a.path).itemArt,'repeated same reward may use its single known icon');
}
for(const [getter,field] of [['GetPropData','WhiteIcon'],['GetAccessoryData','Icon'],
    ['GetMedicineData','HighPrecisionTexture'],['GetMainWeaponTableData','RenderIcon'],['GetShaqiWeaponTableData','RenderIcon']]){
    const h=harness(),a=h.actor('typed-item');a.ItemId='exact-item';h.slots.set(0,a);
    const row={ItemId:a.ItemId,[field]:soft('/Game/Icons/'+getter)},calls=[];
    const backpack={path:'backpack'};
    for(const method of ['GetPropData','GetAccessoryData','GetMedicineData','GetMainWeaponTableData','GetShaqiWeaponTableData']){
        const owner=method.includes('Weapon')?backpack:h.UE.PBZBackPackSubsystem;
        owner[method]=function(id){calls.push(method);return method===getter?row:{ItemId:'None',WhiteIcon:soft('/Game/Icons/placeholder')};};
    }
    h.UE.PBZBackPackSubsystem.GetBackPackSubsytemByContextObject=()=>backpack;
    enable(h);tick(h);const effect=h.state().nativeHighlights.get(a.path);
    check(effect.itemArt&&effect.itemArt.row===row&&effect.widget.icon.brush===row[field],'exact native ItemId selects corresponding table icon: '+getter);
    check(calls.length<=5&&calls.at(-1)===getter,'unrelated default row icons cannot misclassify: '+getter);
    const count=calls.length;tick(h);check(calls.length===count,'typed item lookup remains cached for owned effect: '+getter);
}
{
    const h=harness(),a=h.actor('missing-matched-icon');a.ItemId='known';h.slots.set(0,a);let unrelated=0;
    h.UE.PBZBackPackSubsystem.GetPropData=()=>({ItemId:'known'});
    h.UE.PBZBackPackSubsystem.GetAccessoryData=()=>{++unrelated;return {ItemId:'known',Icon:soft('/Game/Icons/wrong-category')};};
    enable(h);tick(h);
    check(unrelated===0&&h.state().nativeHighlights.get(a.path).itemArt===null,'known row without art cannot be relabeled by a later category');
}
for(const fields of ['authored','mixed','empty']){
    const h=harness(),a=h.actor('authored-item',5,'Scene');
    const item=fields==='mixed'?{[itemField]:'I_03001',Amount:2}:{ItemID:'I_03001',Amount:2};
    if(fields==='empty')item.ItemID='None';
    a.Pickups=contracts.array([item]);h.slots.set(0,a);
    const row={ItemId:'I_03001',WhiteIcon:soft('/Game/Icons/authored')};h.itemRows.set(row.ItemId,row);
    enable(h);tick(h);const effect=h.state().nativeHighlights.get(a.path);
    check(fields==='empty'?effect.itemArt===null:effect.itemArt&&effect.itemArt.row===row,'authored struct fields retain exact inventory identity: '+fields);
    check(h.itemLookups.length===(fields==='empty'?0:1),'authored alias uses no scene-key or malformed-ID lookup: '+fields);
}
console.log(`${checks} checks, 0 failures`);
