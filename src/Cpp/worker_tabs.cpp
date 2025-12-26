// worker_tabs.cpp
#include "worker_tabs.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

static std::wstring Quote(const std::wstring& s)
{
    if (s.find(L' ') == std::wstring::npos) {
        return s;
    }
    return L"\"" + s + L"\"";
}

static std::wstring BuildWorkerCmdLine(
    const std::wstring& exePath,
    int workerId,
    unsigned short port,
    const std::wstring& token,
    int tabId,
    const std::wstring& logPath = L"")
{
    std::wstringstream ss;
    ss << Quote(exePath) << L" --role=worker" << L" --workerId=" << workerId << L" --port=" << port << L" --token="
       << token << L" --tabId=" << tabId;

    if (!logPath.empty()) {
        ss << L" --log=" << Quote(logPath);
    }else {
        std::filesystem::path logpath = exePath;
        logpath = logpath.parent_path() /*/ "debug" / std::to_string(workerId)*/;
        ss << L" --log=" << Quote(logpath);
    }
    return ss.str();
}

static bool StartWorkerProcess(
    WorkerProc& out,
    const std::wstring& exePath,
    int workerId,
    unsigned short port,
    const std::wstring& token,
    int tabId)
{
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};

    std::wstring cmd = BuildWorkerCmdLine(exePath, workerId, port, token, tabId);

    // CreateProcessW 需要可写 buffer
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        buf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW, // 你也可以改成 0 方便调试
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!ok) {
        return false;
    }

    out.pi = pi;
    out.running = true;
    return true;
}

static void StopWorkerProcess(WorkerProc& p)
{
    if (!p.running) {
        return;
    }

    // 温柔一点：先发 WM_CLOSE 或你自己的 quit 消息更好
    // 这里先用 TerminateProcess 当兜底（你可以后续改成发送 control: {"type":"quit"}）
    TerminateProcess(p.pi.hProcess, 0);

    CloseHandle(p.pi.hThread);
    CloseHandle(p.pi.hProcess);
    p = WorkerProc {};
}

WorkerTab& AddWorkerTab(
    TabManager& tm,
    const std::wstring& exePath,
    unsigned short port,
    const std::wstring& token,
    int workerId)
{
    WorkerTab t {};
    t.tab_id = tm.next_tab_id++;
    t.worker_id = workerId;
    t.title = L"Worker " + std::to_wstring(workerId);
    
    StartWorkerProcess(t.proc, exePath, workerId, port, token, t.tab_id);

    tm.tabs.push_back(std::move(t));
    tm.selected_tab_index = (int)tm.tabs.size() - 1;

    if (tm.target_worker) {
        tm.target_worker->store(workerId);
    }
    return tm.tabs.back();
}

void CloseWorkerTab(TabManager& tm, int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= (int)tm.tabs.size()) {
        return;
    }

    StopWorkerProcess(tm.tabs[tabIndex].proc);
    tm.tabs.erase(tm.tabs.begin() + tabIndex);

    if (tm.tabs.empty()) {
        tm.selected_tab_index = -1;
        if (tm.target_worker) {
            tm.target_worker->store(0);
        }
    }
    else {
        tm.selected_tab_index = std::min(tm.selected_tab_index, (int)tm.tabs.size() - 1);
        if (tm.target_worker) {
            tm.target_worker->store(tm.tabs[tm.selected_tab_index].worker_id);
        }
    }
}
