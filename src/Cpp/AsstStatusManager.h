#pragma once
#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <chrono>
#include "nlohmann/json.hpp"

// 调用约定宏定义
#ifndef ASST_CALL
#define ASST_CALL __stdcall
#endif

// 消息类型枚举
enum class AsstMsg
{
    /* Global Info */
    InternalError     = 0,           // 内部错误
    InitFailed        = 1,           // 初始化失败
    ConnectionInfo    = 2,           // 连接相关信息
    AllTasksCompleted = 3,           // 全部任务完成
    AsyncCallInfo     = 4,           // 外部异步调用信息
    Destroyed         = 5,           // 实例已销毁

    /* TaskChain Info */
    TaskChainError     = 10000,      // 任务链执行/识别错误
    TaskChainStart     = 10001,      // 任务链开始
    TaskChainCompleted = 10002,      // 任务链完成
    TaskChainExtraInfo = 10003,      // 任务链额外信息

    /* SubTask Info */
    SubTaskError      = 20000,       // 原子任务执行/识别错误
    SubTaskStart      = 20001,       // 原子任务开始
    SubTaskExtraInfo  = 20003,       // 原子任务额外信息
};

// 连接状态枚举
enum class ConnectionState {
    Disconnected,      // 未连接
    Connecting,        // 连接中
    Connected,         // 已连接（未获取UUID）
    UuidObtained,      // 已获取UUID
    Reconnecting,      // 重连中
    ConnectionFailed,  // 连接失败
    UnsupportedResolution, // 分辨率不支持
    ResolutionError,   // 分辨率获取错误
    Disconnect,        // 连接断开且重试失败
    ScreencapFailed,   // 截图失败且重试失败
    TouchModeNotAvailable // 不支持的触控模式
};

// 任务链类型枚举
enum class TaskChainType {
    Unknown,           // 未知任务链
    StartUp,           // 开始唤醒
    CloseDown,         // 关闭游戏
    Fight,             // 刷理智
    Mall,              // 信用点及购物
    Recruit,           // 自动公招
    Infrast,           // 基建换班
    Award,             // 领取日常奖励
    Roguelike,         // 无限刷肉鸽
    Copilot,           // 自动抄作业
    SSSCopilot         // 自动抄保全作业
};

// 子任务类型枚举
enum class SubTaskType {
    Unknown,
    StartButton2,               // 开始战斗
    MedicineConfirm,            // 使用理智药
    ExpiringMedicineConfirm,    // 使用 48 小时内过期的理智药
    StoneConfirm,               // 碎石
    RecruitRefreshConfirm,      // 公招刷新标签
    RecruitConfirm,             // 公招确认招募
    RecruitNowConfirm,          // 公招使用加急许可
    ReportToPenguinStats,       // 汇报到企鹅数据统计
    ReportToYituliu,            // 汇报到一图流大数据
    InfrastDormDoubleConfirmButton, // 基建宿舍的二次确认按钮
    StartExplore,               // 肉鸽开始探索
    StageTraderInvestConfirm,   // 肉鸽投资了源石锭
    StageTraderInvestSystemFull,// 肉鸽投资达到了游戏上限
    ExitThenAbandon,            // 肉鸽放弃了本次探索
    MissionCompletedFlag,       // 肉鸽战斗完成
    MissionFailedFlag,          // 肉鸽战斗失败
    StageTraderEnter,           // 肉鸽关卡：诡异行商
    StageSafeHouseEnter,        // 肉鸽关卡：安全的角落
    StageEncounterEnter,        // 肉鸽关卡：不期而遇/古堡馈赠
    StageCombatDpsEnter,        // 肉鸽关卡：普通作战
    StageEmergencyDps,          // 肉鸽关卡：紧急作战
    StageDreadfulFoe,           // 肉鸽关卡：险路恶敌
    StartGameTask               // 打开客户端失败
};

// 异步调用信息结构体
struct AsyncCallRecord {
    std::string uuid;          // 设备唯一码
    std::string what;          // 回调类型
    int async_call_id = -1;    // 异步请求ID
    bool ret = false;          // 调用返回值
    int cost = 0;              // 耗时(毫秒)，转换为int存储
    std::string timestamp;     // 记录时间戳
};

// 子任务状态信息结构体
struct SubTaskStatus {
    std::string name;              // 子任务名称
    SubTaskType type = SubTaskType::Unknown; // 子任务类型
    std::chrono::system_clock::time_point start_time; // 开始时间
    std::chrono::system_clock::time_point end_time;   // 结束时间
    std::string extra_info;        // 额外信息(detail字段内容)
    std::string error_msg;         // 错误信息（如有）
    bool completed = false;        // 是否已完成
};

// 任务链状态信息结构体
struct TaskChainStatus {
    std::string id;            // 任务链ID
    TaskChainType type = TaskChainType::Unknown; // 任务链类型
    bool active = false;       // 是否活跃
    std::chrono::system_clock::time_point start_time; // 开始时间
    std::chrono::system_clock::time_point end_time;   // 结束时间
    std::string extra_info;    // 额外信息
    std::string error_msg;     // 错误信息（如有）
    std::unordered_map<std::string, SubTaskStatus> sub_tasks; // 子任务信息
};

// 状态信息结构体
struct AssistantStatus {
    // 全局信息
    bool initialized = false;
    std::string last_error;
    bool all_tasks_completed = false;
    bool destroyed = false;
    
    // 连接信息
    ConnectionState connection_state = ConnectionState::Disconnected;
    std::string connection_what;
    std::string connection_why;
    std::string device_uuid;  // 设备唯一码
    std::string adb_path;
    std::string device_address;
    std::string connection_config;
    int reconnect_attempts = 0;  // 重连尝试次数
    std::vector<std::pair<std::string, std::string>> connection_history; // 时间戳和信息
    
    // 任务链信息
    std::unordered_map<std::string, TaskChainStatus> task_chains; // 任务链ID -> 状态信息
    size_t active_task_count = 0; // 活跃任务数量
    
    // 当前子任务信息
    std::string current_sub_task;
    SubTaskType current_sub_task_type = SubTaskType::Unknown;
    std::string last_sub_task_error;
    
    // 重要提示信息（需要用户注意的）
    std::vector<std::pair<std::string, std::string>> notifications; // 时间戳和提示信息
    
    // 异步调用信息
    std::vector<AsyncCallRecord> async_call_records; // 异步调用记录列表
    std::unordered_set<int> pending_async_ids;       // 待处理的异步请求ID
    
    // 消息计数，用于UI更新
    size_t message_count = 0;
};

// 状态管理类
class AssistantStatusManager {
private:
    AssistantStatus status;
    mutable std::mutex status_mutex;
    static AssistantStatusManager* instance;

    AssistantStatusManager() = default;

    // 获取当前时间戳字符串
    std::string get_current_timestamp();

    // 记录连接历史
    void log_connection_event(const std::string& what, const std::string& why);

    // 添加通知信息
    void add_notification(const std::string& message);

    // 将任务链字符串转换为枚举类型
    TaskChainType task_chain_string_to_type(const std::string& task_chain);

    // 将任务链枚举转换为字符串
    std::string task_chain_type_to_string(TaskChainType type);

    // 将子任务字符串转换为枚举类型
    SubTaskType sub_task_string_to_type(const std::string& task);

    // 将子任务枚举转换为显示字符串
    std::string sub_task_type_to_display_string(SubTaskType type);

public:
    // 单例获取
    static AssistantStatusManager& get_instance();

    // 禁止复制移动
    AssistantStatusManager(const AssistantStatusManager&) = delete;
    AssistantStatusManager& operator=(const AssistantStatusManager&) = delete;
    AssistantStatusManager(AssistantStatusManager&&) = delete;
    AssistantStatusManager& operator=(AssistantStatusManager&&) = delete;

    // 更新初始化失败状态
    void update_init_failed(const nlohmann::json& details);

    // 更新连接信息
    void update_connection_info(const nlohmann::json& details);

    // 更新内部错误
    void update_internal_error(const nlohmann::json& details);

    // 更新任务链状态
    void update_task_chain_status(AsstMsg msg, const nlohmann::json& details);

    // 更新子任务状态
    void update_sub_task_status(AsstMsg msg, const nlohmann::json& details);

    // 处理异步调用信息
    void handle_async_call_info(const nlohmann::json& details);

    // 处理全部任务完成：清空所有绘制用信息
    void handle_all_tasks_completed();

    // 处理实例销毁
    void handle_destroyed();

    // 增加消息计数
    void increment_message_count();

    // 获取当前状态（用于UI绘制）
    AssistantStatus get_status() const;

    // 获取任务链类型的显示名称
    std::string get_task_chain_display_name(TaskChainType type);

    // 获取子任务类型的显示名称
    std::string get_sub_task_display_name(SubTaskType type);

    // 获取任务链的运行时间（秒）
    int get_task_chain_runtime_seconds(const std::string& task_chain_id);
};

// 回调函数声明
void ASST_CALL AsstCallbackHandler(int msg, const char* details, void* custom_arg);

// 供UI绘制使用的辅助函数
const char* connection_state_to_cstr(ConnectionState state);
const char* task_chain_type_to_cstr(TaskChainType type);
const char* sub_task_type_to_cstr(SubTaskType type);
const char* get_task_chain_runtime_cstr(const std::string& task_chain_id);

// 获取状态供UI绘制
AssistantStatus get_assistant_status();
