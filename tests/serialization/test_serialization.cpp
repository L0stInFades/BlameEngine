#include "next/serialization/serialization.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Next {
namespace testing {
namespace {

template<typename T>
void AppendValue(std::vector<uint8_t>& bytes, const T& value) {
    const size_t offset = bytes.size();
    bytes.resize(offset + sizeof(T));
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void AppendBytes(std::vector<uint8_t>& bytes, const char* data, size_t size) {
    const size_t offset = bytes.size();
    bytes.resize(offset + size);
    std::memcpy(bytes.data() + offset, data, size);
}

std::filesystem::path TempFilePath(const char* name) {
    std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

void CreateEmptyFile(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.is_open());
}

} // namespace

TEST(BinaryDeserializerTest, DeserializeVectorConsumesArrayLengthBeforeElements) {
    std::vector<uint8_t> bytes;
    AppendValue<uint32_t>(bytes, 7u);
    AppendValue<uint32_t>(bytes, 3u);
    AppendValue<int32_t>(bytes, 10);
    AppendValue<int32_t>(bytes, 20);
    AppendValue<int32_t>(bytes, 30);

    BinaryDeserializer deserializer(bytes);
    EXPECT_EQ(deserializer.ReadVersion(), 7u);

    const std::vector<int32_t> values =
        SerializationHelper::DeserializeVector<int32_t>(&deserializer, "numbers");
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 20);
    EXPECT_EQ(values[2], 30);
}

TEST(BinaryDeserializerTest, FailedStringReadDoesNotPartiallyConsumeLengthField) {
    std::vector<uint8_t> bytes;
    AppendValue<uint32_t>(bytes, 1u);
    AppendValue<uint32_t>(bytes, 8u);
    AppendBytes(bytes, "hi", 2);

    BinaryDeserializer deserializer(bytes);
    EXPECT_EQ(deserializer.ReadString("name", "fallback"), "fallback");
    EXPECT_EQ(deserializer.ReadUInt32("lengthAfterFailure", 0u), 8u);
}

TEST(BinaryDeserializerTest, EmptyStringAtEndOfBufferDoesNotReadPastBuffer) {
    std::vector<uint8_t> bytes;
    AppendValue<uint32_t>(bytes, 1u);
    AppendValue<uint32_t>(bytes, 0u);

    BinaryDeserializer deserializer(bytes);
    EXPECT_EQ(deserializer.ReadString("empty", "fallback"), "");
}

TEST(DeserializerFileTest, EmptyBinaryFileLoadsAsEmptyDeserializer) {
    const std::filesystem::path path = TempFilePath("next_empty_binary_serialization_test.bin");
    CreateEmptyFile(path);

    auto deserializer = Deserializer::LoadFromFile(path.string(), SerializationFormat::Binary);
    ASSERT_NE(deserializer, nullptr);
    EXPECT_EQ(deserializer->ReadVersion(), 0u);
    EXPECT_EQ(deserializer->ReadUInt32("missing", 99u), 99u);

    std::filesystem::remove(path);
}

TEST(DeserializerFileTest, EmptyJsonFileLoadsThenFailsObjectBegin) {
    const std::filesystem::path path = TempFilePath("next_empty_json_serialization_test.json");
    CreateEmptyFile(path);

    auto deserializer = Deserializer::LoadFromFile(path.string(), SerializationFormat::JSON);
    ASSERT_NE(deserializer, nullptr);
    EXPECT_FALSE(deserializer->BeginObject("root"));

    std::filesystem::remove(path);
}

} // namespace testing
} // namespace Next
