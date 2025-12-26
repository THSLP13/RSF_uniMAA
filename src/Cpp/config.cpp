#define _CRT_SECURE_NO_WARNINGS
#include "config.h"
#include <cstring>
#include <fstream>
#include <limits> // for INT_MAX

// 设施选项
const std::vector<const char*> facility_options = { "Mfg", "Trade", "Power", "Control", "Reception", "Office", "Dorm" };

// 无人机用途选项
const std::vector<std::pair<const char*, const char*>> drones_options = { { "_NotUse", "不使用无人机" },
                                                                          { "Money", "贸易站-龙门币" },
                                                                          { "SyntheticJade", "贸易站-源石碎片" },
                                                                          { "CombatRecord", "制造站-作战记录" },
                                                                          { "PureGold", "制造站-赤金" },
                                                                          { "OriginStone", "制造站-源石碎片" },
                                                                          { "Chip", "制造站-芯片" } };

const std::vector<const char*> client_types = { "Official", "Bilibili", "txwy", "YoStarEN", "YoStarJP", "YoStarKR" };
const std::vector<std::pair<int, const char*>> series_options = { { -1, "禁用" }, { 0, "自动" }, { 1, "1次" },
                                                                  { 2, "2次" },   { 3, "3次" },  { 4, "4次" },
                                                                  { 5, "5次" },   { 6, "6次" } };
const std::vector<std::pair<int, const char*>> extra_tags_options = { { 0, "默认行为" },
                                                                      { 1, "选3个Tags(可能冲突)" },
                                                                      { 2, "更多高星组合(可能冲突)" } };
const std::vector<const char*> server_options = { "CN", "US", "JP", "KR" };

// 主题选项（ID -> 显示名称）
const std::vector<std::pair<std::string, std::string>> theme_options = { { "Phantom", "傀影与猩红血钻" },
                                                                         { "Mizuki", "水月与深蓝之树" },
                                                                         { "Sami", "探索者的银凇止境" },
                                                                         { "Sarkaz", "萨卡兹的无终奇语" },
                                                                         { "JieGarden", "界园" } };

// 模式选项（值 -> 描述）
const std::vector<std::pair<int, std::string>> mode_options = { { 0, "刷分/奖励点数（稳定打更多层数）" },
                                                                { 1, "刷源石锭（第一层投资完退出）" },
                                                                { 4, "凹开局（特定流程）" },
                                                                { 6, "刷月度小队蚊子腿" },
                                                                { 7, "刷深入调查蚊子腿" } };


// JSON序列化/反序列化
namespace nlohmann
{
// 启动配置序列化
template <>
struct adl_serializer<StartupConfig>
{
    static void to_json(json& j, const StartupConfig& cfg)
    {
        j = json { { "enable", cfg.enable },
                   { "client_type", cfg.client_type },
                   { "start_game_enabled", cfg.start_game_enabled },
                   { "account_name", cfg.account_name },
                   { "ldExtraEnable", cfg.ldExtraEnable },
                   { "ldExtraID", cfg.ldExtraID },
                   { "ldExtraPathToConsole", cfg.ldExtraPathToConsole },
                   { "netaddr", cfg.netaddr } };
    }

    static void from_json(const json& j, StartupConfig& cfg)
    {
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }
        if (j.contains("client_type")) {
            strncpy(cfg.client_type, j["client_type"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
        }
        if (j.contains("start_game_enabled")) {
            j["start_game_enabled"].get_to(cfg.start_game_enabled);
        }
        if (j.contains("account_name")) {
            strncpy(cfg.account_name, j["account_name"].get<std::string>().c_str(), ACCOUNT_BUFFER_SIZE - 1);
        }
        if (j.contains("ldExtraEnable")) {
            j["ldExtraEnable"].get_to(cfg.ldExtraEnable);
        }
        if (j.contains("ldExtraID")) {
            j["ldExtraID"].get_to(cfg.ldExtraID);
        }
        if (j.contains("ldExtraPathToConsole")) {
            strncpy(cfg.ldExtraPathToConsole, j["ldExtraPathToConsole"].get<std::string>().c_str(), 256 - 1);
        }
        if (j.contains("netaddr")) {
            strncpy(cfg.netaddr, j["netaddr"].get<std::string>().c_str(), 128 - 1);
        }
    }
};

// 关卡配置序列化
template <>
struct adl_serializer<StageConfig>
{
    static void to_json(json& j, const StageConfig& cfg)
    {
        j = json { { "enable", cfg.enable },
                   { "stage", cfg.stage },
                   { "medicine", cfg.medicine },
                   { "expiring_medicine", cfg.expiring_medicine },
                   { "stone", cfg.stone },
                   { "times", cfg.times },
                   { "series", cfg.series },
                   { "drops", cfg.drops },
                   { "report_to_penguin", cfg.report_to_penguin },
                   { "penguin_id", cfg.penguin_id },
                   { "server", cfg.server },
                   { "client_type", cfg.client_type },
                   { "DrGrandet", cfg.DrGrandet } };
    }

    static void from_json(const json& j, StageConfig& cfg)
    {
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }
        if (j.contains("stage")) {
            strncpy(cfg.stage, j["stage"].get<std::string>().c_str(), STAGE_BUFFER_SIZE - 1);
        }
        if (j.contains("medicine")) {
            j["medicine"].get_to(cfg.medicine);
        }
        if (j.contains("expiring_medicine")) {
            j["expiring_medicine"].get_to(cfg.expiring_medicine);
        }
        if (j.contains("stone")) {
            j["stone"].get_to(cfg.stone);
        }
        if (j.contains("times")) {
            j["times"].get_to(cfg.times);
        }
        if (j.contains("series")) {
            j["series"].get_to(cfg.series);
        }
        if (j.contains("drops")) {
            cfg.drops = j["drops"].get<std::map<std::string, int>>();
        }
        if (j.contains("report_to_penguin")) {
            j["report_to_penguin"].get_to(cfg.report_to_penguin);
        }
        if (j.contains("penguin_id")) {
            strncpy(cfg.penguin_id, j["penguin_id"].get<std::string>().c_str(), PENGUIN_ID_BUFFER_SIZE - 1);
        }
        if (j.contains("server")) {
            strncpy(cfg.server, j["server"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
        }
        if (j.contains("client_type")) {
            strncpy(cfg.client_type, j["client_type"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
        }
        if (j.contains("DrGrandet")) {
            j["DrGrandet"].get_to(cfg.DrGrandet);
        }
    }
};

// 公招配置序列化
template <>
struct adl_serializer<RecruitmentConfig>
{
    static void to_json(json& j, const RecruitmentConfig& cfg)
    {
        j = json { { "enable", cfg.enable },
                   { "refresh", cfg.refresh },
                   { "select", cfg.select },
                   { "confirm", cfg.confirm },
                   { "first_tags", cfg.first_tags },
                   { "extra_tags_mode", cfg.extra_tags_mode },
                   { "times", cfg.times },
                   { "set_time", cfg.set_time },
                   { "expedite", cfg.expedite },
                   { "expedite_times", cfg.expedite_times },
                   { "skip_robot", cfg.skip_robot },
                   { "recruitment_time", cfg.recruitment_time },
                   { "report_to_penguin", cfg.report_to_penguin },
                   { "penguin_id", cfg.penguin_id },
                   { "report_to_yituliu", cfg.report_to_yituliu },
                   { "yituliu_id", cfg.yituliu_id },
                   { "server", cfg.server },
                   { "client_type", cfg.client_type } };
    }

    static void from_json(const json& j, RecruitmentConfig& cfg)
    {
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }
        cfg.refresh = true; // 强制为true
        if (j.contains("select")) {
            cfg.select = j["select"].get<std::vector<int>>();
        }
        if (j.contains("confirm")) {
            cfg.confirm = j["confirm"].get<std::vector<int>>();
        }
        if (j.contains("first_tags")) {
            cfg.first_tags = j["first_tags"].get<std::vector<std::string>>();
        }
        if (j.contains("extra_tags_mode")) {
            j["extra_tags_mode"].get_to(cfg.extra_tags_mode);
        }
        if (j.contains("times")) {
            j["times"].get_to(cfg.times);
        }
        cfg.set_time = true; // 强制为true
        if (j.contains("expedite")) {
            j["expedite"].get_to(cfg.expedite);
        }
        if (j.contains("expedite_times")) {
            j["expedite_times"].get_to(cfg.expedite_times);
        }
        if (j.contains("skip_robot")) {
            j["skip_robot"].get_to(cfg.skip_robot);
        }
        cfg.recruitment_time = {}; // 强制为空
        if (j.contains("report_to_penguin")) {
            j["report_to_penguin"].get_to(cfg.report_to_penguin);
        }
        if (j.contains("penguin_id")) {
            strncpy(cfg.penguin_id, j["penguin_id"].get<std::string>().c_str(), PENGUIN_ID_BUFFER_SIZE - 1);
        }
        if (j.contains("report_to_yituliu")) {
            j["report_to_yituliu"].get_to(cfg.report_to_yituliu);
        }
        if (j.contains("yituliu_id")) {
            strncpy(cfg.yituliu_id, j["yituliu_id"].get<std::string>().c_str(), YITULIU_ID_BUFFER_SIZE - 1);
        }
        if (j.contains("server")) {
            strncpy(cfg.server, j["server"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
        }
        if (j.contains("client_type")) {
            strncpy(cfg.client_type, j["client_type"].get<std::string>().c_str(), STR_BUFFER_SIZE - 1);
        }
    }
};

// 设施配置序列化
template <>
struct adl_serializer<FacilityConfig>
{
    static void to_json(json& j, const FacilityConfig& cfg)
    {
        j = json { { "enable", cfg.enable },
                   { "mode", cfg.mode },
                   { "facility", cfg.facility },
                   { "drones", cfg.drones },
                   { "threshold", cfg.threshold },
                   { "replenish", cfg.replenish },
                   { "dorm_notstationed_enabled", cfg.dorm_notstationed_enabled },
                   { "dorm_trust_enabled", cfg.dorm_trust_enabled } };
    }

    static void from_json(const json& j, FacilityConfig& cfg)
    {
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }
        if (j.contains("mode")) {
            j["mode"].get_to(cfg.mode);
        }
        if (j.contains("facility")) {
            cfg.facility = j["facility"].get<std::vector<std::string>>();
        }
        if (j.contains("drones")) {
            cfg.drones = j["drones"].get<std::string>();
        }
        if (j.contains("threshold")) {
            j["threshold"].get_to(cfg.threshold);
        }
        if (j.contains("replenish")) {
            j["replenish"].get_to(cfg.replenish);
        }
        if (j.contains("dorm_notstationed_enabled")) {
            j["dorm_notstationed_enabled"].get_to(cfg.dorm_notstationed_enabled);
        }
        if (j.contains("dorm_trust_enabled")) {
            j["dorm_trust_enabled"].get_to(cfg.dorm_trust_enabled);
        }
    }
};
}

namespace nlohmann
{
// 商店购物配置序列化
template <>
struct adl_serializer<ShoppingConfig>
{
    static void to_json(json& j, const ShoppingConfig& cfg)
    {
        j = json { { "enable", cfg.enable },
                   { "shopping", cfg.shopping },
                   { "only_buy_discount", cfg.only_buy_discount },
                   { "reserve_max_credit", cfg.reserve_max_credit } };
    }

    static void from_json(const json& j, ShoppingConfig& cfg)
    {
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }
        if (j.contains("shopping")) {
            j["shopping"].get_to(cfg.shopping);
        }
        if (j.contains("only_buy_discount")) {
            j["only_buy_discount"].get_to(cfg.only_buy_discount);
        }
        if (j.contains("reserve_max_credit")) {
            j["reserve_max_credit"].get_to(cfg.reserve_max_credit);
        }
    }
};
}

// 在JSON序列化/反序列化区域添加
namespace nlohmann
{
// 任务奖励配置序列化
template <>
struct adl_serializer<MissionConfig>
{
    static void to_json(json& j, const MissionConfig& cfg)
    {
        j = json { { "enable", cfg.enable }, { "award", cfg.award } };
    }

    static void from_json(const json& j, MissionConfig& cfg)
    {
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }
        if (j.contains("award")) {
            j["award"].get_to(cfg.award);
        }
    }
};
}

// JSON序列化/反序列化
namespace nlohmann
{
// 奖励配置列表序列化
template <>
struct adl_serializer<CollectibleStartList>
{
    static void to_json(json& j, const CollectibleStartList& cfg)
    {
        j = json { { "hot_water", cfg.hot_water }, { "shield", cfg.shield }, { "ingot", cfg.ingot },
                   { "hope", cfg.hope },           { "random", cfg.random }, { "key", cfg.key },
                   { "dice", cfg.dice },           { "ideas", cfg.ideas } };
    }

    static void from_json(const json& j, CollectibleStartList& cfg)
    {
        if (j.contains("hot_water")) {
            j["hot_water"].get_to(cfg.hot_water);
        }
        if (j.contains("shield")) {
            j["shield"].get_to(cfg.shield);
        }
        if (j.contains("ingot")) {
            j["ingot"].get_to(cfg.ingot);
        }
        if (j.contains("hope")) {
            j["hope"].get_to(cfg.hope);
        }
        if (j.contains("random")) {
            j["random"].get_to(cfg.random);
        }
        if (j.contains("key")) {
            j["key"].get_to(cfg.key);
        }
        if (j.contains("dice")) {
            j["dice"].get_to(cfg.dice);
        }
        if (j.contains("ideas")) {
            j["ideas"].get_to(cfg.ideas);
        }
    }
};

// 肉鸽配置序列化
template <>
struct adl_serializer<RoguelikeConfig>
{
    static void to_json(json& j, const RoguelikeConfig& cfg)
    {
        j = json { { "enable", cfg.enable },
                   { "theme", cfg.theme },
                   { "mode", cfg.mode },
                   { "squad", cfg.squad },
                   { "roles", cfg.roles },
                   { "core_char", cfg.core_char },
                   { "use_support", cfg.use_support },
                   { "use_nonfriend_support", cfg.use_nonfriend_support },
                   { "starts_count", cfg.starts_count },
                   { "difficulty", cfg.difficulty },
                   { "stop_at_final_boss", cfg.stop_at_final_boss },
                   { "stop_at_max_level", cfg.stop_at_max_level },
                   { "investment_enabled", cfg.investment_enabled },
                   { "investments_count", cfg.investments_count },
                   { "stop_when_investment_full", cfg.stop_when_investment_full },
                   { "investment_with_more_score", cfg.investment_with_more_score },
                   { "start_with_elite_two", cfg.start_with_elite_two },
                   { "only_start_with_elite_two", cfg.only_start_with_elite_two },
                   { "refresh_trader_with_dice", cfg.refresh_trader_with_dice },
                   { "collectible_mode_start_list", cfg.collectible_mode_start_list },
                   { "use_foldartal", cfg.use_foldartal },
                   { "check_collapsal_paradigms", cfg.check_collapsal_paradigms },
                   { "double_check_collapsal_paradigms", cfg.double_check_collapsal_paradigms },
                   { "monthly_squad_auto_iterate", cfg.monthly_squad_auto_iterate },
                   { "monthly_squad_check_comms", cfg.monthly_squad_check_comms },
                   { "deep_exploration_auto_iterate", cfg.deep_exploration_auto_iterate },
                   { "collectible_mode_shopping", cfg.collectible_mode_shopping },
                   { "collectible_mode_squad", cfg.collectible_mode_squad },
                   { "collectible_mode_start_list", cfg.collectible_mode_collectibles } };
    }

    static void from_json(const json& j, RoguelikeConfig& cfg)
    {
        // 基础配置
        if (j.contains("enable")) {
            j["enable"].get_to(cfg.enable);
        }

        // 主题（C风格字符串处理）
        if (j.contains("theme")) {
            std::string theme = j["theme"].get<std::string>();
            strncpy(cfg.theme, theme.c_str(), sizeof(cfg.theme) - 1);
            cfg.theme[sizeof(cfg.theme) - 1] = '\0'; // 确保字符串终止
        }

        if (j.contains("mode")) {
            j["mode"].get_to(cfg.mode);
        }

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
        if (j.contains("use_support")) {
            j["use_support"].get_to(cfg.use_support);
        }
        if (j.contains("use_nonfriend_support")) {
            j["use_nonfriend_support"].get_to(cfg.use_nonfriend_support);
        }

        // 运行控制
        if (j.contains("starts_count")) {
            j["starts_count"].get_to(cfg.starts_count);
        }
        if (j.contains("difficulty")) {
            j["difficulty"].get_to(cfg.difficulty);
        }
        if (j.contains("stop_at_final_boss")) {
            j["stop_at_final_boss"].get_to(cfg.stop_at_final_boss);
        }
        if (j.contains("stop_at_max_level")) {
            j["stop_at_max_level"].get_to(cfg.stop_at_max_level);
        }

        // 投资相关
        if (j.contains("investment_enabled")) {
            j["investment_enabled"].get_to(cfg.investment_enabled);
        }
        if (j.contains("investments_count")) {
            j["investments_count"].get_to(cfg.investments_count);
        }
        if (j.contains("stop_when_investment_full")) {
            j["stop_when_investment_full"].get_to(cfg.stop_when_investment_full);
        }
        if (j.contains("investment_with_more_score")) {
            j["investment_with_more_score"].get_to(cfg.investment_with_more_score);
        }

        // 凹开局相关
        if (j.contains("start_with_elite_two")) {
            j["start_with_elite_two"].get_to(cfg.start_with_elite_two);
        }
        if (j.contains("only_start_with_elite_two")) {
            j["only_start_with_elite_two"].get_to(cfg.only_start_with_elite_two);
        }
        if (j.contains("collectible_mode_start_list")) {
            cfg.collectible_mode_start_list = j["collectible_mode_start_list"].get<CollectibleStartList>();
        }

        // 主题专属配置
        if (j.contains("refresh_trader_with_dice")) {
            j["refresh_trader_with_dice"].get_to(cfg.refresh_trader_with_dice);
        }
        if (j.contains("use_foldartal")) {
            j["use_foldartal"].get_to(cfg.use_foldartal);
        }
        if (j.contains("check_collapsal_paradigms")) {
            j["check_collapsal_paradigms"].get_to(cfg.check_collapsal_paradigms);
        }
        if (j.contains("double_check_collapsal_paradigms")) {
            j["double_check_collapsal_paradigms"].get_to(cfg.double_check_collapsal_paradigms);
        }

        // 自动切换设置
        if (j.contains("monthly_squad_auto_iterate")) {
            j["monthly_squad_auto_iterate"].get_to(cfg.monthly_squad_auto_iterate);
        }
        if (j.contains("monthly_squad_check_comms")) {
            j["monthly_squad_check_comms"].get_to(cfg.monthly_squad_check_comms);
        }
        if (j.contains("deep_exploration_auto_iterate")) {
            j["deep_exploration_auto_iterate"].get_to(cfg.deep_exploration_auto_iterate);
        }

        // 烧水相关配置
        if (j.contains("collectible_mode_shopping")) {
            j["collectible_mode_shopping"].get_to(cfg.collectible_mode_shopping);
        }
        if (j.contains("collectible_mode_squad")) {
            std::string squad = j["collectible_mode_squad"].get<std::string>();
            strncpy(cfg.collectible_mode_squad, squad.c_str(), sizeof(cfg.collectible_mode_squad) - 1);
            cfg.collectible_mode_squad[sizeof(cfg.collectible_mode_squad) - 1] = '\0';
        }
        if (j.contains("collectible_mode_start_list")) {
            cfg.collectible_mode_collectibles = j["collectible_mode_start_list"].get<CollectibleStartList>();
        }
    }
};
}


// 配置管理器实现
SettingsManager::SettingsManager(std::string path) :
    config_path(std::move(path))
{
    load_config();
    sync_server_with_client_type();
    loaded = false;
}

void SettingsManager::load_config()
{
    try {
        std::ifstream file(config_path);
        if (file.is_open()) {
            json j;
            file >> j;
            // 从JSON加载到结构体
            if (j.contains("startup")) {
                startup_config = j["startup"].get<StartupConfig>();
            }
            if (j.contains("stage")) {
                stage_config = j["stage"].get<StageConfig>();
            }
            if (j.contains("recruitment")) {
                recruitment_config = j["recruitment"].get<RecruitmentConfig>();
            }
            if (j.contains("facility")) {
                facility_config = j["facility"].get<FacilityConfig>();
            }
            if (j.contains("shopping")) {
                shopping_config = j["shopping"].get<ShoppingConfig>();
            }
            if (j.contains("mission")) {
                mission_config = j["mission"].get<MissionConfig>();
            }
            if (j.contains("roguelike")) {
                roguelike_config = j["roguelike"].get<RoguelikeConfig>();
            }
            temp_facilities = facility_config.facility; // 初始化临时变量
        }
        loaded = true;
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
        memset(&shopping_config, 0, sizeof(ShoppingConfig));
        shopping_config.enable = true;
        shopping_config.shopping = true;
        shopping_config.only_buy_discount = false;
        shopping_config.reserve_max_credit = false;

        memset(&mission_config, 0, sizeof(MissionConfig));
        mission_config.enable = true;
        mission_config.award = true;

        roguelike_config = RoguelikeConfig(); // 使用结构体默认值
        loaded = true;
    }
    try {
        save_config();
    }
    catch (...) {
    }
}

json SettingsManager::export_config()
{
    json j;
    j["startup"] = startup_config;
    j["stage"] = stage_config;
    j["recruitment"] = recruitment_config;
    j["facility"] = facility_config;
    j["shopping"] = shopping_config;
    j["mission"] = mission_config;
    j["roguelike"] = roguelike_config;
    return j;
}

void SettingsManager::save_config()
{
    lastSaved = false;
    try {
        std::ofstream file(config_path);
        if (file.is_open()) {
            json j;
            j["startup"] = startup_config;
            j["stage"] = stage_config;
            j["recruitment"] = recruitment_config;
            j["facility"] = facility_config;
            j["shopping"] = shopping_config;
            j["mission"] = mission_config;
            j["roguelike"] = roguelike_config;
            file << std::setw(4) << j << std::endl;
        }
    }
    catch (const std::exception& e) {
        // 错误处理（如日志记录）
        return;
    }
    lastSaved = true;
}

void SettingsManager::sync_server_with_client_type()
{
    // 同步客户端类型
    strncpy(stage_config.client_type, startup_config.client_type, STR_BUFFER_SIZE - 1);
    strncpy(recruitment_config.client_type, startup_config.client_type, STR_BUFFER_SIZE - 1);

    // 根据客户端类型设置服务器（原逻辑不变）
    if (strcmp(startup_config.client_type, "Official") == 0 || strcmp(startup_config.client_type, "Bilibili") == 0) {
        strncpy(stage_config.server, "CN", STR_BUFFER_SIZE - 1);
        strncpy(recruitment_config.server, "CN", STR_BUFFER_SIZE - 1);
    }
    else if (strcmp(startup_config.client_type, "txwy") == 0) {
        strncpy(stage_config.server, "TW", STR_BUFFER_SIZE - 1);
        strncpy(recruitment_config.server, "TW", STR_BUFFER_SIZE - 1);
    }
    // 其他服务器类型处理（省略，与原代码一致）
}

void SettingsManager::on_theme_changed(const std::string& old_theme, const std::string& new_theme)
{
    if (old_theme == new_theme) {
        return;
    }

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
        strncpy(
            roguelike_config.collectible_mode_squad,
            squads[0].c_str(),
            sizeof(roguelike_config.collectible_mode_squad) - 1);
        roguelike_config.collectible_mode_squad[sizeof(roguelike_config.collectible_mode_squad) - 1] = '\0';
    }
}

bool SettingsManager::can_set_account() const
{
    return strcmp(startup_config.client_type, "Official") == 0 || strcmp(startup_config.client_type, "Bilibili") == 0;
}

// 根据主题获取可用分队列表
std::vector<std::string> get_squad_options(const std::string& theme)
{
    if (theme == "Phantom") {
        return { "指挥分队",     "集群分队",     "后勤分队",     "矛头分队", "突击战术分队",
                 "堡垒战术分队", "远程战术分队", "破坏战术分队", "研究分队", "高规格分队" };
    }
    else if (theme == "Mizuki") {
        return { "心胜于物分队", "物尽其用分队", "以人为本分队", "指挥分队",     "集群分队", "后勤分队",  "矛头分队",
                 "突击战术分队", "堡垒战术分队", "远程战术分队", "破坏战术分队", "研究分队", "高规格分队" };
    }
    else if (theme == "Sami") {
        return { "永恒狩猎分队", "生活至上分队", "科学主义分队", "特训分队",     "指挥分队",
                 "集群分队",     "后勤分队",     "矛头分队",     "突击战术分队", "堡垒战术分队",
                 "远程战术分队", "破坏战术分队", "高规格分队" };
    }
    else if (theme == "Sarkaz") {
        return { "因地制宜分队", "魂灵护送分队", "博闻广记分队", "蓝图测绘分队", "指挥分队",     "集群分队",
                 "后勤分队",     "矛头分队",     "突击战术分队", "堡垒战术分队", "远程战术分队", "破坏战术分队",
                 "高规格分队",   "点刺成锭分队", "拟态学者分队", "异想天开分队", "专业人士分队" };
    }
    else if (theme == "JieGarden") {
        return { "特勤分队",   "高台突破分队", "地面突破分队", "游客分队",     "司岁台分队",   "天师府分队",
                 "指挥分队",   "后勤分队",     "突击战术分队", "堡垒战术分队", "远程战术分队", "破坏战术分队",
                 "高规格分队", "花团锦簇分队", "棋行险着分队", "岁影回音分队" };
    }
    return {};
}

// 根据主题获取可用职业组列表
std::vector<std::string> get_roles_options(const std::string& theme)
{
    if (theme == "JieGarden") {
        return { "先手必胜", "稳扎稳打", "取长补短", "灵活部署", "坚不可摧", "随心所欲" };
    }
    else {
        return { "先手必胜", "稳扎稳打", "取长补短", "随心所欲" };
    }
}

// 根据主题和分队名获取tooltip文本
const char* get_squad_tooltip(const std::string& theme, const std::string& squad_name)
{
    // 按主题和分队名返回对应的tooltip
    if (theme == "Phantom") {
        if (squad_name == "指挥分队") {
            return "效果:每场战斗获得4点临时目标生命值\n解锁条件:无";
        }
        if (squad_name == "集群分队") {
            return "效果:可携带干员+2，可同时部署人数+2\n解锁条件:通过第三层【故土残躯】";
        }
        else if (squad_name == "后勤分队") {
            return "效果:初始源石锭+20，初始希望+2\n解锁条件:累计获得200源石锭";
        }
        else if (squad_name == "矛头分队") {
            return "效果:初始目标生命值变为1，所有干员的攻击力+15%，生命值+15%\n解锁条件:"
                   "剩余10点以上目标生命时完成任一游戏结局";
        }
        else if (squad_name == "突击战术分队") {
            return "效果:招募4星以上的【先锋】【近卫】干员时直接就是已进阶的状态	"
                   "\n解锁条件:拥有合计至少5名先锋或近卫干员完成游戏结局";
        }
        else if (squad_name == "堡垒战术分队") {
            return "效果:招募4星以上的【重装】【辅助】干员时直接就是已进阶的状态	"
                   "\n解锁条件:拥有合计至少5名重装或辅助干员完成游戏结局";
        }
        else if (squad_name == "远程战术分队") {
            return "效果:招募4星以上的【医疗】【狙击】干员时直接就是已进阶的状态	"
                   "\n解锁条件:拥有合计至少5名医疗或狙击干员完成游戏结局";
        }
        else if (squad_name == "破坏战术分队") {
            return "效果:招募4星以上的【术师】【特种】干员时直接就是已进阶的状态	"
                   "\n解锁条件:拥有合计至少5名术师或特种干员完成游戏结局";
        }
        else if (squad_name == "研究分队") {
            return "效果:初始目标生命+2，战斗获得的指挥经验+30%\n解锁条件:累计获得1000指挥经验";
        }
        else if (squad_name == "高规格分队") {
            return "效果:初始招募时额外获得1张随机高级招募券\n解锁条件:初始招募时额外获得1张随机高级招募券";
        }
    } //"效果:\n解锁条件:";
    else if (theme == "Mizuki") {
        if (squad_name == "指挥分队") {
            return "效果:目标生命上限+2，每次战斗结束后额外回复1目标生命\n解锁条件:无";
        }
        if (squad_name == "心胜于物分队") {
            return "效果:[在 认知塑造 中提升等级]\n"
                   "[1]战斗有概率获得额外收藏品\n"
                   "[2]战斗有概率获得额外收藏品，[完成委托可额外选择一次奖励]\n"
                   "[3]战斗有[更高]概率获得额外收藏品，完成委托可额外选择一次奖励\n"
                   "[4]战斗有更高概率获得额外收藏品，[初始携带 生还者合约],完成委托可额外选择一次奖励\n"
                   "解锁条件:无"
                   "\n\n生还者合约:战斗开始时，使随机一名干员攻击力和防御力+20%，且每次战斗结束后额外+20%";
        }
        else if (squad_name == "物尽其用分队") {
            return "效果:[在 认知塑造 中提升等级]\n"
                   "通用:进入 得偿所愿 节点时额外出现可选项"
                   "[1]每进入新的一层获得1掷骰次数\n"
                   "[2]每进入新的一层获得1掷骰次数,[初始骰子升级为八面骰子]\n"
                   "[3]每进入新的一层获得[2]掷骰次数,初始骰子升级为八面骰子\n"
                   "[4]每进入新的一层获得2掷骰次数,[每场战斗最多投放并可使用15个骰子],初始骰子升级为八面骰子\n"
                   "解锁条件:无";
        }
        else if (squad_name == "以人为本分队") {
            return "效果:[在 认知塑造 中提升等级]\n"
                   "[1]招募所有干员的希望消耗-1\n"
                   "[2]招募所有干员的希望消耗-1,[在“安全的角落”节点可额外选择一次奖励]\n"
                   "[3]招募[和晋升]所有干员的希望消耗-1,在“安全的角落”节点可额外选择一次奖励\n"
                   "[4]招募和晋升所有干员的希望消耗-1,[指挥等级上限+1],在“安全的角落”节点可额外选择一次奖励\n"
                   "解锁条件:无";
        }
        else if (squad_name == "集群分队") {
            return "效果:可携带干员+2，可同时部署人数+2\n解锁条件:通过第三层【波涛略地】";
        }
        else if (squad_name == "后勤分队") {
            return "效果:初始源石锭+20，初始希望+2\n解锁条件:累计获得200源石锭";
        }
        else if (squad_name == "矛头分队") {
            return "效果:初始目标生命值变为1，所有干员的攻击力+15%，生命值+15%\n解锁条件:"
                   "剩余10点以上目标生命时完成任一游戏结局";
        }
        else if (squad_name == "突击战术分队") {
            return "效果:招募和进阶4星以上的【近卫】【先锋】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名先锋或近卫干员完成游戏结局";
        }
        else if (squad_name == "堡垒战术分队") {
            return "效果:招募和进阶4星以上的【重装】【辅助】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名重装或辅助干员完成游戏结局";
        }
        else if (squad_name == "远程战术分队") {
            return "效果:招募和进阶4星以上的【医疗】【狙击】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名医疗或狙击干员完成游戏结局";
        }
        else if (squad_name == "破坏战术分队") {
            return "效果:招募和进阶4星以上的【术师】【特种】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名术师或特种干员完成游戏结局";
        }
        else if (squad_name == "研究分队") {
            return "效果:初始护盾值+2，战斗获得的指挥经验+30%\n解锁条件:累计获得1000指挥经验";
        }
        else if (squad_name == "高规格分队") {
            return "效果:初始招募时额外获得1张随机精锐招募券,该券必定出现五星临时招募干员\n解锁条件:"
                   "拥有不小于20名干员时完成游戏结局";
        }
    }
    else if (theme == "Sami") {
        if (squad_name == "指挥分队") {
            return "效果:目标生命上限+2，每次战斗结束后额外回复1目标生命\n解锁条件:无";
        }
        if (squad_name == "永恒狩猎分队") {
            return "效果:目标生命上限+2，非完美作战使坍缩值额外+1，完美作战后坍缩值-2\n解锁条件:"
                   "文化比较中解锁【阈限认知】";
        }
        else if (squad_name == "生活至上分队") {
            return "效果:初始携带3个随机密文板，远见预知时总会预知密文板\n解锁条件:文化比较中解锁【交往范式】";
        }
        else if (squad_name == "科学主义分队") {
            return "效果:进入第一层时抗干扰指数设为0，每进入新的一层抗干扰指数+2\n解锁条件:"
                   "文化比较中解锁【工具理性】";
        }
        else if (squad_name == "特训分队") {
            return "效果:初始护盾值+3，进阶干员不消耗希望\n解锁条件:累计获得1000指挥经验";
        }
        else if (squad_name == "集群分队") {
            return "效果:可携带干员+2，可同时部署人数+2\n解锁条件:通过第三层【昧明冻土】";
        }
        else if (squad_name == "后勤分队") {
            return "效果:初始源石锭+20，初始希望+2\n解锁条件:累计获得200源石锭";
        }
        else if (squad_name == "矛头分队") {
            return "效果:初始目标生命值变为1，所有干员的攻击力+15%，生命值+15%\n解锁条件:"
                   "剩余10点以上目标生命时完成任一游戏结局";
        }
        else if (squad_name == "突击战术分队") {
            return "效果:招募和进阶4星以上的【近卫】【先锋】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名先锋或近卫干员完成游戏结局";
        }
        else if (squad_name == "堡垒战术分队") {
            return "效果:招募和进阶4星以上的【重装】【辅助】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名重装或辅助干员完成游戏结局";
        }
        else if (squad_name == "远程战术分队") {
            return "效果:招募和进阶4星以上的【医疗】【狙击】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名医疗或狙击干员完成游戏结局";
        }
        else if (squad_name == "破坏战术分队") {
            return "效果:招募和进阶4星以上的【术师】【特种】干员时希望降低2点和1点，随机直升概率大幅提升\n解锁条件:"
                   "拥有合计至少5名术师或特种干员完成游戏结局";
        }
        else if (squad_name == "高规格分队") {
            return "效果:初始招募时额外获得1张随机精锐招募券,该券必定出现五星临时招募干员,"
                   "且五星干员随机直升概率大幅度提升\n解锁条件:拥有不小于15名干员时完成游戏结局";
        }
    }
    else if (theme == "Sarkaz") {
        if (squad_name == "指挥分队") {
            return "效果:[在不同难度时，该效果有所不同]\n"
                   "[默认]目标生命上限+2，每次战斗结束后额外回复1目标生命\n"
                   "[3+ ]目标生命上限+5，每次战斗结束后额外回复1目标生命\n"
                   "解锁条件:无";
        }
        if (squad_name == "因地制宜分队") {
            return "效果:每进入新的一层，获得一个随机收藏品\n解锁条件:无";
        }
        else if (squad_name == "魂灵护送分队") {
            return "效果:[在 历史重构 中提升等级]\n"
                   "[1]每进入新的一层，获得2缕灵感\n"
                   "[2]每进入新的一层，获得2缕灵感，[且负荷临界点+1]\n"
                   "[3]每进入新的一层，获得2缕灵感，且负荷临界点+1,[每使用一个灵感，负荷临界点+"
                   "1，作战中如使用了灵感，干员再部署时间-12秒。]\n"
                   "解锁条件:历史重构中解锁【灵感潮增】";
        }
        else if (squad_name == "博闻广记分队") {
            return "效果:[在 历史重构 中提升等级]\n"
                   "[1]初始负荷临界点+5,初始希望+2\n"
                   "[2]初始负荷临界点[+10],初始希望+2\n"
                   "[3]初始负荷临界点+10,,初始希望+2,[初始携带数个枯木新枝]\n"
                   "解锁条件:历史重构中解锁【归纳学识】";
        }
        else if (squad_name == "蓝图测绘分队") {
            return "效果:[在 历史重构 中提升等级]\n"
                   "[1]刷新节点次数+1，每个节点的首次刷新不消耗构想\n"
                   "[2]刷新节点次数+1，每个节点的首次刷新不消耗构想,[且初始构想+1]\n"
                   "[3]刷新节点次数+1，每个节点的首次刷新不消耗构想,且初始构想+1,[初始希望+1]\n"
                   "解锁条件:历史重构中解锁【勘探史迹】";
        }
        else if (squad_name == "集群分队") {
            return "效果:可携带干员+2，可同时部署人数+2，初始招募五名罗德岛预备干员\n解锁条件:无";
        }
        else if (squad_name == "后勤分队") {
            return "效果:[在不同难度时，该效果有所不同]\n"
                   "[默认]初始源石锭+20，初始希望+2\n"
                   "[6+  ]初始源石锭+30，初始希望+2\n"
                   "解锁条件:无";
        }
        else if (squad_name == "矛头分队") {
            return "效果:[在不同难度时，该效果有所不同]\n"
                   "[默认]初始目标生命值变为1，所有干员的攻击力+15%，生命值+15%\n"
                   "[9+  ]初始目标生命值变为1，所有干员的攻击力+20%，生命值+20%\n"
                   "解锁条件:无";
        }
        else if (squad_name == "突击战术分队") {
            return "效果:招募和进阶4星以上的【近卫】【先锋】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名先锋或近卫干员完成游戏结局";
        }
        else if (squad_name == "堡垒战术分队") {
            return "效果:招募和进阶4星以上的【重装】【辅助】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名重装或辅助干员完成游戏结局";
        }
        else if (squad_name == "远程战术分队") {
            return "效果:招募和进阶4星以上的【医疗】【狙击】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名医疗或狙击干员完成游戏结局";
        }
        else if (squad_name == "破坏战术分队") {
            return "效果:招募和进阶4星以上的【术师】【特种】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名术师或特种干员完成游戏结局";
        }
        else if (squad_name == "高规格分队") {
            return "效果:5星干员招募时都为直升状态\n解锁条件:无";
        }
        else if (squad_name == "点刺成锭分队") {
            return "效果:[在 历史重构 中提升等级]\n"
                   "[1]每进入新的一层，源石锭+5，首次刷新不消耗构想并出现诡意行商，击败<年代之刺>额外获得2源石锭\n"
                   "[2]每进入新的一层，源石锭+5，首次刷新不消耗构想并出现诡意行商，击败<年代之刺>额外获得2源石锭，["
                   "初始携带 贪婪天平]\n"
                   "[3]每进入新的一层，源石锭+5，首次刷新不消耗构想并出现诡意行商，击败<年代之刺>"
                   "额外获得2源石锭，初始携带 贪婪天平，进入诡意行商后获得1缕构想\n"
                   "解锁条件:[DLC1发布后]在多局游戏中累计在诡意行商消费20源石锭\n"
                   "贪婪天平:当前所有思绪的负荷大于20时，每进入一个非战斗节点，源石锭+3";
        }
        else if (squad_name == "拟态学者分队") {
            return "效果:[在 历史重构 中提升等级]\n"
                   "[1]初始希望+1，构想与遗愿解读时收藏品概率大幅增加且总会获得更稀有的收藏品\n"
                   "[2]初始希望+1，构想与遗愿或[灵感]解读时收藏品概率大幅增加且总会获得更稀有的收藏品\n"
                   "[3]初始希望+1，构想与遗愿或灵感解读时收藏品概率大幅增加且总会获得更稀有的收藏品，["
                   "初始携带探灵伯爵]\n"
                   "解锁条件:[DLC1发布后]在多局游戏中累计获得收藏品10次\n"
                   "探灵伯爵:负荷临界点+2，战斗后有50%概率掉落1缕构想";
        }
        else if (squad_name == "异想天开分队") {
            return "效果:随机获得其他2个分队的部分效果\n解锁条件:[DLC1发布后]完成一次游戏结局";
        }
        else if (squad_name == "专业人士分队") {
            return "效果:初始招募时额外获得1张高级资深干员招募券，可以购买到高级资深干员招募券。\n解锁条件:["
                   "DLC2发布后]完成一次游戏结局";
        }
    }
    else if (theme == "JieGarden") {
        if (squad_name == "指挥分队") {
            return "效果:[在不同难度时，该效果有所不同]\n"
                   "[默认]目标生命上限+2，每次战斗结束后额外回复1目标生命\n"
                   "[3+ ]目标生命上限+4，每次战斗结束后额外回复1目标生命，初始携带 时光之末\n"
                   "解锁条件:无\n"
                   "时光之末:仅一次，在非区域最终战斗和领袖战中失败时(包括主动退出)不结束探索，目标生命+"
                   "1并继续下一步行动";
        }
        if (squad_name == "特勤分队") {
            return "效果:[在 干员电弧 特勤培训中升级]\n"
                   "[默认    ]可携带干员+2，可同时部署人数+2，初始招募五名罗德岛预备干员\n"
                   "[电弧精二]可携带干员+2，可同时部署人数+2，初始招募五名罗德岛预备干员和干员电弧\n"
                   "解锁条件:无\n";
        }
        else if (squad_name == "后勤分队") {
            return "效果:[在不同难度时，该效果有所不同]\n"
                   "[默认]初始源石锭+20，初始希望+2\n"
                   "[6+ ]初始源石锭+20，初始希望+2,初始携带通宝“茧成绢”\n"
                   "解锁条件:无\n"
                   "茧成绢:投出时，每有4点源石锭，获得1点源石锭";
        }
        else if (squad_name == "高台突破分队") {
            return "效果:招募4星及以上【术师】【狙击】【医疗】【辅助】干员时希望降低2点\n解锁条件:"
                   "拥有合计至少4名术师、狙击、医疗或辅助干员完成游戏结局";
        }
        else if (squad_name == "地面突破分队") {
            return "效果:招募4星及以上【先锋】【近卫】【重装】【特种】干员时希望降低2点\n解锁条件:"
                   "拥有合计至少4名先锋、近卫、重装或特种干员完成游戏结局";
        }
        else if (squad_name == "游客分队") {
            return "效果:[在 古今学识 中提升等级]\n"
                   "[1]钱盒容量+1，票券+3。投钱时额外投出1枚通宝\n"
                   "[2]钱盒容量+2，票券+3。投钱时额外投出2枚通宝\n"
                   "解锁条件:古今学识中解锁【《制烛篇》】";
        }
        else if (squad_name == "司岁台分队") {
            return "效果:[在 古今学识 中提升等级]\n"
                   "[1]可留存的招募券数量上限+"
                   "1，每进入新的一层，若留存招募券数量未达到上限，则获得—张随机的招募券并留存\n"
                   "[2]可留存的招募券数量上限+"
                   "2，每进入新的一层，若留存招募券数量未达到上限，则获得—张随机的招募券并留存\n"
                   "解锁条件:古今学识中解锁【《须知篇》】";
        }
        else if (squad_name == "天师府分队") {
            return "效果:[在 古今学识 中提升等级]\n"
                   "[1]第二层、四层进入岁兽残识时，剩余烛火+1，可以在安全的角落进入岁兽残识\n"
                   "[2]每次 进入岁兽残识时，剩余烛火+1，可以在安全的角落进入岁兽残识\n"
                   "解锁条件:古今学识中解锁【《劝学篇》】";
        }
        else if (squad_name == "突击战术分队") {
            return "效果:招募和进阶4星以上的【近卫】【先锋】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名先锋或近卫干员完成游戏结局";
        }
        else if (squad_name == "堡垒战术分队") {
            return "效果:招募和进阶4星以上的【重装】【辅助】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名重装或辅助干员完成游戏结局";
        }
        else if (squad_name == "远程战术分队") {
            return "效果:招募和进阶4星以上的【医疗】【狙击】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名医疗或狙击干员完成游戏结局";
        }
        else if (squad_name == "破坏战术分队") {
            return "效果:招募和进阶4星以上的【术师】【特种】干员时希望降低2点和1点。\n"
                   "招募的第一个该两职业的干员为直升状态，该两职业的招募券更容易出现在商店\n解锁条件:"
                   "拥有合计至少5名术师或特种干员完成游戏结局";
        }
        else if (squad_name == "高规格分队") {
            return "效果:[在不同难度时，该效果有所不同]\n"
                   "[默认]5星干员招募时都为直升状态\n"
                   "[9+ ]初始招募时额外获得一张随机精锐招募券，4星及5星干员招募时都为直升状态\n"
                   "解锁条件:无\n";
        }
        else if (squad_name == "花团锦簇分队") {
            return "效果:"
                   "在筹谋中消耗的目标生命值固定为1；如果投钱时至少投出1枚花钱，获得1票"
                   "券；初始携带通宝“初有文”\n"
                   "解锁条件:[DLC1发布后]在多局游戏中累计重新投钱10次\n"
                   "初有文:在界园投出时，生效期间可刷新节点一次。可在筹谋中升级";
        }
        else if (squad_name == "棋行险着分队") {
            return "效果:"
                   "至少有1枚厉钱被投出时，作战与紧急作战更容易出现宝箱；初始携带“福祸相依”；初始携带通宝“梦奇物”\n"
                   "解锁条件:[DLC1发布后]在多局游戏中累计获取非初始厉钱20次\n"
                   "福祸相依:钱盒中加入厉钱时，获得1张票券\n"
                   "梦奇物:投出时，所有我方干员的技力消耗-40%，但技能会自动开启";
        }
        else if (squad_name == "岁影回音分队") {
            return "效果:随机获得一个变化过的其他分队的效果\n"
                   "解锁条件:[DLC1发布后]完成一次游戏结局";
        }
    }

    // 默认tooltip
    return "";
}
