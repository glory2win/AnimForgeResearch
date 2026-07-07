// AnimForgeWarpVizShared - WarpVizJson.h
// Minimal, dependency-free JSON reader/writer. Maya has no bundled JSON library
// and pulling UE's Json module into the Maya .mll would drag in half of Core,
// so the wire format is implemented once here and compiled into both plugins.
//
// Supports the full JSON grammar we emit: null, bool, number (double), string
// (with \uXXXX escapes), array, object. Object key order is preserved so
// encoded payloads are byte-stable, which keeps golden-file tests meaningful.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace AnimForge
{
namespace WarpViz
{

class JsonValue
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    using Member = std::pair<std::string, JsonValue>;

    JsonValue() : ValueType(Type::Null) {}
    explicit JsonValue(bool B) : ValueType(Type::Bool), BoolValue(B) {}
    explicit JsonValue(double N) : ValueType(Type::Number), NumberValue(N) {}
    explicit JsonValue(int N) : ValueType(Type::Number), NumberValue(static_cast<double>(N)) {}
    explicit JsonValue(const char* S) : ValueType(Type::String), StringValue(S) {}
    explicit JsonValue(const std::string& S) : ValueType(Type::String), StringValue(S) {}

    static JsonValue MakeArray() { JsonValue V; V.ValueType = Type::Array; return V; }
    static JsonValue MakeObject() { JsonValue V; V.ValueType = Type::Object; return V; }

    Type GetType() const { return ValueType; }
    bool IsNull() const { return ValueType == Type::Null; }
    bool IsBool() const { return ValueType == Type::Bool; }
    bool IsNumber() const { return ValueType == Type::Number; }
    bool IsString() const { return ValueType == Type::String; }
    bool IsArray() const { return ValueType == Type::Array; }
    bool IsObject() const { return ValueType == Type::Object; }

    bool AsBool(bool Default = false) const { return IsBool() ? BoolValue : Default; }
    double AsNumber(double Default = 0.0) const { return IsNumber() ? NumberValue : Default; }
    const std::string& AsString() const { return StringValue; }

    // --- array API ------------------------------------------------------
    const std::vector<JsonValue>& Items() const { return ArrayValue; }
    void Add(JsonValue Item) { ArrayValue.push_back(std::move(Item)); }
    size_t Size() const { return IsArray() ? ArrayValue.size() : ObjectValue.size(); }

    // --- object API -----------------------------------------------------
    const std::vector<Member>& Members() const { return ObjectValue; }

    // Sets or replaces a member (last write wins, order of first write kept).
    void Set(const std::string& Key, JsonValue Value);

    // Returns nullptr when the key is missing.
    const JsonValue* Find(const std::string& Key) const;

    // Typed convenience getters used by the protocol layer.
    double GetNumber(const std::string& Key, double Default = 0.0) const;
    std::string GetString(const std::string& Key, const std::string& Default = std::string()) const;
    bool GetBool(const std::string& Key, bool Default = false) const;

    // --- serialization ----------------------------------------------------
    std::string Write() const;

    // Parses Text into OutValue. On failure returns false and fills OutError
    // with a message that includes the byte offset of the problem.
    static bool Parse(const std::string& Text, JsonValue& OutValue, std::string& OutError);

private:
    Type ValueType;
    bool BoolValue = false;
    double NumberValue = 0.0;
    std::string StringValue;
    std::vector<JsonValue> ArrayValue;
    std::vector<Member> ObjectValue;
};

} // namespace WarpViz
} // namespace AnimForge
