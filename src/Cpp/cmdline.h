// cmdline.h
#pragma once
#include <string>

struct Args
{
    bool is_worker = false;

    int tab_id = 0;
    int worker_id = 0;           // worker序列ID
    unsigned short port = 51234; // 默认端口
    std::wstring token = L"2750bch";

    std::wstring log_path;
};

Args ParseArgs();
