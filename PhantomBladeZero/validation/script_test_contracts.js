'use strict';
// Model the reflected container/ASC contracts used by the embedded script.
// This is a VM fixture, not evidence that a live Puerts binding is available.
function array(values=[]){return {Num:()=>values.length,Get:index=>values[index]};}
function install(UE,component,asc){
    UE.Actor=UE.Actor||{};UE.NewArray=()=>array();
    if(!UE.SubsystemBlueprintLibrary)UE.SubsystemBlueprintLibrary={GetWorldSubsystem:()=>({
        GetTargetsByTagInRange:(tag,exact,center,range,out)=>{out.value=array();}})};
    UE.TargetingWorldSubsystem=UE.TargetingWorldSubsystem||{StaticClass:()=>'targeting'};
    component.BlockTag=component.BlockTag??'component.block';
    component.SprintTag=component.SprintTag??'component.sprint';
    component.InAirTag=component.InAirTag??'component.air';
    if(!component.IsValidInteract)component.IsValidInteract=(actor,block,sprint,air)=>
        !block&&!sprint&&!air&&component.CanTriggerInteract(actor);
    if(asc&&!asc.HasAnyMatchingGameplayTags)asc.HasAnyMatchingGameplayTags=()=>false;
    return installNative(UE,component.GetOwner());
}
function installNative(UE,pawn){
    const events=[],widgets=[],hosts=[],pending=new Map(),assets=new Map();let serial=0,timerSerial=0,clock=()=>0;
    const control={destroyFailures:0,configurationFailures:0,createFailures:0,identityReadFailures:0,
        missingControl:null,throwOnInvalidObject:false,iconField:'icon',softTextureFailures:0};
    const record=(name,...args)=>events.push({name,args});
    const element=name=>({name,SetVisibility(value){this.visibility=value;record('elementVisibility',this,value);},
        SetBrushFromTexture(value){this.brush=value;record('texture',this,value);},
        SetBrushFromSoftTexture(value,matchSize){record('softTexture',this,value,matchSize);
            if(control.softTextureFailures){--control.softTextureFailures;throw Error('mock soft texture failed');}
            this.brush=value;},
        SetBrushFromMaterial(value){this.brush=value;record('material',this,value);},
        SetPBZFormatText(value){this.text=value;record('distance',this,value);}});
    function createWidget(){
        if(control.createFailures){--control.createFailures;return null;}
        const widget={path:'native-widget-'+(++serial),Glow:element('Glow'),Arrow:element('Arrow'),
            DIstanceBox:element('DIstanceBox'),DistanceText:element('DistanceText'),AI_SenseQuestFadeIn:{path:'fade-in'},
            StopAnimationsAndLatentActions(){record('stopAnimations',this);},
            SetVisibility(value){this.visibility=value;record('widgetVisibility',this,value);},
            SetRenderOpacity(value){this.opacity=value;},
            PlayAnimationForward(animation,speed,restore){record('animation',this,{animation,speed,restore});}};
        widget[control.iconField]=element(control.iconField);
        if(control.missingControl)delete widget[control.missingControl];
        widgets.push(widget);record('createWidget',widget);return widget;
    }
    function decorate(actor){
        if(!actor||typeof actor!=='object'||typeof actor.K2_GetActorLocation!=='function')return actor;
        if(!actor.AddComponentByClass)actor.AddComponentByClass=function(cls,manual,transform,deferred){
            const host={path:'native-host-'+(++serial),owner:actor,widget:null,valid:true,
                GetOwner(){return this.owner;},GetWidget(){if(control.identityReadFailures){--control.identityReadFailures;throw Error('mock identity read failed');}return this.widget;},
                SetWidget(widget){this.widget=widget;record('setWidget',this,widget);},
                SetVisibility(value){this.visibility=value;record('hostVisibility',this,value);},
                K2_DestroyComponent(owner){record('destroy',this,owner);if(control.destroyFailures){--control.destroyFailures;throw Error('mock destroy failed');}this.valid=false;}};
            for(const name of ['SetWidgetSpace','SetCollisionEnabled','SetDrawAtDesiredSize','SetDrawSize','SetPivot',
                'SetWindowFocusable','SetTickWhenOffscreen'])host[name]=function(value){
                    record(name,this,value);if(control.configurationFailures){--control.configurationFailures;throw Error('mock configure failed');}this[name+'Value']=value;};
            hosts.push(host);record('addComponent',host,{cls,manual,transform,deferred});return host;
        };
        if(!actor.FinishAddComponent)actor.FinishAddComponent=function(host,manual,transform){record('finishComponent',host,{manual,transform});host.finished=true;};
        return actor;
    }
    const originalValid=UE.KismetSystemLibrary.IsValid;
    UE.KismetSystemLibrary.IsValid=object=>{
        // Puerts rejects a released native proxy before IsValid can return false.
        // Keep the simple mode available for other contract suites, and exercise
        // this exact shipping failure separately from unrelated native errors.
        if(object&&object.validityError)throw Error(object.validityError);
        if(control.throwOnInvalidObject&&object&&object.valid===false)throw Error('passing a invalid object');
        decorate(object);return originalValid(object)&&object.valid!==false;
    };
    UE.WidgetComponent={StaticClass:()=>'WidgetComponent'};
    UE.WidgetBlueprintLibrary={Create:(...args)=>{record('createArgs',...args);return createWidget();}};
    const load=kind=>path=>{record('load',kind,path);if(!assets.has(path))assets.set(path,{path,kind});return assets.get(path);};
    UE.Class={Load:load('class')};UE.Texture2D={Load:load('texture')};UE.MaterialInstanceConstant={Load:load('material')};
    UE.EWidgetSpace={Screen:1};UE.ECollisionEnabled={NoCollision:0};UE.ESlateVisibility={SelfHitTestInvisible:4,Collapsed:1};
    UE.KismetTextLibrary={Conv_IntToText:value=>String(value)};
    UE.Transform=function(){};UE.Vector2D=function(X=0,Y=0){this.X=X;this.Y=Y;};decorate(pawn);
    const timers={setTimeout(fn,delay){const id=++timerSerial;pending.set(id,{fn,due:clock()+delay});return id;},
        clearTimeout(id){pending.delete(id);},pending,
        runDue(){let count=0;while(true){const next=[...pending].find(([,timer])=>timer.due<=clock());if(!next)break;
            if(++count>100)throw Error('mock timer runaway');pending.delete(next[0]);next[1].fn();}return count;},
        setClock(read){clock=read;}};
    return {events,widgets,hosts,assets,control,timers,decorate,count:name=>events.filter(event=>event.name===name).length};
}
function installSandbox(sandbox,native){
    native.timers.setClock(()=>sandbox.Date.now());sandbox.setTimeout=native.timers.setTimeout;sandbox.clearTimeout=native.timers.clearTimeout;
}
module.exports={array,install,installNative,installSandbox};
