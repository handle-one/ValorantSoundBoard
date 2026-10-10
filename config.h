#pragma once

#include <windows.h>
#include <string>
#include <vector>

// 一条“热键 → 音频”绑定
struct SoundEntry {
    UINT hotkeyMods = 0;      // MOD_CONTROL / MOD_SHIFT / MOD_ALT 的组合（0 = 无修饰键）
    UINT hotkeyVk   = 0;      // 虚拟键码，如 VK_F6 / 'A' / '0'
    std::wstring hotkeyName;  // 原始写法，如 "F6" 或 "CTRL+F8"，用于界面显示
    std::wstring filePath;    // 音频文件的绝对路径
};

// 整个程序的配置
struct Config {
    std::vector<SoundEntry> sounds;   // 声音条目
    UINT pttVk = 0;                   // 播放时自动按住的 PTT 键（0 = 关闭）
    UINT stopMods = 0;                // 停止键的修饰键（0 = 无）
    UINT stopVk   = VK_F12;           // 停止键的虚拟键码（默认 F12）
    std::wstring stopName = L"F12";   // 停止键的显示名
    std::wstring deviceMatch;         // 输出设备名称匹配子串（空 = 系统默认设备）
    std::wstring soundsDir;           // 音频文件基准目录
};

// 解析 config.txt。失败返回 false 并在 err 中说明原因。
// 注意：cfg.soundsDir 若文件里有 sounds_dir 指令会被覆盖，否则保留调用者给的默认值。
bool LoadConfig(const std::wstring& path, Config& cfg, std::wstring& err);

// 把 "F6" / "CTRL+F8" 这样的文本解析成修饰键 + 虚拟键码。
bool ParseHotkeyText(const std::wstring& text, UINT& mods, UINT& vk);
