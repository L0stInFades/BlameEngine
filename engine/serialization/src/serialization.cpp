#include "next/serialization/serialization.h"
#include "next/foundation/logger.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace Next {

namespace {

class SerializationLogStream {
public:
    explicit SerializationLogStream(LogLevel level)
        : level_(level) {}

    ~SerializationLogStream() {
        Logger::Log(level_, "%s", stream_.str().c_str());
    }

    template <typename T>
    SerializationLogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream stream_;
};

#define SERIALIZATION_LOG_INFO() SerializationLogStream(::Next::LogLevel::Info)
#define SERIALIZATION_LOG_ERROR() SerializationLogStream(::Next::LogLevel::Error)

} // namespace

// =============================================================================
// JSONSerializer 实现
// =============================================================================

JSONSerializer::JSONSerializer() {
    buffer_ << std::boolalpha;  // 使用 true/false 而不是 1/0
}

void JSONSerializer::EnsureDocumentStarted() {
    if (documentFinalized_) {
        return;
    }
    if (documentWrapperStarted_) {
        return;
    }
    if (indentLevel_ != 0) {
        return;
    }

    // The serializer commonly writes a top-level keyed object (e.g. `"root": {...}`),
    // which must be wrapped in `{ ... }` to form valid JSON.
    buffer_ << "{\n";
    indentLevel_ = 1;
    firstElement_ = true;
    inArray_ = false;
    documentWrapperStarted_ = true;
}

void JSONSerializer::FinalizeDocument() {
    if (documentFinalized_) {
        return;
    }
    if (!documentWrapperStarted_) {
        return;
    }

    // We expect all user-opened containers to be closed, leaving only the document wrapper open.
    if (indentLevel_ != 1 || inArray_) {
        SERIALIZATION_LOG_ERROR() << "JSONSerializer: cannot finalize document (indentLevel=" << indentLevel_
                         << ", inArray=" << (inArray_ ? "true" : "false") << ")";
        return;
    }

    buffer_ << "\n";
    indentLevel_--;
    WriteIndent();
    buffer_ << "}\n";
    documentFinalized_ = true;
}

void JSONSerializer::WriteIndent() {
    for (int i = 0; i < indentLevel_; ++i) {
        buffer_ << "  ";
    }
}

void JSONSerializer::WriteKey(const std::string& key) {
    if (!key.empty()) {
        buffer_ << "\"" << key << "\": ";
    }
}

void JSONSerializer::WriteBool(const std::string& key, bool value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << (value ? "true" : "false");
    firstElement_ = false;
}

void JSONSerializer::WriteInt32(const std::string& key, int32_t value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << value;
    firstElement_ = false;
}

void JSONSerializer::WriteInt64(const std::string& key, int64_t value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << value;
    firstElement_ = false;
}

void JSONSerializer::WriteUInt32(const std::string& key, uint32_t value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << value;
    firstElement_ = false;
}

void JSONSerializer::WriteUInt64(const std::string& key, uint64_t value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << value;
    firstElement_ = false;
}

void JSONSerializer::WriteFloat(const std::string& key, float value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << std::setprecision(9) << value;
    firstElement_ = false;
}

void JSONSerializer::WriteDouble(const std::string& key, double value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << std::setprecision(17) << value;
    firstElement_ = false;
}

void JSONSerializer::WriteString(const std::string& key, const std::string& value) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << "\"";

    // 转义特殊字符
    for (char c : value) {
        switch (c) {
            case '"':  buffer_ << "\\\""; break;
            case '\\': buffer_ << "\\\\"; break;
            case '\b': buffer_ << "\\b"; break;
            case '\f': buffer_ << "\\f"; break;
            case '\n': buffer_ << "\\n"; break;
            case '\r': buffer_ << "\\r"; break;
            case '\t': buffer_ << "\\t"; break;
            default:
                if (c < 32) {
                    buffer_ << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    buffer_ << c;
                }
                break;
        }
    }

    buffer_ << "\"";
    firstElement_ = false;
}

void JSONSerializer::BeginArray(const std::string& key, size_t size) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << "[\n";
    indentLevel_++;
    firstElement_ = true;
    inArray_ = true;
}

void JSONSerializer::EndArray() {
    indentLevel_--;
    buffer_ << "\n";
    WriteIndent();
    buffer_ << "]";
    firstElement_ = false;
    inArray_ = false;
}

void JSONSerializer::BeginObject(const std::string& key) {
    if (indentLevel_ == 0 && !key.empty()) {
        EnsureDocumentStarted();
    }
    if (!firstElement_) {
        buffer_ << ",\n";
    }
    WriteIndent();
    WriteKey(key);
    buffer_ << "{\n";
    indentLevel_++;
    firstElement_ = true;
}

void JSONSerializer::EndObject() {
    indentLevel_--;
    buffer_ << "\n";
    WriteIndent();
    buffer_ << "}";
    firstElement_ = false;
}

void JSONSerializer::WriteVersion(uint32_t version) {
    WriteUInt32("__version__", version);
}

SerializationResult JSONSerializer::SaveToFile(const std::string& filePath) {
    if (!documentFinalized_) {
        FinalizeDocument();
    }
    if (documentWrapperStarted_ && !documentFinalized_) {
        return SerializationResult::Fail(SerializationError::ParseError, "Serializer has unbalanced Begin/End calls");
    }

    std::ofstream file(filePath);
    if (!file.is_open()) {
        SERIALIZATION_LOG_ERROR() << "Failed to open file for writing: " << filePath;
        return SerializationResult::Fail(SerializationError::IOError, "Failed to open file");
    }

    file << buffer_.str();
    file.close();

    SERIALIZATION_LOG_INFO() << "Saved JSON to file: " << filePath;
    return SerializationResult::Success();
}

std::string JSONSerializer::ToString() {
    if (!documentFinalized_) {
        FinalizeDocument();
    }
    return buffer_.str();
}

// =============================================================================
// JSONDeserializer 实现
// =============================================================================

namespace {

struct JsonParser {
    const std::string& input;
    size_t pos = 0;
    std::string error;

    explicit JsonParser(const std::string& json) : input(json) {}

    void SkipWhitespace() {
        while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) {
            ++pos;
        }
    }

    bool Match(char expected) {
        SkipWhitespace();
        if (pos < input.size() && input[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    bool ParseLiteral(const char* literal) {
        SkipWhitespace();
        size_t length = std::strlen(literal);
        if (input.compare(pos, length, literal) == 0) {
            pos += length;
            return true;
        }
        return false;
    }

    SerializationDetail::JsonValue ParseValue() {
        SkipWhitespace();
        if (pos >= input.size()) {
            error = "Unexpected end of input";
            return {};
        }

        char c = input[pos];
        if (c == '{') {
            return ParseObject();
        }
        if (c == '[') {
            return ParseArray();
        }
        if (c == '"') {
            return ParseString();
        }
        if (c == 't' || c == 'f') {
            return ParseBool();
        }
        if (c == 'n') {
            return ParseNull();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return ParseNumber();
        }

        error = "Unexpected character in JSON";
        return {};
    }

    SerializationDetail::JsonValue ParseNull() {
        if (!ParseLiteral("null")) {
            error = "Invalid null literal";
            return {};
        }
        SerializationDetail::JsonValue value;
        value.type = SerializationDetail::JsonValue::Type::Null;
        return value;
    }

    SerializationDetail::JsonValue ParseBool() {
        SerializationDetail::JsonValue value;
        if (ParseLiteral("true")) {
            value.type = SerializationDetail::JsonValue::Type::Bool;
            value.boolValue = true;
            return value;
        }
        if (ParseLiteral("false")) {
            value.type = SerializationDetail::JsonValue::Type::Bool;
            value.boolValue = false;
            return value;
        }
        error = "Invalid boolean literal";
        return {};
    }

    SerializationDetail::JsonValue ParseNumber() {
        SkipWhitespace();
        size_t start = pos;
        if (pos < input.size() && input[pos] == '-') {
            ++pos;
        }
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) {
            ++pos;
        }
        if (pos < input.size() && input[pos] == '.') {
            ++pos;
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) {
                ++pos;
            }
        }
        if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
            ++pos;
            if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) {
                ++pos;
            }
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) {
                ++pos;
            }
        }

        std::string token = input.substr(start, pos - start);
        if (token.empty() || token == "-") {
            error = "Invalid number";
            return {};
        }

        char* endPtr = nullptr;
        double number = std::strtod(token.c_str(), &endPtr);
        if (!endPtr || *endPtr != '\0') {
            error = "Invalid number";
            return {};
        }

        SerializationDetail::JsonValue value;
        value.type = SerializationDetail::JsonValue::Type::Number;
        value.numberValue = number;
        return value;
    }

    SerializationDetail::JsonValue ParseString() {
        SerializationDetail::JsonValue value;
        value.type = SerializationDetail::JsonValue::Type::String;
        if (!Match('"')) {
            error = "Expected string";
            return {};
        }

        std::string result;
        while (pos < input.size()) {
            char c = input[pos++];
            if (c == '"') {
                value.stringValue = result;
                return value;
            }
            if (c == '\\') {
                if (pos >= input.size()) {
                    error = "Invalid escape sequence";
                    return {};
                }
                char esc = input[pos++];
                switch (esc) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > input.size()) {
                            error = "Invalid unicode escape";
                            return {};
                        }
                        unsigned int code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char hex = input[pos++];
                            code <<= 4;
                            if (hex >= '0' && hex <= '9') {
                                code += static_cast<unsigned int>(hex - '0');
                            } else if (hex >= 'a' && hex <= 'f') {
                                code += static_cast<unsigned int>(hex - 'a' + 10);
                            } else if (hex >= 'A' && hex <= 'F') {
                                code += static_cast<unsigned int>(hex - 'A' + 10);
                            } else {
                                error = "Invalid unicode escape";
                                return {};
                            }
                        }
                        if (code <= 0x7F) {
                            result.push_back(static_cast<char>(code));
                        } else if (code <= 0x7FF) {
                            result.push_back(static_cast<char>(0xC0 | ((code >> 6) & 0x1F)));
                            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            result.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
                            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default:
                        error = "Invalid escape sequence";
                        return {};
                }
            } else {
                result.push_back(c);
            }
        }

        error = "Unterminated string";
        return {};
    }

    SerializationDetail::JsonValue ParseArray() {
        SerializationDetail::JsonValue value;
        value.type = SerializationDetail::JsonValue::Type::Array;
        if (!Match('[')) {
            error = "Expected array";
            return {};
        }

        SkipWhitespace();
        if (Match(']')) {
            return value;
        }

        while (pos < input.size()) {
            SerializationDetail::JsonValue item = ParseValue();
            if (!error.empty()) {
                return {};
            }
            value.arrayValue.push_back(std::move(item));
            SkipWhitespace();
            if (Match(']')) {
                return value;
            }
            if (!Match(',')) {
                error = "Expected ',' or ']' in array";
                return {};
            }
        }

        error = "Unterminated array";
        return {};
    }

    SerializationDetail::JsonValue ParseObject() {
        SerializationDetail::JsonValue value;
        value.type = SerializationDetail::JsonValue::Type::Object;
        if (!Match('{')) {
            error = "Expected object";
            return {};
        }

        SkipWhitespace();
        if (Match('}')) {
            return value;
        }

        while (pos < input.size()) {
            SerializationDetail::JsonValue keyValue = ParseString();
            if (!error.empty()) {
                return {};
            }
            SkipWhitespace();
            if (!Match(':')) {
                error = "Expected ':' after key";
                return {};
            }
            SerializationDetail::JsonValue data = ParseValue();
            if (!error.empty()) {
                return {};
            }
            value.objectValue.emplace(std::move(keyValue.stringValue), std::move(data));
            SkipWhitespace();
            if (Match('}')) {
                return value;
            }
            if (!Match(',')) {
                error = "Expected ',' or '}' in object";
                return {};
            }
        }

        error = "Unterminated object";
        return {};
    }
};

} // namespace

JSONDeserializer::JSONDeserializer(const std::string& json)
    : json_(json) {
    JsonParser parser(json_);
    root_ = parser.ParseValue();
    parser.SkipWhitespace();
    if (!parser.error.empty()) {
        parseOk_ = false;
        errorMessage_ = parser.error;
        root_ = {};
        return;
    }
    if (parser.pos != json_.size()) {
        parseOk_ = false;
        errorMessage_ = "Trailing characters after JSON";
        root_ = {};
        return;
    }
    parseOk_ = true;
    contextStack_.push_back({&root_, 0});
}

const SerializationDetail::JsonValue* JSONDeserializer::CurrentContextValue() const {
    if (contextStack_.empty()) {
        return nullptr;
    }
    return contextStack_.back().value;
}

void JSONDeserializer::PushContext(const SerializationDetail::JsonValue* value) {
    if (!value) {
        return;
    }
    contextStack_.push_back({value, 0});
}

void JSONDeserializer::PopContext() {
    if (!contextStack_.empty()) {
        contextStack_.pop_back();
    }
}

const SerializationDetail::JsonValue* JSONDeserializer::ResolveValue(const std::string& key, bool advanceArrayIndex) {
    if (!parseOk_) {
        return nullptr;
    }
    const SerializationDetail::JsonValue* context = CurrentContextValue();
    if (!context) {
        return nullptr;
    }

    if (!key.empty()) {
        if (context->type != SerializationDetail::JsonValue::Type::Object) {
            return nullptr;
        }
        auto it = context->objectValue.find(key);
        if (it == context->objectValue.end()) {
            return nullptr;
        }
        return &it->second;
    }

    if (context->type == SerializationDetail::JsonValue::Type::Array) {
        auto& ctx = contextStack_.back();
        if (ctx.index >= context->arrayValue.size()) {
            return nullptr;
        }
        const SerializationDetail::JsonValue* value = &context->arrayValue[ctx.index];
        if (advanceArrayIndex) {
            ctx.index++;
        }
        return value;
    }

    return context;
}

bool JSONDeserializer::ReadBool(const std::string& key, bool defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Bool) {
        return defaultValue;
    }
    return value->boolValue;
}

int32_t JSONDeserializer::ReadInt32(const std::string& key, int32_t defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Number) {
        return defaultValue;
    }
    return static_cast<int32_t>(value->numberValue);
}

int64_t JSONDeserializer::ReadInt64(const std::string& key, int64_t defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Number) {
        return defaultValue;
    }
    return static_cast<int64_t>(value->numberValue);
}

uint32_t JSONDeserializer::ReadUInt32(const std::string& key, uint32_t defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Number) {
        return defaultValue;
    }
    if (value->numberValue < 0.0) {
        return defaultValue;
    }
    return static_cast<uint32_t>(value->numberValue);
}

uint64_t JSONDeserializer::ReadUInt64(const std::string& key, uint64_t defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Number) {
        return defaultValue;
    }
    if (value->numberValue < 0.0) {
        return defaultValue;
    }
    return static_cast<uint64_t>(value->numberValue);
}

float JSONDeserializer::ReadFloat(const std::string& key, float defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Number) {
        return defaultValue;
    }
    return static_cast<float>(value->numberValue);
}

double JSONDeserializer::ReadDouble(const std::string& key, double defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Number) {
        return defaultValue;
    }
    return value->numberValue;
}

std::string JSONDeserializer::ReadString(const std::string& key, const std::string& defaultValue) {
    const auto* value = ResolveValue(key, true);
    if (!value || value->type != SerializationDetail::JsonValue::Type::String) {
        return defaultValue;
    }
    return value->stringValue;
}

bool JSONDeserializer::BeginArray(const std::string& key) {
    const auto* value = ResolveValue(key, false);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Array) {
        return false;
    }
    if (key.empty()) {
        if (!contextStack_.empty()) {
            contextStack_.back().index++;
        }
    }
    PushContext(value);
    return true;
}

size_t JSONDeserializer::GetArraySize() {
    const auto* value = CurrentContextValue();
    if (!value || value->type != SerializationDetail::JsonValue::Type::Array) {
        return 0;
    }
    return value->arrayValue.size();
}

void JSONDeserializer::EndArray() {
    PopContext();
}

bool JSONDeserializer::BeginObject(const std::string& key) {
    const auto* value = ResolveValue(key, false);
    if (!value || value->type != SerializationDetail::JsonValue::Type::Object) {
        return false;
    }
    if (key.empty()) {
        if (!contextStack_.empty()) {
            contextStack_.back().index++;
        }
    }
    PushContext(value);
    return true;
}

void JSONDeserializer::EndObject() {
    PopContext();
}

uint32_t JSONDeserializer::ReadVersion() {
    return ReadUInt32("__version__", 1);
}

bool JSONDeserializer::HasKey(const std::string& key) {
    const auto* context = CurrentContextValue();
    if (!context || context->type != SerializationDetail::JsonValue::Type::Object) {
        return false;
    }
    return context->objectValue.find(key) != context->objectValue.end();
}

bool JSONDeserializer::GetObjectKeys(std::vector<std::string>& keys) {
    keys.clear();
    const auto* context = CurrentContextValue();
    if (!context || context->type != SerializationDetail::JsonValue::Type::Object) {
        return false;
    }
    keys.reserve(context->objectValue.size());
    for (const auto& entry : context->objectValue) {
        keys.push_back(entry.first);
    }
    return true;
}

// =============================================================================
// BinarySerializer 实现
// =============================================================================

BinarySerializer::BinarySerializer() {
    buffer_.reserve(4096);  // 预分配 4KB
}

void BinarySerializer::WriteData(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), bytes, bytes + size);
}

void BinarySerializer::WriteBool(const std::string& key, bool value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteInt32(const std::string& key, int32_t value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteInt64(const std::string& key, int64_t value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteUInt32(const std::string& key, uint32_t value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteUInt64(const std::string& key, uint64_t value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteFloat(const std::string& key, float value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteDouble(const std::string& key, double value) {
    WriteData(&value, sizeof(value));
}

void BinarySerializer::WriteString(const std::string& key, const std::string& value) {
    uint32_t length = static_cast<uint32_t>(value.length());
    WriteData(&length, sizeof(length));
    WriteData(value.c_str(), length);
}

void BinarySerializer::BeginArray(const std::string& key, size_t size) {
    // 二进制格式：写入数组大小
    uint32_t arraySize = static_cast<uint32_t>(size);
    WriteData(&arraySize, sizeof(arraySize));
}

void BinarySerializer::EndArray() {
    // 二进制格式：无需操作
}

void BinarySerializer::BeginObject(const std::string& key) {
    // 二进制格式：无需操作
}

void BinarySerializer::EndObject() {
    // 二进制格式：无需操作
}

void BinarySerializer::WriteVersion(uint32_t version) {
    WriteUInt32("__version__", version);
}

SerializationResult BinarySerializer::SaveToFile(const std::string& filePath) {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        SERIALIZATION_LOG_ERROR() << "Failed to open binary file for writing: " << filePath;
        return SerializationResult::Fail(SerializationError::IOError, "Failed to open file");
    }

    file.write(reinterpret_cast<const char*>(buffer_.data()), buffer_.size());
    file.close();

    SERIALIZATION_LOG_INFO() << "Saved binary data to file: " << filePath << " (" << buffer_.size() << " bytes)";
    return SerializationResult::Success();
}

std::string BinarySerializer::ToString() {
    return "<binary data>";
}

// =============================================================================
// BinaryDeserializer 实现
// =============================================================================

BinaryDeserializer::BinaryDeserializer(const std::vector<uint8_t>& data)
    : data_(data), readPos_(0) {
    uint32_t version = 0;
    if (ReadValue(version)) {
        version_ = version;
    }
}

bool BinaryDeserializer::ReadBool(const std::string& key, bool defaultValue) {
    bool value = false;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

int32_t BinaryDeserializer::ReadInt32(const std::string& key, int32_t defaultValue) {
    int32_t value = 0;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

int64_t BinaryDeserializer::ReadInt64(const std::string& key, int64_t defaultValue) {
    int64_t value = 0;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

uint32_t BinaryDeserializer::ReadUInt32(const std::string& key, uint32_t defaultValue) {
    uint32_t value = 0;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

uint64_t BinaryDeserializer::ReadUInt64(const std::string& key, uint64_t defaultValue) {
    uint64_t value = 0;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

float BinaryDeserializer::ReadFloat(const std::string& key, float defaultValue) {
    float value = 0.0f;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

double BinaryDeserializer::ReadDouble(const std::string& key, double defaultValue) {
    double value = 0.0;
    if (!ReadValue(value)) return defaultValue;
    return value;
}

std::string BinaryDeserializer::ReadString(const std::string& key, const std::string& defaultValue) {
    const size_t lengthOffset = readPos_;
    uint32_t length = 0;
    if (!ReadValue(length)) return defaultValue;

    if (static_cast<size_t>(length) > data_.size() - readPos_) {
        readPos_ = lengthOffset;
        return defaultValue;
    }

    if (length == 0) {
        return "";
    }

    std::string value(reinterpret_cast<const char*>(&data_[readPos_]), length);
    readPos_ += length;
    return value;
}

bool BinaryDeserializer::BeginArray(const std::string& key) {
    uint32_t size = 0;
    if (!ReadValue(size)) {
        return false;
    }
    arraySizeStack_.push_back(static_cast<size_t>(size));
    return true;
}

size_t BinaryDeserializer::GetArraySize() {
    if (arraySizeStack_.empty()) {
        return 0;
    }
    return arraySizeStack_.back();
}

void BinaryDeserializer::EndArray() {
    if (!arraySizeStack_.empty()) {
        arraySizeStack_.pop_back();
    }
}

bool BinaryDeserializer::BeginObject(const std::string& key) {
    return true;
}

void BinaryDeserializer::EndObject() {
    // 无需操作
}

uint32_t BinaryDeserializer::ReadVersion() {
    return version_;
}

bool BinaryDeserializer::HasKey(const std::string& key) {
    // 二进制格式中 key 主要用于文档，实际不存储
    return true;
}

bool BinaryDeserializer::GetObjectKeys(std::vector<std::string>& keys) {
    keys.clear();
    return false;
}

// =============================================================================
// Deserializer 静态方法
// =============================================================================

std::unique_ptr<Deserializer> Deserializer::LoadFromFile(const std::string& filePath, SerializationFormat format) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        SERIALIZATION_LOG_ERROR() << "Failed to open file for reading: " << filePath;
        return nullptr;
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        SERIALIZATION_LOG_ERROR() << "Failed to determine file size: " << filePath;
        return nullptr;
    }
    file.seekg(0, std::ios::beg);
    if (!file) {
        SERIALIZATION_LOG_ERROR() << "Failed to seek file for reading: " << filePath;
        return nullptr;
    }

    if (format == SerializationFormat::Binary) {
        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (size != 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
            SERIALIZATION_LOG_ERROR() << "Failed to read binary file: " << filePath;
            return nullptr;
        }
        return std::make_unique<BinaryDeserializer>(data);
    } else {
        std::string data(static_cast<size_t>(size), '\0');
        if (size != 0 && !file.read(data.data(), size)) {
            SERIALIZATION_LOG_ERROR() << "Failed to read JSON file: " << filePath;
            return nullptr;
        }
        return std::make_unique<JSONDeserializer>(data);
    }
}

std::unique_ptr<Deserializer> Deserializer::LoadFromString(const std::string& data, SerializationFormat format) {
    if (format == SerializationFormat::Binary) {
        std::vector<uint8_t> binaryData(data.begin(), data.end());
        return std::make_unique<BinaryDeserializer>(binaryData);
    } else {
        return std::make_unique<JSONDeserializer>(data);
    }
}

} // namespace Next

#undef SERIALIZATION_LOG_INFO
#undef SERIALIZATION_LOG_ERROR
