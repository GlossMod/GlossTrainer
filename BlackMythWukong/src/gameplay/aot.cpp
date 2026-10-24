#include "aot.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <atomic>
#include <cstring>
#include <format>

namespace gloss::gameplay::aot {
namespace {
std::atomic_bool ready{};
struct Assembly {std::uintptr_t table{},codeStart{},codeEnd{};std::uint32_t count{},metadataCount{};};
Assembly mainAssembly{},scriptAssembly{},runtimeAssembly{},protoAssembly{};
constexpr char Guid[]="65A46ED7-6F85-40F5-AD32-079A507252B6";
bool StringEquals(std::uintptr_t address,const char* expected) {
    char text[64]{};const auto length=std::strlen(expected)+1;
    return length<=sizeof(text)&&ReadMemory(address,text,length)&&std::memcmp(text,expected,length)==0;
}
bool Executable(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory))&&memory.State==MEM_COMMIT&&
        !(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
std::uintptr_t Resolve(const Assembly& assembly,std::uint32_t rid) {
    if(!rid||rid>assembly.metadataCount||rid>assembly.count)return 0;
    const auto entry=assembly.table+static_cast<std::uintptr_t>(rid-1)*5;
    std::array<std::byte,5> bytes{};
    if(!ReadMemory(entry,bytes.data(),5)||bytes[0]!=std::byte{0xe8})return 0;
    std::int32_t displacement{};std::memcpy(&displacement,bytes.data()+1,4);
    const auto address=entry+5+displacement;
    return address>=assembly.codeStart&&address<assembly.codeEnd&&Executable(address)?address:0;
}
}
bool Load(HMODULE module,const char* name,const char* guid,std::uint32_t metadataCount,Assembly& assembly) {
    const auto base=reinterpret_cast<std::uintptr_t>(module),dos=base;
    const auto mz=Read<IMAGE_DOS_HEADER>(dos);
    if(mz.e_magic!=IMAGE_DOS_SIGNATURE||mz.e_lfanew<0||mz.e_lfanew>0x10000)return false;
    const auto nt=Read<IMAGE_NT_HEADERS64>(base+mz.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt.OptionalHeader.SizeOfImage>0x70000000)return false;
    const auto sectionAddress=base+mz.e_lfanew+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    unsigned matches{};std::uintptr_t header{};
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections&&i<96;++i) {
        const auto section=Read<IMAGE_SECTION_HEADER>(sectionAddress+i*sizeof(IMAGE_SECTION_HEADER));
        if(!(section.Characteristics&IMAGE_SCN_MEM_READ)||(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)||section.VirtualAddress>=nt.OptionalHeader.SizeOfImage||section.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-section.VirtualAddress)continue;
        const auto begin=base+section.VirtualAddress,end=begin+section.Misc.VirtualSize;
        for(auto p=(begin+7)&~std::uintptr_t(7);p+0x18c<=end;p+=8) {
            // Fast marker first; the immutable assembly identity and bounds
            // must all agree before accepting a relocated runtime structure.
            if(*reinterpret_cast<const std::uint32_t*>(p)!=180||*reinterpret_cast<const std::uint32_t*>(p+4)!=0)continue;
            if(Read<std::uint32_t>(p+0x188)!=5||Read<std::uint32_t>(p+0x154)<metadataCount)continue;
            if(!StringEquals(Read<std::uintptr_t>(p+0x108),name)||!StringEquals(Read<std::uintptr_t>(p+0xc0),guid))continue;
            const auto start=Read<std::uintptr_t>(p+0x30),finish=Read<std::uintptr_t>(p+0x38),addresses=Read<std::uintptr_t>(p+0x40);
            const auto methods=Read<std::uint32_t>(p+0x154);
            if(start<base||finish<=start||finish>base+nt.OptionalHeader.SizeOfImage||methods>1000000||addresses<base||
                addresses>base+nt.OptionalHeader.SizeOfImage||static_cast<std::uint64_t>(methods)*5>base+nt.OptionalHeader.SizeOfImage-addresses||!Executable(start)||!Executable(finish-1))continue;
            header=p;++matches;
        }
    }
    if(matches!=1){Log(std::format("Mono AOT assembly identity: {} matches; rejected",matches));return false;}
    assembly={Read<std::uintptr_t>(header+0x40),Read<std::uintptr_t>(header+0x30),Read<std::uintptr_t>(header+0x38),Read<std::uint32_t>(header+0x154),metadataCount};
    return true;
}
bool Initialize(HMODULE module) {
    ready=false;
    if(!Load(module,"BtlSvr.Main",Guid,106230,mainAssembly))return false;
    // The getter is independently known from the existing player's virtual
    // dispatch resolver. This cross-check rejects a shifted table/index base.
    const auto getter=Resolve(mainAssembly,0x8ca2);
    const auto expected=Pattern::Parse("55 48 8B EC 48 83 EC 30 48 89 4D F8 48 89 55 F0 E8");
    if(!getter||FindPattern(expected,getter,expected.bytes.size()).size()!=1){Log("Mono AOT getter cross-check failed");return false;}
    if(!Load(module,"B1UI_GSE.Script","64AC5451-3E83-486D-B18A-128163A8971C",22103,scriptAssembly))return false;
    const auto cost=Resolve(scriptAssembly,0x374c);
    const auto prologue=Pattern::Parse("55 48 8B EC");
    if(!cost||FindPattern(prologue,cost,prologue.bytes.size()).size()!=1){Log("Mono AOT script method cross-check failed");return false;}
    runtimeAssembly={};protoAssembly={};
    Load(module,"UnrealEngine.Runtime","70421AB0-86DD-4BD4-AFD6-6C909E8B51A7",15630,runtimeAssembly);
    Load(module,"Protobuf.RunTime","3CED0FE6-3455-4ADB-9501-636B0DB4FC87",36051,protoAssembly);
    ready.store(true,std::memory_order_release);
    Log(std::format("Mono AOT BtlSvr.Main {}: {} methods, getter {:X}; B1UI_GSE.Script {} methods",Guid,mainAssembly.count,getter,scriptAssembly.count));return true;
}
std::uintptr_t Method(std::uint32_t rid){return Ready()?Resolve(mainAssembly,rid):0;}
std::uintptr_t ScriptMethod(std::uint32_t rid){return Ready()?Resolve(scriptAssembly,rid):0;}
std::uintptr_t RuntimeMethod(std::uint32_t rid){return Ready()&&runtimeAssembly.table?Resolve(runtimeAssembly,rid):0;}
std::uintptr_t ProtoMethod(std::uint32_t rid){return Ready()&&protoAssembly.table?Resolve(protoAssembly,rid):0;}
bool Ready(){return ready.load(std::memory_order_acquire);}
}
