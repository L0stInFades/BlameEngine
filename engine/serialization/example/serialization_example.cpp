// 序列化系统使用示例

#include "next/serialization/serialization.h"
#include "next/foundation/logger.h"
#include <iostream>

using namespace Next;

// 示例 1: 简单的配置结构
struct PlayerConfig : public ISerializable {
    std::string name;
    int level;
    float health;
    bool isAlive;

    // ISerializable 接口实现
    void Serialize(Serializer* serializer) const override {
        SerializationHelper::Serialize(serializer, "name", name);
        SerializationHelper::Serialize(serializer, "level", level);
        SerializationHelper::Serialize(serializer, "health", health);
        SerializationHelper::Serialize(serializer, "isAlive", isAlive);
    }

    void Deserialize(Deserializer* deserializer) override {
        name = SerializationHelper::Deserialize<std::string>(deserializer, "name");
        level = SerializationHelper::Deserialize<int>(deserializer, "level");
        health = SerializationHelper::Deserialize<float>(deserializer, "health");
        isAlive = SerializationHelper::Deserialize<bool>(deserializer, "isAlive");
    }

    uint32_t GetVersion() const override { return 1; }
    const char* GetTypeName() const override { return "PlayerConfig"; }
};

// 示例 2: 包含容器的复杂结构
struct GameSave : public ISerializable {
    struct PlayerData {
        std::string id;
        std::string name;
        int x, y;
        std::vector<std::string> inventory;

        void Serialize(Serializer* serializer) const {
            SerializationHelper::Serialize(serializer, "id", id);
            SerializationHelper::Serialize(serializer, "name", name);
            SerializationHelper::Serialize(serializer, "x", x);
            SerializationHelper::Serialize(serializer, "y", y);
            SerializationHelper::Serialize(serializer, "inventory", inventory);
        }

        void Deserialize(Deserializer* deserializer) {
            id = SerializationHelper::Deserialize<std::string>(deserializer, "id");
            name = SerializationHelper::Deserialize<std::string>(deserializer, "name");
            x = SerializationHelper::Deserialize<int>(deserializer, "x");
            y = SerializationHelper::Deserialize<int>(deserializer, "y");
            // inventory 反序列化需要特殊处理
        }
    };

    std::string saveName;
    double saveTime;
    std::vector<PlayerData> players;
    std::unordered_map<std::string, int> questStates;

    void Serialize(Serializer* serializer) const override {
        SerializationHelper::Serialize(serializer, "saveName", saveName);
        SerializationHelper::Serialize(serializer, "saveTime", saveTime);

        serializer->BeginArray("players", players.size());
        for (const auto& player : players) {
            serializer->BeginObject("");
            player.Serialize(serializer);
            serializer->EndObject();
        }
        serializer->EndArray();

        serializer->BeginObject("questStates");
        for (const auto& [questId, state] : questStates) {
            serializer->WriteInt32(questId, state);
        }
        serializer->EndObject();
    }

    void Deserialize(Deserializer* deserializer) override {
        saveName = SerializationHelper::Deserialize<std::string>(deserializer, "saveName");
        saveTime = SerializationHelper::Deserialize<double>(deserializer, "saveTime");
        // players 和 questStates 需要特殊处理
    }

    uint32_t GetVersion() const override { return 1; }
    const char* GetTypeName() const override { return "GameSave"; }
};

// 示例 3: 手动序列化
void ManualSerializationExample() {
    std::cout << "\n=== 手动序列化示例 ===\n";

    // 创建 JSON 序列化器
    auto serializer = std::make_unique<JSONSerializer>();

    // 手动写入数据
    serializer->BeginObject("gameState");

    serializer->WriteString("gameTitle", "两宋演义");
    serializer->WriteVersion(1);
    serializer->WriteDouble("gameTime", 1234.567);

    serializer->BeginObject("player");
    serializer->WriteString("name", "张三");
    serializer->WriteInt32("level", 10);
    serializer->WriteFloat("health", 95.5f);
    serializer->WriteBool("isAlive", true);
    serializer->EndObject();

    serializer->BeginArray("inventory", 3);
    serializer->WriteString("", "剑");
    serializer->WriteString("", "盾牌");
    serializer->WriteString("", "草药");
    serializer->EndArray();

    serializer->EndObject();

    // 输出 JSON
    std::string json = serializer->ToString();
    std::cout << "生成的 JSON:\n" << json << "\n";

    // 保存到文件
    auto result = serializer->SaveToFile("game_state.json");
    if (result.IsSuccess()) {
        std::cout << "保存成功！\n";
    } else {
        std::cout << "保存失败: " << result.errorMessage << "\n";
    }
}

// 示例 4: 使用 ISerializable 接口
void ISerializableExample() {
    std::cout << "\n=== ISerializable 接口示例 ===\n";

    // 创建玩家配置
    PlayerConfig player;
    player.name = "李四";
    player.level = 15;
    player.health = 88.5f;
    player.isAlive = true;

    // 序列化到 JSON 文件
    auto result = SerializeToFile(player, "player_config.json", SerializationFormat::JSON);
    if (result.IsSuccess()) {
        std::cout << "玩家配置已保存到 player_config.json\n";
    }

    // 序列化到二进制文件
    result = SerializeToFile(player, "player_config.bin", SerializationFormat::Binary);
    if (result.IsSuccess()) {
        std::cout << "玩家配置已保存到 player_config.bin\n";
    }

    // 从 JSON 文件加载
    PlayerConfig loadedPlayer;
    result = DeserializeFromFile(loadedPlayer, "player_config.json", SerializationFormat::JSON);
    if (result.IsSuccess()) {
        std::cout << "玩家配置已加载:\n";
        std::cout << "  姓名: " << loadedPlayer.name << "\n";
        std::cout << "  等级: " << loadedPlayer.level << "\n";
        std::cout << "  生命值: " << loadedPlayer.health << "\n";
        std::cout << "  存活: " << (loadedPlayer.isAlive ? "是" : "否") << "\n";
    }
}

// 示例 5: 二进制序列化
void BinarySerializationExample() {
    std::cout << "\n=== 二进制序列化示例 ===\n";

    auto serializer = std::make_unique<BinarySerializer>();

    serializer->BeginObject("binaryData");
    serializer->WriteVersion(1);
    serializer->WriteInt32("intValue", 42);
    serializer->WriteFloat("floatValue", 3.14f);
    serializer->WriteString("stringValue", "Hello, Binary!");
    serializer->EndObject();

    auto result = serializer->SaveToFile("binary_data.bin");
    if (result.IsSuccess()) {
        std::cout << "二进制数据已保存到 binary_data.bin\n";
    }

    // 从文件加载
    auto deserializer = Deserializer::LoadFromFile("binary_data.bin", SerializationFormat::Binary);
    if (deserializer) {
        if (deserializer->BeginObject("binaryData")) {
            uint32_t version = deserializer->ReadVersion();
            int32_t intValue = deserializer->ReadInt32("intValue");
            float floatValue = deserializer->ReadFloat("floatValue");
            std::string stringValue = deserializer->ReadString("stringValue");

            std::cout << "二进制数据已加载:\n";
            std::cout << "  版本: " << version << "\n";
            std::cout << "  整数: " << intValue << "\n";
            std::cout << "  浮点数: " << floatValue << "\n";
            std::cout << "  字符串: " << stringValue << "\n";

            deserializer->EndObject();
        }
    }
}

// 示例 6: 容器序列化
void ContainerSerializationExample() {
    std::cout << "\n=== 容器序列化示例 ===\n";

    auto serializer = std::make_unique<JSONSerializer>();

    serializer->BeginObject("containerDemo");

    // 数组
    serializer->BeginArray("numbers", 5);
    serializer->WriteInt32("", 10);
    serializer->WriteInt32("", 20);
    serializer->WriteInt32("", 30);
    serializer->WriteInt32("", 40);
    serializer->WriteInt32("", 50);
    serializer->EndArray();

    // 字符串数组
    serializer->BeginArray("names", 3);
    serializer->WriteString("", "Alice");
    serializer->WriteString("", "Bob");
    serializer->WriteString("", "Charlie");
    serializer->EndArray();

    serializer->EndObject();

    std::string json = serializer->ToString();
    std::cout << "容器 JSON:\n" << json << "\n";
}

// 示例 7: 版本控制
void VersionControlExample() {
    std::cout << "\n=== 版本控制示例 ===\n";

    auto serializer = std::make_unique<JSONSerializer>();

    serializer->BeginObject("versionedData");
    serializer->WriteVersion(2);  // 数据版本 2
    serializer->WriteString("newField", "这是新增的字段");
    serializer->WriteInt32("oldField", 100);
    serializer->EndObject();

    auto result = serializer->SaveToFile("versioned_data.json");
    if (result.IsSuccess()) {
        std::cout << "版本化数据已保存\n";
    }

    // 加载时检查版本
    auto deserializer = Deserializer::LoadFromFile("versioned_data.json", SerializationFormat::JSON);
    if (deserializer && deserializer->BeginObject("versionedData")) {
        uint32_t version = deserializer->ReadVersion();
        std::cout << "数据版本: " << version << "\n";

        if (version >= 2) {
            std::string newField = deserializer->ReadString("newField");
            std::cout << "新字段: " << newField << "\n";
        }

        int32_t oldField = deserializer->ReadInt32("oldField");
        std::cout << "旧字段: " << oldField << "\n";

        deserializer->EndObject();
    }
}

// 示例 8: 错误处理
void ErrorHandlingExample() {
    std::cout << "\n=== 错误处理示例 ===\n";

    // 尝试加载不存在的文件
    auto deserializer = Deserializer::LoadFromFile("nonexistent.json", SerializationFormat::JSON);
    if (!deserializer) {
        std::cout << "✓ 正确处理了文件不存在的情况\n";
    }

    // 尝试保存到无效路径
    PlayerConfig player;
    player.name = "Test";
    player.level = 1;

    auto result = SerializeToFile(player, "/invalid/path/player.json", SerializationFormat::JSON);
    if (!result.IsSuccess()) {
        std::cout << "✓ 捕获到保存错误: " << result.errorMessage << "\n";
    }
}

int main() {
    std::cout << "序列化系统使用示例\n";
    std::cout << "====================\n";

    // 初始化日志系统
    Logger::Initialize();
    Logger::SetLevel(LogLevel::Info);

    // 运行所有示例
    ManualSerializationExample();
    ISerializableExample();
    BinarySerializationExample();
    ContainerSerializationExample();
    VersionControlExample();
    ErrorHandlingExample();

    // 关闭日志系统
    Logger::Shutdown();

    std::cout << "\n所有示例运行完成！\n";
    std::cout << "生成的文件:\n";
    std::cout << "  - game_state.json\n";
    std::cout << "  - player_config.json\n";
    std::cout << "  - player_config.bin\n";
    std::cout << "  - binary_data.bin\n";
    std::cout << "  - versioned_data.json\n";

    return 0;
}
