#pragma once 
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

struct WorkerProc
{
    PROCESS_INFORMATION pi {};
    bool running = false;
};

struct WorkerTab
{
    int tab_id = 0;
    int worker_id = 0;
    std::wstring title;
    WorkerProc proc;

    uint64_t last_request_id = 0;
    uint64_t last_ts_us = 0;
    std::vector<uint8_t> latest_png_bytes;

    bool auto_capture = true;
};

struct TabManager
{
    std::vector<WorkerTab> tabs;
    int next_tab_id = 1;
    int selected_tab_index = -1;

    std::atomic<int>* target_worker = nullptr;
};

// 非 UI 逻辑
WorkerTab& AddWorkerTab(
    TabManager& tm,
    const std::wstring& exePath,
    unsigned short port,
    const std::wstring& token,
    int workerId);

void CloseWorkerTab(TabManager& tm, int tabIndex);
