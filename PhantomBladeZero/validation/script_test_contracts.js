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
}
module.exports={array,install};
