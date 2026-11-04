'use strict';
const contracts=require('./script_test_contracts');
// Execute the embedded loot script in an isolated VM. No game process or calls.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert'), crypto = require('crypto');
const embedded = fs.readFileSync(path.join(__dirname, '../src/gameplay/script_features.inc'), 'utf8').replace(/^\uFEFF/, '');
const source = Array.from(embedded.matchAll(/R"GLOSSJS\(([\s\S]*?)\)GLOSSJS"/g), match => match[1]).join('');
const noteClass = '/Game/PBZContent/Blueprint/Item/Device/BP_Rubbish.BP_Rubbish_C';
let checks = 0;
function check(value, reason) { ++checks; assert.ok(value, reason); }
const arraySource = /const notePageIds = new Set\((\[[\s\S]*?\])\);/.exec(source);
check(arraySource, 'embedded page allowlist exists');
const ids = vm.runInNewContext(arraySource[1]);
check(ids.length === 218 && new Set(ids).size === 218, 'all 218 exact page rows');
check(crypto.createHash('sha256').update([...ids].sort().join('\n')).digest('hex') ===
    '496894e31b63e8ecae46c8d5861e740d8431e343ca7574324764630ef4882def', 'full FName suffixes match captured IDs');
check(source.includes('e8da54e6f60661b30b24b02d8c23ac1e9823bde626e6c7792cb70be129bc02d4'), 'source capture hash retained');

function harness() {
    const calls = [], replies = [], candidates = new Map();
    const valid = object => !!object && object.valid !== false;
    const gi = {}, pawn = {path:'pawn', CharacterMovement:{MovementMode:1}, K2_GetActorLocation:() => ({X:0,Y:0,Z:0})};
    const asc = {HasMatchingGameplayTag:() => false};
    const component = {DistanceA:300, DistanceB:500, DistanceC:1000, CurrentInteractActor:null,
        GetOwner:() => pawn, GetInteractingActor:() => null, CanTriggerInteract:actor => actor.can !== false,
        GetValidDistance:actor => actor.inRange !== false,
        DoInteract:actor => { calls.push(actor.path); return true; }, InteractActors:{GetMaxIndex:() => 8,
            IsValidIndex:index => candidates.has(index), GetKey:index => index + 100, Get:key => candidates.get(key - 100)}};
    const UE = {KismetSystemLibrary:{IsValid:valid, GetPathName:object => object.path || ''},
        PBZBlueprintFunctionLibrary:{GetPBZLocalCharacter:() => pawn},
        PBZInteractComponent:{GetInteractComponent:() => component}, PBZPickupActor:{StaticClass:() => 'pickup'},
        EMovementMode:{MOVE_Walking:1}, AbilitySystemBlueprintLibrary:{GetAbilitySystemComponent:() => asc},
        Vector2D:function() {}, NewArray:()=>[],SubsystemBlueprintLibrary:{GetWorldSubsystem:()=>({GetTargetsByTagInRange:(tag,exact,center,range,out)=>{out.value=contracts.array();}})},TargetingWorldSubsystem:{StaticClass:()=> 'TargetingWorldSubsystem'}, GameplayStatics:{GetPlayerController:() => ({}),
            ProjectWorldToScreen:(pc, location, out) => { out.value = {X:100,Y:200}; return true; }}};
    const native=contracts.install(UE,component,asc);
    const puerts = {$ref:value => ({value}), $unref:reference => reference.value, argv:{getByName:() => gi}};
    const sandbox = {console:{log:message => replies.push(message)}, Date:{now:() => 10000},
        require:name => name === 'ue' ? UE : name === 'puerts' ? puerts :
            name === 'Library/TsHelper' ? {default:{constructGameplayTag:name => name}} : null};
    contracts.installSandbox(sandbox,native);
    const context = vm.createContext(sandbox,{codeGeneration:{strings:false,wasm:false}});
    function run(command, id = '', enabled = false) {
        replies.length = 0;
        vm.runInContext(`(()=>{const command=${JSON.stringify(command)},id=${JSON.stringify(id)},enabled=${enabled},range=${enabled?(id==='auto_collect'?10:id==='highlight_loot'?50:0):0},ack='ACK:';${source}})();`, context);
        return replies[0];
    }
    function actor(id = 'Zhengdaodaying.R_03013', classPath = noteClass) {
        return {path:'paper', 'Not Interact':false, 'Loot Item ID':id, IsA:() => false, GetClass:() => ({path:classPath}),
            K2_GetActorLocation:() => ({X:100,Y:0,Z:0}),IsValidInteractOutOverlap:()=>true};
    }
    return {run, actor, candidates, component, calls,native,state:()=>sandbox.__glossPBZ302256};
}

for (const id of ['Zhengdaodaying.R_03013', 'Map.Page_CS_12', 'Prefix.Area.R_12020']) {
    const h = harness(), actor = h.actor({toString:() => id});
    h.candidates.set(3, actor);
    check(h.run('toggle', 'highlight_loot', true) === 'ACK:ok');
    check(h.run('toggle', 'auto_collect', true) === 'ACK:ok');
    const markers = JSON.parse(h.run('tick').slice('ACK:ok|'.length));
    check(markers.length === 0 && h.state().lootEntries.get('paper').label === 'Note', 'exact page classified as Note without overlay output');
    check(h.calls.length === 1 && h.calls[0] === 'paper', 'normal interaction invoked');
    h.run('tick'); check(h.calls.length === 1, 'paper is attempted only once while present');
    check(h.run('cleanup') === 'ACK:ok'); check(h.run('tick') === 'ACK:ok|[]');
}

for (const reason of ['noPrefix', 'emptyPrefix', 'emptySuffix', 'missing', 'notInteract', 'missingInteract',
    'getterThrows', 'conversionThrows', 'nonString', 'fakeClass', 'subclass', 'invalid']) {
    const h = harness(), actor = h.actor();
    if (reason === 'notInteract') actor['Not Interact'] = true;
    if (reason === 'missingInteract') delete actor['Not Interact'];
    if (reason === 'noPrefix') actor['Loot Item ID'] = 'R_03013';
    if (reason === 'emptyPrefix') actor['Loot Item ID'] = '.R_03013';
    if (reason === 'emptySuffix') actor['Loot Item ID'] = 'Area.';
    if (reason === 'missing') delete actor['Loot Item ID'];
    if (reason === 'getterThrows') Object.defineProperty(actor, 'Loot Item ID', {get() { throw Error('unknown property'); }});
    if (reason === 'conversionThrows') actor['Loot Item ID'] = {toString() { throw Error('unavailable name'); }};
    if (reason === 'nonString') actor['Loot Item ID'] = {toString:() => ({})};
    if (reason === 'fakeClass') actor.GetClass = () => ({path:'/Game/Fake/BP_Rubbish.BP_Rubbish_C'});
    if (reason === 'subclass') actor.GetClass = () => ({path:noteClass + '_Child'});
    if (reason === 'invalid') actor.valid = false;
    h.candidates.set(3, actor); h.run('toggle', 'highlight_loot', true); h.run('toggle', 'auto_collect', true);
    check(h.run('tick') === 'ACK:ok|[]', 'unknown paper skipped without failing tick: ' + reason);
    check(h.calls.length === 0, 'no unknown interaction: ' + reason);
}

for (const change of ['blocked', 'idChanged', 'classChanged']) {
    const h = harness(), actor = h.actor(); h.candidates.set(3, actor);
    h.run('toggle', 'auto_collect', true);
    h.component.CanTriggerInteract = () => {
        if (change === 'idChanged') actor['Loot Item ID'] = 'Area.Item_001';
        if (change === 'classChanged') actor.GetClass = () => ({path:'/Game/Fake/Note.Note_C'});
        return change !== 'blocked';
    };
    check(h.run('tick').startsWith('ACK:ok|'), 'normal interaction predicate completed');
    check(h.calls.length === 0, 'rejected before DoInteract: ' + change);
}
// Scene loot keys are not inventory IDs; an unrecognized suffix must never be
// presented as a verified page. The normal interaction predicate decides usability.
for (const id of ['Area.UnknownPage', 'Area.Book_12', 'Area.Page_CS', 'Area.r_03013']) {
    const h=harness(), actor=h.actor(id); actor.can=false; h.candidates.set(3,actor);
    h.run('toggle','highlight_loot',true); h.run('toggle','auto_collect',true);
    const markers=JSON.parse(h.run('tick').slice('ACK:ok|'.length));
    check(markers.length===0 && h.state().lootEntries.get('paper').label==='Loot'&&h.state().nativeHighlights.size===1,'unknown suffix is not mislabeled Note and receives native art');
    check(h.calls.length===0,'normal game predicate blocks unavailable scene loot');
}
console.log(`${checks} checks, 0 failures`);
