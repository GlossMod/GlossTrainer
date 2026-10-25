#include "../src/gameplay/engine_bridge.cpp"
#include <iostream>
#include <map>
#include <unordered_set>

namespace gloss
{
    void Log(std::string_view) {}
}
namespace gloss::gameplay
{
    bool ReadMemory(std::uintptr_t address, void *data, std::size_t size) noexcept
    {
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
        operation = Operation::None;
        failStringConstruction = false;
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
        arg = Arg::Structs("ReturnValue", L"/Script/CoreUObject.AssetData", {Arg::Name("ObjectPath"), Arg::Name("AssetName")});
        operation = Operation::Assets;
        CHECK(Invoke(arg).success);
        CHECK(arg.records.size() == 2);
        CHECK(arg.records[0][0].text == L"/Game/Units/First.First");
        CHECK(arg.records[1][1].text == L"Last");
        CHECK(destroyedElements == 2);
        CHECK(allocations.empty());
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
        const std::string_view path[]{"Config"};
        auto arg = Arg::Int("ResID", 123);
        CHECK(PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg, true));
        arg = Arg::Int("ResID", 0);
        CHECK(PropertyPath(reinterpret_cast<std::uintptr_t>(&object), path, arg));
        CHECK(arg.Get<int>() == 123);
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
        api.exportText = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"BlueprintGeneratedClass'/Game/Units/Unit.Unit_C'", out);
            return 1;
        };
        std::wstring path;
        CHECK(SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path));
        CHECK(path == L"/Game/Units/Unit.Unit_C");
        CHECK(allocations.empty());
        api.exportText = [](void *, ScriptArray *out, void *, void *, void *, std::int32_t, void *) {
            AssignString(L"BlueprintGeneratedClass'/Game/Units/Unit.Unit_C", out);
            return 1;
        };
        CHECK(!SoftClassPath(reinterpret_cast<std::uintptr_t>(&object), "MonsterClass", path));
        CHECK(path.empty());
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
}

int main()
{
    TestStringsAndFailureCleanup();
    TestCallExecutionState();
    TestTypedArraysAndReflectedDestruction();
    TestNamesAndDirections();
    TestNestedStructProperties();
    TestKnownStructIdentityAndLayouts();
    TestSoftClassExportAndByteProperties();
    TestReflectedStructCopy();
    TestWeakLifetimeCheckDoesNotResolvePendingObjects();
    CHECK(allocations.empty());
    std::cout << "Engine bridge tests: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
