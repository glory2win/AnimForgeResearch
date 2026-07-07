// AnimForgeWarpVizShared - WarpVizJson.cpp

#include "WarpVizJson.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace AnimForge
{
namespace WarpViz
{

// ---------------------------------------------------------------------------
// JsonValue object helpers
// ---------------------------------------------------------------------------

void JsonValue::Set(const std::string& Key, JsonValue Value)
{
    ValueType = Type::Object;
    for (Member& Existing : ObjectValue)
    {
        if (Existing.first == Key)
        {
            Existing.second = std::move(Value);
            return;
        }
    }
    ObjectValue.emplace_back(Key, std::move(Value));
}

const JsonValue* JsonValue::Find(const std::string& Key) const
{
    for (const Member& Entry : ObjectValue)
    {
        if (Entry.first == Key)
        {
            return &Entry.second;
        }
    }
    return nullptr;
}

double JsonValue::GetNumber(const std::string& Key, double Default) const
{
    const JsonValue* Found = Find(Key);
    return (Found && Found->IsNumber()) ? Found->AsNumber() : Default;
}

std::string JsonValue::GetString(const std::string& Key, const std::string& Default) const
{
    const JsonValue* Found = Find(Key);
    return (Found && Found->IsString()) ? Found->AsString() : Default;
}

bool JsonValue::GetBool(const std::string& Key, bool Default) const
{
    const JsonValue* Found = Find(Key);
    return (Found && Found->IsBool()) ? Found->AsBool() : Default;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

namespace
{

void WriteEscapedString(const std::string& In, std::string& Out)
{
    Out += '"';
    for (const char C : In)
    {
        switch (C)
        {
        case '"':  Out += "\\\""; break;
        case '\\': Out += "\\\\"; break;
        case '\b': Out += "\\b"; break;
        case '\f': Out += "\\f"; break;
        case '\n': Out += "\\n"; break;
        case '\r': Out += "\\r"; break;
        case '\t': Out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(C) < 0x20)
            {
                char Buffer[8];
                std::snprintf(Buffer, sizeof(Buffer), "\\u%04x", C);
                Out += Buffer;
            }
            else
            {
                Out += C; // UTF-8 bytes pass through untouched
            }
            break;
        }
    }
    Out += '"';
}

void WriteNumber(double Value, std::string& Out)
{
    // Integers are written without a fractional part so frame indices and
    // ports stay readable; %.17g round-trips any double exactly.
    const long long AsInt = static_cast<long long>(Value);
    char Buffer[32];
    if (static_cast<double>(AsInt) == Value && std::fabs(Value) < 9.0e15)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%lld", AsInt);
    }
    else
    {
        std::snprintf(Buffer, sizeof(Buffer), "%.17g", Value);
    }
    Out += Buffer;
}

void WriteValue(const JsonValue& Value, std::string& Out)
{
    switch (Value.GetType())
    {
    case JsonValue::Type::Null:
        Out += "null";
        break;
    case JsonValue::Type::Bool:
        Out += Value.AsBool() ? "true" : "false";
        break;
    case JsonValue::Type::Number:
        WriteNumber(Value.AsNumber(), Out);
        break;
    case JsonValue::Type::String:
        WriteEscapedString(Value.AsString(), Out);
        break;
    case JsonValue::Type::Array:
    {
        Out += '[';
        bool bFirst = true;
        for (const JsonValue& Item : Value.Items())
        {
            if (!bFirst) Out += ',';
            bFirst = false;
            WriteValue(Item, Out);
        }
        Out += ']';
        break;
    }
    case JsonValue::Type::Object:
    {
        Out += '{';
        bool bFirst = true;
        for (const JsonValue::Member& Entry : Value.Members())
        {
            if (!bFirst) Out += ',';
            bFirst = false;
            WriteEscapedString(Entry.first, Out);
            Out += ':';
            WriteValue(Entry.second, Out);
        }
        Out += '}';
        break;
    }
    }
}

} // anonymous namespace

std::string JsonValue::Write() const
{
    std::string Out;
    Out.reserve(256);
    WriteValue(*this, Out);
    return Out;
}

// ---------------------------------------------------------------------------
// Parser (recursive descent)
// ---------------------------------------------------------------------------

namespace
{

class Parser
{
public:
    Parser(const std::string& InText) : Text(InText), Pos(0) {}

    bool ParseDocument(JsonValue& OutValue, std::string& OutError)
    {
        SkipWhitespace();
        if (!ParseValue(OutValue, OutError))
        {
            return false;
        }
        SkipWhitespace();
        if (Pos != Text.size())
        {
            OutError = "Trailing characters at offset " + std::to_string(Pos);
            return false;
        }
        return true;
    }

private:
    const std::string& Text;
    size_t Pos;

    void SkipWhitespace()
    {
        while (Pos < Text.size())
        {
            const char C = Text[Pos];
            if (C == ' ' || C == '\t' || C == '\n' || C == '\r')
            {
                ++Pos;
            }
            else
            {
                break;
            }
        }
    }

    bool Fail(std::string& OutError, const std::string& Message)
    {
        OutError = Message + " at offset " + std::to_string(Pos);
        return false;
    }

    bool ParseValue(JsonValue& Out, std::string& OutError)
    {
        if (Pos >= Text.size())
        {
            return Fail(OutError, "Unexpected end of input");
        }

        const char C = Text[Pos];
        switch (C)
        {
        case '{': return ParseObject(Out, OutError);
        case '[': return ParseArray(Out, OutError);
        case '"':
        {
            std::string S;
            if (!ParseString(S, OutError)) return false;
            Out = JsonValue(S);
            return true;
        }
        case 't':
            if (Text.compare(Pos, 4, "true") == 0) { Pos += 4; Out = JsonValue(true); return true; }
            return Fail(OutError, "Invalid literal");
        case 'f':
            if (Text.compare(Pos, 5, "false") == 0) { Pos += 5; Out = JsonValue(false); return true; }
            return Fail(OutError, "Invalid literal");
        case 'n':
            if (Text.compare(Pos, 4, "null") == 0) { Pos += 4; Out = JsonValue(); return true; }
            return Fail(OutError, "Invalid literal");
        default:
            return ParseNumber(Out, OutError);
        }
    }

    bool ParseNumber(JsonValue& Out, std::string& OutError)
    {
        const size_t Start = Pos;
        if (Pos < Text.size() && Text[Pos] == '-') ++Pos;
        while (Pos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Pos]))) ++Pos;
        if (Pos < Text.size() && Text[Pos] == '.')
        {
            ++Pos;
            while (Pos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Pos]))) ++Pos;
        }
        if (Pos < Text.size() && (Text[Pos] == 'e' || Text[Pos] == 'E'))
        {
            ++Pos;
            if (Pos < Text.size() && (Text[Pos] == '+' || Text[Pos] == '-')) ++Pos;
            while (Pos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Pos]))) ++Pos;
        }
        if (Pos == Start || (Pos == Start + 1 && Text[Start] == '-'))
        {
            return Fail(OutError, "Invalid number");
        }

        const std::string Token = Text.substr(Start, Pos - Start);
        char* End = nullptr;
        const double Value = std::strtod(Token.c_str(), &End);
        if (End == Token.c_str() || *End != '\0')
        {
            Pos = Start;
            return Fail(OutError, "Invalid number");
        }
        Out = JsonValue(Value);
        return true;
    }

    void AppendUtf8(unsigned int CodePoint, std::string& Out)
    {
        if (CodePoint < 0x80)
        {
            Out += static_cast<char>(CodePoint);
        }
        else if (CodePoint < 0x800)
        {
            Out += static_cast<char>(0xC0 | (CodePoint >> 6));
            Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
        }
        else if (CodePoint < 0x10000)
        {
            Out += static_cast<char>(0xE0 | (CodePoint >> 12));
            Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
            Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
        }
        else
        {
            Out += static_cast<char>(0xF0 | (CodePoint >> 18));
            Out += static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3F));
            Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
            Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
        }
    }

    bool ParseHex4(unsigned int& Out, std::string& OutError)
    {
        if (Pos + 4 > Text.size())
        {
            return Fail(OutError, "Truncated \\u escape");
        }
        Out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char C = Text[Pos++];
            Out <<= 4;
            if (C >= '0' && C <= '9') Out |= static_cast<unsigned int>(C - '0');
            else if (C >= 'a' && C <= 'f') Out |= static_cast<unsigned int>(C - 'a' + 10);
            else if (C >= 'A' && C <= 'F') Out |= static_cast<unsigned int>(C - 'A' + 10);
            else return Fail(OutError, "Invalid \\u escape");
        }
        return true;
    }

    bool ParseString(std::string& Out, std::string& OutError)
    {
        ++Pos; // consume opening quote
        Out.clear();
        while (true)
        {
            if (Pos >= Text.size())
            {
                return Fail(OutError, "Unterminated string");
            }
            const char C = Text[Pos++];
            if (C == '"')
            {
                return true;
            }
            if (C == '\\')
            {
                if (Pos >= Text.size())
                {
                    return Fail(OutError, "Unterminated escape");
                }
                const char E = Text[Pos++];
                switch (E)
                {
                case '"':  Out += '"'; break;
                case '\\': Out += '\\'; break;
                case '/':  Out += '/'; break;
                case 'b':  Out += '\b'; break;
                case 'f':  Out += '\f'; break;
                case 'n':  Out += '\n'; break;
                case 'r':  Out += '\r'; break;
                case 't':  Out += '\t'; break;
                case 'u':
                {
                    unsigned int CodePoint = 0;
                    if (!ParseHex4(CodePoint, OutError)) return false;
                    // Surrogate pair handling for characters beyond the BMP.
                    if (CodePoint >= 0xD800 && CodePoint <= 0xDBFF
                        && Pos + 1 < Text.size() && Text[Pos] == '\\' && Text[Pos + 1] == 'u')
                    {
                        Pos += 2;
                        unsigned int Low = 0;
                        if (!ParseHex4(Low, OutError)) return false;
                        if (Low >= 0xDC00 && Low <= 0xDFFF)
                        {
                            CodePoint = 0x10000 + ((CodePoint - 0xD800) << 10) + (Low - 0xDC00);
                        }
                    }
                    AppendUtf8(CodePoint, Out);
                    break;
                }
                default:
                    return Fail(OutError, "Invalid escape character");
                }
            }
            else
            {
                Out += C;
            }
        }
    }

    bool ParseArray(JsonValue& Out, std::string& OutError)
    {
        ++Pos; // consume '['
        Out = JsonValue::MakeArray();
        SkipWhitespace();
        if (Pos < Text.size() && Text[Pos] == ']')
        {
            ++Pos;
            return true;
        }
        while (true)
        {
            JsonValue Item;
            SkipWhitespace();
            if (!ParseValue(Item, OutError)) return false;
            Out.Add(std::move(Item));
            SkipWhitespace();
            if (Pos >= Text.size()) return Fail(OutError, "Unterminated array");
            if (Text[Pos] == ',') { ++Pos; continue; }
            if (Text[Pos] == ']') { ++Pos; return true; }
            return Fail(OutError, "Expected ',' or ']'");
        }
    }

    bool ParseObject(JsonValue& Out, std::string& OutError)
    {
        ++Pos; // consume '{'
        Out = JsonValue::MakeObject();
        SkipWhitespace();
        if (Pos < Text.size() && Text[Pos] == '}')
        {
            ++Pos;
            return true;
        }
        while (true)
        {
            SkipWhitespace();
            if (Pos >= Text.size() || Text[Pos] != '"')
            {
                return Fail(OutError, "Expected object key");
            }
            std::string Key;
            if (!ParseString(Key, OutError)) return false;
            SkipWhitespace();
            if (Pos >= Text.size() || Text[Pos] != ':')
            {
                return Fail(OutError, "Expected ':'");
            }
            ++Pos;
            SkipWhitespace();
            JsonValue Value;
            if (!ParseValue(Value, OutError)) return false;
            Out.Set(Key, std::move(Value));
            SkipWhitespace();
            if (Pos >= Text.size()) return Fail(OutError, "Unterminated object");
            if (Text[Pos] == ',') { ++Pos; continue; }
            if (Text[Pos] == '}') { ++Pos; return true; }
            return Fail(OutError, "Expected ',' or '}'");
        }
    }
};

} // anonymous namespace

bool JsonValue::Parse(const std::string& Text, JsonValue& OutValue, std::string& OutError)
{
    Parser P(Text);
    return P.ParseDocument(OutValue, OutError);
}

} // namespace WarpViz
} // namespace AnimForge
