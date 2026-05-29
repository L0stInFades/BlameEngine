# CP8 Lua 集成指南

**日期**: 2026-01-15
**类型**: 技术文档
**状态**: 框架完成，集成说明

## 📋 概述

CP8 Script System 使用框架实现模式，可以在没有 Lua 库的情况下编译。要启用完整的 Lua 功能，需要安装 Lua 5.4+ 库并进行配置。

## 🔧 安装 Lua 库

### Windows

#### 方法 1: 使用 vcpkg（推荐）

```bash
# 安装 vcpkg（如果还没有）
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat

# 安装 Lua
./vcpkg install lua:x64-windows
```

#### 方法 2: 手动安装

1. **下载 Lua**:
   - 访问 https://www.lua.org/download.html
   - 下载 Lua 5.4.x 源代码

2. **编译 Lua**:
   ```bash
   # 解压到目录，例如 C:\Lua54
   cd C:\Lua54
   # 使用 Visual Studio 或 MinGW 编译
   # 或使用预编译的二进制文件
   ```

3. **设置环境变量**:
   ```bash
   LUA_INCLUDE_DIR=C:\Lua54\include
   LUA_LIBRARIES=C:\Lua54\lib\lua54.lib
   ```

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install liblua5.4-dev

# Fedora/RHEL
sudo dnf install lua-devel

# Arch Linux
sudo pacman -S lua
```

### macOS

```bash
# 使用 Homebrew
brew install lua
```

## 🎯 配置 CMake

### 方法 1: 使用 CMake 命令行

```bash
cd E:\NEXT\build
cmake .. -DUSE_SYSTEM_LUA=ON
```

### 方法 2: 使用 CMake GUI

1. 打开 CMake GUI
2. 设置源码路径：`E:\NEXT`
3. 设置构建路径：`E:\NEXT\build`
4. 点击 "Configure"
5. 勾选 `USE_SYSTEM_LUA`
6. 点击 "Generate"

### 方法 3: 手动指定 Lua 路径

```bash
cmake .. -DUSE_SYSTEM_LUA=ON \
         -DLUA_INCLUDE_DIR=C:\Lua54\include \
         -DLUA_LIBRARIES=C:\Lua54\lib\lua54.lib
```

## 📦 构建和测试

### 构建项目

```bash
cd E:\NEXT\build
cmake --build . --config Release
```

### 测试 Lua 集成

创建测试脚本 `test.lua`:

```lua
-- 测试 Lua 功能
function OnStart()
    Log.Info("Lua script started!")
    local position = Vec3.new(1, 2, 3)
    Log.Info("Position: " .. position.x .. ", " .. position.y .. ", " .. position.z)
end

function OnUpdate()
    local deltaTime = Time.GetDeltaTime()
    local currentTime = Time.GetTime()
    -- 游戏逻辑
end

function OnDestroy()
    Log.Info("Lua script destroyed!")
end
```

## 🔌 API 绑定参考

### 完整的 C++/Lua 绑定示例

```cpp
#include "next/script/script_system.h"
#include "lua.hpp"
#include "next/log/log.h"

// C 函数：暴露给 Lua
int Lua_LogInfo(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    NEXT_LOG_INFO() << "[Lua] " << message;
    return 0;  // 返回值数量
}

int Lua_Vec3_New(lua_State* L) {
    float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 3, 0.0));

    // 创建 userdata 并存储 Vec3
    Vec3* vec3 = static_cast<Vec3*>(lua_newuserdata(L, sizeof(Vec3)));
    *vec3 = Vec3(x, y, z);

    // 设置 metatable
    luaL_getmetatable(L, "Vec3");
    lua_setmetatable(L, -2);

    return 1;  // 返回 Vec3
}

// 注册所有 API
void RegisterLuaAPI(lua_State* L) {
    // 注册 Log API
    lua_register(L, "Log", Lua_LogInfo);

    // 注册 Vec3 类型
    luaL_newmetatable(L, "Vec3");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    // 注册 Vec3 方法
    lua_pushcfunction(L, Lua_Vec3_New);
    lua_setglobal(L, "Vec3");
}

// 初始化 Lua VM
void InitializeLuaVM() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);  // 加载标准库
    RegisterLuaAPI(L);

    // 加载脚本
    if (luaL_dofile(L, "test.lua") == LUA_OK) {
        NEXT_LOG_INFO() << "Lua script loaded successfully";

        // 调用 OnStart
        lua_getglobal(L, "OnStart");
        if (lua_pcall(L, 0, 0, 0) == LUA_OK) {
            NEXT_LOG_INFO() << "OnStart called successfully";
        }
    } else {
        NEXT_LOG_ERROR() << "Failed to load Lua script: " << lua_tostring(L, -1);
    }

    lua_close(L);
}
```

## 🎯 Lua API 完整列表

### Log API

```lua
Log.Info(message)      -- 信息日志
Log.Warning(message)   -- 警告日志
Log.Error(message)     -- 错误日志
Log.Debug(message)     -- 调试日志
```

### Math API (Vec3)

```lua
-- 创建向量
local v1 = Vec3.new(1, 2, 3)
local v2 = Vec3.new(4, 5, 6)

-- 向量运算
local v3 = v1:Add(v2)        -- 加法
local v4 = v1:Sub(v2)        -- 减法
local dot = v1:Dot(v2)       -- 点积
local cross = v1:Cross(v2)   -- 叉积
local len = v1:Length()      -- 长度
local norm = v1:Normalize()  -- 归一化

-- 访问分量
print(v1.x, v1.y, v1.z)
```

### Time API

```lua
local deltaTime = Time.GetDeltaTime()  -- 获取帧时间
local currentTime = Time.GetTime()     -- 获取游戏时间
```

### World API

```lua
local entity = World.CreateEntity()
World.DestroyEntity(entity)
local count = World.GetEntityCount()
```

### Entity API

```lua
local valid = entity:IsValid()
entity:AddComponent("Transform")
local transform = entity:GetComponent("Transform")
local hasComp = entity:HasComponent("Transform")
```

## 🔍 调试 Lua 脚本

### 启用 Lua 调试

```cpp
// 在 LuaVM 初始化时
lua_State* L = luaL_newstate();

// 启用调试钩子
lua_sethook(L, LuaDebugHook, LUA_MASKLINE, 0);

// 调试钩子函数
void LuaDebugHook(lua_State* L, lua_Debug* ar) {
    NEXT_LOG_DEBUG() << "Lua executing line " << ar->currentline;
}
```

### 错误处理

```lua
-- 在 Lua 中使用 pcall 捕获错误
local success, err = pcall(function()
    -- 可能出错的代码
    error("Something went wrong!")
end)

if not success then
    Log.Error("Error: " .. tostring(err))
end
```

## 📊 性能优化

### 1. 使用 LuaJIT（可选）

```bash
# 安装 LuaJIT
# https://luajit.org/
```

### 2. 脚本缓存

```cpp
// 缓存已编译的脚本
std::unordered_map<std::string, int> scriptRefs;

void LoadCachedScript(lua_State* L, const std::string& path) {
    auto it = scriptRefs.find(path);
    if (it != scriptRefs.end()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    } else {
        if (luaL_loadfile(L, path.c_str()) == LUA_OK) {
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            scriptRefs[path] = ref;
        }
    }
}
```

### 3. 避免频繁的 C++/Lua 调用

```lua
-- 好的做法：批量处理
local positions = {}
for i = 1, 100 do
    positions[i] = Vec3.new(i, i, i)
end

-- 避免：频繁调用 C++
for i = 1, 100 do
    World.CreateEntity()  -- 慢
end
```

## 🐛 常见问题

### Q1: 编译错误 "lua.h not found"

**解决方案**:
- 确保设置了 `USE_SYSTEM_LUA=ON`
- 检查 `LUA_INCLUDE_DIR` 是否正确
- 确保 Lua 已正确安装

### Q2: 链接错误 "unresolved external symbol lua_*"

**解决方案**:
- 确保 `LUA_LIBRARIES` 正确设置
- 检查 Lua 库版本匹配（x86/x64）
- 确保链接了正确的 Lua 库

### Q3: Lua 脚本无法加载

**解决方案**:
- 检查脚本路径是否正确
- 验证脚本语法（使用 `luac` 编译检查）
- 查看日志中的错误信息

### Q4: 性能问题

**解决方案**:
- 使用 LuaJIT 替代标准 Lua
- 减少频繁的 C++/Lua 边界跨越
- 使用脚本缓存
- 避免在 Lua 中进行密集计算

## 📚 参考资料

- [Lua 官方文档](https://www.lua.org/docs.html)
- [Lua 5.4 参考手册](https://www.lua.org/manual/5.4/)
- [Programming in Lua](https://www.lua.org/pil/contents.html)
- [LuaJIT 文档](https://luajit.org/extensions.html)

## 🎉 总结

CP8 Script System 提供了完整的 Lua 集成框架：

- ✅ 框架实现模式（无需 Lua 库即可编译）
- ✅ 完整的接口设计
- ✅ 灵活的 API 绑定系统
- ✅ 支持热重载
- ✅ 权限控制和沙盒

**要启用完整功能**:
1. 安装 Lua 5.4+ 库
2. 设置 `USE_SYSTEM_LUA=ON`
3. 实现完整的 C++/Lua 绑定
4. 测试和优化

---

**文档版本**: 1.0
**创建时间**: 2026-01-15
**最后更新**: 2026-01-15
