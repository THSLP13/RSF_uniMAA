#pragma once

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

#include "AsstStatusManager.h"

void RenderInitializingState();

void RenderMainMenuState();

void RenderRunningTaskState();

void RenderTaskSuccessState();

void RenderStoppingState();

void UpdateScreenshotDisplay(ID3D11Device* device, ID3D11DeviceContext* context);

void ShowAutoBattleWindow(bool* p_open);

bool maa_loop();

void maa_return();

void UpdateInitializationProgress(float progress);

void check_notification_auto_open(const AssistantStatus& status);

void draw_menu_bar(const AssistantStatus& status);

void show_current_tasks_and_notifications(const AssistantStatus& status);

void show_task_chains(const AssistantStatus& status);

void show_connection_status(const AssistantStatus& status);

int select_best_gpu_adapter();

namespace fs = std::filesystem;
fs::path get_exe_fulldirectory();
