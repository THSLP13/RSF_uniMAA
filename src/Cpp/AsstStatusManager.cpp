#pragma warning(disable: 4996)  

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <chrono>
#include <sstream>
#include "nlohmann/json.hpp"

// 调用约定宏定义
#ifndef ASST_CALL
#define ASST_CALL __stdcall
#endif

// 消息类型枚举 - 补充缺失的20002
enum class AsstMsg
{
    /* Global Info */
    InternalError = 0,           // 内部错误
    InitFailed = 1,           // 初始化失败
    ConnectionInfo = 2,           // 连接相关信息
    AllTasksCompleted = 3,           // 全部任务完成
    AsyncCallInfo = 4,           // 外部异步调用信息
    Destroyed = 5,           // 实例已销毁

    /* TaskChain Info */
    TaskChainError = 10000,      // 任务链执行/识别错误
    TaskChainStart = 10001,      // 任务链开始
    TaskChainCompleted = 10002,      // 任务链完成
    TaskChainExtraInfo = 10003,      // 任务链额外信息

    /* SubTask Info */
    SubTaskError = 20000,       // 原子任务执行/识别错误
    SubTaskStart = 20001,       // 原子任务开始
    SubTaskCompleted = 20002,       // 原子任务完成（新增）
    SubTaskExtraInfo = 20003,       // 原子任务额外信息
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
    std::string get_current_timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&now_time);
        timestamp.pop_back(); // 移除换行符
        return timestamp;
    }

    // 记录连接历史
    void log_connection_event(const std::string& what, const std::string& why) {
        status.connection_history.emplace_back(get_current_timestamp(), what + ": " + why);
        // 限制历史记录数量
        if (status.connection_history.size() > 100) {
            status.connection_history.erase(status.connection_history.begin());
        }
    }

    // 添加通知信息
    void add_notification(const std::string& message) {
        status.notifications.emplace_back(get_current_timestamp(), message);
        // 限制通知数量
        if (status.notifications.size() > 50) {
            status.notifications.erase(status.notifications.begin());
        }
    }

    // 任务链字符串转类型 - 增强容错性
    TaskChainType task_chain_string_to_type(const std::string& task_chain) {
        // 处理可能的大小写问题
        std::string lower_chain = task_chain;
        for (char& c : lower_chain) c = tolower(c);

        if (lower_chain == "startup") return TaskChainType::StartUp;
        if (lower_chain == "closedown") return TaskChainType::CloseDown;
        if (lower_chain == "fight") return TaskChainType::Fight;
        if (lower_chain == "mall") return TaskChainType::Mall;
        if (lower_chain == "recruit") return TaskChainType::Recruit;
        if (lower_chain == "infrast") return TaskChainType::Infrast;
        if (lower_chain == "award") return TaskChainType::Award;
        if (lower_chain == "roguelike") return TaskChainType::Roguelike;
        if (lower_chain == "copilot") return TaskChainType::Copilot;
        if (lower_chain == "ssscopilot") return TaskChainType::SSSCopilot;

        // 调试用：输出未识别的任务链类型
        // std::cout << "未识别的任务链类型: " << task_chain << std::endl;
        return TaskChainType::Unknown;
    }

    // 任务链枚举转字符串
    std::string task_chain_type_to_string(TaskChainType type) {
        switch (type) {
        case TaskChainType::StartUp: return "开始唤醒";
        case TaskChainType::CloseDown: return "关闭游戏";
        case TaskChainType::Fight: return "刷理智";
        case TaskChainType::Mall: return "信用点及购物";
        case TaskChainType::Recruit: return "自动公招";
        case TaskChainType::Infrast: return "基建换班";
        case TaskChainType::Award: return "领取日常奖励";
        case TaskChainType::Roguelike: return "无限刷肉鸽";
        case TaskChainType::Copilot: return "自动抄作业";
        case TaskChainType::SSSCopilot: return "自动抄保全作业";
        default: return "未知任务";
        }
    }

    // 子任务字符串转类型
    SubTaskType sub_task_string_to_type(const std::string& task) {
        if (task == "StartButton2") return SubTaskType::StartButton2;
        if (task == "MedicineConfirm") return SubTaskType::MedicineConfirm;
        if (task == "ExpiringMedicineConfirm") return SubTaskType::ExpiringMedicineConfirm;
        if (task == "StoneConfirm") return SubTaskType::StoneConfirm;
        if (task == "RecruitRefreshConfirm") return SubTaskType::RecruitRefreshConfirm;
        if (task == "RecruitConfirm") return SubTaskType::RecruitConfirm;
        if (task == "RecruitNowConfirm") return SubTaskType::RecruitNowConfirm;
        if (task == "ReportToPenguinStats") return SubTaskType::ReportToPenguinStats;
        if (task == "ReportToYituliu") return SubTaskType::ReportToYituliu;
        if (task == "InfrastDormDoubleConfirmButton") return SubTaskType::InfrastDormDoubleConfirmButton;
        if (task == "StartExplore") return SubTaskType::StartExplore;
        if (task == "StageTraderInvestConfirm") return SubTaskType::StageTraderInvestConfirm;
        if (task == "StageTraderInvestSystemFull") return SubTaskType::StageTraderInvestSystemFull;
        if (task == "ExitThenAbandon") return SubTaskType::ExitThenAbandon;
        if (task == "MissionCompletedFlag") return SubTaskType::MissionCompletedFlag;
        if (task == "MissionFailedFlag") return SubTaskType::MissionFailedFlag;
        if (task == "StageTraderEnter") return SubTaskType::StageTraderEnter;
        if (task == "StageSafeHouseEnter") return SubTaskType::StageSafeHouseEnter;
        if (task == "StageEncounterEnter") return SubTaskType::StageEncounterEnter;
        if (task == "StageCombatDpsEnter") return SubTaskType::StageCombatDpsEnter;
        if (task == "StageEmergencyDps") return SubTaskType::StageEmergencyDps;
        if (task == "StageDreadfulFoe") return SubTaskType::StageDreadfulFoe;
        if (task == "StartGameTask") return SubTaskType::StartGameTask;
        return SubTaskType::Unknown;
    }

    // 子任务枚举转显示字符串
    std::string sub_task_type_to_display_string(SubTaskType type) {
        switch (type) {
        case SubTaskType::StartButton2: return "开始战斗";
        case SubTaskType::MedicineConfirm: return "使用理智药";
        case SubTaskType::ExpiringMedicineConfirm: return "使用48小时内过期的理智药";
        case SubTaskType::StoneConfirm: return "碎石";
        case SubTaskType::RecruitRefreshConfirm: return "公招刷新标签";
        case SubTaskType::RecruitConfirm: return "公招确认招募";
        case SubTaskType::RecruitNowConfirm: return "公招使用加急许可";
        case SubTaskType::ReportToPenguinStats: return "汇报到企鹅数据统计";
        case SubTaskType::ReportToYituliu: return "汇报到一图流大数据";
        case SubTaskType::InfrastDormDoubleConfirmButton: return "基建宿舍的二次确认按钮";
        case SubTaskType::StartExplore: return "肉鸽开始探索";
        case SubTaskType::StageTraderInvestConfirm: return "肉鸽投资了源石锭";
        case SubTaskType::StageTraderInvestSystemFull: return "肉鸽投资达到了游戏上限";
        case SubTaskType::ExitThenAbandon: return "肉鸽放弃了本次探索";
        case SubTaskType::MissionCompletedFlag: return "肉鸽战斗完成";
        case SubTaskType::MissionFailedFlag: return "肉鸽战斗失败";
        case SubTaskType::StageTraderEnter: return "肉鸽关卡：诡异行商";
        case SubTaskType::StageSafeHouseEnter: return "肉鸽关卡：安全的角落";
        case SubTaskType::StageEncounterEnter: return "肉鸽关卡：不期而遇/古堡馈赠";
        case SubTaskType::StageCombatDpsEnter: return "肉鸽关卡：普通作战";
        case SubTaskType::StageEmergencyDps: return "肉鸽关卡：紧急作战";
        case SubTaskType::StageDreadfulFoe: return "肉鸽关卡：险路恶敌";
        case SubTaskType::StartGameTask: return "打开客户端失败（配置文件与传入client_type不匹配）";
        default: return "未知子任务";
        }
    }

public:
    // 单例获取
    static AssistantStatusManager& get_instance() {
        if (!instance) {
            instance = new AssistantStatusManager();
        }
        return *instance;
    }

    // 禁止复制移动
    AssistantStatusManager(const AssistantStatusManager&) = delete;
    AssistantStatusManager& operator=(const AssistantStatusManager&) = delete;
    AssistantStatusManager(AssistantStatusManager&&) = delete;
    AssistantStatusManager& operator=(AssistantStatusManager&&) = delete;

    // 更新初始化失败状态
    void update_init_failed(const nlohmann::json& details) {
        std::lock_guard<std::mutex> lock(status_mutex);
        status.initialized = false;

        if (details.contains("what")) {
            status.last_error = details["what"].get<std::string>();
        }
        if (details.contains("why")) {
            status.last_error += ": " + details["why"].get<std::string>();
        }
    }

    // 更新连接信息
    void update_connection_info(const nlohmann::json& details) {
        std::lock_guard<std::mutex> lock(status_mutex);

        std::string what, why;
        if (details.contains("what")) {
            what = details["what"].get<std::string>();
            status.connection_what = what;
        }
        if (details.contains("why")) {
            why = details["why"].get<std::string>();
            status.connection_why = why;
        }

        if (details.contains("uuid")) {
            status.device_uuid = details["uuid"].get<std::string>();
        }

        if (details.contains("details")) {
            auto& details_obj = details["details"];
            if (details_obj.contains("adb")) {
                status.adb_path = details_obj["adb"].get<std::string>();
            }
            if (details_obj.contains("address")) {
                status.device_address = details_obj["address"].get<std::string>();
            }
            if (details_obj.contains("config")) {
                status.connection_config = details_obj["config"].get<std::string>();
            }
        }

        // 更新连接状态
        if (what == "ConnectFailed") {
            status.connection_state = ConnectionState::ConnectionFailed;
            status.reconnect_attempts = 0;
        }
        else if (what == "Connected") {
            status.connection_state = ConnectionState::Connected;
            status.reconnect_attempts = 0;
        }
        else if (what == "UuidGot") {
            status.connection_state = ConnectionState::UuidObtained;
        }
        else if (what == "UnsupportedResolution") {
            status.connection_state = ConnectionState::UnsupportedResolution;
        }
        else if (what == "ResolutionError") {
            status.connection_state = ConnectionState::ResolutionError;
        }
        else if (what == "Reconnecting") {
            status.connection_state = ConnectionState::Reconnecting;
            status.reconnect_attempts++;
        }
        else if (what == "Reconnected") {
            status.connection_state = ConnectionState::Connected;
            status.reconnect_attempts = 0;
        }
        else if (what == "Disconnect") {
            status.connection_state = ConnectionState::Disconnect;
            status.reconnect_attempts = 0;
        }
        else if (what == "ScreencapFailed") {
            status.connection_state = ConnectionState::ScreencapFailed;
        }
        else if (what == "TouchModeNotAvailable") {
            status.connection_state = ConnectionState::TouchModeNotAvailable;
        }

        log_connection_event(what, why);
    }

    // 更新内部错误
    void update_internal_error(const nlohmann::json& details) {
        std::lock_guard<std::mutex> lock(status_mutex);
        std::string error_msg = "Internal Error";

        if (details.contains("why")) {
            error_msg += ": " + details["why"].get<std::string>();
        }

        status.last_error = error_msg;
    }

    // 更新任务链状态
    void update_task_chain_status(AsstMsg msg, const nlohmann::json& details) {
        std::lock_guard<std::mutex> lock(status_mutex);
        std::string task_chain_id;
        std::string task_chain_name;

        // 从 taskid（int）转为字符串作为任务链ID
        if (details.contains("taskid")) {
            task_chain_id = std::to_string(details["taskid"].get<int>());
        }
        // 从 taskchain 字段获取任务链名称（修正核心）
        if (details.contains("taskchain")) {
            task_chain_name = details["taskchain"].get<std::string>();
        }

        // 确保任务链条目存在
        if (!status.task_chains.count(task_chain_id)) {
            TaskChainStatus new_chain;
            new_chain.id = task_chain_id;
            new_chain.type = task_chain_string_to_type(task_chain_name);
            status.task_chains[task_chain_id] = new_chain;
        }


        TaskChainStatus& chain = status.task_chains[task_chain_id];

        // 更新任务链类型（如果提供了新的类型信息）
        if (!task_chain_name.empty()) {
            chain.type = task_chain_string_to_type(task_chain_name);
        }

        switch (msg) {
        case AsstMsg::TaskChainStart:
            if (!chain.active) {
                status.active_task_count++;
            }
            chain.active = true;
            chain.start_time = std::chrono::system_clock::now();
            chain.error_msg.clear();
            break;

        case AsstMsg::TaskChainCompleted:
            if (chain.active) {
                status.active_task_count--;
            }
            chain.active = false;
            chain.end_time = std::chrono::system_clock::now();
            chain.error_msg.clear();
            break;

        case AsstMsg::TaskChainError:
            if (chain.active) {
                status.active_task_count--;
            }
            chain.active = false;
            chain.end_time = std::chrono::system_clock::now();
            if (details.contains("why")) {
                chain.error_msg = details["why"].get<std::string>();
            }
            break;

        case AsstMsg::TaskChainExtraInfo:
            // 累加额外信息
            if (details.contains("details")) {
                std::string info = details["details"].get<std::string>();
                if (!chain.extra_info.empty()) {
                    chain.extra_info = "{}";
                }
                chain.extra_info = info;
            }
            break;

        default:
            break;
        }
    }

    // 更新子任务状态 - 增加对20002(SubTaskCompleted)的处理
    void update_sub_task_status(AsstMsg msg, const nlohmann::json& details) {
    std::lock_guard<std::mutex> lock(status_mutex);

    // 存储通用信息
    std::string task_chain_id;
    std::string task_chain_name;
    std::string sub_task_class;

    // 从顶层字段解析公共信息（两种回调都可能包含）
    if (details.contains("taskid")) {
        task_chain_id = std::to_string(details["taskid"].get<int>());
    }
    if (details.contains("taskchain")) {
        task_chain_name = details["taskchain"].get<std::string>();
    }
    if (details.contains("class")) {
        sub_task_class = details["class"].get<std::string>();
    }

    // 1. 处理 SubTaskStart/Completed/Error 回调（subtask类型）
    if (msg == AsstMsg::SubTaskStart || msg == AsstMsg::SubTaskCompleted || msg == AsstMsg::SubTaskError) {
        // 检查subtask类型回调必需的字段
        if (!details.contains("details") || !details["details"].contains("task")) {
            return;
        }
        
        // 从 details.details.task 获取子任务名称
        std::string task_name = details["details"]["task"].get<std::string>();
        SubTaskType task_type = sub_task_string_to_type(task_name);

        // 确保任务链存在
        if (!task_chain_id.empty() && !status.task_chains.count(task_chain_id)) {
            TaskChainStatus new_chain;
            new_chain.id = task_chain_id;
            new_chain.type = task_chain_string_to_type(task_chain_name);
            status.task_chains[task_chain_id] = new_chain;
        }

        // 处理子任务状态
        if (!task_chain_id.empty() && status.task_chains.count(task_chain_id)) {
            TaskChainStatus& chain = status.task_chains[task_chain_id];
            
            // 确保子任务存在
            if (!chain.sub_tasks.count(task_name)) {
                SubTaskStatus new_sub_task;
                new_sub_task.name = task_name;
                new_sub_task.type = task_type;
                chain.sub_tasks[task_name] = new_sub_task;
            }

            SubTaskStatus& sub_task = chain.sub_tasks[task_name];

            // 更新子任务状态
            switch (msg) {
            case AsstMsg::SubTaskStart:
                sub_task.start_time = std::chrono::system_clock::now();
                sub_task.completed = false;
                sub_task.error_msg.clear();
                break;

            case AsstMsg::SubTaskCompleted:
                sub_task.completed = true;
                sub_task.end_time = std::chrono::system_clock::now();
                break;

            case AsstMsg::SubTaskError:
                sub_task.completed = true;
                sub_task.end_time = std::chrono::system_clock::now();
                if (details["details"].contains("why")) {
                    sub_task.error_msg = details["details"]["why"].get<std::string>();
                }
                break;

            default:
                break;
            }
        }

        // 更新全局当前子任务信息
        switch (msg) {
        case AsstMsg::SubTaskStart:
            status.current_sub_task = task_name;
            status.current_sub_task_type = task_type;
            break;

        case AsstMsg::SubTaskCompleted:
            if (status.current_sub_task == task_name) {
                status.current_sub_task.clear();
                status.current_sub_task_type = SubTaskType::Unknown;
            }
            break;

        case AsstMsg::SubTaskError:
            if (details["details"].contains("why")) {
                status.last_sub_task_error = sub_task_type_to_display_string(task_type) + ": " + details["details"]["why"].get<std::string>();
            }
            if (status.current_sub_task == task_name) {
                status.current_sub_task.clear();
                status.current_sub_task_type = SubTaskType::Unknown;
            }
            break;

        default:
            break;
        }
    }
    // 2. 处理 SubTaskExtraInfo 回调（单独的信息回调）
    else if (msg == AsstMsg::SubTaskExtraInfo) {
        // 检查ExtraInfo必需的字段
        if (!details.contains("details")) {
            return;
        }

        // 从class字段获取子任务类型（ExtraInfo没有task字段，用class标识）
        SubTaskType task_type = sub_task_string_to_type(sub_task_class);
        // 子任务名称使用class字段（ExtraInfo无task字段）
        std::string task_name = sub_task_class;

        // 确保任务链存在
        if (!task_chain_id.empty() && !status.task_chains.count(task_chain_id)) {
            TaskChainStatus new_chain;
            new_chain.id = task_chain_id;
            new_chain.type = task_chain_string_to_type(task_chain_name);
            status.task_chains[task_chain_id] = new_chain;
        }

        // 处理额外信息
        if (!task_chain_id.empty() && status.task_chains.count(task_chain_id)) {
            TaskChainStatus& chain = status.task_chains[task_chain_id];
            
            // 确保子任务存在
            if (!chain.sub_tasks.count(task_name)) {
                SubTaskStatus new_sub_task;
                new_sub_task.name = task_name;
                new_sub_task.type = task_type;
                chain.sub_tasks[task_name] = new_sub_task;
            }

            SubTaskStatus& sub_task = chain.sub_tasks[task_name];

            // 存储原始details信息（原样传递）
            std::string info_str;
            if (details["details"].is_object()) {
                info_str = details["details"].dump(2); // 格式化JSON
            } else {
                info_str = details["details"].get<std::string>();
            }
            sub_task.extra_info = info_str;

            //BattleFormationOperUnavailable
            if(details["what"]=="BattleFormationOperUnavailable") {
                sub_task.error_msg = "干员";
                sub_task.error_msg += details["details"]["oper_name"].get<std::string>();
                sub_task.error_msg += " 不满足 ";
                sub_task.error_msg += details["details"]["requirement_type"].get<std::string>();
                status.last_sub_task_error = sub_task_type_to_display_string(task_type) + ": " + sub_task.error_msg;
                add_notification(sub_task.error_msg);
            }

            // 特殊子任务通知（如基建宿舍冲突）
            if (task_type == SubTaskType::InfrastDormDoubleConfirmButton) {
                add_notification("注意：基建宿舍存在干员冲突，需要二次确认！");
            }
        }
    }
}
    

    // 处理异步调用信息
    void handle_async_call_info(const nlohmann::json& details) {
        std::lock_guard<std::mutex> lock(status_mutex);
        AsyncCallRecord record;

        // 解析基础字段
        if (details.contains("uuid")) {
            record.uuid = details["uuid"].get<std::string>();
        }
        if (details.contains("what")) {
            record.what = details["what"].get<std::string>();
        }
        if (details.contains("async_call_id")) {
            record.async_call_id = details["async_call_id"].get<int>();
        }

        // 解析详情字段
        if (details.contains("details")) {
            auto& details_obj = details["details"];
            if (details_obj.contains("ret")) {
                record.ret = details_obj["ret"].get<bool>();
            }
            if (details_obj.contains("cost")) {
                // 将int64转换为int存储
                int64_t cost_val = details_obj["cost"].get<int64_t>();
                record.cost = static_cast<int>(cost_val);
            }
        }

        // 设置时间戳
        record.timestamp = get_current_timestamp();

        // 添加到记录列表
        status.async_call_records.push_back(record);

        // 移除已完成的请求ID
        if (record.async_call_id != -1) {
            status.pending_async_ids.erase(record.async_call_id);
        }

        // 限制记录数量
        if (status.async_call_records.size() > 50) {
            status.async_call_records.erase(status.async_call_records.begin());
        }
    }

    // 处理全部任务完成：清空所有绘制用信息
    void handle_all_tasks_completed() {
        std::lock_guard<std::mutex> lock(status_mutex);
        status.all_tasks_completed = true;

        // 清空任务相关信息
        status.task_chains.clear();
        status.active_task_count = 0;
        status.current_sub_task.clear();
        status.current_sub_task_type = SubTaskType::Unknown;
        //status.last_sub_task_error.clear();

        // 清空异步调用记录
        status.async_call_records.clear();
        status.pending_async_ids.clear();

        // 保留连接信息但重置任务完成状态
        status.last_error.clear();
    }

    // 处理实例销毁
    void handle_destroyed() {
        std::lock_guard<std::mutex> lock(status_mutex);
        status.destroyed = true;
        status.connection_state = ConnectionState::Disconnected;
        status.task_chains.clear();
        status.active_task_count = 0;
    }

    // 增加消息计数
    void increment_message_count() {
        std::lock_guard<std::mutex> lock(status_mutex);
        status.message_count++;
    }

    // 获取当前状态（用于UI绘制）
    AssistantStatus get_status() const {
        std::lock_guard<std::mutex> lock(status_mutex);
        return status; // 返回副本保证线程安全
    }

    // 获取任务链类型的显示名称
    std::string get_task_chain_display_name(TaskChainType type) {
        return task_chain_type_to_string(type);
    }

    // 获取子任务类型的显示名称
    std::string get_sub_task_display_name(SubTaskType type) {
        return sub_task_type_to_display_string(type);
    }

    // 获取任务链的运行时间（秒）
    int get_task_chain_runtime_seconds(const std::string& task_chain_id) {
        std::lock_guard<std::mutex> lock(status_mutex);
        if (!status.task_chains.count(task_chain_id)) {
            return 0;
        }

        const TaskChainStatus& chain = status.task_chains[task_chain_id];
        // 检查开始时间是否有效（未初始化的时间点会是_epoch）
        if (chain.start_time == std::chrono::system_clock::time_point()) {
            return 0;
        }

        std::chrono::system_clock::time_point end_time = chain.end_time;
        // 如果任务仍活跃，使用当前时间作为结束时间
        if (chain.active) {
            end_time = std::chrono::system_clock::now();
        }
        // 确保结束时间不早于开始时间
        if (end_time < chain.start_time) {
            return 0;
        }

        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                end_time - chain.start_time
            ).count()
            );
    }
};

// 初始化单例实例
AssistantStatusManager* AssistantStatusManager::instance = nullptr;

// 回调函数实现 - 忽略未处理的消息类型，不发出警告
void ASST_CALL AsstCallbackHandler(int msg, const char* details, void* custom_arg) {
    AssistantStatusManager::get_instance().increment_message_count();
    AsstMsg message_type = static_cast<AsstMsg>(msg);
    nlohmann::json details_json;

    // 解析JSON详情
    if (details && *details != '\0') {
        try {
            details_json = nlohmann::json::parse(details);
        }
        catch (const nlohmann::json::parse_error& e) {
            AssistantStatusManager::get_instance().update_internal_error(
                nlohmann::json{ {"why", "Failed to parse details: " + std::string(e.what())} }
            );
            return;
        }
    }

    // 消息分发处理 - 只处理已知类型，其他类型不处理也不警告
    switch (message_type) {
    case AsstMsg::InternalError:
        AssistantStatusManager::get_instance().update_internal_error(details_json);
        break;
    case AsstMsg::InitFailed:
        AssistantStatusManager::get_instance().update_init_failed(details_json);
        break;
    case AsstMsg::ConnectionInfo:
        AssistantStatusManager::get_instance().update_connection_info(details_json);
        break;
    case AsstMsg::AllTasksCompleted:
        AssistantStatusManager::get_instance().handle_all_tasks_completed();
        break;
    case AsstMsg::AsyncCallInfo:
        AssistantStatusManager::get_instance().handle_async_call_info(details_json);
        break;
    case AsstMsg::Destroyed:
        AssistantStatusManager::get_instance().handle_destroyed();
        break;
    case AsstMsg::TaskChainError:
    case AsstMsg::TaskChainStart:
    case AsstMsg::TaskChainCompleted:
    case AsstMsg::TaskChainExtraInfo:
        AssistantStatusManager::get_instance().update_task_chain_status(message_type, details_json);
        break;
    case AsstMsg::SubTaskError:
    case AsstMsg::SubTaskStart:
    case AsstMsg::SubTaskCompleted:  // 处理新增的子任务完成消息
    case AsstMsg::SubTaskExtraInfo:
        AssistantStatusManager::get_instance().update_sub_task_status(message_type, details_json);
        break;
        // 其他消息类型不处理，也不发出警告
    default:
        break;
    }
}

// C风格字符串转换函数 - 供ImGui使用
const char* connection_state_to_cstr(ConnectionState state) {
    switch (state) {
    case ConnectionState::Disconnected: return "未连接";
    case ConnectionState::Connecting: return "连接中";
    case ConnectionState::Connected: return "已连接";
    case ConnectionState::UuidObtained: return "已获取设备ID";
    case ConnectionState::Reconnecting: return "重连中";
    case ConnectionState::ConnectionFailed: return "连接失败";
    case ConnectionState::UnsupportedResolution: return "分辨率不支持";
    case ConnectionState::ResolutionError: return "分辨率获取错误";
    case ConnectionState::Disconnect: return "连接断开";
    case ConnectionState::ScreencapFailed: return "截图失败";
    case ConnectionState::TouchModeNotAvailable: return "不支持的触控模式";
    default: return "未知状态";
    }
}

const char* task_chain_type_to_cstr(TaskChainType type) {
    switch (type) {
    case TaskChainType::StartUp: return "开始唤醒";
    case TaskChainType::CloseDown: return "关闭游戏";
    case TaskChainType::Fight: return "刷理智";
    case TaskChainType::Mall: return "信用点及购物";
    case TaskChainType::Recruit: return "自动公招";
    case TaskChainType::Infrast: return "基建换班";
    case TaskChainType::Award: return "领取日常奖励";
    case TaskChainType::Roguelike: return "无限刷肉鸽";
    case TaskChainType::Copilot: return "自动抄作业";
    case TaskChainType::SSSCopilot: return "自动抄保全作业";
    default: return "未知任务";
    }
}

const char* sub_task_type_to_cstr(SubTaskType type) {
    switch (type) {
    case SubTaskType::StartButton2: return "开始战斗";
    case SubTaskType::MedicineConfirm: return "使用理智药";
    case SubTaskType::ExpiringMedicineConfirm: return "使用48小时内过期的理智药";
    case SubTaskType::StoneConfirm: return "碎石";
    case SubTaskType::RecruitRefreshConfirm: return "公招刷新标签";
    case SubTaskType::RecruitConfirm: return "公招确认招募";
    case SubTaskType::RecruitNowConfirm: return "公招使用加急许可";
    case SubTaskType::ReportToPenguinStats: return "汇报到企鹅数据统计";
    case SubTaskType::ReportToYituliu: return "汇报到一图流大数据";
    case SubTaskType::InfrastDormDoubleConfirmButton: return "基建宿舍的二次确认按钮";
    case SubTaskType::StartExplore: return "肉鸽开始探索";
    case SubTaskType::StageTraderInvestConfirm: return "肉鸽投资了源石锭";
    case SubTaskType::StageTraderInvestSystemFull: return "肉鸽投资达到了游戏上限";
    case SubTaskType::ExitThenAbandon: return "肉鸽放弃了本次探索";
    case SubTaskType::MissionCompletedFlag: return "肉鸽战斗完成";
    case SubTaskType::MissionFailedFlag: return "肉鸽战斗失败";
    case SubTaskType::StageTraderEnter: return "肉鸽关卡：诡异行商";
    case SubTaskType::StageSafeHouseEnter: return "肉鸽关卡：安全的角落";
    case SubTaskType::StageEncounterEnter: return "肉鸽关卡：不期而遇/古堡馈赠";
    case SubTaskType::StageCombatDpsEnter: return "肉鸽关卡：普通作战";
    case SubTaskType::StageEmergencyDps: return "肉鸽关卡：紧急作战";
    case SubTaskType::StageDreadfulFoe: return "肉鸽关卡：险路恶敌";
    case SubTaskType::StartGameTask: return "打开客户端失败";
    default: return "未知子任务";
    }
}

const char* get_task_chain_runtime_cstr(const std::string& task_chain_id) {
    static thread_local char buffer[32]; // 线程局部静态缓冲区，确保线程安全
    int seconds = AssistantStatusManager::get_instance().get_task_chain_runtime_seconds(task_chain_id);

    if (seconds <= 0) {
        strcpy(buffer, "0s");
        return buffer;
    }

    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;

    int offset = 0;
    if (hours > 0) {
        offset += sprintf(buffer + offset, "%dh", hours);
    }
    if (minutes > 0) {
        offset += sprintf(buffer + offset, "%dm", minutes);
    }
    offset += sprintf(buffer + offset, "%ds", seconds);

    return buffer;
}

// 获取状态供UI绘制
AssistantStatus get_assistant_status() {
    return AssistantStatusManager::get_instance().get_status();
}
