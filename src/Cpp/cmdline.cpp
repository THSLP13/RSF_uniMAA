// cmdline.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "cmdline.h"
#include <string>
#include <vector>

static std::wstring GetOpt(const std::vector<std::wstring>& argv, const std::wstring& key)
{
    const std::wstring prefix = key + L"=";
    for (auto& a : argv) {
        if (a.rfind(prefix, 0) == 0) {
            return a.substr(prefix.size());
        }
    }
    return L"";
}

static bool HasFlag(const std::vector<std::wstring>& argv, const std::wstring& flag)
{
    for (auto& a : argv) {
        if (a == flag) {
            return true;
        }
    }
    return false;
}

Args ParseArgs()
{
    Args out {};

    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> argv;
    argv.reserve(argc);
    for (int i = 0; i < argc; i++) {
        argv.push_back(wargv[i]);
    }
    LocalFree(wargv);

    // role
    // 兼容你原来写法：--role=worker
    for (auto& a : argv) {
        if (a == L"--role=worker") {
            out.is_worker = true;
        }
    }
    // 也支持：--worker 这种flag（可选）
    if (HasFlag(argv, L"--worker")) {
        out.is_worker = true;
    }

    // tabId
    auto tab = GetOpt(argv, L"--tabId");
    if (!tab.empty()) {
        out.tab_id = _wtoi(tab.c_str());
    }

    // workerId
    auto wid = GetOpt(argv, L"--workerId");
    if (!wid.empty()) {
        out.worker_id = _wtoi(wid.c_str());
    }

    // port
    auto port = GetOpt(argv, L"--port");
    if (!port.empty()) {
        int p = _wtoi(port.c_str());
        if (p > 0 && p <= 65535) {
            out.port = (unsigned short)p;
        }
    }

    // token（没给就用默认 2750bch）
    auto tok = GetOpt(argv, L"--token");
    if (!tok.empty()) {
        out.token = tok;
    }

    // log
    out.log_path = GetOpt(argv, L"--log");

    // 兼容旧参数（如果你还没删干净调用方，先保留也无妨）
    // out.shm_name  = GetOpt(argv, L"--shm");
    // out.evt_name  = GetOpt(argv, L"--evt");
    // out.pipe_name = GetOpt(argv, L"--pipe");

    return out;
}
