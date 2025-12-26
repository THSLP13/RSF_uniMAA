// main.cpp
#include "cmdline.h"
#define NOMINMAX
#include <Windows.h>

#pragma comment(lib, "d3d11.lib")                  // 对应 d3d11.h
#pragma comment(lib, "dxgi.lib")                   // DXGI 库（DX11 通常依赖）
#pragma comment(lib, "opencv_world4.lib")          // 对应 opencv2/opencv.hpp
#pragma comment(lib, "Shell32.lib")

int RenderMain(HINSTANCE hInstance, int nCmdShow); // 你现有的 Win32+DX11+ImGui 主逻辑搬进去
int WorkerMain(const Args& args);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow)
{
    system("chcp 65001");

    Args args = ParseArgs();
    if (args.is_worker) {
        int ret = WorkerMain(args);
        //MessageBoxA(0, "rsf.renderer.core 已丢失链接,rsf.worker.core将退出", "rsf.main -> rsf.tools.msgbox", 0);
        return ret;
    }
    return RenderMain(hInstance, nCmdShow);
}
