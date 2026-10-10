// ValoSoundBoard —— 用全局热键在游戏语音里播放音频的小工具
//
// 总流程（教学时对照看）：
//   Windows 会把“全局热键”做成消息：调用 RegisterHotKey 注册后，
//   只要程序还在消息循环（GetMessage），按下的热键就会变成一条
//   WM_HOTKEY 消息送到这里。收到消息 -> 查表找到对应的音频文件 ->
//   调用播放模块把它放给虚拟声卡 -> 虚拟声卡作为麦克风输入进游戏语音。
//
// 编译：见 build.bat / build_msvc.bat（或 README.md）

#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <thread>
#include <cstdarg>
#include "config.h"
#include "player.h"

// ---------------- 控制台输出 ----------------

// 按控制台当前代码页输出（解决中文乱码）：
// 程序内部统一用宽字符串（UTF-16）；输出前转成控制台正在用的代码页
// （GBK 控制台转 GBK、UTF-8 终端转 UTF-8），任何终端都显示正常，
// 不再依赖 SetConsoleOutputCP。
static void CPrint(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2048, fmt, ap);
    va_end(ap);
    UINT cp = GetConsoleOutputCP();
    if (cp == 0) cp = CP_UTF8;
    char mb[4096];
    int n = WideCharToMultiByte(cp, 0, buf, -1, mb, (int)sizeof(mb), nullptr, nullptr);
    if (n > 0) fputs(mb, stdout);
}

// ---------------- 全局状态 ----------------
static Config g_cfg;
static std::wstring g_exeDir;
static std::vector<SoundEntry> g_bindings;  // 已成功注册的热键 -> 声音
static bool g_configDirty = false;          // 配置有问题时用于提示

// 三个保留热键
static const UINT kIdReload = 900;  // F5        ：重新加载 config.txt
static const UINT kIdQuit   = 901;  // Ctrl+Q    ：退出程序
static const UINT kIdStop   = 902;  // 停止键（config.txt 的 stop = ...，默认 F12）：停止当前播放

// ---------------- 小工具函数 ----------------

// 向系统发送一次键盘按下/抬起（用于自动按住 PTT）
static void SendKey(UINT vk, bool down) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

// 把配置里的相对路径补全成绝对路径
static std::wstring ResolvePath(const std::wstring& p) {
    if (p.size() >= 2 && p[1] == L':') return p;   // D:\xxx 已是绝对路径
    if (!p.empty() && (p[0] == L'\\' || p[0] == L'/')) return p;  // UNC / 根路径
    return g_cfg.soundsDir + L"\\" + p;
}

// 把每条声音的路径补全并检查文件是否存在
static void FinalizePaths() {
    for (auto& e : g_cfg.sounds) {
        e.filePath = ResolvePath(e.filePath);
        if (GetFileAttributesW(e.filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            CPrint(L"  [警告] 文件不存在：%ls\n", e.filePath.c_str());
        }
    }
}

// ---------------- 热键注册 ----------------

static void RegisterAll() {
    g_bindings.clear();
    for (auto& e : g_cfg.sounds) {
        if (e.hotkeyMods == g_cfg.stopMods && e.hotkeyVk == g_cfg.stopVk) {
            // 与“停止播放”键撞车：跳过绑定（可在 config.txt 用 stop = 换一个停止键）
            CPrint(L"  [警告] %ls 是“停止播放”保留键，已跳过 %ls 的绑定\n",
                         g_cfg.stopName.c_str(), e.hotkeyName.c_str());
            continue;
        }
        UINT id = (UINT)(g_bindings.size() + 1);  // 1..N
        if (RegisterHotKey(nullptr, id, e.hotkeyMods | MOD_NOREPEAT, e.hotkeyVk)) {
            g_bindings.push_back(e);
        } else {
            CPrint(L"  [警告] 热键 %ls 注册失败（可能被其他程序占用，换个键试试）\n",
                         e.hotkeyName.c_str());
        }
    }
    RegisterHotKey(nullptr, kIdReload, MOD_NOREPEAT, VK_F5);
    RegisterHotKey(nullptr, kIdStop, g_cfg.stopMods | MOD_NOREPEAT, g_cfg.stopVk);
    RegisterHotKey(nullptr, kIdQuit, MOD_CONTROL | MOD_NOREPEAT, L'Q');
}

static void UnregisterAll() {
    for (size_t i = 0; i < g_bindings.size(); i++)
        UnregisterHotKey(nullptr, (UINT)(i + 1));
    UnregisterHotKey(nullptr, kIdReload);
    UnregisterHotKey(nullptr, kIdStop);
    UnregisterHotKey(nullptr, kIdQuit);
    g_bindings.clear();
}

// ---------------- 界面输出 ----------------

static void PrintStatus() {
    CPrint(L"\n========== ValoSoundBoard ==========\n");
    CPrint(L"热键列表：\n");
    for (auto& e : g_bindings)
        CPrint(L"  %-12ls -> %ls\n", e.hotkeyName.c_str(), e.filePath.c_str());
    if (g_cfg.pttVk)
        CPrint(L"自动按住 PTT 键：VK=0x%02X\n", g_cfg.pttVk);
    else
        CPrint(L"自动 PTT：关闭（请在游戏里自己按住“说话”键）\n");
    CPrint(L"F5 = 重新加载配置    %ls = 停止播放    Ctrl+Q = 退出\n",
                 g_cfg.stopName.c_str());
    CPrint(L"=====================================\n\n");
}

// ---------------- 播放 ----------------

static void PlayEntry(const SoundEntry& e) {
    DWORD dur = 0;
    std::wstring err;

    bool ptt = g_cfg.pttVk != 0;
    if (ptt) SendKey(g_cfg.pttVk, true);  // 先按住游戏里的“说话”键

    bool ok = PlayerPlayFile(e.filePath, err, dur);
    if (!ok) {
        CPrint(L"播放失败 [%ls]：%ls\n", e.hotkeyName.c_str(), err.c_str());
        if (ptt) SendKey(g_cfg.pttVk, false);
        return;
    }

    CPrint(L"[%ls] 播放 %ls（%.1f 秒）\n",
                 e.hotkeyName.c_str(), e.filePath.c_str(), dur / 1000.0);

    // 自动 PTT：播放结束后再松开（多等 300ms 防吞尾音）
    if (ptt) {
        UINT vk = g_cfg.pttVk;
        DWORD hold = dur + 300;
        std::thread([vk, hold] { Sleep(hold); SendKey(vk, false); }).detach();
    }
}

// ---------------- 配置重载 ----------------

static void ReloadConfig() {
    UnregisterAll();

    Config backup = g_cfg;
    std::wstring err;
    if (!LoadConfig(g_exeDir + L"config.txt", g_cfg, err)) {
        CPrint(L"配置重新加载失败：%ls（保留旧配置）\n", err.c_str());
        g_cfg = backup;
        RegisterAll();
        PrintStatus();
        return;
    }
    FinalizePaths();

    // device 变了就重启播放器
    if (g_cfg.deviceMatch != backup.deviceMatch) {
        std::wstring perr, usedDev;
        PlayerShutdown();
        PlayerInitialize(g_cfg.deviceMatch, perr, usedDev);
        if (!usedDev.empty())
            CPrint(L"音频输出设备：%ls\n", usedDev.c_str());
        if (!perr.empty())
            CPrint(L"提示：%ls\n", perr.c_str());
    }

    RegisterAll();
    PrintStatus();
}

// ---------------- 控制台关闭时的清理 ----------------

static BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        UnregisterAll();
        PlayerShutdown();
        return TRUE;
    }
    return FALSE;
}

// ---------------- 入口 ----------------

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // 无缓冲输出：日志实时可见（重定向也不丢）
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // ---- 测试模式：ValoSoundBoard.exe --play <音频文件> ----
    // 用于不按热键直接验证播放管线（开发/调试用）
    if (argc >= 3 && wcscmp(argv[1], L"--play") == 0) {
        std::wstring perr, dev;
        if (!PlayerInitialize(L"", perr, dev)) {
            CPrint(L"初始化失败：%ls\n", perr.c_str());
            return 1;
        }
        DWORD dur = 0;
        std::wstring err;
        if (!PlayerPlayFile(argv[2], err, dur)) {
            CPrint(L"播放失败：%ls\n", err.c_str());
            PlayerShutdown();
            return 1;
        }
        CPrint(L"开始播放 %ls（%.1f 秒）……\n", argv[2], dur / 1000.0);
        Sleep(dur + 800);
        PlayerShutdown();
        CPrint(L"测试完成。\n");
        return 0;
    }

    // 1. 定位程序所在目录（config.txt 与程序放一起）
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_exeDir = exe;
    size_t slash = g_exeDir.find_last_of(L"\\/");
    g_exeDir = g_exeDir.substr(0, slash + 1);
    g_cfg.soundsDir = g_exeDir + L"sounds";

    // 2. 加载配置
    std::wstring err;
    if (!LoadConfig(g_exeDir + L"config.txt", g_cfg, err)) {
        CPrint(L"配置加载失败：%ls\n请在程序目录放好 config.txt 并填写热键。\n",
                     err.c_str());
        CPrint(L"按回车键退出……");
        getchar();
        return 1;
    }
    FinalizePaths();

    // 3. 初始化播放器
    std::wstring perr, usedDev;
    if (!PlayerInitialize(g_cfg.deviceMatch, perr, usedDev)) {
        CPrint(L"播放器初始化失败：%ls\n", perr.c_str());
        return 1;
    }
    if (!usedDev.empty())
        CPrint(L"音频输出设备：%ls\n", usedDev.c_str());
    if (!perr.empty())
        CPrint(L"提示：%ls\n", perr.c_str());

    // 4. 注册热键并进入消息循环
    RegisterAll();
    PrintStatus();

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY) {
            UINT id = (UINT)msg.wParam;
            if (id == kIdReload) { ReloadConfig(); continue; }
            if (id == kIdStop) {
                PlayerStop();  // 停掉正在播放的会话；程序继续留在消息循环
                CPrint(L"[停止] 已停止当前播放\n");
                continue;
            }
            if (id == kIdQuit)   { break; }
            if (id >= 1 && id <= (UINT)g_bindings.size())
                PlayEntry(g_bindings[id - 1]);
        }
    }

    UnregisterAll();
    PlayerShutdown();
    return 0;
}
