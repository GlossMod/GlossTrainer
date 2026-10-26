#include "../src/gameplay/engine_bridge.cpp"
#include <iostream>
#include <map>
#include <type_traits>
#include <unordered_set>

namespace gloss
{
    std::vector<std::string> bridgeLogs;
    void Log(std::string_view value) { bridgeLogs.emplace_back(value); }
}
namespace
{
    std::uintptr_t failedReadAddress{};
}
namespace gloss::gameplay
{
    bool ReadMemory(std::uintptr_t address, void *data, std::size_t size) noexcept
    {
        if (address == failedReadAddress)
            return false;
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), data, size, &copied) && copied == size;
    }
    bool WriteMemory(std::uintptr_t address, const void *data, std::size_t size) noexcept
    {
        SIZE_T copied{};
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), data, size, &copied) && copied == size;
    }
}

namespace
{
    using namespace gloss::gameplay::engine;
    int failures{}, eventCalls{}, destroyedElements{};
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)
    struct MockStruct;
    struct MockField
    {
        std::wstring name;
        int offset{}, size{}, dim{1};
        std::uint64_t cast{}, flags{0x80};
        MockField *next{}, *inner{};
        MockStruct *structure{};
    };
    struct MockStruct
    {
        std::uintptr_t vtable{1};
        int size{};
        MockField *children{};
        MockStruct *parent{};
    };
    struct MockObject
    {
        std::uintptr_t vtable{1};
        MockStruct *klass{};
        std::byte storage[256]{};
    } object;
    MockStruct function;
    std::map<std::wstring, void *> objects;
    std::unordered_set<void *> allocations;
    std::vector<std::wstring> names{L"None"};
    enum class Operation { None, String, Ints, Assets, BadArray, FailAfterString, Name, Transform, InOut } operation;
    bool failStringConstruction{};

    void AssignString(const wchar_t *text, ScriptArray *array)
    {
        const auto length = std::wcslen(text) + 1;
        auto *bytes = new std::byte[length * sizeof(wchar_t)];
        allocations.insert(bytes);
        std::memcpy(bytes, text, length * sizeof(wchar_t));
        *array = {bytes, static_cast<std::int32_t>(length), static_cast<std::int32_t>(length)};
        if (failStringConstruction && std::wcscmp(text, L"payload") == 0)
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    void DestroyArray(ScriptArray *array)
    {
        if (array->data && allocations.erase(array->data))
            delete[] static_cast<std::byte *>(array->data);
        *array = {};
    }
    void InitializeValue(void *, void *address) { *static_cast<ScriptArray *>(address) = {}; }
    void DestroyValue(void *property, void *address)
    {
        auto *field = static_cast<MockField *>(property);
        auto *array = static_cast<ScriptArray *>(address);
        if (field->inner && field->inner->structure)
            destroyedElements += (std::max)(0, array->num);
        DestroyArray(array);
    }
    void NameFromString(NameValue *result, ScriptArray *text, std::int32_t)
    {
        const auto value = text->data ? std::wstring(static_cast<wchar_t *>(text->data)) : L"";
        const auto it = std::find(names.begin(), names.end(), value);
        if (it == names.end())
        {
            names.push_back(value);
            result->comparisonIndex = static_cast<std::int32_t>(names.size() - 1);
        }
        else
            result->comparisonIndex = static_cast<std::int32_t>(it - names.begin());
        result->number = 0;
    }
    void NameToString(NameValue *name, ScriptArray *text)
    {
        AssignString(names.at(name->comparisonIndex).c_str(), text);
    }
    NameValue MakeName(const wchar_t *value)
    {
        ScriptArray text{};
        NameValue result;
        AssignString(value, &text);
        NameFromString(&result, &text, 1);
        DestroyArray(&text);
        return result;
    }
    template <class T> void Output(void *buffer, const T &value, int offset = 0)
    {
        std::memcpy(static_cast<std::byte *>(buffer) + offset, &value, sizeof(value));
    }
    void Event(void *, void *, void *buffer)
    {
        ++eventCalls;
        if (operation == Operation::String || operation == Operation::FailAfterString)
        {
            AssignString(L"变身角色", static_cast<ScriptArray *>(buffer));
            if (operation == Operation::FailAfterString)
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        }
        else if (operation == Operation::Ints || operation == Operation::BadArray)
        {
            auto *data = new std::byte[12];
            allocations.insert(data);
            const int values[]{17, 42, 99};
            std::memcpy(data, values, sizeof(values));
            *static_cast<ScriptArray *>(buffer) = {data, operation == Operation::BadArray ? 131073 : 3, 131073};
        }
        else if (operation == Operation::Assets)
        {
            auto *data = new std::byte[128]{};
            allocations.insert(data);
            Output(data, MakeName(L"/Game/Units/First.First"));
            Output(data, MakeName(L"First"), 8);
            Output(data, MakeName(L"/Game/Units/Last.Last"), 64);
            Output(data, MakeName(L"Last"), 72);
            *static_cast<ScriptArray *>(buffer) = {data, 2, 2};
        }
        else if (operation == Operation::Name)
            Output(buffer, MakeName(L"YangJian"));
        else if (operation == Operation::Transform)
        {
            CHECK(reinterpret_cast<std::uintptr_t>(buffer) % alignof(Transform) == 0);
            Transform value;
            value.translation = {1., 2., 3.};
            Output(buffer, value);
        }
        else if (operation == Operation::InOut)
        {
            int value{};
            std::memcpy(&value, buffer, sizeof(value));
            Output(buffer, value + 1);
        }
    }
    void Reset()
    {
        CHECK(allocations.empty());
        api = {};
        ready.store(true);
        function = {};
        function.size = 16;
        object = {};
        object.vtable = 1;
        objects.clear();
        objects[L"/Mock.Function:Run"] = &function;
        objects[L"/Mock.Function"] = &function;
        eventCalls = destroyedElements = 0;
        gloss::bridgeLogs.clear();
        operation = Operation::None;
        failStringConstruction = false;
        failedReadAddress = 0;
        api.thread = [] { return 1; };
        api.gc = [] { return 0; };
        api.valid = [](void *) { return 1; };
        api.pendingKill = [](void *) { return 0; };
        api.isA = [](void *, void *) { return 1; };
        api.string = AssignString;
        api.destroy = DestroyArray;
        api.find = [](void *, void *, ScriptArray *text, std::int32_t) -> void * {
            const auto it = objects.find(static_cast<wchar_t *>(text->data));
            return it == objects.end() ? nullptr : it->second;
        };
        api.cdo = [](void *, std::int32_t) -> void * { return &object; };
        api.event = Event;
        api.size = [](void *value) { return static_cast<std::uint16_t>(static_cast<MockStruct *>(value)->size); };
        api.children = [](void *value) -> void * { return static_cast<MockStruct *>(value)->children; };
        api.super = [](void *value) -> void * { return static_cast<MockStruct *>(value)->parent; };
        api.structureSize = [](void *value) { return static_cast<MockStruct *>(value)->size; };
        api.structureStorageSize = api.structureSize;
        api.cppStructOps = [](void *) -> void * { return nullptr; };
        api.cppStructSize = api.structureSize;
        api.objectClass = [](void *value) -> void * { return static_cast<MockObject *>(value)->klass; };
        api.next = [](void *value) -> void * { return static_cast<MockField *>(value)->next; };
        api.name = [](void *value, ScriptArray *out) { AssignString(static_cast<MockField *>(value)->name.c_str(), out); };
        api.offset = api.internalOffset = [](void *value) { return static_cast<MockField *>(value)->offset; };
        api.element = [](void *value) { return static_cast<MockField *>(value)->size; };
        api.dim = [](void *value) { return static_cast<MockField *>(value)->dim; };
        api.flags = [](void *value) { return static_cast<MockField *>(value)->flags; };
        api.cast = [](void *value, std::uint64_t flag) { return static_cast<std::int32_t>((static_cast<MockField *>(value)->cast & flag) != 0); };
        api.inner = [](void *value) -> void * { return static_cast<MockField *>(value)->inner; };
        api.structType = [](void *value) -> void * { return static_cast<MockField *>(value)->structure; };
        api.initializeValue = InitializeValue;
        api.destroyValue = DestroyValue;
        api.nameFromString = NameFromString;
        api.nameToString = NameToString;
        api.setBool = [](void *, void *address, std::int32_t value) { *static_cast<std::uint8_t *>(address) = value != 0; };
        api.getBool = [](void *, void *address) { return static_cast<std::int32_t>(*static_cast<std::uint8_t *>(address) != 0); };
    }
    CallResult Invoke(Arg &arg) { return Call(L"/Mock.Function:Run", {&arg, 1}, reinterpret_cast<std::uintptr_t>(&object)); }
    MockField ResultField(Type type, int size = 16)
    {
        return {L"ReturnValue", 0, size, 1, CastFlag(type), 0x480};
    }
    void TestStringsAndFailureCleanup()
    {
        Reset();
        auto field = ResultField(Type::String);
        function.children = &field;
        auto arg = Arg::String("ReturnValue", {}, true);
        operation = Operation::String;
        CHECK(Invoke(arg).success);
        CHECK(arg.text == L"变身角色");
        CHECK(allocations.empty());
        operation = Operation::FailAfterString;
        CHECK(!Invoke(arg).success);
        CHECK(arg.text.empty());
        CHECK(allocations.empty());
        field.name = L"Input";
        field.flags = 0x80;
        arg = Arg::String("Input", L"payload");
        failStringConstruction = true;
        CHECK(!Invoke(arg).success);
        CHECK(allocations.empty());
    }
    void TestCallExecutionState()
    {
        Reset();
        CallResult legacy{true, "two-field aggregate"};
        CHECK(legacy.state == CallState::NotCalled);
        auto field = ResultField(Type::String);
        function.children = &field;
        auto arg = Arg::String("ReturnValue", {}, true);
        operation = Operation::String;
        auto result = Invoke(arg);
        CHECK(result.success && result.state == CallState::Completed);
        CHECK(eventCalls == 1 && allocations.empty());

        arg.name = "WrongParameterName";
        result = Invoke(arg);
        CHECK(!result.success && result.state == CallState::NotCalled);
        CHECK(eventCalls == 1 && allocations.empty());
        arg.name = "ReturnValue";
        operation = Operation::FailAfterString; // Event mutates output, then faults.
        result = Invoke(arg);
        CHECK(!result.success && result.state == CallState::Indeterminate);
        CHECK(eventCalls == 2 && allocations.empty());

        field.name = L"Input";
        field.flags = 0x80;
        arg = Arg::String("Input", L"payload");
        failStringConstruction = true;
        result = Invoke(arg);
        CHECK(!result.success && result.state == CallState::NotCalled);
        CHECK(eventCalls == 2 && allocations.empty());

        failStringConstruction = false;
        auto inner = ResultField(Type::Int, 4);
        field = ResultField(Type::IntArray);
        field.inner = &inner;
        arg = Arg::Integers("ReturnValue");
        operation = Operation::BadArray;
        result = Invoke(arg);
        CHECK(!result.success && result.state == CallState::Completed);
        CHECK(allocations.empty()); // Decode failed after the event returned.
        operation = Operation::Ints;
        api.destroyValue = [](void *property, void *value) {
            DestroyValue(property, value);
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        result = Invoke(arg);
        CHECK(!result.success && result.state == CallState::Completed);
        CHECK(allocations.empty()); // Cleanup failure cannot imply "not called".
    }
    void TestSpawnCollisionEnumSignatureAndDiagnostics()
    {
        Reset();
        // The game's generated UBGUFunctionLibrary binding validates this
        // parameter as FEnumProperty, even though its payload occupies one
        // byte. Exercise this distinction independently of other spawn args.
        constexpr std::wstring_view path = L"/Script/b1.BGUFunctionLibrary:BGUBeginDeferredActorSpawnFromClass";
        objects[std::wstring(path)] = &function;
        MockField collision{L"CollisionHandlingOverride", 0, 1, 1, CastFlag(Type::Enum), 0x80};
        function.children = &collision;
        function.size = 1;
        const auto call = [&](Arg &arg) { return Call(path, {&arg, 1}, reinterpret_cast<std::uintptr_t>(&object)); };
        auto arg = Arg::Enum("CollisionHandlingOverride", 1);
        auto result = call(arg);
        CHECK(result.success && result.state == CallState::Completed && eventCalls == 1);
        CHECK(gloss::bridgeLogs.empty());
        arg = Arg::Byte("CollisionHandlingOverride", 1);
        result = call(arg);
        CHECK(!result.success && result.state == CallState::NotCalled && eventCalls == 1);
        CHECK(gloss::bridgeLogs.size() == 1);
        const auto &message = gloss::bridgeLogs.back();
        CHECK(message.find("function=/Script/b1.BGUFunctionLibrary:BGUBeginDeferredActorSpawnFromClass") != std::string::npos);
        CHECK(message.find("parameter=CollisionHandlingOverride") != std::string::npos);
        CHECK(message.find("expected=Byte expected_size=1 argument_size=1 actual_size=1 offset=0") != std::string::npos);
        CHECK(message.find("cast_match=0 enum_cast=1 byte_cast=0 struct_cast=0") != std::string::npos);
        arg = Arg::Int("CollisionHandlingOverride", 1);
        CHECK(!call(arg).success && eventCalls == 1);
        arg = Arg::Enum("CollisionHandlingOverride", 1);
        collision.cast = CastFlag(Type::Byte);
        CHECK(!call(arg).success && eventCalls == 1); // Enum never accepts a raw byte property either.
        arg = Arg::Byte("CollisionHandlingOverride", 1);
        CHECK(call(arg).success && eventCalls == 2);
        collision.cast = CastFlag(Type::Enum);
        collision.size = function.size = 4;
        arg = Arg::Enum("CollisionHandlingOverride", 1);
        CHECK(!call(arg).success && eventCalls == 2); // A wider enum payload needs its own verified marshaller.
        CHECK(gloss::bridgeLogs.back().find("expected=Enum expected_size=1 argument_size=1 actual_size=4 offset=0") != std::string::npos);
        CHECK(gloss::bridgeLogs.back().find("cast_match=1 enum_cast=1 byte_cast=0 struct_cast=0") != std::string::npos);
        CHECK(allocations.empty());
    }
    void TestLiveMovementFNameCasingAndStrictSignature()
    {
        Reset();
        // REA live evidence ev_25710f59...: same build's actual UFunction
        // exports lowercase "position", despite managed declaration Position.
        MockField x{L"X", 0, 8, 1, CastFlag(Type::Double)};
        MockField y{L"Y", 8, 8, 1, CastFlag(Type::Double)};
        MockField z{L"Z", 16, 8, 1, CastFlag(Type::Double)};
        x.next = &y; y.next = &z;
        MockStruct vector;
        vector.size = 24; vector.children = &x;
        objects[L"/Script/CoreUObject.Vector"] = &vector;
        std::array fields{
            MockField{L"Unit", 0, 8, 1, CastFlag(Type::Object)},
            MockField{L"position", 8, 24, 1, CastFlag(Type::Vector)},
            MockField{L"SpeedRateType", 32, 1, 1, CastFlag(Type::Enum)},
            MockField{L"AcceptableRadius", 36, 4, 1, CastFlag(Type::Float)},
            MockField{L"MoveAIType", 40, 1, 1, CastFlag(Type::Enum)},
            MockField{L"IncludeSelfRadius", 41, 1, 1, CastFlag(Type::Bool)},
            MockField{L"IncludeTargetRadius", 42, 1, 1, CastFlag(Type::Bool)},
            MockField{L"ReturnValue", 44, 4, 1, CastFlag(Type::Int), 0x480}};
        fields[1].structure = &vector;
        for (std::size_t i = 1; i != fields.size(); ++i) fields[i - 1].next = &fields[i];
        function.size = 48; function.children = fields.data();
        constexpr std::wstring_view path = L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocation";
        objects[std::wstring(path)] = &function;
        const auto valid = std::array{
            Arg::Object("Unit", reinterpret_cast<std::uintptr_t>(&object)), Arg::Vector("Position", {101., 202., 303.}),
            Arg::Enum("SpeedRateType", 1), Arg::Float("AcceptableRadius", 5.f), Arg::Enum("MoveAIType", 3),
            Arg::Boolean("IncludeSelfRadius", false), Arg::Boolean("IncludeTargetRadius", true),
            Arg::Data("RETURNVALUE", Type::Int, 0, true)};
        api.event = [](void *, void *, void *buffer) {
            ++eventCalls;
            auto *bytes = static_cast<std::byte *>(buffer);
            std::uintptr_t unit{};
            gloss::gameplay::extensions::Vec3 position{};
            float radius{};
            std::memcpy(&unit, bytes, 8);
            std::memcpy(&position, bytes + 8, 24);
            std::memcpy(&radius, bytes + 36, 4);
            CHECK(unit == reinterpret_cast<std::uintptr_t>(&object));
            CHECK(position.x == 101. && position.y == 202. && position.z == 303.);
            CHECK(radius == 5.f && bytes[32] == std::byte{1} && bytes[40] == std::byte{3});
            CHECK(bytes[41] == std::byte{0} && bytes[42] == std::byte{1});
            Output(buffer, 71, 44);
        };
        auto args = valid;
        const auto call = [&](auto &values) { return Call(path, values, reinterpret_cast<std::uintptr_t>(&object)); };
        CHECK(call(args).success && eventCalls == 1 && args.back().Get<int>() == 71);
        CHECK(std::string_view(args[1].name) == "Position" && fields[1].name == L"position");
        for (int mutation = 0; mutation != 6; ++mutation)
        {
            args = valid;
            if (mutation == 0) args[1].name = "Postion";
            if (mutation == 1) args[3].name = "POSITION"; // Duplicate supplied FName.
            if (mutation == 2) args[2] = Arg::Byte("speedratetype", 1);
            if (mutation == 3) args[1].bytes.resize(12);
            if (mutation == 4) args.back().output = false;
            if (mutation == 5) args[1].name = nullptr;
            const auto result = call(args);
            CHECK(!result.success && result.state == CallState::NotCalled && eventCalls == 1);
            if (mutation == 0 || mutation == 1 || mutation == 5)
            {
                CHECK(gloss::bridgeLogs.back().find("function=/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocation") != std::string::npos);
                CHECK(gloss::bridgeLogs.back().find(mutation == 0 ? "requested=Postion" : mutation == 1 ? "requested=POSITION" : "requested=<null>") != std::string::npos);
            }
        }
        args = valid;
        fields.back().name = L"POSITION"; // Duplicate reflected FName, not a new field.
        CHECK(!call(args).success && eventCalls == 1);
        CHECK(gloss::bridgeLogs.back().find("duplicate reflected FName") != std::string::npos);
        CHECK(allocations.empty());
    }
    void TestIndexFNameCasingForInputAndOutput()
    {
        Reset();
        // REA ev_ae778173...: BGUGetWeaponByIndex declares managed "index",
        // but its actual reflected display name is "Index" in this process.
        MockField owner{L"Owner", 0, 8, 1, CastFlag(Type::Object)};
        MockField index{L"Index", 8, 4, 1, CastFlag(Type::Int)};
        MockField result{L"ReturnValue", 16, 8, 1, CastFlag(Type::Object), 0x480};
        owner.next = &index;
        index.next = &result;
        function.size = 24; function.children = &owner;
        constexpr std::wstring_view path = L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetWeaponByIndex";
        objects[std::wstring(path)] = &function;
        auto args = std::array{Arg::Object("Owner", reinterpret_cast<std::uintptr_t>(&object)), Arg::Int("index", 41),
            Arg::Data("returnvalue", Type::Object, std::uintptr_t{}, true)};
        api.event = [](void *, void *, void *buffer) {
            ++eventCalls;
            std::uintptr_t owner{};
            int input{};
            std::memcpy(&owner, buffer, 8);
            std::memcpy(&input, static_cast<std::byte *>(buffer) + 8, 4);
            CHECK(owner == reinterpret_cast<std::uintptr_t>(&object) && input == 41);
            Output(buffer, reinterpret_cast<std::uintptr_t>(&object), 16);
        };
        CHECK(Call(path, args, reinterpret_cast<std::uintptr_t>(&object)).success);
        CHECK(eventCalls == 1 && args[2].Get<std::uintptr_t>() == reinterpret_cast<std::uintptr_t>(&object));
        CHECK(index.name == L"Index" && std::string_view(args[1].name) == "index");
        args[1].name = "UnknownIndex";
        CHECK(!Call(path, args, reinterpret_cast<std::uintptr_t>(&object)).success && eventCalls == 1);
        CHECK(allocations.empty());
    }
    void TestTypedArraysAndReflectedDestruction()
    {
        Reset();
        auto inner = ResultField(Type::Int, 4);
        auto field = ResultField(Type::IntArray);
        field.inner = &inner;
        function.children = &field;
        auto arg = Arg::Integers("ReturnValue");
        operation = Operation::Ints;
        CHECK(Invoke(arg).success);
        CHECK(arg.bytes.size() == 12);
        int last{};
        std::memcpy(&last, arg.bytes.data() + 8, 4);
        CHECK(last == 99);
        CHECK(allocations.empty());
        operation = Operation::BadArray;
        CHECK(!Invoke(arg).success);
        CHECK(arg.bytes.empty());
        CHECK(allocations.empty());
        inner.cast = CastFlag(Type::Float);
        CHECK(!Invoke(arg).success);
        CHECK(eventCalls == 2);

        MockField path{L"ObjectPath", 0, 8, 1, CastFlag(Type::Name)};
        MockField name{L"AssetName", 8, 8, 1, CastFlag(Type::Name)};
        path.next = &name;
        MockStruct asset;
        asset.size = 64;
        asset.children = &path;
        objects[L"/Script/CoreUObject.AssetData"] = &asset;
        inner.cast = 0x100000;
        inner.size = 64;
        inner.structure = &asset;
        arg = Arg::Structs("returnvalue", L"/Script/CoreUObject.AssetData", {Arg::Name("objectpath"), Arg::Name("ASSETNAME")});
        operation = Operation::Assets;
        CHECK(Invoke(arg).success);
        CHECK(arg.records.size() == 2);
        CHECK(arg.records[0][0].text == L"/Game/Units/First.First");
        CHECK(arg.records[1][1].text == L"Last");
        CHECK(destroyedElements == 2);
        CHECK(allocations.empty());
        arg.fields[1].name = "OBJECTPATH";
        CHECK(!Invoke(arg).success && eventCalls == 3); // Case-only duplicate selection.
        arg.fields[1].name = "ASSETNAME";
        api.destroyValue = nullptr;
        CHECK(!Invoke(arg).success);
        CHECK(eventCalls == 3);
    }
    void TestNamesAndDirections()
    {
        Reset();
        auto field = ResultField(Type::Name, 8);
        function.children = &field;
        auto arg = Arg::Name("ReturnValue", {}, true);
        operation = Operation::Name;
        CHECK(Invoke(arg).success);
        CHECK(arg.text == L"YangJian");
        CHECK(allocations.empty());
        field.flags = 0x80;
        CHECK(!Invoke(arg).success);
        field.name = L"Input";
        field.cast = CastFlag(Type::Int);
        field.size = 4;
        field.flags = 0x80 | 0x100 | 0x2 | 0x8000000;
        arg = Arg::Int("Input", 41);
        operation = Operation::None;
        CHECK(Invoke(arg).success); // Const reference is input.
        field.flags &= ~0x2ull;
        CHECK(!Invoke(arg).success);
        arg.output = true;
        operation = Operation::InOut;
        CHECK(Invoke(arg).success);
        CHECK(arg.Get<int>() == 42);
        field.size = 8;
        CHECK(!Invoke(arg).success);
        CHECK(allocations.empty());
    }
    void TestNestedStructProperties()
    {
        Reset();
        MockField id{L"ResID", 4, 4, 1, CastFlag(Type::Int)};
        MockStruct config;
        config.size = 16;
        config.children = &id;
        MockField property{L"Config", 16, 16, 1, 0x100000};
        property.structure = &config;
        MockStruct cls;
        cls.size = sizeof(object);
        cls.children = &property;
        object.klass = &cls;
        const std::string_view path[]{"config"};
        auto arg = Arg::Int("resid", 123);
        CHECK(PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg, true));
        arg = Arg::Int("RESID", 0);
        CHECK(PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg));
        CHECK(arg.Get<int>() == 123);
        MockField duplicate = id;
        duplicate.name = L"rEsId";
        id.next = &duplicate;
        CHECK(!PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg));
        id.next = nullptr;
        id.offset = 14;
        CHECK(!PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg, true));
        id.offset = 4;
        property.size = 8;
        CHECK(!PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg));
        CHECK(allocations.empty());
    }
    void TestKnownStructIdentityAndLayouts()
    {
        Reset();
        MockField x{L"X", 0, 8, 1, CastFlag(Type::Double)};
        MockField y{L"Y", 8, 8, 1, CastFlag(Type::Double)};
        MockField z{L"Z", 16, 8, 1, CastFlag(Type::Double)};
        MockField w{L"W", 24, 8, 1, CastFlag(Type::Double)};
        x.next = &y; y.next = &z;
        MockStruct vector;
        vector.size = 24; vector.children = &x;
        MockStruct quat;
        quat.size = 32; quat.children = &x;
        objects[L"/Script/CoreUObject.Vector"] = &vector;
        objects[L"/Script/CoreUObject.Quat"] = &quat;
        MockField rotation{L"Rotation", 0, 32, 1, 0x100000}; rotation.structure = &quat;
        MockField translation{L"Translation", 32, 24, 1, 0x100000}; translation.structure = &vector;
        MockField scale{L"Scale3D", 64, 24, 1, 0x100000}; scale.structure = &vector;
        rotation.next = &translation; translation.next = &scale;
        MockStruct transform;
        transform.size = 96; transform.children = &rotation;
        objects[L"/Script/CoreUObject.Transform"] = &transform;
        auto field = ResultField(Type::Transform, 96);
        field.structure = &transform;
        function.size = 96; function.children = &field;
        // Quat shares XYZ metadata with Vector; include W only for its validation.
        z.next = &w;
        auto arg = Arg::TransformValue("ReturnValue", {}, true);
        operation = Operation::Transform;
        CHECK(Invoke(arg).success);
        CHECK(arg.Get<Transform>().translation.z == 3.);
        MockStruct impostor = transform;
        field.structure = &impostor;
        CHECK(!Invoke(arg).success); // Same size is not enough.
        field.structure = &transform;
        translation.offset = 40;
        CHECK(!Invoke(arg).success);
        translation.offset = 32;
        transform.size = 48;
        CHECK(!Invoke(arg).success);
        CHECK(eventCalls == 1);
        CHECK(allocations.empty());
    }
    void TestSoftClassExportAndByteProperties()
    {
        Reset();
        MockField soft{L"MonsterClass", 16, 40, 1, 0x200000000ull};
        MockStruct cls;
        cls.size = sizeof(object); cls.children = &soft;
        object.klass = &cls;
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"BlueprintGeneratedClass'/Game/Units/Unit.Unit_C'", out);
        };
        std::wstring path;
        CHECK(SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path));
        CHECK(path == L"/Game/Units/Unit.Unit_C");
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"BlueprintGeneratedClass'/Game/Units/Unit.Unit_C", out);
        };
        CHECK(!SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path));
        CHECK(path.empty());
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"None", out);
            failedReadAddress = reinterpret_cast<std::uintptr_t>(out->data);
        };
        CHECK(!SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path) && path.empty());
        CHECK(allocations.empty());
        failedReadAddress = 0;
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"partial export", out);
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        CHECK(!SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path) && path.empty());
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"XClass'/Game/Units/Unit.Unit_C'", out);
            static_cast<wchar_t *>(out->data)[0] = 0;
        };
        CHECK(!SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path) && path.empty());
        CHECK(allocations.empty());
        soft.cast = CastFlag(Type::Object);
        CHECK(!SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path));
        soft.cast = CastFlag(Type::Byte);
        soft.name = L"Mode"; soft.size = 1;
        auto arg = Arg::Byte("Mode", 5);
        CHECK(Property(reinterpret_cast<std::uintptr_t>(&object), arg, true));
        arg = Arg::Byte("Mode", 0);
        CHECK(Property(reinterpret_cast<std::uintptr_t>(&object), arg));
        CHECK(arg.Get<std::uint8_t>() == 5);
        arg = Arg::Enum("Mode", 0);
        CHECK(!Property(reinterpret_cast<std::uintptr_t>(&object), arg));
    }
    int nativeTextItemCalls{}, nativeDirectSkips{};
    void NativeTextItem(void *property, ScriptArray *out, void *value, void *defaults, void *owner, std::int32_t flags, void *root)
    {
        ++nativeTextItemCalls;
        CHECK(property && value && defaults == nullptr && owner == &object && flags == 0 && root == nullptr);
        const auto *field = static_cast<MockField *>(property);
        AssignString(field->cast == 0x200000ull ? L"()" : L"None", out);
    }
    std::int32_t NativeTextDirect(void *property, ScriptArray *out, void *value, void *defaults, void *owner, std::int32_t flags, void *root)
    {
        // Reproduce this build's differential native contract: null soft
        // references/empty containers compare identical to a null default,
        // so Direct skips the virtual text exporter and returns false.
        if (!defaults)
        {
            ++nativeDirectSkips;
            return 0;
        }
        NativeTextItem(property, out, value, nullptr, owner, flags, root);
        return 1;
    }
    void TestUnconditionalNativeTextItemAbiAndEmptyValues()
    {
        Reset();
        using TextItemAbi = void (*)(void *, ScriptArray *, void *, void *, void *, std::int32_t, void *);
        static_assert(std::is_same_v<decltype(api.exportTextItem), TextItemAbi>);
        nativeTextItemCalls = nativeDirectSkips = 0;
        MockField soft{L"BeginTriggerBox", 16, 40, 1, 0x20000000ull};
        MockField softClass{L"MonsterClass", 56, 40, 1, 0x200000000ull};
        MockField emptyArray{L"OtherFsmTemplates", 96, sizeof(ScriptArray), 1, 0x200000ull};
        MockField leaf{L"TargetTamerList", 8, sizeof(ScriptArray), 1, 0x200000ull};
        MockStruct nested;
        nested.size = 32; nested.children = &leaf;
        MockField wake{L"WakeUpConfig", 112, 32, 1, 0x100000ull};
        wake.structure = &nested;
        soft.next = &softClass; softClass.next = &emptyArray; emptyArray.next = &wake;
        MockStruct cls;
        cls.size = sizeof(object); cls.children = &soft;
        object.klass = &cls;
        ScriptArray skipped{};
        CHECK(NativeTextDirect(&soft, &skipped, object.storage, nullptr, &object, 0, nullptr) == 0);
        CHECK(skipped.data == nullptr && skipped.num == 0 && nativeTextItemCalls == 0 && nativeDirectSkips == 1);
        api.exportTextItem = NativeTextItem;
        const auto address = reinterpret_cast<std::uintptr_t>(&object);
        bool assigned = true;
        CHECK(ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && !assigned);
        std::wstring path = L"previous", text = L"previous";
        CHECK(SoftClassPath(address, "MonsterClass", path) && path.empty());
        CHECK(ExportPropertyText(address, "OtherFsmTemplates", text) && text == L"()");
        constexpr std::array<std::string_view, 1> wakePath{"WakeUpConfig"};
        CHECK(ExportPropertyPathText(address, wakePath, "TargetTamerList", text) && text == L"()");
        CHECK(nativeTextItemCalls == 4 && nativeDirectSkips == 1);
        CHECK(allocations.empty());
    }
    void TestReflectedStructCopy()
    {
        Reset();
        MockStruct config;
        config.size = sizeof(ScriptArray);
        MockField field{L"Config", 16, sizeof(ScriptArray), 1, CastFlag(Type::Vector)};
        field.structure = &config;
        MockStruct cls;
        cls.size = sizeof(object); cls.children = &field;
        object.klass = &cls;
        MockObject destination;
        destination.klass = &cls;
        auto *from = reinterpret_cast<ScriptArray *>(object.storage);
        auto *to = reinterpret_cast<ScriptArray *>(destination.storage);
        AssignString(L"owned source", from);
        AssignString(L"previous target", to);
        api.copyValue = [](void *, void *destination, void *source) {
            auto *target = static_cast<ScriptArray *>(destination);
            DestroyArray(target);
            AssignString(static_cast<wchar_t *>(static_cast<ScriptArray *>(source)->data), target);
        };
        const auto sourceAddress = reinterpret_cast<std::uintptr_t>(&object);
        const auto destinationAddress = reinterpret_cast<std::uintptr_t>(&destination);
        CHECK(CopyStructProperty(sourceAddress, destinationAddress, "Config"));
        CHECK(from->data != to->data && std::wcscmp(static_cast<wchar_t *>(to->data), L"owned source") == 0);
        MockField impostor = field;
        MockStruct otherClass = cls;
        otherClass.children = &impostor;
        destination.klass = &otherClass;
        CHECK(!CopyStructProperty(sourceAddress, destinationAddress, "Config"));
        destination.klass = &cls;
        config.size = 8;
        CHECK(!CopyStructProperty(sourceAddress, destinationAddress, "Config"));
        config.size = sizeof(ScriptArray);
        field.offset = sizeof(object) - 1;
        CHECK(!CopyStructProperty(sourceAddress, destinationAddress, "Config"));
        DestroyArray(from);
        DestroyArray(to);
        CHECK(allocations.empty());
    }
    void TestReadOnlyPropertyTextExport()
    {
        Reset();
        MockField field{L"OtherFsmTemplatesPath", 16, sizeof(ScriptArray), 1, 0x200000};
        MockStruct parent;
        parent.size = sizeof(object); parent.children = &field;
        MockStruct cls;
        cls.size = sizeof(object); cls.parent = &parent;
        object.klass = &cls;
        api.exportTextItem = [](void *property, ScriptArray *out, void *value, void *defaults, void *owner, std::int32_t flags, void *root) {
            CHECK(static_cast<MockField *>(property)->name == L"OtherFsmTemplatesPath");
            CHECK(value == object.storage && defaults == nullptr && owner == &object && flags == 0 && root == nullptr);
            AssignString(L"(\"/Game/State/Fsm.Fsm\",\"/Game/State/Other.Other\")", out);
        };
        const auto address = reinterpret_cast<std::uintptr_t>(&object);
        const auto before = object;
        std::wstring text = L"previous";
        CHECK(ExportPropertyText(address, "OtherFsmTemplatesPath", text));
        CHECK(text == L"(\"/Game/State/Fsm.Fsm\",\"/Game/State/Other.Other\")");
        CHECK(std::memcmp(&before, &object, sizeof(object)) == 0);
        CHECK(allocations.empty());
        CHECK(!ExportPropertyText(address, "MissingProperty", text) && text.empty());
        CHECK(!ExportPropertyText(address, nullptr, text) && text.empty());
        field.offset = sizeof(object) - 1;
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        field.offset = 16;
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"None", out);
            out->num = -1; // Malformed output must fail validation, even without a status return.
        };
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"partial export", out);
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"aXb", out);
            static_cast<wchar_t *>(out->data)[1] = 0;
        };
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"()", out);
            ++out->num; // Exported string exceeds its declared allocation.
        };
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"()", out);
        };
        CHECK(ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text == L"()");
        api.thread = [] { return 0; };
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        api.thread = [] { return 1; };
        api.exportTextItem = nullptr;
        CHECK(!ExportPropertyText(address, "OtherFsmTemplatesPath", text) && text.empty());
        CHECK(allocations.empty());
    }
    void TestObjectReferenceAssignmentTypes()
    {
        Reset();
        MockField field{L"Reference", 16, sizeof(std::uintptr_t), 1, CastFlag(Type::Object)};
        MockStruct cls;
        cls.size = sizeof(object); cls.children = &field;
        object.klass = &cls;
        const auto address = reinterpret_cast<std::uintptr_t>(&object);
        bool assigned = true;
        CHECK(ObjectReferenceAssigned(address, "Reference", assigned) && !assigned);
        Output(object.storage, std::uintptr_t{0x1234});
        CHECK(ObjectReferenceAssigned(address, "Reference", assigned) && assigned);
        field.size = 4;
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned) && !assigned);
        field.size = sizeof(WeakObject);
        field.cast = 0x8000000ull;
        api.weakGet = [](WeakObject *) -> void * { CHECK(false); return nullptr; };
        api.weakValid = [](WeakObject *, std::int32_t, std::int32_t) { CHECK(false); return 0; };
        for (const WeakObject token : {WeakObject{-1, 0}, WeakObject{0, 0}})
        {
            Output(object.storage, token);
            CHECK(ObjectReferenceAssigned(address, "Reference", assigned) && !assigned);
        }
        // No native resolution is needed: even a stale token is assigned.
        for (const WeakObject token : {WeakObject{2, 17}, WeakObject{2, 18}, WeakObject{0, 7}})
        {
            Output(object.storage, token);
            CHECK(ObjectReferenceAssigned(address, "Reference", assigned) && assigned);
        }
        for (const WeakObject token : {WeakObject{-1, 17}, WeakObject{-2, 0}, WeakObject{2, 0}, WeakObject{2, -1}})
        {
            Output(object.storage, token);
            CHECK(!ObjectReferenceAssigned(address, "Reference", assigned) && !assigned);
        }
        Output(object.storage, WeakObject{-1, 0});
        auto hard = Arg::Object("Reference", 0);
        CHECK(!Property(address, hard)); // Weak storage must not become a hard pointer.
        field.size = 4;
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        field.size = sizeof(WeakObject);
        field.dim = 2;
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        field.dim = 1;
        field.offset = sizeof(object) - 1;
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        field.offset = 16;
        failedReadAddress = address + field.offset;
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        field.cast = CastFlag(Type::Object);
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        failedReadAddress = 0;
        field.cast = CastFlag(Type::Int);
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        field.cast = CastFlag(Type::Object) | 0x8000000ull;
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        CHECK(!ObjectReferenceAssigned(address, "Missing", assigned));
        CHECK(!ObjectReferenceAssigned(address, nullptr, assigned));
        api.thread = [] { return 0; };
        CHECK(!ObjectReferenceAssigned(address, "Reference", assigned));
        CHECK(allocations.empty());
    }
    std::wstring softReferenceExport;
    void TestSoftObjectReferenceAssignmentAndCleanup()
    {
        Reset();
        MockField field{L"BeginTriggerBox", 16, 40, 1, 0x20000000ull};
        MockStruct cls;
        cls.size = sizeof(object); cls.children = &field;
        object.klass = &cls;
        const auto address = reinterpret_cast<std::uintptr_t>(&object);
        const auto before = object;
        api.exportTextItem = [](void *property, ScriptArray *out, void *value, void *defaults, void *owner, std::int32_t flags, void *root) {
            CHECK(property == object.klass->children && value == object.storage && defaults == nullptr && owner == &object && flags == 0 && root == nullptr);
            AssignString(softReferenceExport.c_str(), out);
        };
        bool assigned{};
        for (const auto text : {L"", L"None"})
        {
            softReferenceExport = text;
            assigned = true;
            CHECK(ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && !assigned);
            CHECK(allocations.empty());
        }
        // These paths need not be loaded. Level actor suffixes are preserved.
        for (const auto text : {L"/Game/Maps/Test.Test:PersistentLevel.TriggerBox_1", L"TriggerBox'/Game/Maps/Test.Test:PersistentLevel.TriggerBox_1'"})
        {
            softReferenceExport = text;
            CHECK(ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && assigned);
            CHECK(allocations.empty());
        }
        for (const auto text : {L"unknown", L"()", L"\"None\"", L"/Game/NoObjectSeparator", L"TriggerBox'/Game/Maps/Test.Test", L"TriggerBox''", L"/Game/Bad Path.Bad"})
        {
            softReferenceExport = text;
            CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && !assigned);
            CHECK(allocations.empty());
        }
        softReferenceExport = L"/Game/Maps/Test.Test";
        auto hard = Arg::Object("BeginTriggerBox", 0);
        CHECK(!Property(address, hard));
        field.dim = 2;
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned));
        field.dim = 1;
        field.size = 0;
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned));
        field.size = sizeof(object);
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned));
        field.size = 40;
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"None", out);
            out->num = -1; // Malformed output must fail validation, even without a status return.
        };
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && !assigned);
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"None", out);
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && !assigned);
        CHECK(allocations.empty());
        api.exportTextItem = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"/Game/Maps/Test.Test", out);
            static_cast<wchar_t *>(out->data)[2] = 0;
        };
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned) && !assigned);
        CHECK(allocations.empty());
        api.exportTextItem = nullptr;
        CHECK(!ObjectReferenceAssigned(address, "BeginTriggerBox", assigned));
        CHECK(std::memcmp(&before, &object, sizeof(object)) == 0);
        CHECK(allocations.empty());
    }
    void TestWeakLifetimeCheckDoesNotResolvePendingObjects()
    {
        Reset();
        api.weakSet = [](WeakObject *, void *) {};
        api.weakGet = [](WeakObject *) -> void * { CHECK(false); return nullptr; };
        api.weakValid = [](WeakObject *weak, std::int32_t pending, std::int32_t threadSafe) {
            CHECK(pending == 1 && threadSafe == 1);
            return weak->index == 2 && weak->serial == 17 ? 1 : 0;
        };
        CHECK(WeakLifetimeCurrent({2, 17}));
        CHECK(!WeakLifetimeCurrent({2, 18}));
        CHECK(!WeakLifetimeCurrent({-1, 17}));
        CHECK(!WeakLifetimeCurrent({2, 0}));
        api.thread = [] { return 0; };
        CHECK(!WeakLifetimeCurrent({2, 17}));
    }
    void TestNestedPropertyTextExport()
    {
        Reset();
        MockField leaf{L"TargetTamerList", 8, sizeof(ScriptArray), 1, 0x200000};
        MockStruct nested;
        nested.size = 32; nested.children = &leaf;
        MockField field{L"WakeUpConfig", 16, 32, 1, 0x100000};
        field.structure = &nested;
        MockStruct cls;
        cls.size = sizeof(object); cls.children = &field;
        object.klass = &cls;
        api.exportTextItem = [](void *property, ScriptArray *out, void *value, void *, void *owner, std::int32_t, void *) {
            CHECK(static_cast<MockField *>(property)->name == L"TargetTamerList");
            CHECK(value == object.storage + 8 && owner == &object);
            AssignString(L"()", out);
        };
        const auto address = reinterpret_cast<std::uintptr_t>(&object);
        const std::string_view path[]{"WakeUpConfig"};
        std::wstring text;
        CHECK(ExportPropertyPathText(address, path, "TargetTamerList", text) && text == L"()");
        CHECK(allocations.empty());
        const std::string_view missing[]{"MissingConfig"};
        CHECK(!ExportPropertyPathText(address, missing, "TargetTamerList", text) && text.empty());
        CHECK(!ExportPropertyPathText(address, path, "MissingLeaf", text) && text.empty());
        field.cast = CastFlag(Type::Object);
        CHECK(!ExportPropertyPathText(address, path, "TargetTamerList", text) && text.empty());
        field.cast = 0x100000;
        field.size = 24;
        CHECK(!ExportPropertyPathText(address, path, "TargetTamerList", text) && text.empty());
        field.size = 32;
        leaf.offset = 24;
        CHECK(!ExportPropertyPathText(address, path, "TargetTamerList", text) && text.empty());
        leaf.offset = 8;
        field.structure = nullptr;
        CHECK(!ExportPropertyPathText(address, path, "TargetTamerList", text) && text.empty());
        field.structure = &nested;
        std::array<std::string_view, 17> excessive;
        excessive.fill("WakeUpConfig");
        CHECK(!ExportPropertyPathText(address, excessive, "TargetTamerList", text) && text.empty());
        CHECK(allocations.empty());
    }
}

int main()
{
    TestStringsAndFailureCleanup();
    TestCallExecutionState();
    TestSpawnCollisionEnumSignatureAndDiagnostics();
    TestLiveMovementFNameCasingAndStrictSignature();
    TestIndexFNameCasingForInputAndOutput();
    TestTypedArraysAndReflectedDestruction();
    TestNamesAndDirections();
    TestNestedStructProperties();
    TestKnownStructIdentityAndLayouts();
    TestSoftClassExportAndByteProperties();
    TestUnconditionalNativeTextItemAbiAndEmptyValues();
    TestReflectedStructCopy();
    TestReadOnlyPropertyTextExport();
    TestObjectReferenceAssignmentTypes();
    TestSoftObjectReferenceAssignmentAndCleanup();
    TestWeakLifetimeCheckDoesNotResolvePendingObjects();
    TestNestedPropertyTextExport();
    CHECK(allocations.empty());
    std::cout << "Engine bridge tests: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
