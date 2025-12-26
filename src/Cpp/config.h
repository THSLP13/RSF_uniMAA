#ifndef ACFG_H
#define ACFG_H

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using nlohmann::json;

// 字符串缓冲区大小定义（全局可见）
const int STR_BUFFER_SIZE = 256;
const int STAGE_BUFFER_SIZE = 128;
const int ACCOUNT_BUFFER_SIZE = 128;
const int PENGUIN_ID_BUFFER_SIZE = 64;
const int YITULIU_ID_BUFFER_SIZE = 64;
const int TAG_BUFFER_SIZE = 128;
const int FACILITY_BUFFER_SIZE = 64;

// 配置结构体定义（与config.cpp保持一致）
struct StartupConfig
{
    bool enable = true;
    char client_type[STR_BUFFER_SIZE] = { "YoStarJP" };
    bool start_game_enabled = true;
    char account_name[ACCOUNT_BUFFER_SIZE] = { 0 };
    bool ldExtraEnable = false;
    int ldExtraID = -1;
    char ldExtraPathToConsole[256] = { 0 };
    char netaddr[128] = { "192.168.127.10:5557" };
};

struct StageConfig
{
    bool enable = true;
    char stage[STAGE_BUFFER_SIZE] = { 0 };
    int medicine = 0;
    int expiring_medicine = 0;
    int stone = 0;
    int times = 255;
    int series = 0;
    std::map<std::string, int> drops;
    bool report_to_penguin = false;
    char penguin_id[PENGUIN_ID_BUFFER_SIZE] = { 0 };
    char server[STR_BUFFER_SIZE] = "CN";
    char client_type[STR_BUFFER_SIZE] = { 0 };
    bool DrGrandet = false;
};

struct RecruitmentConfig
{
    bool enable = true;
    bool refresh = true;
    std::vector<int> select = { 3, 4 };
    std::vector<int> confirm = { 3, 4 };
    std::vector<std::string> first_tags;
    int extra_tags_mode = 0;
    int times = 3;
    bool set_time = true;
    bool expedite = false;
    int expedite_times = 0;
    bool skip_robot = true;
    std::map<std::string, int> recruitment_time;
    bool report_to_penguin = false;
    char penguin_id[PENGUIN_ID_BUFFER_SIZE] = { 0 };
    bool report_to_yituliu = false;
    char yituliu_id[YITULIU_ID_BUFFER_SIZE] = { 0 };
    char server[STR_BUFFER_SIZE] = "CN";
    char client_type[STR_BUFFER_SIZE] = { 0 };
};

struct FacilityConfig
{
    bool enable = true;
    int mode = 0;
    std::vector<std::string> facility;
    std::string drones = "_NotUse";
    float threshold = 0.3f;
    bool replenish = false;
    bool dorm_notstationed_enabled = false;
    bool dorm_trust_enabled = false;
};

struct ShoppingConfig
{
    bool enable = true;
    bool shopping = true;
    bool only_buy_discount = false;
    bool reserve_max_credit = false;
};

struct MissionConfig
{
    bool enable = true;
    bool award = true;
};

struct CollectibleStartList
{
    bool hot_water = false;
    bool shield = false;
    bool ingot = false;
    bool hope = false;
    bool random = false;
    bool key = false;
    bool dice = false;
    bool ideas = false;
};

struct RoguelikeConfig
{
    bool enable = true;
    char theme[32] = "Phantom";
    int mode = 0;
    char squad[128] = "指挥分队";
    char roles[128] = "先手必胜";
    char core_char[128] = "";
    bool use_support = false;
    bool use_nonfriend_support = false;
    int starts_count = 999;
    int difficulty = 0;
    bool stop_at_final_boss = false;
    bool stop_at_max_level = false;
    bool investment_enabled = true;
    int investments_count = INT_MAX;
    bool stop_when_investment_full = false;
    bool investment_with_more_score = false;
    bool start_with_elite_two = false;
    bool only_start_with_elite_two = false;
    CollectibleStartList collectible_mode_start_list;
    bool refresh_trader_with_dice = false;
    bool use_foldartal = true;
    bool check_collapsal_paradigms = false;
    bool double_check_collapsal_paradigms = false;
    bool monthly_squad_auto_iterate = false;
    bool monthly_squad_check_comms = false;
    bool deep_exploration_auto_iterate = false;
    bool collectible_mode_shopping = false;
    char collectible_mode_squad[128] = "";
    CollectibleStartList collectible_mode_collectibles;
};

// JSON序列化声明（供nlohmann/json使用）
namespace nlohmann
{
template <>
struct adl_serializer<StartupConfig>;
template <>
struct adl_serializer<StageConfig>;
template <>
struct adl_serializer<RecruitmentConfig>;
template <>
struct adl_serializer<FacilityConfig>;
template <>
struct adl_serializer<ShoppingConfig>;
template <>
struct adl_serializer<MissionConfig>;
template <>
struct adl_serializer<CollectibleStartList>;
template <>
struct adl_serializer<RoguelikeConfig>;
}

extern const std::vector<const char*> facility_options;
extern const std::vector<std::pair<const char*, const char*>> drones_options;
extern const std::vector<const char*> client_types;
extern const std::vector<std::pair<int, const char*>> series_options;
extern const std::vector<std::pair<int, const char*>> extra_tags_options;
extern const std::vector<const char*> server_options;
extern const std::vector<std::pair<std::string, std::string>> theme_options;
extern const std::vector<std::pair<int, std::string>> mode_options;

    // 根据主题获取可用分队列表
std::vector<std::string> get_squad_options(const std::string& theme);

// 根据主题获取可用职业组列表
std::vector<std::string> get_roles_options(const std::string& theme);

// 根据主题和分队名获取tooltip文本
const char* get_squad_tooltip(const std::string& theme, const std::string& squad_name);


// 配置管理器类
class SettingsManager
{
public:
    // 公开配置结构体，允许直接操作
    StartupConfig startup_config;
    StageConfig stage_config;
    RecruitmentConfig recruitment_config;
    FacilityConfig facility_config;
    ShoppingConfig shopping_config; // 修正原变量名拼写错误
    MissionConfig mission_config;
    RoguelikeConfig roguelike_config;

    // 构造函数：接收配置文件路径
    explicit SettingsManager(std::string path);

    // 加载配置文件（从路径读取并解析）
    void load_config();

    // 保存配置文件（将当前结构体数据写入路径）
    void save_config();

    json export_config();

    // 同步服务器与客户端类型
    void sync_server_with_client_type();

    void on_theme_changed(const std::string& old_theme, const std::string& new_theme);

    // 检查是否可设置账号
    bool can_set_account() const;

    // 获取当前配置文件路径
    std::string get_config_path() const { return config_path; }

    std::string config_path;

    bool loaded = false;
    bool lastSaved = false;

private:                                      // 每个实例独立的配置文件路径
    std::vector<std::string> temp_facilities; // 原代码中的临时变量
};
#endif // SETTINGS_MANAGER_H
