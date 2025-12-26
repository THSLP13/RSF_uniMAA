// render_main.cpp (核心片段)
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common.hpp" // 你前面那份：Frame + send_frame/recv_frame + wsa_init + set_nodelay
#include <nlohmann/json.hpp>
using nlohmann::json;

#include "config.h"

#define IMGUI_USE_STD_STRING
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include <d3d11.h>
#include <opencv2/opencv.hpp>

#include "urlmon.h"
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")

extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;

std::mutex g_imageMutex;
std::queue<std::vector<unsigned char>> g_imageQueue;
ID3D11ShaderResourceView* g_screenTexture = nullptr;
ID3D11Texture2D* g_screenTextureResource = nullptr;
float g_screenshotFps = 30.0f;  // 默认30fps
float g_actualFps = 30.0f;      // 实际帧率
bool g_showFpsSettings = false; // 帧率设置窗口显示标志
cv::Mat g_currentScreenImage;
int g_textureWidth = 0;
int g_textureHeight = 0;

void UpdateScreenshotDisplay(ID3D11Device* device, ID3D11DeviceContext* context)
{
    std::vector<unsigned char> imageData;
    {
        std::lock_guard<std::mutex> lock(g_imageMutex);
        if (g_imageQueue.empty()) {
            return;
        }
        imageData = std::move(g_imageQueue.front());
        g_imageQueue.pop();
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
        desc.Usage = D3D11_USAGE_DYNAMIC;             // 使用动态纹理以便更新
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
        HRESULT hr = context->Map(g_screenTextureResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

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


static std::wstring GetExePathW()
{
    wchar_t buf[MAX_PATH] {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

// Windows 下线程安全的本地时间转换
inline std::tm getLocalTm(std::time_t timeT)
{
    std::tm localTm;
    localtime_s(&localTm, &timeT); // Windows 专用
    return localTm;
}

// 线程安全版的日期时间函数（替换上面的 localTm 赋值）
std::string getDateTimeStringThreadSafe(bool withMilliseconds = false)
{
    auto now = std::chrono::system_clock::now();
    std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
    std::tm localTm = getLocalTm(nowTimeT); // 线程安全

    std::ostringstream oss;
    oss << std::setfill('0') << (localTm.tm_year + 1900) << "-" << std::setw(2) << (localTm.tm_mon + 1) << "-"
        << std::setw(2) << localTm.tm_mday << " " << std::setw(2) << localTm.tm_hour << ":" << std::setw(2)
        << localTm.tm_min << ":" << std::setw(2) << localTm.tm_sec;

    if (withMilliseconds) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        oss << "." << std::setw(3) << ms.count();
    }

    return oss.str();
}

// 封装有限容量的字符串数组类（最大32条）
class LimitedStringArray
{
private:
    // 存储字符串的容器
    std::vector<std::string> strArray;
    // 最大容量（可直接修改此常量调整上限）
    static const size_t MAX_SIZE = 32;

public:
    // 新增字符串（核心逻辑：超过32条则删除第一个）
    void addString(const std::string& newStr)
    {
        // 如果当前数量达到上限，删除第一个元素（最旧的）
        if (strArray.size() >= MAX_SIZE) {
            // 擦除第一个元素（vector的begin()指向第一个元素）
            strArray.erase(strArray.begin());
        }
        // 添加新元素到末尾
        strArray.push_back(newStr);
    }

    void addString_timed(const std::string& newStr)
    {
        std::time_t now = std::time(nullptr); // 获取当前时间戳
        std::tm localTime {};                 // 存储本地时间的结构体

        localtime_s(&localTime, &now);

        std::ostringstream oss;
        oss << (localTime.tm_mon + 1) << "-"                                  // 月份（tm_mon从0开始，+1转为1-12）
            << localTime.tm_mday << " "                                       // 日期（1-31）
            << std::setw(2) << std::setfill('0') << localTime.tm_hour << ":"  // 2位小时（补0）
            << std::setw(2) << std::setfill('0') << localTime.tm_min << ":"   // 2位分钟（补0）
            << std::setw(2) << std::setfill('0') << localTime.tm_sec << " · " // 2位秒（补0）
            << newStr;                                                        // 拼接原始字符串

        if (strArray.size() >= MAX_SIZE) {
            strArray.erase(strArray.begin()); // 满则删除最旧的第一个元素
        }
        strArray.push_back(oss.str());
    }
    // 获取所有字符串（只读）
    const std::vector<std::string>& getAllStrings() const { return strArray; }

    // 获取当前元素数量
    size_t getSize() const { return strArray.size(); }

    // 清空所有元素
    void clear() { strArray.clear(); }

    // 按索引获取字符串（只读，索引从0开始）
    const std::string& getStringAt(size_t index) const
    {
        // 简单的越界检查（可根据需求改为抛异常/返回空串）
        if (index >= strArray.size()) {
            static const std::string emptyStr = "";
            return emptyStr;
        }
        return strArray[index];
    }
};

LimitedStringArray msgArray;
bool done = false;
using namespace std::chrono_literals;

static constexpr const char* kToken = "2750bch";
static constexpr uint16_t kListenPort = 39000;

struct LatestPng
{
    uint64_t request_id = 0;
    uint64_t ts_us = 0;
    std::vector<uint8_t> bytes; // PNG原始字节
};

struct WorkerSession
{
    SOCKET control = INVALID_SOCKET;
    SOCKET data = INVALID_SOCKET;

    // inflight=1：request_id!=0 表示正在等一帧回来
    uint64_t inflight_request = 0;

    // 收到的最新PNG（仅存bytes）
    LatestPng latest;

    SettingsManager workerSetting = SettingsManager(std::string(""));

    // 可选：统计/状态
    bool ready() const { return control != INVALID_SOCKET && data != INVALID_SOCKET; }
};

static SOCKET create_listen_socket(uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        throw std::runtime_error("socket failed");
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        throw std::runtime_error("bind failed");
    }
    if (listen(s, SOMAXCONN) != 0) {
        throw std::runtime_error("listen failed");
    }

    return s;
}

static uint64_t now_us()
{
    using clock = std::chrono::steady_clock;
    static const auto base = clock::now();
    auto d = clock::now() - base;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

struct ServerState
{
    std::mutex mtx;
    std::unordered_map<int, WorkerSession> workers; // key=worker_id
    std::atomic<bool> running { true };
};

static void close_socket_safe(SOCKET& s)
{
    if (s != INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
    }
}

int received = 0;
// accept线程：接入 -> 读HELLO -> 配对到 worker session
static void accept_loop(ServerState* st, SOCKET listenSock)
{
    while (st->running.load()) {
        sockaddr_in client {};
        int clen = sizeof(client);
        SOCKET s = accept(listenSock, (sockaddr*)&client, &clen);
        if (s == INVALID_SOCKET) {
            if (!st->running.load()) {
                break;
            }
            continue;
        }
        //set_nodelay(s, true);
        DWORD to_ms = 2000; // 2秒
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to_ms, sizeof(to_ms));

        try {
            Frame hello = recv_frame(s);
            if (!hello.j.is_object()) {
                std::cerr << "[accept] hello is not object: " << hello.j.dump() << "\n";
                throw std::runtime_error("hello must be json object");
            }

            auto require_string = [&](const char* k) -> std::string {
                if (!hello.j.contains(k)) {
                    throw std::runtime_error(std::string("missing key: ") + k);
                }
                if (!hello.j[k].is_string()) {
                    std::cerr << "[accept] key '" << k << "' not string: " << hello.j[k].dump() << "\n";
                    throw std::runtime_error(std::string("key not string: ") + k);
                }
                return hello.j[k].get<std::string>();
            };

            auto require_int = [&](const char* k) -> int {
                if (!hello.j.contains(k)) {
                    throw std::runtime_error(std::string("missing key: ") + k);
                }
                if (!hello.j[k].is_number_integer()) {
                    std::cerr << "[accept] key '" << k << "' not int: " << hello.j[k].dump() << "\n";
                    throw std::runtime_error(std::string("key not int: ") + k);
                }
                return hello.j[k].get<int>();
            };

            std::string type = require_string("type");
            std::string channel = require_string("channel");
            std::string token = require_string("token");
            int worker_id = require_int("worker_id");


            if (token != kToken) {
                throw std::runtime_error("bad token");
            }
            if (channel != "control" && channel != "data") {
                throw std::runtime_error("bad channel");
            }

            {
                std::lock_guard lk(st->mtx);
                auto& ws = st->workers[worker_id];

                HMODULE hm = GetModuleHandleA(NULL);
                char exePath[255] = { 0 };
                GetModuleFileNameA(hm, exePath, 254);
                std::filesystem::path cfg_p = exePath;
                cfg_p = cfg_p.parent_path() / "config" / std::string(std::to_string(worker_id) + ".json");
                st->workers[worker_id].workerSetting.config_path = cfg_p.string();
                st->workers[worker_id].workerSetting.load_config();
                assert(st->workers[worker_id].workerSetting.loaded == true);
                // 同channel重复连接：替换旧连接
                if (channel == "control") {
                    close_socket_safe(ws.control);
                    ws.control = s;
                }
                else {
                    close_socket_safe(ws.data);
                    ws.data = s;
                }
            }

            msgArray.addString_timed("rsf.socket.accept => worker " + std::to_string(worker_id) + " present.");
            // 可选：回WELCOME（走 control/data 都行，这里简单回同socket）
            send_frame(
                s,
                { { "type", "welcome" }, { "channel", channel }, { "worker_id", worker_id }, { "ts_us", now_us() } });

            std::cout << "[accept] worker " << worker_id << " channel=" << channel << " connected\n";
        }
        catch (const std::exception& e) {
            std::cerr << "[accept] handshake error: " << e.what() << " WSA=" << WSAGetLastError() << "\n";
            MessageBoxA(0, e.what(), "", 0);
            close_socket_safe(s);
        }
    }
}

// data线程：遍历所有 ready worker 的 data socket，收file帧，把PNG bytes存到变量里
static void data_loop(ServerState* st)
{
    while (st->running.load()) {
        // 复制一份 sockets 列表，避免锁住太久
        struct Item
        {
            int id;
            SOCKET s;
        };

        std::vector<Item> dsocks;

        {
            std::lock_guard lk(st->mtx);
            dsocks.reserve(st->workers.size());
            for (auto& [id, ws] : st->workers) {
                if (ws.data != INVALID_SOCKET) {
                    dsocks.push_back({ id, ws.data });
                }
            }
        }

        if (dsocks.empty()) {
            std::this_thread::sleep_for(1ms);
            continue;
        }

        // 用 select 轮询（<=10个worker，很合适）
        fd_set rfds;
        FD_ZERO(&rfds);
        SOCKET maxfd = 0;
        for (auto& it : dsocks) {
            FD_SET(it.s, &rfds);
            if (it.s > maxfd) {
                maxfd = it.s;
            }
        }
        timeval tv {};
        tv.tv_sec = 0;
        tv.tv_usec = 10 * 1000; // 10ms

        int r = select((int)maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (r <= 0) {
            continue;
        }

        for (auto& it : dsocks) {
            if (!FD_ISSET(it.s, &rfds)) {
                continue;
            }

            try {
                Frame f = recv_frame(it.s);

                if (f.j.value("type", "") == "file" && f.j.value("subtype", "") == "json") {
                    uint64_t request_id = f.j.at("request_id").get<uint64_t>();
                    uint64_t ts = f.j.value("ts_us", now_us());
                    int worker_id = f.j.at("worker_id").get<int>();
                    json rfile = f.j["file"];
                    if (rfile.value("type", "") == "asst.load") {
                        bool isok = rfile.at("ok").get<bool>();
                        std::string err_text = rfile.at("error").get<std::string>();
                        msgArray.addString_timed("[WRK "+ std::to_string(worker_id) + "] " + err_text);
                    }
                }
                if (f.j.value("type", "") == "file" && f.j.value("subtype", "") == "frame_png") {
                    uint64_t request_id = f.j.at("request_id").get<uint64_t>();
                    uint64_t ts = f.j.value("ts_us", now_us());
                    uint64_t size_d = f.j.at("file").at("size").get<uint64_t>();

                    size_t nbytes = 0;
                    
                    {
                        std::lock_guard lk(st->mtx);
                        auto wi = st->workers.find(it.id);
                        if (wi != st->workers.end()) {
                            auto& ws = wi->second;
                            ws.latest.request_id = request_id;
                            ws.latest.ts_us = ts;
                            ws.latest.bytes = std::move(f.bin);

                            nbytes = ws.latest.bytes.size();

                            // inflight完成
                            if (ws.inflight_request == request_id) {
                                ws.inflight_request = 0;
                            }
                        }
                    }

                    // 到这里 PNG bytes 已经在变量里了：workers[id].latest.bytes
                    // 你后面任何时刻都能拿这份 bytes 去加载/解码。
                    std::cout << "[data] worker " << it.id << " got PNG request_id=" << request_id
                              << " bytes=" << nbytes << "\n";
                    if (nbytes > 8000) {
                        msgArray.addString_timed(
                            "[" + std::to_string(it.id) + "] Recv. " + std::to_string(nbytes) + " bytes");
                    }

                    received += 1;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "[data] socket error worker " << it.id << ": " << e.what() << "\n";
                // 断线清理
                std::lock_guard lk(st->mtx);
                auto wi = st->workers.find(it.id);
                if (wi != st->workers.end() && wi->second.data == it.s) {
                    close_socket_safe(wi->second.data);
                    wi->second.inflight_request = 0;
                }
            }
        }
    }
}

// 发送 capture（inflight=1）
static bool send_capture(ServerState* st, int worker_id, uint64_t request_id, const nlohmann::json& params)
{
    SOCKET ctrl = INVALID_SOCKET;

    {
        std::lock_guard lk(st->mtx);
        auto it = st->workers.find(worker_id);
        if (it == st->workers.end()) {
            return false;
        }

        auto& ws = it->second;
        if (ws.control == INVALID_SOCKET) {
            return false;
        }

        // inflight=1：还没回来就不发
        if (ws.inflight_request != 0) {
            return false;
        }

        ws.inflight_request = request_id;
        ctrl = ws.control;
    }

    try {
        send_frame(
            ctrl,
            { { "type", "capture" },
              { "worker_id", worker_id },
              { "request_id", request_id },
              { "params", params },
              { "ts_us", now_us() } });
        return true;
    }
    catch (...) {
        std::lock_guard lk(st->mtx);
        auto it = st->workers.find(worker_id);
        if (it != st->workers.end() && it->second.control == ctrl) {
            close_socket_safe(it->second.control);
            it->second.inflight_request = 0;
        }
        return false;
    }
}

// 发送 capture（inflight=1）
static bool send_ctrl(ServerState* st, int worker_id, uint64_t request_id, const nlohmann::json& params)
{
    SOCKET ctrl = INVALID_SOCKET;

    {
        std::lock_guard lk(st->mtx);
        auto it = st->workers.find(worker_id);
        if (it == st->workers.end()) {
            return false;
        }

        auto& ws = it->second;
        if (ws.control == INVALID_SOCKET) {
            return false;
        }

        // inflight=1：还没回来就等
        //while (ws.inflight_request != 0) {
        //    std::this_thread::yield();
        //}

        //ws.inflight_request = request_id;
        ctrl = ws.control;
    }

    try {
        send_frame(
            ctrl,
            { { "type", "ctrl" },
              { "worker_id", worker_id },
              { "request_id", request_id },
              { "params", params },
              { "ts_us", now_us() } });
        return true;
    }
    catch (...) {
        std::lock_guard lk(st->mtx);
        auto it = st->workers.find(worker_id);
        if (it != st->workers.end() && it->second.control == ctrl) {
            close_socket_safe(it->second.control);
            it->second.inflight_request = 0;
        }
        return false;
    }
}

static void capture_loop(ServerState* st, std::atomic<int>* target_worker, std::atomic<int>* fps_hz)
{
    uint64_t req = 1;

    while (st->running.load()) {
        int wid = target_worker->load();
        int hz = fps_hz->load();
        if (hz < 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }
        
        nlohmann::json params = { { "want", "png" }, { "quality", -1 } };

        // inflight=1：没空就跳过，不积压
        (void)send_capture(st, wid, req++, params);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / hz));
    }
}

static std::optional<LatestPng> pop_latest_png(ServerState* st, int worker_id)
{
    std::lock_guard lk(st->mtx);
    auto it = st->workers.find(worker_id);
    if (it == st->workers.end()) {
        return std::nullopt;
    }

    auto& ws = it->second;
    if (ws.latest.bytes.empty()) {
        return std::nullopt;
    }

    LatestPng out = std::move(ws.latest);
    ws.latest = LatestPng {}; // 清空，表示已消费
    return out;
}

#include "worker_tabs.h"
//#include "worker_tabs_ui.h"

static bool IsWorkerConnected(ServerState& st, int workerId, bool* outCtrl = nullptr, bool* outData = nullptr)
{
    std::lock_guard lk(st.mtx);
    auto it = st.workers.find(workerId);
    if (it == st.workers.end()) {
        if (outCtrl) {
            *outCtrl = false;
        }
        if (outData) {
            *outData = false;
        }
        return false;
    }
    bool c = it->second.control != INVALID_SOCKET;
    bool d = it->second.data != INVALID_SOCKET;
    if (outCtrl) {
        *outCtrl = c;
    }
    if (outData) {
        *outData = d;
    }
    return c && d;
}

static void PumpLatestPngIntoTab(ServerState& st, WorkerTab& tab)
{
    // 用你已经有的 pop_latest_png
    if (auto pkt = pop_latest_png(&st, tab.worker_id)) {
        tab.last_request_id = pkt->request_id;
        tab.last_ts_us = pkt->ts_us;
        tab.latest_png_bytes = std::move(pkt->bytes);
    }
}

// 先定义辅助函数：用于编辑std::vector<int>、std::vector<std::string>、std::map<std::string, int>
// 编辑int类型向量
void EditVectorInt(const char* label, std::vector<int>& vec)
{
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        vec.push_back(0);
    }

    for (int i = 0; i < vec.size(); i++) {
        ImGui::PushID(i);
        ImGui::InputInt(("##" + std::to_string(i)).c_str(), &vec[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) {
            vec.erase(vec.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

// 编辑string类型向量
void EditVectorString(const char* label, std::vector<std::string>& vec)
{
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        vec.push_back("");
    }

    for (int i = 0; i < vec.size(); i++) {
        ImGui::PushID(i);
        char buf[256] = { 0 };
        strncpy(buf, vec[i].c_str(), 255);
        ImGui::InputText(("##" + std::to_string(i)).c_str(), buf, 256);
        vec[i] = buf;
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) {
            vec.erase(vec.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

// 编辑std::map<std::string, int>
void EditMapStringInt(const char* label, std::map<std::string, int>& map)
{
    ImGui::PushID(label);
    ImGui::Text("%s", label);

    // 新增键值对
    static char newKey[256] = { 0 };
    static int newValue = 0;
    ImGui::InputText("新增Key", newKey, 256);
    ImGui::SameLine();
    ImGui::InputInt("新增Value", &newValue);
    ImGui::SameLine();
    if (ImGui::SmallButton("添加")) {
        if (strlen(newKey) > 0) {
            map[newKey] = newValue;
        }
        memset(newKey, 0, 256);
        newValue = 0;
    }

    // 显示现有键值对
    std::vector<std::string> keysToErase;
    for (auto& pair : map) {
        ImGui::PushID(pair.first.c_str());
        char keyBuf[256] = { 0 };
        strncpy(keyBuf, pair.first.c_str(), 255);
        ImGui::InputText("Key", keyBuf, 256, ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        ImGui::InputInt("Value", &pair.second);
        ImGui::SameLine();
        if (ImGui::SmallButton("删除")) {
            keysToErase.push_back(pair.first);
        }
        ImGui::PopID();
    }

    // 批量删除（避免迭代时修改容器）
    for (const auto& key : keysToErase) {
        map.erase(key);
    }
    ImGui::PopID();
}

#define window_min_width 240
#define input_max_width 190
bool settings_window_open = false;
bool startup_settings_open = false;
bool stage_settings_open = false;
bool recruitment_settings_open = false;
bool facility_settings_open = false; // 设施配置窗口
bool stage_picker_open = false;
bool drops_editor_open = false;
bool tags_editor_open = false;
bool facility_editor_open = false;   // 设施编辑窗口
bool shopping_settings_open = false; // 购物配置窗口
bool mission_settings_open = false;  // 任务配置窗口
bool roguelike_settings_open = false;

// 自定义窗口临时缓冲区
char temp_stage[STAGE_BUFFER_SIZE] = { 0 };
std::map<std::string, int> temp_drops;
char new_tag[TAG_BUFFER_SIZE] = { 0 };
char new_facility[FACILITY_BUFFER_SIZE] = { 0 }; // 新设施临时输入
std::vector<std::string> temp_facilities = {
    "Mfg", "Trade", "Power", "Control", "Reception", "Office", "Dorm"
}; // 设施编辑临时存储

// 全局状态变量
struct AutoBattleState
{
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
bool isAllDigits(const std::string& s)
{
    return std::all_of(s.begin(), s.end(), ::isdigit);
}

// 辅助函数：下载文件
bool downloadFile(const std::string& url, const std::string& savePath)
{
    // 创建cache目录（如果不存在）
    std::filesystem::create_directories(std::filesystem::path(savePath).parent_path());

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
struct DropInfo
{
    char dropType[32];
    char itemId[MAX_ITEMID_LENGTH];
};

// 关卡信息结构体
struct StageInfo
{
    int apCost;
    char code[MAX_CODE_LENGTH];
    char stageId[64];
    std::vector<DropInfo> dropInfos;
};

// 物品信息结构体
struct ItemInfo
{
    char name[MAX_NAME_LENGTH];
    char description[MAX_DESCRIPTION_LENGTH];
};

// 全局数据存储
std::vector<StageInfo> all_stages;
std::unordered_map<const char*, ItemInfo> item_index; // 使用const char*作为键
char search_query[SEARCH_QUERY_LENGTH] = "";
std::vector<StageInfo> filtered_stages;
bool show_drop_popup = false;
const StageInfo* current_stage = nullptr;

// 辅助函数：字符串转小写
void to_lower(const char* src, char* dest)
{
    while (*src) {
        //*dest++ = tolower((unsigned char)*src++);
        *dest++ = (unsigned char)*src++;
    }
    *dest = '\0';
}

// 辅助函数：检查字符串是否包含指定子串
static bool string_contains(const char* str, const char* substr)
{
    return strstr(str, substr) != nullptr;
}

// 加载关卡数据
bool load_stages()
{
    std::ifstream file("resource/stages.json");
    if (!file.is_open()) {
        return false;
    }

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
static bool load_item_index()
{
    std::ifstream file("resource/item_index.json");
    if (!file.is_open()) {
        return false;
    }

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
static const ItemInfo* find_item(const char* itemId)
{
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
void filter_stages(const char* query)
{
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
#pragma comment(lib, "wininet.lib")

const std::string PENGUIN_FILE = "cache/penguin-stats.json";
const std::string PENGUIN_URL = "https://penguin-stats.io/PenguinStats/api/v2/_private/result/matrix/CN/global/all";
const int MAX_DOWNLOAD_TIMEOUT = 30000; // 30秒超时
const int64_t ONE_DAY_MS = 86'400'000;  // 一天的毫秒数

// 下载状态
enum class DownloadStatus
{
    Idle,
    Downloading,
    Complete,
    Failed,
    Cancelled
};

// 线程安全的下载状态管理
struct DownloadState
{
    std::mutex mtx;
    DownloadStatus status = DownloadStatus::Idle;
    int progress = 0;
    std::string error_msg;
};

static DownloadState download_state;
static std::thread download_thread;


// 辅助函数：检查文件是否存在且未过期（修正版）
static bool is_file_valid()
{
    namespace fs = std::filesystem;
    try {
        if (!fs::exists(PENGUIN_FILE)) {
            return false;
        }

        // 获取文件最后修改时间
        auto last_write = fs::last_write_time(PENGUIN_FILE);

        // 将文件时间转换为system_clock时间（兼容不同时钟类型）
        auto file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            last_write - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

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
static void download_file()
{
    HINTERNET hInternet = InternetOpenA("ryuni32/131.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        std::lock_guard<std::mutex> lock(download_state.mtx);
        download_state.status = DownloadStatus::Failed;
        download_state.error_msg = "初始化网络失败";
        return;
    }

    HINTERNET hConnect = InternetOpenUrlA(
        hInternet,
        PENGUIN_URL.c_str(),
        NULL,
        0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
        0);
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
    HttpQueryInfoA(hConnect, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &content_length, &len, NULL);

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
static time_t timestamp_to_timet(int64_t ms_timestamp)
{
    return static_cast<time_t>(ms_timestamp / 1000);
}

// 辅助函数：获取当前时间戳（秒）
static time_t get_current_time()
{
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

void draw_label(const char* text)
{
    ImGui::Text("%s", text);
    ImGui::SameLine(150);
}

void draw_tags_editor(SettingsManager& workerSetting)
{
    if (!tags_editor_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 200), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("编辑首选标签", &tags_editor_open)) {
        ImGui::Text("仅在Tag等级为3时有效");
        ImGui::Spacing();

        // 显示当前标签
        if (ImGui::BeginListBox("##current_tags", ImVec2(-1, 150))) {
            for (size_t i = 0; i < workerSetting.recruitment_config.first_tags.size(); ++i) {
                const char* tag = workerSetting.recruitment_config.first_tags[i].c_str();
                char label[256];
                sprintf(label, "%s##tag_%d", tag, (int)i);
                if (ImGui::Selectable(label)) {
                    // 点击删除
                    workerSetting.recruitment_config.first_tags.erase(
                        workerSetting.recruitment_config.first_tags.begin() + i);
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
                workerSetting.recruitment_config.first_tags.push_back(new_tag);
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
void draw_facility_editor(SettingsManager& workerSetting)
{
    if (!facility_editor_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 250), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 350), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("编辑换班设施", &facility_editor_open)) {
        ImGui::Text("设置要换班的设施（有序）");
        ImGui::TextDisabled("不支持运行中修改");
        ImGui::Spacing();

        static bool initialized = false;
        static size_t selected_index = SIZE_MAX; // 用于跟踪选中的项
        if (!initialized) {
            temp_facilities = workerSetting.facility_config.facility;
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
                bool is_selected = (selected_index == i); // 使用临时变量存储状态
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
                if (!can_move_up) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("↑", ImVec2(25, 20))) {
                    std::swap(temp_facilities[i], temp_facilities[i - 1]);
                    // 交换后保持选中状态
                    if (selected_index == i) {
                        selected_index = i - 1;
                    }
                    else if (selected_index == i - 1) {
                        selected_index = i;
                    }
                }
                if (!can_move_up) {
                    ImGui::EndDisabled();
                }

                ImGui::SameLine();

                // 下移按钮
                bool can_move_down = i < temp_facilities.size() - 1;
                if (!can_move_down) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("↓", ImVec2(25, 20))) {
                    std::swap(temp_facilities[i], temp_facilities[i + 1]);
                    // 交换后保持选中状态
                    if (selected_index == i) {
                        selected_index = i + 1;
                    }
                    else if (selected_index == i + 1) {
                        selected_index = i;
                    }
                }
                if (!can_move_down) {
                    ImGui::EndDisabled();
                }

                ImGui::SameLine();

                // 删除按钮 - 单独的删除按钮，避免与选择冲突
                if (ImGui::Button("×", ImVec2(25, 20))) {
                    temp_facilities.erase(temp_facilities.begin() + i);
                    if (selected_index == i) {
                        selected_index = SIZE_MAX;
                    }
                    else if (selected_index > i) {
                        selected_index--;
                    }
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
            workerSetting.facility_config.facility = temp_facilities;
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

bool is_string_in_list(const char* str, const std::vector<std::string>& list)
{
    if (!str) {
        return false;
    }
    return std::find(list.begin(), list.end(), std::string(str)) != list.end();
}


void draw_stage_picker(SettingsManager& workerSetting)
{
    if (!stage_picker_open) {
        return;
    }

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
            strncpy(workerSetting.stage_config.stage, temp_stage, STAGE_BUFFER_SIZE - 1);
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
void draw_drops_editor(SettingsManager& workerSetting)
{
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
            temp_drops = workerSetting.stage_config.drops;
            initialized = true;
        }

        static char new_item_id[32] = { 0 };
        static int new_item_count = 1;
        static char search_name[128] = { 0 };               // 材料名称搜索框
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
                        if (!file.is_open()) {
                            throw "Access to penguin-stat.cn temp file failed.";
                        }

                        nlohmann::json data;
                        file >> data;
                        if (!data.contains("matrix")) {
                            throw "Access to penguin-stat.cn temp file failed:File incomplete.";
                        }

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
                                if (end_time < now) {
                                    continue;
                                }
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
                            if (ap_cost <= 0) {
                                continue;
                            }

                            // 关键计算：期望理智 = 单次作战理智消耗 / stdDev
                            if (entry.contains("stdDev")) {
                                float std_dev = entry["stdDev"].get<float>();

                                // 避免除以零或负数
                                if (std_dev <= 0) {
                                    continue;
                                }

                                float cost_per = ap_cost / std_dev; // 使用指定公式计算
                                temp_results.emplace_back(cost_per, entry);
                            }
                        }

                        // 按期望理智升序排序（数值越小越划算）
                        std::sort(temp_results.begin(), temp_results.end());
                        size_t take = std::min((size_t)3'000'000, temp_results.size());
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
        ImGui::Text("材料名称: %s", find_item(new_item_id) ? find_item(new_item_id)->name : "未知");
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
                        strncpy(workerSetting.stage_config.stage, stage_code.c_str(), STAGE_BUFFER_SIZE - 1);
                    }
                }
                ImGui::EndTable();
            }

            // 底部按钮
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("确认设置##drops")) {
                workerSetting.stage_config.drops = temp_drops;
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


void draw_startup_subpage(SettingsManager& workerSetting)
{
    // 保持原有实现不变
    if (!startup_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 250), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("启动参数设置", &startup_settings_open)) {
        draw_label("是否启用本任务");
        ImGui::Checkbox("##startup_enable", &workerSetting.startup_config.enable);
        ImGui::Spacing();

        draw_label("模拟器链接路径");
        ImGui::InputText("##emu_addr", workerSetting.startup_config.netaddr, 128 - 1);
        ImGui::Spacing();

        draw_label("客户端版本");
        ImGui::SetNextItemWidth(input_max_width);
        if (ImGui::BeginCombo("##startup_client", workerSetting.startup_config.client_type)) {
            for (const char* type : client_types) {
                bool selected = (strcmp(workerSetting.startup_config.client_type, type) == 0);
                if (ImGui::Selectable(type, selected)) {
                    // 仅在选择不同选项时执行操作
                    if (strcmp(workerSetting.startup_config.client_type, type) != 0) {
                        // 更新配置
                        strncpy(workerSetting.startup_config.client_type, type, STR_BUFFER_SIZE - 1);
                        workerSetting.sync_server_with_client_type();

                        workerSetting.save_config();

                        // 退出当前实例
                        exit(0);
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        draw_label("自动启动客户端");
        ImGui::Text("已启用");
        ImGui::Spacing();

        draw_label("切换账号");
        bool is_enabled = workerSetting.can_set_account();
        if (!is_enabled) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputText("##account_name", workerSetting.startup_config.account_name, ACCOUNT_BUFFER_SIZE);
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
        ImGui::Checkbox("##ldex_enable", &workerSetting.startup_config.ldExtraEnable);
        ImGui::Spacing();

        draw_label("雷电快速链接ID");
        ImGui::InputInt("##ldex_idi", &workerSetting.startup_config.ldExtraID, 1, 5);
        ImGui::Spacing();

        draw_label("雷电Console路径");
        ImGui::InputText("##ldex_cpth", workerSetting.startup_config.ldExtraPathToConsole, 256 - 1);
        ImGui::Spacing();

        if (ImGui::Button("保存##startup")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##startup");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##startup")) {
            startup_settings_open = false;
        }

        if (ImGui::BeginPopup("提示##startup")) {
            ImGui::Text("设置已保存");
            if (ImGui::Button("确定##startup")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}


void draw_stage_subpage(SettingsManager& workerSetting)
{
    // 保持原有实现不变
    if (!stage_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 400), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("关卡任务设置", &stage_settings_open)) {
        draw_label("是否启用本任务");
        ImGui::Checkbox("##stage_enable", &workerSetting.stage_config.enable);
        ImGui::Spacing();

        draw_label("关卡名");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputText("##stage_name", workerSetting.stage_config.stage, STAGE_BUFFER_SIZE);
        ImGui::SameLine();
        if (ImGui::Button("选择##stage_picker_btn")) {
            strncpy(temp_stage, workerSetting.stage_config.stage, STAGE_BUFFER_SIZE - 1);
            stage_picker_open = true;
        }
        ImGui::TextDisabled("格式：1-7、S3-2Hard等");
        ImGui::Spacing();

        draw_label("理智药数量");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputInt("##medicine_count", &workerSetting.stage_config.medicine);
        ImGui::Spacing();

        draw_label("过期理智药数量");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputInt("##exp_medicine_count", &workerSetting.stage_config.expiring_medicine);
        ImGui::Spacing();

        draw_label("石头数量");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputInt("##stone_count", &workerSetting.stage_config.stone);
        ImGui::Spacing();

        draw_label("战斗次数");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputInt("##battle_times", &workerSetting.stage_config.times);
        ImGui::Spacing();

        draw_label("连战次数");
        ImGui::SetNextItemWidth(input_max_width);
        const char* current_series = "未知";
        for (const auto& [val, str] : series_options) {
            if (val == workerSetting.stage_config.series) {
                current_series = str;
                break;
            }
        }
        if (ImGui::BeginCombo("##series_combo", current_series)) {
            for (const auto& [val, str] : series_options) {
                bool selected = (workerSetting.stage_config.series == val);
                if (ImGui::Selectable(str, selected)) {
                    workerSetting.stage_config.series = val;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
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
            ImGui::TableSetupColumn("调整", ImGuiTableColumnFlags_WidthFixed, 80);      // 新增增减按钮列
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 60);      // 新增删除列
            ImGui::TableHeadersRow();

            if (workerSetting.stage_config.drops.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("无数据");
                // 合并空数据行的所有列
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("--");
            }
            else {
                // 使用非const迭代器以便修改数量
                for (auto& [id, count] : workerSetting.stage_config.drops) {
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
                        workerSetting.stage_config.drops.erase(id);
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
        ImGui::Checkbox("##report_to_penguin", &workerSetting.stage_config.report_to_penguin);
        ImGui::Spacing();

        draw_label("企鹅ID");
        if (!workerSetting.stage_config.report_to_penguin) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputText("##penguin_id_input", workerSetting.stage_config.penguin_id, PENGUIN_ID_BUFFER_SIZE);
        if (!workerSetting.stage_config.report_to_penguin) {
            ImGui::EndDisabled();
        }
        ImGui::Spacing();

        draw_label("服务器");
        ImGui::Text("%s", workerSetting.stage_config.server);
        ImGui::Spacing();

        draw_label("客户端版本");
        ImGui::Text("%s", workerSetting.stage_config.client_type);
        ImGui::Spacing();

        draw_label("节省碎石模式");
        ImGui::Checkbox("##dr_grandet_mode", &workerSetting.stage_config.DrGrandet);
        ImGui::TextDisabled("等待理智恢复后碎石");
        ImGui::Spacing();

        if (ImGui::Button("保存##stage")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##stage");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##stage")) {
            stage_settings_open = false;
        }

        if (ImGui::BeginPopup("提示##stage")) {
            ImGui::Text("设置已保存");
            if (ImGui::Button("确定##stage")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    draw_stage_picker(workerSetting);
    draw_drops_editor(workerSetting);
}

void draw_recruitment_subpage(SettingsManager& workerSetting)
{
    // 保持原有实现不变
    if (!recruitment_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 450), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("公招配置", &recruitment_settings_open)) {
        draw_label("是否启用本任务");
        ImGui::Checkbox("##recruitment_enable", &workerSetting.recruitment_config.enable);
        ImGui::Spacing();

        draw_label("选择标签等级");
        ImGui::Text("3, 4");
        ImGui::Spacing();

        draw_label("确认标签等级");
        ImGui::Text("3, 4");
        ImGui::Spacing();

        draw_label("首选标签");
        if (workerSetting.recruitment_config.first_tags.empty()) {
            ImGui::TextDisabled("无标签");
        }
        else {
            ImGui::Text("%zu个标签", workerSetting.recruitment_config.first_tags.size());
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
            if (val == workerSetting.recruitment_config.extra_tags_mode) {
                current_mode = str;
                break;
            }
        }
        if (ImGui::BeginCombo("##extra_tags_mode", current_mode)) {
            for (const auto& [val, str] : extra_tags_options) {
                bool selected = (workerSetting.recruitment_config.extra_tags_mode == val);
                if (ImGui::Selectable(str, selected)) {
                    workerSetting.recruitment_config.extra_tags_mode = val;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        draw_label("招募次数");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputInt("##recruitment_times", &workerSetting.recruitment_config.times);
        ImGui::Spacing();

        draw_label("使用加急许可");
        ImGui::Checkbox("##expedite", &workerSetting.recruitment_config.expedite);
        ImGui::Spacing();

        draw_label("加急次数");
        if (!workerSetting.recruitment_config.expedite) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputInt("##expedite_times", &workerSetting.recruitment_config.expedite_times);
        if (!workerSetting.recruitment_config.expedite) {
            ImGui::EndDisabled();
        }
        ImGui::TextDisabled("0表示无限制");
        ImGui::Spacing();

        draw_label("跳过小车词条");
        ImGui::Checkbox("##skip_robot", &workerSetting.recruitment_config.skip_robot);
        ImGui::Spacing();

        draw_label("汇报企鹅数据");
        ImGui::Checkbox("##recruit_penguin", &workerSetting.recruitment_config.report_to_penguin);
        ImGui::Spacing();

        draw_label("企鹅ID");
        if (!workerSetting.recruitment_config.report_to_penguin) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputText("##recruit_penguin_id", workerSetting.recruitment_config.penguin_id, PENGUIN_ID_BUFFER_SIZE);
        if (!workerSetting.recruitment_config.report_to_penguin) {
            ImGui::EndDisabled();
        }
        ImGui::Spacing();

        draw_label("汇报一图流数据");
        ImGui::Checkbox("##report_to_yituliu", &workerSetting.recruitment_config.report_to_yituliu);
        ImGui::Spacing();

        draw_label("一图流ID");
        if (!workerSetting.recruitment_config.report_to_yituliu) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputText("##yituliu_id", workerSetting.recruitment_config.yituliu_id, YITULIU_ID_BUFFER_SIZE);
        if (!workerSetting.recruitment_config.report_to_yituliu) {
            ImGui::EndDisabled();
        }
        ImGui::Spacing();

        draw_label("服务器");
        ImGui::SetNextItemWidth(input_max_width);
        if (ImGui::BeginCombo("##recruit_server", workerSetting.recruitment_config.server)) {
            for (const char* server : server_options) {
                bool selected = (strcmp(workerSetting.recruitment_config.server, server) == 0);
                if (ImGui::Selectable(server, selected)) {
                    strncpy(workerSetting.recruitment_config.server, server, STR_BUFFER_SIZE - 1);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        if (ImGui::Button("保存##recruitment")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##recruitment");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##recruitment")) {
            recruitment_settings_open = false;
        }

        if (ImGui::BeginPopup("提示##recruitment")) {
            ImGui::Text("设置已保存");
            if (ImGui::Button("确定##recruitment")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    draw_tags_editor(workerSetting);
}

// 新增：设施配置子页面
void draw_facility_subpage(SettingsManager& workerSetting)
{
    if (!facility_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 400), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("设施换班配置", &facility_settings_open)) {
        // 启用本任务
        draw_label("是否启用本任务");
        ImGui::Checkbox("##facility_enable", &workerSetting.facility_config.enable);
        ImGui::Spacing();

        // 模式选择
        draw_label("模式");
        ImGui::RadioButton("MAA换班", &workerSetting.facility_config.mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("一键换班", &workerSetting.facility_config.mode, 20000);
        ImGui::Spacing();

        // 换班设施列表
        draw_label("换班设施");
        if (workerSetting.facility_config.facility.empty()) {
            ImGui::TextDisabled("无设施");
        }
        else {
            ImGui::Text("%zu个设施", workerSetting.facility_config.facility.size());
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
            if (workerSetting.facility_config.drones == val) {
                current_drone = desc;
                break;
            }
        }
        if (ImGui::BeginCombo("##drones_usage", current_drone)) {
            for (const auto& [val, desc] : drones_options) {
                bool selected = (workerSetting.facility_config.drones == val);
                if (ImGui::Selectable(desc, selected)) {
                    workerSetting.facility_config.drones = val;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        // 工作心情阈值
        draw_label("心情阈值");
        ImGui::SetNextItemWidth(input_max_width);
        ImGui::InputFloat("##mood_threshold", &workerSetting.facility_config.threshold, 0.1f, 0.0f, "%.1f");
        ImGui::TextDisabled("范围 [0, 1.0]，默认 0.3");
        ImGui::Spacing();

        // 贸易站自动补货
        draw_label("自动补货");
        ImGui::Checkbox("##auto_replenish", &workerSetting.facility_config.replenish);
        ImGui::TextDisabled("贸易站“源石碎片”自动补货");
        ImGui::Spacing();

        // 宿舍“未进驻”选项
        draw_label("未进驻选项");
        ImGui::Checkbox("##dorm_notstationed", &workerSetting.facility_config.dorm_notstationed_enabled);
        ImGui::TextDisabled("启用宿舍“未进驻”选项");
        ImGui::Spacing();

        // 宿舍信赖未满选项
        draw_label("信赖未满选项");
        ImGui::Checkbox("##dorm_trust", &workerSetting.facility_config.dorm_trust_enabled);
        ImGui::TextDisabled("宿舍填入信赖未满干员");
        ImGui::Spacing();

        // 保存按钮
        if (ImGui::Button("保存##facility")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##facility");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##facility")) {
            facility_settings_open = false;
        }

        if (ImGui::BeginPopup("提示##facility")) {
            ImGui::Text("设置已保存");
            if (ImGui::Button("确定##facility")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    draw_facility_editor(workerSetting); // 绘制设施编辑窗口
}

// 新增：购物配置子页面
void draw_shopping_subpage(SettingsManager& workerSetting)
{
    if (!shopping_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 250), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("商店购物配置", &shopping_settings_open)) {
        // 启用本任务
        draw_label("是否启用本任务");
        ImGui::Checkbox("##shopping_enable", &workerSetting.shopping_config.enable);
        ImGui::Spacing();

        // 是否购物
        draw_label("是否购物");
        ImGui::Checkbox("##do_shopping", &workerSetting.shopping_config.shopping);
        ImGui::Spacing();

        // 是否只购买折扣物品
        draw_label("只买折扣物品");
        ImGui::Checkbox("##only_discount", &workerSetting.shopping_config.only_buy_discount);
        ImGui::TextDisabled("仅作用于第二轮购买");
        ImGui::Spacing();

        // 信用点低于300时停止购买
        draw_label("保留信用点");
        ImGui::Checkbox("##reserve_credit", &workerSetting.shopping_config.reserve_max_credit);
        ImGui::TextDisabled("信用点低于300时停止购买，仅作用于第二轮");
        ImGui::Spacing();

        // 保存按钮
        if (ImGui::Button("保存##shopping")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##shopping");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##shopping")) {
            shopping_settings_open = false;
        }

        if (ImGui::BeginPopup("提示##shopping")) {
            ImGui::Text("设置已保存");
            if (ImGui::Button("确定##shopping")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

// 新增：任务配置子页面
void draw_mission_subpage(SettingsManager& workerSetting)
{
    if (!mission_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 200), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("任务奖励配置", &mission_settings_open)) {
        // 启用本任务
        draw_label("是否启用本任务");
        ImGui::Checkbox("##mission_enable", &workerSetting.mission_config.enable);
        ImGui::Spacing();

        // 领取每日/每周任务奖励
        draw_label("领取任务奖励");
        ImGui::Checkbox("##get_award", &workerSetting.mission_config.award);
        ImGui::TextDisabled("包括每日任务和每周任务奖励");
        ImGui::Spacing();

        // 保存按钮
        if (ImGui::Button("保存##mission")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##mission");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##mission")) {
            mission_settings_open = false;
        }

        if (ImGui::BeginPopup("提示##mission")) {
            ImGui::Text("设置已保存");
            if (ImGui::Button("确定##mission")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

// 绘制奖励配置编辑器（带主题条件控制）
void draw_collectible_editor(CollectibleStartList& list, const std::string& theme)
{
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
        if (list.hope) {
            list.hope = false; // 强制重置
        }
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
        if (list.key) {
            list.key = false;
        }
        if (list.dice) {
            list.dice = false;
        }
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
        if (list.ideas) {
            list.ideas = false;
        }
        ImGui::BeginDisabled();
        ImGui::Checkbox("2构想奖励（仅Sarkaz可用）##id_dis", &list.ideas);
        ImGui::EndDisabled();
    }
}

// 完整的肉鸽配置界面实现（包含所有配置项）
void draw_roguelike_config(SettingsManager& workerSetting)
{
    if (!roguelike_settings_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(800, FLT_MAX));
    if (ImGui::Begin("肉鸽模式配置", &roguelike_settings_open)) {
        // 启用滚动条以适应多内容
        ImGui::BeginChild(
            "ScrollArea",
            ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2),
            false,
            ImGuiWindowFlags_HorizontalScrollbar);

        // 基础配置区域
        ImGui::Text("基础配置");
        ImGui::Separator();

        // 主题选择
        std::string old_theme = workerSetting.roguelike_config.theme;
        ImGui::SetNextItemWidth(300);
        // 1. 先提前计算出当前选中的主题显示名称（替代原来的lambda逻辑）
        const char* current_theme_name = "未知主题";
        for (const auto& [id, name] : theme_options) {
            // 对比当前配置的theme ID和选项中的ID
            if (strcmp(workerSetting.roguelike_config.theme, id.c_str()) == 0) {
                current_theme_name = name.c_str();
                break; // 找到匹配项后立即退出循环，提升效率
            }
        }

        // 2. 渲染下拉选择框，直接使用预计算的名称，不再依赖lambda和this
        if (ImGui::BeginCombo("主题##theme", current_theme_name)) {
            for (const auto& [id, name] : theme_options) {
                bool selected = (strcmp(workerSetting.roguelike_config.theme, id.c_str()) == 0);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    // 选中新选项时更新配置，保持原有逻辑不变
                    strncpy(
                        workerSetting.roguelike_config.theme,
                        id.c_str(),
                        sizeof(workerSetting.roguelike_config.theme) - 1);
                    workerSetting.roguelike_config.theme[sizeof(workerSetting.roguelike_config.theme) - 1] = '\0';
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (strcmp(old_theme.c_str(), workerSetting.roguelike_config.theme) != 0) {
            std::string new_Theme = workerSetting.roguelike_config.theme;
            workerSetting.on_theme_changed(old_theme, new_Theme);
        }
        ImGui::Spacing();

        // 模式选择
        ImGui::SetNextItemWidth(300);
        // 1. 提前计算当前选中的模式显示名称（替代依赖this的lambda）
        const char* current_mode_desc = "未知模式";
        for (const auto& [val, desc] : mode_options) {
            if (val == workerSetting.roguelike_config.mode) {
                current_mode_desc = desc.c_str();
                break; // 找到匹配项后退出循环，优化性能
            }
        }

        // 2. 渲染模式下拉选择框，直接使用预计算的名称
        if (ImGui::BeginCombo("模式##mode", current_mode_desc)) {
            for (const auto& [val, desc] : mode_options) {
                bool selected = (workerSetting.roguelike_config.mode == val);
                if (ImGui::Selectable(desc.c_str(), selected)) {
                    // 注意：原代码可能存在笔误（少了workerSetting），这里修正为完整路径
                    workerSetting.roguelike_config.mode = val;
                }
                // 给当前选中项设置默认焦点（和主题选择框保持一致的最佳实践）
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        // 开局分队选择
        ImGui::SetNextItemWidth(300);
        auto squads = get_squad_options(workerSetting.roguelike_config.theme);
        if (ImGui::BeginCombo("开局分队##squad", workerSetting.roguelike_config.squad)) {
            for (const auto& squad : squads) {
                bool selected = (strcmp(workerSetting.roguelike_config.squad, squad.c_str()) == 0);
                if (ImGui::Selectable(squad.c_str(), selected)) {
                    strncpy(
                        workerSetting.roguelike_config.squad,
                        squad.c_str(),
                        sizeof(workerSetting.roguelike_config.squad) - 1);
                    workerSetting.roguelike_config.squad[sizeof(workerSetting.roguelike_config.squad) - 1] = '\0';
                }
                // 添加鼠标悬停提示功能
                if (ImGui::IsItemHovered()) {
                    const char* tooltip = get_squad_tooltip(workerSetting.roguelike_config.theme, squad);
                    if (tooltip != nullptr) {
                        ImGui::SetTooltip(tooltip);
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        // 开局职业组选择
        ImGui::SetNextItemWidth(300);
        auto roles = get_roles_options(workerSetting.roguelike_config.theme);
        if (ImGui::BeginCombo("开局职业组##roles", workerSetting.roguelike_config.roles)) {
            for (const auto& role : roles) {
                bool selected = (strcmp(workerSetting.roguelike_config.roles, role.c_str()) == 0);
                if (ImGui::Selectable(role.c_str(), selected)) {
                    strncpy(
                        workerSetting.roguelike_config.roles,
                        role.c_str(),
                        sizeof(workerSetting.roguelike_config.roles) - 1);
                    workerSetting.roguelike_config.roles[sizeof(workerSetting.roguelike_config.roles) - 1] = '\0';
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
        // 开局干员名
        ImGui::SetNextItemWidth(300);

        // 注意：这里我们不再需要检查 InputText 的返回值来处理 Enter 键了
        ImGui::InputText(
            "开局干员名##core",
            workerSetting.roguelike_config.core_char,
            IM_ARRAYSIZE(workerSetting.roguelike_config.core_char));

        ImGui::SameLine();
        if (ImGui::Button("清空")) {
            memset(workerSetting.roguelike_config.core_char, 0, IM_ARRAYSIZE(workerSetting.roguelike_config.core_char));
        }
        ImGui::TextDisabled("仅支持单个干员中文名，留空则自动选择");
        ImGui::Spacing();

        // 助战设置
        ImGui::Checkbox("使用助战干员##support", &workerSetting.roguelike_config.use_support);

        if (!workerSetting.roguelike_config.use_support) {
            ImGui::BeginDisabled();
        }
        ImGui::SameLine(200);
        ImGui::Checkbox("允许非好友助战##nonfriend", &workerSetting.roguelike_config.use_nonfriend_support);
        if (!workerSetting.roguelike_config.use_support) {
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 运行控制区域
        ImGui::Text("运行控制");
        ImGui::Separator();

        // 开始探索次数
        ImGui::SetNextItemWidth(300);
        ImGui::InputInt("开始探索次数##starts_count", &workerSetting.roguelike_config.starts_count);
        ImGui::TextDisabled("达到次数后自动停止任务，默认999");
        ImGui::Spacing();

        // 难度设置（除Phantom外可编辑）
        if (strcmp(workerSetting.roguelike_config.theme, "Phantom") == 0) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(300);
        ImGui::InputInt("指定难度等级##diff", &workerSetting.roguelike_config.difficulty);
        if (strcmp(workerSetting.roguelike_config.theme, "Phantom") == 0) {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Phantom主题不支持难度设置");
        }
        else {
            ImGui::TextDisabled("若未解锁难度，则使用已解锁的最高难度");
        }
        ImGui::Spacing();

        // 第五层停止（除Phantom外可编辑）
        if (strcmp(workerSetting.roguelike_config.theme, "Phantom") == 0) {
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("第五层险路恶敌前停止##stop_boss", &workerSetting.roguelike_config.stop_at_final_boss);
        if (strcmp(workerSetting.roguelike_config.theme, "Phantom") == 0) {
            ImGui::EndDisabled();
        }
        ImGui::Spacing();

        // 等级满后停止
        ImGui::Checkbox("肉鸽等级刷满后停止##stop_max_level", &workerSetting.roguelike_config.stop_at_max_level);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 投资相关设置
        ImGui::Text("投资设置");
        ImGui::Separator();

        ImGui::Checkbox("启用源石锭投资##investment_enabled", &workerSetting.roguelike_config.investment_enabled);

        if (!workerSetting.roguelike_config.investment_enabled) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(300);
        ImGui::InputInt("最大投资次数##investments_count", &workerSetting.roguelike_config.investments_count);
        ImGui::TextDisabled("达到次数后自动停止任务，默认无上限");
        ImGui::Spacing();

        ImGui::Checkbox(
            "投资到达上限后停止##stop_when_full",
            &workerSetting.roguelike_config.stop_when_investment_full);
        ImGui::Spacing();

        // 投资后购物（仅模式1）
        if (workerSetting.roguelike_config.mode != 1) {
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox(
            "投资后尝试购物##investment_shopping",
            &workerSetting.roguelike_config.investment_with_more_score);
        if (workerSetting.roguelike_config.mode != 1) {
            ImGui::EndDisabled();
        }
        if (workerSetting.roguelike_config.mode != 1) {
            ImGui::TextDisabled("仅模式1（刷源石锭）有效");
        }
        if (!workerSetting.roguelike_config.investment_enabled) {
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 凹开局设置（仅模式4显示）
        if (workerSetting.roguelike_config.mode == 4) {
            ImGui::Text("凹开局设置");
            ImGui::Separator();

            ImGui::Checkbox("同时凹干员精二直升##elite2", &workerSetting.roguelike_config.start_with_elite_two);

            if (!workerSetting.roguelike_config.start_with_elite_two) {
                ImGui::BeginDisabled();
            }
            ImGui::SameLine(200);
            ImGui::Checkbox("只凹精二直升##only_elite2", &workerSetting.roguelike_config.only_start_with_elite_two);
            if (!workerSetting.roguelike_config.start_with_elite_two) {
                ImGui::EndDisabled();
            }

            // 凹开局奖励列表
            ImGui::Spacing();
            draw_collectible_editor(
                workerSetting.roguelike_config.collectible_mode_start_list,
                workerSetting.roguelike_config.theme);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // 主题专属设置
        ImGui::Text("主题专属设置");
        ImGui::Separator();

        // Mizuki专属设置
        if (strcmp(workerSetting.roguelike_config.theme, "Mizuki") == 0) {
            ImGui::Checkbox(
                "用骰子刷新商店（刷指路鳞）##refresh",
                &workerSetting.roguelike_config.refresh_trader_with_dice);
            ImGui::Spacing();
        }

        // Sami专属设置
        if (strcmp(workerSetting.roguelike_config.theme, "Sami") == 0) {
            ImGui::Checkbox("使用密文板##foldartal", &workerSetting.roguelike_config.use_foldartal);
            ImGui::Checkbox("检测坍缩范式##paradigms", &workerSetting.roguelike_config.check_collapsal_paradigms);

            if (!workerSetting.roguelike_config.check_collapsal_paradigms) {
                ImGui::BeginDisabled();
            }
            ImGui::SameLine(200);
            ImGui::Checkbox(
                "双重检测防漏##double_check",
                &workerSetting.roguelike_config.double_check_collapsal_paradigms);
            if (!workerSetting.roguelike_config.check_collapsal_paradigms) {
                ImGui::EndDisabled();
            }
            ImGui::Spacing();
        }

        // 其他主题提示
        if (strcmp(workerSetting.roguelike_config.theme, "Mizuki") != 0 &&
            strcmp(workerSetting.roguelike_config.theme, "Sami") != 0) {
            ImGui::TextDisabled("当前主题无专属设置");
            ImGui::Spacing();
        }
        ImGui::Separator();
        ImGui::Spacing();

        // 自动切换设置
        ImGui::Text("自动切换设置");
        ImGui::Separator();

        ImGui::Checkbox("月度小队自动切换##monthly_auto", &workerSetting.roguelike_config.monthly_squad_auto_iterate);
        if (workerSetting.roguelike_config.monthly_squad_auto_iterate) {
            ImGui::SameLine(200);
            ImGui::Checkbox(
                "将通信作为切换依据##monthly_comms",
                &workerSetting.roguelike_config.monthly_squad_check_comms);
        }
        ImGui::Spacing();

        ImGui::Checkbox("深入调查自动切换##deep_auto", &workerSetting.roguelike_config.deep_exploration_auto_iterate);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 烧水相关配置
        ImGui::Text("烧水相关配置");
        ImGui::Separator();

        ImGui::Checkbox("烧水时启用购物##shopping", &workerSetting.roguelike_config.collectible_mode_shopping);
        ImGui::Spacing();

        // 烧水使用的分队
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo(
                "烧水时使用的分队##collect_squad",
                workerSetting.roguelike_config.collectible_mode_squad)) {
            for (const auto& squad : squads) {
                bool selected = (strcmp(workerSetting.roguelike_config.collectible_mode_squad, squad.c_str()) == 0);
                if (ImGui::Selectable(squad.c_str(), selected)) {
                    strncpy(
                        workerSetting.roguelike_config.collectible_mode_squad,
                        squad.c_str(),
                        sizeof(workerSetting.roguelike_config.collectible_mode_squad) - 1);
                    workerSetting.roguelike_config
                        .collectible_mode_squad[sizeof(workerSetting.roguelike_config.collectible_mode_squad) - 1] =
                        '\0';
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("默认与开局分队同步");
        ImGui::Spacing();

        // 烧水期望奖励
        draw_collectible_editor(
            workerSetting.roguelike_config.collectible_mode_collectibles,
            workerSetting.roguelike_config.theme);
        ImGui::Spacing();

        ImGui::EndChild();

        // 保存/取消按钮
        if (ImGui::Button("保存配置##save")) {
            workerSetting.save_config();
            ImGui::OpenPopup("提示##saved");
        }
        ImGui::SameLine();
        if (ImGui::Button("取消##cancel")) {
            roguelike_settings_open = false;
        }

        // 保存提示弹窗
        if (ImGui::BeginPopup("提示##saved")) {
            ImGui::Text("配置已保存");
            if (ImGui::Button("确定##ok")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

void draw_settings_window(SettingsManager& workerSetting)
{
    if (!settings_window_open) {
        return;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(window_min_width, 300), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(window_min_width, 350), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("设置", &settings_window_open)) {
        ImGui::Text("配置项列表");
        ImGui::Separator();

        // 启动参数设置入口
        ImGui::BeginGroup();
        ImGui::Checkbox("##main_startup_enable", &workerSetting.startup_config.enable);
        ImGui::SameLine();
        draw_label("启动参数配置");
        if (ImGui::Button("配置##main_startup_btn")) {
            startup_settings_open = true;
        }
        ImGui::EndGroup();
        ImGui::Spacing();

        // 关卡任务设置入口
        ImGui::BeginGroup();
        ImGui::Checkbox("##main_stage_enable", &workerSetting.stage_config.enable);
        ImGui::SameLine();
        draw_label("关卡任务配置");
        if (ImGui::Button("配置##main_stage_btn")) {
            stage_settings_open = true;
        }
        ImGui::EndGroup();
        ImGui::Spacing();

        // 公招配置入口
        ImGui::BeginGroup();
        ImGui::Checkbox("##main_recruitment_enable", &workerSetting.recruitment_config.enable);
        ImGui::SameLine();
        draw_label("公招配置");
        if (ImGui::Button("配置##main_recruitment_btn")) {
            recruitment_settings_open = true;
        }
        ImGui::EndGroup();
        ImGui::Spacing();

        // 新增：设施配置入口
        ImGui::BeginGroup();
        ImGui::Checkbox("##main_facility_enable", &workerSetting.facility_config.enable);
        ImGui::SameLine();
        draw_label("设施换班配置");
        if (ImGui::Button("配置##main_facility_btn")) {
            facility_settings_open = true;
        }
        ImGui::EndGroup();

        // 新增：购物配置入口
        ImGui::BeginGroup();
        ImGui::Checkbox("##main_shopping_enable", &workerSetting.shopping_config.enable);
        ImGui::SameLine();
        draw_label("商店购物配置");
        if (ImGui::Button("配置##main_shopping_btn")) {
            shopping_settings_open = true;
        }
        ImGui::EndGroup();
        ImGui::Spacing();

        // 新增：任务配置入口
        ImGui::BeginGroup();
        ImGui::Checkbox("##main_mission_enable", &workerSetting.mission_config.enable);
        ImGui::SameLine();
        draw_label("任务奖励配置");
        if (ImGui::Button("配置##main_mission_btn")) {
            mission_settings_open = true;
        }
        ImGui::EndGroup();
        ImGui::Spacing();

        ImGui::BeginGroup();
        ImGui::Checkbox("##roguelike_toggle", &workerSetting.roguelike_config.enable);
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
            workerSetting.save_config();
        }
    }
    ImGui::End();
}

void RenderConfigUI(SettingsManager& workerSetting) // 这里WorkerSetting是包含所有config的基底结构体
{
    draw_startup_subpage(workerSetting);
    draw_stage_subpage(workerSetting);
    draw_recruitment_subpage(workerSetting);
    draw_facility_subpage(workerSetting); // 绘制设施配置页面
    draw_shopping_subpage(workerSetting); // 绘制购物配置页面
    draw_mission_subpage(workerSetting);  // 绘制任务配置页面
    draw_roguelike_config(workerSetting);
}

constexpr auto VERSION_TITLE = L"RSF|1.80.0_01|MAA v6.0.1(08cfe7c)|Standard AVX2";
std::atomic<int> target_worker { 1 };
std::atomic<int> capture_hz { 15 };

static void DrawWorkerTabsUI(
    TabManager& tm,
    ServerState& st,
    const std::wstring& exePath,
    unsigned short port,
    const std::wstring& token)
{
    if (ImGui::BeginMenuBar()) {
        // 2. 创建「实例」主菜单
        if (ImGui::BeginMenu("实例")) {
            // 3. 子菜单：新建
            if (ImGui::MenuItem("新建")) {
                int newId = (int)tm.tabs.size() + 1;
                AddWorkerTab(tm, exePath, port, token, newId);
            }

            // 4. 子菜单：退出所有
            if (ImGui::MenuItem("退出所有")) {
                done = true;
            }

            ImGui::EndMenu();    // 结束「实例」菜单
        }
        ImGui::EndMenuBar(); // 结束主菜单栏
    }
    //ImGui::SameLine();
    //ImGui::Text("Tabs: %d", (int)tm.tabs.size());

    if (ImGui::BeginTabBar("WorkersTabBar")) {
        if (ImGui::BeginTabItem("关于")) {
            ImGui::Text(WToUtf8(std::wstring(VERSION_TITLE)).c_str());
            ImGui::EndTabItem();
        }
        for (int i = 0; i < (int)tm.tabs.size(); ++i) {
            auto& tab = tm.tabs[i];
            
            // 每帧把最新 PNG “泵”进 tab 变量（等待加载）
            PumpLatestPngIntoTab(st, tab);

            bool open = true;
            // Tab标题（UTF-8需要转换；这里简化用窄字符也行）
            std::string label = "Worker " + std::to_string(tab.worker_id) + "###tab" + std::to_string(tab.tab_id);

            if (ImGui::BeginTabItem(label.c_str(), &open)) {
                // 被选中：切换 capture 目标
                tm.selected_tab_index = i;
                if (tm.target_worker) {
                    tm.target_worker->store(tab.worker_id);
                }
                target_worker.store(i+1);
                if (!tab.auto_capture) {
                    capture_hz.store(0);
                }else {
                    capture_hz.store(30);
                }
                bool ctrl = false, data = false;
                bool ready = IsWorkerConnected(st, tab.worker_id, &ctrl, &data);

                ImGui::Text("id: %d (%s)", tab.worker_id, ready ? "已就绪" : "未就绪");
                ImGui::Text("这里显示配置的连接参数区服等");
                ImGui::Text(
                    "区服:%s 连接地址%s",
                    st.workers[i+1].workerSetting.startup_config.client_type,
                    st.workers[i+1].workerSetting.startup_config.netaddr);
                if (ImGui::Button("打开设置")) {
                    settings_window_open = true;
                }

                draw_settings_window(st.workers[i + 1].workerSetting);
                RenderConfigUI(st.workers[i + 1].workerSetting);

                ImGui::SameLine();

                if (ImGui::Button("点我测试连接")) {
                    uint64_t req = (uint64_t)now_us();
                    send_ctrl(
                        &st,
                        tab.worker_id,
                        req,
                        { { "task", "asst.load" },
                          { "server", st.workers[i + 1].workerSetting.startup_config.client_type } });
                }

                ImGui::SameLine();
                ImGui::Checkbox("同步截图", & tab.auto_capture);
                ImGui::Text("ID:%d", received);

                ImGuiIO& io = ImGui::GetIO();
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
                const ImVec2 pos((windowSize.x - imageSize.x) * 0.5f, (windowSize.y - imageSize.y) * 0.5f);
                if (g_screenTexture) {
                    //ImGui::SetCursorPos(pos);
                    ImGui::Image(
                        g_screenTexture, // 纹理ID（和原AddImage第一个参数一致）
                        imageSize,       // 显示尺寸（原AddImage通过pos计算的宽高）
                        ImVec2(0, 0),    // 纹理左上角UV（和原AddImage一致）
                        ImVec2(1, 1)     // 纹理右下角UV（和原AddImage一致）
                        // 可选参数：tint_col（染色，默认白色不染色）、border_col（边框色，默认无）
                    );
                }
                // 你后续要把 bytes 解码成纹理/贴图，就在这里或交给别的线程
                ImGui::EndTabItem();
            }

            if (!open) {
                CloseWorkerTab(tm, i);
                --i;
            }
        }
        ImGui::EndTabBar();
    }
}

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
ID3D11ShaderResourceView* g_backgroundTexture = nullptr;

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
    ImFont* mainFont =
        io.Fonts->AddFontFromFileTTF(yaheiPath.c_str(), 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull());

    // 备用方案：如果雅黑不存在，使用系统默认
    if (!mainFont) {
        mainFont = io.Fonts->AddFontDefault();
    }

    // 合并日文字体
    config.MergeMode = true; // 启用合并模式

    // 优先尝试Yu Gothic UI (Win8.1+)
    std::string yuGothicPath = fontsDir + "YuGothm.ttc";
    if (GetFileAttributesA(yuGothicPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(yuGothicPath.c_str(), 18.0f, &config, io.Fonts->GetGlyphRangesJapanese());
    }
    // 回退到MS UI Gothic (WinXP+)
    else {
        std::string uiGothicPath = fontsDir + "msgothic.ttc";
        io.Fonts->AddFontFromFileTTF(uiGothicPath.c_str(), 18.0f, &config, io.Fonts->GetGlyphRangesJapanese());
    }

    io.FontDefault = mainFont;
}

int RenderMain(HINSTANCE hInstance, int nCmdShow)
{
    msgArray.addString_timed("rsf.prep => boot up.");
    try {
        HMODULE hm = GetModuleHandleA(NULL);
        wchar_t exePath[255] = { 0 };
        GetModuleFileNameW(hm, exePath, 254);

        wsa_init();
        msgArray.addString_timed("rsf.prep => wsa up.");

        SOCKET listenSock = create_listen_socket(kListenPort);
        std::cout << "[renderer] listening 127.0.0.1:" << kListenPort << "\n";

        ServerState st;

        std::thread thAccept([&] { accept_loop(&st, listenSock); });
        std::thread thData([&] { data_loop(&st); });

        // 由 capture 线程驱动请求（主线程负责渲染，不要在主 loop 里做 60Hz 网络调度）

        std::thread thCapture([&] { capture_loop(&st, &target_worker, &capture_hz); });

        msgArray.addString_timed("rsf.prep => watchdog up.");
        // 主线程消费到的“最新 PNG bytes”（你可以替换为你自己的资源加载管线/队列）
        std::vector<uint8_t> latest_png_bytes;

        TabManager tm;
        tm.target_worker = &target_worker;

        ImGui_ImplWin32_EnableDpiAwareness();
        float main_scale =
            ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

        // Create application window
        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L,      0L,     GetModuleHandle(nullptr),
                           nullptr,    nullptr,    nullptr, nullptr, L"RSS", nullptr };
        ::RegisterClassExW(&wc);
        HWND hwnd = ::CreateWindowW(
            wc.lpszClassName,
            VERSION_TITLE,
            WS_OVERLAPPEDWINDOW,
            100,
            100,
            (int)(550),
            (int)(220),
            nullptr,
            nullptr,
            wc.hInstance,
            nullptr);

        // Initialize Direct3D
        if (!CreateDeviceD3D(hwnd)) {
            CleanupDeviceD3D();
            ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return 1;
        }

        // LoadBackgroundTexture(g_pd3dDevice);

        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hwnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        SetupImGuiFonts();

        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup scaling
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style
                                         // scaling, changing this requires resetting Style + calling this again)
        style.FontScaleDpi = main_scale; // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this
                                         // unnecessary. We leave both here for documentation purpose)
        io.ConfigDpiScaleFonts =
            true; // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor
                  // DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
        io.ConfigDpiScaleViewports =
            true; // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular
        // ones.
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer backends
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        // Our state
        bool show_demo_window = false;
        bool show_another_window = true;
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

        msgArray.addString_timed("rsf.prep => imgui up.");
        msgArray.addString_timed("rsf.prep => moved to rsf.main");
        // Main loop
        done = false;
        while (!done) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) {
                    done = true;
                }
            }
            if (done) {
                // CleanupScreenshotResources();
                break;
            }
            // Handle window being minimized or screen locked
            if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
                ::Sleep(10);
                continue;
            }
            g_SwapChainOccluded = false;

            // Handle window resize (we don't resize directly in the WM_SIZE handler)
            if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_ResizeWidth = g_ResizeHeight = 0;
                CreateRenderTarget();
            }

            // Start the Dear ImGui frame
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // 从网络线程取出最新 PNG bytes（move，不拷贝），并存到变量里等待加载
            if (auto pkt = pop_latest_png(&st, target_worker.load())) {
                latest_png_bytes = std::move(pkt->bytes);
                //msgArray.addString(
                //    getDateTimeStringThreadSafe(true) + " got png bytes=" + std::to_string(latest_png_bytes.size()));
                {
                    std::lock_guard<std::mutex> lock(g_imageMutex);
                    // 只保留最新帧
                    if (!g_imageQueue.empty()) {
                        g_imageQueue.pop();
                    }
                    g_imageQueue.push(std::move(latest_png_bytes));
                }
                UpdateScreenshotDisplay(g_pd3dDevice, g_pd3dDeviceContext);
            }

            ImGuiWindowFlags window2_flags = 0;
            // window2_flags |= ImGuiWindowFlags_NoTitleBar; // 隐藏标题栏
            window2_flags |= ImGuiWindowFlags_NoCollapse;
            window2_flags |= ImGuiWindowFlags_MenuBar;
            if (ImGui::Begin("Tabs##1000000000070", nullptr, window2_flags)) {
                // 更新活动标签页的纹理
                DrawWorkerTabsUI(tm, st, exePath, kListenPort, L"2750bch");
                ImGui::End();
            }
            // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its
            // code to learn more about Dear ImGui!).
            // if (show_demo_window) {
            ImGui::ShowDemoWindow(&show_demo_window);

            // 获取IO和主视口（主窗口对应的ImGui视口）
            ImGuiIO& io = ImGui::GetIO();
            ImGuiViewport* main_viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(main_viewport->Pos);
            ImGui::SetNextWindowSize(main_viewport->Size);
            ImGui::SetNextWindowViewport(main_viewport->ID);

            // 4. 调整窗口标志（强化约束，避免独立窗口）
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoResize;              // 禁止调整大小
            window_flags |= ImGuiWindowFlags_NoTitleBar;            // 隐藏标题栏
            //window_flags |= ImGuiWindowFlags_NoMove;                // 禁止移动
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus; // 焦点时不置顶
            window_flags |= ImGuiWindowFlags_NoDecoration;          // 等价于NoTitleBar+NoResize+NoScrollbar等（可选）
            window_flags |= ImGuiWindowFlags_NoDocking;             // 禁止被其他窗口Dock（可选，根据需求）
            window_flags |= ImGuiWindowFlags_NoNavFocus;            // 禁用导航焦点（可选，减少交互干扰）

            // 确保窗口创建并嵌入主视口
            if (ImGui::Begin("rsf", nullptr, window_flags)) {
                if (ImGui::BeginMainMenuBar()) {
                    if (ImGui::BeginMenu("日志窗口")) {
                        ImGui::EndMenu();
                    }
                    ImGui::EndMainMenuBar();
                }

                ImGui::Text("");

                // 绘制日志列表
                const size_t start_idx = (msgArray.getSize() > 32) ? (msgArray.getSize() - 32) : 0;
                for (size_t i = start_idx; i < msgArray.getSize(); ++i) {
                    ImGui::Text("%s", msgArray.getStringAt(i).c_str());
                }
                bool is_near_bottom = (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() * 0.98f);
                if (is_near_bottom || (msgArray.getSize() <= 32)) { // 日志少的时候直接到底
                    ImGui::SetScrollHereY(1.0f);                    // 1.0f 表示滚动到当前项的底部（比SetScrollY更稳定）
                }

                // 方案2：强制滚动（兜底）- 无视用户操作，始终到底（注释方案1后启用）
                // ImGui::SetScrollY(ImGui::GetScrollMaxY());

                ImGui::End();
            }

            ImGui::Render();
            const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w,
                                                      clear_color.y * clear_color.w,
                                                      clear_color.z * clear_color.w,
                                                      clear_color.w };
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            // Update and Render additional Platform Windows
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            // Present
            HRESULT hr = g_pSwapChain->Present(1, 0); // Present with vsync
            // HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
            g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        }

        // Cleanup
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        // 不会走到这里：演示代码。实际你会有退出逻辑：
        st.running.store(false);
        closesocket(listenSock);
        if (thCapture.joinable()) {
            thCapture.join();
        }
        thAccept.join();
        thData.join();
        WSACleanup();
    }

    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could use e.g. DXGI_SWAP_EFFECT_FLIP_DISCARD and handle fullscreen mode
    // differently. See #8979 for suggestions.
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
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) { // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevelArray,
            2,
            D3D11_SDK_VERSION,
            &sd,
            &g_pSwapChain,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext);
    }
    if (res != S_OK) {
        return false;
    }

    // Disable DXGI's default Alt+Enter fullscreen behavior.
    // - You are free to leave this enabled, but it will not work properly with multiple viewports.
    // - This must be done for all windows associated to the device. Our DX11 backend does this automatically for
    // secondary viewports that it creates.
    IDXGIFactory* pSwapChainFactory;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&pSwapChainFactory)))) {
        pSwapChainFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        pSwapChainFactory->Release();
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
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
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite
// your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or
// clear/overwrite your copy of the keyboard data. Generally you may always pass all inputs to dear imgui, and hide them
// from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            return 0;
        }
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) { // Disable ALT application menu
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
