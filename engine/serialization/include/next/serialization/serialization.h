#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <fstream>
#include <type_traits>
#include <functional>
#include <sstream>
#include <cstring>

namespace SerializationDetail {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;
};

struct JsonContext {
    const JsonValue* value = nullptr;
    size_t index = 0;
};

} // namespace SerializationDetail

namespace Next {

/**
 * @brief 统一序列化系统
 *
 * 特性：
 * 1. 支持多种格式（JSON/Binary）
 * 2. 支持版本控制
 * 3. 支持向后兼容
 * 4. 类型安全
 * 5. 高性能
 * 6. 易于扩展
 */

/**
 * @brief 序列化格式
 */
enum class SerializationFormat {
    JSON,       // JSON 格式（可读性好）
    Binary,     // 二进制格式（性能好）
    Compact     // 紧凑 JSON（压缩）
};

/**
 * @brief 序列化错误
 */
enum class SerializationError {
    None,
    FileNotFound,
    ParseError,
    VersionMismatch,
    TypeMismatch,
    InvalidData,
    IOError
};

/**
 * @brief 序列化结果
 */
struct SerializationResult {
    SerializationError error = SerializationError::None;
    std::string errorMessage;

    bool IsSuccess() const { return error == SerializationError::None; }
    static SerializationResult Success() { return SerializationResult{}; }
    static SerializationResult Fail(SerializationError err, const std::string& msg) {
        SerializationResult result;
        result.error = err;
        result.errorMessage = msg;
        return result;
    }
};

/**
 * @brief 序列化接口
 */
class Serializer {
public:
    virtual ~Serializer() = default;

    // 基础类型
    virtual void WriteBool(const std::string& key, bool value) = 0;
    virtual void WriteInt32(const std::string& key, int32_t value) = 0;
    virtual void WriteInt64(const std::string& key, int64_t value) = 0;
    virtual void WriteUInt32(const std::string& key, uint32_t value) = 0;
    virtual void WriteUInt64(const std::string& key, uint64_t value) = 0;
    virtual void WriteFloat(const std::string& key, float value) = 0;
    virtual void WriteDouble(const std::string& key, double value) = 0;
    virtual void WriteString(const std::string& key, const std::string& value) = 0;

    // 数组和对象
    virtual void BeginArray(const std::string& key, size_t size) = 0;
    virtual void EndArray() = 0;
    virtual void BeginObject(const std::string& key) = 0;
    virtual void EndObject() = 0;

    // 版本控制
    virtual void WriteVersion(uint32_t version) = 0;

    // 保存到文件
    virtual SerializationResult SaveToFile(const std::string& filePath) = 0;
    virtual std::string ToString() = 0;
};

/**
 * @brief 反序列化接口
 */
class Deserializer {
public:
    virtual ~Deserializer() = default;

    // 基础类型
    virtual bool ReadBool(const std::string& key, bool defaultValue = false) = 0;
    virtual int32_t ReadInt32(const std::string& key, int32_t defaultValue = 0) = 0;
    virtual int64_t ReadInt64(const std::string& key, int64_t defaultValue = 0) = 0;
    virtual uint32_t ReadUInt32(const std::string& key, uint32_t defaultValue = 0) = 0;
    virtual uint64_t ReadUInt64(const std::string& key, uint64_t defaultValue = 0) = 0;
    virtual float ReadFloat(const std::string& key, float defaultValue = 0.0f) = 0;
    virtual double ReadDouble(const std::string& key, double defaultValue = 0.0) = 0;
    virtual std::string ReadString(const std::string& key, const std::string& defaultValue = "") = 0;

    // 数组和对象
    virtual bool BeginArray(const std::string& key) = 0;
    virtual size_t GetArraySize() = 0;
    virtual void EndArray() = 0;
    virtual bool BeginObject(const std::string& key) = 0;
    virtual void EndObject() = 0;

    // 版本控制
    virtual uint32_t ReadVersion() = 0;

    // 检查字段是否存在
    virtual bool HasKey(const std::string& key) = 0;

    // 获取对象字段列表（用于容器反序列化）
    virtual bool GetObjectKeys(std::vector<std::string>& keys) { (void)keys; return false; }

    // 从文件加载
    static std::unique_ptr<Deserializer> LoadFromFile(const std::string& filePath, SerializationFormat format);
    static std::unique_ptr<Deserializer> LoadFromString(const std::string& data, SerializationFormat format);
};

/**
 * @brief JSON 序列化器
 */
class JSONSerializer : public Serializer {
public:
    JSONSerializer();
    ~JSONSerializer() override = default;

    void WriteBool(const std::string& key, bool value) override;
    void WriteInt32(const std::string& key, int32_t value) override;
    void WriteInt64(const std::string& key, int64_t value) override;
    void WriteUInt32(const std::string& key, uint32_t value) override;
    void WriteUInt64(const std::string& key, uint64_t value) override;
    void WriteFloat(const std::string& key, float value) override;
    void WriteDouble(const std::string& key, double value) override;
    void WriteString(const std::string& key, const std::string& value) override;

    void BeginArray(const std::string& key, size_t size) override;
    void EndArray() override;
    void BeginObject(const std::string& key) override;
    void EndObject() override;

    void WriteVersion(uint32_t version) override;

    SerializationResult SaveToFile(const std::string& filePath) override;
    std::string ToString() override;

private:
    std::ostringstream buffer_;
    int indentLevel_ = 0;
    bool inArray_ = false;
    bool firstElement_ = true;
    bool documentWrapperStarted_ = false;  // Emits an outer `{ ... }` wrapper when writing top-level keyed values.
    bool documentFinalized_ = false;

    void WriteIndent();
    void WriteKey(const std::string& key);
    void EnsureDocumentStarted();
    void FinalizeDocument();
};

/**
 * @brief JSON 反序列化器
 */
class JSONDeserializer : public Deserializer {
public:
    explicit JSONDeserializer(const std::string& json);
    ~JSONDeserializer() override = default;

    bool ReadBool(const std::string& key, bool defaultValue = false) override;
    int32_t ReadInt32(const std::string& key, int32_t defaultValue = 0) override;
    int64_t ReadInt64(const std::string& key, int64_t defaultValue = 0) override;
    uint32_t ReadUInt32(const std::string& key, uint32_t defaultValue = 0) override;
    uint64_t ReadUInt64(const std::string& key, uint64_t defaultValue = 0) override;
    float ReadFloat(const std::string& key, float defaultValue = 0.0f) override;
    double ReadDouble(const std::string& key, double defaultValue = 0.0) override;
    std::string ReadString(const std::string& key, const std::string& defaultValue = "") override;

    bool BeginArray(const std::string& key) override;
    size_t GetArraySize() override;
    void EndArray() override;
    bool BeginObject(const std::string& key) override;
    void EndObject() override;

    uint32_t ReadVersion() override;
    bool HasKey(const std::string& key) override;
    bool GetObjectKeys(std::vector<std::string>& keys) override;

private:
    std::string json_;
    SerializationDetail::JsonValue root_;
    std::vector<SerializationDetail::JsonContext> contextStack_;
    bool parseOk_ = false;
    std::string errorMessage_;

    const SerializationDetail::JsonValue* ResolveValue(const std::string& key, bool advanceArrayIndex);
    const SerializationDetail::JsonValue* CurrentContextValue() const;
    void PushContext(const SerializationDetail::JsonValue* value);
    void PopContext();
};

/**
 * @brief 二进制序列化器
 */
class BinarySerializer : public Serializer {
public:
    BinarySerializer();
    ~BinarySerializer() override = default;

    void WriteBool(const std::string& key, bool value) override;
    void WriteInt32(const std::string& key, int32_t value) override;
    void WriteInt64(const std::string& key, int64_t value) override;
    void WriteUInt32(const std::string& key, uint32_t value) override;
    void WriteUInt64(const std::string& key, uint64_t value) override;
    void WriteFloat(const std::string& key, float value) override;
    void WriteDouble(const std::string& key, double value) override;
    void WriteString(const std::string& key, const std::string& value) override;

    void BeginArray(const std::string& key, size_t size) override;
    void EndArray() override;
    void BeginObject(const std::string& key) override;
    void EndObject() override;

    void WriteVersion(uint32_t version) override;

    SerializationResult SaveToFile(const std::string& filePath) override;
    std::string ToString() override;

private:
    std::vector<uint8_t> buffer_;
    std::vector<std::string> keyStack_;

    void WriteData(const void* data, size_t size);
};

/**
 * @brief 二进制反序列化器
 */
class BinaryDeserializer : public Deserializer {
public:
    explicit BinaryDeserializer(const std::vector<uint8_t>& data);
    ~BinaryDeserializer() override = default;

    bool ReadBool(const std::string& key, bool defaultValue = false) override;
    int32_t ReadInt32(const std::string& key, int32_t defaultValue = 0) override;
    int64_t ReadInt64(const std::string& key, int64_t defaultValue = 0) override;
    uint32_t ReadUInt32(const std::string& key, uint32_t defaultValue = 0) override;
    uint64_t ReadUInt64(const std::string& key, uint64_t defaultValue = 0) override;
    float ReadFloat(const std::string& key, float defaultValue = 0.0f) override;
    double ReadDouble(const std::string& key, double defaultValue = 0.0) override;
    std::string ReadString(const std::string& key, const std::string& defaultValue = "") override;

    bool BeginArray(const std::string& key) override;
    size_t GetArraySize() override;
    void EndArray() override;
    bool BeginObject(const std::string& key) override;
    void EndObject() override;

    uint32_t ReadVersion() override;
    bool HasKey(const std::string& key) override;
    bool GetObjectKeys(std::vector<std::string>& keys) override;

private:
    std::vector<uint8_t> data_;
    std::vector<size_t> arraySizeStack_;
    size_t readPos_ = 0;
    uint32_t version_ = 0;

    template<typename T>
    bool ReadValue(T& value) {
        return ReadBytes(&value, sizeof(T));
    }

    bool ReadBytes(void* out, size_t size) {
        if (!out && size != 0) {
            return false;
        }
        if (readPos_ > data_.size() || size > data_.size() - readPos_) {
            return false;
        }
        if (size != 0) {
            std::memcpy(out, data_.data() + readPos_, size);
        }
        readPos_ += size;
        return true;
    }
};

/**
 * @brief 可序列化接口
 */
class ISerializable {
public:
    virtual ~ISerializable() = default;

    // 序列化
    virtual void Serialize(Serializer* serializer) const = 0;
    virtual void Deserialize(Deserializer* deserializer) = 0;

    // 版本信息
    virtual uint32_t GetVersion() const { return 1; }
    virtual const char* GetTypeName() const = 0;
};

/**
 * @brief 序列化辅助函数
 */
namespace SerializationHelper {

// 基础类型序列化
template<typename T>
typename std::enable_if<std::is_integral<T>::value>::type
Serialize(Serializer* serializer, const std::string& key, T value) {
    if constexpr (sizeof(T) <= 4) {
        if constexpr (std::is_signed<T>::value) {
            serializer->WriteInt32(key, static_cast<int32_t>(value));
        } else {
            serializer->WriteUInt32(key, static_cast<uint32_t>(value));
        }
    } else {
        if constexpr (std::is_signed<T>::value) {
            serializer->WriteInt64(key, static_cast<int64_t>(value));
        } else {
            serializer->WriteUInt64(key, static_cast<uint64_t>(value));
        }
    }
}

// 浮点类型序列化
template<typename T>
typename std::enable_if<std::is_floating_point<T>::value>::type
Serialize(Serializer* serializer, const std::string& key, T value) {
    if constexpr (sizeof(T) == 4) {
        serializer->WriteFloat(key, static_cast<float>(value));
    } else {
        serializer->WriteDouble(key, static_cast<double>(value));
    }
}

// 字符串序列化
inline void Serialize(Serializer* serializer, const std::string& key, const std::string& value) {
    serializer->WriteString(key, value);
}

// bool 序列化
inline void Serialize(Serializer* serializer, const std::string& key, bool value) {
    serializer->WriteBool(key, value);
}

// 容器序列化
template<typename T>
void Serialize(Serializer* serializer, const std::string& key, const std::vector<T>& value) {
    serializer->BeginArray(key, value.size());
    for (const auto& item : value) {
        Serialize(serializer, "", item);
    }
    serializer->EndArray();
}

template<typename K, typename V>
void Serialize(Serializer* serializer, const std::string& key, const std::unordered_map<K, V>& value) {
    serializer->BeginObject(key);
    for (const auto& [k, v] : value) {
        Serialize(serializer, std::to_string(k), v);
    }
    serializer->EndObject();
}

// ISerializable 对象序列化
template<typename T>
typename std::enable_if<std::is_base_of<ISerializable, T>::value>::type
Serialize(Serializer* serializer, const std::string& key, const T& value) {
    serializer->BeginObject(key);
    serializer->WriteVersion(value.GetVersion());
    value.Serialize(serializer);
    serializer->EndObject();
}

// 反序列化
template<typename T>
T Deserialize(Deserializer* deserializer, const std::string& key);

// 基础类型反序列化
template<>
inline bool Deserialize<bool>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadBool(key);
}

template<>
inline int32_t Deserialize<int32_t>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadInt32(key);
}

template<>
inline int64_t Deserialize<int64_t>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadInt64(key);
}

template<>
inline uint32_t Deserialize<uint32_t>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadUInt32(key);
}

template<>
inline uint64_t Deserialize<uint64_t>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadUInt64(key);
}

template<>
inline float Deserialize<float>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadFloat(key);
}

template<>
inline double Deserialize<double>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadDouble(key);
}

template<>
inline std::string Deserialize<std::string>(Deserializer* deserializer, const std::string& key) {
    return deserializer->ReadString(key);
}

// 容器反序列化
template<typename T>
std::vector<T> DeserializeVector(Deserializer* deserializer, const std::string& key) {
    std::vector<T> result;
    if (!deserializer->BeginArray(key)) {
        return result;
    }

    size_t count = deserializer->GetArraySize();
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.push_back(Deserialize<T>(deserializer, ""));
    }

    deserializer->EndArray();
    return result;
}

template<typename V>
std::unordered_map<std::string, V> DeserializeStringMap(Deserializer* deserializer, const std::string& key) {
    std::unordered_map<std::string, V> result;
    if (!deserializer->BeginObject(key)) {
        return result;
    }

    std::vector<std::string> keys;
    if (deserializer->GetObjectKeys(keys)) {
        for (const auto& entryKey : keys) {
            result[entryKey] = Deserialize<V>(deserializer, entryKey);
        }
    }

    deserializer->EndObject();
    return result;
}

template<typename V>
std::map<std::string, V> DeserializeOrderedStringMap(Deserializer* deserializer, const std::string& key) {
    std::map<std::string, V> result;
    if (!deserializer->BeginObject(key)) {
        return result;
    }

    std::vector<std::string> keys;
    if (deserializer->GetObjectKeys(keys)) {
        for (const auto& entryKey : keys) {
            result[entryKey] = Deserialize<V>(deserializer, entryKey);
        }
    }

    deserializer->EndObject();
    return result;
}

} // namespace SerializationHelper

/**
 * @brief 便捷序列化函数
 */
template<typename T>
SerializationResult SerializeToFile(const T& obj, const std::string& filePath, SerializationFormat format = SerializationFormat::JSON) {
    std::unique_ptr<Serializer> serializer;

    if (format == SerializationFormat::JSON) {
        serializer = std::make_unique<JSONSerializer>();
    } else if (format == SerializationFormat::Binary) {
        serializer = std::make_unique<BinarySerializer>();
    }

    serializer->BeginObject("root");
    serializer->WriteVersion(obj.GetVersion());
    obj.Serialize(serializer.get());
    serializer->EndObject();

    return serializer->SaveToFile(filePath);
}

template<typename T>
SerializationResult DeserializeFromFile(T& obj, const std::string& filePath, SerializationFormat format = SerializationFormat::JSON) {
    auto deserializer = Deserializer::LoadFromFile(filePath, format);
    if (!deserializer) {
        return SerializationResult::Fail(SerializationError::FileNotFound, "Failed to load file: " + filePath);
    }

    if (!deserializer->BeginObject("root")) {
        return SerializationResult::Fail(SerializationError::ParseError, "Failed to parse root object");
    }

    const uint32_t version = deserializer->ReadVersion();
    (void)version;
    obj.Deserialize(deserializer.get());
    deserializer->EndObject();

    return SerializationResult::Success();
}

} // namespace Next
