#include "config.h"

#include <sstream>
#include <algorithm>
#include <cwctype>

namespace {

// 去掉首尾空白
std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::wstring ToUpperW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towupper(c);
    return s;
}

// 用 Win32 API 读取文件（兼容中文路径），再按 UTF-8 / ANSI 解码成宽字符串
bool ReadFileToWstring(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(h, nullptr);
    std::string bytes(size, '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, &bytes[0], size, &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    bytes.resize(read);

    // 去掉 UTF-8 BOM（若有）
    size_t start = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) {
        start = 3;
    }
    // 优先按 UTF-8 解码；失败（乱码字节）则退回系统 ANSI 代码页
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(cp, flags, bytes.data() + start,
                                (int)(bytes.size() - start), nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, bytes.data() + start,
                                (int)(bytes.size() - start), nullptr, 0);
    }
    if (n <= 0) return false;
    out.resize(n);
    MultiByteToWideChar(cp, flags, bytes.data() + start,
                        (int)(bytes.size() - start), &out[0], n);
    return true;
}

} // namespace

bool ParseHotkeyText(const std::wstring& text, UINT& mods, UINT& vk) {
    mods = 0;
    std::wstring t = ToUpperW(Trim(text));

    // 解析 "CTRL+" / "SHIFT+" / "ALT+" 前缀
    size_t pos;
    while ((pos = t.find(L"+")) != std::wstring::npos) {
        std::wstring head = t.substr(0, pos);
        if (head == L"CTRL")  mods |= MOD_CONTROL;
        else if (head == L"SHIFT") mods |= MOD_SHIFT;
        else if (head == L"ALT")   mods |= MOD_ALT;
        else return false;  // 未知修饰键
        t = t.substr(pos + 1);
    }
    t = Trim(t);

    if (t.size() == 1 && t[0] >= L'0' && t[0] <= L'9') { vk = (UINT)t[0]; return true; }
    if (t.size() == 1 && t[0] >= L'A' && t[0] <= L'Z') { vk = (UINT)t[0]; return true; }
    if (t[0] == L'F' && t.size() >= 2) {
        int n = _wtoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) { vk = VK_F1 + (UINT)(n - 1); return true; }
    }
    return false;
}

bool LoadConfig(const std::wstring& path, Config& cfg, std::wstring& err) {
    std::wstring content;
    if (!ReadFileToWstring(path, content)) {
        err = L"无法读取文件（请确认 config.txt 与程序在同一目录）";
        return false;
    }

    cfg.sounds.clear();
    cfg.pttVk = 0;
    cfg.stopMods = 0;
    cfg.stopVk   = VK_F12;
    cfg.stopName = L"F12";
    cfg.deviceMatch.clear();
    // 注意：cfg.soundsDir 保留调用者给的默认值，除非文件里有 sounds_dir 指令

    std::wistringstream ss(content);
    std::wstring line;
    int lineNo = 0;
    while (std::getline(ss, line)) {
        lineNo++;
        line = Trim(line);
        if (line.empty() || line[0] == L'#') continue;   // 空行 / 注释

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;          // 没有等号，忽略

        std::wstring key = Trim(line.substr(0, eq));
        std::wstring val = Trim(line.substr(eq + 1));
        std::wstring keyU = ToUpperW(key);

        if (keyU == L"DEVICE")      { cfg.deviceMatch = val; continue; }
        if (keyU == L"PTT_KEY") {
            UINT m, v;
            if (!ParseHotkeyText(val, m, v)) {
                err = L"第" + std::to_wstring(lineNo) + L"行：无法识别的 PTT 按键 [" + val + L"]";
                return false;
            }
            cfg.pttVk = v;
            continue;
        }
        if (keyU == L"STOP") {
            UINT m, v;
            if (!ParseHotkeyText(val, m, v)) {
                err = L"第" + std::to_wstring(lineNo) + L"行：无法识别的停止键 [" + val + L"]";
                return false;
            }
            cfg.stopMods = m;
            cfg.stopVk   = v;
            cfg.stopName = val;
            continue;
        }
        if (keyU == L"SOUNDS_DIR")  { cfg.soundsDir = val; continue; }

        // 其余一律当作“热键 = 文件”
        SoundEntry e;
        if (!ParseHotkeyText(key, e.hotkeyMods, e.hotkeyVk)) {
            err = L"第" + std::to_wstring(lineNo) + L"行：无法识别的热键写法 [" + key + L"]";
            return false;
        }
        e.hotkeyName = key;
        e.filePath = val;
        cfg.sounds.push_back(std::move(e));
    }
    return true;
}
