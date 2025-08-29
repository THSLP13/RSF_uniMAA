#pragma warning(disable: 4996)  

#define IMGUI_USE_STD_STRING
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"

#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <Windows.h>

#include <filesystem>
#include <stdexcept>

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <vector>
#include <map>
#include <cstring>
#include <iomanip>
#include <tlhelp32.h>
#include <psapi.h>
#include <array>
#include <memory>
#include "winternl.h"
#include "atlbase.h"
#include "atlstr.h"
#include <atomic>
#include <codecvt>
#include <fcntl.h>
#include <functional>
#include <io.h>
#include <iostream>
#include <mutex>
#include <queue>
#include <shellapi.h>
#include <thread>

#pragma comment(lib, "ntdll.lib")

#include "main.h"
#include "AsstCaller.h"
#include "AsstStatusManager.h"

// 确保已链接这些库
#pragma comment(lib, "opencv_world4.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// 全局变量
std::mutex g_imageMutex;
std::queue<std::vector<unsigned char>> g_imageQueue;
std::atomic<bool> g_threadRunning(false);
std::thread g_captureThread;
ID3D11ShaderResourceView* g_screenTexture = nullptr;
ID3D11Texture2D* g_screenTextureResource = nullptr;
std::atomic<bool> g_newImageAvailable(false);
std::atomic<bool> g_textureUpdateRequested(false);
cv::Mat g_currentScreenImage;
int g_textureWidth = 0;
int g_textureHeight = 0;


AsstHandle asstPtr = nullptr;

using json = nlohmann::json;


#include <regex>
#include <urlmon.h>
#include <ShlObj.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")

// 全局状态变量
struct AutoBattleState {
    char codeInput[128] = "";
    char filePath[512] = "";
    bool showDetails = false;
    json copilotInfo;
    json copilotContent;
    bool autoFormation = false;
    bool ignoreRequirements = false;
    bool addLowTrust = false;
};

AutoBattleState g_autoBattleState;
static bool showAutoBattleWindow = false;

// 辅助函数：检查字符串是否为纯数字
bool isAllDigits(const std::string& s) {
    return std::all_of(s.begin(), s.end(), ::isdigit);
}

// 辅助函数：下载文件
bool downloadFile(const std::string& url, const std::string& savePath) {
    // 创建cache目录（如果不存在）
    fs::create_directories(fs::path(savePath).parent_path());

    HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), savePath.c_str(), 0, NULL);
    return hr == S_OK;
}

// 常量定义
#define MAX_CODE_LENGTH 32
#define MAX_ITEMID_LENGTH 64
#define MAX_NAME_LENGTH 128
#define MAX_DESCRIPTION_LENGTH 512
#define SEARCH_QUERY_LENGTH 128

// 掉落信息结构体
struct DropInfo {
    char dropType[32];
    char itemId[MAX_ITEMID_LENGTH];
};

// 关卡信息结构体
struct StageInfo {
    int apCost;
    char code[MAX_CODE_LENGTH];
    char stageId[64];
    std::vector<DropInfo> dropInfos;
};

// 物品信息结构体
struct ItemInfo {
    char name[MAX_NAME_LENGTH];
    char description[MAX_DESCRIPTION_LENGTH];
};

// 全局数据存储
std::vector<StageInfo> all_stages;
std::unordered_map<const char*, ItemInfo> item_index;  // 使用const char*作为键
char search_query[SEARCH_QUERY_LENGTH] = "";
std::vector<StageInfo> filtered_stages;
bool show_drop_popup = false;
const StageInfo* current_stage = nullptr;

// 辅助函数：字符串转小写
void to_lower(const char* src, char* dest) {
    while (*src) {
        //*dest++ = tolower((unsigned char)*src++);
        *dest++ = (unsigned char)*src++;
    }
    *dest = '\0';
}
// 辅助函数：检查字符串是否包含指定子串
static bool string_contains(const char* str, const char* substr) {
    return strstr(str, substr) != nullptr;
}

// 加载关卡数据
bool load_stages() {
    std::ifstream file("resource/stages.json");
    if (!file.is_open()) return false;

    json j;
    file >> j;

    all_stages.clear();
    for (const auto& entry : j) {
        StageInfo stage;
        stage.apCost = entry["apCost"].get<int>();

        // 复制字符串
        strncpy(stage.code, entry["code"].get<std::string>().c_str(), MAX_CODE_LENGTH - 1);
        stage.code[MAX_CODE_LENGTH - 1] = '\0';

        strncpy(stage.stageId, entry["stageId"].get<std::string>().c_str(), 63);
        stage.stageId[63] = '\0';

        // 处理掉落信息
        for (const auto& drop : entry["dropInfos"]) {
            DropInfo di;
            strncpy(di.dropType, drop["dropType"].get<std::string>().c_str(), 31);
            di.dropType[31] = '\0';

            strncpy(di.itemId, drop["itemId"].get<std::string>().c_str(), MAX_ITEMID_LENGTH - 1);
            di.itemId[MAX_ITEMID_LENGTH - 1] = '\0';

            stage.dropInfos.push_back(di);
        }

        all_stages.push_back(stage);
    }

    return true;
}

// 加载物品索引（修复物品查询问题）
static bool load_item_index() {
    std::ifstream file("resource/item_index.json");
    if (!file.is_open()) return false;

    json j;
    try {
        file >> j;
    }
    catch (const std::exception& e) {
        // 输出JSON解析错误信息
        printf("解析item_index.json失败: %s\n", e.what());
        return false;
    }

    // 清理之前的数据
    for (auto& pair : item_index) {
        delete[] pair.first;
    }
    item_index.clear();

    for (const auto& entry : j.items()) {
        ItemInfo item;
        const std::string& key = entry.key();

        // 确保名称字段存在
        if (!entry.value().contains("name")) {
            printf("物品 %s 缺少name字段\n", key.c_str());
            continue;
        }

        // 复制物品名称
        const std::string& name = entry.value()["name"].get<std::string>();
        strncpy(item.name, name.c_str(), MAX_NAME_LENGTH - 1);
        item.name[MAX_NAME_LENGTH - 1] = '\0';

        // 修复description字段解析（处理非字符串类型）
        if (entry.value().contains("description")) {
            const auto& desc_element = entry.value()["description"];
            // 先检查是否为字符串类型
            if (desc_element.is_string()) {
                const std::string& desc = desc_element.get<std::string>();
                strncpy(item.description, desc.c_str(), MAX_DESCRIPTION_LENGTH - 1);
                item.description[MAX_DESCRIPTION_LENGTH - 1] = '\0';
            }
            // 处理null类型
            else if (desc_element.is_null()) {
                item.description[0] = '\0';
                printf("物品 %s 的description字段为null\n", key.c_str());
            }
            // 处理其他非字符串类型
            else {
                const std::string desc = desc_element.dump(); // 转换为JSON字符串表示
                strncpy(item.description, desc.c_str(), MAX_DESCRIPTION_LENGTH - 1);
                item.description[MAX_DESCRIPTION_LENGTH - 1] = '\0';
                printf("物品 %s 的description字段类型不是字符串，已转换为JSON表示\n", key.c_str());
            }
        }
        else {
            item.description[0] = '\0';
        }

        // 分配键的内存并存储
        char* key_str = new char[key.length() + 1];
        strcpy(key_str, key.c_str());
        item_index[key_str] = item;
    }

    return true;
}

// 辅助函数：查找物品信息（新增，修复查询问题）
static const ItemInfo* find_item(const char* itemId) {
    // 遍历哈希表查找物品（修复哈希表查找问题）
    for (const auto& pair : item_index) {
        if (strcmp(pair.first, itemId) == 0) {
            return &pair.second;
        }
    }
    // 调试信息：输出未找到的物品ID
    printf("未找到物品: %s\n", itemId);
    return nullptr;
}


// 过滤关卡
void filter_stages(const char* query) {
    filtered_stages.clear();
    if (strlen(query) == 0) {
        return;
    }

    char lower_query[SEARCH_QUERY_LENGTH];
    char lower_code[MAX_CODE_LENGTH];

    to_lower(query, lower_query);

    for (const auto& stage : all_stages) {
        to_lower(stage.code, lower_code);

        if (strstr(lower_code, lower_query) != nullptr) {
            filtered_stages.push_back(stage);
        }
    }
}

#include <WinINet.h>
#include <chrono>
#include <sstream>
#pragma comment(lib, "wininet.lib")

// 辅助常量与状态变量
namespace {
    const std::string PENGUIN_FILE = "cache/penguin-stats.json";
    const std::string PENGUIN_URL = "https://penguin-stats.io/PenguinStats/api/v2/_private/result/matrix/CN/global/all";
    const int MAX_DOWNLOAD_TIMEOUT = 30000; // 30秒超时
    const int64_t ONE_DAY_MS = 86400000;   // 一天的毫秒数

    // 下载状态
    enum class DownloadStatus {
        Idle,
        Downloading,
        Complete,
        Failed,
        Cancelled
    };

    // 线程安全的下载状态管理
    struct DownloadState {
        std::mutex mtx;
        DownloadStatus status = DownloadStatus::Idle;
        int progress = 0;
        std::string error_msg;
    };

    static DownloadState download_state;
    static std::thread download_thread;
}

// 辅助函数：检查文件是否存在且未过期（修正版）
static bool is_file_valid() {
    namespace fs = std::filesystem;
    try {
        if (!fs::exists(PENGUIN_FILE)) return false;

        // 获取文件最后修改时间
        auto last_write = fs::last_write_time(PENGUIN_FILE);

        // 将文件时间转换为system_clock时间（兼容不同时钟类型）
        auto file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            last_write - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );

        // 计算与当前时间的差值（毫秒）
        auto now = std::chrono::system_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - file_time).count();

        return diff <= ONE_DAY_MS;
    }
    catch (...) {
        return false;
    }
}

// 辅助函数：HTTP下载文件（使用WinINet，MSVC自带）
static void download_file() {
    HINTERNET hInternet = InternetOpenA("StageEditor/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        std::lock_guard<std::mutex> lock(download_state.mtx);
        download_state.status = DownloadStatus::Failed;
        download_state.error_msg = "初始化网络失败";
        return;
    }

    HINTERNET hConnect = InternetOpenUrlA(hInternet, PENGUIN_URL.c_str(), NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        std::lock_guard<std::mutex> lock(download_state.mtx);
        download_state.status = DownloadStatus::Failed;
        download_state.error_msg = "连接服务器失败";
        return;
    }

    // 创建缓存目录
    std::filesystem::create_directories("cache");
    std::ofstream file(PENGUIN_FILE, std::ios::binary);
    if (!file.is_open()) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        std::lock_guard<std::mutex> lock(download_state.mtx);
        download_state.status = DownloadStatus::Failed;
        download_state.error_msg = "无法创建文件";
        return;
    }

    // 下载数据并更新进度
    char buffer[4096];
    DWORD bytes_read;
    DWORD file_size = 0;
    DWORD content_length = 0;
    DWORD len = sizeof(content_length);

    // 获取文件总大小
    HttpQueryInfoA(hConnect, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
        &content_length, &len, NULL);

    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
        // 检查是否取消下载
        {
            std::lock_guard<std::mutex> lock(download_state.mtx);
            if (download_state.status == DownloadStatus::Cancelled) {
                file.close();
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                std::filesystem::remove(PENGUIN_FILE);
                return;
            }
        }

        file.write(buffer, bytes_read);
        file_size += bytes_read;

        // 更新进度
        if (content_length > 0) {
            std::lock_guard<std::mutex> lock(download_state.mtx);
            download_state.progress = static_cast<int>((file_size * 100) / content_length);
        }
    }

    file.close();
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    std::lock_guard<std::mutex> lock(download_state.mtx);
    if (file_size == 0) {
        download_state.status = DownloadStatus::Failed;
        download_state.error_msg = "下载内容为空";
        std::filesystem::remove(PENGUIN_FILE);
    }
    else {
        download_state.status = DownloadStatus::Complete;
    }
}

// 辅助函数：时间戳转换（毫秒级时间戳转time_t）
static time_t timestamp_to_timet(int64_t ms_timestamp) {
    return static_cast<time_t>(ms_timestamp / 1000);
}

// 辅助函数：获取当前时间戳（秒）
static time_t get_current_time() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

// 字符串缓冲区大小定义
const int STR_BUFFER_SIZE = 256;
const int STAGE_BUFFER_SIZE = 128;
const int ACCOUNT_BUFFER_SIZE = 128;
const int PENGUIN_ID_BUFFER_SIZE = 64;
const int YITULIU_ID_BUFFER_SIZE = 64;
const int TAG_BUFFER_SIZE = 128;
const int FACILITY_BUFFER_SIZE = 64;  // 设施名称缓冲区大小

// 第一份配置：启动参数
struct StartupConfig {
    bool enable = true;
    char client_type[STR_BUFFER_SIZE] = { 0 };
    bool start_game_enabled = true;
    char account_name[ACCOUNT_BUFFER_SIZE] = { 0 };
    bool ldExtraEnable = false;
    int ldExtraID = -1;
    char ldExtraPathToConsole[256] = { 0 };
    char netaddr[128] = { 0 };
};

// 第二份配置：关卡任务参数
struct StageConfig {
    bool enable = true;
    char stage[STAGE_BUFFER_SIZE] = { 0 };
    int medicine = 0;
    int expiring_medicine = 0;
    int stone = 0;
    int times = 255;
    int series = -1;
    std::map<std::string, int> drops;
    bool report_to_penguin = false;
    char penguin_id[PENGUIN_ID_BUFFER_SIZE] = { 0 };
    char server[STR_BUFFER_SIZE] = "CN";
    char client_type[STR_BUFFER_SIZE] = { 0 };
    bool DrGrandet = false;
};

// 第三份配置：公招参数
struct RecruitmentConfig {
    bool enable = true;
    bool refresh = true;  // 永远为true且不提供设置
    std::vector<int> select = { 3, 4 };  // 必选，默认3,4
    std::vector<int> confirm = { 3, 4 };  // 必选，默认3,4
    std::vector<std::string> first_tags;  // 留空
    int extra_tags_mode = 0;  // 0-默认，1-选3个，2-更多高星组合
    int times = 3;  // 默认3
    bool set_time = true;  // 永远为true且不提供设置
    bool expedite = false;
    int expedite_times = 0;  // 加急次数，expedite为true时有效
    bool skip_robot = true;  // 默认跳过
    std::map<std::string, int> recruitment_time;  // 空数组
    bool report_to_penguin = false;
    char penguin_id[PENGUIN_ID_BUFFER_SIZE] = { 0 };
    bool report_to_yituliu = false;
    char yituliu_id[YITULIU_ID_BUFFER_SIZE] = { 0 };
    char server[STR_BUFFER_SIZE] = "CN";
    char client_type[STR_BUFFER_SIZE] = { 0 };
};

// 第四份配置：设施换班参数
struct FacilityConfig {
    bool enable = true;
    int mode = 0;  // 模式
    std::vector<std::string> facility;  // 要换班的设施（有序）
    std::string drones = "_NotUse";  // 无人机用途
    float threshold = 0.3f;  // 工作心情阈值
    bool replenish = false;  // 贸易站自动补货
    bool dorm_notstationed_enabled = false;  // 宿舍“未进驻”选项
    bool dorm_trust_enabled = false;  // 宿舍填入信赖未满干员
};

// 新增：商店购物配置
struct ShoppingConfig {
    bool enable = true;                // 是否启用本任务
    bool shopping = true;              // 是否购物
    bool only_buy_discount = false;    // 是否只购买折扣物品
    bool reserve_max_credit = false;   // 信用点低于300时停止购买
};

// 新增：任务奖励配置
struct MissionConfig {
    bool enable = true;     // 是否启用本任务
    bool award = true;      // 领取每日/每周任务奖励
};

// 嵌套结构体：奖励配置列表
struct CollectibleStartList {
    bool hot_water = false;    // 热水壶奖励（通用）
    bool shield = false;       // 护盾奖励（通用）
    bool ingot = false;        // 源石锭奖励（通用）
    bool hope = false;         // 希望奖励（JieGarden主题不可用）
    bool random = false;       // 随机奖励（通用）
    bool key = false;          // 钥匙奖励（仅Mizuki）
    bool dice = false;         // 骰子奖励（仅Mizuki）
    bool ideas = false;        // 2构想奖励（仅Sarkaz）
};

// 肉鸽模式主配置结构体
struct RoguelikeConfig {
    // 基础配置
    bool enable = true;                          // 是否启用本任务
    char theme[32] = "Phantom";                  // 主题（C风格字符串）
    int mode = 0;                                // 模式
    char squad[128] = "指挥分队";                // 开局分队名（C风格字符串）
    char roles[128] = "先手必胜";                // 开局职业组（C风格字符串）
    char core_char[128] = "";                    // 开局干员名（C风格字符串）
    bool use_support = false;                    // 是否使用助战干员
    bool use_nonfriend_support = false;          // 是否允许非好友助战

    // 运行控制
    int starts_count = 999;                      // 开始探索次数
    int difficulty = 0;                          // 指定难度等级（除Phantom外有效）
    bool stop_at_final_boss = false;             // 第五层前停止（除Phantom外有效）
    bool stop_at_max_level = false;              // 等级满后停止

    // 投资相关
    bool investment_enabled = true;              // 是否投资源石锭
    int investments_count = INT_MAX;             // 投资次数
    bool stop_when_investment_full = false;      // 投资上限后停止
    bool investment_with_more_score = false;     // 投资后尝试购物（仅模式1）

    // 凹开局相关（仅模式4有效）
    bool start_with_elite_two = false;           // 凹干员精二直升
    bool only_start_with_elite_two = false;      // 只凹精二直升（需start_with_elite_two为true）
    CollectibleStartList collectible_mode_start_list;  // 凹开局期望奖励

    // 主题专属配置
    bool refresh_trader_with_dice = false;       // 用骰子刷新商店（仅Mizuki）
    bool use_foldartal = true;                   // 使用密文板（仅Sami）
    bool check_collapsal_paradigms = false;      // 检测坍缩范式（仅Sami）
    bool double_check_collapsal_paradigms = false; // 双重检测（仅Sami且check_collapsal_paradigms为true）

    // 自动切换设置
    bool monthly_squad_auto_iterate = false;     // 月度小队自动切换
    bool monthly_squad_check_comms = false;      // 月度小队通信切换依据
    bool deep_exploration_auto_iterate = false;  // 深入调查自动切换

    // 烧水相关配置
    bool collectible_mode_shopping = false;      // 烧水启用购物
    char collectible_mode_squad[128] = "";    // 烧水使用的分队
    CollectibleStartList collectible_mode_collectibles;  // 烧水期望奖励
};


// JSON序列化/反序列化
namespace nlohmann {
    // 启动配置序列化
    template <>
    struct adl_serializer<StartupConfig> {
        static void to_json(json& j, const StartupConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"client_type", cfg.client_type},
                {"start_game_enabled", cfg.start_game_enabled},
                {"account_name", cfg.account_name},
                {"ldExtraEnable", cfg.ldExtraEnable},
                {"ldExtraID", cfg.ldExtraID},
                {"ldExtraPathToConsole", cfg.ldExtraPathToConsole},
                {"netaddr", cfg.netaddr}
            };
        }
        static void from_json(const json& j, StartupConfig& cfg) {
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);
            if (j.contains("client_type")) strncpy(cfg.client_type, j["client_type"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
            if (j.contains("start_game_enabled")) j["start_game_enabled"].get_to(cfg.start_game_enabled);
            if (j.contains("account_name")) strncpy(cfg.account_name, j["account_name"].get<std::string>().c_str(), ACCOUNT_BUFFER_SIZE - 1);
            if (j.contains("ldExtraEnable")) j["ldExtraEnable"].get_to(cfg.ldExtraEnable);
            if (j.contains("ldExtraID")) j["ldExtraID"].get_to(cfg.ldExtraID);
            if (j.contains("ldExtraPathToConsole")) strncpy(cfg.ldExtraPathToConsole, j["ldExtraPathToConsole"].get<std::string>().c_str(), 256 - 1);
            if (j.contains("netaddr")) strncpy(cfg.netaddr, j["netaddr"].get<std::string>().c_str(), 128 - 1);
        }
    };

    // 关卡配置序列化
    template <>
    struct adl_serializer<StageConfig> {
        static void to_json(json& j, const StageConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"stage", cfg.stage},
                {"medicine", cfg.medicine},
                {"expiring_medicine", cfg.expiring_medicine},
                {"stone", cfg.stone},
                {"times", cfg.times},
                {"series", cfg.series},
                {"drops", cfg.drops},
                {"report_to_penguin", cfg.report_to_penguin},
                {"penguin_id", cfg.penguin_id},
                {"server", cfg.server},
                {"client_type", cfg.client_type},
                {"DrGrandet", cfg.DrGrandet}
            };
        }
        static void from_json(const json& j, StageConfig& cfg) {
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);
            if (j.contains("stage")) strncpy(cfg.stage, j["stage"].get<std::string>().c_str(), STAGE_BUFFER_SIZE - 1);
            if (j.contains("medicine")) j["medicine"].get_to(cfg.medicine);
            if (j.contains("expiring_medicine")) j["expiring_medicine"].get_to(cfg.expiring_medicine);
            if (j.contains("stone")) j["stone"].get_to(cfg.stone);
            if (j.contains("times")) j["times"].get_to(cfg.times);
            if (j.contains("series")) j["series"].get_to(cfg.series);
            if (j.contains("drops")) cfg.drops = j["drops"].get<std::map<std::string, int>>();
            if (j.contains("report_to_penguin")) j["report_to_penguin"].get_to(cfg.report_to_penguin);
            if (j.contains("penguin_id")) strncpy(cfg.penguin_id, j["penguin_id"].get<std::string>().c_str(), PENGUIN_ID_BUFFER_SIZE - 1);
            if (j.contains("server")) strncpy(cfg.server, j["server"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
            if (j.contains("client_type")) strncpy(cfg.client_type, j["client_type"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
            if (j.contains("DrGrandet")) j["DrGrandet"].get_to(cfg.DrGrandet);
        }
    };

    // 公招配置序列化
    template <>
    struct adl_serializer<RecruitmentConfig> {
        static void to_json(json& j, const RecruitmentConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"refresh", cfg.refresh},
                {"select", cfg.select},
                {"confirm", cfg.confirm},
                {"first_tags", cfg.first_tags},
                {"extra_tags_mode", cfg.extra_tags_mode},
                {"times", cfg.times},
                {"set_time", cfg.set_time},
                {"expedite", cfg.expedite},
                {"expedite_times", cfg.expedite_times},
                {"skip_robot", cfg.skip_robot},
                {"recruitment_time", cfg.recruitment_time},
                {"report_to_penguin", cfg.report_to_penguin},
                {"penguin_id", cfg.penguin_id},
                {"report_to_yituliu", cfg.report_to_yituliu},
                {"yituliu_id", cfg.yituliu_id},
                {"server", cfg.server},
                {"client_type", cfg.client_type}
            };
        }
        static void from_json(const json& j, RecruitmentConfig& cfg) {
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);
            cfg.refresh = true;  // 强制为true
            if (j.contains("select")) cfg.select = j["select"].get<std::vector<int>>();
            if (j.contains("confirm")) cfg.confirm = j["confirm"].get<std::vector<int>>();
            if (j.contains("first_tags")) cfg.first_tags = j["first_tags"].get<std::vector<std::string>>();
            if (j.contains("extra_tags_mode")) j["extra_tags_mode"].get_to(cfg.extra_tags_mode);
            if (j.contains("times")) j["times"].get_to(cfg.times);
            cfg.set_time = true;  // 强制为true
            if (j.contains("expedite")) j["expedite"].get_to(cfg.expedite);
            if (j.contains("expedite_times")) j["expedite_times"].get_to(cfg.expedite_times);
            if (j.contains("skip_robot")) j["skip_robot"].get_to(cfg.skip_robot);
            cfg.recruitment_time = {};  // 强制为空
            if (j.contains("report_to_penguin")) j["report_to_penguin"].get_to(cfg.report_to_penguin);
            if (j.contains("penguin_id")) strncpy(cfg.penguin_id, j["penguin_id"].get<std::string>().c_str(), PENGUIN_ID_BUFFER_SIZE - 1);
            if (j.contains("report_to_yituliu")) j["report_to_yituliu"].get_to(cfg.report_to_yituliu);
            if (j.contains("yituliu_id")) strncpy(cfg.yituliu_id, j["yituliu_id"].get<std::string>().c_str(), YITULIU_ID_BUFFER_SIZE - 1);
            if (j.contains("server")) strncpy(cfg.server, j["server"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
            if (j.contains("client_type")) strncpy(cfg.client_type, j["client_type"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
        }
    };

    // 设施配置序列化
    template <>
    struct adl_serializer<FacilityConfig> {
        static void to_json(json& j, const FacilityConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"mode", cfg.mode},
                {"facility", cfg.facility},
                {"drones", cfg.drones},
                {"threshold", cfg.threshold},
                {"replenish", cfg.replenish},
                {"dorm_notstationed_enabled", cfg.dorm_notstationed_enabled},
                {"dorm_trust_enabled", cfg.dorm_trust_enabled}
            };
        }
        static void from_json(const json& j, FacilityConfig& cfg) {
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);
            if (j.contains("mode")) j["mode"].get_to(cfg.mode);
            if (j.contains("facility")) cfg.facility = j["facility"].get<std::vector<std::string>>();
            if (j.contains("drones")) cfg.drones = j["drones"].get<std::string>();
            if (j.contains("threshold")) j["threshold"].get_to(cfg.threshold);
            if (j.contains("replenish")) j["replenish"].get_to(cfg.replenish);
            if (j.contains("dorm_notstationed_enabled")) j["dorm_notstationed_enabled"].get_to(cfg.dorm_notstationed_enabled);
            if (j.contains("dorm_trust_enabled")) j["dorm_trust_enabled"].get_to(cfg.dorm_trust_enabled);
        }
    };
}

namespace nlohmann {
    // 商店购物配置序列化
    template <>
    struct adl_serializer<ShoppingConfig> {
        static void to_json(json& j, const ShoppingConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"shopping", cfg.shopping},
                {"only_buy_discount", cfg.only_buy_discount},
                {"reserve_max_credit", cfg.reserve_max_credit}
            };
        }
        static void from_json(const json& j, ShoppingConfig& cfg) {
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);
            if (j.contains("shopping")) j["shopping"].get_to(cfg.shopping);
            if (j.contains("only_buy_discount")) j["only_buy_discount"].get_to(cfg.only_buy_discount);
            if (j.contains("reserve_max_credit")) j["reserve_max_credit"].get_to(cfg.reserve_max_credit);
        }
    };
}

// 在JSON序列化/反序列化区域添加
namespace nlohmann {
    // 任务奖励配置序列化
    template <>
    struct adl_serializer<MissionConfig> {
        static void to_json(json& j, const MissionConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"award", cfg.award}
            };
        }
        static void from_json(const json& j, MissionConfig& cfg) {
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);
            if (j.contains("award")) j["award"].get_to(cfg.award);
        }
    };
}

// JSON序列化/反序列化
namespace nlohmann {
    // 奖励配置列表序列化
    template <>
    struct adl_serializer<CollectibleStartList> {
        static void to_json(json& j, const CollectibleStartList& cfg) {
            j = json{
                {"hot_water", cfg.hot_water},
                {"shield", cfg.shield},
                {"ingot", cfg.ingot},
                {"hope", cfg.hope},
                {"random", cfg.random},
                {"key", cfg.key},
                {"dice", cfg.dice},
                {"ideas", cfg.ideas}
            };
        }

        static void from_json(const json& j, CollectibleStartList& cfg) {
            if (j.contains("hot_water")) j["hot_water"].get_to(cfg.hot_water);
            if (j.contains("shield")) j["shield"].get_to(cfg.shield);
            if (j.contains("ingot")) j["ingot"].get_to(cfg.ingot);
            if (j.contains("hope")) j["hope"].get_to(cfg.hope);
            if (j.contains("random")) j["random"].get_to(cfg.random);
            if (j.contains("key")) j["key"].get_to(cfg.key);
            if (j.contains("dice")) j["dice"].get_to(cfg.dice);
            if (j.contains("ideas")) j["ideas"].get_to(cfg.ideas);
        }
    };

    // 肉鸽配置序列化
    template <>
    struct adl_serializer<RoguelikeConfig> {
        static void to_json(json& j, const RoguelikeConfig& cfg) {
            j = json{
                {"enable", cfg.enable},
                {"theme", cfg.theme},
                {"mode", cfg.mode},
                {"squad", cfg.squad},
                {"roles", cfg.roles},
                {"core_char", cfg.core_char},
                {"use_support", cfg.use_support},
                {"use_nonfriend_support", cfg.use_nonfriend_support},
                {"starts_count", cfg.starts_count},
                {"difficulty", cfg.difficulty},
                {"stop_at_final_boss", cfg.stop_at_final_boss},
                {"stop_at_max_level", cfg.stop_at_max_level},
                {"investment_enabled", cfg.investment_enabled},
                {"investments_count", cfg.investments_count},
                {"stop_when_investment_full", cfg.stop_when_investment_full},
                {"investment_with_more_score", cfg.investment_with_more_score},
                {"start_with_elite_two", cfg.start_with_elite_two},
                {"only_start_with_elite_two", cfg.only_start_with_elite_two},
                {"refresh_trader_with_dice", cfg.refresh_trader_with_dice},
                {"collectible_mode_start_list", cfg.collectible_mode_start_list},
                {"use_foldartal", cfg.use_foldartal},
                {"check_collapsal_paradigms", cfg.check_collapsal_paradigms},
                {"double_check_collapsal_paradigms", cfg.double_check_collapsal_paradigms},
                {"monthly_squad_auto_iterate", cfg.monthly_squad_auto_iterate},
                {"monthly_squad_check_comms", cfg.monthly_squad_check_comms},
                {"deep_exploration_auto_iterate", cfg.deep_exploration_auto_iterate},
                {"collectible_mode_shopping", cfg.collectible_mode_shopping},
                {"collectible_mode_squad", cfg.collectible_mode_squad},
                {"collectible_mode_start_list", cfg.collectible_mode_collectibles}
            };
        }

        static void from_json(const json& j, RoguelikeConfig& cfg) {
            // 基础配置
            if (j.contains("enable")) j["enable"].get_to(cfg.enable);

            // 主题（C风格字符串处理）
            if (j.contains("theme")) {
                std::string theme = j["theme"].get<std::string>();
                strncpy(cfg.theme, theme.c_str(), sizeof(cfg.theme) - 1);
                cfg.theme[sizeof(cfg.theme) - 1] = '\0'; // 确保字符串终止
            }

            if (j.contains("mode")) j["mode"].get_to(cfg.mode);

            // 开局分队（C风格字符串处理）
            if (j.contains("squad")) {
                std::string squad = j["squad"].get<std::string>();
                strncpy(cfg.squad, squad.c_str(), sizeof(cfg.squad) - 1);
                cfg.squad[sizeof(cfg.squad) - 1] = '\0';
            }

            // 开局职业组（C风格字符串处理）
            if (j.contains("roles")) {
                std::string roles = j["roles"].get<std::string>();
                strncpy(cfg.roles, roles.c_str(), sizeof(cfg.roles) - 1);
                cfg.roles[sizeof(cfg.roles) - 1] = '\0';
            }

            // 开局干员名（C风格字符串处理）
            if (j.contains("core_char")) {
                std::string core_char = j["core_char"].get<std::string>();
                strncpy(cfg.core_char, core_char.c_str(), sizeof(cfg.core_char) - 1);
                cfg.core_char[sizeof(cfg.core_char) - 1] = '\0';
            }
            if (j.contains("use_support")) j["use_support"].get_to(cfg.use_support);
            if (j.contains("use_nonfriend_support")) j["use_nonfriend_support"].get_to(cfg.use_nonfriend_support);

            // 运行控制
            if (j.contains("starts_count")) j["starts_count"].get_to(cfg.starts_count);
            if (j.contains("difficulty")) j["difficulty"].get_to(cfg.difficulty);
            if (j.contains("stop_at_final_boss")) j["stop_at_final_boss"].get_to(cfg.stop_at_final_boss);
            if (j.contains("stop_at_max_level")) j["stop_at_max_level"].get_to(cfg.stop_at_max_level);

            // 投资相关
            if (j.contains("investment_enabled")) j["investment_enabled"].get_to(cfg.investment_enabled);
            if (j.contains("investments_count")) j["investments_count"].get_to(cfg.investments_count);
            if (j.contains("stop_when_investment_full")) j["stop_when_investment_full"].get_to(cfg.stop_when_investment_full);
            if (j.contains("investment_with_more_score")) j["investment_with_more_score"].get_to(cfg.investment_with_more_score);

            // 凹开局相关
            if (j.contains("start_with_elite_two")) j["start_with_elite_two"].get_to(cfg.start_with_elite_two);
            if (j.contains("only_start_with_elite_two")) j["only_start_with_elite_two"].get_to(cfg.only_start_with_elite_two);
            if (j.contains("collectible_mode_start_list")) cfg.collectible_mode_start_list = j["collectible_mode_start_list"].get<CollectibleStartList>();

            // 主题专属配置
            if (j.contains("refresh_trader_with_dice")) j["refresh_trader_with_dice"].get_to(cfg.refresh_trader_with_dice);
            if (j.contains("use_foldartal")) j["use_foldartal"].get_to(cfg.use_foldartal);
            if (j.contains("check_collapsal_paradigms")) j["check_collapsal_paradigms"].get_to(cfg.check_collapsal_paradigms);
            if (j.contains("double_check_collapsal_paradigms")) j["double_check_collapsal_paradigms"].get_to(cfg.double_check_collapsal_paradigms);

            // 自动切换设置
            if (j.contains("monthly_squad_auto_iterate")) j["monthly_squad_auto_iterate"].get_to(cfg.monthly_squad_auto_iterate);
            if (j.contains("monthly_squad_check_comms")) j["monthly_squad_check_comms"].get_to(cfg.monthly_squad_check_comms);
            if (j.contains("deep_exploration_auto_iterate")) j["deep_exploration_auto_iterate"].get_to(cfg.deep_exploration_auto_iterate);

            // 烧水相关配置
            if (j.contains("collectible_mode_shopping")) j["collectible_mode_shopping"].get_to(cfg.collectible_mode_shopping);
            if (j.contains("collectible_mode_squad")) {
                std::string squad = j["collectible_mode_squad"].get<std::string>();
                strncpy(cfg.collectible_mode_squad, squad.c_str(), sizeof(cfg.collectible_mode_squad) - 1);
                cfg.collectible_mode_squad[sizeof(cfg.collectible_mode_squad) - 1] = '\0';
            }
            if (j.contains("collectible_mode_start_list")) cfg.collectible_mode_collectibles = j["collectible_mode_start_list"].get<CollectibleStartList>();
        }
    };
}

class SettingsManager {
public:
    StartupConfig startup_config;
    StageConfig stage_config;
    RecruitmentConfig recruitment_config;
    FacilityConfig facility_config;  // 新增设施配置
    ShoppingConfig shoppinsettings;  // 商店购物配置
    MissionConfig mission_config;  // 任务奖励配置
    RoguelikeConfig roguelike_config;
    std::string config_path;
private:
    // 窗口状态
    bool settings_window_open = false;
    bool startup_settings_open = false;
    bool stage_settings_open = false;
    bool recruitment_settings_open = false;
    bool facility_settings_open = false;  // 设施配置窗口
    bool stage_picker_open = false;
    bool drops_editor_open = false;
    bool tags_editor_open = false;
    bool facility_editor_open = false;  // 设施编辑窗口
    bool shopping_settings_open = false;  // 购物配置窗口
    bool mission_settings_open = false;  // 任务配置窗口
    bool roguelike_settings_open = false;

    // 自定义窗口临时缓冲区
    char temp_stage[STAGE_BUFFER_SIZE] = { 0 };
    std::map<std::string, int> temp_drops;
    char new_tag[TAG_BUFFER_SIZE] = { 0 };
    char new_facility[FACILITY_BUFFER_SIZE] = { 0 };  // 新设施临时输入
    std::vector<std::string> temp_facilities;  // 设施编辑临时存储

    // UI尺寸常量
    const float text_width = 120.0f;
    const float input_max_width = 180.0f;
    const float window_min_width = 400.0f;

    // 设施选项
    const std::vector<const char*> facility_options = {
        "Mfg", "Trade", "Power", "Control",
        "Reception", "Office", "Dorm"
    };

    // 无人机用途选项
    const std::vector<std::pair<const char*, const char*>> drones_options = {
        {"_NotUse", "不使用无人机"},
        {"Money", "贸易站-龙门币"},
        {"SyntheticJade", "贸易站-源石碎片"},
        {"CombatRecord", "制造站-作战记录"},
        {"PureGold", "制造站-赤金"},
        {"OriginStone", "制造站-源石碎片"},
        {"Chip", "制造站-芯片"}
    };

    const std::vector<const char*> client_types = {
        "Official", "Bilibili", "txwy",
        "YoStarEN", "YoStarJP", "YoStarKR"
    };
    const std::vector<std::pair<int, const char*>> series_options = {
        {-1, "禁用"}, {0, "自动"},
        {1, "1次"}, {2, "2次"}, {3, "3次"},
        {4, "4次"}, {5, "5次"}, {6, "6次"}
    };
    const std::vector<std::pair<int, const char*>> extra_tags_options = {
        {0, "默认行为"},
        {1, "选3个Tags(可能冲突)"},
        {2, "更多高星组合(可能冲突)"}
    };
    const std::vector<const char*> server_options = {
        "CN", "US", "JP", "KR"
    };


    // 主题选项（ID -> 显示名称）
    const std::vector<std::pair<std::string, std::string>> theme_options = {
        {"Phantom", "傀影与猩红血钻"},
        {"Mizuki", "水月与深蓝之树"},
        {"Sami", "探索者的银凇止境"},
        {"Sarkaz", "萨卡兹的无终奇语"},
        {"JieGarden", "界园"}
    };

    // 模式选项（值 -> 描述）
    const std::vector<std::pair<int, std::string>> mode_options = {
        {0, "刷分/奖励点数（稳定打更多层数）"},
        {1, "刷源石锭（第一层投资完退出）"},
        {4, "凹开局（特定流程）"},
        {6, "刷月度小队蚊子腿"},
        {7, "刷深入调查蚊子腿"}
    };

    // 根据主题获取可用分队列表
    std::vector<std::string> get_squad_options(const std::string& theme) {
        if (theme == "Phantom") {
            return { "指挥分队", "集群分队", "后勤分队", "矛头分队",
                    "突击战术分队", "堡垒战术分队", "远程战术分队",
                    "破坏战术分队", "研究分队", "高规格分队" };
        }
        else if (theme == "Mizuki") {
            return { "心胜于物分队", "物尽其用分队", "以人为本分队", "指挥分队",
                    "集群分队", "后勤分队", "矛头分队", "突击战术分队",
                    "堡垒战术分队", "远程战术分队", "破坏战术分队",
                    "研究分队", "高规格分队" };
        }
        else if (theme == "Sami") {
            return { "永恒狩猎分队", "生活至上分队", "科学主义分队", "特训分队",
                    "指挥分队", "集群分队", "后勤分队", "矛头分队",
                    "突击战术分队", "堡垒战术分队", "远程战术分队",
                    "破坏战术分队", "高规格分队" };
        }
        else if (theme == "Sarkaz") {
            return { "因地制宜分队", "魂灵护送分队", "博闻广记分队", "蓝图测绘分队",
                    "指挥分队", "集群分队", "后勤分队", "矛头分队",
                    "突击战术分队", "堡垒战术分队", "远程战术分队",
                    "破坏战术分队", "高规格分队", "点刺成锭分队",
                    "拟态学者分队", "异想天开分队" };
        }
        else if (theme == "JieGarden") {
            return { "特勤分队", "高台突破分队", "地面突破分队", "游客分队",
                    "司岁台分队", "天师府分队", "指挥分队", "后勤分队",
                    "突击战术分队", "堡垒战术分队", "远程战术分队",
                    "破坏战术分队", "高规格分队" };
        }
        return {};
    }

    // 根据主题获取可用职业组列表
    std::vector<std::string> get_roles_options(const std::string& theme) {
        if (theme == "JieGarden") {
            return { "先手必胜", "稳扎稳打", "取长补短", "灵活部署",
                    "坚不可摧", "随心所欲" };
        }
        else {
            return { "先手必胜", "稳扎稳打", "取长补短", "随心所欲" };
        }
    }

    // 3. 修复主题切换时的同步逻辑
    void on_theme_changed(const std::string& old_theme, const std::string& new_theme) {
        if (old_theme == new_theme) return;

        // 重置职业组为"先手必胜"
        strncpy(roguelike_config.roles, "先手必胜", sizeof(roguelike_config.roles) - 1);
        roguelike_config.roles[sizeof(roguelike_config.roles) - 1] = '\0';

        // 重置分队为列表第一个
        auto squads = get_squad_options(new_theme);
        if (!squads.empty()) {
            // 同步更新开局分队和烧水分队
            strncpy(roguelike_config.squad, squads[0].c_str(), sizeof(roguelike_config.squad) - 1);
            roguelike_config.squad[sizeof(roguelike_config.squad) - 1] = '\0';

            // 保持烧水分队与开局分队同步
            strncpy(roguelike_config.collectible_mode_squad, squads[0].c_str(),
                sizeof(roguelike_config.collectible_mode_squad) - 1);
            roguelike_config.collectible_mode_squad[sizeof(roguelike_config.collectible_mode_squad) - 1] = '\0';
        }
    }

    void sync_server_with_client_type() {
        // 同步关卡配置服务器
        strncpy(stage_config.client_type, startup_config.client_type, STR_BUFFER_SIZE - 1);
        // 同步公招配置服务器
        strncpy(recruitment_config.client_type, startup_config.client_type, STR_BUFFER_SIZE - 1);

        // 根据客户端类型设置服务器
        if (strcmp(startup_config.client_type, "Official") == 0 ||
            strcmp(startup_config.client_type, "Bilibili") == 0) {
            strncpy(stage_config.server, "CN", STR_BUFFER_SIZE - 1);
            strncpy(recruitment_config.server, "CN", STR_BUFFER_SIZE - 1);
        }
        else if (strcmp(startup_config.client_type, "txwy") == 0) {
            strncpy(stage_config.server, "TW", STR_BUFFER_SIZE - 1);
            strncpy(recruitment_config.server, "TW", STR_BUFFER_SIZE - 1);
        }
        else if (strcmp(startup_config.client_type, "YoStarEN") == 0) {
            strncpy(stage_config.server, "US", STR_BUFFER_SIZE - 1);
            strncpy(recruitment_config.server, "US", STR_BUFFER_SIZE - 1);
        }
        else if (strcmp(startup_config.client_type, "YoStarJP") == 0) {
            strncpy(stage_config.server, "JP", STR_BUFFER_SIZE - 1);
            strncpy(recruitment_config.server, "JP", STR_BUFFER_SIZE - 1);
        }
        else if (strcmp(startup_config.client_type, "YoStarKR") == 0) {
            strncpy(stage_config.server, "KR", STR_BUFFER_SIZE - 1);
            strncpy(recruitment_config.server, "KR", STR_BUFFER_SIZE - 1);
        }
    }

    bool can_set_account() const {
        return strcmp(startup_config.client_type, "Official") == 0 ||
            strcmp(startup_config.client_type, "Bilibili") == 0;
    }

    void draw_label(const char* text) {
        ImGui::Text("%s", text);
        ImGui::SameLine(text_width);
    }

    void draw_stage_picker() {
        if (!stage_picker_open) return;

        // 确保数据已加载
        static bool data_loaded = false;
        if (!data_loaded) {
            data_loaded = load_stages() && load_item_index();
        }

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 400), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("选择关卡", &stage_picker_open)) {
            ImGui::Text("请选择要刷取的关卡...");
            ImGui::Spacing();

            // 搜索区域
            ImGui::SetNextItemWidth(input_max_width - 100);
            ImGui::InputText("##stage_search", search_query, SEARCH_QUERY_LENGTH);
            ImGui::SameLine();
            if (ImGui::Button("查询", ImVec2(80, 0))) {
                filter_stages(search_query);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (!data_loaded) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "加载关卡数据失败，请检查相关文件");
            }
            else if (ImGui::BeginTable("stage_results", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("关卡名", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("消耗理智", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("掉落材料", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("选择", ImGuiTableColumnFlags_WidthFixed); // 新增选择列
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < filtered_stages.size(); i++) {
                    const auto& stage = filtered_stages[i];
                    ImGui::TableNextRow();

                    // 关卡名 - 修正HARD显示
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", stage.code);
                    // 如果stageId包含"tough"，显示高亮的HARD
                    if (string_contains(stage.stageId, "tough")) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Hard");
                    }

                    // 消耗理智
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", stage.apCost);

                    // 查看掉落材料按钮
                    ImGui::TableSetColumnIndex(2);
                    char button_id[128];
                    snprintf(button_id, sizeof(button_id), "查看##stage_%zu", i);

                    if (ImGui::Button(button_id)) {
                        current_stage = &filtered_stages[i];
                        show_drop_popup = true;
                    }

                    // 选择按钮 - 新增功能
                    ImGui::TableSetColumnIndex(3);
                    char select_id[128];
                    snprintf(select_id, sizeof(select_id), "选择##stage_%zu", i);
                    if (ImGui::Button(select_id)) {
                        // 构建带Hard后缀的关卡名
                        char full_stage_name[128];
                        snprintf(full_stage_name, sizeof(full_stage_name), "%s", stage.code);

                        // 如果是Hard关卡，添加Hard后缀
                        if (string_contains(stage.stageId, "tough")) {
                            strncat(full_stage_name, "Hard", sizeof(full_stage_name) - strlen(full_stage_name) - 1);
                        }

                        // 填入输入框
                        strncpy(temp_stage, full_stage_name, STAGE_BUFFER_SIZE - 1);
                        temp_stage[STAGE_BUFFER_SIZE - 1] = '\0';

                    }
                }

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 手动输入关卡代码
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##stage_preview", temp_stage, STAGE_BUFFER_SIZE);

            if (ImGui::Button("确认选择##stage_picker")) {
                strncpy(stage_config.stage, temp_stage, STAGE_BUFFER_SIZE - 1);
                stage_picker_open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##stage_picker")) {
                stage_picker_open = false;
            }

            // 掉落材料弹窗
            if (show_drop_popup && current_stage) {
                ImGui::SetNextWindowSizeConstraints(ImVec2(400, 200), ImVec2(600, 500));
                ImGui::OpenPopup("掉落材料");

                if (ImGui::BeginPopup("掉落材料", ImGuiWindowFlags_Popup | ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("关卡 %s", current_stage->code);
                    // 弹窗标题显示Hard标记
                    if (string_contains(current_stage->stageId, "tough")) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Hard");
                    }
                    ImGui::Spacing();

                    if (ImGui::BeginTable("drop_items", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("物品名", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("掉落类型", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableHeadersRow();

                        for (const auto& drop : current_stage->dropInfos) {
                            ImGui::TableNextRow();

                            // ID
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", drop.itemId);

                            // 物品名
                            ImGui::TableSetColumnIndex(1);
                            const ItemInfo* item = find_item(drop.itemId);
                            if (item) {
                                ImGui::Text("%s", item->name);
                            }
                            else {
                                ImGui::TextDisabled("未知物品");
                            }

                            // 掉落类型（带翻译）
                            ImGui::TableSetColumnIndex(2);
                            if (strcmp(drop.dropType, "NORMAL_DROP") == 0) {
                                ImGui::Text("常规掉落");
                            }
                            else if (strcmp(drop.dropType, "EXTRA_DROP") == 0) {
                                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "额外掉落");
                            }
                            else if (strcmp(drop.dropType, "FURNITURE") == 0) {
                                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "家具");
                            }
                            else {
                                ImGui::Text("%s", drop.dropType);
                            }
                        }

                        ImGui::EndTable();
                    }

                    if (ImGui::Button("关闭", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                        show_drop_popup = false;
                        current_stage = nullptr;
                    }
                    ImGui::EndPopup();
                }
                else {
                    show_drop_popup = false;
                }
            }
        }
        ImGui::End();
    }

    // 修改后的编辑窗口函数
    void draw_drops_editor() {
        if (!drops_editor_open) {
            // 清理下载线程
            if (download_thread.joinable()) {
                {
                    std::lock_guard<std::mutex> lock(download_state.mtx);
                    download_state.status = DownloadStatus::Cancelled;
                }
                download_thread.join();
            }
            download_state.status = DownloadStatus::Idle;
            download_state.progress = 0;
            download_state.error_msg.clear();
            return;
        }

        // 检查并下载文件
        static bool checked_file = false;
        if (!checked_file) {
            if (!is_file_valid()) {
                // 启动下载线程
                std::lock_guard<std::mutex> lock(download_state.mtx);
                download_state.status = DownloadStatus::Downloading;
                download_state.progress = 0;
                download_thread = std::thread(download_file);
            }
            checked_file = true;
        }

        // 显示下载进度窗口
        {
            std::lock_guard<std::mutex> lock(download_state.mtx);
            if (download_state.status == DownloadStatus::Downloading) {
                ImGui::OpenPopup("下载数据中");
                if (ImGui::BeginPopupModal("下载数据中", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("正在更新掉落数据...");
                    ImGui::ProgressBar(static_cast<float>(download_state.progress) / 100.0f, ImVec2(-1, 0));
                    ImGui::Text("%d%%", download_state.progress);
                    if (ImGui::Button("取消下载")) {
                        download_state.status = DownloadStatus::Cancelled;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                return; // 下载中阻塞其他操作
            }
            else if (download_state.status == DownloadStatus::Failed) {
                ImGui::OpenPopup("下载失败");
                if (ImGui::BeginPopupModal("下载失败", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "下载失败: %s", download_state.error_msg.c_str());
                    if (ImGui::Button("重试")) {
                        download_state.status = DownloadStatus::Downloading;
                        download_state.progress = 0;
                        download_thread = std::thread(download_file);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("关闭")) {
                        drops_editor_open = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                return;
            }
        }

        // 窗口布局
        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 400), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("编辑掉落条件", &drops_editor_open)) {
            ImGui::Text("请设置需要的掉落材料...");
            ImGui::Spacing();

            static bool initialized = false;
            if (!initialized) {
                temp_drops = stage_config.drops;
                initialized = true;
            }

            static char new_item_id[32] = { 0 };
            static int new_item_count = 1;
            static char search_name[128] = { 0 }; // 材料名称搜索框
            static std::vector<nlohmann::json> filtered_matrix; // 筛选后的掉落数据

            ImGui::Text("搜索材料:");
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##search_name", search_name, sizeof(search_name));
            ImGui::SameLine();

            // 添加查询按钮，点击才触发查询
            if (ImGui::Button("查询材料")) {
                filtered_matrix.clear();

                // 空查询内容则清空列表
                if (strlen(search_name) <= 1) {
                }
                else {

                    // 1. 从item_index查找全字匹配的itemId（全字匹配材料名称）
                    std::vector<std::string> target_ids;
                    for (const auto& [id, item] : item_index) {
                        // 全字匹配判断（完全相等）
                        if (strcmp(item.name, search_name) == 0) {
                            target_ids.push_back(id);
                        }
                    }
                    if (!target_ids.empty()) {
                        // 2. 解析penguin-stats.json
                        try {
                            std::ifstream file(PENGUIN_FILE);
                            if (!file.is_open()) throw "Access to penguin-stat.cn temp file failed.";

                            nlohmann::json data;
                            file >> data;
                            if (!data.contains("matrix")) throw "Access to penguin-stat.cn temp file failed:File incomplete.";

                            // 3. 筛选并计算"刷取一个所需理智"（使用 stdDev 计算）
                            std::vector<std::tuple<float, nlohmann::json>> temp_results;
                            auto now = get_current_time();

                            for (const auto& entry : data["matrix"]) {
                                std::string item_id = entry["itemId"].get<std::string>();
                                if (std::find(target_ids.begin(), target_ids.end(), item_id) == target_ids.end()) {
                                    continue;
                                }

                                // 过滤过期关卡
                                if (entry.contains("end") && !entry["end"].is_null()) {
                                    time_t end_time = timestamp_to_timet(entry["end"].get<int64_t>());
                                    if (end_time < now) continue;
                                }

                                // 获取关卡消耗理智
                                std::string stage_id = entry["stageId"].get<std::string>();
                                int ap_cost = -1;
                                for (const auto& stage : all_stages) {
                                    if (stage.stageId == stage_id) {
                                        ap_cost = stage.apCost;
                                        break;
                                    }
                                }
                                if (ap_cost <= 0) continue;

                                // 关键计算：期望理智 = 单次作战理智消耗 / stdDev
                                if (entry.contains("stdDev")) {
                                    float std_dev = entry["stdDev"].get<float>();

                                    // 避免除以零或负数
                                    if (std_dev <= 0) continue;

                                    float cost_per = ap_cost / std_dev;  // 使用指定公式计算
                                    temp_results.emplace_back(cost_per, entry);
                                }
                            }

                            // 按期望理智升序排序（数值越小越划算）
                            std::sort(temp_results.begin(), temp_results.end());
                            size_t take = std::min(30ULL, temp_results.size());
                            for (size_t i = 0; i < take; ++i) {
                                filtered_matrix.push_back(std::get<1>(temp_results[i]));
                            }
                        }
                        catch (...) {
                            ImGui::TextColored(ImVec4(1, 0, 0, 1), "解析数据失败");
                        }
                    }
                }
            }


            ImGui::SameLine();

            // 添加清空按钮
            if (ImGui::Button("清空")) {
                search_name[0] = '\0';
                filtered_matrix.clear();
            }

            // 原材料列表（带新增列）
            ImGui::Spacing();
            ImGui::Text("已选材料:");
            if (ImGui::BeginTable("drops_temp", 5, ImGuiTableFlags_Borders)) { // 新增列：名称、+、-、删除
                ImGui::TableSetupColumn("材料ID", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("材料名称", ImGuiTableColumnFlags_WidthFixed, 120);
                ImGui::TableSetupColumn("数量", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("调整", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableHeadersRow();

                // 遍历现有材料
                auto it = temp_drops.begin();
                while (it != temp_drops.end()) {
                    ImGui::TableNextRow();
                    const auto& [id, count] = *it;

                    // 材料ID
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", id.c_str());

                    // 材料名称
                    ImGui::TableSetColumnIndex(1);
                    const ItemInfo* item = find_item(id.c_str());
                    ImGui::Text("%s", item ? item->name : "未知");

                    // 数量
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", count);

                    // 增减按钮
                    ImGui::TableSetColumnIndex(3);
                    char add_id[64], sub_id[64];
                    snprintf(add_id, sizeof(add_id), "+##+%s", id.c_str());
                    snprintf(sub_id, sizeof(sub_id), "-##-%s", id.c_str());

                    if (ImGui::Button(add_id, ImVec2(30, 0))) {
                        it->second++;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(sub_id, ImVec2(30, 0)) && count > 1) {
                        it->second--;
                    }

                    // 删除按钮
                    ImGui::TableSetColumnIndex(4);
                    char del_id[64];
                    snprintf(del_id, sizeof(del_id), "删除##del%s", id.c_str());
                    if (ImGui::Button(del_id, ImVec2(-1, 0))) {
                        it = temp_drops.erase(it);
                        continue;
                    }

                    ++it;
                }
                ImGui::EndTable();
            }

            // 添加新材料输入区
            ImGui::Spacing();
            ImGui::Text("添加新材料:");
            ImGui::SetNextItemWidth(80);
            ImGui::InputText("##new_item_id", new_item_id, sizeof(new_item_id));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::Text("材料名称: %s",
                find_item(new_item_id) ? find_item(new_item_id)->name : "未知");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::InputInt("##new_item_count", &new_item_count);
            new_item_count = std::max(1, new_item_count); // 限制最小值1
            ImGui::SameLine();
            if (ImGui::Button("添加##drops")) {
                if (strlen(new_item_id) > 0) {
                    temp_drops[new_item_id] = new_item_count;
                }
            }

            // 搜索结果表格（关卡推荐）
            if (!filtered_matrix.empty()) {
                ImGui::Spacing();
                ImGui::Text("推荐关卡 (按刷取成本升序):");
                if (ImGui::BeginTable("stage_recommend", 4, ImGuiTableFlags_Borders)) {
                    ImGui::TableSetupColumn("关卡", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableSetupColumn("刷取一个所需理智", ImGuiTableColumnFlags_WidthFixed, 120);
                    ImGui::TableSetupColumn("添加材料", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableSetupColumn("选择关卡", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableHeadersRow();

                    // 使用索引生成唯一ID
                    for (size_t idx = 0; idx < filtered_matrix.size(); ++idx) {
                        const auto& entry = filtered_matrix[idx];
                        ImGui::TableNextRow();
                        std::string item_id = entry["itemId"].get<std::string>();
                        std::string stage_id = entry["stageId"].get<std::string>();

                        // 关卡名
                        ImGui::TableSetColumnIndex(0);
                        std::string stage_code = "未知";
                        for (const auto& stage : all_stages) {
                            if (stage.stageId == stage_id) {
                                stage_code = stage.code;
                                if (string_contains(stage.stageId, "tough")) {
                                    stage_code += "Hard";
                                }
                                break;
                            }
                        }
                        ImGui::Text("%s", stage_code.c_str());

                        // 刷取成本
                        ImGui::TableSetColumnIndex(1);
                        int ap_cost = 0;
                        for (const auto& stage : all_stages) {
                            if (stage.stageId == stage_id) {
                                ap_cost = stage.apCost;
                                break;
                            }
                        }
                        float cost_per = ap_cost / entry["stdDev"].get<float>();
                        ImGui::Text("%.2f", cost_per);

                        // 添加材料按钮（显示"添加"，ID格式为"添加#唯一标识"）
                        ImGui::TableSetColumnIndex(2);
                        char add_btn_id[128];
                        // 按钮ID格式："添加#add_mat_索引"，既保证唯一性又包含明确标识
                        snprintf(add_btn_id, sizeof(add_btn_id), "添加##add_mat_%zu", idx);
                        if (ImGui::Button(add_btn_id, ImVec2(-1, 0))) {
                            temp_drops[item_id] = 1;
                        }

                        // 选择关卡按钮（显示"选择"，ID格式为"选择#唯一标识"）
                        ImGui::TableSetColumnIndex(3);
                        char select_btn_id[128];
                        // 按钮ID格式："选择#sel_stage_索引"
                        snprintf(select_btn_id, sizeof(select_btn_id), "选择##sel_stage_%zu", idx);
                        if (ImGui::Button(select_btn_id, ImVec2(-1, 0))) {
                            strncpy(stage_config.stage, stage_code.c_str(), STAGE_BUFFER_SIZE - 1);
                        }

                    }
                    ImGui::EndTable();
                }


                // 底部按钮
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("确认设置##drops")) {
                    stage_config.drops = temp_drops;
                    initialized = false;
                    drops_editor_open = false;
                    checked_file = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("取消##drops")) {
                    initialized = false;
                    drops_editor_open = false;
                    checked_file = false;
                }
            }
            ImGui::End();
        }
    }
    void draw_tags_editor() {
        if (!tags_editor_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 200), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("编辑首选标签", &tags_editor_open)) {
            ImGui::Text("仅在Tag等级为3时有效");
            ImGui::Spacing();

            // 显示当前标签
            if (ImGui::BeginListBox("##current_tags", ImVec2(-1, 150))) {
                for (size_t i = 0; i < recruitment_config.first_tags.size(); ++i) {
                    const char* tag = recruitment_config.first_tags[i].c_str();
                    char label[256];
                    sprintf(label, "%s##tag_%d", tag, (int)i);
                    if (ImGui::Selectable(label)) {
                        // 点击删除
                        recruitment_config.first_tags.erase(recruitment_config.first_tags.begin() + i);
                        break;
                    }
                }
                ImGui::EndListBox();
            }

            // 添加新标签
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##new_tag", new_tag, TAG_BUFFER_SIZE);
            ImGui::SameLine();
            if (ImGui::Button("添加标签")) {
                if (strlen(new_tag) > 0) {
                    recruitment_config.first_tags.push_back(new_tag);
                    memset(new_tag, 0, TAG_BUFFER_SIZE);
                }
            }

            if (ImGui::Button("确认##tags")) {
                tags_editor_open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##tags")) {
                tags_editor_open = false;
            }
        }
        ImGui::End();
    }

    // 新增：设施编辑窗口
    void draw_facility_editor() {
        if (!facility_editor_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 250), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("编辑换班设施", &facility_editor_open)) {
            ImGui::Text("设置要换班的设施（有序）");
            ImGui::TextDisabled("不支持运行中修改");
            ImGui::Spacing();

            static bool initialized = false;
            static size_t selected_index = SIZE_MAX; // 用于跟踪选中的项
            if (!initialized) {
                temp_facilities = facility_config.facility;
                selected_index = SIZE_MAX;
                memset(new_facility, 0, FACILITY_BUFFER_SIZE);
                initialized = true;
            }

            // 显示当前设施列表 - 使用Table布局
            if (ImGui::BeginTable("facilities_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings)) {
                // 表格列设置
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 20); // 选择框列
                ImGui::TableSetupColumn("设施名称", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("排序", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableHeadersRow();

                // 遍历设施列表
                for (size_t i = 0; i < temp_facilities.size(); ++i) {
                    ImGui::TableNextRow();

                    // 选择框列 - 单独的选择区域，避免与删除冲突
                    ImGui::TableSetColumnIndex(0);
                    char check_label[32];
                    sprintf(check_label, "##check_%d", (int)i);
                    bool is_selected = (selected_index == i);  // 使用临时变量存储状态
                    if (ImGui::Checkbox(check_label, &is_selected)) {
                        // 根据复选框状态更新选中索引
                        selected_index = is_selected ? i : SIZE_MAX;
                    }

                    // 设施名称列
                    ImGui::TableSetColumnIndex(1);
                    char label[256];
                    sprintf(label, "%s##name_%d", temp_facilities[i].c_str(), (int)i);
                    ImGui::TextUnformatted(temp_facilities[i].c_str());

                    // 排序和删除按钮列
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID((int)i);

                    // 上移按钮
                    bool can_move_up = i > 0;
                    if (!can_move_up) ImGui::BeginDisabled();
                    if (ImGui::Button("↑", ImVec2(25, 20))) {
                        std::swap(temp_facilities[i], temp_facilities[i - 1]);
                        // 交换后保持选中状态
                        if (selected_index == i) selected_index = i - 1;
                        else if (selected_index == i - 1) selected_index = i;
                    }
                    if (!can_move_up) ImGui::EndDisabled();

                    ImGui::SameLine();

                    // 下移按钮
                    bool can_move_down = i < temp_facilities.size() - 1;
                    if (!can_move_down) ImGui::BeginDisabled();
                    if (ImGui::Button("↓", ImVec2(25, 20))) {
                        std::swap(temp_facilities[i], temp_facilities[i + 1]);
                        // 交换后保持选中状态
                        if (selected_index == i) selected_index = i + 1;
                        else if (selected_index == i + 1) selected_index = i;
                    }
                    if (!can_move_down) ImGui::EndDisabled();

                    ImGui::SameLine();

                    // 删除按钮 - 单独的删除按钮，避免与选择冲突
                    if (ImGui::Button("×", ImVec2(25, 20))) {
                        temp_facilities.erase(temp_facilities.begin() + i);
                        if (selected_index == i) selected_index = SIZE_MAX;
                        else if (selected_index > i) selected_index--;
                        break; // 因为删除了元素，需要重新遍历
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();

            // 添加新设施
            ImGui::SetNextItemWidth(input_max_width);
            const char* combo_label = "选择设施...";
            if (strlen(new_facility) > 0) {
                combo_label = new_facility;
            }

            if (ImGui::BeginCombo("##new_facility", combo_label)) {
                for (const char* facility : facility_options) {
                    bool is_selected = (strcmp(new_facility, facility) == 0);
                    if (ImGui::Selectable(facility, is_selected)) {
                        strncpy(new_facility, facility, FACILITY_BUFFER_SIZE - 1);
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("添加设施")) {
                if (strlen(new_facility) > 0) {
                    bool exists = false;
                    for (const auto& f : temp_facilities) {
                        if (f == new_facility) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        temp_facilities.push_back(new_facility);
                    }
                    memset(new_facility, 0, FACILITY_BUFFER_SIZE);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("确认设置##facility")) {
                facility_config.facility = temp_facilities;
                initialized = false;
                facility_editor_open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##facility")) {
                initialized = false;
                facility_editor_open = false;
            }
        }
        ImGui::End();
    }


    bool is_string_in_list(const char* str, const std::vector<std::string>& list) {
        if (!str) return false;
        return std::find(list.begin(), list.end(), std::string(str)) != list.end();
    }

public:
    SettingsManager(std::string path) : config_path(std::move(path)) {
        load_config();
        sync_server_with_client_type();
    }

    void load_config() {
        try {
            std::ifstream file(config_path);
            if (file.is_open()) {
                json j;
                file >> j;
                if (j.contains("startup")) startup_config = j["startup"].get<StartupConfig>();
                if (j.contains("stage")) stage_config = j["stage"].get<StageConfig>();
                if (j.contains("recruitment")) recruitment_config = j["recruitment"].get<RecruitmentConfig>();
                if (j.contains("facility")) facility_config = j["facility"].get<FacilityConfig>();  // 加载设施配置
                if (j.contains("shopping")) shoppinsettings = j["shopping"].get<ShoppingConfig>();
                if (j.contains("mission")) mission_config = j["mission"].get<MissionConfig>();
                if (j.contains("roguelike")) roguelike_config = j["roguelike"].get<RoguelikeConfig>();
            }
        }
        catch (const std::exception& e) {
            // 初始化默认配置
            memset(&startup_config, 0, sizeof(StartupConfig));
            startup_config.enable = true;
            startup_config.start_game_enabled = true;
            startup_config.ldExtraEnable = false;
            startup_config.ldExtraID = -1;
            strncpy(startup_config.ldExtraPathToConsole, "D:/leidian/LDPlayer9/", 256 - 1);
            strncpy(startup_config.client_type, "Official", STR_BUFFER_SIZE - 1);
            strncpy(startup_config.netaddr, "127.0.0.1:5563", 128 - 1);

            memset(&stage_config, 0, sizeof(StageConfig));
            stage_config.enable = true;
            stage_config.times = 255;
            stage_config.series = -1;
            stage_config.report_to_penguin = false;
            strncpy(stage_config.server, "CN", STR_BUFFER_SIZE - 1);

            // 初始化公招默认配置
            memset(&recruitment_config, 0, sizeof(RecruitmentConfig));
            recruitment_config.enable = true;
            recruitment_config.refresh = true;
            recruitment_config.select = { 3, 4 };
            recruitment_config.confirm = { 3, 4 };
            recruitment_config.extra_tags_mode = 0;
            recruitment_config.times = 3;
            recruitment_config.set_time = true;
            recruitment_config.expedite = false;
            recruitment_config.expedite_times = 0;
            recruitment_config.skip_robot = true;
            recruitment_config.recruitment_time = {};
            recruitment_config.report_to_penguin = false;
            recruitment_config.report_to_yituliu = false;
            strncpy(recruitment_config.server, "CN", STR_BUFFER_SIZE - 1);

            // 初始化设施默认配置
            memset(&facility_config, 0, sizeof(FacilityConfig));
            facility_config.enable = true;
            facility_config.mode = 0;
            // 设置默认设施顺序：Mfg -> Trade -> Power -> Control -> Reception -> Office -> Dorm
            facility_config.facility = { "Mfg", "Trade", "Power", "Control", "Reception", "Office", "Dorm" };
            facility_config.drones = "_NotUse";
            facility_config.threshold = 0.3f;
            facility_config.replenish = false;
            facility_config.dorm_notstationed_enabled = false;
            facility_config.dorm_trust_enabled = false;
            temp_facilities = facility_config.facility;


            // 初始化商店购物默认配置
            memset(&shoppinsettings, 0, sizeof(ShoppingConfig));
            shoppinsettings.enable = true;
            shoppinsettings.shopping = true;
            shoppinsettings.only_buy_discount = false;
            shoppinsettings.reserve_max_credit = false;

            memset(&mission_config, 0, sizeof(MissionConfig));
            mission_config.enable = true;
            mission_config.award = true;

            roguelike_config = RoguelikeConfig(); // 使用结构体默认值
        }
    }

    void save_config() {
        try {
            std::ofstream file(config_path);
            if (file.is_open()) {
                json j;
                j["startup"] = startup_config;
                j["stage"] = stage_config;
                j["recruitment"] = recruitment_config;
                j["facility"] = facility_config;  // 保存设施配置
                j["shopping"] = shoppinsettings;  // 保存购物配置
                j["mission"] = mission_config;  // 保存任务配置
                j["roguelike"] = roguelike_config;
                file << std::setw(4) << j << std::endl;
            }
        }
        catch (const std::exception& e) {
            // 错误处理
        }
    }

    void draw_settings_button() {
        //if (ImGui::Button("打开设置##main_button")) {
        settings_window_open = true;
        //}
    }

    void draw_startup_subpage() {
        // 保持原有实现不变
        if (!startup_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 250), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 280), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("启动参数设置", &startup_settings_open)) {
            draw_label("是否启用本任务");
            ImGui::Checkbox("##startup_enable", &startup_config.enable);
            ImGui::Spacing();

            draw_label("模拟器链接路径");
            ImGui::InputText("##emu_addr", startup_config.netaddr, 128 - 1);
            ImGui::Spacing();

            draw_label("客户端版本");
            ImGui::SetNextItemWidth(input_max_width);
            if (ImGui::BeginCombo("##startup_client", startup_config.client_type)) {
                for (const char* type : client_types) {
                    bool selected = (strcmp(startup_config.client_type, type) == 0);
                    if (ImGui::Selectable(type, selected)) {
                        // 仅在选择不同选项时执行操作
                        if (strcmp(startup_config.client_type, type) != 0) {
                            // 更新配置
                            strncpy(startup_config.client_type, type, STR_BUFFER_SIZE - 1);
                            sync_server_with_client_type();

                            // 保存配置到文件
                            save_config(); // 假设存在此函数用于保存配置

                            std::string app_path = get_exe_fulldirectory().string();

                            // 启动新实例
#ifdef _WIN32
                            ShellExecuteA(NULL, "open", app_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
                            system(("nohup " + app_path + " &").c_str());
#endif

                            // 退出当前实例
                            exit(0);
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            draw_label("自动启动客户端");
            ImGui::Text("已启用");
            ImGui::Spacing();

            draw_label("切换账号");
            bool is_enabled = can_set_account();
            if (!is_enabled) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##account_name", startup_config.account_name, ACCOUNT_BUFFER_SIZE);
            if (!is_enabled) {
                ImGui::EndDisabled();
                ImGui::TextDisabled("仅Official和Bilibili支持");
            }
            else {
                ImGui::TextDisabled("输入账号唯一标识");
            }
            ImGui::Spacing();

            ImGui::SeparatorText("雷电模拟器快速链接设置");

            draw_label("启用雷电快速链接");
            ImGui::Checkbox("##ldex_enable", &startup_config.ldExtraEnable);
            ImGui::Spacing();

            draw_label("雷电快速链接ID");
            ImGui::InputInt("##ldex_idi", &startup_config.ldExtraID, 1, 5);
            ImGui::Spacing();

            draw_label("雷电Console路径");
            ImGui::InputText("##ldex_cpth", startup_config.ldExtraPathToConsole, 256 - 1);
            ImGui::Spacing();

            if (ImGui::Button("保存##startup")) {
                save_config();
                ImGui::OpenPopup("提示##startup");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##startup")) {
                startup_settings_open = false;
            }

            if (ImGui::BeginPopup("提示##startup")) {
                ImGui::Text("设置已保存");
                if (ImGui::Button("确定##startup")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    void draw_stage_subpage() {
        // 保持原有实现不变
        if (!stage_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 400), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("关卡任务设置", &stage_settings_open)) {
            draw_label("是否启用本任务");
            ImGui::Checkbox("##stage_enable", &stage_config.enable);
            ImGui::Spacing();

            draw_label("关卡名");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##stage_name", stage_config.stage, STAGE_BUFFER_SIZE);
            ImGui::SameLine();
            if (ImGui::Button("选择##stage_picker_btn")) {
                strncpy(temp_stage, stage_config.stage, STAGE_BUFFER_SIZE - 1);
                stage_picker_open = true;
            }
            ImGui::TextDisabled("格式：1-7、S3-2Hard等");
            ImGui::Spacing();

            draw_label("理智药数量");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##medicine_count", &stage_config.medicine);
            ImGui::Spacing();

            draw_label("过期理智药数量");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##exp_medicine_count", &stage_config.expiring_medicine);
            ImGui::Spacing();

            draw_label("石头数量");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##stone_count", &stage_config.stone);
            ImGui::Spacing();

            draw_label("战斗次数");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##battle_times", &stage_config.times);
            ImGui::Spacing();

            draw_label("连战次数");
            ImGui::SetNextItemWidth(input_max_width);
            const char* current_series = "未知";
            for (const auto& [val, str] : series_options) {
                if (val == stage_config.series) {
                    current_series = str;
                    break;
                }
            }
            if (ImGui::BeginCombo("##series_combo", current_series)) {
                for (const auto& [val, str] : series_options) {
                    bool selected = (stage_config.series == val);
                    if (ImGui::Selectable(str, selected)) {
                        stage_config.series = val;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            draw_label("指定掉落");
            // 调整表格宽度，为新增列留出空间
            ImGui::PushItemWidth(-300); // 预留足够空间给新列
            if (ImGui::BeginTable("drops_table", 5, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("材料ID", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("材料名称", ImGuiTableColumnFlags_WidthFixed, 120); // 新增材料名称列
                ImGui::TableSetupColumn("数量", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("调整", ImGuiTableColumnFlags_WidthFixed, 80); // 新增增减按钮列
                ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 60); // 新增删除列
                ImGui::TableHeadersRow();

                if (stage_config.drops.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("无数据");
                    // 合并空数据行的所有列
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextDisabled("--");
                }
                else {
                    // 使用非const迭代器以便修改数量
                    for (auto& [id, count] : stage_config.drops) {
                        ImGui::TableNextRow();

                        // 材料ID
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", id.c_str());

                        // 材料名称（从item_index查询）
                        ImGui::TableSetColumnIndex(1);
                        const ItemInfo* item = find_item(id.c_str());
                        if (item) {
                            ImGui::Text("%s", item->name);
                        }
                        else {
                            ImGui::TextDisabled("未知材料");
                        }

                        // 数量
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%d", count);

                        // 增减按钮
                        ImGui::TableSetColumnIndex(3);
                        char add_btn_id[64], sub_btn_id[64];
                        snprintf(add_btn_id, sizeof(add_btn_id), "+##add_%s_main", id.c_str());
                        snprintf(sub_btn_id, sizeof(sub_btn_id), "-##sub_%s_main", id.c_str());

                        if (ImGui::Button(add_btn_id, ImVec2(30, 0))) {
                            count++; // 增加数量
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(sub_btn_id, ImVec2(30, 0)) && count > 1) {
                            count--; // 减少数量，最少为1
                        }

                        // 删除按钮
                        ImGui::TableSetColumnIndex(4);
                        char del_btn_id[64];
                        snprintf(del_btn_id, sizeof(del_btn_id), "删除##del_%s_main", id.c_str());
                        if (ImGui::Button(del_btn_id, ImVec2(-1, 0))) {
                            // 从map中删除该条目
                            stage_config.drops.erase(id);
                            // 由于修改了容器，需要打破循环重新绘制
                            break;
                        }
                    }
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("选择材料##drops_btn", ImVec2(100, 0))) {
                drops_editor_open = true;
            }
            ImGui::Spacing();

            draw_label("汇报企鹅数据");
            ImGui::Checkbox("##report_to_penguin", &stage_config.report_to_penguin);
            ImGui::Spacing();

            draw_label("企鹅ID");
            if (!stage_config.report_to_penguin) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##penguin_id_input", stage_config.penguin_id, PENGUIN_ID_BUFFER_SIZE);
            if (!stage_config.report_to_penguin) ImGui::EndDisabled();
            ImGui::Spacing();

            draw_label("服务器");
            ImGui::Text("%s", stage_config.server);
            ImGui::Spacing();

            draw_label("客户端版本");
            ImGui::Text("%s", stage_config.client_type);
            ImGui::Spacing();

            draw_label("节省碎石模式");
            ImGui::Checkbox("##dr_grandet_mode", &stage_config.DrGrandet);
            ImGui::TextDisabled("等待理智恢复后碎石");
            ImGui::Spacing();

            if (ImGui::Button("保存##stage")) {
                save_config();
                ImGui::OpenPopup("提示##stage");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##stage")) {
                stage_settings_open = false;
            }

            if (ImGui::BeginPopup("提示##stage")) {
                ImGui::Text("设置已保存");
                if (ImGui::Button("确定##stage")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();

        draw_stage_picker();
        draw_drops_editor();
    }

    void draw_recruitment_subpage() {
        // 保持原有实现不变
        if (!recruitment_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 450), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("公招配置", &recruitment_settings_open)) {
            draw_label("是否启用本任务");
            ImGui::Checkbox("##recruitment_enable", &recruitment_config.enable);
            ImGui::Spacing();

            draw_label("选择标签等级");
            ImGui::Text("3, 4");
            ImGui::Spacing();

            draw_label("确认标签等级");
            ImGui::Text("3, 4");
            ImGui::Spacing();

            draw_label("首选标签");
            if (recruitment_config.first_tags.empty()) {
                ImGui::TextDisabled("无标签");
            }
            else {
                ImGui::Text("%zu个标签", recruitment_config.first_tags.size());
            }
            ImGui::SameLine();
            if (ImGui::Button("编辑##tags_btn")) {
                memset(new_tag, 0, TAG_BUFFER_SIZE);
                tags_editor_open = true;
            }
            ImGui::TextDisabled("仅Tag等级为3时有效");
            ImGui::Spacing();

            draw_label("额外标签模式");
            ImGui::SetNextItemWidth(input_max_width);
            const char* current_mode = "未知";
            for (const auto& [val, str] : extra_tags_options) {
                if (val == recruitment_config.extra_tags_mode) {
                    current_mode = str;
                    break;
                }
            }
            if (ImGui::BeginCombo("##extra_tags_mode", current_mode)) {
                for (const auto& [val, str] : extra_tags_options) {
                    bool selected = (recruitment_config.extra_tags_mode == val);
                    if (ImGui::Selectable(str, selected)) {
                        recruitment_config.extra_tags_mode = val;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            draw_label("招募次数");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##recruitment_times", &recruitment_config.times);
            ImGui::Spacing();

            draw_label("使用加急许可");
            ImGui::Checkbox("##expedite", &recruitment_config.expedite);
            ImGui::Spacing();

            draw_label("加急次数");
            if (!recruitment_config.expedite) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##expedite_times", &recruitment_config.expedite_times);
            if (!recruitment_config.expedite) ImGui::EndDisabled();
            ImGui::TextDisabled("0表示无限制");
            ImGui::Spacing();

            draw_label("跳过小车词条");
            ImGui::Checkbox("##skip_robot", &recruitment_config.skip_robot);
            ImGui::Spacing();

            draw_label("汇报企鹅数据");
            ImGui::Checkbox("##recruit_penguin", &recruitment_config.report_to_penguin);
            ImGui::Spacing();

            draw_label("企鹅ID");
            if (!recruitment_config.report_to_penguin) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##recruit_penguin_id", recruitment_config.penguin_id, PENGUIN_ID_BUFFER_SIZE);
            if (!recruitment_config.report_to_penguin) ImGui::EndDisabled();
            ImGui::Spacing();

            draw_label("汇报一图流数据");
            ImGui::Checkbox("##report_to_yituliu", &recruitment_config.report_to_yituliu);
            ImGui::Spacing();

            draw_label("一图流ID");
            if (!recruitment_config.report_to_yituliu) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputText("##yituliu_id", recruitment_config.yituliu_id, YITULIU_ID_BUFFER_SIZE);
            if (!recruitment_config.report_to_yituliu) ImGui::EndDisabled();
            ImGui::Spacing();

            draw_label("服务器");
            ImGui::SetNextItemWidth(input_max_width);
            if (ImGui::BeginCombo("##recruit_server", recruitment_config.server)) {
                for (const char* server : server_options) {
                    bool selected = (strcmp(recruitment_config.server, server) == 0);
                    if (ImGui::Selectable(server, selected)) {
                        strncpy(recruitment_config.server, server, STR_BUFFER_SIZE - 1);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            if (ImGui::Button("保存##recruitment")) {
                save_config();
                ImGui::OpenPopup("提示##recruitment");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##recruitment")) {
                recruitment_settings_open = false;
            }

            if (ImGui::BeginPopup("提示##recruitment")) {
                ImGui::Text("设置已保存");
                if (ImGui::Button("确定##recruitment")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();

        draw_tags_editor();
    }

    // 新增：设施配置子页面
    void draw_facility_subpage() {
        if (!facility_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 400), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("设施换班配置", &facility_settings_open)) {
            // 启用本任务
            draw_label("是否启用本任务");
            ImGui::Checkbox("##facility_enable", &facility_config.enable);
            ImGui::Spacing();

            // 模式选择
            draw_label("模式");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputInt("##facility_mode", &facility_config.mode);
            ImGui::Spacing();

            // 换班设施列表
            draw_label("换班设施");
            if (facility_config.facility.empty()) {
                ImGui::TextDisabled("无设施");
            }
            else {
                ImGui::Text("%zu个设施", facility_config.facility.size());
            }
            ImGui::SameLine();
            if (ImGui::Button("编辑##facility_btn")) {
                memset(new_facility, 0, FACILITY_BUFFER_SIZE);
                facility_editor_open = true;
            }
            ImGui::TextDisabled("不支持运行中修改");
            ImGui::Spacing();

            // 无人机用途
            draw_label("无人机用途");
            ImGui::SetNextItemWidth(input_max_width);
            const char* current_drone = "未知";
            for (const auto& [val, desc] : drones_options) {
                if (facility_config.drones == val) {
                    current_drone = desc;
                    break;
                }
            }
            if (ImGui::BeginCombo("##drones_usage", current_drone)) {
                for (const auto& [val, desc] : drones_options) {
                    bool selected = (facility_config.drones == val);
                    if (ImGui::Selectable(desc, selected)) {
                        facility_config.drones = val;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            // 工作心情阈值
            draw_label("心情阈值");
            ImGui::SetNextItemWidth(input_max_width);
            ImGui::InputFloat("##mood_threshold", &facility_config.threshold, 0.1f, 0.0f, "%.1f");
            ImGui::TextDisabled("范围 [0, 1.0]，默认 0.3");
            ImGui::Spacing();

            // 贸易站自动补货
            draw_label("自动补货");
            ImGui::Checkbox("##auto_replenish", &facility_config.replenish);
            ImGui::TextDisabled("贸易站“源石碎片”自动补货");
            ImGui::Spacing();

            // 宿舍“未进驻”选项
            draw_label("未进驻选项");
            ImGui::Checkbox("##dorm_notstationed", &facility_config.dorm_notstationed_enabled);
            ImGui::TextDisabled("启用宿舍“未进驻”选项");
            ImGui::Spacing();

            // 宿舍信赖未满选项
            draw_label("信赖未满选项");
            ImGui::Checkbox("##dorm_trust", &facility_config.dorm_trust_enabled);
            ImGui::TextDisabled("宿舍填入信赖未满干员");
            ImGui::Spacing();

            // 保存按钮
            if (ImGui::Button("保存##facility")) {
                save_config();
                ImGui::OpenPopup("提示##facility");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##facility")) {
                facility_settings_open = false;
            }

            if (ImGui::BeginPopup("提示##facility")) {
                ImGui::Text("设置已保存");
                if (ImGui::Button("确定##facility")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();

        draw_facility_editor();  // 绘制设施编辑窗口
    }

    // 新增：购物配置子页面
    void draw_shopping_subpage() {
        if (!shopping_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 250), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("商店购物配置", &shopping_settings_open)) {
            // 启用本任务
            draw_label("是否启用本任务");
            ImGui::Checkbox("##shopping_enable", &shoppinsettings.enable);
            ImGui::Spacing();

            // 是否购物
            draw_label("是否购物");
            ImGui::Checkbox("##do_shopping", &shoppinsettings.shopping);
            ImGui::Spacing();

            // 是否只购买折扣物品
            draw_label("只买折扣物品");
            ImGui::Checkbox("##only_discount", &shoppinsettings.only_buy_discount);
            ImGui::TextDisabled("仅作用于第二轮购买");
            ImGui::Spacing();

            // 信用点低于300时停止购买
            draw_label("保留信用点");
            ImGui::Checkbox("##reserve_credit", &shoppinsettings.reserve_max_credit);
            ImGui::TextDisabled("信用点低于300时停止购买，仅作用于第二轮");
            ImGui::Spacing();

            // 保存按钮
            if (ImGui::Button("保存##shopping")) {
                save_config();
                ImGui::OpenPopup("提示##shopping");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##shopping")) {
                shopping_settings_open = false;
            }

            if (ImGui::BeginPopup("提示##shopping")) {
                ImGui::Text("设置已保存");
                if (ImGui::Button("确定##shopping")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }


    // 新增：任务配置子页面
    void draw_mission_subpage() {
        if (!mission_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 200), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 220), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("任务奖励配置", &mission_settings_open)) {
            // 启用本任务
            draw_label("是否启用本任务");
            ImGui::Checkbox("##mission_enable", &mission_config.enable);
            ImGui::Spacing();

            // 领取每日/每周任务奖励
            draw_label("领取任务奖励");
            ImGui::Checkbox("##get_award", &mission_config.award);
            ImGui::TextDisabled("包括每日任务和每周任务奖励");
            ImGui::Spacing();

            // 保存按钮
            if (ImGui::Button("保存##mission")) {
                save_config();
                ImGui::OpenPopup("提示##mission");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##mission")) {
                mission_settings_open = false;
            }

            if (ImGui::BeginPopup("提示##mission")) {
                ImGui::Text("设置已保存");
                if (ImGui::Button("确定##mission")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    // 绘制奖励配置编辑器（带主题条件控制）
    void draw_collectible_editor(CollectibleStartList& list, const std::string& theme) {
        ImGui::Text("期望奖励配置:");
        ImGui::Spacing();

        // 通用奖励
        ImGui::Checkbox("热水壶奖励##hw", &list.hot_water);
        ImGui::Checkbox("护盾奖励##sh", &list.shield);
        ImGui::Checkbox("源石锭奖励##ig", &list.ingot);

        // 希望奖励（JieGarden不可用）
        if (theme != "JieGarden") {
            ImGui::Checkbox("希望奖励##hp", &list.hope);
        }
        else {
            if (list.hope) list.hope = false; // 强制重置
            ImGui::BeginDisabled();
            ImGui::Checkbox("希望奖励（当前主题不可用）##hp_dis", &list.hope);
            ImGui::EndDisabled();
        }

        ImGui::Checkbox("随机奖励##rd", &list.random);

        // Mizuki专属奖励
        if (theme == "Mizuki") {
            ImGui::Checkbox("钥匙奖励##ky", &list.key);
            ImGui::Checkbox("骰子奖励##dc", &list.dice);
        }
        else {
            if (list.key) list.key = false;
            if (list.dice) list.dice = false;
            ImGui::BeginDisabled();
            ImGui::Checkbox("钥匙奖励（仅Mizuki可用）##ky_dis", &list.key);
            ImGui::Checkbox("骰子奖励（仅Mizuki可用）##dc_dis", &list.dice);
            ImGui::EndDisabled();
        }

        // Sarkaz专属奖励
        if (theme == "Sarkaz") {
            ImGui::Checkbox("2构想奖励##id", &list.ideas);
        }
        else {
            if (list.ideas) list.ideas = false;
            ImGui::BeginDisabled();
            ImGui::Checkbox("2构想奖励（仅Sarkaz可用）##id_dis", &list.ideas);
            ImGui::EndDisabled();
        }
    }

    // 完整的肉鸽配置界面实现（包含所有配置项）
    void draw_roguelike_config() {
        if (!roguelike_settings_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(800, FLT_MAX));
        if (ImGui::Begin("肉鸽模式配置", &roguelike_settings_open)) {
            // 启用滚动条以适应多内容
            ImGui::BeginChild("ScrollArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2), false, ImGuiWindowFlags_HorizontalScrollbar);

            // 基础配置区域
            ImGui::Text("基础配置");
            ImGui::Separator();

            // 主题选择
            std::string old_theme = roguelike_config.theme;
            ImGui::SetNextItemWidth(300);
            if (ImGui::BeginCombo("主题##theme",
                [this]() {
                    for (const auto& [id, name] : theme_options) {
                        if (strcmp(roguelike_config.theme, id.c_str()) == 0)
                            return name.c_str();
                    }
                    return "未知主题";
                }())) {
                for (const auto& [id, name] : theme_options) {
                    bool selected = (strcmp(roguelike_config.theme, id.c_str()) == 0);
                    if (ImGui::Selectable(name.c_str(), selected)) {
                        strncpy(roguelike_config.theme, id.c_str(), sizeof(roguelike_config.theme) - 1);
                        roguelike_config.theme[sizeof(roguelike_config.theme) - 1] = '\0';
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (strcmp(old_theme.c_str(), roguelike_config.theme) != 0) {
                on_theme_changed(old_theme, roguelike_config.theme);
            }
            ImGui::Spacing();

            // 模式选择
            ImGui::SetNextItemWidth(300);
            if (ImGui::BeginCombo("模式##mode",
                [this]() {
                    for (const auto& [val, desc] : mode_options) {
                        if (val == roguelike_config.mode) return desc.c_str();
                    }
                    return "未知模式";
                }())) {
                for (const auto& [val, desc] : mode_options) {
                    if (ImGui::Selectable(desc.c_str(), roguelike_config.mode == val)) {
                        roguelike_config.mode = val;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            // 开局分队选择
            ImGui::SetNextItemWidth(300);
            auto squads = get_squad_options(roguelike_config.theme);
            if (ImGui::BeginCombo("开局分队##squad", roguelike_config.squad)) {
                for (const auto& squad : squads) {
                    bool selected = (strcmp(roguelike_config.squad, squad.c_str()) == 0);
                    if (ImGui::Selectable(squad.c_str(), selected)) {
                        strncpy(roguelike_config.squad, squad.c_str(), sizeof(roguelike_config.squad) - 1);
                        roguelike_config.squad[sizeof(roguelike_config.squad) - 1] = '\0';
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            // 开局职业组选择
            ImGui::SetNextItemWidth(300);
            auto roles = get_roles_options(roguelike_config.theme);
            if (ImGui::BeginCombo("开局职业组##roles", roguelike_config.roles)) {
                for (const auto& role : roles) {
                    bool selected = (strcmp(roguelike_config.roles, role.c_str()) == 0);
                    if (ImGui::Selectable(role.c_str(), selected)) {
                        strncpy(roguelike_config.roles, role.c_str(), sizeof(roguelike_config.roles) - 1);
                        roguelike_config.roles[sizeof(roguelike_config.roles) - 1] = '\0';
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();

            // 开局干员名
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("开局干员名##core", roguelike_config.core_char, IM_ARRAYSIZE(roguelike_config.core_char));
            ImGui::TextDisabled("仅支持单个干员中文名，留空则自动选择");
            ImGui::Spacing();

            // 助战设置
            ImGui::Checkbox("使用助战干员##support", &roguelike_config.use_support);

            if (!roguelike_config.use_support) ImGui::BeginDisabled();
            ImGui::SameLine(200);
            ImGui::Checkbox("允许非好友助战##nonfriend", &roguelike_config.use_nonfriend_support);
            if (!roguelike_config.use_support) ImGui::EndDisabled();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 运行控制区域
            ImGui::Text("运行控制");
            ImGui::Separator();

            // 开始探索次数
            ImGui::SetNextItemWidth(300);
            ImGui::InputInt("开始探索次数##starts_count", &roguelike_config.starts_count);
            ImGui::TextDisabled("达到次数后自动停止任务，默认999");
            ImGui::Spacing();

            // 难度设置（除Phantom外可编辑）
            if (strcmp(roguelike_config.theme, "Phantom") == 0) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(300);
            ImGui::InputInt("指定难度等级##diff", &roguelike_config.difficulty);
            if (strcmp(roguelike_config.theme, "Phantom") == 0) {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Phantom主题不支持难度设置");
            }
            else {
                ImGui::TextDisabled("若未解锁难度，则使用已解锁的最高难度");
            }
            ImGui::Spacing();

            // 第五层停止（除Phantom外可编辑）
            if (strcmp(roguelike_config.theme, "Phantom") == 0) ImGui::BeginDisabled();
            ImGui::Checkbox("第五层险路恶敌前停止##stop_boss", &roguelike_config.stop_at_final_boss);
            if (strcmp(roguelike_config.theme, "Phantom") == 0) ImGui::EndDisabled();
            ImGui::Spacing();

            // 等级满后停止
            ImGui::Checkbox("肉鸽等级刷满后停止##stop_max_level", &roguelike_config.stop_at_max_level);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 投资相关设置
            ImGui::Text("投资设置");
            ImGui::Separator();

            ImGui::Checkbox("启用源石锭投资##investment_enabled", &roguelike_config.investment_enabled);

            if (!roguelike_config.investment_enabled) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(300);
            ImGui::InputInt("最大投资次数##investments_count", &roguelike_config.investments_count);
            ImGui::TextDisabled("达到次数后自动停止任务，默认无上限");
            ImGui::Spacing();

            ImGui::Checkbox("投资到达上限后停止##stop_when_full", &roguelike_config.stop_when_investment_full);
            ImGui::Spacing();

            // 投资后购物（仅模式1）
            if (roguelike_config.mode != 1) ImGui::BeginDisabled();
            ImGui::Checkbox("投资后尝试购物##investment_shopping", &roguelike_config.investment_with_more_score);
            if (roguelike_config.mode != 1) ImGui::EndDisabled();
            if (roguelike_config.mode != 1) {
                ImGui::TextDisabled("仅模式1（刷源石锭）有效");
            }
            if (!roguelike_config.investment_enabled) ImGui::EndDisabled();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 凹开局设置（仅模式4显示）
            if (roguelike_config.mode == 4) {
                ImGui::Text("凹开局设置");
                ImGui::Separator();

                ImGui::Checkbox("同时凹干员精二直升##elite2", &roguelike_config.start_with_elite_two);

                if (!roguelike_config.start_with_elite_two) ImGui::BeginDisabled();
                ImGui::SameLine(200);
                ImGui::Checkbox("只凹精二直升##only_elite2", &roguelike_config.only_start_with_elite_two);
                if (!roguelike_config.start_with_elite_two) ImGui::EndDisabled();

                // 凹开局奖励列表
                ImGui::Spacing();
                draw_collectible_editor(roguelike_config.collectible_mode_start_list, roguelike_config.theme);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }

            // 主题专属设置
            ImGui::Text("主题专属设置");
            ImGui::Separator();

            // Mizuki专属设置
            if (strcmp(roguelike_config.theme, "Mizuki") == 0) {
                ImGui::Checkbox("用骰子刷新商店（刷指路鳞）##refresh", &roguelike_config.refresh_trader_with_dice);
                ImGui::Spacing();
            }

            // Sami专属设置
            if (strcmp(roguelike_config.theme, "Sami") == 0) {
                ImGui::Checkbox("使用密文板##foldartal", &roguelike_config.use_foldartal);
                ImGui::Checkbox("检测坍缩范式##paradigms", &roguelike_config.check_collapsal_paradigms);

                if (!roguelike_config.check_collapsal_paradigms) ImGui::BeginDisabled();
                ImGui::SameLine(200);
                ImGui::Checkbox("双重检测防漏##double_check", &roguelike_config.double_check_collapsal_paradigms);
                if (!roguelike_config.check_collapsal_paradigms) ImGui::EndDisabled();
                ImGui::Spacing();
            }

            // 其他主题提示
            if (strcmp(roguelike_config.theme, "Mizuki") != 0 &&
                strcmp(roguelike_config.theme, "Sami") != 0) {
                ImGui::TextDisabled("当前主题无专属设置");
                ImGui::Spacing();
            }
            ImGui::Separator();
            ImGui::Spacing();

            // 自动切换设置
            ImGui::Text("自动切换设置");
            ImGui::Separator();

            ImGui::Checkbox("月度小队自动切换##monthly_auto", &roguelike_config.monthly_squad_auto_iterate);
            if (roguelike_config.monthly_squad_auto_iterate) {
                ImGui::SameLine(200);
                ImGui::Checkbox("将通信作为切换依据##monthly_comms", &roguelike_config.monthly_squad_check_comms);
            }
            ImGui::Spacing();

            ImGui::Checkbox("深入调查自动切换##deep_auto", &roguelike_config.deep_exploration_auto_iterate);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 烧水相关配置
            ImGui::Text("烧水相关配置");
            ImGui::Separator();

            ImGui::Checkbox("烧水时启用购物##shopping", &roguelike_config.collectible_mode_shopping);
            ImGui::Spacing();

            // 烧水使用的分队
            ImGui::SetNextItemWidth(300);
            if (ImGui::BeginCombo("烧水时使用的分队##collect_squad", roguelike_config.collectible_mode_squad)) {
                for (const auto& squad : squads) {
                    bool selected = (strcmp(roguelike_config.collectible_mode_squad, squad.c_str()) == 0);
                    if (ImGui::Selectable(squad.c_str(), selected)) {
                        strncpy(roguelike_config.collectible_mode_squad, squad.c_str(),
                            sizeof(roguelike_config.collectible_mode_squad) - 1);
                        roguelike_config.collectible_mode_squad[sizeof(roguelike_config.collectible_mode_squad) - 1] = '\0';
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("默认与开局分队同步");
            ImGui::Spacing();

            // 烧水期望奖励
            draw_collectible_editor(roguelike_config.collectible_mode_collectibles, roguelike_config.theme);
            ImGui::Spacing();

            ImGui::EndChild();

            // 保存/取消按钮
            if (ImGui::Button("保存配置##save")) {
                save_config();
                ImGui::OpenPopup("提示##saved");
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##cancel")) {
                roguelike_settings_open = false;
            }

            // 保存提示弹窗
            if (ImGui::BeginPopup("提示##saved")) {
                ImGui::Text("配置已保存");
                if (ImGui::Button("确定##ok")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }


    void draw_settings_window() {
        if (!settings_window_open) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 300), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(window_min_width, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("设置", &settings_window_open)) {
            ImGui::Text("配置项列表");
            ImGui::Separator();

            // 启动参数设置入口
            ImGui::BeginGroup();
            ImGui::Checkbox("##main_startup_enable", &startup_config.enable);
            ImGui::SameLine();
            draw_label("启动参数配置");
            if (ImGui::Button("配置##main_startup_btn")) {
                startup_settings_open = true;
            }
            ImGui::EndGroup();
            ImGui::Spacing();

            // 关卡任务设置入口
            ImGui::BeginGroup();
            ImGui::Checkbox("##main_stage_enable", &stage_config.enable);
            ImGui::SameLine();
            draw_label("关卡任务配置");
            if (ImGui::Button("配置##main_stage_btn")) {
                stage_settings_open = true;
            }
            ImGui::EndGroup();
            ImGui::Spacing();

            // 公招配置入口
            ImGui::BeginGroup();
            ImGui::Checkbox("##main_recruitment_enable", &recruitment_config.enable);
            ImGui::SameLine();
            draw_label("公招配置");
            if (ImGui::Button("配置##main_recruitment_btn")) {
                recruitment_settings_open = true;
            }
            ImGui::EndGroup();
            ImGui::Spacing();

            // 新增：设施配置入口
            ImGui::BeginGroup();
            ImGui::Checkbox("##main_facility_enable", &facility_config.enable);
            ImGui::SameLine();
            draw_label("设施换班配置");
            if (ImGui::Button("配置##main_facility_btn")) {
                facility_settings_open = true;
            }
            ImGui::EndGroup();

            // 新增：购物配置入口
            ImGui::BeginGroup();
            ImGui::Checkbox("##main_shopping_enable", &shoppinsettings.enable);
            ImGui::SameLine();
            draw_label("商店购物配置");
            if (ImGui::Button("配置##main_shopping_btn")) {
                shopping_settings_open = true;
            }
            ImGui::EndGroup();
            ImGui::Spacing();

            // 新增：任务配置入口
            ImGui::BeginGroup();
            ImGui::Checkbox("##main_mission_enable", &mission_config.enable);
            ImGui::SameLine();
            draw_label("任务奖励配置");
            if (ImGui::Button("配置##main_mission_btn")) {
                mission_settings_open = true;
            }
            ImGui::EndGroup();
            ImGui::Spacing();

            ImGui::BeginGroup();
            ImGui::Checkbox("##roguelike_toggle", &roguelike_config.enable);
            ImGui::SameLine();
            draw_label("肉鸽模式配置");
            if (ImGui::Button("配置##roguelike_btn")) {
                roguelike_settings_open = true;
            }
            ImGui::EndGroup();
            ImGui::Spacing();

            ImGui::Separator();
            ImGui::TextDisabled("勾选框控制功能启用状态");

            if (ImGui::Button("保存并关闭##main_window")) {
                settings_window_open = false;
                save_config();
            }
        }
        ImGui::End();

        draw_startup_subpage();
        draw_stage_subpage();
        draw_recruitment_subpage();
        draw_facility_subpage();  // 绘制设施配置页面
        draw_shopping_subpage();  // 绘制购物配置页面
        draw_mission_subpage();  // 绘制任务配置页面
        draw_roguelike_config();
    }
};
SettingsManager settings("config.json");
namespace fs = std::filesystem;
fs::path exec_dir;
fs::path dbg_dir;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void SetupImGuiFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // 获取Windows字体目录
    char winDir[MAX_PATH];
    GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string fontsDir = std::string(winDir) + "\\Fonts\\";

    // 主字体：英文+中文+常用符号
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false; // 防止重复释放

    // 尝试加载微软雅黑（中文）
    std::string yaheiPath = fontsDir + "msyh.ttc";
    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(
        yaheiPath.c_str(),
        18.0f,
        &config,
        io.Fonts->GetGlyphRangesChineseFull()
    );

    // 备用方案：如果雅黑不存在，使用系统默认
    if (!mainFont) {
        mainFont = io.Fonts->AddFontDefault();
    }

    // 合并日文字体
    config.MergeMode = true; // 启用合并模式

    // 优先尝试Yu Gothic UI (Win8.1+)
    std::string yuGothicPath = fontsDir + "YuGothm.ttc";
    if (GetFileAttributesA(yuGothicPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(
            yuGothicPath.c_str(),
            18.0f,
            &config,
            io.Fonts->GetGlyphRangesJapanese()
        );
    }
    // 回退到MS UI Gothic (WinXP+)
    else {
        std::string uiGothicPath = fontsDir + "msgothic.ttc";
        io.Fonts->AddFontFromFileTTF(
            uiGothicPath.c_str(),
            18.0f,
            &config,
            io.Fonts->GetGlyphRangesJapanese()
        );
    }

    io.FontDefault = mainFont;
}

// 宽字符转ANSI字符串
std::string ConvertWideToANSI(const std::wstring& wstr) {
    if (wstr.empty()) return "";

    // 获取所需缓冲区大小
    int bufferSize = WideCharToMultiByte(
        CP_ACP,               // 使用系统默认ANSI代码页
        0,                    // 转换标志
        wstr.c_str(),         // 宽字符字符串
        -1,                   // 自动计算长度（包含终止符）
        nullptr,              // 输出缓冲区
        0,                    // 输出缓冲区大小（先获取所需大小）
        nullptr, nullptr      // 默认字符（未使用）
    );

    if (bufferSize <= 0) return "";

    // 分配缓冲区并转换
    std::string result(bufferSize, 0);
    WideCharToMultiByte(
        CP_ACP,
        0,
        wstr.c_str(),
        -1,
        &result[0],
        bufferSize,
        nullptr, nullptr
    );

    return result;
}

BOOL GetCmdLine(DWORD pid, std::wstring& cmdline)
{
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (INVALID_HANDLE_VALUE == hProcess) {
        return FALSE;
    }

    PROCESS_BASIC_INFORMATION pbi;
    NTSTATUS status =
        NtQueryInformationProcess(hProcess, ProcessBasicInformation, (PVOID)&pbi, sizeof(PROCESS_BASIC_INFORMATION), 0);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }

    PEB peb;
    BOOL ret = ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(PEB), 0);
    if (FALSE == ret) {
        return FALSE;
    }

    RTL_USER_PROCESS_PARAMETERS upps;
    ret = ReadProcessMemory(hProcess, peb.ProcessParameters, &upps, sizeof(RTL_USER_PROCESS_PARAMETERS), 0);
    if (FALSE == ret) {
        return FALSE;
    }

    USHORT ByteLength = upps.CommandLine.Length + 1;
    WCHAR* buffer = new WCHAR[ByteLength];
    ZeroMemory(buffer, ByteLength * sizeof(WCHAR));
    if (ReadProcessMemory(hProcess, upps.CommandLine.Buffer, buffer, upps.CommandLine.Length, 0)) {
        cmdline = std::wstring(buffer);
        delete[] buffer;
        return TRUE;
    }

    delete[] buffer;
    return FALSE;
}

int queryLdID(int id)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == hSnapshot) {
        return 0;
    }
    PROCESSENTRY32W pi;
    pi.dwSize = sizeof(PROCESSENTRY32); // 第一次使用必须初始化成员
    BOOL bRet = Process32First(hSnapshot, &pi);
    while (bRet) {
        /*
        循环遍历添加自己的额外代码
        */
        // printf("进程ID = %d ,进程路径 = %s\r\n", pi.th32ProcessID, pi.szExeFile);
        std::wstring cmdlinew = L"";
        std::string cmdline = "";
        std::string tobefind = "index=" + std::to_string(id);
        if (ConvertWideToANSI(pi.szExeFile) == std::string("dnplayer.exe")) {
            if (GetCmdLine(pi.th32ProcessID, cmdlinew)) {
                cmdline = ConvertWideToANSI(cmdlinew);
                if (cmdline.find(tobefind) != std::string::npos) {
                    break;
                }
            }
        }
        bRet = Process32Next(hSnapshot, &pi);
    }
    return pi.th32ProcessID;
}


// 获取当前EXE所在的目录
fs::path get_exe_directory() {
    // 初始缓冲区大小，可根据需要调整
    DWORD buffer_size = MAX_PATH;
    std::wstring path_buffer(buffer_size, L'\0');

    // 循环获取完整路径（处理长路径情况）
    while (true) {
        DWORD copied = GetModuleFileNameW(nullptr, &path_buffer[0], buffer_size);

        if (copied == 0) {
            // 获取失败，抛出异常
            throw std::runtime_error("无法获取EXE路径，错误码: " + std::to_string(GetLastError()));
        }

        if (copied < buffer_size) {
            // 成功获取，截断多余字符
            path_buffer.resize(copied);
            break;
        }

        // 缓冲区不足，扩大缓冲区重试
        buffer_size *= 2;
        if (buffer_size > 32768) {  // 防止无限扩大
            throw std::runtime_error("EXE路径过长，超出处理范围");
        }
        path_buffer.resize(buffer_size, L'\0');
    }

    // 转换为filesystem路径并返回父目录
    return fs::path(path_buffer).parent_path();
}

// 获取当前EXE所在的目录
fs::path get_exe_fulldirectory() {
    // 初始缓冲区大小，可根据需要调整
    DWORD buffer_size = MAX_PATH;
    std::wstring path_buffer(buffer_size, L'\0');

    // 循环获取完整路径（处理长路径情况）
    while (true) {
        DWORD copied = GetModuleFileNameW(nullptr, &path_buffer[0], buffer_size);

        if (copied == 0) {
            // 获取失败，抛出异常
            throw std::runtime_error("无法获取EXE路径，错误码: " + std::to_string(GetLastError()));
        }

        if (copied < buffer_size) {
            // 成功获取，截断多余字符
            path_buffer.resize(copied);
            break;
        }

        // 缓冲区不足，扩大缓冲区重试
        buffer_size *= 2;
        if (buffer_size > 32768) {  // 防止无限扩大
            throw std::runtime_error("EXE路径过长，超出处理范围");
        }
        path_buffer.resize(buffer_size, L'\0');
    }

    // 转换为filesystem路径并返回父目录
    return fs::path(path_buffer);
}
// 在全局变量区域添加
enum AppState {
    STATE_INITIALIZING = 0,
    STATE_MAIN_MENU,
    STATE_RUNNING_TASK,
    STATE_TASK_SUCCESS,
    STATE_STOPPING
};

AppState g_currentState = STATE_INITIALIZING;
ID3D11ShaderResourceView* g_backgroundTexture = nullptr;
float g_taskProgress = 0.0f; // 用于状态0的进度
std::string g_taskError = "";
std::string g_taskInfo = "";
std::string g_taskInfoEx = "";

std::atomic<bool> g_taskRunning(false); // 线程运行标志
cv::Mat g_cachedBackground; // OpenCV缓存背景图

// 背景图片加载函数
bool LoadBackgroundTexture(ID3D11Device* device) {
    cv::Mat img;

    // 尝试从磁盘加载 - 使用正确的标志
    try {
        // 使用IMREAD_COLOR确保加载为彩色图
        img = cv::imread("background.png", cv::IMREAD_COLOR);
    }
    catch (...) {
        img = cv::Mat();
    }

    /*
    // 如果加载失败，使用外部资源
    if (img.empty()) {
        // 假设有外部定义的png资源
        extern const unsigned char* EMBEDDED_BACKGROUND_DATA;
        extern const size_t EMBEDDED_BACKGROUND_SIZE;

        if (EMBEDDED_BACKGROUND_DATA && EMBEDDED_BACKGROUND_SIZE > 0) {
            cv::_InputArray buf(EMBEDDED_BACKGROUND_DATA, EMBEDDED_BACKGROUND_SIZE);
            img = cv::imdecode(buf, cv::IMREAD_UNCHANGED);
        }
    }
    */
    // 如果加载失败，使用外部资源或创建默认背景
    if (img.empty()) {
        // 创建彩色默认背景
        img = cv::Mat(540, 960, CV_8UC3, cv::Scalar(30, 30, 30));
    }

    // 确保图片尺寸正确 (960x540)
    if (img.cols != 960 || img.rows != 540) {
        cv::resize(img, img, cv::Size(960, 540));
    }

    // 暗淡化处理 (0.8倍亮度)
    img.convertTo(img, -1, 0.8);

    // 转换到BGRA格式 (添加alpha通道)
    cv::Mat bgraImg;
    cv::cvtColor(img, bgraImg, cv::COLOR_BGR2BGRA);

    // 缓存OpenCV副本
    g_cachedBackground = bgraImg.clone();

    // 创建DX11纹理
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = bgraImg.cols;
    desc.Height = bgraImg.rows;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 匹配BGRA格式
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = bgraImg.data;
    subResource.SysMemPitch = bgraImg.step;
    subResource.SysMemSlicePitch = 0;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr)) {
        printf("创建纹理失败: 0x%X\n", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(pTexture, &srvDesc, &g_backgroundTexture);
    pTexture->Release();

    if (FAILED(hr)) {
        printf("创建SRV失败: 0x%X\n", hr);
        return false;
    }

    printf("背景纹理加载成功: %dx%d\n", bgraImg.cols, bgraImg.rows);
    return true;
}

// 弹窗提示用户
void show_message(const std::string& title, const std::string& message) {
    MessageBoxA(
        nullptr,               // 父窗口句柄，nullptr表示无父窗口
        message.c_str(),       // 消息内容
        title.c_str(),         // 窗口标题
        MB_OK | MB_ICONINFORMATION  // 按钮样式和图标
    );
}

// 状态处理函数
void RenderAppState() {
    ImGuiIO& io = ImGui::GetIO();

    // 1. 绘制背景（最低层级）
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    // 关键：添加 NoBringToFrontOnFocus 确保背景在最底层，且不抢占焦点
    ImGui::Begin("Background", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus
    );

    if (g_backgroundTexture) {
        // 保持原始宽高比（960:540 = 16:9）
        const float aspect = 16.0f / 9.0f; // 简化为标准16:9比例
        const ImVec2 windowSize = io.DisplaySize;

        // 计算适配窗口的图像尺寸（保持比例，不超出窗口）
        ImVec2 imageSize;
        if (windowSize.x / windowSize.y > aspect) {
            // 窗口更宽：高度充满窗口，宽度按比例缩放
            imageSize.y = windowSize.y;
            imageSize.x = imageSize.y * aspect;
        }
        else {
            // 窗口更高：宽度充满窗口，高度按比例缩放
            imageSize.x = windowSize.x;
            imageSize.y = imageSize.x / aspect;
        }

        // 计算居中位置（确保在窗口范围内）
        const ImVec2 pos(
            (windowSize.x - imageSize.x) * 0.5f,
            (windowSize.y - imageSize.y) * 0.5f
        );
        if (g_currentState != STATE_RUNNING_TASK) {
            ImGui::GetWindowDrawList()->AddImage(
                g_backgroundTexture,
                pos,                  // 左上角位置
                ImVec2(pos.x + imageSize.x, pos.y + imageSize.y),      // 右下角位置（左上角 + 尺寸）
                ImVec2(0, 0),         // 纹理左上角UV
                ImVec2(1, 1)          // 纹理右下角UV（完整显示纹理）
            );
        }
        else {
            // 在ImGui渲染循环中
            UpdateScreenshotDisplay(g_pd3dDevice, g_pd3dDeviceContext);
            if (g_screenTexture) {
                ImGui::GetWindowDrawList()->AddImage(
                    g_screenTexture,
                    pos,                  // 左上角位置
                    ImVec2(pos.x + imageSize.x, pos.y + imageSize.y),      // 右下角位置（左上角 + 尺寸）
                    ImVec2(0, 0),         // 纹理左上角UV
                    ImVec2(1, 1)          // 纹理右下角UV（完整显示纹理）
                );
            }
        }
    }
    ImGui::End();

    // 状态特定UI
    switch (g_currentState) {
    case STATE_INITIALIZING:
        RenderInitializingState();
        break;
    case STATE_MAIN_MENU:
        RenderMainMenuState();
        break;
    case STATE_RUNNING_TASK:
        RenderRunningTaskState();
        break;
    case STATE_TASK_SUCCESS:
        RenderTaskSuccessState();
        break;
    case STATE_STOPPING:
        RenderStoppingState();
        break;
    }
}

// 在所有状态函数中，将:
// ImVec2 center = io.DisplaySize * 0.5f;
// 替换为手动计算每个分量:

void RenderInitializingState() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Initializing", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove);

    // 修正：手动计算中心位置
    float centerX = io.DisplaySize.x * 0.5f;
    float centerY = io.DisplaySize.y * 0.5f;

    ImGui::SetCursorPos(ImVec2(centerX - 150, centerY - 20));
    ImGui::Text(g_taskInfo.c_str());
    //g_taskError
    if (g_taskError != "") {
        ImGui::SetCursorPos(ImVec2(centerX - 150, centerY));
        ImGui::TextColored(ImVec4(1, 1, 0, 1), g_taskError.c_str());
    }
    else {
        ImGui::SetCursorPos(ImVec2(centerX - 150, centerY));
        ImGui::ProgressBar(g_taskProgress, ImVec2(300, 20));
    }

    ImGui::SetCursorPos(ImVec2(centerX - 150, centerY + 20));
    ImGui::Text(g_taskInfoEx.c_str());

    ImGui::SetCursorPos(ImVec2(centerX - 180, centerY - 4));
    ImGui::Spinner("##spinner", 10, 2, ImGui::GetColorU32(ImGuiCol_ButtonHovered));

    ImGui::End();
}

void* c_arg = nullptr;
// 全局状态变量（假设已定义）
const char* g_serverRegion;  // 区服信息
const char* g_deviceAddress; // 设备地址

// 窗口显示状态跟踪
static bool s_showTaskChains = false;
static bool s_showConnectionStatus = false;
static bool s_showNotifications = false;
static bool s_needsNotificationAutoOpen = false;
void RenderMainMenuState() {
    AssistantStatus status = get_assistant_status();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    // 确保窗口创建成功后再进行后续操作
    if (ImGui::Begin("Main Menu", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("开始！")) {
                if (ImGui::MenuItem("启动所有任务")) {
                    // 正确处理asstPtr的生命周期
                    if (!asstPtr) {
                        asstPtr = AsstCreateEx(AsstCallbackHandler, c_arg);
                    }
                    else {
                        // 先停止再销毁，确保资源释放
                        AsstStop(asstPtr);
                        AsstDestroy(asstPtr);
                        asstPtr = AsstCreateEx(AsstCallbackHandler, c_arg);
                    }

                    // 按照指定结构构建JSON
                    nlohmann::json j;

                    // 1. 启动模块配置
                    j["startup"] = settings.startup_config;
                    std::string startup_json = j["startup"].dump();
                    AsstAppendTask(asstPtr, "StartUp", startup_json.c_str());

                    // 2. 战斗模块配置
                    j["stage"] = settings.stage_config;
                    std::string fight_json = j["stage"].dump();
                    AsstAppendTask(asstPtr, "Fight", fight_json.c_str());

                    // 3. 招募模块配置
                    j["recruitment"] = settings.recruitment_config;
                    std::string recruit_json = j["recruitment"].dump();
                    AsstAppendTask(asstPtr, "Recruit", recruit_json.c_str());

                    // 4. 基建模块配置
                    j["facility"] = settings.facility_config;
                    std::string infrast_json = j["facility"].dump();
                    AsstAppendTask(asstPtr, "Infrast", infrast_json.c_str());

                    // 5. 商店模块配置
                    j["shopping"] = settings.shoppinsettings;
                    std::string mall_json = j["shopping"].dump();
                    AsstAppendTask(asstPtr, "Mall", mall_json.c_str());

                    // 6. 任务奖励模块配置
                    j["mission"] = settings.mission_config;
                    std::string award_json = j["mission"].dump();
                    AsstAppendTask(asstPtr, "Award", award_json.c_str());

                    // 7. 肉鸽模块配置
                    j["roguelike"] = settings.roguelike_config;
                    std::string roguelike_json = j["roguelike"].dump();
                    AsstAppendTask(asstPtr, "Roguelike", roguelike_json.c_str());

                    std::thread([]() {
                        maa_loop();
                        }).detach();
                    g_currentState = STATE_RUNNING_TASK; // 切换到运行状态
                }

                // 单独启动肉鸽任务
                ImGui::Separator();
                if (ImGui::MenuItem("自动作战")) {
                    showAutoBattleWindow = true;
                }

                ImGui::EndMenu(); // 菜单结束
            }
            if (ImGui::BeginMenu("设置")) {
                settings.draw_settings_button();
                ImGui::EndMenu();
            }

            draw_menu_bar(status);

            ImGui::EndMainMenuBar(); // 菜单栏结束
        }

        ImGui::End(); // 主窗口结束
    }
    settings.draw_settings_window(); // 绘制设置窗口
    // 检查是否需要自动打开通知
    check_notification_auto_open(status);

    // 渲染各个窗口
    show_connection_status(status);
    show_task_chains(status);
    show_current_tasks_and_notifications(status);
    if (showAutoBattleWindow) {
        ShowAutoBattleWindow(&showAutoBattleWindow);
    }
}

// 对话框样式窗口基础函数（兼容版本）
static void BeginDialog(const char* title, bool* p_open = nullptr) {
    // 移除不支持的ImGuiWindowFlags_DialogHeader，使用基础标志组合
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        //ImGuiWindowFlags_TitleBar | 
        ImGuiWindowFlags_AlwaysAutoResize; // 自动调整大小适应内容

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin(title, p_open, flags);
}

static void EndDialog(bool* p_open) {
    // 移除不支持的OpenVarPtr，直接使用传入的p_open指针控制窗口显示
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 右对齐关闭按钮
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80);
    if (ImGui::Button("关闭", ImVec2(70, 28))) {
        if (p_open != nullptr) {
            *p_open = false;
        }
    }

    ImGui::End();
}

// 显示连接状态信息（对话框样式）
void show_connection_status(const AssistantStatus& status) {
    if (!s_showConnectionStatus) return;

    BeginDialog("连接状态", &s_showConnectionStatus);

    ImGui::Text("设备UUID: %s", status.device_uuid.c_str());
    ImGui::Text("连接状态: %s", connection_state_to_cstr(status.connection_state));
    ImGui::Text("ADB路径: %s", status.adb_path.c_str());
    ImGui::Text("设备地址: %s", status.device_address.c_str());
    ImGui::Text("重连次数: %d", status.reconnect_attempts);

    if (ImGui::CollapsingHeader("连接历史")) {
        for (const auto& entry : status.connection_history) {
            ImGui::Text("[%s] %s", entry.first.c_str(), entry.second.c_str());
        }
    }

    EndDialog(&s_showConnectionStatus);
}

// 显示任务链信息（对话框样式）
void show_task_chains(const AssistantStatus& status) {
    if (!s_showTaskChains) return;

    // 静态变量存储状态，跨帧保留信息
    static size_t last_active_count = 0;
    static std::string first_active_chain_id;
    static bool initial_scroll_done = false;

    BeginDialog("任务链状态", &s_showTaskChains);

    // 显示活跃任务数量及详情（保持时长显示不变）
    ImGui::Text("活跃任务数量: %zu", status.active_task_count);

    // 活跃任务详情（仅当有活跃任务时显示）
    if (status.active_task_count > 0) {
        ImGui::Indent();
        // 记录第一个活跃任务ID
        std::string current_first_active;
        for (const auto& [chain_id, chain] : status.task_chains) {
            if (chain.active) {
                const char* chain_name = task_chain_type_to_cstr(chain.type);
                const char* runtime = get_task_chain_runtime_cstr(chain_id);
                ImGui::Text("- %s: %s", chain_name, runtime);

                if (current_first_active.empty()) {
                    current_first_active = chain_id;
                }
            }
        }
        ImGui::Unindent();

        // 检查活跃任务状态是否变化
        bool active_state_changed = (status.active_task_count != last_active_count) ||
            (status.active_task_count > 0 && current_first_active != first_active_chain_id);

        // 更新状态记录
        last_active_count = status.active_task_count;
        first_active_chain_id = current_first_active;

        // 仅在初始加载或活跃任务状态变化时需要自动滚动
        initial_scroll_done = !active_state_changed;
    }
    else {
        // 没有活跃任务时重置状态
        last_active_count = 0;
        first_active_chain_id.clear();
        initial_scroll_done = true;
    }

    ImGui::Separator();
    ImGui::Spacing();

    // 任务链列表（标题格式："任务名称 - 状态"）
    for (const auto& [chain_id, chain] : status.task_chains) {
        // 构建任务链标题："任务类型 - 状态"（仅显示活动/结束）
        const char* chain_name = task_chain_type_to_cstr(chain.type);
        const char* status_text = chain.active ? "活动中" : "已结束";

        // 组合标题格式："%s - %s"
        char tree_node_label[256];
        snprintf(tree_node_label, sizeof(tree_node_label), "%s - %s", chain_name, status_text);

        // 对活跃任务设置默认展开
        if (chain.active) {
            ImGui::SetNextItemOpen(true);
        }
        else {
            ImGui::SetNextItemOpen(false);
        }

        // 绘制树节点
        bool is_open = ImGui::TreeNode(tree_node_label);

        // 仅在初始状态或活跃任务变化时自动滚动到第一个活跃任务
        if (chain.active && !initial_scroll_done && chain_id == first_active_chain_id) {
            // 滚动到当前项目位置（0.3f表示顶部留30%空间）
            ImGui::SetScrollHereY(0.3f);
            initial_scroll_done = true; // 标记滚动完成
        }

        if (is_open) {
            ImGui::Text("任务ID: %s", chain_id.c_str());
            ImGui::Text("状态: %s", chain.active ? "活跃中" : "已完成");
            ImGui::Text("运行时间: %s", get_task_chain_runtime_cstr(chain_id));

            if (!chain.error_msg.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "错误: %s", chain.error_msg.c_str());
            }

            if (ImGui::CollapsingHeader("任务链详情")) {
                ImGui::TextWrapped("%s", chain.extra_info.c_str());
            }

            if (ImGui::CollapsingHeader("子任务", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& [sub_task_name, sub_task] : chain.sub_tasks) {
                    // 对子任务也做类似处理：活跃任务的子任务默认展开
                    if (chain.active && !sub_task.completed) {
                        ImGui::SetNextItemOpen(true);
                    }
                    else {
                        ImGui::SetNextItemOpen(false);
                    }

                    if (ImGui::TreeNode(sub_task_name.c_str())) {
                        ImGui::Text("子任务类型: %s", sub_task_type_to_cstr(sub_task.type));
                        ImGui::Text("状态: %s", sub_task.completed ? "已完成" : "进行中");

                        if (!sub_task.error_msg.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "错误: %s", sub_task.error_msg.c_str());
                        }

                        if (!sub_task.extra_info.empty()) {
                            if (ImGui::CollapsingHeader("子任务详情")) {
                                ImGui::TextWrapped("%s", sub_task.extra_info.c_str());
                            }
                        }

                        ImGui::TreePop();
                    }
                }
            }

            ImGui::TreePop();
        }
    }

    EndDialog(&s_showTaskChains);
}

// 显示当前任务和通知（对话框样式）
void show_current_tasks_and_notifications(const AssistantStatus& status) {
    // 检查是否需要自动打开
    if (!s_showNotifications && s_needsNotificationAutoOpen) {
        s_showNotifications = true;
        s_needsNotificationAutoOpen = false;
    }

    if (!s_showNotifications) return;

    BeginDialog("当前状态与通知", &s_showNotifications);

    if (!status.current_sub_task.empty()) {
        ImGui::Text("当前子任务: %s", sub_task_type_to_cstr(status.current_sub_task_type));
    }

    if (!status.last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "错误: %s", status.last_error.c_str());
    }

    if (!status.last_sub_task_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "子任务错误: %s", status.last_sub_task_error.c_str());
    }

    if (ImGui::CollapsingHeader("通知", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& notification : status.notifications) {
            ImGui::Text("[%s] %s", notification.first.c_str(), notification.second.c_str());
        }
    }

    if (ImGui::CollapsingHeader("异步调用记录")) {
        for (const auto& record : status.async_call_records) {
            ImGui::Text("[%s] %s (ID: %d, 耗时: %dms, 结果: %s)",
                record.timestamp.c_str(),
                record.what.c_str(),
                record.async_call_id,
                record.cost,
                record.ret ? "成功" : "失败");
        }
    }

    EndDialog(&s_showNotifications);
}

// 绘制菜单内容（移除主菜单栏头尾）
void draw_menu_bar(const AssistantStatus& status) {
    // 状态相关菜单
    if (g_currentState == STATE_MAIN_MENU) {
        // 主菜单状态
        if (ImGui::BeginMenu("查看")) {
            if (ImGui::MenuItem("上一次任务链信息")) {
                s_showTaskChains = true;

                s_showNotifications = true;
            }
            if (ImGui::MenuItem("连接状态")) {
                s_showConnectionStatus = true;
            }
            ImGui::EndMenu();
        }
    }
    else if (g_currentState == STATE_RUNNING_TASK) {
        // 1. 当前任务链菜单（动态标题）
        const char* activeChainName = "无";
        for (const auto& [chain_id, chain] : status.task_chains) {
            if (chain.active) {
                activeChainName = task_chain_type_to_cstr(chain.type);
                break;
            }
        }
        char taskChainMenuTitle[256];
        snprintf(taskChainMenuTitle, sizeof(taskChainMenuTitle),
            "当前任务链：%s", activeChainName);

        if (ImGui::BeginMenu(taskChainMenuTitle)) {
            ImGui::Text("当前任务链: %s", activeChainName);
            ImGui::Separator();
            if (ImGui::MenuItem("查看详情")) {
                s_showTaskChains = true;
            }
            ImGui::EndMenu();
        }

        // 2. 通知菜单
        if (ImGui::BeginMenu("通知")) {
            if (ImGui::MenuItem("查看所有通知")) {
                s_showNotifications = true;
            }
            ImGui::EndMenu();
        }

        // 3. 区服和设备地址菜单
        char serverDeviceMenuTitle[256];

        const char* region = g_serverRegion ? g_serverRegion : "未知区服";
        const char* device = g_deviceAddress ? g_deviceAddress : "未知地址";
        snprintf(serverDeviceMenuTitle, sizeof(serverDeviceMenuTitle),
            "%s - %s", region, device);

        if (ImGui::BeginMenu(serverDeviceMenuTitle)) {
            ImGui::Text("区服: %s", region);
            ImGui::Text("设备地址: %s", device);
            ImGui::Separator();
            if (ImGui::MenuItem("查看连接状态")) {
                s_showConnectionStatus = true;
            }
            ImGui::EndMenu();
        }
    }
}


// 检查是否需要自动打开通知（在主循环中调用）
void check_notification_auto_open(const AssistantStatus& status) {
    if (g_currentState == STATE_RUNNING_TASK) {
        // 如果有错误信息则需要自动打开通知
        if (!status.last_error.empty() || !status.last_sub_task_error.empty()) {
            s_needsNotificationAutoOpen = true;
        }
    }
}

void RenderRunningTaskState() {
    AssistantStatus status = get_assistant_status();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Processing", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("停止！")) {
            g_currentState = STATE_STOPPING;

            AsstStop(asstPtr);
            ImGui::EndMenu(); // 菜单结束
        }
        draw_menu_bar(status);


        ImGui::EndMainMenuBar(); // 菜单结束
    }
    // 修正：手动计算中心位置
    float centerX = io.DisplaySize.x * 0.5f;
    float centerY = io.DisplaySize.y * 0.5f;

    ImGui::SetCursorPos(ImVec2(centerX - 100, centerY - 30));
    ImGui::Text("Task in progress...");

    ImGui::SetCursorPos(ImVec2(centerX - 15, centerY + 10));
    ImGui::Spinner("##spinner", 30, 6, ImGui::GetColorU32(ImGuiCol_ButtonHovered));

    ImGui::End();

    // 检查是否需要自动打开通知
    check_notification_auto_open(status);

    // 渲染各个窗口
    show_connection_status(status);
    show_task_chains(status);
    show_current_tasks_and_notifications(status);
}

void RenderTaskSuccessState() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Success", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove);

    // 修正：手动计算中心位置
    float centerX = io.DisplaySize.x * 0.5f;
    float centerY = io.DisplaySize.y * 0.5f;

    ImGui::SetCursorPos(ImVec2(centerX - 90, centerY - 20));
    if (ImGui::Button("Stop", ImVec2(180, 40))) {
        // ... 按钮逻辑
    }

    ImGui::End();
}

void RenderStoppingState() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Stopping", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove);

    // 修正：手动计算中心位置
    float centerX = io.DisplaySize.x * 0.5f;
    float centerY = io.DisplaySize.y * 0.5f;

    ImGui::SetCursorPos(ImVec2(centerX - 150, centerY - 30));
    ImGui::Text("Stopping task, please wait...");

    ImGui::SetCursorPos(ImVec2(centerX - 15, centerY + 10));
    ImGui::Spinner("##spinner", 30, 6, ImGui::GetColorU32(ImGuiCol_ButtonHovered));

    ImGui::End();
}

// 初始化函数（带进度回调）
void maa_initiating(std::function<void(float)> progressCallback) {
    g_taskError = "";
    g_taskInfo = "正在查验显卡";
    g_taskInfoEx = "";

    int selected_gpu_index = select_best_gpu_adapter();
    AsstSetStaticOption(2, std::to_string(selected_gpu_index).c_str());

    g_taskError = "";
    g_taskInfo = "正在验证MAA基本资源";
    g_taskInfoEx = "";
    try {
        fs::path exe_dir = get_exe_directory();
        exec_dir = exe_dir;
        g_taskInfoEx = exec_dir.string();
    }
    catch (const std::exception& e) {
        g_taskError = "请通过常规方式打开RSS,无工作目录的情况下RSS不能运行。";
        Sleep(5000);
        exit(-1);
    }
    dbg_dir = exec_dir / "debug";
    AsstSetUserDir(dbg_dir.string().c_str());


    progressCallback(0.10f); // 更新进度

    if (!AsstLoadResource(exec_dir.string().c_str())) {
        g_taskError = "基本 resource 丢失或不完整。";
        Sleep(5000);
        exit(-1);
    }


    progressCallback(0.50f); // 更新进度

    g_taskInfo = "正在验证区服特定资源";
    g_taskInfoEx = "";
    g_taskError = "";

    fs::path ressExPathBase = exec_dir / "resource" / "global";
    fs::path ressExPath;
    if (strcmp(settings.startup_config.client_type, "YoStarJP") == 0) {
        ressExPath = ressExPathBase / "YoStarJP";
        if (!AsstLoadResource(ressExPath.string().c_str())) {
            g_taskError = "进阶 resource 丢失或不完整。";
            Sleep(5000);
            exit(-1);
        }
    }
    if (strcmp(settings.startup_config.client_type, "YoStarEN") == 0) {
        ressExPath = ressExPathBase / "YoStarEN";
        if (!AsstLoadResource(ressExPath.string().c_str())) {
            g_taskError = "进阶 resource 丢失或不完整。";
            Sleep(5000);
            exit(-1);
        }
    }
    if (strcmp(settings.startup_config.client_type, "YoStarKR") == 0) {
        ressExPath = ressExPathBase / "YoStarKR";
        if (!AsstLoadResource(ressExPath.string().c_str())) {
            g_taskError = "进阶 resource 丢失或不完整。";
            Sleep(5000);
            exit(-1);
        }
    }
    if (strcmp(settings.startup_config.client_type, "txwy") == 0) {
        ressExPath = ressExPathBase / "txwy";
        if (!AsstLoadResource(ressExPath.string().c_str())) {
            g_taskError = "进阶 resource 丢失或不完整。";
            Sleep(5000);
            exit(-1);
        }
    }

    progressCallback(0.70f); // 更新进度

    g_taskInfo = "正在加载数据...";
    g_taskInfoEx = "";
    g_taskError = "";
    load_stages();
    load_item_index();

    progressCallback(1.00f); // 更新进度
}
// 截图线程函数 - 仅获取数据，不处理D3D资源
void ScreenshotThread(AsstHandle asstPtr) {
    const size_t BUFFER_SIZE = 4 * 1024 * 1024; // 1MB
    std::vector<unsigned char> buffer(BUFFER_SIZE);

    while (g_threadRunning) {
        // 触发截图
        AsstAsyncScreencap(asstPtr, false);

        // 获取截图数据
        size_t actualSize = AsstGetImage(asstPtr, buffer.data(), BUFFER_SIZE);

        if (actualSize > 0) {
            // 复制数据到新缓冲区
            std::vector<unsigned char> imageData(buffer.begin(), buffer.begin() + actualSize);

            // 添加到队列
            {
                std::lock_guard<std::mutex> lock(g_imageMutex);
                // 只保留最新帧
                while (!g_imageQueue.empty()) {
                    g_imageQueue.pop();
                }
                g_imageQueue.push(std::move(imageData));
                g_newImageAvailable = true;
            }
        }

        // 等待500毫秒
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

// 启动截图线程
void StartScreenshotThread(AsstHandle asstPtr) {
    if (g_threadRunning) return;

    g_threadRunning = true;
    g_captureThread = std::thread([asstPtr] {
        ScreenshotThread(asstPtr);
        });
}

// 停止截图线程
void StopScreenshotThread() {
    g_threadRunning = false;
    if (g_captureThread.joinable()) {
        g_captureThread.join();
    }
}

// 在主线程中处理截图数据
void ProcessScreenshotInMainThread(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!g_newImageAvailable) return;

    std::vector<unsigned char> imageData;
    {
        std::lock_guard<std::mutex> lock(g_imageMutex);
        if (g_imageQueue.empty()) return;

        imageData = std::move(g_imageQueue.front());
        g_imageQueue.pop();
        g_newImageAvailable = false;
    }

    // 解码PNG图像
    cv::_InputArray buf(imageData.data(), imageData.size());
    cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);

    if (img.empty()) {
        printf("图像解码失败\n");
        return;
    }

    // 转换为BGRA格式
    cv::Mat bgraImg;
    cv::cvtColor(img, bgraImg, cv::COLOR_BGR2BGRA);

    // 保存当前图像副本
    g_currentScreenImage = bgraImg.clone();

    // 检查纹理尺寸是否变化
    bool sizeChanged = (g_textureWidth != bgraImg.cols || g_textureHeight != bgraImg.rows);

    // 创建或更新纹理
    if (sizeChanged) {
        // 释放旧资源
        if (g_screenTexture) {
            g_screenTexture->Release();
            g_screenTexture = nullptr;
        }
        if (g_screenTextureResource) {
            g_screenTextureResource->Release();
            g_screenTextureResource = nullptr;
        }

        // 创建新纹理
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = bgraImg.cols;
        desc.Height = bgraImg.rows;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC; // 使用动态纹理以便更新
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // 允许CPU写入

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &g_screenTextureResource);
        if (FAILED(hr)) {
            printf("创建屏幕纹理失败: 0x%X\n", hr);
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        hr = device->CreateShaderResourceView(g_screenTextureResource, &srvDesc, &g_screenTexture);
        if (FAILED(hr)) {
            printf("创建屏幕SRV失败: 0x%X\n", hr);
            g_screenTextureResource->Release();
            g_screenTextureResource = nullptr;
            return;
        }

        g_textureWidth = bgraImg.cols;
        g_textureHeight = bgraImg.rows;
        printf("创建新屏幕纹理: %dx%d\n", bgraImg.cols, bgraImg.rows);
    }

    // 更新纹理内容
    if (g_screenTextureResource) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = context->Map(
            g_screenTextureResource,
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mappedResource
        );

        if (SUCCEEDED(hr)) {
            // 确保尺寸匹配
            if (mappedResource.RowPitch == static_cast<UINT>(bgraImg.step)) {
                memcpy(mappedResource.pData, bgraImg.data, bgraImg.step * bgraImg.rows);
            }
            else {
                // 行步长不匹配，逐行复制
                unsigned char* dst = static_cast<unsigned char*>(mappedResource.pData);
                const unsigned char* src = bgraImg.data;
                const size_t rowSize = bgraImg.cols * 4; // BGRA每像素4字节

                for (int y = 0; y < bgraImg.rows; y++) {
                    memcpy(dst, src, rowSize);
                    dst += mappedResource.RowPitch;
                    src += bgraImg.step;
                }
            }

            context->Unmap(g_screenTextureResource, 0);
        }
        else {
            printf("映射纹理失败: 0x%X\n", hr);
        }
    }
}

// 在渲染循环中调用此函数
void UpdateScreenshotDisplay(ID3D11Device* device, ID3D11DeviceContext* context) {
    // 处理截图数据（在主线程）
    if (g_newImageAvailable) {
        ProcessScreenshotInMainThread(device, context);
    }
    /*
    // 在ImGui中显示截图
    if (g_screenTexture) {
        // 保持原始宽高比
        float aspectRatio = static_cast<float>(g_textureWidth) /
                           static_cast<float>(g_textureHeight);
        float displayHeight = ImGui::GetContentRegionAvail().y * 0.8f;
        float displayWidth = displayHeight * aspectRatio;

        // 居中显示
        float availWidth = ImGui::GetContentRegionAvail().x;
        if (displayWidth > availWidth) {
            displayWidth = availWidth;
            displayHeight = displayWidth / aspectRatio;
        }

        float indent = (availWidth - displayWidth) * 0.5f;
        if (indent > 0.0f) {
            ImGui::Indent(indent);
        }

        // 显示图像
        ImGui::Image(
            (void*)g_screenTexture,
            ImVec2(displayWidth, displayHeight)
        );

        if (indent > 0.0f) {
            ImGui::Unindent(indent);
        }
    }
    */
}

// 在程序退出时调用
void CleanupScreenshotResources() {
    StopScreenshotThread();

    if (g_screenTexture) {
        g_screenTexture->Release();
        g_screenTexture = nullptr;
    }
    if (g_screenTextureResource) {
        g_screenTextureResource->Release();
        g_screenTextureResource = nullptr;
    }

    g_textureWidth = 0;
    g_textureHeight = 0;
}



// 辅助函数：显示干员信息
void displayOperatorInfo(const json& op) {
    ImGui::Text("干员: %s", op["name"].get<std::string>().c_str());

    if (op.contains("skill") && !op["skill"].is_null()) {
        ImGui::Text("技能: %d", op["skill"].get<int>());
    }

    if (op.contains("requirements") && !op["requirements"].is_null()) {
        const json& req = op["requirements"];

        if (req.contains("elite") && !req["elite"].is_null()) {
            ImGui::Text("精英化: %d", req["elite"].get<int>());
        }

        if (req.contains("skill_level") && !req["skill_level"].is_null()) {
            int level = req["skill_level"].get<int>();
            std::string skillLevelStr;

            if (level <= 7) {
                skillLevelStr = std::to_string(level) + "级";
            }
            else if (level == 8) {
                skillLevelStr = "专一";
            }
            else if (level == 9) {
                skillLevelStr = "专二";
            }
            else if (level == 10) {
                skillLevelStr = "专三";
            }

            ImGui::Text("技能等级: %s", skillLevelStr.c_str());
        }

        if (req.contains("module") && !req["module"].is_null()) {
            int module = req["module"].get<int>();
            std::string moduleStr;

            if (module == -1) {
                moduleStr = "无指定/无模组";
            }
            else {
                moduleStr = "等级 " + std::to_string(module);
            }

            ImGui::Text("模组: %s", moduleStr.c_str());
        }
    }
}

// 辅助函数：生成干员列表项标题（修正格式）
std::string getOperatorListItemText(const json& op) {
    std::vector<std::string> parts;
    parts.push_back(op["name"].get<std::string>()); // 干员名称

    // 精英化信息
    if (op.contains("requirements") && op["requirements"].contains("elite") && !op["requirements"]["elite"].is_null()) {
        parts.push_back("精英" + std::to_string(op["requirements"]["elite"].get<int>()));
    }

    // 技能等级和技能信息
    if (op.contains("skill") && !op["skill"].is_null()) {
        int skillLevel = -1;
        if (op.contains("requirements") && op["requirements"].contains("skill_level") && !op["requirements"]["skill_level"].is_null()) {
            skillLevel = op["requirements"]["skill_level"].get<int>();
        }

        std::string skillLevelStr;
        if (skillLevel != -1) {
            if (skillLevel <= 7) {
                skillLevelStr = std::to_string(skillLevel) + "级";
            }
            else if (skillLevel == 8) {
                skillLevelStr = "专一";
            }
            else if (skillLevel == 9) {
                skillLevelStr = "专二";
            }
            else if (skillLevel == 10) {
                skillLevelStr = "专三";
            }
        }
        else {
            skillLevelStr = "未指定";
        }
        parts.push_back(skillLevelStr + std::to_string(op["skill"].get<int>()) + "技能");
    }

    // 模组信息
    if (op.contains("requirements") && op["requirements"].contains("module") && !op["requirements"]["module"].is_null()) {
        int module = op["requirements"]["module"].get<int>();
        std::string moduleStr;
        if (module == -1) {
            moduleStr = "无指定模组";
        }
        else {
            moduleStr = "模组" + std::to_string(module);
        }
        parts.push_back(moduleStr);
    }

    // 用逗号拼接所有部分
    std::string text;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) text += ",";
        text += parts[i];
    }
    return text;
}

// 辅助函数：显示干员详细信息（补充信息）
void displayOperatorInfoEx(const json& op) {
    // 只显示额外的详细信息，不再重复显示标题中已有的内容
    if (op.contains("skill_usage") && !op["skill_usage"].is_null()) {
        ImGui::Text("技能使用次数: %d", op["skill_usage"].get<int>());
    }
}

// 显示自动作战窗口的函数
void ShowAutoBattleWindow(bool* p_open) {
    if (!ImGui::Begin("自动作战", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // 神秘代码输入区域
    ImGui::Text("神秘代码:");
    ImGui::SameLine();
    ImGui::InputText("##CodeInput", g_autoBattleState.codeInput, IM_ARRAYSIZE(g_autoBattleState.codeInput));
    ImGui::SameLine();

    if (ImGui::Button("查询")) {
        // 清空本地文件路径
        memset(g_autoBattleState.filePath, 0, sizeof(g_autoBattleState.filePath));

        std::string code = g_autoBattleState.codeInput;
        if (code.empty()) {
            ImGui::OpenPopup("提示");
            return;
        }

        // 检查是否为纯数字
        if (!isAllDigits(code)) {
            ImGui::OpenPopup("错误");
            return;
        }

        // 下载文件
        std::string url = "https://prts.maa.plus/copilot/get/" + code;
        std::string tempInfoPath = "cache/temp_copilotinfo.json";

        if (downloadFile(url, tempInfoPath)) {
            // 读取并解析文件
            std::ifstream infoFile(tempInfoPath);
            if (infoFile.is_open()) {
                try {
                    json infoJson;
                    infoFile >> infoJson;
                    infoFile.close();

                    if (infoJson.contains("status_code") && infoJson["status_code"] == 200 &&
                        infoJson.contains("data")) {

                        g_autoBattleState.copilotInfo = infoJson["data"];

                        // 解析content字段
                        if (g_autoBattleState.copilotInfo.contains("content")) {
                            std::string content = g_autoBattleState.copilotInfo["content"].get<std::string>();
                            g_autoBattleState.copilotContent = json::parse(content);

                            // 保存到cache/temp_copilot.json
                            std::string tempContentPath = "cache/temp_copilot.json";
                            std::ofstream contentFile(tempContentPath);
                            if (contentFile.is_open()) {
                                contentFile << g_autoBattleState.copilotContent.dump(4);
                                contentFile.close();

                                // 填入本地文件输入框
                                strcpy_s(g_autoBattleState.filePath, tempContentPath.c_str());
                            }
                        }

                        g_autoBattleState.showDetails = true;
                    }
                    else {
                        ImGui::OpenPopup("无效数据");
                    }
                }
                catch (...) {
                    ImGui::OpenPopup("解析错误");
                }
            }
            else {
                ImGui::OpenPopup("文件错误");
            }
        }
        else {
            ImGui::OpenPopup("下载失败");
        }
    }

    // 分隔线
    ImGui::Separator();

    // 本地文件选择区域
    ImGui::Text("本地文件:");
    ImGui::SameLine();
    ImGui::InputText("##FilePath", g_autoBattleState.filePath, IM_ARRAYSIZE(g_autoBattleState.filePath));
    ImGui::SameLine();

    if (ImGui::Button("选择")) {
        // 清空神秘代码输入框
        memset(g_autoBattleState.codeInput, 0, sizeof(g_autoBattleState.codeInput));

        // 打开文件选择器
        OPENFILENAMEA ofn;
        char szFile[260] = { 0 };

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        if (GetOpenFileNameA(&ofn) == TRUE) {
            strcpy_s(g_autoBattleState.filePath, ofn.lpstrFile);

            // 检查文件是否存在
            if (fs::exists(g_autoBattleState.filePath)) {
                // 尝试解析文件
                try {
                    std::ifstream file(g_autoBattleState.filePath);
                    g_autoBattleState.copilotContent = json::parse(file);
                    file.close();
                    g_autoBattleState.showDetails = true;
                    // 清空copilotInfo，因为这是本地文件
                    g_autoBattleState.copilotInfo.clear();
                }
                catch (...) {
                    ImGui::OpenPopup("文件解析错误");
                }
            }
            else {
                ImGui::OpenPopup("文件不存在");
            }
        }
    }

    // 显示详细信息
    if (g_autoBattleState.showDetails) {
        ImGui::Separator();

        // 显示作业信息（如果有）
        if (!g_autoBattleState.copilotInfo.empty()) {
            if (g_autoBattleState.copilotInfo.contains("id")) {
                ImGui::Text("作业ID: %d", g_autoBattleState.copilotInfo["id"].get<int>());
            }

            if (g_autoBattleState.copilotInfo.contains("upload_time")) {
                ImGui::Text("上传时间: %s",
                    g_autoBattleState.copilotInfo["upload_time"].get<std::string>().c_str());
            }

            if (g_autoBattleState.copilotInfo.contains("not_enough_rating") &&
                !g_autoBattleState.copilotInfo["not_enough_rating"].get<bool>() &&
                g_autoBattleState.copilotInfo.contains("rating_level")) {
                ImGui::Text("评价: %d/10", g_autoBattleState.copilotInfo["rating_level"].get<int>());
            }

            ImGui::Separator();
        }

        // 显示content中的信息
        if (g_autoBattleState.copilotContent.contains("doc")) {
            if (g_autoBattleState.copilotContent["doc"].contains("title")) {
                ImGui::Text("标题: %s",
                    g_autoBattleState.copilotContent["doc"]["title"].get<std::string>().c_str());
            }

            if (g_autoBattleState.copilotContent["doc"].contains("details")) {
                ImGui::Text("描述:");
                ImGui::TextWrapped("%s",
                    g_autoBattleState.copilotContent["doc"]["details"].get<std::string>().c_str());
            }

            ImGui::Separator();
        }

        // 显示groups
        if (g_autoBattleState.copilotContent.contains("groups") &&
            !g_autoBattleState.copilotContent["groups"].is_null()) {

            const json& groups = g_autoBattleState.copilotContent["groups"];
            if (!groups.empty()) {
                ImGui::Text("干员分组:");

                for (size_t i = 0; i < groups.size(); i++) {
                    const json& group = groups[i];
                    if (group.contains("name") && group.contains("opers")) {
                        std::string groupName = group["name"].get<std::string>();

                        if (ImGui::TreeNode(("分组 " + std::to_string(i + 1) + ": " + groupName).c_str())) {
                            ImGui::Text("至少拥有分组中的其中一个干员");

                            const json& opers = group["opers"];
                            for (size_t j = 0; j < opers.size(); j++) {
                                std::string opText = getOperatorListItemText(opers[j]);
                                ImGui::Text("- %s", opText.c_str()); // 直接显示文本，无展开
                            }

                            ImGui::TreePop();
                        }
                    }
                }

                ImGui::Separator();
            }
        }

        // 显示独立的opers
        if (g_autoBattleState.copilotContent.contains("opers") &&
            !g_autoBattleState.copilotContent["opers"].is_null()) {

            // 独立干员列表（原树节点部分替换）
            const json& opers = g_autoBattleState.copilotContent["opers"];
            if (!opers.empty()) {
                ImGui::Text("干员列表:");
                for (size_t i = 0; i < opers.size(); i++) {
                    std::string opText = getOperatorListItemText(opers[i]);
                    ImGui::Text("- %s", opText.c_str()); // 直接显示文本，无展开
                }
                ImGui::Separator();
            }
        }

        // 选项和执行按钮
        ImGui::Checkbox("自动编队", &g_autoBattleState.autoFormation);
        ImGui::Checkbox("忽略严格要求", &g_autoBattleState.ignoreRequirements);
        ImGui::Checkbox("补充低信赖干员", &g_autoBattleState.addLowTrust);

        if (ImGui::Button("执行")) {
            // 生成JSON
            json result;
            result["enable"] = true;
            result["formation"] = g_autoBattleState.autoFormation;
            result["ignore_requirements"] = g_autoBattleState.ignoreRequirements;
            result["add_trust"] = g_autoBattleState.addLowTrust;
            result["filename"] = g_autoBattleState.filePath;
            // 这里可以将result传递给其他部分
            // 例如：SendToOtherModule(result);
            // 正确处理asstPtr的生命周期
            if (!asstPtr) {
                asstPtr = AsstCreateEx(AsstCallbackHandler, c_arg);
            }
            else {
                // 先停止再销毁，确保资源释放
                AsstStop(asstPtr);
                AsstDestroy(asstPtr);
                asstPtr = AsstCreateEx(AsstCallbackHandler, c_arg);
            }
            AsstAppendTask(asstPtr, "Copilot", result.dump().c_str());
            std::thread([]() {
                maa_loop();
                }).detach();
            g_currentState = STATE_RUNNING_TASK;
            //ImGui::OpenPopup("执行成功");
        }
    }

    // 弹出提示框
    if (ImGui::BeginPopup("提示")) {
        ImGui::Text("请输入神秘代码");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("错误")) {
        ImGui::Text("神秘代码必须为纯数字");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("无效数据")) {
        ImGui::Text("获取的数据无效");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("解析错误")) {
        ImGui::Text("解析数据失败");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("文件错误")) {
        ImGui::Text("无法打开文件");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("下载失败")) {
        ImGui::Text("下载文件失败");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("文件不存在")) {
        ImGui::Text("所选文件不存在");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("文件解析错误")) {
        ImGui::Text("解析文件失败");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("执行成功")) {
        ImGui::Text("执行参数已生成");
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}


// 第二段任务（返回是否成功）
bool maa_loop() {
    // 检查asstPtr是否有效
    if (!asstPtr) {
        return false;
    }


    // 检查LDPlayer额外配置
    if (settings.startup_config.ldExtraEnable) {
        // 如果ID有效（>=0），则处理额外配置
        if (settings.startup_config.ldExtraID >= 0) {
            // 查询进程ID
            int pid = queryLdID(settings.startup_config.ldExtraID);

            // 进程ID小于4时直接返回失败
            if (pid < 4) {
                AsstDestroy(asstPtr);
                asstPtr = nullptr;
                g_currentState = STATE_STOPPING;
                std::thread([]() {maa_return();}).detach();
                return false;
            }

            // 构建并设置LD配置的JSON对象
            json ldex;
            ldex["index"] = settings.startup_config.ldExtraID;
            ldex["path"] = settings.startup_config.ldExtraPathToConsole;
            ldex["pid"] = pid;

            AsstSetConnectionExtras("LDPlayer", ldex.dump().c_str());
            AsstAsyncConnect(asstPtr, "adb", settings.startup_config.netaddr, "LDPlayer", true);
        }
        else {
            // 尝试异步连接设备
            AsstAsyncConnect(asstPtr, "adb", settings.startup_config.netaddr, nullptr, true);
        }
    }
    else {
        // 尝试异步连接设备
        AsstAsyncConnect(asstPtr, "adb", settings.startup_config.netaddr, nullptr, true);
    }


    // 检查连接状态
    if (!AsstConnected(asstPtr)) {
        // 连接失败，清理资源
        AsstDestroy(asstPtr);
        asstPtr = nullptr;
        std::thread([]() {maa_return();}).detach();
        return false;
    }

    g_serverRegion = settings.startup_config.client_type;
    g_deviceAddress = settings.startup_config.netaddr;
    // 连接成功，启动任务
    AsstStart(asstPtr);
    StartScreenshotThread(asstPtr);

    // 等待任务执行完成
    while (AsstRunning(asstPtr) && g_currentState == STATE_RUNNING_TASK) {
        std::this_thread::yield(); // 让出CPU时间片，避免忙等
    }

    // 更新状态为停止中
    g_currentState = STATE_STOPPING;
    std::thread([]() {maa_return();}).detach();
    return true;
}


// 第三段任务
void maa_return() {
    AsstStop(asstPtr);
    AsstDestroy(asstPtr);
    asstPtr = nullptr;
    CleanupScreenshotResources();

    g_currentState = STATE_MAIN_MENU;
}

// 在全局区域添加
void UpdateInitializationProgress(float progress) {
    g_taskProgress = progress;
    // 当进度完成时切换到主菜单
    if (g_taskProgress >= 1.0f) {
        g_currentState = STATE_MAIN_MENU;
    }
}

#include <setupapi.h>
#include <cfgmgr32.h>

#pragma comment(lib, "setupapi.lib")

// 判断是否为独立显卡的辅助函数
bool is_dedicated_gpu(const std::wstring& device_desc) {
    // 常见独立显卡关键词列表
    const std::vector<std::wstring> dedicated_keywords = {
        L"nvidia", L"geforce", L"quadro", L"rtx",
        L"radeon", L"amd", L"rx", L"vega",
        L"arc", L"intel discrete", L"a770", L"a750"
    };

    // 常见集成显卡关键词列表
    const std::vector<std::wstring> integrated_keywords = {
        L"intel", L"hd graphics", L"uhd graphics", L"iris xe",
        L"radeon graphics", L"vega graphics", L"microsoft basic display"
    };

    // 转换为小写方便比较
    std::wstring lower_desc = device_desc;
    for (auto& c : lower_desc) {
        c = towlower(c);
    }

    // 检查是否为独立显卡
    for (const auto& keyword : dedicated_keywords) {
        if (lower_desc.find(keyword) != std::wstring::npos) {
            return true;
        }
    }

    // 检查是否为集成显卡（排除误判）
    for (const auto& keyword : integrated_keywords) {
        if (lower_desc.find(keyword) != std::wstring::npos) {
            return false;
        }
    }

    // 默认情况下认为不是独立显卡
    return false;
}

// 获取显卡适配器信息
std::vector<std::pair<int, std::wstring>> get_gpu_adapters() {
    std::vector<std::pair<int, std::wstring>> adapters;

    // 获取显示设备类GUID
    GUID display_guid;
    if (CLSIDFromString(L"{4d36e968-e325-11ce-bfc1-08002be10318}", &display_guid) != S_OK) {
        return adapters;
    }

    // 获取设备信息集
    HDEVINFO dev_info = SetupDiGetClassDevs(&display_guid, nullptr, nullptr, DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE) {
        return adapters;
    }

    // 枚举设备
    SP_DEVINFO_DATA dev_info_data = { sizeof(SP_DEVINFO_DATA) };
    DWORD index = 0;

    while (SetupDiEnumDeviceInfo(dev_info, index, &dev_info_data)) {
        WCHAR device_desc[512] = { 0 };

        // 获取设备描述
        if (SetupDiGetDeviceRegistryProperty(
            dev_info,
            &dev_info_data,
            SPDRP_DEVICEDESC,
            nullptr,
            reinterpret_cast<PBYTE>(device_desc),
            sizeof(device_desc),
            nullptr))
        {
            adapters.emplace_back(index, device_desc);
        }

        index++;
    }

    // 清理资源
    SetupDiDestroyDeviceInfoList(dev_info);
    return adapters;
}

// 选择最佳显卡适配器
int select_best_gpu_adapter() {
    auto adapters = get_gpu_adapters();
    int dedicated_index = -1;
    int integrated_index = -1;

    std::wcout << L"Detected GPU adapters:\n";
    for (const auto& [index, desc] : adapters) {
        std::wcout << L"[" << index << L"] " << desc << L"\n";

        if (is_dedicated_gpu(desc)) {
            std::wcout << L"  --> Dedicated GPU detected\n";
            if (dedicated_index == -1) {
                dedicated_index = index;
            }
        }
        else {
            std::wcout << L"  --> Integrated GPU\n";
            if (integrated_index == -1) {
                integrated_index = index;
            }
        }
    }

    // 优先选择独立显卡
    if (dedicated_index != -1) {
        std::wcout << L"Using dedicated GPU at index: " << dedicated_index << L"\n";
        return dedicated_index;
    }

    // 没有独显则选择集成显卡
    if (integrated_index != -1) {
        std::wcout << L"Using integrated GPU at index: " << integrated_index << L"\n";
        return integrated_index;
    }

    // 没有检测到任何显卡则使用默认
    std::wcout << L"No GPUs detected, using default adapter (0)\n";
    return 0;
}

//#define WinMain(a,b) main(a,b)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    system("chcp 65001");
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"RSS", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"RSF|1.60.0|MAA 51a0732|AVX2", WS_OVERLAPPEDWINDOW, 100, 100, (int)(950), (int)(540), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    LoadBackgroundTexture(g_pd3dDevice);

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    SetupImGuiFonts();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Our state
    bool show_demo_window = false;
    bool show_another_window = true;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // 在WinMain的初始化部分添加（在加载背景后）
    // 启动初始化线程
    std::thread([]() {
        // 这里执行你的第一段初始化代码
        maa_initiating([](float progress) {
            UpdateInitializationProgress(progress);
            });
        }).detach();

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) {
            CleanupScreenshotResources();
            break;
        }
        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("关于");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("欢迎使用RSF,在beta模式下RSF的关于窗口不会关闭。");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("示例与Debug", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("RS:uMAA", &show_another_window);

            ImGui::SliderFloat("示例浮点数滑条", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("取色器", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("点我计数增加"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("计数器 = %d", counter);

            ImGui::Text("渲染帧速度 %.3f ms/每帧 (对应%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::Text("");
            ImGui::Text("工程仓库:https://github.com/THSLP13/RSF_uniMAA");
            ImGui::Text("工程上游仓库:https://github.com/MaaAssistantArknights/MaaAssistantArknights");
            ImGui::Text("工程上游代码来自MaaAssistantArknights,由MAA Team管理");
            ImGui::Text("本工程利用上游代码重新修改再编译，以AGPL-3.0协议发布并免费开源");
            ImGui::Text("截至该副本版本仓库由Eliata(@THSLP13)维护");
            ImGui::Text("编译信息:");
            ImGui::Text("__cplusplus:%d", __cplusplus);
            ImGui::Text("__DATE__:%s", __DATE__);
            ImGui::Text("__TIME__:%s", __TIME__);
            ImGui::Text("__FILE__:%s", __FILE__);
            ImGui::Text("__LINE__:%d", __LINE__);
            ImGui::Text("__STDC__:%d", 0);
            ImGui::Text("__STDC_VERSION__ :%d", 0);
            ImGui::Text("__STDCPP_THREADS__:%d", __STDCPP_THREADS__);
            ImGui::Text("__AVX__:%d", __AVX__);
            ImGui::Text("__AVX2__:%d", __AVX2__);
            ImGui::Text("_CPPUNWIND:%d", _CPPUNWIND);
            ImGui::Text("_MSC_FULL_VER:%d", _MSC_FULL_VER);
            ImGui::Text("_MSVC_LANG:%d", _MSVC_LANG);
            ImGui::Text("_MT:%d", _MT);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            RenderAppState();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            // 释放旧背景纹理
            if (g_backgroundTexture) {
                g_backgroundTexture->Release();
                g_backgroundTexture = nullptr;
            }

            // 重建渲染目标
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();

            // 重新加载背景纹理（适应新窗口大小）
            LoadBackgroundTexture(g_pd3dDevice);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
