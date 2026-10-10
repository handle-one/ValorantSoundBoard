# ValoSoundBoard —— 游戏语音音效板（C++ 项目）

一个用 C++ 写的小程序：打《无畏契约》时按下**全局热键**（比如 F6），就能在**队内语音**里播放一段下载好的音频（"Nice!"、"我封烟了"、整活语音等）。

> 配套架构图见 `architecture.html`（用浏览器打开）。
> 完整项目含：`main.cpp`（热键 + 消息循环）、`config.cpp`（配置解析）、`player.cpp`（Media Foundation 播放）、`build.bat`（一键编译）。

---

## 一、这个程序能干什么

- 注册**全局热键**：哪怕游戏窗口全屏、焦点在游戏里，按 F6/F7……也能触发。
- 按热键 → 播放对应的音频文件（mp3 / wav / m4a）。
- 声音**只进虚拟声卡**（默认配置），不会从你的音箱/耳机里放出来打扰自己——只有队友能在语音里听到。
- 支持"自动按住说话键"：如果你的语音是"按住说话"模式，程序可以在播放期间自动帮你按住 PTT 键（可选，默认关闭）。

## 二、核心原理（先看这里，这是重点）

### 2.1 一句话原理

> **让程序把音频"播放"到一个虚拟麦克风，再把游戏的麦克风设置成这个虚拟设备。**
> 这样游戏语音的输入 = 程序播放的声音，热键一按，声音就"进"了队内语音。

### 2.2 数据流

```
你按 F6 (任意窗口下)
   │  ① RegisterHotKey 已经把这个热键注册给系统
   ▼
系统把一条 WM_HOTKEY 消息投进程序的消息队列
   │  ② 程序的主循环 GetMessage() 收到消息
   ▼
main.cpp：查表 g_bindings —— F6 → sounds\example.wav
   │  ③ 调用播放模块
   ▼
player.cpp：Media Foundation 构建"拓扑"
   媒体源(解码example.wav) → 音频渲染器(指定输出设备=虚拟声卡)
   │  ④ 声音被送往 VB-CABLE 的 Output 端
   ▼
VB-CABLE 内部把 Output 接到 Input（这就是"虚拟声卡"干的活）
   │  ⑤ 系统把 CABLE Input 看作一个麦克风
   ▼
无畏契约：语音输入设备 = CABLE Input
   │  ⑥ 声音进队内语音，队友听到
   ▼
队友：？？？
```

### 2.3 两个关键概念

**① 全局热键 = 系统级的"事件订阅"**
Windows 提供了 `RegisterHotKey(hwnd, id, modifiers, vk)`：注册之后，**不管焦点在哪个窗口**，只要这个键被按下，系统就会往你的消息队列投一条 `WM_HOTKEY` 消息。你的程序什么都不用做，只要老老实实跑一个消息循环（`GetMessage`）等着收消息就行。

**② 虚拟声卡 = "程序输出"和"麦克风输入"之间的桥**
真实麦克风把空气振动变成电信号；而虚拟声卡（VB-CABLE）把"某个程序的音频输出"当作信号源。VB-CABLE 装好后系统里会出现两个设备：

- `CABLE Output`（输出设备）：程序可以把声音"播放"到这里
- `CABLE Input`（输入设备）：系统把它当麦克风，游戏可以选它当语音输入

所以：**程序 → CABLE Output →（内部跳线）→ CABLE Input → 游戏语音**。声音根本没经过真实扬声器，队友却能听到。

## 三、你需要准备的东西

| 东西 | 说明 |
| --- | --- |
| Windows 10/11 | 程序基于 Win32 + Media Foundation，都是 Windows 自带 |
| VB-CABLE | 免费虚拟声卡驱动，官网 https://vb-audio.com/Cable/ ，下载 `VBCABLE_Driver_Pack` 后**以管理员身份**运行安装 |
| C++ 编译器 | 如果电脑上没有，本项目已经给你装好了便携版 MinGW-w64：`C:\tools\mingw64` |

## 四、编译

在项目目录（本文件夹）里：

**方法 A：双击 `build.bat`**（自动找 g++，一键编译出 `ValoSoundBoard.exe`）

**方法 B：手动命令（MinGW）**
```
C:\tools\mingw64\bin\g++.exe -O2 -std=c++17 -municode main.cpp config.cpp player.cpp -o ValoSoundBoard.exe -static -luser32 -lmfplat -lmf -lmfuuid -luuid -lole32
```
（`-static` 把所有运行时打进去，exe 拿到任何电脑都能跑；`-municode` 启用宽字符入口 `wmain`）

**方法 C：Visual Studio**
打开"开发者 PowerShell"，运行 `build_msvc.bat`；或者新建控制台项目，把三个 .cpp 加进去，附加依赖库填：`user32.lib mfplat.lib mf.lib mfuuid.lib ole32.lib`。

## 五、第一次运行与测试

1. 还没装 VB-CABLE 的话，先把 `config.txt` 里的 `device = CABLE` 改成 `device =`（留空 = 用默认输出设备，声音从音箱出来，方便先试通）。
2. 双击 `ValoSoundBoard.exe`，会打印当前热键列表。
3. 按 **F6**：应该能听到 `sounds\example.wav`（程序自带的一小段提示音）。
4. 按 **F5** 重新加载配置，**F12** 停止当前播放（可在 config.txt 用 `stop =` 改键），**Ctrl+Q** 退出。
5. 测试通过后，装好 VB-CABLE，把 `device = CABLE` 改回来，重启程序。

> 调试小技巧：`ValoSoundBoard.exe --play 声音文件.wav` 可以不按热键直接播放一次，用来快速验证播放管线有没有问题。

## 六、配置 config.txt（语法）

```
热键 = 音频文件
```

- **热键写法**：`F1`~`F24`、`A`~`Z`、`0`~`9`，可加前缀 `CTRL+` / `SHIFT+` / `ALT+`，例如 `CTRL+F8`。
- **音频文件**：相对路径以 `sounds` 文件夹为基准；也可以写绝对路径如 `D:\我的声音\太棒了.mp3`。
- **保留热键**：`F5` = 重新加载配置；`stop` 指令指定的键 = 停止当前播放（默认 `F12`，不退出程序）；`Ctrl+Q` = 退出程序。请别把保留键配置成声音键。

可选指令：

```
device = CABLE        # 输出设备名匹配子串；空 = 默认设备
ptt_key = V           # 播放期间自动按住该键（游戏里"按住说话"的键）
sounds_dir = sounds   # 音频目录（默认就是 sounds）
```

## 七、接入无畏契约语音

1. 装好 VB-CABLE（见第三节）。
2. 打开无畏契约 → 设置 → 音频 → 语音，把**麦克风输入设备**改成 `CABLE Input (VB-Audio Virtual Cable)`。
3. 把 `config.txt` 里 `device = CABLE` 保持开启，重启程序。
4. 游戏里开一局自定义或靶场测试：按 F6，问队友"听得到吗"。

如果语音是"按住说话"模式：要么你自己按住说话键的同时按热键；要么在配置里加 `ptt_key = V`（V 是无畏契约默认的语音键），让程序自动按住。

## 八、代码讲解（教学部分）

### 8.1 总体架构：三个模块

| 文件 | 职责 | 对应你学过的知识 |
| --- | --- | --- |
| `main.cpp` | 主循环、热键注册与分发 | 事件驱动、查表分发（像状态机/命令分发） |
| `config.cpp` | 读取解析 config.txt，生成 `Config` 结构 | 字符串解析、结构体/容器 |
| `player.cpp` | 用 Media Foundation 播放音频到指定设备 | 资源管理（RAII 思想）、组件化 |

### 8.2 main.cpp —— 热键与消息循环

```cpp
RegisterHotKey(nullptr, id, mods | MOD_NOREPEAT, vk);
```
- 第一个参数是窗口句柄，传 `nullptr` 表示"注册到当前线程的消息队列"，不依赖任何窗口。
- 返回 `FALSE` 说明热键被别的程序占了（比如 F10 常被占），换个键即可。

之后程序进入消息循环：

```cpp
MSG msg;
while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == WM_HOTKEY) {
        UINT id = (UINT)msg.wParam;   // 按的是第几个注册的热键
        // 查表 → 播放
    }
}
```

**教学点**：Windows 程序本质是"消息驱动"——系统把键盘、鼠标、定时器都变成消息，你的循环就是分发中心。`g_bindings` 是一个 `vector<SoundEntry>`，用热键 id 当下标 O(1) 查表，这就是你学过的数据结构在真实程序里的用法。

自动 PTT 用了 `SendInput`（模拟一次按键按下/抬起），放在一个 `std::thread` 里延迟松键，避免阻塞主循环。

### 8.3 config.cpp —— 配置解析

- 用 `CreateFileW / ReadFile` 读文件（不是 `fstream`，因为宽字符路径更稳），按 UTF-8 优先、ANSI 兜底解码成 `std::wstring`。
- **为什么用 `wstring`**：Windows 文件路径和界面文本都可能含中文（`D:\我的声音\太棒了.mp3`），`wchar_t` 是 Windows 原生字符类型。
- 解析逻辑：逐行 → 去注释/空白 → 按 `=` 拆分 → 左半边解析成热键（`ParseHotkeyText` 把 `"CTRL+F8"` 拆成修饰键 + 虚拟键码），右半边存文件路径。

```cpp
struct SoundEntry { UINT hotkeyMods; UINT hotkeyVk; std::wstring hotkeyName; std::wstring filePath; };
struct Config   { std::vector<SoundEntry> sounds; UINT pttVk; std::wstring deviceMatch; std::wstring soundsDir; };
```

### 8.4 player.cpp —— Media Foundation 播放

Media Foundation（MF）是 Windows 自带的媒体框架。核心抽象是**拓扑（Topology）**：

```
[媒体源节点 Source]  --连接-->  [输出节点 Audio Renderer]
    解码文件                   把 PCM 声音交给某个音频设备
```

构建步骤：
1. **创建媒体源**：代码里用 `MFCreateSourceResolver()` + `CreateObjectFromURL()`（等价于 `MFCreateMediaSourceFromURL`，因为 MinGW 头文件缺后者）。按扩展名自动选解码器（mp3/wav/m4a 都行）。
2. `source->CreatePresentationDescriptor(...)`：读出文件时长（MF 时长单位是 100ns，除以 10000 得毫秒）。
3. `BuildTopology(...)`：建两个节点连起来。**源节点类型是 `MF_TOPOLOGY_SOURCESTREAM_NODE`（=1）**，输出节点是 `MFCreateAudioRendererActivate()`。**关键是这句**：

```cpp
renderer->SetString(MFAUDIO_RENDER_ATTRIBUTE_ENDPOINT_ID, endpointId.c_str());
```

设备 ID 从哪来？`IMMDeviceEnumerator` 枚举所有输出设备，逐个读"友好名称"，匹配包含 `CABLE` 的那个，取它的 ID。所以 `config.txt` 里写 `device = CABLE` 就是匹配规则。

4. `g_session->SetTopology(0, topo)` + `g_session->Start(&GUID_NULL, &var)`：把拓扑交给会话播放。播放是**异步**的，函数立即返回，主循环继续收热键。

> **调试时踩过的三个真实坑**（写进这里帮你少走弯路）：
> 1. 拓扑节点类型：网上一些旧资料写源节点是 `MF_TOPOLOGY_SOURCENODE = 0x10000000`，**在本机实测这个值会被拒绝（返回 E_INVALIDARG）**。正确值是 `MF_TOPOLOGY_SOURCESTREAM_NODE = 1`（以微软官方文档为准）。
> 2. `session->Start()` 两个参数不能传 `NULL`：`Start(nullptr, nullptr)` 在本机实测返回 E_POINTER。正确写法是 `Start(&GUID_NULL, &var)`，其中 `var` 是 `vt = VT_EMPTY` 的 `PROPVARIANT`。
> 3. **第二次播放没有声音（只响第一次）**：会话（`IMFMediaSession`）在播放结束后，旧拓扑/旧源的清理是**异步**的。如果旧源还在被会话引用时就调用 `Shutdown`，会话后续会报 `MF_E_SHUTDOWN (0xC00D3E85)`——此时 `SetTopology`/`Start` 都返回成功，但实际没有声音。排查方法：用 `GetEvent` 读会话事件，能看到报错。修复：**每次播放都新建一个全新会话**（旧会话先整体 `Shutdown`，立即失效），见 player.cpp 的 `StopLocked` 与 `PlayerPlayFile`。

### 8.5 关键 API 速查表

| API | 作用 | 属于 |
| --- | --- | --- |
| `RegisterHotKey` | 注册全局热键 | Win32 |
| `GetMessageW` / `WM_HOTKEY` | 消息循环收热键消息 | Win32 |
| `SendInput` | 模拟按键（自动 PTT） | Win32 |
| `CreateFileW` / `MultiByteToWideChar` | 读配置、转宽字符 | Win32 |
| `CoInitializeEx` / `MFStartup` | 初始化 COM 和媒体框架 | COM / MF |
| `MFCreateMediaSourceFromURL` | 按文件创建解码源 | MF |
| `MFCreateAudioRendererActivate` | 创建音频渲染器（可指定设备） | MF |
| `MFAUDIO_RENDER_ATTRIBUTE_ENDPOINT_ID` | 渲染器属性：目标设备 ID | MF |
| `IMMDeviceEnumerator` | 枚举系统音频设备 | WASAPI |

## 九、常见问题（FAQ）

**Q：按热键没反应？**
先看控制台：程序启动时会打印注册成功的列表。如果某一行没出现，说明热键被占用（换个键）。另外确认程序在运行（窗口别关）。

**Q：怎么停止正在播放的声音，又不退出程序？**
按停止键（默认 **F12**，可在 config.txt 里用 `stop = F1` 之类的写法改键）。它会立刻停掉当前播放（`PlayerStop()` 把正在播放的媒体会话 Stop + Shutdown），但程序本身还在消息循环里，热键照常可用。停止键不能同时配给声音。

**Q：提示"播放失败 / 打开音频文件失败"？**
文件格式不被 MF 支持（如 .ogg、.flac）。转成 mp3 / wav 即可。

**Q：控制台里中文显示成乱码（????）？**
程序会按控制台当前的代码页输出（GBK 控制台输出 GBK、UTF-8 终端输出 UTF-8），正常不会乱码。如果用的是旧版程序，重新编译一次即可（新版 main.cpp 里用 `CPrint` 统一处理编码）。

**Q：第一次按有声音，之后按了日志显示播放但没有声音？**
这是旧版本的一个 bug（会话异步清理导致 `MF_E_SHUTDOWN`），已在 2026/10/5 修复：现在每次播放都新建会话。重新编译或直接使用新版 exe 即可。

**Q：游戏全屏时热键没反应？**
全局热键不受全屏影响。但有些游戏/输入法（尤其"游戏模式"）可能拦截键盘，可在 Windows 设置里关掉"游戏模式"的键盘干扰，或换个不冲突的键。

**Q：Vanguard 反作弊会封号吗？**
这个程序**不注入游戏进程、不读写游戏内存**，只用了 Windows 系统级 API（全局热键 + 音频输出 + 可选的模拟按键）。原理上等同于"语音变声器/声卡跳线"，是系统层面的事。但任何工具都有使用风险，尤其 `ptt_key` 自动按键属于模拟输入，**建议用默认的虚拟声卡方案（不开自动 PTT）**，最稳。

**Q：队友听到的声音有延迟/断续？**
MF 播放走系统音频管线，一般延迟可忽略。若卡顿，检查文件是不是超高码率（转成 44100Hz 立体声 mp3 最稳），以及虚拟声卡采样率是否和游戏一致。

**Q：为什么自己也能听到声音？**
因为你没开虚拟声卡方案：`device` 留空时声音走默认设备（音箱）。改成 `device = CABLE` 后声音只进虚拟声卡。

## 十、进阶方向（可以自己扩展）

- **托盘图标**：把控制台换成托盘程序（`Shell_NotifyIcon`），不占任务栏。
- **音量控制**：用 `IMFAudioStreamVolume` 给不同声音设不同音量。
- **播放队列/连招**：一次热键连播多段声音（用 `std::queue`）。
- **开机自启**：注册表 `Run` 键加一行。
- **可视化配置界面**：做个简单的 Win32 对话框，代替手写 config.txt。

---

*本项目是教学向的完整可运行示例：热键、消息循环、配置解析、COM/MF 播放、虚拟声卡路由，覆盖了 Windows 桌面编程的常见骨架。祝你整活愉快。*
