// 播放模块实现：Windows Media Foundation（MF）
//
// 思路（也是“原理”的一部分，教学时对照着看）：
//   MF 把“播放一个文件”抽象成一条【拓扑】：
//       媒体源节点（Source） --连接--> 输出节点（Audio Renderer）
//   媒体源负责解码文件，Audio Renderer 负责把声音交给某个音频输出设备
//   （我们在这里指定“虚拟声卡”的设备 ID，声音就只进虚拟声卡，不进音箱）。
//   一个“会话”（IMFMediaSession）负责调度整条拓扑、控制播放/停止。

#include "player.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <objbase.h>
#include <guiddef.h>
#include <wchar.h>
#include <cwctype>
#include <mutex>
#include <string>

// ================= 兼容性适配层 =================
// MinGW 与微软 SDK 的少数命名/取值差异，在这里统一抹平。

// 1) 设备友好名称属性键：优先用官方头文件；缺失时手动定义
#if defined(__has_include)
#  if __has_include(<functiondiscoverykeys_devpkey.h>)
#    include <functiondiscoverykeys_devpkey.h>
#    define HAS_DEV_PKEY_HEADER 1
#  endif
#endif
#ifndef HAS_DEV_PKEY_HEADER
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20,
                   0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);
#endif

// 2) 拓扑节点类型：官方枚举为 OUTPUT_NODE=0 / SOURCESTREAM_NODE=1 /
//    TRANSFORM_NODE=2 / TEE_NODE=3（MinGW 与微软 SDK 同名同值，无需适配）

// 3) 音频渲染器“目标设备 ID”属性：MinGW 用的是旧名（指向同一 GUID）
#if defined(__MINGW32__)
#define MFAUDIO_RENDER_ATTRIBUTE_ENDPOINT_ID MF_AUDIO_RENDERER_ATTRIBUTE_ENDPOINT_ID
#endif

// 4) 设备枚举用的两个 GUID：MinGW 头文件只有声明、导入库缺定义，这里补上实体。
//    （MSVC 由 uuid.lib 提供定义，不能重复定义，所以只对 MinGW 生效）
#ifdef __MINGW32__
EXTERN_C const CLSID CLSID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
EXTERN_C const IID IID_IMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
#endif

// 5) MinGW 缺少 MFCreateMediaSourceFromURL：用“源解析器”等价实现
static HRESULT CreateSourceFromURL(const wchar_t* url, IMFMediaSource** outSource) {
    IMFSourceResolver* resolver = nullptr;
    HRESULT hr = MFCreateSourceResolver(&resolver);
    if (FAILED(hr)) return hr;
    MF_OBJECT_TYPE type = MF_OBJECT_INVALID;
    IUnknown* obj = nullptr;
    hr = resolver->CreateObjectFromURL(url, MF_RESOLUTION_MEDIASOURCE, nullptr,
                                       &type, &obj);
    resolver->Release();
    if (FAILED(hr)) return hr;
    hr = obj->QueryInterface(IID_IMFMediaSource, (void**)outSource);
    obj->Release();
    return hr;
}

namespace {

IMFMediaSession* g_session   = nullptr;  // 媒体会话：负责调度播放
IMFMediaSource*  g_source    = nullptr;  // 当前/最近一次播放的媒体源
IMFTopology*     g_topology  = nullptr;  // 当前/最近一次播放的拓扑
std::wstring     g_endpointId;           // 目标输出设备的 ID（空 = 系统默认）
std::mutex       g_mtx;                  // 保护上面的全局状态
bool             g_initialized = false;

// 把 HRESULT 转成可读的错误文本
std::wstring HrText(HRESULT hr, const wchar_t* what) {
    wchar_t buf[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, hr, 0, buf, 512, nullptr);
    wchar_t out[640];
    swprintf(out, 640, L"%s 失败 (0x%08X): %s", what, (unsigned)hr,
             buf[0] ? buf : L"未知错误");
    return out;
}

// 按名称子串匹配一个已启用的输出设备，得到它的设备 ID。
// 设备 ID 是 Windows 给每个音频端点分配的唯一标识（如
// {0.0.0.00000000}.{xxxx-...}），MF 靠它把声音定向到指定设备。
bool FindOutputDevice(const std::wstring& match, std::wstring& id, std::wstring& name) {
    if (match.empty()) return true;  // 空匹配 = 用系统默认设备

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr)) return false;

    IMMDeviceCollection* coll = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll);
    bool found = false;

    std::wstring m = match;
    for (auto& c : m) c = (wchar_t)towlower(c);

    if (SUCCEEDED(hr) && coll) {
        UINT count = 0;
        coll->GetCount(&count);
        for (UINT i = 0; i < count && !found; i++) {
            IMMDevice* dev = nullptr;
            if (FAILED(coll->Item(i, &dev))) continue;

            LPWSTR idStr = nullptr;
            dev->GetId(&idStr);

            IPropertyStore* store = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store))) {
                PROPVARIANT var;
                var.vt = VT_EMPTY;
                if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &var)) &&
                    var.vt == VT_LPWSTR && var.pwszVal) {
                    std::wstring nm = var.pwszVal;
                    std::wstring low = nm;
                    for (auto& c : low) c = (wchar_t)towlower(c);
                    if (low.find(m) != std::wstring::npos) {
                        found = true;
                        name = nm;
                        id = idStr ? idStr : L"";
                    }
                }
                PropVariantClear(&var);
                store->Release();
            }
            if (idStr) CoTaskMemFree(idStr);
            dev->Release();
        }
        coll->Release();
    }
    if (enumerator) enumerator->Release();
    return found;
}

// 把媒体源接进一条拓扑：源节点 -> 音频渲染器节点（指定输出设备）
HRESULT BuildTopology(IMFMediaSource* source, IMFPresentationDescriptor* pd,
                      const std::wstring& endpointId, IMFTopology** outTopo) {
    IMFTopology* topo = nullptr;
    HRESULT hr = MFCreateTopology(&topo);
    if (FAILED(hr)) return hr;

    DWORD count = 0;
    hr = pd->GetStreamDescriptorCount(&count);

    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++) {
        BOOL selected = FALSE;
        IMFStreamDescriptor* sd = nullptr;
        hr = pd->GetStreamDescriptorByIndex(i, &selected, &sd);
        if (FAILED(hr)) break;

        // 源节点：告诉拓扑“从哪个媒体源、哪个流取数据”
        IMFTopologyNode* srcNode = nullptr;
        hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &srcNode);
        if (FAILED(hr)) { sd->Release(); break; }
        srcNode->SetUnknown(MF_TOPONODE_SOURCE, source);
        srcNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pd);
        srcNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, sd);

        // 输出节点：音频渲染器（可指定输出设备 ID）
        IMFActivate* renderer = nullptr;
        hr = MFCreateAudioRendererActivate(&renderer);
        if (SUCCEEDED(hr) && !endpointId.empty())
            hr = renderer->SetString(MFAUDIO_RENDER_ATTRIBUTE_ENDPOINT_ID,
                                     endpointId.c_str());

        IMFTopologyNode* outNode = nullptr;
        if (SUCCEEDED(hr)) hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &outNode);
        if (SUCCEEDED(hr)) hr = outNode->SetObject(renderer);
        if (SUCCEEDED(hr)) hr = topo->AddNode(outNode);
        if (SUCCEEDED(hr)) hr = topo->AddNode(srcNode);
        if (SUCCEEDED(hr)) hr = srcNode->ConnectOutput(0, outNode, 0);

        if (sd) sd->Release();
        if (srcNode) srcNode->Release();
        if (renderer) renderer->Release();
        if (outNode) outNode->Release();
        break;  // 音频文件一般只有一个流，取第一个即可
    }

    if (FAILED(hr)) { topo->Release(); return hr; }
    *outTopo = topo;
    return S_OK;
}

// 停止当前播放并释放整个会话（必须在持有 g_mtx 时调用）
//
// 调试坑（本机实测，2026/10/5）：Media Foundation 的会话在播放结束后，
// ClearTopologies / 旧拓扑的清理是【异步】的。如果旧拓扑/旧源还在被会话
// 引用时就直接 Shutdown 源，会话后续会报 MF_E_SHUTDOWN (0xC00D3E85)，
// 表现就是"第一次播放有声音、之后日志正常但没有声音"。
// 正确做法：把旧会话整体 Shutdown（立即失效，不再异步引用任何东西），
// 下一次播放时新建一个全新会话。
void StopLocked() {
    if (g_session) {
        g_session->Stop();
        g_session->Shutdown();      // 会话立即失效
        g_session->Release();
        g_session = nullptr;
    }
    if (g_source) { g_source->Shutdown(); g_source->Release(); g_source = nullptr; }
    if (g_topology) { g_topology->Release(); g_topology = nullptr; }
}

} // namespace

bool PlayerInitialize(const std::wstring& deviceMatch, std::wstring& err,
                      std::wstring& usedDevice) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_initialized) return true;

    // COM 与 MF 都要先初始化（MFStartup 内部也会初始化 COM）
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { err = HrText(hr, L"MFStartup"); return false; }

    g_endpointId.clear();
    if (!deviceMatch.empty()) {
        if (FindOutputDevice(deviceMatch, g_endpointId, usedDevice)) {
            if (usedDevice.empty()) usedDevice = L"（匹配成功但设备名为空）";
        } else {
            usedDevice.clear();
            err = L"未找到名称包含 \"" + deviceMatch + L"\" 的输出设备，将使用系统默认设备（如果装了 VB-CABLE，请检查设备是否启用）";
        }
    }

    // 注意：媒体会话不在初始化时创建，改为每次播放新建一个
    // （见 PlayerPlayFile 与 StopLocked 的注释：复用旧会话会导致二次播放无声）
    g_initialized = true;
    return true;
}

bool PlayerPlayFile(const std::wstring& path, std::wstring& err, DWORD& durationMs) {
    std::lock_guard<std::mutex> lk(g_mtx);
    durationMs = 0;
    if (!g_initialized) { err = L"播放模块未初始化"; return false; }

    StopLocked();  // 停掉上一个会话并释放；下面为本次播放新建一个全新会话

    HRESULT hr = MFCreateMediaSession(nullptr, &g_session);
    if (FAILED(hr)) { err = HrText(hr, L"MFCreateMediaSession"); return false; }

    // 1. 由文件路径创建媒体源（自动选择系统里的解码器）
    IMFMediaSource* source = nullptr;
    hr = CreateSourceFromURL(path.c_str(), &source);
    if (FAILED(hr)) { err = HrText(hr, L"打开音频文件"); return false; }

    // 2. 拿到“演示描述符”，读出文件时长（100ns 单位）
    IMFPresentationDescriptor* pd = nullptr;
    hr = source->CreatePresentationDescriptor(&pd);
    if (FAILED(hr)) { source->Release(); err = HrText(hr, L"创建演示描述符"); return false; }
    UINT64 dur100ns = MFGetAttributeUINT64(pd, MF_PD_DURATION, 0);
    durationMs = (DWORD)(dur100ns / 10000);

    // 3. 构建拓扑（源 -> 音频渲染器，定向到 g_endpointId）
    IMFTopology* topo = nullptr;
    hr = BuildTopology(source, pd, g_endpointId, &topo);
    pd->Release();
    if (FAILED(hr)) { source->Release(); err = HrText(hr, L"构建播放拓扑"); return false; }

    // 4. 把拓扑交给会话，开始播放
    hr = g_session->SetTopology(0, topo);
    if (FAILED(hr)) {
        topo->Release();
        source->Release();
        err = HrText(hr, L"SetTopology");
        return false;
    }
    // 会话内部会引用这份拓扑和源，我们保留引用直到下次播放/退出
    g_source = source;
    g_topology = topo;

    // 实测：Start 必须带有效的时间格式 GUID 和 PROPVARIANT（NULL 会报 E_POINTER）
    PROPVARIANT var;
    var.vt = VT_EMPTY;
    hr = g_session->Start(&GUID_NULL, &var);
    if (FAILED(hr)) { err = HrText(hr, L"Start"); return false; }
    return true;
}

void PlayerStop() {
    std::lock_guard<std::mutex> lk(g_mtx);
    StopLocked();
}

void PlayerShutdown() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_initialized) return;
    StopLocked();  // 会话已在其中 Shutdown + Release
    MFShutdown();
    g_initialized = false;
}
