#pragma once

#include <windows.h>
#include <string>

// 音频播放模块（基于 Windows Media Foundation）
// 职责：把音频文件异步播放到指定的音频输出设备（默认设备或虚拟声卡）。

// 初始化播放模块。
// deviceMatch: 按名称子串匹配一个“输出”设备（如 "CABLE"）；为空则用系统默认输出设备。
// usedDevice:  实际匹配到的设备名（空表示默认设备）。
// 返回 false 表示初始化失败，err 给出原因。
bool PlayerInitialize(const std::wstring& deviceMatch, std::wstring& err, std::wstring& usedDevice);

// 播放一个音频文件（异步，立即返回）。
// durationMs 返回文件时长（毫秒），可用于自动按住 PTT。
bool PlayerPlayFile(const std::wstring& path, std::wstring& err, DWORD& durationMs);

// 停止当前播放。
void PlayerStop();

// 释放播放模块（程序退出时调用）。
void PlayerShutdown();
