#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <cstdint>
#include <variant>

#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/event_bus.h"

namespace Next {

// NOTE: Runtime types are included above; keep this header self-contained.

/**
 * @brief CP9: Task System (任务系统)
 *
 * 核心理念：
 * 1. 事件驱动（Event-first）- 任务订阅事件，不持有脆弱引用
 * 2. 声明式条件（Declarative Conditions）- 条件集合描述目标
 * 3. 可回放（Replay）- 事件流 + 关键快照
 *
 * 三层模型：
 * 1. 叙事层（Narrative Intent）- 故事和体验目标
 * 2. 目标层（Goals & Constraints）- 达成条件和限制
 * 3. 执行层（Plans/Steps）- 具体步骤（可重规划）
 *
 * 对标：
 * - UE5: Quest System
 * - Unity: Quest Machine
 * - AAA: Dynamic quest systems with replanning
 */

// ============================================================================
// 条件系统（声明式条件）
// ============================================================================

/**
 * @brief 条件操作符
 */
enum class ConditionOperator {
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Contains,
    InRange,
    HasFlag,
    Exists
};

/**
 * @brief 条件类型
 */
enum class ConditionType {
    None,
    Time,              // 时间条件
    Location,          // 地点条件
    Relationship,      // 人物关系
    Item,              // 物品状态
    TownState,         // 城镇状态
    WorldEvent,        // 世界事件
    Custom,            // 自定义条件
    Composite          // 复合条件（AND/OR/NOT）
};

/**
 * @brief 声明式条件
 */
struct Condition {
    ConditionType type = ConditionType::None;
    std::string target;          // 目标（例如："player.health"）
    ConditionOperator op = ConditionOperator::Equal;
    std::variant<int64_t, double, bool, std::string> value;

    // 用于复合条件
    enum LogicOp { And, Or, Not } logicOp = And;
    std::vector<Condition> subConditions;

    // 用于范围检查
    struct {
        std::variant<int64_t, double, bool, std::string> min;
        std::variant<int64_t, double, bool, std::string> max;
    } range;

    Condition() = default;
    Condition(ConditionType t, const std::string& tgt,
              ConditionOperator o, const std::variant<int64_t, double, bool, std::string>& v)
        : type(t), target(tgt), op(o), value(v) {}

    bool Evaluate(const World* world) const;
};

/**
 * @brief 条件集合（AND/OR逻辑）
 */
struct ConditionSet {
    enum LogicOp { All, Any, One } logicOp = All;
    std::vector<Condition> conditions;

    bool Evaluate(const World* world) const;
    bool IsEmpty() const { return conditions.empty(); }
};

// ============================================================================
// 动作系统
// ============================================================================

/**
 * @brief 动作类型
 */
enum class ActionType {
    None,
    TriggerDialogue,   // 触发对话
    SpawnNPC,          // 生成NPC
    GiveItem,          // 给予物品
    SetWorldState,     // 改变世界状态
    Teleport,          // 传送
    EnableTask,        // 启用任务
    CompleteTask,      // 完成任务
    FailTask,          // 失败任务
    ShowNotification,  // 显示通知
    PlayCinematic,     // 播放过场动画
    Custom,            // 自定义动作
    Delay              // 延迟
};

/**
 * @brief 动作参数
 */
struct ActionParams {
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, int64_t> ints;
    std::unordered_map<std::string, double> floats;
    std::unordered_map<std::string, bool> bools;

    void Set(const std::string& key, const std::string& value) { strings[key] = value; }
    void Set(const std::string& key, int64_t value) { ints[key] = value; }
    void Set(const std::string& key, double value) { floats[key] = value; }
    void Set(const std::string& key, bool value) { bools[key] = value; }
};

/**
 * @brief 动作
 */
struct Action {
    ActionType type = ActionType::None;
    ActionParams params;

    // 用于延迟动作
    float delaySeconds = 0.0f;

    std::function<bool(const World*, const ActionParams&)> customExecutor;

    bool Execute(const World* world) const;
};

// ============================================================================
// 任务步骤（执行层）
// ============================================================================

/**
 * @brief 步骤状态
 */
enum class StepStatus {
    Pending,        // 等待开始
    InProgress,     // 进行中
    Completed,      // 已完成
    Failed,         // 已失败
    Skipped,        // 已跳过
    Blocked         // 被阻塞（条件不满足）
};

/**
 * @brief 任务步骤
 */
struct TaskStep {
    std::string id;
    std::string description;

    ConditionSet prerequisites;      // 前置条件
    ConditionSet completionCriteria; // 完成条件
    ConditionSet failureConditions;  // 失败条件

    std::vector<Action> onStartActions;     // 开始时的动作
    std::vector<Action> onCompleteActions;  // 完成时的动作
    std::vector<Action> onFailActions;      // 失败时的动作

    StepStatus status = StepStatus::Pending;
    bool optional = false;           // 是否可选
    bool silent = false;             // 是否静默（不显示UI）

    // 重规划相关
    bool replannable = true;         // 是否可重规划
    std::vector<std::string> alternativeStepIds; // 替代步骤ID

    TaskStep() = default;
    TaskStep(const std::string& i, const std::string& desc)
        : id(i), description(desc) {}

    bool CanStart(const World* world) const;
    bool IsComplete(const World* world) const;
    bool HasFailed(const World* world) const;
};

// ============================================================================
// 任务目标（目标层）
// ============================================================================

/**
 * @brief 目标类型
 */
enum class GoalType {
    None,
    Primary,        // 主目标
    Secondary,      // 次要目标
    Optional,       // 可选目标
    Hidden,         // 隐藏目标
    FailCondition   // 失败条件
};

/**
 * @brief 任务目标
 */
struct TaskGoal {
    std::string id;
    std::string description;
    GoalType type = GoalType::Primary;

    ConditionSet achievementConditions;  // 达成条件
    std::vector<std::string> requiredStepIds; // 需要完成的步骤

    bool achieved = false;
    bool visible = true;

    TaskGoal() = default;
    TaskGoal(const std::string& i, const std::string& desc, GoalType t)
        : id(i), description(desc), type(t) {}

    bool IsAchieved(const World* world) const;
};

// ============================================================================
// 任务定义（叙事层 + 目标层 + 执行层）
// ============================================================================

/**
 * @brief 任务优先级
 */
enum class TaskPriority {
    Background,     // 后台任务
    Normal,         // 普通任务
    Main,           // 主线任务
    Critical        // 关键任务
};

/**
 * @brief 任务状态
 */
enum class TaskStatus {
    NotStarted,     // 未开始
    Available,      // 可接取
    InProgress,     // 进行中
    Completed,      // 已完成
    Failed,         // 已失败
    Abandoned,      // 已放弃
    Blocked         // 被阻塞
};

/**
 * @brief 任务定义
 */
struct TaskDefinition {
    // 基本信息
    std::string id;
    std::string name;
    std::string description;
    TaskPriority priority = TaskPriority::Normal;

    // 叙事层
    std::string narrativeIntent;   // 叙事意图（要达成的体验）
    std::string storyContext;      // 故事背景

    // 目标层
    std::vector<TaskGoal> goals;

    // 执行层
    std::vector<TaskStep> steps;
    std::string firstStepId;

    // 接取条件
    ConditionSet acceptanceConditions;

    // 失败条件
    ConditionSet failureConditions;

    // 自动接取
    bool autoAccept = false;

    // 可重规划
    bool replannable = true;

    // 事件订阅
    std::vector<std::string> subscribedEvents;

    // 任务序列
    std::vector<std::string> prerequisiteTaskIds;  // 前置任务
    std::vector<std::string> nextTaskIds;          // 后续任务

    TaskDefinition() = default;
    TaskDefinition(const std::string& i) : id(i) {}

    const TaskStep* GetStep(const std::string& stepId) const;
    const TaskGoal* GetGoal(const std::string& goalId) const;
};

// ============================================================================
// 任务实例（运行时状态）
// ============================================================================

/**
 * @brief 任务变量（运行时数据）
 */
struct TaskVariables {
    std::unordered_map<std::string, int64_t> ints;
    std::unordered_map<std::string, double> floats;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, bool> bools;

    void Set(const std::string& key, int64_t value) { ints[key] = value; }
    void Set(const std::string& key, double value) { floats[key] = value; }
    void Set(const std::string& key, const std::string& value) { strings[key] = value; }
    void Set(const std::string& key, bool value) { bools[key] = value; }

    bool Has(const std::string& key) const;
};

/**
 * @brief 任务实例
 */
struct TaskInstance {
    const TaskDefinition* definition = nullptr;
    std::string instanceId;

    TaskStatus status = TaskStatus::NotStarted;

    // 当前步骤
    std::string currentStepId;
    std::unordered_map<std::string, StepStatus> stepStatuses;

    // 目标状态
    std::unordered_map<std::string, bool> goalAchieved;

    // 运行时变量
    TaskVariables variables;

    // 事件追踪（用于调试和回放）
    struct EventRecord {
        std::string eventId;
        double timestamp;
        std::string description;
    };
    std::vector<EventRecord> eventHistory;

    // 统计
    double timeStarted = 0.0;
    double timeCompleted = 0.0;
    int stepsCompleted = 0;
    int stepsFailed = 0;

    TaskInstance() = default;
    TaskInstance(const TaskDefinition* def, const std::string& instId)
        : definition(def), instanceId(instId) {}

    bool HasCompletedStep(const std::string& stepId) const;
    bool HasFailedStep(const std::string& stepId) const;
    void SetStepStatus(const std::string& stepId, StepStatus status);
    StepStatus GetStepStatus(const std::string& stepId) const;

    bool IsGoalAchieved(const std::string& goalId) const;
    void SetGoalAchieved(const std::string& goalId, bool achieved);
};

// ============================================================================
// 任务执行器
// ============================================================================

/**
 * @brief 任务执行器配置
 */
struct TaskExecutorConfig {
    bool enableEventProcessing = true;
    bool enableAutoSave = true;
    bool autoSaveOnStateChange = true;
    bool enableReplay = true;
    float updateInterval = 0.1f;  // 更新间隔（秒）
    size_t maxHistorySize = 1000; // 最大历史记录数
    std::string autoSavePath = "task_state_autosave.json";
};

/**
 * @brief 任务执行器
 */
class TaskExecutor {
public:
    TaskExecutor(World* world, EventBus* eventBus, const TaskExecutorConfig& config = {});
    ~TaskExecutor();

    void Initialize();
    void Update(float deltaTime);
    void Shutdown();

    // 任务管理
    TaskInstance* StartTask(const TaskDefinition* definition);
    void CompleteTask(const std::string& instanceId);
    void FailTask(const std::string& instanceId);
    void AbandonTask(const std::string& instanceId);

    // 任务查询
    TaskInstance* GetTaskInstance(const std::string& instanceId);
    const TaskInstance* GetTaskInstance(const std::string& instanceId) const;
    std::vector<TaskInstance*> GetAllTasks();
    std::vector<TaskInstance*> GetTasksByStatus(TaskStatus status);

    // 任务定义管理
    void RegisterTaskDefinition(const TaskDefinition& definition);
    void UnregisterTaskDefinition(const std::string& taskId);
    const TaskDefinition* GetTaskDefinition(const std::string& taskId) const;
    std::vector<const TaskDefinition*> GetRegisteredTaskDefinitions() const;

    // 事件处理
    void ProcessEvent(const std::string& eventId, const void* eventData);
    void SubscribeToEvents(TaskInstance* instance);

    // 重规划
    bool ReplanTask(const std::string& instanceId, const std::string& reason);

    // 保存和加载
    bool SaveState(const std::string& filePath);
    bool LoadState(const std::string& filePath);

    // 统计
    struct Statistics {
        size_t totalTasksStarted = 0;
        size_t totalTasksCompleted = 0;
        size_t totalTasksFailed = 0;
        size_t activeTasks = 0;
        double averageCompletionTime = 0.0;
    };
    Statistics GetStatistics() const { return stats_; }
    void ResetStatistics();
    bool CanStartTask(const TaskDefinition* definition);

private:
    World* world_;
    TaskExecutorConfig config_;

    std::unordered_map<std::string, TaskDefinition> taskDefinitions_;
    std::unordered_map<std::string, std::unique_ptr<TaskInstance>> taskInstances_;

    // 事件订阅映射
    std::unordered_map<std::string, std::vector<std::string>> eventToTasks_;

    // 内部方法
    void UpdateTask(TaskInstance* instance, float deltaTime);
    void UpdateCurrentStep(TaskInstance* instance);
    void CheckTaskCompletion(TaskInstance* instance);
    void CheckTaskFailure(TaskInstance* instance);

    void ExecuteActions(const std::vector<Action>& actions);
    void MarkStateDirty();
    void FlushAutoSaveIfNeeded(bool force = false);

    Statistics stats_;
    float accumulatedTime_ = 0.0f;
    bool autoSaveDirty_ = false;
    bool initialized_ = false;
};

// ============================================================================
// 任务调度器
// ============================================================================

/**
 * @brief 任务调度器配置
 */
struct TaskSchedulerConfig {
    bool autoAcceptTasks = true;
    bool enableTaskPrerequisites = true;
    bool enableEventBasedTriggering = true;
};

/**
 * @brief 任务调度器
 */
class TaskScheduler {
public:
    TaskScheduler(TaskExecutor* executor, const TaskSchedulerConfig& config = {});
    ~TaskScheduler() = default;

    void Initialize();
    void Update(float deltaTime);
    void Shutdown();

    // 任务可用性检查
    bool IsTaskAvailable(const std::string& taskId);
    std::vector<std::string> GetAvailableTasks();

    // 自动任务触发
    void ProcessEvent(const std::string& eventId, const void* eventData);
    void CheckAutoAcceptTasks();

private:
    TaskExecutor* executor_;
    TaskSchedulerConfig config_;

    bool CheckPrerequisites(const TaskDefinition& definition);
};

// ============================================================================
// 任务重规划器
// ============================================================================

/**
 * @brief 重规划原因
 */
enum class ReplanReason {
    Disaster,       // 灾害
    CharacterDead,  // 角色死亡
    LocationBlocked,// 地点封锁
    ItemMissing,    // 物品丢失
    TimeWindowPassed, // 时间窗口过期
    Custom          // 自定义
};

/**
 * @brief 重规划策略
 */
enum class ReplanStrategy {
    ReplaceStep,    // 替换步骤
    DelayTask,      // 延迟任务
    ChangeLocation, // 改变地点
    ChangeContact,  // 改变联系人
    FailTask,       // 失败任务
    CreateRemedial  // 创建补救任务
};

/**
 * @brief 重规划规则
 */
struct ReplanRule {
    Condition triggerCondition;
    ReplanStrategy strategy;
    std::vector<Action> actions;

    ReplanRule() = default;
    ReplanRule(const Condition& cond, ReplanStrategy s)
        : triggerCondition(cond), strategy(s) {}
};

/**
 * @brief 任务重规划器
 */
class TaskReplanner {
public:
    TaskReplanner(World* world, TaskExecutor* executor);

    void RegisterReplanRule(const std::string& taskId, const ReplanRule& rule);
    bool ReplanTask(TaskInstance* instance, ReplanReason reason, const std::string& description);

    bool EvaluateReplannability(const TaskInstance* instance) const;

private:
    World* world_;
    TaskExecutor* executor_;

    std::unordered_map<std::string, std::vector<ReplanRule>> replanRules_;

    bool FindAlternativeStep(TaskInstance* instance, const std::string& stepId);
    bool CreateRemedialTask(TaskInstance* instance, const std::string& reason);
};

// ============================================================================
// 任务系统管理器（单例）
// ============================================================================

/**
 * @brief 任务系统管理器
 */
class TaskSystemManager {
public:
    static TaskSystemManager& GetInstance();

    void Initialize(World* world, EventBus* eventBus);
    void Update(float deltaTime);
    void Shutdown();

    TaskExecutor* GetExecutor() { return executor_.get(); }
    TaskScheduler* GetScheduler() { return scheduler_.get(); }
    TaskReplanner* GetReplanner() { return replanner_.get(); }

private:
    TaskSystemManager() = default;
    ~TaskSystemManager();

    std::unique_ptr<TaskExecutor> executor_;
    std::unique_ptr<TaskScheduler> scheduler_;
    std::unique_ptr<TaskReplanner> replanner_;

    bool initialized_ = false;
};

} // namespace Next
