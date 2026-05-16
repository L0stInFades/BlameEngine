#include "next/task/task_system.h"
#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/event_bus.h"
#include "next/foundation/logger.h"
#include "next/serialization/serialization.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace Next {

// 使用统一日志系统

namespace {

class TaskLogStream {
public:
    explicit TaskLogStream(LogLevel level)
        : level_(level) {}

    ~TaskLogStream() {
        Logger::Log(level_, "%s", stream_.str().c_str());
    }

    template <typename T>
    TaskLogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream stream_;
};

#define TASK_LOG_INFO() TaskLogStream(::Next::LogLevel::Info)
#define TASK_LOG_WARN() TaskLogStream(::Next::LogLevel::Warning)
#define TASK_LOG_ERROR() TaskLogStream(::Next::LogLevel::Error)

void WarnUnsupportedConditionOnce(const char* label) {
    static std::unordered_map<std::string, bool> warned;
    if (warned[label]) {
        return;
    }

    warned[label] = true;
    TASK_LOG_WARN() << "Condition evaluation for '" << label
                    << "' is not backed by runtime data yet; returning false conservatively";
}

} // namespace

// ============================================================================
// 条件系统实现
// ============================================================================

bool Condition::Evaluate(const World* world) const {
    switch (type) {
        case ConditionType::None:
            return true;

        case ConditionType::Time: {
            WarnUnsupportedConditionOnce("Time");
            return false;
        }

        case ConditionType::Location: {
            WarnUnsupportedConditionOnce("Location");
            return false;
        }

        case ConditionType::Composite: {
            // 复合条件：AND/OR/NOT
            switch (logicOp) {
                case And:
                    for (const auto& cond : subConditions) {
                        if (!cond.Evaluate(world)) return false;
                    }
                    return !subConditions.empty();
                case Or:
                    for (const auto& cond : subConditions) {
                        if (cond.Evaluate(world)) return true;
                    }
                    return false;
                case Not:
                    return !subConditions.empty() && !subConditions[0].Evaluate(world);
            }
            return false;
        }

        case ConditionType::Custom:
            WarnUnsupportedConditionOnce("Custom");
            return false;

        case ConditionType::Relationship:
            WarnUnsupportedConditionOnce("Relationship");
            return false;

        case ConditionType::Item:
            WarnUnsupportedConditionOnce("Item");
            return false;

        case ConditionType::TownState:
            WarnUnsupportedConditionOnce("TownState");
            return false;

        case ConditionType::WorldEvent:
            WarnUnsupportedConditionOnce("WorldEvent");
            return false;

        default:
            return false;
    }
}

bool ConditionSet::Evaluate(const World* world) const {
    if (conditions.empty()) return true;

    switch (logicOp) {
        case All:
            for (const auto& cond : conditions) {
                if (!cond.Evaluate(world)) return false;
            }
            return true;

        case Any:
            for (const auto& cond : conditions) {
                if (cond.Evaluate(world)) return true;
            }
            return false;

        case One:
            int count = 0;
            for (const auto& cond : conditions) {
                if (cond.Evaluate(world)) count++;
                if (count > 1) return false;
            }
            return count == 1;
    }

    return false;
}

// ============================================================================
// 动作系统实现
// ============================================================================

bool Action::Execute(const World* world) const {
    TASK_LOG_INFO() << "Action::Execute: Executing action type " << static_cast<int>(type);

    switch (type) {
        case ActionType::None:
            return true;

        case ActionType::TriggerDialogue: {
            const char* npcId = params.strings.count("npcId") ? params.strings.at("npcId").c_str() : "unknown";
            TASK_LOG_INFO() << "Triggering dialogue: " << npcId;
            return true;
        }

        case ActionType::SpawnNPC: {
            const char* npcId = params.strings.count("npcId") ? params.strings.at("npcId").c_str() : "unknown";
            TASK_LOG_INFO() << "Spawning NPC: " << npcId;
            return true;
        }

        case ActionType::GiveItem: {
            int64_t count = params.ints.count("count") ? params.ints.at("count") : 1;
            const char* itemId = params.strings.count("itemId") ? params.strings.at("itemId").c_str() : "unknown";
            TASK_LOG_INFO() << "Giving item: " << itemId << " x " << count;
            return true;
        }

        case ActionType::SetWorldState: {
            const char* key = params.strings.count("key") ? params.strings.at("key").c_str() : "unknown";
            const char* value = params.strings.count("value") ? params.strings.at("value").c_str() : "unknown";
            TASK_LOG_INFO() << "Setting world state: " << key << " = " << value;
            return true;
        }

        case ActionType::Teleport: {
            float x = params.floats.count("x") ? params.floats.at("x") : 0.0f;
            float y = params.floats.count("y") ? params.floats.at("y") : 0.0f;
            float z = params.floats.count("z") ? params.floats.at("z") : 0.0f;
            TASK_LOG_INFO() << "Teleporting to: (" << x << ", " << y << ", " << z << ")";
            return true;
        }

        case ActionType::EnableTask: {
            const char* taskId = params.strings.count("taskId") ? params.strings.at("taskId").c_str() : "unknown";
            TASK_LOG_INFO() << "Enabling task: " << taskId;
            return true;
        }

        case ActionType::CompleteTask: {
            const char* taskId = params.strings.count("taskId") ? params.strings.at("taskId").c_str() : "unknown";
            TASK_LOG_INFO() << "Completing task: " << taskId;
            return true;
        }

        case ActionType::FailTask: {
            const char* taskId = params.strings.count("taskId") ? params.strings.at("taskId").c_str() : "unknown";
            TASK_LOG_INFO() << "Failing task: " << taskId;
            return true;
        }

        case ActionType::ShowNotification: {
            const char* message = params.strings.count("message") ? params.strings.at("message").c_str() : "unknown";
            TASK_LOG_INFO() << "Showing notification: " << message;
            return true;
        }

        case ActionType::PlayCinematic: {
            const char* cinematicId = params.strings.count("cinematicId") ? params.strings.at("cinematicId").c_str() : "unknown";
            TASK_LOG_INFO() << "Playing cinematic: " << cinematicId;
            return true;
        }

        case ActionType::Delay:
            // 延迟动作：由调度器处理
            return true;

        case ActionType::Custom:
            if (customExecutor) {
                return customExecutor(world, params);
            }
            TASK_LOG_WARN() << "Custom action has no executor";
            return false;

        default:
            return true;
    }
}

// ============================================================================
// 任务步骤实现
// ============================================================================

bool TaskStep::CanStart(const World* world) const {
    return prerequisites.Evaluate(world);
}

bool TaskStep::IsComplete(const World* world) const {
    return completionCriteria.Evaluate(world);
}

bool TaskStep::HasFailed(const World* world) const {
    return !failureConditions.IsEmpty() && failureConditions.Evaluate(world);
}

// ============================================================================
// 任务目标实现
// ============================================================================

bool TaskGoal::IsAchieved(const World* world) const {
    if (achieved) return true;
    return achievementConditions.Evaluate(world);
}

// ============================================================================
// 任务定义实现
// ============================================================================

const TaskStep* TaskDefinition::GetStep(const std::string& stepId) const {
    auto it = std::find_if(steps.begin(), steps.end(),
        [&stepId](const TaskStep& step) { return step.id == stepId; });
    return it != steps.end() ? &(*it) : nullptr;
}

const TaskGoal* TaskDefinition::GetGoal(const std::string& goalId) const {
    auto it = std::find_if(goals.begin(), goals.end(),
        [&goalId](const TaskGoal& goal) { return goal.id == goalId; });
    return it != goals.end() ? &(*it) : nullptr;
}

// ============================================================================
// 任务实例实现
// ============================================================================

bool TaskVariables::Has(const std::string& key) const {
    return ints.count(key) || floats.count(key) ||
           strings.count(key) || bools.count(key);
}

bool TaskInstance::HasCompletedStep(const std::string& stepId) const {
    auto it = stepStatuses.find(stepId);
    return it != stepStatuses.end() && it->second == StepStatus::Completed;
}

bool TaskInstance::HasFailedStep(const std::string& stepId) const {
    auto it = stepStatuses.find(stepId);
    return it != stepStatuses.end() && it->second == StepStatus::Failed;
}

void TaskInstance::SetStepStatus(const std::string& stepId, StepStatus status) {
    stepStatuses[stepId] = status;
    if (status == StepStatus::Completed) stepsCompleted++;
    if (status == StepStatus::Failed) stepsFailed++;
}

StepStatus TaskInstance::GetStepStatus(const std::string& stepId) const {
    auto it = stepStatuses.find(stepId);
    return it != stepStatuses.end() ? it->second : StepStatus::Pending;
}

bool TaskInstance::IsGoalAchieved(const std::string& goalId) const {
    auto it = goalAchieved.find(goalId);
    return it != goalAchieved.end() && it->second;
}

void TaskInstance::SetGoalAchieved(const std::string& goalId, bool achieved) {
    goalAchieved[goalId] = achieved;
}

// ============================================================================
// 任务执行器实现
// ============================================================================

TaskExecutor::TaskExecutor(World* world, EventBus* eventBus, const TaskExecutorConfig& config)
    : world_(world)
    , config_(config)
{
    (void)eventBus;
    std::memset(&stats_, 0, sizeof(stats_));
}

TaskExecutor::~TaskExecutor() {
    Shutdown();
}

void TaskExecutor::Initialize() {
    if (initialized_) {
        return;
    }

    initialized_ = true;
    TASK_LOG_INFO() << "TaskExecutor::Initialize: Task executor initialized";
}

void TaskExecutor::Shutdown() {
    if (!initialized_) {
        return;
    }

    FlushAutoSaveIfNeeded(true);

    taskInstances_.clear();
    initialized_ = false;
    TASK_LOG_INFO() << "TaskExecutor::Shutdown: Task executor shut down";
}

void TaskExecutor::Update(float deltaTime) {
    accumulatedTime_ += deltaTime;

    if (accumulatedTime_ < config_.updateInterval) {
        return;
    }

    accumulatedTime_ = 0.0f;

    // 更新所有活动任务
    for (auto& [instanceId, instance] : taskInstances_) {
        if (instance->status == TaskStatus::InProgress) {
            UpdateTask(instance.get(), deltaTime);
        }
    }

    FlushAutoSaveIfNeeded();
}

void TaskExecutor::UpdateTask(TaskInstance* instance, float deltaTime) {
    if (!instance || !instance->definition) return;

    // 更新当前步骤
    UpdateCurrentStep(instance);

    // 检查任务完成
    CheckTaskCompletion(instance);

    // 检查任务失败
    CheckTaskFailure(instance);
}

void TaskExecutor::UpdateCurrentStep(TaskInstance* instance) {
    if (instance->currentStepId.empty()) {
        // 任务刚开始，设置第一步
        if (!instance->definition->firstStepId.empty()) {
            instance->currentStepId = instance->definition->firstStepId;
            const TaskStep* step = instance->definition->GetStep(instance->currentStepId);
            if (step && step->CanStart(world_)) {
                instance->SetStepStatus(instance->currentStepId, StepStatus::InProgress);
                ExecuteActions(step->onStartActions);
                MarkStateDirty();

                TASK_LOG_INFO() << "TaskExecutor: Starting step '" << step->id << "' of task '" << instance->definition->id << "'";
            }
        }
        return;
    }

    const TaskStep* step = instance->definition->GetStep(instance->currentStepId);
    if (!step) return;

    StepStatus currentStatus = instance->GetStepStatus(instance->currentStepId);

    // 检查步骤是否完成
    if (currentStatus == StepStatus::InProgress && step->IsComplete(world_)) {
        instance->SetStepStatus(instance->currentStepId, StepStatus::Completed);
        ExecuteActions(step->onCompleteActions);
        MarkStateDirty();

        TASK_LOG_INFO() << "TaskExecutor: Completed step '" << step->id << "' of task '" << instance->definition->id << "'";

        // 找到下一步骤
        // 这里简化处理：实际应该根据步骤的连接关系查找
        // 当前实现：查找下一个未完成的步骤
        for (const auto& s : instance->definition->steps) {
            if (instance->GetStepStatus(s.id) == StepStatus::Pending) {
                instance->currentStepId = s.id;
                if (s.CanStart(world_)) {
                    instance->SetStepStatus(s.id, StepStatus::InProgress);
                    ExecuteActions(s.onStartActions);
                    MarkStateDirty();

                    TASK_LOG_INFO() << "TaskExecutor: Starting next step '" << s.id << "'";
                }
                break;
            }
        }
    }

    // 检查步骤是否失败
    if (currentStatus == StepStatus::InProgress && step->HasFailed(world_)) {
        instance->SetStepStatus(instance->currentStepId, StepStatus::Failed);
        ExecuteActions(step->onFailActions);
        MarkStateDirty();

        TASK_LOG_WARN() << "TaskExecutor: Step '" << step->id << "' of task '" << instance->definition->id << "' failed";
    }
}

void TaskExecutor::CheckTaskCompletion(TaskInstance* instance) {
    if (!instance->definition) return;

    // 检查所有必需步骤是否完成
    bool allStepsComplete = true;
    for (const auto& step : instance->definition->steps) {
        if (!step.optional && !instance->HasCompletedStep(step.id)) {
            allStepsComplete = false;
            break;
        }
    }

    // 检查所有主目标是否达成
    bool allGoalsAchieved = true;
    for (const auto& goal : instance->definition->goals) {
        if (goal.type == GoalType::Primary && !instance->IsGoalAchieved(goal.id)) {
            if (goal.IsAchieved(world_)) {
                instance->SetGoalAchieved(goal.id, true);
                MarkStateDirty();
            } else {
                allGoalsAchieved = false;
            }
        }
    }

    if (allStepsComplete && allGoalsAchieved) {
        CompleteTask(instance->instanceId);
    }
}

void TaskExecutor::CheckTaskFailure(TaskInstance* instance) {
    if (!instance->definition) return;

    // 检查失败条件
    if (instance->definition->failureConditions.Evaluate(world_)) {
        FailTask(instance->instanceId);
    }
}

TaskInstance* TaskExecutor::StartTask(const TaskDefinition* definition) {
    if (!definition) {
        TASK_LOG_ERROR() << "TaskExecutor::StartTask: Invalid task definition";
        return nullptr;
    }

    if (!CanStartTask(definition)) {
        TASK_LOG_WARN() << "TaskExecutor::StartTask: Task '" << definition->id << "' cannot be started (conditions not met)";
        return nullptr;
    }

    std::string instanceId = definition->id + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    auto instance = std::make_unique<TaskInstance>(definition, instanceId);
    instance->status = TaskStatus::InProgress;
    instance->timeStarted = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    TaskInstance* ptr = instance.get();
    taskInstances_[instanceId] = std::move(instance);

    // 订阅事件
    if (config_.enableEventProcessing) {
        SubscribeToEvents(ptr);
    }

    stats_.totalTasksStarted++;
    stats_.activeTasks++;
    MarkStateDirty();

    TASK_LOG_INFO() << "TaskExecutor::StartTask: Started task '" << definition->id << "' (instance: " << instanceId << ")";

    return ptr;
}

void TaskExecutor::CompleteTask(const std::string& instanceId) {
    auto it = taskInstances_.find(instanceId);
    if (it == taskInstances_.end()) return;

    TaskInstance* instance = it->second.get();
    if (instance->status == TaskStatus::Completed) return;

    instance->status = TaskStatus::Completed;
    instance->timeCompleted = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    stats_.totalTasksCompleted++;
    stats_.activeTasks--;

    double completionTime = instance->timeCompleted - instance->timeStarted;
    stats_.averageCompletionTime =
        (stats_.averageCompletionTime * (stats_.totalTasksCompleted - 1) + completionTime) /
        stats_.totalTasksCompleted;
    MarkStateDirty();

    TASK_LOG_INFO() << "TaskExecutor::CompleteTask: Completed task '" << instance->definition->id << "' (instance: " << instanceId << ", time: " << completionTime << " s)";

    // 启动后续任务
    if (instance->definition) {
        for (const auto& nextTaskId : instance->definition->nextTaskIds) {
            const TaskDefinition* nextDef = GetTaskDefinition(nextTaskId);
            if (nextDef && nextDef->autoAccept) {
                StartTask(nextDef);
            }
        }
    }
}

void TaskExecutor::FailTask(const std::string& instanceId) {
    auto it = taskInstances_.find(instanceId);
    if (it == taskInstances_.end()) return;

    TaskInstance* instance = it->second.get();
    if (instance->status == TaskStatus::Failed) return;

    instance->status = TaskStatus::Failed;
    instance->timeCompleted = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    stats_.totalTasksFailed++;
    stats_.activeTasks--;
    MarkStateDirty();

    TASK_LOG_WARN() << "TaskExecutor::FailTask: Failed task '" << instance->definition->id << "' (instance: " << instanceId << ")";
}

void TaskExecutor::AbandonTask(const std::string& instanceId) {
    auto it = taskInstances_.find(instanceId);
    if (it == taskInstances_.end()) return;

    TaskInstance* instance = it->second.get();
    instance->status = TaskStatus::Abandoned;
    stats_.activeTasks--;
    MarkStateDirty();

    TASK_LOG_INFO() << "TaskExecutor::AbandonTask: Abandoned task '" << instance->definition->id << "' (instance: " << instanceId << ")";
}

TaskInstance* TaskExecutor::GetTaskInstance(const std::string& instanceId) {
    auto it = taskInstances_.find(instanceId);
    return it != taskInstances_.end() ? it->second.get() : nullptr;
}

const TaskInstance* TaskExecutor::GetTaskInstance(const std::string& instanceId) const {
    auto it = taskInstances_.find(instanceId);
    return it != taskInstances_.end() ? it->second.get() : nullptr;
}

std::vector<TaskInstance*> TaskExecutor::GetAllTasks() {
    std::vector<TaskInstance*> tasks;
    tasks.reserve(taskInstances_.size());
    for (auto& [id, instance] : taskInstances_) {
        tasks.push_back(instance.get());
    }
    return tasks;
}

std::vector<TaskInstance*> TaskExecutor::GetTasksByStatus(TaskStatus status) {
    std::vector<TaskInstance*> tasks;
    for (auto& [id, instance] : taskInstances_) {
        if (instance->status == status) {
            tasks.push_back(instance.get());
        }
    }
    return tasks;
}

void TaskExecutor::RegisterTaskDefinition(const TaskDefinition& definition) {
    taskDefinitions_[definition.id] = definition;
    TASK_LOG_INFO() << "TaskExecutor::RegisterTaskDefinition: Registered task '" << definition.id << "': " << definition.name;
}

void TaskExecutor::UnregisterTaskDefinition(const std::string& taskId) {
    taskDefinitions_.erase(taskId);
    TASK_LOG_INFO() << "TaskExecutor::UnregisterTaskDefinition: Unregistered task '" << taskId << "'";
}

const TaskDefinition* TaskExecutor::GetTaskDefinition(const std::string& taskId) const {
    auto it = taskDefinitions_.find(taskId);
    return it != taskDefinitions_.end() ? &it->second : nullptr;
}

std::vector<const TaskDefinition*> TaskExecutor::GetRegisteredTaskDefinitions() const {
    std::vector<const TaskDefinition*> definitions;
    definitions.reserve(taskDefinitions_.size());
    for (const auto& [taskId, definition] : taskDefinitions_) {
        (void)taskId;
        definitions.push_back(&definition);
    }
    return definitions;
}

void TaskExecutor::ProcessEvent(const std::string& eventId, const void* eventData) {
    // 查找订阅了此事件的任务
    auto it = eventToTasks_.find(eventId);
    if (it == eventToTasks_.end()) return;

    for (const auto& instanceId : it->second) {
        TaskInstance* instance = GetTaskInstance(instanceId);
        if (instance && instance->definition) {
            // 记录事件历史
            if (config_.enableReplay) {
                TaskInstance::EventRecord record;
                record.eventId = eventId;
                record.timestamp = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                record.description = "Event received";
                instance->eventHistory.push_back(record);

                // 限制历史记录大小
                if (instance->eventHistory.size() > config_.maxHistorySize) {
                    instance->eventHistory.erase(instance->eventHistory.begin());
                }
            }

            // 更新任务
            UpdateTask(instance, 0.0f);
            MarkStateDirty();
        }
    }

    FlushAutoSaveIfNeeded();
}

void TaskExecutor::SubscribeToEvents(TaskInstance* instance) {
    if (!instance || !instance->definition) return;

    for (const auto& eventId : instance->definition->subscribedEvents) {
        eventToTasks_[eventId].push_back(instance->instanceId);
    }
}

bool TaskExecutor::ReplanTask(const std::string& instanceId, const std::string& reason) {
    auto it = taskInstances_.find(instanceId);
    if (it == taskInstances_.end()) return false;

    TaskInstance* instance = it->second.get();
    if (!instance->definition || !instance->definition->replannable) {
        TASK_LOG_WARN() << "TaskExecutor::ReplanTask: Task '" << instance->definition->id << "' is not replannable";
        return false;
    }

    TASK_LOG_INFO() << "TaskExecutor::ReplanTask: Replanning task '" << instance->definition->id << "' (reason: " << reason << ")";

    // 简化实现：重置当前步骤状态
    if (!instance->currentStepId.empty()) {
        instance->SetStepStatus(instance->currentStepId, StepStatus::Pending);

        // 尝试找到替代步骤
        const TaskStep* currentStep = instance->definition->GetStep(instance->currentStepId);
        if (currentStep) {
            for (const auto& altStepId : currentStep->alternativeStepIds) {
                const TaskStep* altStep = instance->definition->GetStep(altStepId);
                if (altStep && altStep->CanStart(world_)) {
                    instance->currentStepId = altStepId;
                    TASK_LOG_INFO() << "Switched to alternative step '" << altStepId << "'";
                    return true;
                }
            }
        }
    }

    return true;
}

bool TaskExecutor::SaveState(const std::string& filePath) {
    TASK_LOG_INFO() << "TaskExecutor::SaveState: Saving state to '" << filePath << "'";

    // Versioned JSON save (definitions are assumed to be registered externally).
    ::Next::JSONSerializer ser;
    ser.BeginObject("root");
    ser.WriteVersion(1);

    ser.BeginArray("instances", taskInstances_.size());
    for (const auto& [instanceId, instancePtr] : taskInstances_) {
        const TaskInstance* inst = instancePtr.get();
        if (!inst || !inst->definition) {
            continue;
        }

        ser.BeginObject("");
        ser.WriteString("instanceId", inst->instanceId);
        ser.WriteString("taskId", inst->definition->id);
        ser.WriteInt32("status", static_cast<int32_t>(inst->status));
        ser.WriteString("currentStepId", inst->currentStepId);
        ser.WriteDouble("timeStarted", inst->timeStarted);
        ser.WriteDouble("timeCompleted", inst->timeCompleted);
        ser.WriteInt32("stepsCompleted", inst->stepsCompleted);
        ser.WriteInt32("stepsFailed", inst->stepsFailed);

        // stepStatuses: map<string, int>
        ser.BeginObject("stepStatuses");
        for (const auto& [stepId, st] : inst->stepStatuses) {
            ser.WriteInt32(stepId, static_cast<int32_t>(st));
        }
        ser.EndObject();

        // goalAchieved: map<string, bool>
        ser.BeginObject("goalAchieved");
        for (const auto& [goalId, achieved] : inst->goalAchieved) {
            ser.WriteBool(goalId, achieved);
        }
        ser.EndObject();

        // variables
        ser.BeginObject("variables");
        ser.BeginObject("ints");
        for (const auto& [k, v] : inst->variables.ints) ser.WriteInt64(k, v);
        ser.EndObject();
        ser.BeginObject("floats");
        for (const auto& [k, v] : inst->variables.floats) ser.WriteDouble(k, v);
        ser.EndObject();
        ser.BeginObject("strings");
        for (const auto& [k, v] : inst->variables.strings) ser.WriteString(k, v);
        ser.EndObject();
        ser.BeginObject("bools");
        for (const auto& [k, v] : inst->variables.bools) ser.WriteBool(k, v);
        ser.EndObject();
        ser.EndObject();  // variables

        // event history
        ser.BeginArray("eventHistory", inst->eventHistory.size());
        for (const auto& e : inst->eventHistory) {
            ser.BeginObject("");
            ser.WriteString("eventId", e.eventId);
            ser.WriteDouble("timestamp", e.timestamp);
            ser.WriteString("description", e.description);
            ser.EndObject();
        }
        ser.EndArray();

        ser.EndObject();
    }
    ser.EndArray();

    // statistics
    ser.BeginObject("statistics");
    ser.WriteUInt64("totalTasksStarted", static_cast<uint64_t>(stats_.totalTasksStarted));
    ser.WriteUInt64("totalTasksCompleted", static_cast<uint64_t>(stats_.totalTasksCompleted));
    ser.WriteUInt64("totalTasksFailed", static_cast<uint64_t>(stats_.totalTasksFailed));
    ser.WriteUInt64("activeTasks", static_cast<uint64_t>(stats_.activeTasks));
    ser.WriteDouble("averageCompletionTime", stats_.averageCompletionTime);
    ser.EndObject();

    ser.EndObject();  // root

    auto res = ser.SaveToFile(filePath);
    if (!res.IsSuccess()) {
        TASK_LOG_ERROR() << "TaskExecutor::SaveState: Failed (" << res.errorMessage << ")";
        return false;
    }
    return true;
}

bool TaskExecutor::LoadState(const std::string& filePath) {
    TASK_LOG_INFO() << "TaskExecutor::LoadState: Loading state from '" << filePath << "'";

    auto des = ::Next::Deserializer::LoadFromFile(filePath, ::Next::SerializationFormat::JSON);
    if (!des) {
        TASK_LOG_ERROR() << "TaskExecutor::LoadState: Failed to open/parse file";
        return false;
    }

    if (!des->BeginObject("root")) {
        TASK_LOG_ERROR() << "TaskExecutor::LoadState: Missing root object";
        return false;
    }

    const uint32_t version = des->ReadVersion();
    if (version != 1) {
        TASK_LOG_WARN() << "TaskExecutor::LoadState: Version mismatch (got " << version << ", expected 1)";
    }

    taskInstances_.clear();
    eventToTasks_.clear();

    if (des->BeginArray("instances")) {
        const size_t n = des->GetArraySize();
        for (size_t i = 0; i < n; ++i) {
            if (!des->BeginObject("")) {
                continue;
            }

            const std::string instanceId = des->ReadString("instanceId");
            const std::string taskId = des->ReadString("taskId");

            const TaskDefinition* def = GetTaskDefinition(taskId);
            if (!def) {
                TASK_LOG_WARN() << "TaskExecutor::LoadState: Unknown task definition '" << taskId << "', skipping instance";
                des->EndObject();
                continue;
            }

            auto inst = std::make_unique<TaskInstance>(def, instanceId);
            inst->status = static_cast<TaskStatus>(des->ReadInt32("status", static_cast<int32_t>(TaskStatus::NotStarted)));
            inst->currentStepId = des->ReadString("currentStepId");
            inst->timeStarted = des->ReadDouble("timeStarted", 0.0);
            inst->timeCompleted = des->ReadDouble("timeCompleted", 0.0);
            inst->stepsCompleted = des->ReadInt32("stepsCompleted", 0);
            inst->stepsFailed = des->ReadInt32("stepsFailed", 0);

            // stepStatuses
            if (des->BeginObject("stepStatuses")) {
                std::vector<std::string> keys;
                if (des->GetObjectKeys(keys)) {
                    for (const auto& k : keys) {
                        inst->stepStatuses[k] = static_cast<StepStatus>(des->ReadInt32(k, static_cast<int32_t>(StepStatus::Pending)));
                    }
                }
                des->EndObject();
            }

            // goalAchieved
            if (des->BeginObject("goalAchieved")) {
                std::vector<std::string> keys;
                if (des->GetObjectKeys(keys)) {
                    for (const auto& k : keys) {
                        inst->goalAchieved[k] = des->ReadBool(k, false);
                    }
                }
                des->EndObject();
            }

            // variables
            if (des->BeginObject("variables")) {
                if (des->BeginObject("ints")) {
                    std::vector<std::string> keys;
                    if (des->GetObjectKeys(keys)) {
                        for (const auto& k : keys) inst->variables.ints[k] = des->ReadInt64(k, 0);
                    }
                    des->EndObject();
                }
                if (des->BeginObject("floats")) {
                    std::vector<std::string> keys;
                    if (des->GetObjectKeys(keys)) {
                        for (const auto& k : keys) inst->variables.floats[k] = des->ReadDouble(k, 0.0);
                    }
                    des->EndObject();
                }
                if (des->BeginObject("strings")) {
                    std::vector<std::string> keys;
                    if (des->GetObjectKeys(keys)) {
                        for (const auto& k : keys) inst->variables.strings[k] = des->ReadString(k, "");
                    }
                    des->EndObject();
                }
                if (des->BeginObject("bools")) {
                    std::vector<std::string> keys;
                    if (des->GetObjectKeys(keys)) {
                        for (const auto& k : keys) inst->variables.bools[k] = des->ReadBool(k, false);
                    }
                    des->EndObject();
                }
                des->EndObject();  // variables
            }

            // eventHistory
            if (des->BeginArray("eventHistory")) {
                const size_t m = des->GetArraySize();
                inst->eventHistory.reserve(m);
                for (size_t j = 0; j < m; ++j) {
                    if (!des->BeginObject("")) {
                        continue;
                    }
                    TaskInstance::EventRecord rec;
                    rec.eventId = des->ReadString("eventId");
                    rec.timestamp = des->ReadDouble("timestamp", 0.0);
                    rec.description = des->ReadString("description");
                    inst->eventHistory.push_back(std::move(rec));
                    des->EndObject();
                }
                des->EndArray();
            }

            TaskInstance* instPtr = inst.get();
            taskInstances_[instanceId] = std::move(inst);

            // Rebuild event subscriptions.
            if (config_.enableEventProcessing) {
                SubscribeToEvents(instPtr);
            }

            des->EndObject();
        }
        des->EndArray();
    }

    // statistics
    if (des->BeginObject("statistics")) {
        stats_.totalTasksStarted = static_cast<size_t>(des->ReadUInt64("totalTasksStarted", 0));
        stats_.totalTasksCompleted = static_cast<size_t>(des->ReadUInt64("totalTasksCompleted", 0));
        stats_.totalTasksFailed = static_cast<size_t>(des->ReadUInt64("totalTasksFailed", 0));
        stats_.activeTasks = static_cast<size_t>(des->ReadUInt64("activeTasks", 0));
        stats_.averageCompletionTime = des->ReadDouble("averageCompletionTime", 0.0);
        des->EndObject();
    }

    des->EndObject();  // root
    autoSaveDirty_ = false;

    return true;
}

void TaskExecutor::ResetStatistics() {
    std::memset(&stats_, 0, sizeof(stats_));
}

bool TaskExecutor::CanStartTask(const TaskDefinition* definition) {
    if (!definition) return false;
    return definition->acceptanceConditions.Evaluate(world_);
}

void TaskExecutor::ExecuteActions(const std::vector<Action>& actions) {
    for (const auto& action : actions) {
        action.Execute(world_);
    }
}

void TaskExecutor::MarkStateDirty() {
    if (config_.enableAutoSave && config_.autoSaveOnStateChange) {
        autoSaveDirty_ = true;
    }
}

void TaskExecutor::FlushAutoSaveIfNeeded(bool force) {
    if (!config_.enableAutoSave) {
        autoSaveDirty_ = false;
        return;
    }

    if (!force && !autoSaveDirty_) {
        return;
    }

    if (config_.autoSavePath.empty()) {
        TASK_LOG_WARN() << "TaskExecutor::FlushAutoSaveIfNeeded: auto-save path is empty; skipping";
        autoSaveDirty_ = false;
        return;
    }

    if (!SaveState(config_.autoSavePath)) {
        TASK_LOG_ERROR() << "TaskExecutor::FlushAutoSaveIfNeeded: failed to write auto-save '"
                         << config_.autoSavePath << "'";
        return;
    }

    autoSaveDirty_ = false;
}

// ============================================================================
// 任务调度器实现
// ============================================================================

TaskScheduler::TaskScheduler(TaskExecutor* executor, const TaskSchedulerConfig& config)
    : executor_(executor)
    , config_(config)
{
}

void TaskScheduler::Initialize() {
    TASK_LOG_INFO() << "TaskScheduler::Initialize: Task scheduler initialized";
}

void TaskScheduler::Update(float deltaTime) {
    if (config_.autoAcceptTasks) {
        CheckAutoAcceptTasks();
    }
}

void TaskScheduler::Shutdown() {
    TASK_LOG_INFO() << "TaskScheduler::Shutdown: Task scheduler shut down";
}

bool TaskScheduler::IsTaskAvailable(const std::string& taskId) {
    const TaskDefinition* definition = executor_->GetTaskDefinition(taskId);
    if (!definition) return false;

    return CheckPrerequisites(*definition);
}

std::vector<std::string> TaskScheduler::GetAvailableTasks() {
    std::vector<std::string> availableTasks;
    const auto definitions = executor_->GetRegisteredTaskDefinitions();
    const auto tasks = executor_->GetAllTasks();

    for (const TaskDefinition* definition : definitions) {
        if (!definition) {
            continue;
        }

        bool alreadyTracked = false;
        for (const TaskInstance* task : tasks) {
            if (!task || !task->definition) {
                continue;
            }
            if (task->definition->id == definition->id &&
                task->status != TaskStatus::Abandoned &&
                task->status != TaskStatus::Failed) {
                alreadyTracked = true;
                break;
            }
        }

        if (alreadyTracked) {
            continue;
        }

        if (CheckPrerequisites(*definition) && executor_->CanStartTask(definition)) {
            availableTasks.push_back(definition->id);
        }
    }

    return availableTasks;
}

void TaskScheduler::ProcessEvent(const std::string& eventId, const void* eventData) {
    if (!config_.enableEventBasedTriggering) return;
    executor_->ProcessEvent(eventId, eventData);

    const auto definitions = executor_->GetRegisteredTaskDefinitions();
    for (const TaskDefinition* definition : definitions) {
        if (!definition || !definition->autoAccept) {
            continue;
        }

        if (std::find(definition->subscribedEvents.begin(),
                      definition->subscribedEvents.end(),
                      eventId) == definition->subscribedEvents.end()) {
            continue;
        }

        if (!CheckPrerequisites(*definition) || !executor_->CanStartTask(definition)) {
            continue;
        }

        bool alreadyTracked = false;
        for (TaskInstance* task : executor_->GetAllTasks()) {
            if (!task || !task->definition) {
                continue;
            }
            if (task->definition->id == definition->id &&
                task->status != TaskStatus::Abandoned &&
                task->status != TaskStatus::Failed) {
                alreadyTracked = true;
                break;
            }
        }

        if (!alreadyTracked) {
            executor_->StartTask(definition);
        }
    }
}

void TaskScheduler::CheckAutoAcceptTasks() {
    const auto definitions = executor_->GetRegisteredTaskDefinitions();
    for (const TaskDefinition* definition : definitions) {
        if (!definition || !definition->autoAccept) {
            continue;
        }

        if (!CheckPrerequisites(*definition) || !executor_->CanStartTask(definition)) {
            continue;
        }

        bool alreadyTracked = false;
        for (TaskInstance* task : executor_->GetAllTasks()) {
            if (!task || !task->definition) {
                continue;
            }
            if (task->definition->id == definition->id &&
                task->status != TaskStatus::Abandoned &&
                task->status != TaskStatus::Failed) {
                alreadyTracked = true;
                break;
            }
        }

        if (!alreadyTracked) {
            executor_->StartTask(definition);
        }
    }
}

bool TaskScheduler::CheckPrerequisites(const TaskDefinition& definition) {
    // 检查前置任务是否完成
    if (config_.enableTaskPrerequisites) {
        for (const auto& prereqId : definition.prerequisiteTaskIds) {
            auto tasks = executor_->GetTasksByStatus(TaskStatus::Completed);
            bool found = false;
            for (auto* task : tasks) {
                if (task->definition && task->definition->id == prereqId) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
    }

    return true;
}

// ============================================================================
// 任务重规划器实现
// ============================================================================

TaskReplanner::TaskReplanner(World* world, TaskExecutor* executor)
    : world_(world)
    , executor_(executor)
{
}

void TaskReplanner::RegisterReplanRule(const std::string& taskId, const ReplanRule& rule) {
    replanRules_[taskId].push_back(rule);
    TASK_LOG_INFO() << "TaskReplanner::RegisterReplanRule: Registered replan rule for task '" << taskId << "'";
}

bool TaskReplanner::ReplanTask(TaskInstance* instance, ReplanReason reason, const std::string& description) {
    if (!instance || !instance->definition) return false;

    if (!EvaluateReplannability(instance)) {
        TASK_LOG_WARN() << "TaskReplanner::ReplanTask: Task '" << instance->definition->id << "' is not replannable";
        return false;
    }

    TASK_LOG_INFO() << "TaskReplanner::ReplanTask: Replanning task '" << instance->definition->id << "' (reason: " << static_cast<int>(reason) << ", description: " << description << ")";

    // 执行重规划规则
    auto it = replanRules_.find(instance->definition->id);
    if (it != replanRules_.end()) {
        for (const auto& rule : it->second) {
            if (rule.triggerCondition.Evaluate(world_)) {
                // 执行重规划动作
                for (const auto& action : rule.actions) {
                    action.Execute(world_);
                }

                switch (rule.strategy) {
                    case ReplanStrategy::ReplaceStep:
                        return FindAlternativeStep(instance, instance->currentStepId);

                    case ReplanStrategy::DelayTask:
                        TASK_LOG_INFO() << "Strategy: Delay task";
                        return true;

                    case ReplanStrategy::FailTask:
                        executor_->FailTask(instance->instanceId);
                        return true;

                    case ReplanStrategy::CreateRemedial:
                        return CreateRemedialTask(instance, description);

                    default:
                        break;
                }
            }
        }
    }

    // 默认重规划行为
    return executor_->ReplanTask(instance->instanceId, description);
}

bool TaskReplanner::EvaluateReplannability(const TaskInstance* instance) const {
    if (!instance || !instance->definition) return false;
    return instance->definition->replannable;
}

bool TaskReplanner::FindAlternativeStep(TaskInstance* instance, const std::string& stepId) {
    if (!instance->definition) return false;

    const TaskStep* currentStep = instance->definition->GetStep(stepId);
    if (!currentStep) return false;

    for (const auto& altStepId : currentStep->alternativeStepIds) {
        const TaskStep* altStep = instance->definition->GetStep(altStepId);
        if (altStep && altStep->CanStart(world_)) {
            instance->currentStepId = altStepId;
            instance->SetStepStatus(stepId, StepStatus::Blocked);
            instance->SetStepStatus(altStepId, StepStatus::InProgress);

            TASK_LOG_INFO() << "Found alternative step: " << stepId << " -> " << altStepId;
            return true;
        }
    }

    return false;
}

bool TaskReplanner::CreateRemedialTask(TaskInstance* instance, const std::string& reason) {
    TASK_LOG_INFO() << "Creating remedial task for: " << reason;
    return false;
}

// ============================================================================
// 任务系统管理器实现
// ============================================================================

TaskSystemManager& TaskSystemManager::GetInstance() {
    static TaskSystemManager instance;
    return instance;
}

TaskSystemManager::~TaskSystemManager() {
    Shutdown();
}

void TaskSystemManager::Initialize(World* world, EventBus* eventBus) {
    if (initialized_) {
        TASK_LOG_WARN() << "TaskSystemManager::Initialize: Already initialized";
        return;
    }

    // 创建执行器
    executor_ = std::make_unique<TaskExecutor>(world, eventBus);
    executor_->Initialize();

    // 创建调度器
    scheduler_ = std::make_unique<TaskScheduler>(executor_.get());
    scheduler_->Initialize();

    // 创建重规划器
    replanner_ = std::make_unique<TaskReplanner>(world, executor_.get());

    initialized_ = true;

    TASK_LOG_INFO() << "TaskSystemManager::Initialize: Task system initialized";
}

void TaskSystemManager::Update(float deltaTime) {
    if (!initialized_) return;

    if (scheduler_) scheduler_->Update(deltaTime);
    if (executor_) executor_->Update(deltaTime);
}

void TaskSystemManager::Shutdown() {
    if (!initialized_) return;

    if (executor_) executor_->Shutdown();
    if (scheduler_) scheduler_->Shutdown();

    replanner_.reset();
    scheduler_.reset();
    executor_.reset();

    initialized_ = false;

    TASK_LOG_INFO() << "TaskSystemManager::Shutdown: Task system shut down";
}

} // namespace Next

#undef TASK_LOG_INFO
#undef TASK_LOG_WARN
#undef TASK_LOG_ERROR
