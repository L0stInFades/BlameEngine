#include "next/script/script_system.h"
#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/event_bus.h"
#include "next/foundation/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

#ifdef NEXT_WITH_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

namespace Next {

namespace {

class ScriptLogStream {
public:
    explicit ScriptLogStream(LogLevel level)
        : level_(level) {}

    ~ScriptLogStream() {
        Logger::Log(level_, "%s", stream_.str().c_str());
    }

    template <typename T>
    ScriptLogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream stream_;
};

#define SCRIPT_LOG_INFO() ScriptLogStream(::Next::LogLevel::Info)
#define SCRIPT_LOG_WARN() ScriptLogStream(::Next::LogLevel::Warning)
#define SCRIPT_LOG_ERROR() ScriptLogStream(::Next::LogLevel::Error)

#ifdef NEXT_WITH_LUA

lua_State* AsLuaState(void* state) {
    return static_cast<lua_State*>(state);
}

std::string LuaValueToString(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TSTRING:
        return lua_tostring(L, index);
    case LUA_TNUMBER: {
        std::ostringstream stream;
        stream << lua_tonumber(L, index);
        return stream.str();
    }
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index) ? "true" : "false";
    case LUA_TNIL:
        return "nil";
    default:
        return lua_typename(L, lua_type(L, index));
    }
}

std::string BuildLuaLogMessage(lua_State* L) {
    std::ostringstream message;
    const int argc = lua_gettop(L);
    for (int i = 1; i <= argc; ++i) {
        if (i > 1) {
            message << ' ';
        }
        message << LuaValueToString(L, i);
    }
    return message.str();
}

int LuaLogInfo(lua_State* L) {
    SCRIPT_LOG_INFO() << BuildLuaLogMessage(L);
    return 0;
}

int LuaLogWarn(lua_State* L) {
    SCRIPT_LOG_WARN() << BuildLuaLogMessage(L);
    return 0;
}

int LuaLogError(lua_State* L) {
    SCRIPT_LOG_ERROR() << BuildLuaLogMessage(L);
    return 0;
}

void PushParamsTable(lua_State* L, const std::unordered_map<std::string, std::string>& parameters) {
    lua_newtable(L);
    for (const auto& [key, value] : parameters) {
        lua_pushlstring(L, value.data(), value.size());
        lua_setfield(L, -2, key.c_str());
    }
}

void PushGlobalTableCompat(lua_State* L) {
#if LUA_VERSION_NUM >= 502
    lua_pushglobaltable(L);
#else
    lua_pushvalue(L, LUA_GLOBALSINDEX);
#endif
}

bool AssignEnvironmentToChunk(lua_State* L, int chunkIndex, int envIndex) {
#if LUA_VERSION_NUM >= 502
    lua_pushvalue(L, envIndex);
    return lua_setupvalue(L, chunkIndex, 1) != nullptr;
#else
    lua_pushvalue(L, envIndex);
    lua_setfenv(L, chunkIndex);
    return true;
#endif
}

std::string PopLuaError(lua_State* L) {
    const char* message = lua_tostring(L, -1);
    std::string error = message ? message : "unknown Lua error";
    lua_pop(L, 1);
    return error;
}

#endif

} // namespace

// ============================================================================
// LuaVM实现（stub 版本）
// ============================================================================

LuaVM::LuaVM(const LuaVMConfig& config)
    : L_(nullptr)
    , config_(config)
{
    std::memset(&stats_, 0, sizeof(stats_));
}

LuaVM::~LuaVM() {
    Shutdown();
}

bool LuaVM::Initialize() {
    if (L_) {
        SCRIPT_LOG_WARN() << "LuaVM::Initialize: VM already initialized";
        return true;
    }

#ifdef NEXT_WITH_LUA
    lua_State* state = luaL_newstate();
    if (!state) {
        SCRIPT_LOG_ERROR() << "LuaVM::Initialize: Failed to create Lua state";
        return false;
    }

    luaL_openlibs(state);
    L_ = state;
    LuaBindings::Initialize(L_);
    UpdateSharedBindings();

    SCRIPT_LOG_INFO() << "LuaVM::Initialize: Lua VM initialized (system Lua)";
    return true;
#else
    SCRIPT_LOG_INFO() << "LuaVM::Initialize: Lua VM initialized (stub mode)";
    SCRIPT_LOG_INFO() << "  To enable full Lua support, set USE_SYSTEM_LUA=ON in CMake";
    return true;
#endif
}

void LuaVM::Shutdown() {
    for (auto& [id, script] : scripts_) {
        (void)id;
        ReleaseScriptEnvironment(script);
    }
    scripts_.clear();

#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_)) {
        lua_close(state);
    }
#endif

    L_ = nullptr;
    currentDeltaTime_ = 0.0f;
    std::memset(&stats_, 0, sizeof(stats_));

    SCRIPT_LOG_INFO() << "LuaVM::Shutdown: Lua VM shut down";
}

LuaVM::ScriptID LuaVM::LoadScript(
    const std::string& scriptPath,
    const std::unordered_map<std::string, std::string>* parameters) {
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        SCRIPT_LOG_ERROR() << "LuaVM::LoadScript: Failed to open script: " << scriptPath;
        return 0;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return LoadScriptFromString(scriptPath, buffer.str(), parameters);
}

LuaVM::ScriptID LuaVM::LoadScriptFromString(
    const std::string& scriptName,
    const std::string& scriptContent,
    const std::unordered_map<std::string, std::string>* parameters) {
    auto startTime = std::chrono::high_resolution_clock::now();

    ScriptInfo info;
    info.id = nextScriptID_++;
    info.name = scriptName;
    info.path = scriptName;
    info.content = scriptContent;
    if (parameters) {
        info.parameters = *parameters;
    }
    if (!PrepareScriptEnvironment(info)) {
        SCRIPT_LOG_ERROR() << "LuaVM::LoadScriptFromString: Failed to prepare script '" << scriptName << "'";
        return 0;
    }
    info.isValid = true;
    scripts_[info.id] = info;

    auto endTime = std::chrono::high_resolution_clock::now();
    const double loadTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    SCRIPT_LOG_INFO() << "LuaVM::LoadScriptFromString: Loaded script '" << scriptName
                    << "' (ID: " << info.id << ", Time: " << loadTimeMs << " ms)";

    stats_.scriptCount = scripts_.size();
    stats_.activeScripts = static_cast<uint32_t>(scripts_.size());
    stats_.averageExecutionTimeMs = static_cast<float>(loadTimeMs);

    return info.id;
}

void LuaVM::UnloadScript(ScriptID scriptID) {
    auto it = scripts_.find(scriptID);
    if (it == scripts_.end()) {
        return;
    }

    ReleaseScriptEnvironment(it->second);
    it->second.isValid = false;
    scripts_.erase(it);

    stats_.scriptCount = scripts_.size();
    stats_.activeScripts = static_cast<uint32_t>(scripts_.size());

    SCRIPT_LOG_INFO() << "LuaVM::UnloadScript: Unloaded script ID " << scriptID;
}

bool LuaVM::ReloadScript(ScriptID scriptID) {
    auto it = scripts_.find(scriptID);
    if (it == scripts_.end()) {
        return false;
    }

    ScriptInfo reloaded = it->second;
    if (!reloaded.path.empty()) {
        std::ifstream file(reloaded.path);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            reloaded.content = buffer.str();
        }
    }

    ReleaseScriptEnvironment(reloaded);
    if (!PrepareScriptEnvironment(reloaded)) {
        SCRIPT_LOG_ERROR() << "LuaVM::ReloadScript: Failed to reload script '" << reloaded.name << "'";
        return false;
    }

    reloaded.hasRuntimeError = false;
    reloaded.lastError.clear();
    reloaded.isValid = true;
    it->second = std::move(reloaded);

    SCRIPT_LOG_INFO() << "LuaVM::ReloadScript: Reloaded script '" << it->second.name << "'";
    return true;
}

bool LuaVM::CallScriptFunction(ScriptID scriptID, const std::string& functionName) {
    auto it = scripts_.find(scriptID);
    if (it == scripts_.end() || !it->second.isValid || it->second.hasRuntimeError) {
        return false;
    }

#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_)) {
        const int stackTop = lua_gettop(state);

        if (it->second.environmentRef < 0) {
            it->second.lastError = "script environment is not available";
            it->second.hasRuntimeError = true;
            return false;
        }

        lua_rawgeti(state, LUA_REGISTRYINDEX, it->second.environmentRef);
        if (!lua_istable(state, -1)) {
            it->second.lastError = "script environment is not available";
            it->second.hasRuntimeError = true;
            lua_settop(state, stackTop);
            return false;
        }

        lua_getfield(state, -1, functionName.c_str());
        if (lua_isnil(state, -1)) {
            lua_settop(state, stackTop);
            return true;
        }
        if (!lua_isfunction(state, -1)) {
            it->second.lastError = "callback '" + functionName + "' is not a function";
            it->second.hasRuntimeError = true;
            lua_settop(state, stackTop);
            SCRIPT_LOG_ERROR() << "LuaVM::CallScriptFunction: " << it->second.lastError
                             << " for script '" << it->second.name << "'";
            return false;
        }

        const auto startTime = std::chrono::high_resolution_clock::now();
        if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
            it->second.lastError = PopLuaError(state);
            it->second.hasRuntimeError = true;
            it->second.isValid = false;
            lua_settop(state, stackTop);
            SCRIPT_LOG_ERROR() << "LuaVM::CallScriptFunction: Script '" << it->second.name
                             << "' callback '" << functionName << "' failed: " << it->second.lastError;
            return false;
        }

        const auto endTime = std::chrono::high_resolution_clock::now();
        stats_.averageExecutionTimeMs =
            std::chrono::duration<double, std::milli>(endTime - startTime).count();
        lua_settop(state, stackTop);
        return true;
    }
#endif

    (void)functionName;
    stats_.averageExecutionTimeMs = 0.1f;
    return true;
}

void LuaVM::RegisterCFunction(const std::string& luaName, void* function) {
    (void)function;
    SCRIPT_LOG_WARN() << "LuaVM::RegisterCFunction: Direct void* binding is not implemented for '" << luaName << "'";
}

bool LuaVM::ExecuteString(const std::string& code) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_)) {
        if (luaL_dostring(state, code.c_str()) != LUA_OK) {
            SCRIPT_LOG_ERROR() << "LuaVM::ExecuteString: " << PopLuaError(state);
            return false;
        }
        return true;
    }
#endif

    SCRIPT_LOG_INFO() << "LuaVM::ExecuteString: Executing code snippet (" << code.length()
                    << " chars, stub mode)";
    return true;
}

bool LuaVM::ExecuteFile(const std::string& filePath) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_)) {
        if (luaL_dofile(state, filePath.c_str()) != LUA_OK) {
            SCRIPT_LOG_ERROR() << "LuaVM::ExecuteFile: " << PopLuaError(state);
            return false;
        }
        return true;
    }
#endif

    return LoadScript(filePath) != 0;
}

void LuaVM::Update(float deltaTime) {
    currentDeltaTime_ = deltaTime;
    UpdateSharedBindings();
}

void LuaVM::ResetStats() {
    std::memset(&stats_, 0, sizeof(stats_));
    stats_.scriptCount = scripts_.size();
    stats_.activeScripts = static_cast<uint32_t>(scripts_.size());
}

void LuaVM::UpdateSharedBindings() {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_)) {
        lua_getglobal(state, "Time");
        if (lua_istable(state, -1)) {
            lua_pushnumber(state, currentDeltaTime_);
            lua_setfield(state, -2, "DeltaTime");
        }
        lua_pop(state, 1);
    }
#endif
}

bool LuaVM::PrepareScriptEnvironment(ScriptInfo& script) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_)) {
        const int stackTop = lua_gettop(state);

        if (luaL_loadbuffer(state, script.content.data(), script.content.size(), script.name.c_str()) != LUA_OK) {
            script.lastError = PopLuaError(state);
            SCRIPT_LOG_ERROR() << "LuaVM::PrepareScriptEnvironment: Failed to compile script '"
                             << script.name << "': " << script.lastError;
            lua_settop(state, stackTop);
            return false;
        }

        const int chunkIndex = lua_gettop(state);
        lua_newtable(state);
        const int envIndex = lua_gettop(state);

        PushParamsTable(state, script.parameters);
        lua_setfield(state, envIndex, "params");

        lua_pushvalue(state, envIndex);
        lua_setfield(state, envIndex, "_G");

        lua_newtable(state);
        PushGlobalTableCompat(state);
        lua_setfield(state, -2, "__index");
        lua_setmetatable(state, envIndex);

        if (!AssignEnvironmentToChunk(state, chunkIndex, envIndex)) {
            script.lastError = "failed to bind script environment";
            lua_settop(state, stackTop);
            SCRIPT_LOG_ERROR() << "LuaVM::PrepareScriptEnvironment: " << script.lastError
                             << " for '" << script.name << "'";
            return false;
        }

        if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
            script.lastError = PopLuaError(state);
            lua_settop(state, stackTop);
            SCRIPT_LOG_ERROR() << "LuaVM::PrepareScriptEnvironment: Script '" << script.name
                             << "' failed during load: " << script.lastError;
            return false;
        }

        lua_pushvalue(state, envIndex);
        script.environmentRef = luaL_ref(state, LUA_REGISTRYINDEX);
        script.lastError.clear();
        script.hasRuntimeError = false;
        lua_settop(state, stackTop);
        return true;
    }
#endif

    script.environmentRef = -1;
    script.lastError.clear();
    script.hasRuntimeError = false;
    return true;
}

void LuaVM::ReleaseScriptEnvironment(ScriptInfo& script) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L_); state && script.environmentRef >= 0) {
        luaL_unref(state, LUA_REGISTRYINDEX, script.environmentRef);
    }
#endif

    script.environmentRef = -1;
}

// ============================================================================
// LuaBindings实现
// ============================================================================

std::function<bool(LuaAPIPermission)> LuaBindings::permissionCallback_ =
    [](LuaAPIPermission perm) {
        return perm == LuaAPIPermission::Safe || perm == LuaAPIPermission::Restricted;
    };

void LuaBindings::Initialize(void* L) {
    RegisterAllBindings(L);
}

void LuaBindings::RegisterAllBindings(void* L) {
    RegisterMathAPI(L);
    RegisterLogAPI(L);
    RegisterTimeAPI(L);
    SCRIPT_LOG_INFO() << "LuaBindings::RegisterAllBindings: Registered Log/Time/Math APIs";
}

void LuaBindings::RegisterWorldAPI(void* L, World* world) {
    (void)L;
    (void)world;
    SCRIPT_LOG_INFO() << "LuaBindings::RegisterWorldAPI: World binding surface reserved";
}

void LuaBindings::RegisterEntityAPI(void* L) {
    (void)L;
    SCRIPT_LOG_INFO() << "LuaBindings::RegisterEntityAPI: Entity binding surface reserved";
}

void LuaBindings::RegisterEventAPI(void* L, EventBus* eventBus) {
    (void)L;
    (void)eventBus;
    SCRIPT_LOG_INFO() << "LuaBindings::RegisterEventAPI: Event binding surface reserved";
}

void LuaBindings::RegisterMathAPI(void* L) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L)) {
        lua_getglobal(state, "math");
        if (lua_istable(state, -1)) {
            lua_setglobal(state, "Math");
            return;
        }
        lua_pop(state, 1);
    }
#else
    (void)L;
#endif
}

void LuaBindings::RegisterLogAPI(void* L) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L)) {
        lua_newtable(state);
        lua_pushcfunction(state, LuaLogInfo);
        lua_setfield(state, -2, "Info");
        lua_pushcfunction(state, LuaLogWarn);
        lua_setfield(state, -2, "Warn");
        lua_pushcfunction(state, LuaLogError);
        lua_setfield(state, -2, "Error");
        lua_setglobal(state, "Log");
    }
#else
    (void)L;
#endif
}

void LuaBindings::RegisterTimeAPI(void* L) {
#ifdef NEXT_WITH_LUA
    if (lua_State* state = AsLuaState(L)) {
        lua_newtable(state);
        lua_pushnumber(state, 0.0);
        lua_setfield(state, -2, "DeltaTime");
        lua_setglobal(state, "Time");
    }
#else
    (void)L;
#endif
}

void LuaBindings::SetPermissionCheckCallback(std::function<bool(LuaAPIPermission)> callback) {
    permissionCallback_ = std::move(callback);
}

bool LuaBindings::CheckPermission(LuaAPIPermission permission) {
    return permissionCallback_ ? permissionCallback_(permission) : false;
}

// ============================================================================
// ScriptComponent实现
// ============================================================================

bool ScriptComponent::CallFunction(const std::string& functionName, LuaVM* vm) {
    if (!vm || !isEnabled) {
        return false;
    }

    return vm->CallScriptFunction(scriptID, functionName);
}

void ScriptComponent::SetParameter(const std::string& key, const std::string& value) {
    config.parameters[key] = value;
}

std::string ScriptComponent::GetParameter(const std::string& key) const {
    auto it = config.parameters.find(key);
    return it != config.parameters.end() ? it->second : "";
}

// ============================================================================
// ScriptSystem实现
// ============================================================================

ScriptSystem::ScriptSystem(World* world, LuaVM* vm)
    : world_(world)
    , vm_(vm)
{
    std::memset(&stats_, 0, sizeof(stats_));
}

ScriptSystem::~ScriptSystem() {
    // 清理所有脚本组件
    for (auto& [entityID, scriptMap] : entityScripts_) {
        for (auto& [scriptPath, component] : scriptMap) {
            UnloadScript(&component);
        }
    }
}

void ScriptSystem::Initialize() {
    if (!world_ || !vm_) {
        SCRIPT_LOG_ERROR() << "ScriptSystem::Initialize: World or Lua VM not set";
        return;
    }

    vm_->Update(0.0f);
    for (auto& scriptRef : allScripts_) {
        if (scriptRef.component && scriptRef.component->isEnabled &&
            scriptRef.component->config.autoStart && !scriptRef.component->isStarted) {
            StartScript(scriptRef.component);
        }
    }

    SCRIPT_LOG_INFO() << "ScriptSystem::Initialize: Script system initialized";
}

void ScriptSystem::Update(float deltaTime) {
    auto startTime = std::chrono::high_resolution_clock::now();
    if (vm_) {
        vm_->Update(deltaTime);
    }

    for (auto& scriptRef : allScripts_) {
        ScriptComponent* script = scriptRef.component;
        if (!script || !script->isEnabled || script->scriptID == 0) {
            continue;
        }

        if (script->config.autoStart && !script->isStarted) {
            StartScript(script);
            if (!script->isEnabled || !script->isStarted) {
                continue;
            }
        }

        if (script->config.updateInterval > 0.0f) {
            script->timeSinceLastUpdate += deltaTime;
            if (script->timeSinceLastUpdate < script->config.updateInterval) {
                continue;
            }
            script->timeSinceLastUpdate = 0.0f;
        }

        UpdateScript(script, deltaTime);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double updateTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    stats_.totalUpdateTimeMs += updateTimeMs;
    if (stats_.totalCalls > 0) {
        stats_.averageUpdateTimeMs = stats_.totalUpdateTimeMs / stats_.totalCalls;
    }
}

ScriptComponent* ScriptSystem::AddScriptComponent(Entity entity, const std::string& scriptPath,
                                                 const ScriptComponentConfig* config) {
    if (!world_ || !vm_) {
        return nullptr;
    }

    uint64_t entityID = static_cast<uint64_t>(entity);

    // 检查是否已存在
    auto entityIt = entityScripts_.find(entityID);
    if (entityIt != entityScripts_.end() && entityIt->second.find(scriptPath) != entityIt->second.end()) {
        SCRIPT_LOG_WARN() << "ScriptSystem::AddScriptComponent: Script '" << scriptPath
                        << "' already exists on entity";
        return &entityIt->second[scriptPath];
    }

    ScriptComponent component(entity, scriptPath);
    if (config) {
        component.config = *config;
    }
    component.isEnabled = component.config.enabled;

    if (!LoadScript(&component)) {
        SCRIPT_LOG_ERROR() << "ScriptSystem::AddScriptComponent: Failed to load script '" << scriptPath << "'";
        return nullptr;
    }

    entityScripts_[entityID][scriptPath] = component;
    ScriptComponent* componentPtr = &entityScripts_[entityID][scriptPath];

    allScripts_.push_back({entityID, componentPtr});

    SCRIPT_LOG_INFO() << "ScriptSystem::AddScriptComponent: Added script '" << scriptPath
                    << "' to entity " << entityID;

    return componentPtr;
}

void ScriptSystem::RemoveScriptComponent(Entity entity, const std::string& scriptPath) {
    uint64_t entityID = static_cast<uint64_t>(entity);

    auto entityIt = entityScripts_.find(entityID);
    if (entityIt == entityScripts_.end()) {
        return;
    }

    auto scriptIt = entityIt->second.find(scriptPath);
    if (scriptIt == entityIt->second.end()) {
        return;
    }

    UnloadScript(&scriptIt->second);

    allScripts_.erase(
        std::remove_if(allScripts_.begin(), allScripts_.end(),
            [&](const ScriptRef& ref) {
                return ref.entity == entityID && ref.component &&
                       ref.component->scriptPath == scriptPath;
            }),
        allScripts_.end()
    );

    entityIt->second.erase(scriptIt);
    if (entityIt->second.empty()) {
        entityScripts_.erase(entityIt);
    }

    SCRIPT_LOG_INFO() << "ScriptSystem::RemoveScriptComponent: Removed script '" << scriptPath
                    << "' from entity " << entityID;
}

ScriptComponent* ScriptSystem::GetScriptComponent(Entity entity, const std::string& scriptPath) {
    uint64_t entityID = static_cast<uint64_t>(entity);

    auto entityIt = entityScripts_.find(entityID);
    if (entityIt == entityScripts_.end()) {
        return nullptr;
    }

    auto scriptIt = entityIt->second.find(scriptPath);
    if (scriptIt == entityIt->second.end()) {
        return nullptr;
    }

    return &scriptIt->second;
}

void ScriptSystem::EnableScript(Entity entity, const std::string& scriptPath) {
    ScriptComponent* script = GetScriptComponent(entity, scriptPath);
    if (script) {
        script->isEnabled = true;
        CallScriptCallback(script, "OnEnable");
    }
}

void ScriptSystem::DisableScript(Entity entity, const std::string& scriptPath) {
    ScriptComponent* script = GetScriptComponent(entity, scriptPath);
    if (script) {
        script->isEnabled = false;
        CallScriptCallback(script, "OnDisable");
    }
}

ScriptSystem::SystemStats ScriptSystem::GetStats() const {
    SystemStats snapshot = stats_;
    snapshot.totalScripts = allScripts_.size();
    snapshot.activeScripts = static_cast<size_t>(std::count_if(
        allScripts_.begin(), allScripts_.end(),
        [](const ScriptRef& ref) {
            return ref.component && ref.component->isEnabled && ref.component->scriptID != 0;
        }));
    if (snapshot.totalCalls > 0) {
        snapshot.averageUpdateTimeMs = snapshot.totalUpdateTimeMs / static_cast<double>(snapshot.totalCalls);
    }
    return snapshot;
}

void ScriptSystem::ResetStats() {
    stats_.totalScripts = 0;
    stats_.activeScripts = 0;
    stats_.totalCalls = 0;
    stats_.totalUpdateTimeMs = 0.0;
    stats_.averageUpdateTimeMs = 0.0;
}

void ScriptSystem::StartScript(ScriptComponent* script) {
    if (!script || script->isStarted || !script->isEnabled) {
        return;
    }

    if (CallScriptCallback(script, "OnStart")) {
        script->isStarted = true;
    }
}

void ScriptSystem::UpdateScript(ScriptComponent* script, float deltaTime) {
    (void)deltaTime;
    if (!script || !script->isEnabled) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    if (!CallScriptCallback(script, "OnUpdate")) {
        return;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double updateTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    script->stats.updateCount++;
    script->stats.totalUpdateTimeMs += updateTimeMs;
    script->stats.averageUpdateTimeMs =
        script->stats.totalUpdateTimeMs / script->stats.updateCount;

    stats_.totalCalls++;
}

bool ScriptSystem::CallScriptCallback(ScriptComponent* script, const std::string& callbackName) {
    if (!vm_ || !script || script->scriptID == 0) {
        return false;
    }

    if (vm_->CallScriptFunction(script->scriptID, callbackName)) {
        return true;
    }

    if (callbackName != "OnDestroy") {
        script->isEnabled = false;
    }

    SCRIPT_LOG_ERROR() << "ScriptSystem::CallScriptCallback: Disabled script '" << script->scriptPath
                     << "' after callback '" << callbackName << "' failed";
    return false;
}

bool ScriptSystem::LoadScript(ScriptComponent* script) {
    if (!vm_ || !script) {
        return false;
    }

    LuaVM::ScriptID id = vm_->LoadScript(script->scriptPath, &script->config.parameters);
    if (id == 0) {
        return false;
    }

    script->scriptID = id;
    script->isStarted = false;
    script->timeSinceLastUpdate = 0.0f;

    return true;
}

void ScriptSystem::UnloadScript(ScriptComponent* script) {
    if (!vm_ || !script || script->scriptID == 0) {
        return;
    }

    CallScriptCallback(script, "OnDestroy");
    vm_->UnloadScript(script->scriptID);
    script->scriptID = 0;
    script->isStarted = false;
}

// ============================================================================
// ScriptSystemManager实现
// ============================================================================

ScriptSystemManager& ScriptSystemManager::GetInstance() {
    static ScriptSystemManager instance;
    return instance;
}

ScriptSystemManager::~ScriptSystemManager() {
    DestroyAll();
}

LuaVM* ScriptSystemManager::CreateVM(const std::string& name, const LuaVMConfig& config) {
    auto vm = std::make_unique<LuaVM>(config);
    if (!vm->Initialize()) {
        return nullptr;
    }

    LuaVM* ptr = vm.get();
    vms_[name] = std::move(vm);

    if (!defaultVM_) {
        defaultVM_ = ptr;
    }

    return ptr;
}

void ScriptSystemManager::DestroyVM(const std::string& name) {
    auto it = vms_.find(name);
    if (it != vms_.end()) {
        if (it->second.get() == defaultVM_) {
            defaultVM_ = nullptr;
        }
        vms_.erase(it);
    }
}

LuaVM* ScriptSystemManager::GetVM(const std::string& name) {
    auto it = vms_.find(name);
    return it != vms_.end() ? it->second.get() : nullptr;
}

LuaVM* ScriptSystemManager::GetDefaultVM() {
    return defaultVM_;
}

void ScriptSystemManager::DestroyAll() {
    vms_.clear();
    defaultVM_ = nullptr;
}

} // namespace Next

#undef SCRIPT_LOG_INFO
#undef SCRIPT_LOG_WARN
#undef SCRIPT_LOG_ERROR
