// worker_main.cpp
#include "cmdline.h"
#include <vector>
#include "common.hpp"
#include <thread>
#include <atomic>
#include <cstdio>

#include "config.h"
#include "AsstStatusManager.h"
#include "AsstCaller.h"
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
using nlohmann::json;

AsstHandle ptr = nullptr;
void* customarg = nullptr;

std::string WToUtf8(const std::wstring& w)
{
    if (w.empty()) {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static SOCKET connect_to(const char* host, uint16_t port)
{
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0) {
        throw std::runtime_error("getaddrinfo");
    }
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        throw std::runtime_error("socket");
    }
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        throw std::runtime_error("connect");
    }
    freeaddrinfo(res);
    set_nodelay(s, true);
    return s;
}
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <winternl.h>
#include <memory.h>
#pragma comment(lib, "ntdll.lib")

std::string ConvertWideToANSI(const std::wstring& wstr)
{
    if (wstr.empty()) {
        return "";
    }

    // 获取所需缓冲区大小
    int bufferSize = WideCharToMultiByte(
        CP_ACP,       // 使用系统默认ANSI代码页
        0,            // 转换标志
        wstr.c_str(), // 宽字符字符串
        -1,           // 自动计算长度（包含终止符）
        nullptr,      // 输出缓冲区
        0,            // 输出缓冲区大小（先获取所需大小）
        nullptr,
        nullptr       // 默认字符（未使用）
    );

    if (bufferSize <= 0) {
        return "";
    }

    // 分配缓冲区并转换
    std::string result(bufferSize, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], bufferSize, nullptr, nullptr);

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
    pi.dwSize = sizeof(PROCESSENTRY32W); // 修正：使用宽字符结构体的大小
    // 显式调用宽字符版本的函数 Process32FirstW
    BOOL bRet = Process32FirstW(hSnapshot, &pi);
    while (bRet) {
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
        // 显式调用宽字符版本的函数 Process32NextW
        bRet = Process32NextW(hSnapshot, &pi);
    }
    CloseHandle(hSnapshot); // 注意：需要关闭快照句柄，避免资源泄漏
    return pi.th32ProcessID;
}


std::atomic<bool> AsstLoaded = { false };
std::filesystem::path AsstPath;

json build_header_loadAsstData(int worker_id,uint64_t request_id,bool isok,bool ispending,std::string err_)
{
    json j = { { "type", "file" },
               { "subtype", "json" },
               { "worker_id", worker_id },
               { "request_id", request_id },
               { "file", { { "type", "asst.load" }, { "ok", isok }, { "pending", ispending }, { "error", err_ } } } };
    return j;
}

int loadAsstData(SOCKET data, std::string srv, int worker_id, uint64_t request_id)
{
    bool isok = false;
    bool ispending = true;
    std::string err_ = "asst.load.start";
    nlohmann::json header = build_header_loadAsstData(worker_id, request_id,isok, ispending, err_);
    send_frame(data, header);

    if (AsstLoaded) {
        isok = true;
        ispending = false;
        err_ = "asst.load.alreadyPresent";
        header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
        send_frame(data, header);
        return 0;
    }

    std::filesystem::path AsstlogPath = AsstPath / "logs" / std::to_string(worker_id);
    AsstSetUserDir(AsstlogPath.string().c_str());
    err_ = "asst.load.setLog => " + AsstlogPath.string();
    header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);

    std::filesystem::path AsstbaseressPath = AsstPath;
    if (!AsstLoadResource(AsstbaseressPath.string().c_str())) {
        isok = false;
        ispending = false;
        err_ = "asst.load.baseResourceFail";
        header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
        send_frame(data, header);
        return 2;
    }

    if (srv != "Official") {
        std::filesystem::path AsstbaseressPath = AsstPath / "resource" / "global" / srv;
        err_ = "asst.load.setOverseaRessPath => " + AsstbaseressPath.string();
        header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
        if (!AsstLoadResource(AsstbaseressPath.string().c_str())) {
            isok = false;
            ispending = false;
            err_ = "asst.load.globalResourceFail";
            header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
            send_frame(data, header);
            return 2;
        }
        else {
            isok = false;
            ispending = true;
            err_ = "asst.load => globalResourceOK";
            header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
            send_frame(data, header);
        }
    }

    isok = true;
    ispending = false;
    err_ = "asst.load => OK";
    header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);

    AsstLoaded = true;

    return 0;
}

SettingsManager workerSetting = SettingsManager(std::string(""));

int launchAsst(SOCKET data, std::string srv, int worker_id, uint64_t request_id)
{
    bool isok = false;
    bool ispending = true;
    std::string err_ = "asst.core.start";
    nlohmann::json header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);

    if (ptr) {
        isok = true;
        ispending = false;
        err_ = "asst.core.start => ptr already present.";
        header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
        send_frame(data, header);
    }
    else {
        ptr = AsstCreateEx(AsstCallbackHandler, customarg);
        if (!ptr) {
            isok = false;
            ispending = false;
            err_ = "asst.core.create => start failed,see debug for detail.";
            header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
            send_frame(data, header);
            return -1;
        }
    }

    HMODULE hm = GetModuleHandleA(NULL);
    char exePath[255] = { 0 };
    GetModuleFileNameA(hm, exePath, 254);
    std::filesystem::path cfg_p = exePath;
    cfg_p = cfg_p.parent_path() / "config" / std::string(std::to_string(worker_id) + ".json");
    workerSetting.config_path = cfg_p.string();
    workerSetting.load_config();

    json j = workerSetting.export_config();
    /*
    j["startup"] = startup_config;
    j["stage"] = stage_config;
    j["recruitment"] = recruitment_config;
    j["facility"] = facility_config;
    j["shopping"] = shopping_config;
    j["mission"] = mission_config;
    j["roguelike"] = roguelike_config;*/

    // 1. 启动模块配置
    std::string startup_json = j["startup"].dump();
    AsstAppendTask(ptr, "StartUp", startup_json.c_str());

    // 2. 战斗模块配置
    std::string fight_json = j["stage"].dump();
    AsstAppendTask(ptr, "Fight", fight_json.c_str());

    // 3. 招募模块配置
    std::string recruit_json = j["recruitment"].dump();
    AsstAppendTask(ptr, "Recruit", recruit_json.c_str());

    // 4. 基建模块配置
    std::string infrast_json = j["facility"].dump();
    AsstAppendTask(ptr, "Infrast", infrast_json.c_str());

    // 5. 商店模块配置
    std::string mall_json = j["shopping"].dump();
    AsstAppendTask(ptr, "Mall", mall_json.c_str());

    // 6. 任务奖励模块配置
    std::string award_json = j["mission"].dump();
    AsstAppendTask(ptr, "Award", award_json.c_str());

    // 7. 肉鸽模块配置
    std::string roguelike_json = j["roguelike"].dump();
    AsstAppendTask(ptr, "Roguelike", roguelike_json.c_str());

    isok = false;
    ispending = true;
    err_ = "asst.core.create => Task Appended.";
    header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);

    if (workerSetting.startup_config.ldExtraEnable) {
        // 如果ID有效（>=0），则处理额外配置
        if (workerSetting.startup_config.ldExtraID >= 0) {
            // 查询进程ID
            int pid = queryLdID(workerSetting.startup_config.ldExtraID);

            // 进程ID小于4时直接返回失败
            if (pid < 4) {
                isok = false;
                ispending = false;
                err_ = "asst.core.connect => module _ld_ failed";
                header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
                send_frame(data, header);
                return -1;
            }

            // 构建并设置LD配置的JSON对象
            json ldex;
            ldex["index"] = workerSetting.startup_config.ldExtraID;
            ldex["path"] = workerSetting.startup_config.ldExtraPathToConsole;
            ldex["pid"] = pid;

            AsstSetConnectionExtras("LDPlayer", ldex.dump().c_str());
            AsstAsyncConnect(ptr, "adb", workerSetting.startup_config.netaddr, "LDPlayer", false);

            isok = false;
            ispending = false;
            err_ = "asst.core.connect => module _ld_ => using LDConsole";
            header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
            send_frame(data, header);
        }
        else {
            // 尝试异步连接设备
            AsstAsyncConnect(ptr, "adb", workerSetting.startup_config.netaddr, nullptr, false);
        }
    }
    else {
        // 尝试异步连接设备
        AsstAsyncConnect(ptr, "adb", workerSetting.startup_config.netaddr, nullptr, false);
    }

    isok = false;
    ispending = false;
    err_ = "asst.core.connect => trying to connect...";
    header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);

    int to_ = 0;
    while (!AsstConnected(ptr)) {
        if (to_ > 50) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        to_ += 1;
    }
    if (!AsstConnected(ptr)) {
        isok = false;
        ispending = false;
        err_ = "asst.core.connect => Failed to connect.";
        header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
        send_frame(data, header);
        return -1;
    }

    isok = false;
    ispending = true;
    err_ = "asst.core.connect => Connected,starting task.";
    header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);

    to_ = 0;
    while (!AsstStart(ptr)) {
        if (to_ > 3) {
            isok = false;
            ispending = false;
            err_ = "asst.core.start => Asst Starting failed.";
            header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
            send_frame(data, header);
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        to_ += 1;
    }

    isok = true;
    ispending = false;
    err_ = "asst.core => OK.";
    header = build_header_loadAsstData(worker_id, request_id, isok, ispending, err_);
    send_frame(data, header);
    return 0;
}

int WorkerMain(const Args& args)
{
    /*
    Sleep(500);
    if (!AllocConsole()) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) { // 忽略重复创建的错误
            fprintf(stderr, "创建控制台失败，错误码：%lu\n", err);
            return 1;
        }
    }

    // 2. 重定向标准输入/输出/错误到控制台（否则 cout/printf 无输出）
    FILE* fp_in = nullptr;
    FILE* fp_out = nullptr;
    FILE* fp_err = nullptr;
    freopen_s(&fp_in, "CONIN$", "r", stdin);    // 绑定标准输入
    freopen_s(&fp_out, "CONOUT$", "w", stdout); // 绑定标准输出
    freopen_s(&fp_err, "CONOUT$", "w", stderr); // 绑定标准错误
    */
    wsa_init();

    // 解析命令行：port, worker_id, token（略）
    uint16_t port = args.port;
    int worker_id = args.worker_id;
    std::string token = WToUtf8(args.token);
    std::string logPath = WToUtf8(args.log_path);
    AsstPath = logPath;

    SOCKET control = connect_to("127.0.0.1", port);
    SOCKET data = connect_to("127.0.0.1", port);

    nlohmann::json hello_ctrl = nlohmann::json::object();
    hello_ctrl["type"] = "hello";
    hello_ctrl["channel"] = "control";
    hello_ctrl["worker_id"] = worker_id;
    hello_ctrl["token"] = token;
    hello_ctrl["ver"] = 1;

    send_frame(control, hello_ctrl);
    Frame welcome1 = recv_frame(control);

    nlohmann::json hello_data = nlohmann::json::object();
    hello_data["type"] = "hello";
    hello_data["channel"] = "data";
    hello_data["worker_id"] = worker_id;
    hello_data["token"] = token;
    hello_data["ver"] = 1;

    send_frame(data, hello_data);
    Frame welcome2 = recv_frame(data);

    const size_t BUFFER_SIZE = 4 * 1024 * 1024; // 1MB
    std::vector<unsigned char> buffer(BUFFER_SIZE);
    std::vector<uchar> png_buffer(BUFFER_SIZE);
    try {
        for (;;) {
            Frame f = recv_frame(control);
            std::string type = f.j.value("type", "");
            if (type == "capture") {
                int rworker_id = f.j.at("worker_id").get<int>();
                if (rworker_id != worker_id) {
                    continue;
                }
                uint64_t request_id = f.j.at("request_id").get<uint64_t>();

                nlohmann::json header = {
                    { "type", "file" },
                    { "subtype", "frame_png" },
                    { "worker_id", worker_id },
                    { "request_id", request_id },
                    { "frame_seq", f.j.value("frame_seq", 0) }, // 可由 renderer 指定或 worker 自增
                    { "file", { { "name", "frame.png" }, { "mime", "image/png" }, { "size", 0 } } }
                };

                if (!AsstRunning(ptr) or !AsstConnected(ptr)) {
                    cv::Mat img(1280, 720, CV_8UC3, cv::Scalar(20, 40, 60));

                    std::vector<uint8_t> png;
                    cv::imencode(".png", img, png);

                    header = { { "type", "file" },
                               { "subtype", "frame_png" },
                               { "worker_id", worker_id },
                               { "request_id", request_id },
                               { "frame_seq", f.j.value("frame_seq", 0) }, // 可由 renderer 指定或 worker 自增
                               { "file",
                                 { { "name", "frame.png" }, { "mime", "image/png" }, { "size", png.size() } } } };

                    send_frame(data, header, png);
                }
                else {
                    AsstAsyncScreencap(ptr, false);

                   size_t actualSize = AsstGetImage(ptr, png_buffer.data(), png_buffer.size());

                   png_buffer.resize(actualSize);

                    header = { { "type", "file" },
                               { "subtype", "frame_png" },
                               { "worker_id", worker_id },
                               { "request_id", request_id },
                               { "frame_seq", f.j.value("frame_seq", 0) }, // 可由 renderer 指定或 worker 自增
                               { "file",
                                { { "name", "frame.png" }, { "mime", "image/png" }, { "size", actualSize } } } };

                    send_frame(data, header, png_buffer);

                    png_buffer.assign(BUFFER_SIZE,0);
                }
            }
            else if (type == "ctrl") {
                int rworker_id = f.j.at("worker_id").get<int>();
                if (rworker_id != worker_id) {
                    continue;
                }
                uint64_t request_id = f.j.at("request_id").get<uint64_t>();
                json params = f.j["params"];
                std::string task = params.value("task", "");
                if (task == "asst.load") {
                    std::string server = params.value("server", "Official");
                    if (loadAsstData(data, server, worker_id, request_id) == 0) {
                        launchAsst(data, server, worker_id, request_id);

                    }
                    // MessageBoxA(0, "", "Test",0);
                }
            }
            else if (type == "quit") {
                break;
            }
        }
    }
    catch (const std::exception& e) {
        int wsa = WSAGetLastError();
        std::cerr << "[worker] recv_frame(control) failed: " << e.what() << " WSA=" << wsa << "\n";
        // MessageBoxA(0, e.what(), "", 0);
    }
    closesocket(control);
    closesocket(data);
    WSACleanup();
    return 0;
}
