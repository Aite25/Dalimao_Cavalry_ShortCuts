// Cavalry_ShortCut.cpp — Cavalry 径向热盒 v6
//
// 参考 AE 插件 DalimaoShortcuts 的热盒：
//   · 径向 8 扇区（N/NE/E/SE/S/SW/W/NW）+ 中心圆
//   · 鼠标移出中心 → 扇区高亮，中心画蓝色弧线
//   · 扇区热键（1~8 或自定义字母）+ 搜索菜单（Tab / 直接打字）+ 设置菜单（改绑定）
//
// 数据：
//   Cavalry_ShortCut.ini   → 呼出快捷键等
//   Cavalry_ShortCut.json  → 8 个扇区的绑定（脚本文件 或 添加图层）
//   actions/*.js → 脚本动作本体（点的时候才读 → 热更新）
//
// 触发：Cavalry_ShortCut.ini 里的 hotkey（默认 Alt+Q）；Cavalry 脚本可用 Cavalry_ShortCut.exe --show。

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winhttp.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <imm.h>
#include <commctrl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <cmath>
#include <vector>
#include <ole2.h>
#include <oleauto.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "imm32.lib")

// ---------------------------------------------------------------- 常量
static const int kBridgePort = 6769;
static int g_winW = 640;
static int g_winH = 560;
static const int kPadX = 180;   // 透明输入区左右扩展
static const int kPadY = 70;    // 透明输入区上下扩展

static const COLORREF kBg          = RGB(0, 0, 0);        // 色键透明
static const COLORREF kSectorFill  = RGB(35, 35, 35);
static const COLORREF kSectorSel    = RGB(77, 77, 77);
static const COLORREF kSectorBorder = RGB(95, 95, 95);
static const COLORREF kText         = RGB(255, 255, 255);
static const COLORREF kTextDim      = RGB(200, 200, 200);
static const COLORREF kCenterFill   = RGB(38, 38, 38);
static const COLORREF kCenterBorder = RGB(31, 31, 31);
static const COLORREF kArcColor     = RGB(56, 181, 119);
static const COLORREF kPanelFill    = RGB(30, 30, 32);
static const COLORREF kPanelBorder  = RGB(70, 70, 74);
static const COLORREF kRowSel       = RGB(44, 132, 88);

static const int kCenterR  = 28;
static const int kRectW    = 150;
static const int kRectH    = 28;
static const int kOffVert  = 118;
static const int kOffHoriz = 172;
static const int kOffDiagX = 105;
static const int kOffDiagY = 68;

static HWND   g_main = nullptr;
static HBRUSH g_bgBrush = nullptr;

// ---------------------------------------------------------------- 状态
struct Slot {
    std::wstring name;    // 显示名
    std::wstring kind;    // "script" | "layer"
    std::wstring value;   // 文件名 或 图层类型
    std::wstring key;     // 扇区热键（单个字符，可空）
    bool used = false;
};
static Slot g_slots[8];          // 当前激活的一组
static Slot g_store[16][8];      // [box*4+mod][slot]
static int  g_box = 0;           // 0..3（热盒 1..4）
static int  g_mod = 0;
static bool g_ctrlUsed = false;   // Ctrl 是否被当作组合键使用
static bool g_binding = false;


struct Item {
    std::wstring name;
    std::wstring kind;
    std::wstring value;
    std::wstring cat;      // 脚本 / 形状 / 行为 / 工具 / 效果
    std::wstring key;      // 展示用提示（如快捷键）
};
static Item g_bindItem;            // 待绑定的搜索项
static std::vector<std::wstring> g_pinned;   // 置顶项 key = kind + "|" + value
static std::vector<std::wstring> g_opsJs;   // 操作项的 JS（按 index 引用，避免存进 json 被截断）
static std::vector<std::wstring> g_opsNames;
static std::vector<std::wstring> g_opsHints;   // 对应的原生快捷键
static std::vector<std::wstring> g_opsFiles;   // 对应的脚本文件名（放在 Cavalry Scripts 目录）
static bool g_opsUseKeys = false;   // 默认走 MCP（Cavalry 不响应模拟按键）
static std::vector<Item> g_index;      // 搜索结果来源

enum Mode { M_PIE, M_SEARCH, M_SETTINGS };
static Mode g_mode = M_PIE;
static std::wstring g_query;
static int g_sel = 0;
static int g_bindSlot = -1;            // 设置模式：正在给哪个扇区选绑定
static int g_hover = -1;
static std::wstring g_hotkeySpec = L"Alt+Q";
static UINT g_hotkeyMods = MOD_ALT | MOD_NOREPEAT;
static UINT g_hotkeyVk = 'Q';
static int g_trayTick = 0;
static bool g_inMenu = false;
static bool g_imeOn = true;   // 输入框是否启用输入法（Cavalry_ShortCut.ini: ime=off 可关）
static bool g_dragging = false;
static bool g_selAll = false;
static bool g_swallowChar = false;   // 捕获快捷键后吞掉随之而来的 WM_CHAR
static void SyncEditText();
static void UpdateResults();
static POINT g_dragOffset = {0, 0};

static std::vector<Item> g_results;
static RECT SetInputRect();
static void SyncEditFromMode();
static void UpdateScrollInfo();
static int g_tab = 0;
static const wchar_t* kTabs[] = { L"全部", L"脚本", L"形状", L"行为", L"工具", L"效果", L"操作" };
static const int kTabCount = 7;
static int g_tabOrder[kTabCount] = { 0, 1, 2, 3, 4, 5, 6 };   // 显示位置 -> 页签 id
static int g_tabDragFrom = -1;   // 拖拽起点（位置）
static bool g_captureHotkey = false;   // 设置页正在捕获快捷键
static int g_searchRows = 0;   // 0 = 自动（按窗口高度算）
static int g_selSearch = 0;
static HWND g_edit = nullptr;
static HWND g_editHost = nullptr;
static HWND g_scrollbar = nullptr;
static int g_scroll = 0;
static int g_hoverRow = -1;
static int g_maxScroll = 0;
static bool g_thumbDrag = false;
static bool g_resizing = false;
static int g_resizeEdges = 0;   // 1=右边 2=下边 3=右下
static POINT g_resizeStart = {0,0};
static int g_resizeStartW = 0, g_resizeStartH = 0;
static int g_dragStartY = 0;
static int g_dragStartScroll = 0;
static bool g_hoverFolder = false;
static bool g_hoverGear = false;

// ---------------------------------------------------------------- 基础
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L'\\');
    return p == std::wstring::npos ? std::wstring(L".") : s.substr(0, p);
}

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string EscapeJson(const std::string& in) {
    std::string o;
    for (unsigned char c : in) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}

static std::string ReadFileUtf8(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) fread(&s[0], 1, n, f);
    fclose(f);
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
    return s;
}

static void WriteFileUtf8(const std::wstring& path, const std::string& data) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}


static void Log(const std::string& s);
static void FillRound(HDC dc, RECT r, int radius, COLORREF fill, COLORREF border);
static HFONT MakeFont(int px, bool bold = false);
static void SaveActive();
static void LoadActive();

// ---------------------------------------------------------------- 顶部页签（热盒 1-4 / 修饰键）
static RECT TabBoxRect(int i) {   // 第一行：1..4
    RECT r;
    r.left = 24 + i * 58;   // 与修饰键行同宽同间距
    r.top = 24;
    r.right = r.left + 58;
    r.bottom = r.top + 24;
    return r;
}

static RECT TabModRect(int i) {   // 第二行：无 / Ctrl / Shift / Alt
    RECT r;
    r.left = 24 + i * 58;
    r.top = 54;
    r.right = r.left + 58;
    r.bottom = r.top + 24;
    return r;
}

static void SwitchSet(int box, int mod) {
    SaveActive();
    g_box = (box >= 0 && box < 4) ? box : g_box;
    g_mod = (mod >= 0 && mod < 4) ? mod : g_mod;
    LoadActive();
    Log("set box=" + std::to_string(g_box + 1) + " mod=" + std::to_string(g_mod));
}

static void DrawPieTabs(HDC dc) {
    const wchar_t* boxLabels[4] = { L"1", L"2", L"3", L"4" };
    const wchar_t* modLabels[4] = { L"无", L"Ctrl", L"Shift", L"Alt" };

    for (int i = 0; i < 4; ++i) {
        RECT r = TabBoxRect(i);
        bool on = (i == g_box);
        { HBRUSH tb = CreateSolidBrush(on ? RGB(52, 54, 60) : RGB(36, 36, 40)); FillRect(dc, &r, tb); DeleteObject(tb);
          HPEN tp2 = CreatePen(PS_SOLID, 1, on ? RGB(52, 54, 60) : RGB(36, 36, 40)); HGDIOBJ ob2 = SelectObject(dc, tp2), obb2 = SelectObject(dc, GetStockObject(NULL_BRUSH));
          Rectangle(dc, r.left, r.top, r.right, r.bottom); SelectObject(dc, obb2); SelectObject(dc, ob2); DeleteObject(tp2); }
        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, on ? RGB(235, 240, 250) : RGB(140, 140, 146));
        HFONT f = MakeFont(13, on);
        HGDIOBJ of = SelectObject(dc, f);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        DrawTextW(dc, boxLabels[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(dc, of);
        DeleteObject(f);
        if (on) { RECT u = { r.left + 8, r.bottom - 2, r.right - 8, r.bottom }; HBRUSH ub = CreateSolidBrush(kRowSel); FillRect(dc, &u, ub); DeleteObject(ub); }
    }
    for (int i = 0; i < 4; ++i) {
        RECT r = TabModRect(i);
        bool on = (i == g_mod);
        { HBRUSH tb = CreateSolidBrush(on ? RGB(52, 54, 60) : RGB(36, 36, 40)); FillRect(dc, &r, tb); DeleteObject(tb);
          HPEN tp2 = CreatePen(PS_SOLID, 1, on ? RGB(52, 54, 60) : RGB(36, 36, 40)); HGDIOBJ ob2 = SelectObject(dc, tp2), obb2 = SelectObject(dc, GetStockObject(NULL_BRUSH));
          Rectangle(dc, r.left, r.top, r.right, r.bottom); SelectObject(dc, obb2); SelectObject(dc, ob2); DeleteObject(tp2); }
        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, on ? RGB(235, 240, 250) : RGB(140, 140, 146));
        HFONT f = MakeFont(13, on);
        HGDIOBJ of = SelectObject(dc, f);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        DrawTextW(dc, modLabels[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(dc, of);
        DeleteObject(f);
        if (on) { RECT u = { r.left + 4, r.bottom - 2, r.right - 4, r.bottom }; HBRUSH ub = CreateSolidBrush(kRowSel); FillRect(dc, &u, ub); DeleteObject(ub); }
    }
}
// ---------------------------------------------------------------- 图标（Add Layers 同款）
static ULONG_PTR g_gdiplusToken = 0;
static std::wstring g_iconDir;
static std::map<std::wstring, Gdiplus::Image*> g_iconCache;

static void Log(const std::string& s);

static void InitIcons() {
    GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);

    const wchar_t* cands[] = {
        L"C:\\Program Files\\Cavalry\\assets\\Icons\\nodes",
        L"E:\\Program Files\\Cavalry\\assets\\Icons\\nodes",
        L"D:\\Program Files\\Cavalry\\assets\\Icons\\nodes",
    };
    for (auto c : cands) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { g_iconDir = c; break; }
    }
    if (g_iconDir.empty()) Log("icons: not found");
    else Log("icons: " + Narrow(g_iconDir));
}

static Gdiplus::Image* GetIcon(const std::wstring& type) {
    if (g_iconDir.empty() || type.empty()) return nullptr;
    auto it = g_iconCache.find(type);
    if (it != g_iconCache.end()) return it->second;

    std::wstring path = g_iconDir + L"\\" + type + L".png";
    Gdiplus::Image* img = nullptr;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        img = Gdiplus::Image::FromFile(path.c_str());
        if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; img = nullptr; }
    }
    g_iconCache[type] = img;
    return img;
}

static void DrawIcon(HDC dc, const std::wstring& type, RECT box) {
    Gdiplus::Image* img = GetIcon(type);
    if (!img) return;
        int dcSaved = SaveDC(dc);   // GDI+ 会改 DC 状态，用完恢复
    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(img, box.left, box.top, box.right - box.left, box.bottom - box.top);
}

static void Log(const std::string& s) {
    std::wstring path = ExeDir() + L"\\Cavalry_ShortCut.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"ab") == 0 && f) {
        SYSTEMTIME st; GetLocalTime(&st);
        char ts[48];
        sprintf_s(ts, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        fwrite(ts, 1, strlen(ts), f);
        fwrite(s.data(), 1, s.size(), f);
        fwrite("\n", 1, 1, f);
        fclose(f);
    }
}

// ---------------------------------------------------------------- HTTP → bridge
static HINTERNET g_httpSession = nullptr;
static HINTERNET g_httpConnect = nullptr;

static bool HttpCall(const wchar_t* method, const std::wstring& path, const std::string& body,
                     std::string& out, DWORD& status) {
    out.clear();
    status = 0;

    if (!g_httpSession) {
        g_httpSession = WinHttpOpen(L"cavalry-shortcut/0.6", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_httpSession) return false;
        WinHttpSetTimeouts(g_httpSession, 3000, 3000, 8000, 60000);
    }
    if (!g_httpConnect) {
        g_httpConnect = WinHttpConnect(g_httpSession, L"127.0.0.1", (INTERNET_PORT)kBridgePort, 0);
        if (!g_httpConnect) return false;
    }

    HINTERNET request = WinHttpOpenRequest(g_httpConnect, method, path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        WinHttpCloseHandle(g_httpConnect);
        g_httpConnect = nullptr;
        return false;
    }

    const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(request, headers, body.empty() ? 0 : (DWORD)-1L,
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                 (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (ok) {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        status = code;
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) break;
            if (!avail) break;
            std::string chunk(avail, '\0');
            DWORD read = 0;
            WinHttpReadData(request, &chunk[0], avail, &read);
            chunk.resize(read);
            out += chunk;
        } while (avail > 0);
    }

    WinHttpCloseHandle(request);
    return ok != 0;
}


static bool BridgeAlive() {
    std::string out;
    DWORD st = 0;
    if (!HttpCall(L"GET", L"/status", "", out, st)) return false;
    return st == 200 && out.find("\"initialized\":true") != std::string::npos;
}

static bool StartBridge() {
    std::vector<std::wstring> candidates = {
        ExeDir() + L"\\cavalry_bridge.mjs",
        ExeDir() + L"\\..\\cavalry_bridge.mjs",
        ExeDir() + L"\\..\\bridge\\cavalry_bridge.mjs",
    };
    std::wstring script;
    for (auto& c : candidates) {
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) { script = c; break; }
    }
    if (script.empty()) return false;

    std::wstring cmd = L"node \"" + script + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    for (int i = 0; i < 40; ++i) {
        Sleep(500);
        if (BridgeAlive()) return true;
    }
    return false;
}

static bool g_preambleDone = false;

static void EnsurePreamble() {
    if (g_preambleDone) return;
    std::string out; DWORD st = 0;
    if (HttpCall(L"POST", L"/call", "{\"name\":\"read_preamble\",\"args\":{}}", out, st)) {
        g_preambleDone = true;
    }
}

static void RunScriptText(const std::string& script) {
    if (script.size() < 400) Log("script: " + script);
    if (script.empty()) return;
    std::string body = "{\"name\":\"execute_script\",\"args\":{\"script\":\"" + EscapeJson(script) + "\"}}";
    std::string out;
    DWORD st = 0;
    EnsurePreamble();
    if (!HttpCall(L"POST", L"/call", body, out, st)) Log("bridge call failed");
    else if (out.find("Read the preamble") != std::string::npos) {
        g_preambleDone = false;
        EnsurePreamble();
        HttpCall(L"POST", L"/call", body, out, st);
        Log("ran script (retry after preamble) -> " + std::string(out.find("isError\":false") != std::string::npos ? "ok" : "error"));
        return;
    }
    else Log("ran script (" + std::to_string(script.size()) + " bytes) -> " +
             (out.find("\"isError\":false") != std::string::npos ? "ok" : "error") + " | " + out.substr(0, out.size() < 120 ? out.size() : 120));
}

static std::wstring Trim(const std::wstring& s);
static bool IsCavalryWindow(HWND h);
static void HideHotbox(HWND hwnd);
static bool AnyCavalryVisible();
static std::string DescribeWindow(HWND h);
static HWND g_cavalryWnd = nullptr;
// ---------------------------------------------------------------- 操作（快捷键）
static UINT VkFromToken(const std::wstring& t) {
    if (t.empty()) return 0;
    std::wstring low = ToLower(t);
    if (low == L"alt") return VK_MENU;
    if (low == L"ctrl") return VK_CONTROL;
    if (low == L"shift") return VK_SHIFT;
    if (low == L"space") return VK_SPACE;
    if (low == L"home") return VK_HOME;
    if (low == L"end") return VK_END;
    if (low == L"left") return VK_LEFT;
    if (low == L"right") return VK_RIGHT;
    if (low == L"up") return VK_UP;
    if (low == L"down") return VK_DOWN;
    if (low == L"enter") return VK_RETURN;
    if (low == L"delete") return VK_DELETE;
    if (t == L"[") return VK_OEM_4;
    if (t == L"]") return VK_OEM_6;
    if (t == L",") return VK_OEM_COMMA;
    if (t == L".") return VK_OEM_PERIOD;
    if (t == L"-") return VK_OEM_MINUS;
    if (t == L"=") return VK_OEM_PLUS;
    if (low.size() >= 2 && low[0] == L'f') { int n = _wtoi(low.c_str() + 1); if (n >= 1 && n <= 24) return VK_F1 + n - 1; }
    if (t.size() == 1) { wchar_t c = (wchar_t)towupper(t[0]); if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return (UINT)c; }
    return 0;
}

static void SendCombo(const std::wstring& spec) {
    std::vector<UINT> keys;
    size_t start = 0;
    while (start <= spec.size()) {
        size_t plus = spec.find(L'+', start);
        std::wstring tok = Trim(plus == std::wstring::npos ? spec.substr(start) : spec.substr(start, plus - start));
        UINT vk = VkFromToken(tok);
        if (vk) keys.push_back(vk);
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    if (keys.empty()) return;

    // 直接投递到 Cavalry 的焦点窗口（绕过系统输入队列）
    HWND cav = g_cavalryWnd;
    if (!cav || !IsWindow(cav)) { HWND fg = GetForegroundWindow(); if (IsCavalryWindow(fg)) cav = fg; }
    if (!cav) return;
    DWORD tid = GetWindowThreadProcessId(cav, nullptr);
    GUITHREADINFO gi = {}; gi.cbSize = sizeof(gi);
    GetGUIThreadInfo(tid, &gi);
    HWND target = gi.hwndFocus ? gi.hwndFocus : cav;

    bool sys = false;
    for (auto vk : keys) if (vk == VK_MENU) sys = true;

    for (auto vk : keys) {
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LPARAM lp = (LPARAM)((scan & 0xFF) << 16) | 1;
        PostMessageW(target, sys ? WM_SYSKEYDOWN : WM_KEYDOWN, (WPARAM)vk, lp);
        Sleep(25);
    }
    Sleep(30);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        UINT scan = MapVirtualKeyW(*it, MAPVK_VK_TO_VSC);
        LPARAM lp = (LPARAM)((scan & 0xFF) << 16) | 1 | 0xC0000000;
        PostMessageW(target, sys ? WM_SYSKEYUP : WM_KEYUP, (WPARAM)*it, lp);
        Sleep(25);
    }
    Log("sent combo " + Narrow(spec) + " to " + DescribeWindow(target));
}

static void LoadOps() {
    // 扫描 exe 目录下的 ops\*.js；脚本头部用注释声明显示名与快捷键：
    //   // name: 整体移动：入点对齐播放头
    //   // key:  Alt+[
    // 新增脚本 = 放进 ops\ 后，下次呼出热盒立即出现
    g_opsNames.clear(); g_opsHints.clear(); g_opsFiles.clear(); g_opsJs.clear();
    std::wstring dir = ExeDir() + L"\\ops";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*.js").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());

    for (auto& fname : files) {
        std::string raw = ReadFileUtf8(dir + L"\\" + fname);
        std::wstring name = fname.substr(0, fname.find_last_of(L'.'));
        std::wstring key;
        {
            size_t p = 0;
            int cnt = 0;
            while (p < raw.size() && cnt < 6) {
                size_t e = raw.find((char)10, p);
                if (e == std::string::npos) e = raw.size();
                std::string ln = raw.substr(p, e - p);
                p = e + 1; ++cnt;
                std::wstring w = Trim(Widen(ln));
                if (w.rfind(L"//", 0) != 0) continue;
                std::wstring body = Trim(w.substr(2));
                if (body.rfind(L"name:", 0) == 0) name = Trim(body.substr(5));
                else if (body.rfind(L"key:", 0) == 0) key = Trim(body.substr(4));
            }
        }
        Item it;
        it.name = name;
        it.kind = L"ops";
        it.value = L"op:" + std::to_wstring(g_opsFiles.size());
        it.key = key;
        it.cat = L"操作";
        g_index.push_back(it);
        g_opsNames.push_back(name);
        g_opsHints.push_back(key);
        g_opsFiles.push_back(fname);
    }
}


static void RunItem(const Item& it) {
    HideHotbox(g_main ? g_main : GetActiveWindow());   // 先收起，立刻给反馈
    Item item = it;
    std::thread([item]() {
        if (item.kind == L"script") {
            RunScriptText(ReadFileUtf8(ExeDir() + L"\\actions\\" + item.value));
        } else if (item.kind == L"layer") {
            RunScriptText("(function(){var id=api.create(\"" + Narrow(item.value) + "\"); if(id) api.select([id]);})();");
        } else if (item.kind == L"ops") {
            DWORD t0 = GetTickCount();
            std::string v = Narrow(item.value);
            size_t idx = (v.rfind("op:", 0) == 0) ? (size_t)atoi(v.c_str() + 3) : (size_t)-1;
            if (g_opsUseKeys && idx < g_opsHints.size()) {
                HWND cav = g_cavalryWnd;
                if (!cav || !IsWindow(cav)) { HWND fg = GetForegroundWindow(); if (IsCavalryWindow(fg)) cav = fg; }
                if (cav && IsWindow(cav)) {
                    HWND fgNow = GetForegroundWindow();
                    DWORD fgTid = fgNow ? GetWindowThreadProcessId(fgNow, nullptr) : 0;
                    DWORD cavTid = GetWindowThreadProcessId(cav, nullptr);
                    if (fgTid && cavTid && fgTid != cavTid) AttachThreadInput(fgTid, cavTid, TRUE);
                    BringWindowToTop(cav); SetForegroundWindow(cav);
                    if (fgTid && cavTid && fgTid != cavTid) AttachThreadInput(fgTid, cavTid, FALSE);
                    Sleep(60);
                }
                SendCombo(g_opsHints[idx]);
            } else if (idx != (size_t)-1) {
                if (idx < g_opsFiles.size()) {
                    std::string code = ReadFileUtf8(ExeDir() + L"\\ops\\" + g_opsFiles[idx]);
                    if (code.empty()) Log("op script missing: " + Narrow(g_opsFiles[idx]));
                    else RunScriptText(code);
                }
            } else { RunScriptText(v); }
            if (idx != (size_t)-1) Log("op took " + std::to_string(GetTickCount() - t0) + " ms");
        } else if (item.kind == L"keys") {
            SendCombo(item.value);
        }
    }).detach();
}

static RECT SetInputRect();
static void SyncEditFromMode();
static void UpdateScrollInfo();

// ---------------------------------------------------------------- 原生输入框
#define ID_EDIT 4001
#define ID_SCROLL 4002

static LRESULT CALLBACK EditSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    if (m == WM_CHAR && w < 32 && w != 8) return 0;   // 控制字符（保留 Backspace=8）
    if (m == WM_KEYDOWN) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && w == 'A') { g_selAll = true; SendMessageW(h, EM_SETSEL, (WPARAM)0, (LPARAM)-1); if (g_main) InvalidateRect(g_main, nullptr, TRUE); return 0; }
        if (ctrl && w >= '1' && w <= '6') { PostMessageW(g_main, WM_APP + 5, (WPARAM)(w - '0'), 0); return 0; }
        if (w == VK_TAB) { PostMessageW(g_main, WM_APP + 6, (WPARAM)(shift ? 1 : 0), 0); return 0; }
        if (w == VK_OEM_3) { PostMessageW(g_main, WM_APP + 7, 0, 0); return 0; }   // ` 执行
        if (w == VK_ESCAPE || w == VK_RETURN || w == VK_UP || w == VK_DOWN) {
            PostMessageW(g_main, WM_KEYDOWN, w, 0);
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}

static LRESULT CALLBACK EditHostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CTLCOLOREDIT) {
        SetTextColor((HDC)w, RGB(235, 235, 240));
        SetBkColor((HDC)w, RGB(22, 22, 24));
        static HBRUSH br = CreateSolidBrush(RGB(22, 22, 24));
        return (LRESULT)br;
    }
    if (m == WM_COMMAND && LOWORD(w) == ID_EDIT && HIWORD(w) == EN_CHANGE) {
        wchar_t buf[256] = {0};
        GetWindowTextW(g_edit, buf, 255);
        g_query = buf;
        g_selAll = false;
        g_sel = 0; g_scroll = 0;
        UpdateResults();
        if (g_main) InvalidateRect(g_main, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void CreateEdit(HWND parent, HINSTANCE inst) {
    // 原生 EDIT 放独立弹出窗口（主窗口是分层窗口，子窗口不会被绘制）
    WNDCLASSW hc = {};
    hc.lpfnWndProc = EditHostProc;
    hc.hInstance = inst;
    hc.hbrBackground = CreateSolidBrush(RGB(22, 22, 24));
    hc.lpszClassName = L"CavalryShortCutEditHost";
    RegisterClassW(&hc);
    g_editHost = CreateWindowExW(WS_EX_TOOLWINDOW, L"CavalryShortCutEditHost", L"",
                                 WS_POPUP | WS_CLIPCHILDREN, 0, 0, 10, 10,
                                 nullptr, nullptr, inst, nullptr);
    g_edit = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                             0, 0, 10, 10, g_editHost, (HMENU)ID_EDIT, inst, nullptr);
    if (!g_edit) return;
    HFONT f = MakeFont(19);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)f, TRUE);
    if (!g_imeOn) ImmAssociateContext(g_edit, nullptr);
    SetWindowSubclass(g_edit, EditSubclass, 1, 0);

    g_scrollbar = CreateWindowExW(0, L"SCROLLBAR", nullptr,
                                  WS_CHILD | SBS_VERT,
                                  0, 0, 10, 10, parent, (HMENU)ID_SCROLL, inst, nullptr);
}

static RECT InputRect();
static void SyncEditFromMode() {
    if (g_mode == M_SETTINGS) {   // 设置页：输入框放在右侧搜索框
        RECT sr = SetInputRect();
        RECT wr2; GetWindowRect(g_main, &wr2);
        int ew2 = sr.right - sr.left - 16, eh2 = sr.bottom - sr.top - 8;
        SetWindowPos(g_editHost, HWND_TOP, wr2.left + kPadX + sr.left + 8, wr2.top + kPadY + sr.top + 4,
                     ew2, eh2, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetWindowPos(g_edit, nullptr, 0, 0, ew2, eh2, SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        SetFocus(g_edit);
        return;
    }
    if (!g_edit || !g_editHost) return;
    if (g_mode == M_SEARCH) {
        RECT ir = InputRect();
        RECT wr; GetWindowRect(g_main, &wr);
        int ew = ir.right - ir.left - 16;
        int eh = ir.bottom - ir.top - 10;
        SetWindowPos(g_editHost, HWND_TOP, wr.left + kPadX + ir.left + 8, wr.top + kPadY + ir.top + 6,
                     ew, eh, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetWindowPos(g_edit, nullptr, 0, 0, ew, eh, SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        {
            RECT pr = { 40, 60, g_winW - 40, g_winH - 60 };
            SetWindowPos(g_scrollbar, HWND_TOP, kPadX + pr.right - 30, kPadY + pr.top + 110,
                         14, 7 * 38 - 4, SWP_NOZORDER);
            UpdateScrollInfo();
        }
        SetFocus(g_edit);
    } else {
        ShowWindow(g_editHost, SW_HIDE);
        ShowWindow(g_scrollbar, SW_HIDE);
    }
}

static void SyncEditText() {
    if (!g_edit) return;
    SetWindowTextW(g_edit, g_query.c_str());
}

static void SetClipboardText(const std::wstring& s) {
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* dst = GlobalLock(h);
        if (dst) { memcpy(dst, s.c_str(), bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    }
    CloseClipboard();
}

static std::wstring GetClipboardText() {
    std::wstring out;
    if (!OpenClipboard(g_main)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) { out = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    for (auto& c : out) if (c == L'\r' || c == L'\n') c = L' ';
    return out;
}
// ---------------------------------------------------------------- 索引
static std::wstring CategoryOf(const std::wstring& type, const std::wstring& kind) {
    if (kind == L"script") return L"脚本";
    std::wstring t = ToLower(type);
    if (t.find(L"filter") != std::wstring::npos || t.find(L"shader") != std::wstring::npos) return L"效果";
    if (t.size() > 5 && t.substr(t.size() - 5) == L"shape") return L"形状";
    if (t == L"basicshape" || t == L"rectangle" || t == L"ellipse" || t == L"group" ||
        t == L"null" || t == L"compnode" || t == L"line" || t == L"polygon" || t == L"star") return L"形状";
    const wchar_t* beh[] = { L"oscillator", L"noise", L"random", L"stagger", L"value", L"frame",
                             L"falloff", L"submesh", L"duplicator", L"numberrange", L"math",
                             L"step", L"range", L"remap" };
    for (auto b : beh) if (t == b || t.find(b) != std::wstring::npos) return L"行为";
    return L"工具";
}

static void LoadIndex() {
    g_index.clear();

    // 动作脚本
                    std::wstring dir = ExeDir() + L"\\actions";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*.js").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            Item it;
            it.name = name.substr(0, name.find_last_of(L'.'));
            it.kind = L"script";
            it.value = name;
            it.cat = L"脚本";
            g_index.push_back(it);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // 图层类型：读缓存文件（由 cavalry 一次性导出，280 个）
    std::string raw = ReadFileUtf8(ExeDir() + L"\\layer-types.txt");
    if (!raw.empty()) {
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t eol = raw.find('\n', pos);
            std::string line = raw.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = (eol == std::string::npos) ? raw.size() : eol + 1;
            while (!line.empty() && (line.back() == '\r')) line.pop_back();
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            Item it;
            it.name = Widen(line.substr(0, tab));
            it.value = Widen(line.substr(tab + 1));
            it.kind = L"layer";
            it.cat = CategoryOf(it.value, it.kind);
            if (!it.name.empty()) g_index.push_back(it);
        }
    }

    Log("index: " + std::to_string(g_index.size()) + " items");
}
// ---------------------------------------------------------------- 配置 Cavalry_ShortCut.json
static std::wstring JsonGetStr(const std::string& obj, const char* key) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return L"";
    p = obj.find('"', p + pat.size());
    if (p == std::string::npos) return L"";
    size_t e = p + 1;
    std::string s;
    while (e < obj.size() && obj[e] != '"') {
        if (obj[e] == '\\' && e + 1 < obj.size()) { s += obj[e + 1]; e += 2; continue; }
        s += obj[e++];
    }
    return Widen(s);
}

static void SaveSectors();
static void LoadActive() {
    int idx = g_box * 4 + g_mod;
    for (int i = 0; i < 8; ++i) g_slots[i] = g_store[idx][i];
}

static void SaveActive() {
    int idx = g_box * 4 + g_mod;
    for (int i = 0; i < 8; ++i) g_store[idx][i] = g_slots[i];
}

static const wchar_t* kDefaultKeys[8] = { L"W", L"E", L"D", L"C", L"S", L"Z", L"A", L"Q" };
static void LoadSectors() {
    for (int b = 0; b < 16; ++b) for (int i = 0; i < 8; ++i) g_store[b][i] = Slot();

    std::wstring path = ExeDir() + L"\\Cavalry_ShortCut.json";
    std::string raw = ReadFileUtf8(path);

    std::vector<std::string> segs;
    size_t pos = 0;
    while ((pos = raw.find("\"name\"", pos)) != std::string::npos) {
        size_t segEnd = raw.find("}", pos);
        segs.push_back(raw.substr(pos, segEnd == std::string::npos ? std::string::npos : segEnd - pos));
        pos = (segEnd == std::string::npos) ? raw.size() : segEnd;
    // 统一使用 WASD 套按键（空或 1~8 数字的旧值一律替换）
    for (int b = 0; b < 16; ++b) {
        for (int s = 0; s < 8; ++s) {
            Slot& sl2 = g_store[b][s];
            bool numeric = (sl2.key.size() == 1 && sl2.key[0] >= L'1' && sl2.key[0] <= L'8');
            if (sl2.key.empty() || numeric) sl2.key = kDefaultKeys[s];
        }
    }
    LoadActive();
    }

    int total = (int)segs.size();
    for (int i = 0; i < total && i < 128; ++i) {
        Slot s;
        s.name = JsonGetStr(segs[i], "name");
        s.kind = JsonGetStr(segs[i], "kind");
        s.value = JsonGetStr(segs[i], "value");
        s.key = JsonGetStr(segs[i], "key");
        s.used = !s.value.empty();
        if (s.kind.empty()) s.kind = L"script";
        if (s.kind == L"ops") {
            if (s.name == L"设入点（设为当前帧）") s.name = L"设入点";
            else if (s.name == L"设出点（设为当前帧）") s.name = L"设出点";
            else if (s.name == L"入点=当前帧（裁剪头部）") s.name = L"裁剪入点";
            else if (s.name == L"出点=当前帧（裁剪尾部）") s.name = L"裁剪出点";
            else if (s.name == L"跳到当前图层出点") s.name = L"跳到图层出点";
            else if (s.name == L"整体移动：入点对齐播放头（Move In）") s.name = L"整体入点对齐";
            else if (s.name == L"整体移动：出点对齐播放头（Move Out）") s.name = L"整体出点对齐";
            for (size_t k = 0; k < g_opsNames.size(); ++k) {
                if (g_opsNames[k] == s.name) { s.value = L"op:" + std::to_wstring(k); break; }
            }
        }
        g_store[i / 8][i % 8] = s;
    }
    if (total == 8) {   // 旧格式：复制到其余 15 组
        /* 各组独立：不复制默认绑定 */
    }

    if (total == 0) {   // 空配置：用 actions 目录生成默认绑定
                    std::wstring dir = ExeDir() + L"\\actions";
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((dir + L"\\*.js").c_str(), &fd);
        std::vector<std::pair<std::wstring, std::wstring>> found;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring nm = fd.cFileName;
                found.push_back({ nm.substr(0, nm.find_last_of(L'.')), nm });
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(found.begin(), found.end());
        for (size_t i = 0; i < found.size() && i < 8; ++i) {
            g_store[0][i].name = found[i].first;
            g_store[0][i].kind = L"script";
            g_store[0][i].value = found[i].second;
            g_store[0][i].key = std::to_wstring(i + 1);
            g_store[0][i].used = true;
        }
        /* 各组独立：不复制默认绑定 */
        SaveSectors();
    }
    LoadActive();
}

static void SaveSectors();

static void SaveSectors() {
    SaveActive();
    std::string out = "{\n  \"sets\": [\n";
    for (int b = 0; b < 16; ++b) {
        out += "    [\n";
        for (int i = 0; i < 8; ++i) {
            const Slot& s = g_store[b][i];
            out += "      {\"name\":\"" + EscapeJson(Narrow(s.name)) + "\", \"kind\":\"" + EscapeJson(Narrow(s.kind)) +
                   "\", \"value\":\"" + EscapeJson(Narrow(s.value)) + "\", \"key\":\"" + EscapeJson(Narrow(s.key)) + "\"}";
            out += (i < 7) ? ",\n" : "\n";
        }
        out += (b < 15) ? "    ],\n" : "    ]\n";
    }
    out += "  ]\n}\n";
    WriteFileUtf8(ExeDir() + L"\\Cavalry_ShortCut.json", out);
}

// ---------------------------------------------------------------- 搜索
static bool MatchScore(const std::wstring& name, const std::wstring& q) {
    if (q.empty()) return true;
    std::wstring n = ToLower(name), qq = ToLower(q);
    if (n.find(qq) != std::wstring::npos) return true;

    // 首字母匹配：dup → Duplicator 里的 d...u...p
    size_t qi = 0;
    for (size_t i = 0; i < n.size() && qi < qq.size(); ++i) {
        if (n[i] == qq[qi]) ++qi;
    }
    return qi == qq.size();
}

static void UpdateResults() {
    g_results.clear();
    for (auto& it : g_index) {
        if (g_tab > 0 && it.cat != kTabs[g_tab]) continue;
        if (!MatchScore(it.name, g_query) && !MatchScore(it.value, g_query)) continue;
        g_results.push_back(it);
        if (g_results.size() >= 400) break;
    }
    if (!g_pinned.empty()) {
        std::vector<Item> pref, rest;
        for (auto& it : g_results) {
            std::wstring key = it.kind + L"|" + it.value;
            bool pin = false;
            for (auto& k : g_pinned) if (k == key) { pin = true; break; }
            if (pin) pref.push_back(it); else rest.push_back(it);
        }
        g_results = pref;
        for (auto& it : rest) g_results.push_back(it);
    }
    if (g_mode == M_SETTINGS) {
        if (g_selSearch >= (int)g_results.size()) g_selSearch = 0;   // 设置页：不要动 g_sel（那是选中的扇区）
    } else if (g_sel >= (int)g_results.size()) {
        g_sel = 0;
    }
}

// ---------------------------------------------------------------- 几何
static int CenterX() { return g_winW / 2; }
static int CenterY() { return g_winH / 2; }

static RECT SectorRect(int index) {
    int cx = CenterX(), cy = CenterY();
    int ox = 0, oy = 0;
    switch (index) {
        case 0: oy = -kOffVert; break;
        case 1: ox = kOffDiagX; oy = -kOffDiagY; break;
        case 2: ox = kOffHoriz; break;
        case 3: ox = kOffDiagX; oy = kOffDiagY; break;
        case 4: oy = kOffVert; break;
        case 5: ox = -kOffDiagX; oy = kOffDiagY; break;
        case 6: ox = -kOffHoriz; break;
        case 7: ox = -kOffDiagX; oy = -kOffDiagY; break;
    }
    RECT r;
    r.left = cx + ox - kRectW / 2;
    r.top = cy + oy - kRectH / 2;
    r.right = r.left + kRectW;
    r.bottom = r.top + kRectH;
    return r;
}

static int HitTestPie(POINT p) {
    int dx = p.x - CenterX(), dy = CenterY() - p.y;   // 屏幕 Y 向下，转成数学坐标
    if (dx * dx + dy * dy <= kCenterR * kCenterR) return -2;   // 中心圆

    // 8 方向：任意角度都归到最近的一个扇区
    double ang = atan2((double)dy, (double)dx);        // -pi..pi，0 = 东
    int oct = (int)floor((ang + 3.14159265358979 / 8) / (3.14159265358979 / 4));
    oct = (oct + 8) % 8;                            // 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE
    static const int map[8] = { 2, 1, 0, 7, 6, 5, 4, 3 };   // 转到 N=0,NE=1,E=2,...
    return map[oct];
}

static RECT PanelRect() {
    RECT r;
    r.left = 24; r.top = 24; r.right = g_winW - 14; r.bottom = g_winH - 14;
    return r;
}

static RECT RowRect(int i);

static int VisibleRows() {
    int h = PanelRect().bottom - (RowRect(0).top) - 6;
    int n = h / 30;
    if (n < 3) n = 3;
    if (n > 30) n = 30;
    return n;
}

static RECT RowRect(int i) {
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 16;
    r.right = p.right - 34;
    r.top = p.top + 132 + i * 30;
    r.bottom = r.top + 30;   // 行高 30（与 VisibleRows/命中判定一致）
    return r;
}

static RECT TabRect(int i);
static int TabPosFromPoint(POINT pt) {
    for (int i = 0; i < kTabCount; ++i) {
        RECT r = TabRect(i);
        if (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom) return i;
    }
    return -1;
}

static RECT TabRect(int i) {
    RECT p = PanelRect();
    int w = 74, gap = 6;
    RECT r;
    r.left = p.left + 16 + i * (w + gap);
    r.right = r.left + w;
    r.top = p.top + 32;
    r.bottom = r.top + 20;
    return r;
}

static RECT IconRect(int i) {   // 只保留设置（齿轮），忽略参数
    RECT p = PanelRect();
    RECT r;
    r.right = p.right - 3;
    r.left = r.right - 14;   // 正方形 14x14
    r.top = (g_mode == M_SEARCH) ? (p.top - 22) : (p.top + 5);   // 搜索页放到面板上方（留 8px 间距）
    r.bottom = r.top + 14;   // 29..43 → 中心 36（与 Ctrl 行一致）
    return r;
}

static void UpdateScrollInfo() {
    if (!g_scrollbar) return;
    int total = (int)g_results.size();
    int page = VisibleRows();
    int maxScroll = total > page ? total - page : 0;
    if (g_scroll > maxScroll) g_scroll = maxScroll;
    if (g_scroll < 0) g_scroll = 0;

    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0;
    si.nMax = total > 0 ? total - 1 : 0;
    si.nPage = page;
    si.nPos = g_scroll;
    g_maxScroll = maxScroll;
    SetScrollInfo(g_scrollbar, SB_CTL, &si, TRUE);
    ShowWindow(g_scrollbar, SW_HIDE);   // 自绘滚动条，原生控件只保留句柄不用
}

// ---------------------------------------------------------------- 绘制
static HFONT MakeFont(int px, bool bold) {
    return CreateFontW(px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void FillRound(HDC dc, RECT r, int radius, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}

static void DrawPie(HDC dc) {
    for (int i = 0; i < 8; ++i) {
        RECT r = SectorRect(i);
        bool sel = (i == g_hover);
        FillRound(dc, r, 8, sel ? kSectorSel : kSectorFill, sel ? kRowSel : kSectorBorder);   // 选中：灰底 + 蓝框

        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        HFONT f = MakeFont(15);
        HGDIOBJ of = SelectObject(dc, f);

        if (!g_slots[i].used) {
            SetTextColor(dc, RGB(90, 90, 90));
            DrawTextW(dc, L"—", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            {   // 图标（图层类显示 Add Layers 同款图标）
                int cy0 = (r.top + r.bottom) / 2;
                RECT ib = { r.left + 8, cy0 - 8, r.left + 24, cy0 + 8 };
                if (g_slots[i].kind == L"layer") DrawIcon(dc, g_slots[i].value, ib);
                else if (g_slots[i].used) {
                    HFONT gf = CreateFontW(11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                    HGDIOBJ go2 = SelectObject(dc, gf);
                    SetTextColor(dc, RGB(150, 155, 165));
                    const wchar_t* badge = (g_slots[i].kind == L"ops") ? L"OP" : L"JS";
                    SetTextColor(dc, RGB(150, 155, 165));
                    DrawTextW(dc, badge, -1, &ib, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
                    SelectObject(dc, go2);
                    DeleteObject(gf);
                }
            }
            SetTextColor(dc, kText);
            RECT tr = { r.left + 28, r.top, r.right - 34, r.bottom };
            DrawTextW(dc, g_slots[i].name.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOCLIP);

            SetTextColor(dc, kTextDim);
            SetTextAlign(dc, TA_LEFT | TA_TOP);
            std::wstring k = g_slots[i].key.empty() ? std::to_wstring(i + 1) : g_slots[i].key;
            RECT kr = { r.right - 32, r.top, r.right - 8, r.bottom };
            SetTextAlign(dc, TA_LEFT | TA_TOP);
            DrawTextW(dc, k.c_str(), -1, &kr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, of);
        DeleteObject(f);
    }

    // 中心圆 + 弧线
    int cx = CenterX(), cy = CenterY();
    // 中心圆 + 弧线：GDI+ 抗锯齿
    {
        int dcSaved = SaveDC(dc);   // GDI+ 会改 DC 状态，用完恢复
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush ffill(Gdiplus::Color(255, 43, 43, 46));
        Gdiplus::Pen bborder(Gdiplus::Color(255, 62, 62, 68), 1.5f);
        g.FillEllipse(&ffill, cx - kCenterR, cy - kCenterR, kCenterR * 2, kCenterR * 2);
        g.DrawEllipse(&bborder, cx - kCenterR, cy - kCenterR, kCenterR * 2, kCenterR * 2);

        if (g_hover >= 0) {
            RECT r = SectorRect(g_hover);
            double dx = (r.left + r.right) / 2.0 - cx;
            double dy = (r.top + r.bottom) / 2.0 - cy;
            double angDeg = atan2(dy, dx) * 180.0 / 3.14159265358979;
            int ar = kCenterR + 2;   // 弧线压在圆环外沿（AE 风格）
            Gdiplus::Pen arcPen(Gdiplus::Color(255, 90, 155, 232), 4.0f);
            arcPen.SetStartCap(Gdiplus::LineCapRound);
            arcPen.SetEndCap(Gdiplus::LineCapRound);
            g.DrawArc(&arcPen, (REAL)(cx - ar), (REAL)(cy - ar), (REAL)(ar * 2), (REAL)(ar * 2),
                      (REAL)(angDeg - 26.0), (REAL)52.0);
        }
        RestoreDC(dc, dcSaved);
    }

        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(150, 150, 150));
    HFONT f = MakeFont(11);
    HGDIOBJ of = SelectObject(dc, f);
    RECT cr = { cx - kCenterR, cy - 10, cx + kCenterR, cy + 10 };
    DrawTextW(dc, L"Tab 搜索", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
    DeleteObject(f);
}

static RECT InputRect() {
    RECT p = PanelRect();
    RECT r = { p.left + 16, p.top + 66, p.right - 16, p.top + 98 };
    return r;
}

static void DrawPanel(HDC dc, const std::wstring& title, const std::wstring& input) {
    RECT p = PanelRect();
    FillRound(dc, p, 14, kPanelFill, kPanelBorder);

        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetBkMode(dc, TRANSPARENT);
    HFONT ft = MakeFont(15);
    HGDIOBJ of = SelectObject(dc, ft);
    SetTextColor(dc, RGB(140, 140, 140));
    RECT tr = { p.left + 16, p.top + 10, p.right - 16, p.top + 30 };
    DrawTextW(dc, title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, of);
    DeleteObject(ft);

    // 输入框
    RECT ir = InputRect();
    FillRound(dc, ir, 8, RGB(22, 22, 24), RGB(80, 80, 84));
    {   // 输入内容自己画：分层窗口（UpdateLayeredWindow）内的子窗口 EDIT 不会被合成
        HFONT fi = MakeFont(19);
        of = SelectObject(dc, fi);
        SetTextColor(dc, input.empty() ? RGB(110, 110, 110) : RGB(235, 235, 240));
        RECT irc = ir; irc.left += 10; irc.right -= 10;
        if (g_selAll && !input.empty()) {   // 全选高亮
            SIZE sz0 = {0, 0};
            GetTextExtentPoint32W(dc, input.c_str(), (int)input.size(), &sz0);
            RECT hr0 = { irc.left - 2, ir.top + 6, irc.left + sz0.cx + 3, ir.bottom - 6 };
            HBRUSH hb0 = CreateSolidBrush(RGB(48, 160, 92));
            FillRect(dc, &hr0, hb0);
            DeleteObject(hb0);
        }
        DrawTextW(dc, input.empty() ? L"输入以搜索（Tab / Esc 返回）" : input.c_str(), -1, &irc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (!input.empty()) {
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(dc, input.c_str(), (int)input.size(), &sz);
            int cxp = irc.left + sz.cx + 1;
            if (cxp < irc.right) {
                RECT cr = { cxp, ir.top + 8, cxp + 2, ir.bottom - 8 };
                HBRUSH cbr = CreateSolidBrush(RGB(200, 205, 215));
                FillRect(dc, &cr, cbr);
                DeleteObject(cbr);
            }
        }
        SelectObject(dc, of);
        DeleteObject(fi);
    }
}

static RECT ScrollTrackRect() {
    RECT p = PanelRect();
    RECT r;
    RECT r0 = RowRect(0);
    RECT r6 = RowRect(VisibleRows() - 1);
    r.right = r0.right + 10;
    r.left = r.right - 6;
    r.top = r0.top;
    r.bottom = r6.bottom;
    return r;
}

static RECT ScrollThumbRect() {
    RECT t = ScrollTrackRect();
    int total = (int)g_results.size();
    if (total <= VisibleRows()) return RECT{0,0,0,0};
    int trackH = t.bottom - t.top;
    int thumbH = (int)((double)trackH * VisibleRows() / total);
    if (thumbH < 26) thumbH = 26;
    int maxScroll = g_maxScroll > 0 ? g_maxScroll : 1;
    int y = t.top + (int)((double)(trackH - thumbH) * g_scroll / maxScroll);
    RECT r = { t.left - 1, y, t.right + 1, y + thumbH };
    return r;
}

static void DrawScrollBar(HDC dc) {
    if (g_mode != M_SEARCH) return;
    if ((int)g_results.size() <= VisibleRows()) return;

    RECT t = ScrollTrackRect();
    FillRound(dc, t, 3, RGB(44, 44, 47), RGB(44, 44, 47));
    RECT th = ScrollThumbRect();
    FillRound(dc, th, 3, g_thumbDrag ? RGB(150, 150, 158) : RGB(110, 110, 118), RGB(110, 110, 118));
}

static void DrawKeyCap(HDC dc, const std::wstring& s, int rightX, RECT row) {
    if (s.empty()) return;
    HFONT f = MakeFont(12);
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int h = 18, padX = 7;
    int cy = (row.top + row.bottom) / 2;
    RECT b = { rightX - sz.cx - padX * 2, cy - h / 2, rightX, cy + h / 2 };
    FillRound(dc, b, 4, RGB(46, 46, 51), RGB(74, 74, 82));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(205, 210, 220));
    DrawTextW(dc, s.c_str(), -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    SelectObject(dc, of);
    DeleteObject(f);
}

static bool IsPinnedItem(const Item& it) {
    std::wstring key = it.kind + L"|" + it.value;
    for (auto& k : g_pinned) if (k == key) return true;
    return false;
}

static void DrawResults(HDC dc) {
    for (int row = 0; row < VisibleRows(); ++row) {
        int i = g_scroll + row;
        if (i >= (int)g_results.size()) break;
        RECT r = RowRect(row);
        RECT fr = r; fr.bottom = r.top + 30;   // 整行（含行距），高亮无缝隙
        bool sel = (i == g_sel);
        bool hov = (i == g_hoverRow);
        // 普通行填面板色（看起来"无块"），hover 蓝、选中灰；再画 1px 描边
        {
            COLORREF fillc = hov ? RGB(46, 120, 84) : (sel ? RGB(70, 70, 74) : kPanelFill);
            HBRUSH fb = CreateSolidBrush(fillc);
            FillRect(dc, &fr, fb);
            DeleteObject(fb);
            // 行间 1px 极暗分隔线
            {
                RECT sl = { fr.left, fr.bottom - 1, fr.right, fr.bottom };
                HBRUSH sb = CreateSolidBrush(RGB(42, 42, 45));
                FillRect(dc, &sl, sb);
                DeleteObject(sb);
            }
        }
        if (sel) { RECT bar = { fr.left, fr.top, fr.left + 4, fr.bottom }; HBRUSH bb2 = CreateSolidBrush(kRowSel); FillRect(dc, &bar, bb2); DeleteObject(bb2); }

        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        HFONT f = MakeFont(17);
        HGDIOBJ of = SelectObject(dc, f);
        RECT ibox = { r.left + 30, (r.top + r.bottom) / 2 - 8, r.left + 46, (r.top + r.bottom) / 2 + 8 };
        if (g_results[i].kind == L"layer") DrawIcon(dc, g_results[i].value, ibox);
        RECT tr = r; tr.left += 52; tr.right -= 90;
        {   // 所有行都编号（1..n）；Ctrl+1~6 数字快捷键仍只到 6
            SetTextColor(dc, hov ? RGB(225, 232, 245) : (sel ? RGB(200, 200, 210) : RGB(120, 120, 128)));
            RECT nr = r; nr.left += 8; nr.right = nr.left + 22;
            std::wstring num = std::to_wstring(row + 1);
            DrawTextW(dc, num.c_str(), -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, kText);
        }
        DrawTextW(dc, g_results[i].name.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (IsPinnedItem(g_results[i])) {
            SetTextColor(dc, RGB(255, 183, 77));
            RECT pr2 = { r.right - 92, r.top, r.right - 66, r.bottom };
            {
                HFONT pf = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
                HGDIOBJ po = SelectObject(dc, pf);
                DrawTextW(dc, L"\uE718", -1, &pr2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, po);
                DeleteObject(pf);
            }
            SetTextColor(dc, RGB(210, 210, 210));
        }
        SetTextColor(dc, RGB(210, 210, 210));
        {
            if ((g_results[i].kind == L"ops" && !g_results[i].key.empty()) || g_results[i].kind == L"keys") {
                std::wstring hint = (g_results[i].kind == L"ops") ? g_results[i].key : g_results[i].value;
                DrawKeyCap(dc, hint, r.right - 66, r);
            }
            RECT tg = { r.right - 60, r.top, r.right - 8, r.bottom };
            std::wstring tagv = (g_results[i].kind == L"script") ? L"脚本" : ((g_results[i].kind == L"keys" || g_results[i].kind == L"ops") ? L"操作" : L"图层");
            DrawTextW(dc, tagv.c_str(), -1, &tg, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(dc, of);
        DeleteObject(f);
    }

    if (g_results.empty() && !g_query.empty()) {
        RECT r = RowRect(0);
        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(130, 130, 130));
        HFONT f = MakeFont(16);
        HGDIOBJ of = SelectObject(dc, f);
        DrawTextW(dc, L"没有匹配项", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(f);
    }
}

static RECT SettingOptRect(int i) {
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 480;
    r.right = p.right - 16;
    r.top = p.top + 132 + i * 34;
    r.bottom = r.top + 28;
    return r;
}

static RECT SettingRowRect(int i) {
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 16;
    r.right = p.right - 16;
    r.top = p.top + 132 + i * 30;
    r.bottom = r.top + 30;   // 行高 30，与列表一致
    return r;
}

static void DrawMiniPie(HDC dc, RECT area, int sel) {
    int cx = (area.left + area.right) / 2;
    int cy = (area.top + area.bottom) / 2;
    int bw = 74, bh = 26;
    int ox = 62, oy = 52, dx = 46, dy = 34;

    for (int i = 0; i < 8; ++i) {
        int x = 0, y = 0;
        switch (i) {
            case 0: y = -oy; break;
            case 1: x = dx; y = -dy; break;
            case 2: x = ox; break;
            case 3: x = dx; y = dy; break;
            case 4: y = oy; break;
            case 5: x = -dx; y = dy; break;
            case 6: x = -ox; break;
            case 7: x = -dx; y = -dy; break;
        }
        RECT r = { cx + x - bw / 2, cy + y - bh / 2, cx + x + bw / 2, cy + y + bh / 2 };
        bool on = (i == sel);
        FillRound(dc, r, 5, on ? kRowSel : kSectorFill, on ? kRowSel : kSectorBorder);
        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_slots[i].used ? kText : RGB(100, 100, 100));
        HFONT f = MakeFont(12);
        HGDIOBJ of = SelectObject(dc, f);
        std::wstring label = g_slots[i].used ? g_slots[i].name : std::to_wstring(i + 1);
        DrawTextW(dc, label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, of);
        DeleteObject(f);
    }
}
static RECT SetRingRect(int i) {
    RECT p = PanelRect();
    int cx = p.left + 250, cy = p.top + 290;
    int ox = 0, oy = 0;
    const double s = 0.85;
    const int dx = (int)(kOffHoriz * s), dy = (int)(kOffVert * s);   // E/W 用水平偏移，与斜向分开
    const int ddx = (int)(kOffDiagX * s), ddy = (int)(kOffDiagY * s);
    switch (i) {
        case 0: oy = -dy; break;
        case 1: ox = ddx; oy = -ddy; break;
        case 2: ox = dx; break;
        case 3: ox = ddx; oy = ddy; break;
        case 4: oy = dy; break;
        case 5: ox = -ddx; oy = ddy; break;
        case 6: ox = -dx; break;
        case 7: ox = -ddx; oy = -ddy; break;
    }
    RECT r;
    r.left = cx + ox - 58;
    r.right = cx + ox + 58;
    r.top = cy + oy - 13;
    r.bottom = cy + oy + 13;
    return r;
}

static RECT SetHotkeyRect() {
    RECT p = PanelRect();
    RECT r = { p.left + 60, p.top + 438, p.left + 440, p.top + 468 };
    return r;
}

static RECT SetTabRect(int i) {   // 设置页里的分类页签
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 480 + i * 54;
    r.right = r.left + 52;
    r.top = p.top + 108;
    r.bottom = r.top + 24;
    return r;
}

static int SetTabPosFromPoint(POINT pt) {
    for (int i = 0; i < kTabCount; ++i) {
        RECT r = SetTabRect(i);
        if (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom) return i;
    }
    return -1;
}

static RECT SetInputRect() {
    RECT p = PanelRect();
    RECT r = { p.left + 480, p.top + 136, p.right - 16, p.top + 166 };
    return r;
}

static RECT SetResultRect(int row) {
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 480;
    r.right = p.right - 16;
    r.top = p.top + 174 + row * 26;
    r.bottom = r.top + 28;
    return r;
}

static void DrawSettings(HDC dc) {
    RECT p = PanelRect();
    FillRound(dc, p, 14, kPanelFill, kPanelBorder);
    SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr);
    SetBkMode(dc, TRANSPARENT);
    DrawPieTabs(dc);

    {   // 右上角：关闭
        RECT xr = IconRect(0);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 185));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, xr.left + 4, xr.top + 4, nullptr); LineTo(dc, xr.right - 4, xr.bottom - 4);
        MoveToEx(dc, xr.right - 4, xr.top + 4, nullptr); LineTo(dc, xr.left + 4, xr.bottom - 4);
        SelectObject(dc, op);
        DeleteObject(pen);
    }

    {   // 左：8 个环状热盒
        static const wchar_t* dirs[8] = { L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW" };
        HFONT f = MakeFont(14);
        for (int i = 0; i < 8; ++i) {
            RECT r = SetRingRect(i);
            bool sel = (i == g_sel);
            COLORREF fillc = sel ? RGB(44, 108, 72) : RGB(42, 42, 46);
            COLORREF bordc = sel ? RGB(70, 150, 110) : RGB(64, 64, 70);
            FillRound(dc, r, 6, fillc, bordc);
            HGDIOBJ of = SelectObject(dc, f);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_slots[i].used ? RGB(225, 228, 235) : RGB(140, 143, 150));
            RECT tr = r; tr.left += 10; tr.right -= 6;
            std::wstring label = std::wstring(dirs[i]) + L"  " + (g_slots[i].used ? g_slots[i].name : L"（空）");
            DrawTextW(dc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, of);
        }
        DeleteObject(f);
        // 中心提示
        HFONT fc = MakeFont(12);
        HGDIOBJ oc = SelectObject(dc, fc);
        SetTextColor(dc, RGB(130, 134, 142));
        RECT pp2 = PanelRect(); int ccx = pp2.left + 250, ccy = pp2.top + 290;
        RECT cr = { ccx - 80, ccy - 12, ccx + 80, ccy + 12 };
        const wchar_t* mods2[4] = { L"无", L"Ctrl", L"Shift", L"Alt" };
        std::wstring ctr = L"热盒 " + std::to_wstring(g_box + 1) + L" · " + mods2[g_mod];
        DrawTextW(dc, ctr.c_str(), -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oc);
        DeleteObject(fc);
    }

    {   // 左下半：呼出快捷键
        RECT hk = SetHotkeyRect();
        FillRound(dc, hk, 6, RGB(34, 34, 38), RGB(62, 62, 68));
        SetBkMode(dc, TRANSPARENT);
        HFONT fh = MakeFont(15);
        HGDIOBJ oh = SelectObject(dc, fh);
        SetTextColor(dc, RGB(205, 208, 215));
        RECT lt = hk; lt.left += 12; lt.right -= 150;
        static const wchar_t* dnames[8] = { L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW" };
        std::wstring kl = std::wstring(L"扇区快捷键（") + dnames[g_sel] + L"）";
        DrawTextW(dc, kl.c_str(), -1, &lt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, g_captureHotkey ? RGB(120, 255, 200) : RGB(255, 210, 120));
        std::wstring hv = g_captureHotkey ? std::wstring(L"按下按键…") : (g_slots[g_sel].key.empty() ? std::wstring(L"未设置") : g_slots[g_sel].key);
        RECT vt = hk; vt.left = hk.right - 150; vt.right -= 10;
        DrawTextW(dc, hv.c_str(), -1, &vt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oh);
        DeleteObject(fh);
    }
    {   // 右：搜索 + 结果（点击即绑定到左侧选中的扇区）
        HFONT ft = MakeFont(14);
        HGDIOBJ of = SelectObject(dc, ft);
        SetTextColor(dc, RGB(170, 175, 185));
        static const wchar_t* dirs2[8] = { L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW" };
        RECT hr = { p.left + 480, p.top + 86, p.right - 16, p.top + 108 };
        std::wstring ht = L"搜索并绑定到 " + std::wstring(dirs2[g_sel]);
        if (g_slots[g_sel].used) ht += L"（当前：" + g_slots[g_sel].name + L"）";
        DrawTextW(dc, ht.c_str(), -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, of);
        DeleteObject(ft);

        RECT ir = SetInputRect();
        FillRound(dc, ir, 8, RGB(22, 22, 24), RGB(80, 80, 84));

        {   // 分类页签
            HFONT ftt = MakeFont(13);
            for (int ti = 0; ti < kTabCount; ++ti) {
                RECT tr = SetTabRect(ti);
                bool on = (g_tabOrder[ti] == g_tab);
                if (on) FillRound(dc, tr, 5, RGB(50, 52, 58), RGB(60, 62, 70));
                HGDIOBJ o9 = SelectObject(dc, ftt);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, on ? RGB(240, 244, 250) : RGB(150, 152, 160));
                DrawTextW(dc, kTabs[g_tabOrder[ti]], -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, o9);
            }
            DeleteObject(ftt);
        }
        // 结果
        int rows = (p.bottom - 40 - SetResultRect(0).top) / 28;
        if (rows > 12) rows = 12;
        HFONT fr = MakeFont(15);
        for (int row = 0; row < rows; ++row) {
            int idx = g_scroll + row;
            if (idx >= (int)g_results.size()) break;
            RECT r = SetResultRect(row);
            if (idx == g_selSearch) { FillRound(dc, r, 5, RGB(40, 100, 68), RGB(54, 122, 88)); }
            of = SelectObject(dc, fr);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(225, 228, 235));
            RECT ibx = { r.left + 6, r.top + 5, r.left + 22, r.top + 21 };
            if (g_results[idx].kind == L"layer") DrawIcon(dc, g_results[idx].value, ibx);
            RECT tr = r; tr.left += 26; tr.right -= 96;
            DrawTextW(dc, g_results[idx].name.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SetTextColor(dc, RGB(140, 144, 152));
            RECT tg = r; tg.left = r.right - 76; tg.right -= 8;
            if ((g_results[idx].kind == L"ops" && !g_results[idx].key.empty())) DrawKeyCap(dc, g_results[idx].key, r.right - 84, r);
            std::wstring tagv = (g_results[idx].kind == L"script") ? L"脚本" : ((g_results[idx].kind == L"keys" || g_results[idx].kind == L"ops") ? L"操作" : L"图层");
            DrawTextW(dc, tagv.c_str(), -1, &tg, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, of);
        }
        DeleteObject(fr);
        if (g_results.empty()) {
            HFONT fe = MakeFont(14);
            of = SelectObject(dc, fe);
            SetTextColor(dc, RGB(120, 124, 132));
            RECT er = { p.left + 330, p.top + 160, p.right - 20, p.top + 190 };
            DrawTextW(dc, L"没有匹配项", -1, &er, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, of);
            DeleteObject(fe);
        }
    }

    {   // 底部：提示 + 按钮
        HFONT f3 = MakeFont(12);
        HGDIOBJ o3 = SelectObject(dc, f3);
        SetTextColor(dc, RGB(120, 124, 132));
        RECT hr = { p.left + 16, p.bottom - 52, p.right - 200, p.bottom - 30 };
        DrawTextW(dc, L"左：点热盒选中扇区 · 右：点结果即绑定", -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, o3);
        DeleteObject(f3);

        int bw = 76, bh = 26, gap = 10;
        RECT okB = { p.right - 16 - bw, p.bottom - 16 - bh, p.right - 16, p.bottom - 16 };
        RECT caB = { okB.left - gap - bw, okB.top, okB.left - gap, okB.bottom };
        FillRound(dc, caB, 5, RGB(48, 48, 52), RGB(70, 70, 78));
        FillRound(dc, okB, 5, RGB(46, 120, 84), RGB(56, 132, 96));
        HFONT bf = MakeFont(13);
        HGDIOBJ bo = SelectObject(dc, bf);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(210, 214, 220));
        DrawTextW(dc, L"取消", -1, &caB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, RGB(240, 245, 255));
        DrawTextW(dc, L"确定", -1, &okB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, bo);
        DeleteObject(bf);
    }
}


static void DrawTabs(HDC dc) {
    for (int i = 0; i < kTabCount; ++i) {
        RECT r = TabRect(i);
        bool on = (g_tabOrder[i] == g_tab);

        SetMapMode(dc, MM_TEXT); SetViewportOrgEx(dc, 0, 0, nullptr); SetWindowOrgEx(dc, 0, 0, nullptr); SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, on ? RGB(255, 255, 255) : RGB(150, 150, 155));
        HFONT f = MakeFont(16);
        HGDIOBJ of = SelectObject(dc, f);
        DrawTextW(dc, kTabs[g_tabOrder[i]], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(f);

        if (on) {
            RECT u = { r.left + 10, r.bottom - 2, r.right - 10, r.bottom };
            HBRUSH b = CreateSolidBrush(kRowSel);
            FillRect(dc, &u, b);
            DeleteObject(b);
        }
    }
}
static void DrawTopIcons(HDC dc) {
    // 右上角：设置齿轮（小号，中心与 Ctrl 页签行对齐）
    RECT gr = IconRect(0);
    {   // 底板
        RECT pill = { gr.left - 4, gr.top - 4, gr.right + 4, gr.bottom + 4 };
        FillRound(dc, pill, 5, RGB(40, 40, 43), RGB(56, 56, 60));
    }
    int cx = (gr.left + gr.right) / 2, cy = (gr.top + gr.bottom) / 2;
    const double kPi = 3.14159265358979323846;
    const double R = 6.0, Rin = 3.9;
    const int teeth = 7;
    const int steps = 320;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    {
        std::vector<Gdiplus::PointF> pts;
        pts.reserve(steps + 2);
        for (int i = 0; i <= steps; ++i) {
            double th = (double)i / steps * 2 * kPi;
            double ph = fmod(th * teeth / (2 * kPi), 1.0);
            double rr;
            if (ph < 0.44) {
                rr = R;
            } else if (ph < 0.54) {
                double u = (ph - 0.44) / 0.10;
                rr = R - u * (R - Rin);
            } else {
                rr = Rin;
            }
            pts.push_back(Gdiplus::PointF((Gdiplus::REAL)(cx + cos(th) * rr),
                                          (Gdiplus::REAL)(cy + sin(th) * rr)));
        }
        Gdiplus::GraphicsPath path;
        path.AddLines(&pts[0], (INT)pts.size());
        path.CloseFigure();
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 168, 168, 175));
        g.FillPath(&brush, &path);
    }
    {   // 中心孔：用底板色挖空
        Gdiplus::SolidBrush hole(Gdiplus::Color(255, 40, 40, 43));
        g.FillEllipse(&hole, (Gdiplus::REAL)(cx - 2), (Gdiplus::REAL)(cy - 2), 4.0f, 4.0f);
    }
}



static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    const int winW = g_winW + kPadX * 2;
    const int winH = g_winH + kPadY * 2;

    // 1) 先按原来的尺寸把界面画好（坐标体系完全不变）
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, g_winW, g_winH);
    HGDIOBJ old = SelectObject(mem, bmp);

    RECT bg = { 0, 0, g_winW, g_winH };
    HBRUSH bb = CreateSolidBrush(kBg);
    FillRect(mem, &bg, bb);
    DeleteObject(bb);

    if (g_mode == M_PIE) {
        DrawPie(mem);
    } else if (g_mode == M_SEARCH) {
        std::wstring title = (g_bindSlot >= 0)
            ? (L"为扇区 " + std::to_wstring(g_bindSlot + 1) + L" 选择绑定")
            : L"搜索（Enter 执行，Esc 返回）";
        DrawPanel(mem, title, g_query);
        DrawTabs(mem);
        DrawResults(mem);
        DrawScrollBar(mem);
    } else {
        DrawSettings(mem);
    }

    if (g_mode == M_PIE) DrawPieTabs(mem);
    if (g_binding) {
        std::wstring hint = L"绑定：" + g_bindItem.name + L"  →  点击扇区完成 · Esc 取消";
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 210, 120));
        HFONT hf = MakeFont(15);
        HGDIOBJ ho = SelectObject(mem, hf);
        RECT hr = { 0, 92, g_winW, 118 };
        SetMapMode(mem, MM_TEXT); SetViewportOrgEx(mem, 0, 0, nullptr); SetTextAlign(mem, TA_LEFT | TA_TOP);
        DrawTextW(mem, hint.c_str(), -1, &hr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, ho);
        DeleteObject(hf);
    }
    if (g_mode != M_SETTINGS) DrawTopIcons(mem);

    if (g_mode != M_PIE) {   // 仅搜索/设置页显示拖拽条
        RECT pr = PanelRect();
        HBRUSH gb = CreateSolidBrush(RGB(34, 34, 37));
        RECT b1 = { pr.left + 8, pr.bottom - 12, pr.right - 8, pr.bottom - 5 };
        RECT b2 = { pr.right - 12, pr.top + 48, pr.right - 5, pr.bottom - 8 };
        FillRect(mem, &b1, gb);
        FillRect(mem, &b2, gb);
        HBRUSH db = CreateSolidBrush(RGB(140, 140, 148));
        int cxp = (pr.left + pr.right) / 2, cyp = (pr.top + pr.bottom) / 2;
        for (int i = -1; i <= 1; ++i) {
            RECT d1 = { cxp + i * 8 - 1, pr.bottom - 10, cxp + i * 8 + 2, pr.bottom - 7 };
            RECT d2 = { pr.right - 10, cyp + i * 8 - 1, pr.right - 7, cyp + i * 8 + 2 };
            FillRect(mem, &d1, db);
            FillRect(mem, &d2, db);
        }
        DeleteObject(db);
        DeleteObject(gb);
    }

    // 2) 放大到整块输入区：黑色背景 + 居中的界面 + 边缘小点
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = winW;
    bi.bmiHeader.biHeight = -winH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC big = CreateCompatibleDC(dc);
    HBITMAP bigBmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBig = bigBmp ? SelectObject(big, bigBmp) : nullptr;

    RECT all = { 0, 0, winW, winH };
    HBRUSH nb = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(big, &all, nb);
    DeleteObject(nb);

    BitBlt(big, kPadX, kPadY, g_winW, g_winH, mem, 0, 0, SRCCOPY);

    {   // 边缘提示点：暗示这块区域会接收鼠标
        HBRUSH dot = CreateSolidBrush(RGB(56, 56, 62));
        HBRUSH corner = CreateSolidBrush(RGB(96, 96, 106));
        const int step = 32, s = 2;
        for (int x = 12; x < winW - 6; x += step) {
            RECT t1 = { x, 8, x + s, 8 + s };
            RECT t2 = { x, winH - 10, x + s, winH - 10 + s };
            FillRect(big, &t1, dot); FillRect(big, &t2, dot);
        }
        for (int y = 12; y < winH - 6; y += step) {
            RECT t1 = { 8, y, 8 + s, y + s };
            RECT t2 = { winW - 10, y, winW - 10 + s, y + s };
            FillRect(big, &t1, dot); FillRect(big, &t2, dot);
        }
        RECT c1 = { 8, 8, 22, 11 }, c2 = { 8, 8, 11, 22 };
        RECT c3 = { winW - 22, 8, winW - 8, 11 }, c4 = { winW - 11, 8, winW - 8, 22 };
        RECT c5 = { 8, winH - 11, 22, winH - 8 }, c6 = { 8, winH - 22, 11, winH - 8 };
        RECT c7 = { winW - 22, winH - 11, winW - 8, winH - 8 }, c8 = { winW - 11, winH - 22, winW - 8, winH - 8 };
        FillRect(big, &c1, corner); FillRect(big, &c2, corner);
        FillRect(big, &c3, corner); FillRect(big, &c4, corner);
        FillRect(big, &c5, corner); FillRect(big, &c6, corner);
        FillRect(big, &c7, corner); FillRect(big, &c8, corner);
        DeleteObject(dot); DeleteObject(corner);
    }

    // 3) 逐像素 alpha：纯黑=1（不可见但可点），其它=255
    if (bits) {
        DWORD* px = (DWORD*)bits;
        int n = winW * winH;
        for (int i = 0; i < n; ++i) {
            DWORD c = px[i] & 0x00FFFFFFu;
            px[i] = (c == 0) ? 0x01000000u : (c | 0xFF000000u);
        }
        RECT wr; GetWindowRect(hwnd, &wr);
        POINT dst = { wr.left, wr.top };
        SIZE sz = { winW, winH };
        POINT src = { 0, 0 };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        HDC screen = GetDC(nullptr);
        UpdateLayeredWindow(hwnd, screen, &dst, &sz, big, &src, 0, &bf, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }

    if (oldBig) SelectObject(big, oldBig);
    if (bigBmp) DeleteObject(bigBmp);
    DeleteDC(big);
    if (old) SelectObject(mem, old);
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}



// ---------------------------------------------------------------- 热键配置
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    size_t b = s.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}

static bool ParseHotkey(const std::wstring& spec, UINT& mods, UINT& vk) {
    mods = MOD_NOREPEAT;
    vk = 0;
    std::wstring s = spec;
    while (!s.empty()) {
        size_t plus = s.find(L'+');
        std::wstring token = Trim(plus == std::wstring::npos ? s : s.substr(0, plus));
        s = (plus == std::wstring::npos) ? L"" : s.substr(plus + 1);
        if (token.empty()) continue;
        std::wstring low = ToLower(token);
        if (low == L"ctrl" || low == L"control") { mods |= MOD_CONTROL; continue; }
        if (low == L"alt")                       { mods |= MOD_ALT;     continue; }
        if (low == L"shift")                     { mods |= MOD_SHIFT;   continue; }
        if (low == L"win")                       { mods |= MOD_WIN;     continue; }
        if (token == L"`") { vk = VK_OEM_3; continue; }   // 反引号
        if (low == L"space" || token == L" ") { vk = VK_SPACE; continue; }
        if (token.size() == 1) {
            wchar_t c = (wchar_t)towupper(token[0]);
            if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) { vk = (UINT)c; continue; }
        }
        if (low.size() >= 2 && low[0] == L'f') {
            int n = _wtoi(low.c_str() + 1);
            if (n >= 1 && n <= 24) { vk = VK_F1 + n - 1; continue; }
        }
        return false;
    }
    return vk != 0;
}

static std::wstring VkNameFromVk(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F12) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk == VK_SPACE) return L"Space";
    if (vk == VK_OEM_3) return L"`";
    return L"";
}

static void SaveIni() {
    std::wstring s;
    s += L"; Cavalry ShortCut\r\n";
    s += L"hotkey=" + g_hotkeySpec + L"\r\n";
    s += std::wstring(L"ime=") + (g_imeOn ? L"on" : L"off") + L"\r\n";
    s += std::wstring(L"opsmode=") + (g_opsUseKeys ? L"keys" : L"mcp") + L"\r\n";
    std::wstring order;
    for (int i = 0; i < kTabCount; ++i) { if (i) order += L","; order += std::to_wstring(g_tabOrder[i]); }
    s += L"taborder=" + order + L"\r\n";
    WriteFileUtf8(ExeDir() + L"\\Cavalry_ShortCut.ini", Narrow(s));
}

static void LoadConfig() {
    std::wstring path = ExeDir() + L"\\Cavalry_ShortCut.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteFileUtf8(path, "; Cavalry ShortCut\r\n; hotkey: Ctrl/Alt/Shift/Win + A-Z / 0-9 / F1-F24\r\nhotkey=Alt+Q\r\n");
    }
    std::string raw = ReadFileUtf8(path);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        std::string line = raw.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? raw.size() : eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("taborder=", 0) == 0) {
            std::wstring v = Trim(Widen(line.substr(9)));
            int idx = 0; size_t pos = 0;
            while (pos <= v.size() && idx < kTabCount) {
                size_t comma = v.find(L",", pos);
                std::wstring tokv = (comma == std::wstring::npos) ? v.substr(pos) : v.substr(pos, comma - pos);
                int id = _wtoi(tokv.c_str());
                if (id >= 0 && id < kTabCount) g_tabOrder[idx++] = id;
                if (comma == std::wstring::npos) break;
                pos = comma + 1;
            }
            return;
        }
        if (line.rfind("opsmode=", 0) == 0) {
            std::wstring v = Trim(Widen(line.substr(8)));
            g_opsUseKeys = (ToLower(v) != L"mcp");
            return;
        }
        if (line.rfind("ime=", 0) == 0) {
            g_imeOn = (line.find("off") == std::string::npos);
            continue;
        }
        if (line.rfind("hotkey=", 0) == 0) {
            std::wstring spec = Trim(Widen(line.substr(7)));
            UINT mods = 0, vk = 0;
            std::wstring lowSpec = ToLower(spec);
            if (lowSpec == L"off" || lowSpec == L"none" || spec.empty()) {
                g_hotkeySpec = L"off"; g_hotkeyMods = 0; g_hotkeyVk = 0;
            } else if (ParseHotkey(spec, mods, vk)) {
                g_hotkeySpec = spec; g_hotkeyMods = mods; g_hotkeyVk = vk;
            }
            return;
        }
    }
}


// ---------------------------------------------------------------- 鼠标钩子（圆盘外部确认）
static HHOOK g_mouseHook = nullptr;
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    static bool swallowUp = false;
    if (nCode == HC_ACTION && wParam == WM_RBUTTONDOWN && g_main && IsWindowVisible(g_main) && g_mode == M_PIE) {
        swallowUp = true;
        PostMessageW(g_main, WM_APP + 8, 0, 0);
        return 1;
    }
    if (nCode == HC_ACTION && wParam == WM_RBUTTONUP && swallowUp) {
        swallowUp = false;
        return 1;   // 抬起也吞掉，阻断 WM_CONTEXTMENU
    }
    if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN && g_main && IsWindowVisible(g_main) && g_mode == M_PIE) {
        PostMessageW(g_main, WM_APP + 7, 0, 0);
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------- 点击捕获层
// 圆盘用色键透明，透明像素的点击会穿透到下层窗口。为了能收到"热盒外部"的左键，
// 额外铺一层几乎全透明（alpha=1）的全屏窗口，只负责接收点击。
static HWND g_catcher = nullptr;

// 按需注册热键：只有 Cavalry（或热盒自己）在前台时才占用 Ctrl+`，其它应用不受影响
static HWND g_msgWndGlobal = nullptr;
static bool g_hotkeyOn = false;

static void SetHotkeyEnabled(bool on) {
    if (!g_msgWndGlobal) return;
    if (on) {
        if (g_hotkeyOn) return;
        if (RegisterHotKey(g_msgWndGlobal, 1, g_hotkeyMods, g_hotkeyVk)) {
            g_hotkeyOn = true;
            Log("hotkey registered");
        } else {
            Log("hotkey register FAILED err=" + std::to_string(GetLastError()));
        }
    } else {
        if (!g_hotkeyOn) return;
        UnregisterHotKey(g_msgWndGlobal, 1);
        g_hotkeyOn = false;
    }
}
static bool IsCavalryWindow(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;   // 排除热盒自己（文件名也含 cavalry）
    if (!pid) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    wchar_t path[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    bool ok = false;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        std::wstring s = path;
        for (auto& c : s) c = (wchar_t)towlower(c);
        ok = (s.find(L"cavalry.exe") != std::wstring::npos);
    }
    CloseHandle(hProc);
    return ok;
}
static bool g_cavalryAlive = true;

struct CavProbe { bool found; bool visible; };

static BOOL CALLBACK CavProbeProc(HWND h, LPARAM lp) {
    CavProbe* pr = (CavProbe*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return TRUE;
    wchar_t buf[MAX_PATH] = {0};
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hp, 0, buf, &sz);
    CloseHandle(hp);
    if (!ok) return TRUE;
    std::wstring path(buf);
    for (auto& ch : path) ch = (wchar_t)towlower(ch);
    if (path.find(L"cavalry") == std::wstring::npos) return TRUE;
    if (path.find(L"cavalry_shortcut") != std::wstring::npos) return TRUE;
    pr->found = true;
    if (IsWindowVisible(h) && !IsIconic(h)) { pr->visible = true; return FALSE; }
    return TRUE;
}

// 每 tick 调用：Cavalry 没有"可见且未最小化"的窗口 → 收起热盒

static bool OpenFolderViaPidl(const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return false;
    HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    CoTaskMemFree(pidl);
    return SUCCEEDED(hr);
}

static bool OpenFolderViaShell(const std::wstring& path) {
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // 走 Explorer 自己的 COM 服务，避免受限进程直接拉 explorer.exe 失败
    IDispatch* pShell = nullptr;
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Shell.Application", &clsid))) { if (SUCCEEDED(hrInit)) CoUninitialize(); return false; }
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&pShell))) { if (SUCCEEDED(hrInit)) CoUninitialize(); return false; }
    if (!pShell) return false;

    LPOLESTR name = (LPOLESTR)L"ShellExecute";
    DISPID dispid = 0;
    bool ok = false;
    if (SUCCEEDED(pShell->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid))) {
        VARIANT arg;
        VariantInit(&arg);
        arg.vt = VT_BSTR;
        arg.bstrVal = SysAllocString(path.c_str());
        DISPPARAMS dp = { &arg, nullptr, 1, 0 };
        VARIANT res;
        VariantInit(&res);
        ok = SUCCEEDED(pShell->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &res, nullptr, nullptr));
        VariantClear(&res);
        VariantClear(&arg);
    }
    pShell->Release();
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}

static std::string DescribeWindow(HWND h) {
    if (!h) return "null";
    wchar_t cls[128] = {0}; GetClassNameW(h, cls, 128);
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    std::string out = Narrow(cls) + "/" + std::to_string(pid);
    return out;
}
static int g_focusRetry = 0;   // 显示后短暂抢焦点重试
static DWORD g_shownTick = 0;
static bool g_forceShow = false;   // 仅调试用：--show-force 时即使 Cavalry 没开也显示
static bool g_startSettings = false;   // 调试：--settings 直接进设置页
static bool g_spaceReg = false;   // 空格热键是否已注册
static bool g_warmedOnce = false;   // 只在第一次呼出时预热一次


static bool AnyCavalryVisible() {
    CavProbe pr = { false, false };
    EnumWindows(CavProbeProc, (LPARAM)&pr);
    return pr.visible;
}

static void TickCavalryState(HWND hotbox) {
    // 先确保搜索/设置模式一定没有注册空格热键
    if (g_mode != M_PIE) { if (g_spaceReg) { UnregisterHotKey(g_msgWndGlobal, 2); g_spaceReg = false; } }
    {   // Cavalry 启动时预载一次：让第一次操作也快（不显示任何窗口）
        static bool prevCav = false;
        bool cavNow = AnyCavalryVisible();
        if (cavNow && !prevCav) {
            std::thread([]() {
                EnsurePreamble();
                std::string o; DWORD st = 0;
                HttpCall(L"POST", L"/call", "{\"name\":\"execute_script\",\"args\":{\"script\":\"api.getFrame();\"}}", o, st);
                Log("preload done");
            }).detach();
        }
        prevCav = cavNow;
    }
    // 只做一件事：热盒被别的窗口挡住时，把空格热键归还；露出来时再收回
    if (!g_msgWndGlobal) return;
    bool visible = IsWindowVisible(hotbox) != 0;
    bool covered = true;
    if (visible) {
        RECT r; GetWindowRect(hotbox, &r);
        POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        HWND under = WindowFromPoint(c);
        covered = !(under && (under == hotbox || GetAncestor(under, GA_ROOT) == hotbox));
    }
    bool want = visible && !covered && (GetForegroundWindow() == hotbox) && (g_mode == M_PIE);   // 仅圆盘模式
    if (want && !g_spaceReg) { RegisterHotKey(g_msgWndGlobal, 2, MOD_NOREPEAT, VK_SPACE); g_spaceReg = true; }
    else if (!want && g_spaceReg) { UnregisterHotKey(g_msgWndGlobal, 2); g_spaceReg = false; }
}


static LRESULT CALLBACK CatcherProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_LBUTTONDOWN) {
        if (g_main) PostMessageW(g_main, WM_APP + 7, 0, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowCatcher(bool show) {
    if (!g_catcher) return;
    if (show) {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        SetWindowPos(g_catcher, HWND_TOPMOST, vx, vy, vw, vh, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        ShowWindow(g_catcher, SW_HIDE);
    }
}
// ---------------------------------------------------------------- 窗口
#define HOTKEY_TOGGLE 1
#define IDM_ACTIONS 3003
#define IDM_LOG 3004
#define IDM_RELOAD 3005
#define IDM_EXIT 3006
#define IDM_INI 3007
#define IDM_SETTINGS 3008

static void ForceForeground(HWND hwnd) {
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();
    if (fgThread && fgThread != myThread) AttachThreadInput(fgThread, myThread, TRUE);
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (fgThread && fgThread != myThread) AttachThreadInput(fgThread, myThread, FALSE);
}

static void HideHotbox(HWND hwnd) {
    if (g_editHost) ShowWindow(g_editHost, SW_HIDE);   // 先收输入框小窗
    if (g_scrollbar) ShowWindow(g_scrollbar, SW_HIDE);
    // 先把界面复位成圆盘并画好，再隐藏：这样下次显示的第一帧就是圆盘，不会闪搜索面板
    g_mode = M_PIE;
    g_hover = -1;
    g_query.clear();
    SyncEditFromMode();
    if (IsWindowVisible(hwnd)) {
        InvalidateRect(hwnd, nullptr, TRUE);
        UpdateWindow(hwnd);
    }
    ShowWindow(hwnd, SW_HIDE);
}

static int g_savedWinW = 0;
static void EnterSettingsMode(HWND hwnd) {
    if (g_savedWinW == 0) { g_savedWinW = g_winW; g_winW = 900; }
    RECT wr; GetWindowRect(hwnd, &wr);
    SetWindowPos(hwnd, HWND_TOP, wr.left, wr.top, g_winW + kPadX * 2, g_winH + kPadY * 2, SWP_NOMOVE | SWP_NOZORDER);
    g_mode = M_SETTINGS;
    SyncEditFromMode();
    InvalidateRect(hwnd, nullptr, TRUE);
}
static void ExitSettingsMode(HWND hwnd) {
    if (g_savedWinW) { g_winW = g_savedWinW; g_savedWinW = 0; }
    RECT wr; GetWindowRect(hwnd, &wr);
    SetWindowPos(hwnd, HWND_TOP, wr.left, wr.top, g_winW + kPadX * 2, g_winH + kPadY * 2, SWP_NOMOVE | SWP_NOZORDER);
}

static void ShowAtCursor(HWND hwnd) {
    {   // 热更新：每次呼出重新扫描 actions\ 与 ops\，新脚本立刻可用
        g_index.clear();
        LoadIndex();
        LoadOps();
        Log("reload: " + std::to_string(g_index.size()) + " items, ops=" + std::to_string(g_opsFiles.size()));
    }
    if (!g_warmedOnce) {   // 第一次呼出：后台发一次真实 API 调用，把 Cavalry 的 JS 上下文热起来
        g_warmedOnce = true;
        std::thread([]() { EnsurePreamble(); std::string o; DWORD st = 0;
            HttpCall(L"POST", L"/call", "{\"name\":\"execute_script\",\"args\":{\"script\":\"api.getFrame();\"}}", o, st); }).detach();
    }
    if (!g_forceShow && !AnyCavalryVisible()) { Log("show skipped: cavalry not running"); return; }
    bool cavFrontAtShow = IsCavalryWindow(GetForegroundWindow());   // 只有从 Cavalry 触发才抢焦点
    if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);   // 先藏起来，避免旧画面（搜索面板）闪一下
    g_mode = M_PIE;
    { HWND fg0 = GetForegroundWindow(); if (fg0 && IsCavalryWindow(fg0)) g_cavalryWnd = fg0; }
    g_box = 0; g_mod = 0; LoadActive();   // 每次呼出都从"热盒1 · 无"开始
    g_ctrlUsed = true;   // 呼出用的 Ctrl 不算"单独按 Ctrl"，避免一开就切到 Ctrl 版
    g_query.clear();
    g_sel = 0;
    g_hover = -1;
    g_bindSlot = -1;
    LoadSectors();
    SyncEditFromMode();      // 回到圆盘：收起输入框
    UpdateScrollInfo();      // 顺手把滚动条也藏掉

    POINT c;
    GetCursorPos(&c);
    const int winW = g_winW + kPadX * 2, winH = g_winH + kPadY * 2;
    // 圆盘：界面中心严格对准光标（盲甩也能选到边缘扇区：扇区矩形允许超出屏幕）
    int cx0 = c.x - g_winW / 2, cy0 = c.y - g_winH / 2;
    int x = cx0 - kPadX, y = cy0 - kPadY;

    g_shownTick = GetTickCount();
    g_focusRetry = cavFrontAtShow ? 60 : 0;
    // 先短暂置顶再取消：确保能盖住当前前台窗口，但之后仍可被其它窗口覆盖
{ HDC dc0 = GetDC(hwnd); RECT rc0 = { 0, 0, winW, winH }; HBRUSH b0 = CreateSolidBrush(RGB(0,0,0)); FillRect(dc0, &rc0, b0); DeleteObject(b0); ReleaseDC(hwnd, dc0); }
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);   // 先按"圆盘"画好，再显示
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, winW, winH, cavFrontAtShow ? SWP_SHOWWINDOW : (SWP_SHOWWINDOW | SWP_NOACTIVATE));
    SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, winW, winH, 0);
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);   // 显示后立刻补画一帧，抵消首帧残影
    ShowCatcher(false);   // 不再铺捕获层：不影响其它应用
    if (cavFrontAtShow) ForceForeground(hwnd);   // 非 Cavalry 触发：不抢焦点
    if (g_startSettings) { g_startSettings = false; EnterSettingsMode(hwnd); }
    if (g_edit && g_mode != M_SETTINGS) ShowWindow(g_edit, SW_HIDE);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    InvalidateRect(hwnd, nullptr, TRUE);
}

static void ToggleWindow(HWND hwnd) {
    if (IsWindowVisible(hwnd)) { ShowWindow(hwnd, SW_HIDE); g_mode = M_PIE; return; }
    ShowAtCursor(hwnd);
}

static void EnterSearch(wchar_t firstChar = 0) {
    g_mode = M_SEARCH;
    g_query.clear();
    g_sel = 0;
    g_scroll = 0;
    g_selAll = false;
    if (g_main) {   // 搜索面板需要完整可见：把界面拉回工作区
        RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        RECT wr; GetWindowRect(g_main, &wr);
        int nx = wr.left, ny = wr.top;
        int px = nx + kPadX, py = ny + kPadY;
        if (px < work.left) nx += work.left - px;
        if (py < work.top) ny += work.top - py;
        if (px + g_winW > work.right) nx -= (px + g_winW - work.right);
        if (py + g_winH > work.bottom) ny -= (py + g_winH - work.bottom);
        if (nx != wr.left || ny != wr.top) SetWindowPos(g_main, HWND_TOP, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    SyncEditText();
    UpdateResults();
    SyncEditFromMode();
    Log("search mode");
}

static void ExecSelected() {
    if (g_results.empty()) return;
    Item it = g_results[g_sel];

    if (g_bindSlot >= 0) {
        g_slots[g_bindSlot].name = it.name;
        g_slots[g_bindSlot].kind = it.kind;
        g_slots[g_bindSlot].value = it.value;
        if (g_slots[g_bindSlot].key.empty()) g_slots[g_bindSlot].key = kDefaultKeys[g_bindSlot];
        g_slots[g_bindSlot].used = true;
        SaveSectors();
        Log("bind slot " + std::to_string(g_bindSlot) + " -> " + Narrow(it.name));
        g_bindSlot = -1;
        g_mode = M_SETTINGS;
        InvalidateRect(g_main, nullptr, TRUE);
        return;
    }

    RunItem(it);
    InvalidateRect(g_main, nullptr, TRUE);
}

static void ShowMenu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_RELOAD, L"重新读取 actions");
    AppendMenuW(menu, MF_STRING, IDM_LOG, L"查看调试日志");
    AppendMenuW(menu, MF_STRING, IDM_INI, L"打开 Cavalry_ShortCut.ini");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出热盒");
    SetForegroundWindow(hwnd);
    g_inMenu = true;
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    g_inMenu = false;
}

static void OpenPath(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
        msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
        int mx = GET_X_LPARAM(lp) - kPadX;
        int my = GET_Y_LPARAM(lp) - kPadY;
        lp = MAKELPARAM((WORD)(short)mx, (WORD)(short)my);
    }
    switch (msg) {
    case WM_TIMER:
        if (g_editHost && !IsWindowVisible(g_main) && IsWindowVisible(g_editHost)) ShowWindow(g_editHost, SW_HIDE);
        {   // 仅当 Cavalry 或热盒自己在前台时才占用热键
            HWND fg = GetForegroundWindow();
            bool want = IsCavalryWindow(fg) || fg == hwnd || (g_main && fg == g_main) || (g_edit && fg == g_edit);
            // 保持常驻注册；是否响应在 WM_HOTKEY 里按前台判断
        }
        TickCavalryState(hwnd);
        if (g_focusRetry > 0 && IsWindowVisible(hwnd)) {
            --g_focusRetry;
            HWND fgNow = GetForegroundWindow();
            if (fgNow != hwnd && fgNow != g_edit) ForceForeground(hwnd);
        }
        {   // 触发器：Cavalry 脚本写文件 -> 呼出热盒（替代全局热键）
            static wchar_t trig[MAX_PATH] = {0};
            static FILETIME lastTrig = {0, 0};
            if (!trig[0]) {
                wchar_t* env = nullptr; size_t len = 0;
                _wdupenv_s(&env, &len, L"TEMP");
                if (env) { wcscpy_s(trig, env); wcscat_s(trig, L"\\CavalryShortCut.show"); free(env); }
            }
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            bool cavThere = AnyCavalryVisible();
            bool trigHit = cavThere && trig[0] && GetFileAttributesExW(trig, GetFileExInfoStandard, &fad) && CompareFileTime(&fad.ftLastWriteTime, &lastTrig) != 0;
            if (trigHit) {
                { HWND fgx = GetForegroundWindow(); Log(std::string("trigger hit, fg=") + DescribeWindow(fgx) + " cavFront=" + ((IsCavalryWindow(fgx) || IsWindowVisible(hwnd)) ? "1" : "0")); }
                lastTrig = fad.ftLastWriteTime;
                ShowAtCursor(hwnd);
                Log(std::string("after show: vis=") + (IsWindowVisible(hwnd) ? "1" : "0") + " fg=" + DescribeWindow(GetForegroundWindow()));
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        if (IsWindowVisible(hwnd) && g_mode == M_PIE) {
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) { ShowWindow(hwnd, SW_HIDE); return 0; }   // 右键取消
            POINT cp; GetCursorPos(&cp);
            POINT cl = cp; ScreenToClient(hwnd, &cl);
            cl.x -= kPadX; cl.y -= kPadY;   // 换算到内容坐标系
            int hv = HitTestPie(cl);
            if (hv < 0) hv = -1;
            if (hv != g_hover) { g_hover = hv; InvalidateRect(hwnd, nullptr, FALSE); }
            {
                static bool prevDown = false;
                bool nowDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (nowDown && !prevDown) Log("pie click: hover=" + std::to_string(g_hover));
                prevDown = nowDown;
            }
            if (false && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                bool outside = (cl.x < 0 || cl.y < 0 || cl.x >= g_winW || cl.y >= g_winH);
                if (g_hover >= 0 && g_slots[g_hover].used) {
                    Item it{ g_slots[g_hover].name, g_slots[g_hover].kind, g_slots[g_hover].value };
                    RunItem(it); ShowCatcher(false);
                } else if (outside) {
                    ShowWindow(hwnd, SW_HIDE);
                }
            }
        }
        if ((g_mode == M_SEARCH || g_mode == M_SETTINGS) && IsWindowVisible(hwnd) && g_edit && !g_inMenu && !g_captureHotkey) {
            if (GetFocus() != g_edit) SetFocus(g_edit);
        }
        return 0;
    case WM_CREATE:
        g_bgBrush = CreateSolidBrush(kBg);
        return 0;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MBUTTONDOWN: {
        g_dragging = true;
        SetCapture(hwnd);
        POINT pt; GetCursorPos(&pt);
        RECT wr; GetWindowRect(hwnd, &wr);
        g_dragOffset.x = pt.x - wr.left;
        g_dragOffset.y = pt.y - wr.top;
        return 0;
    }
    case WM_MBUTTONUP:
        if (g_mode == M_SEARCH || g_mode == M_SETTINGS) SyncEditFromMode();   // 拖动结束恢复输入框
        g_dragging = false;
        ReleaseCapture();
        return 0;
    case WM_SHOWWINDOW:
        if (!wp) {   // 主窗口一隐藏，原生输入框小窗必须同步隐藏，否则会留在屏幕上
            if (g_editHost) ShowWindow(g_editHost, SW_HIDE);
            if (g_scrollbar) ShowWindow(g_scrollbar, SW_HIDE);
        }
        return 0;
    case WM_ACTIVATE:
        return 0;   // 切窗口不再关闭面板，方便观察状态
    case WM_SETCURSOR: {
        POINT cp; GetCursorPos(&cp); ScreenToClient(hwnd, &cp);
        cp.x -= kPadX; cp.y -= kPadY;
        RECT pc = PanelRect();
        int ed = 0;
        if (g_mode != M_PIE && cp.x >= pc.right - 14 && cp.x <= pc.right && cp.y >= pc.top + 48 && cp.y <= pc.bottom) ed |= 1;
        if (g_mode != M_PIE && cp.y >= pc.bottom - 14 && cp.y <= pc.bottom && cp.x >= pc.left && cp.x <= pc.right) ed |= 2;
        if (ed) { SetCursor(LoadCursor(nullptr, (ed == 3) ? IDC_SIZENWSE : (ed == 1 ? IDC_SIZEWE : IDC_SIZENS))); return TRUE; }
        break;
    }
    case WM_MOUSEMOVE: {
        if (g_resizing) {
            POINT rp; GetCursorPos(&rp);
            int nw = g_winW, nh = g_winH;
            if (g_resizeEdges & 1) nw = g_resizeStartW + (rp.x - g_resizeStart.x);
            if (g_resizeEdges & 2) nh = g_resizeStartH + (rp.y - g_resizeStart.y);
            if (nw < 480) nw = 480;
            if (nh < 380) nh = 380;
            if (nw != g_winW || nh != g_winH) {
                g_winW = nw; g_winH = nh;
                SetWindowPos(hwnd, HWND_TOP, 0, 0, nw + kPadX * 2, nh + kPadY * 2, SWP_NOMOVE | SWP_NOZORDER);
                UpdateScrollInfo();
                InvalidateRect(hwnd, nullptr, FALSE);   // 不擦背景，避免闪
            }
            return 0;
        }
        if (g_mode == M_SEARCH) {
            POINT mp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hr = -1;
            {
                RECT first = RowRect(0);
                RECT area = { first.left, first.top, first.right, first.bottom + 30 * (VisibleRows() - 1) + 6 };
                if (mp.x >= area.left && mp.x < area.right && mp.y >= area.top && mp.y < area.bottom) {
                    int idx = (mp.y - first.top) / 30;
                    if (idx >= 0 && idx < VisibleRows()) hr = g_scroll + idx;
                }
            }
            if (hr != g_hoverRow) { g_hoverRow = hr; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        if (g_dragging) {
            POINT pt; GetCursorPos(&pt);
            int nx = pt.x - g_dragOffset.x, ny = pt.y - g_dragOffset.y;
            RECT wr0; GetWindowRect(hwnd, &wr0);
            SetWindowPos(hwnd, HWND_TOP, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            if (g_editHost && IsWindowVisible(g_editHost)) {   // 拖动期间先藏起输入框，避免闪烁
                ShowWindow(g_editHost, SW_HIDE);
                return 0;
            }
            if (false) {
                int dxw = nx - wr0.left, dyw = ny - wr0.top;
                RECT hwr; GetWindowRect(g_editHost, &hwr);
                SetWindowPos(g_editHost, nullptr, hwr.left + dxw, hwr.top + dyw, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g_mode == M_PIE) {
            int h = HitTestPie(p);
            int hv = (h >= 0) ? h : -1;
            if (hv != g_hover) { g_hover = hv; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT rp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g_mode == M_SEARCH) {
            RECT fr = RowRect(0);
            int rowHit = (rp.y - fr.top) / 30;   // 行高 30
            int idx2 = g_scroll + rowHit;
            RECT rr0 = RowRect(rowHit >= 0 ? rowHit : 0);
            if (rp.x >= rr0.left && rp.x < rr0.right && rp.y >= rr0.top && rp.y < rr0.bottom && rowHit >= 0 && rowHit < VisibleRows() && idx2 >= 0 && idx2 < (int)g_results.size()) {
                g_sel = idx2;
                HMENU hm = CreatePopupMenu();
                AppendMenuW(hm, MF_STRING, 1001, L"绑定到热盒…");
                std::wstring pinKey = g_results[idx2].kind + L"|" + g_results[idx2].value;
                bool isPinned = false;
                for (auto& k : g_pinned) if (k == pinKey) { isPinned = true; break; }
                AppendMenuW(hm, MF_STRING, 1002, isPinned ? L"取消置顶" : L"置顶");
                POINT sp; GetCursorPos(&sp);
                ForceForeground(hwnd);   // 输入框宿主可能占着前台，用 AttachThreadInput 版强制切回
                g_inMenu = true;
                int cmd = (int)TrackPopupMenu(hm, TPM_RETURNCMD | TPM_RIGHTBUTTON, sp.x, sp.y, 0, hwnd, nullptr);
                g_inMenu = false;
                DestroyMenu(hm);
                PostMessageW(hwnd, WM_NULL, 0, 0);   // 让菜单正常关闭（避免焦点锁导致一闪就没）
                if (cmd == 1002) {
                    std::wstring pk = g_results[idx2].kind + L"|" + g_results[idx2].value;
                    bool found = false;
                    for (size_t q = 0; q < g_pinned.size(); ++q) if (g_pinned[q] == pk) { g_pinned.erase(g_pinned.begin() + q); found = true; break; }
                    if (!found) g_pinned.push_back(pk);
                    {
                        std::string raw;
                        for (auto& k : g_pinned) raw += Narrow(k) + "\n";
                        WriteFileUtf8(ExeDir() + L"\\Cavalry_ShortCut_pinned.txt", raw);
                    }
                    UpdateResults();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                if (cmd == 1001) {
                    g_bindItem = g_results[idx2];
                    g_binding = true;
                    ShowAtCursor(hwnd);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                return 0;
            }
        }
        ShowWindow(hwnd, SW_HIDE);
        g_mode = M_PIE;
        SyncEditFromMode();
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (g_mode == M_SEARCH) {   // 页签拖动排序
            POINT tp0 = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int pos0 = TabPosFromPoint(tp0);
            if (pos0 >= 0) { g_tabDragFrom = pos0; SetCapture(hwnd); return 0; }
        }
        {
            int ex = (int)GET_X_LPARAM(lp), ey = (int)GET_Y_LPARAM(lp);
            int edges = 0;
            RECT prr0 = PanelRect();
            if (g_mode != M_PIE && ex >= prr0.right - 14 && ex <= prr0.right && ey >= prr0.top + 48 && ey <= prr0.bottom) edges |= 1;
            if (g_mode != M_PIE && ey >= prr0.bottom - 14 && ey <= prr0.bottom && ex >= prr0.left && ex <= prr0.right) edges |= 2;
            if (edges) {
                g_resizing = true;
                g_resizeEdges = edges;
                GetCursorPos(&g_resizeStart);
                g_resizeStartW = g_winW;
                g_resizeStartH = g_winH;
                SetCapture(hwnd);
                return 0;
            }
        }
        if (g_mode == M_SEARCH && (int)g_results.size() > 7) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT th = ScrollThumbRect();
            if (p.x >= th.left - 4 && p.x < th.right + 4 && p.y >= th.top && p.y < th.bottom) {
                g_thumbDrag = true;
                g_dragStartY = p.y;
                g_dragStartScroll = g_scroll;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            RECT tr = ScrollTrackRect();
            if (p.x >= tr.left - 6 && p.x < tr.right + 6 && p.y >= tr.top && p.y < tr.bottom) {
                g_scroll += (p.y < th.top) ? -VisibleRows() : VisibleRows();
                UpdateScrollInfo();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (g_tabDragFrom >= 0) {
            POINT tp1 = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int pos1 = TabPosFromPoint(tp1);
            if (pos1 == g_tabDragFrom || pos1 < 0) g_tab = g_tabOrder[g_tabDragFrom];
            SaveIni();
            g_tabDragFrom = -1;
            ReleaseCapture();
            UpdateResults();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_resizing) { g_resizing = false; ReleaseCapture(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        // 页签点击（圆盘模式）
        if (g_mode == M_PIE) {
            for (int i = 0; i < 4; ++i) {
                RECT br = TabBoxRect(i);
                if (p.x >= br.left && p.x < br.right && p.y >= br.top && p.y < br.bottom) { SwitchSet(i, g_mod); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
                RECT mr = TabModRect(i);
                if (p.x >= mr.left && p.x < mr.right && p.y >= mr.top && p.y < mr.bottom) { SwitchSet(g_box, i); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            }
        }
        // 图标点击：两个独立命中框（左=文件夹，右=设置）
        // 图标点击：仅设置（右上角齿轮）
        if (g_mode != M_SETTINGS) {   // 设置页里这个位置是关闭按钮，不能再当齿轮
            RECT gear = IconRect(0);
            InflateRect(&gear, 8, 10);
            if (p.x >= gear.left && p.x < gear.right && p.y >= gear.top && p.y < gear.bottom) {
                EnterSettingsMode(hwnd);
                g_sel = 0;
                g_query.clear(); g_scroll = 0; g_selSearch = 0; UpdateResults(); SyncEditFromMode();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
        }

        // 搜索页签
        if (g_mode == M_SEARCH) {
            for (int i = 0; i < kTabCount; ++i) {
                RECT tr = TabRect(i);
                if (p.x >= tr.left && p.x < tr.right && p.y >= tr.top && p.y < tr.bottom) {
                    g_tab = g_tabOrder[i];
                    g_sel = 0;
                    UpdateResults();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
        }

        if (g_mode == M_PIE) {
            int h = HitTestPie(p);
            if (g_binding) {
                if (h >= 0) {
                    g_slots[h].name = g_bindItem.name;
                    g_slots[h].kind = g_bindItem.kind;
                    g_slots[h].value = g_bindItem.value;
                    g_slots[h].key = std::to_wstring(h + 1);
                    g_slots[h].used = true;
                    SaveSectors();
                    Log("bound to sector " + std::to_string(h));
                }
                g_binding = false;
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (h == -2) { ShowWindow(hwnd, SW_HIDE); return 0; }
            if (h >= 0 && g_slots[h].used) {
                Item it{ g_slots[h].name, g_slots[h].kind, g_slots[h].value };
                RunItem(it);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        } else if (g_mode == M_SEARCH) {
            {   // 点击搜索面板以外（含透明输入区）= 关闭面板
                RECT pr0 = PanelRect();
                POINT cp0 = { p.x, p.y };
                if (cp0.x < pr0.left || cp0.x >= pr0.right || cp0.y < pr0.top || cp0.y >= pr0.bottom) { HideHotbox(hwnd); return 0; }
            }
    for (int row = 0; row < VisibleRows(); ++row) {
        int i = g_scroll + row;
        if (i >= (int)g_results.size()) break;
        RECT r = RowRect(row);
                if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) {
                    g_sel = (int)i;
                    ExecSelected();
                    return 0;
                }
            }
        } else if (g_mode == M_SETTINGS) {
            {   // 页签点击（与圆盘一致）
                POINT tp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                for (int i = 0; i < 4; ++i) {
                    RECT br = TabBoxRect(i);
                    if (tp.x >= br.left && tp.x < br.right && tp.y >= br.top && tp.y < br.bottom) { SaveActive(); SwitchSet(i, g_mod); LoadActive(); g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
                    RECT mr = TabModRect(i);
                    if (tp.x >= mr.left && tp.x < mr.right && tp.y >= mr.top && tp.y < mr.bottom) { SaveActive(); SwitchSet(g_box, i); LoadActive(); g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
                }
            }
            {
                POINT mp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                RECT p = PanelRect();
                int bw = 76, bh = 26, gap = 10;
                RECT okB = { p.right - 16 - bw, p.bottom - 16 - bh, p.right - 16, p.bottom - 16 };
                RECT caB = { okB.left - gap - bw, okB.top, okB.left - gap, okB.bottom };
                if (mp.x >= okB.left && mp.x < okB.right && mp.y >= okB.top && mp.y < okB.bottom) {
                    SaveSectors(); ExitSettingsMode(hwnd); HideHotbox(hwnd); return 0;
                }
                if (mp.x >= caB.left && mp.x < caB.right && mp.y >= caB.top && mp.y < caB.bottom) {
                    LoadSectors(); ExitSettingsMode(hwnd); HideHotbox(hwnd); return 0;
                }
            }
            {   // 右上角 × ：关闭设置页
                RECT xr2 = IconRect(0);
                InflateRect(&xr2, 14, 14);   // 放大判定框，好点中
                if (p.x >= xr2.left && p.x < xr2.right && p.y >= xr2.top && p.y < xr2.bottom) {
                    ExitSettingsMode(hwnd);
                    HideHotbox(hwnd);   // 直接关闭热盒
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
            {   // 呼出快捷键行
                RECT hk = SetHotkeyRect();
                if (p.x >= hk.left && p.x < hk.right && p.y >= hk.top && p.y < hk.bottom) {
                    g_captureHotkey = true;
                    SetFocus(hwnd);   // 焦点交给主窗口，否则按键会打进输入框
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
            {   // 分类页签点击
                int tp = SetTabPosFromPoint(p);
                if (tp >= 0) { g_tab = g_tabOrder[tp]; g_selSearch = 0; g_scroll = 0; UpdateResults(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            }
            for (int i = 0; i < 8; ++i) {   // 左侧环状热盒：点击选中
                RECT rr0 = SetRingRect(i);
                if (p.x >= rr0.left && p.x < rr0.right && p.y >= rr0.top && p.y < rr0.bottom) { g_sel = i; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            }
            for (int row = 0; row < 12; ++row) {   // 右侧结果：点击绑定到选中扇区
                RECT rr1 = SetResultRect(row);
                if (p.x >= rr1.left && p.x < rr1.right && p.y >= rr1.top && p.y < rr1.bottom) {
                    int idx = g_scroll + row;
                    if (idx >= 0 && idx < (int)g_results.size()) {
                        Item& it = g_results[idx];
                        g_slots[g_sel].name = it.name;
                        g_slots[g_sel].kind = it.kind;
                        g_slots[g_sel].value = it.value;
                        if (g_slots[g_sel].key.empty()) g_slots[g_sel].key = kDefaultKeys[g_sel];   // 绑定不改已有快捷键
                        g_slots[g_sel].used = true;
                        SaveSectors();
                        Log("bind sector " + std::to_string(g_sel) + " -> " + Narrow(it.name));
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                    return 0;
                }
            }
        }
        if (g_mode == M_PIE && g_hover >= 0 && g_slots[g_hover].used) { Item it2; it2.name = g_slots[g_hover].name; it2.kind = g_slots[g_hover].kind; it2.value = g_slots[g_hover].value; it2.cat = g_slots[g_hover].kind; RunItem(it2); return 0; }   // 空白处左键 = 确认悬停项
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, kText);
        SetBkColor(dc, RGB(22, 22, 24));
        static HBRUSH br = CreateSolidBrush(RGB(22, 22, 24));
        return (LRESULT)br;
    }

    case WM_CHAR: {
        if (g_swallowChar) {   // 捕获快捷键时按下的那个字符，不要再打进输入框
            g_swallowChar = false;
            if (g_edit) SetWindowTextW(g_edit, g_query.c_str());
            return 0;
        }
        if (g_mode == M_SEARCH || g_mode == M_SETTINGS) {
            if (g_selAll) { g_query.clear(); g_selAll = false; if (g_edit) SetWindowTextW(g_edit, L""); }
            if (g_edit) {   // 消息能到主窗口，就说明字符是冲我们来的，一律转交
                SendMessageW(g_edit, WM_CHAR, wp, lp);
                SendMessageW(g_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
            }
        }
        return 0;
    }

    case WM_VSCROLL: {
        if ((HWND)lp == g_scrollbar) {
            int pos = g_scroll;
            switch (LOWORD(wp)) {
                case SB_LINEUP:   pos -= 1; break;
                case SB_LINEDOWN: pos += 1; break;
                case SB_PAGEUP:   pos -= VisibleRows(); break;
                case SB_PAGEDOWN: pos += VisibleRows(); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = HIWORD(wp); break;
            }
            g_scroll = pos;
            UpdateScrollInfo();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT mp; GetCursorPos(&mp); ScreenToClient(hwnd, &mp);
        mp.x -= kPadX; mp.y -= kPadY;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (g_mode == M_PIE) {
            bool onModRow = false;
            for (int i = 0; i < 4; ++i) {
                RECT mr = TabModRect(i);
                if (mp.x >= mr.left && mp.x < mr.right && mp.y >= mr.top && mp.y < mr.bottom) {
                    SwitchSet(g_box, (g_mod + (delta > 0 ? 3 : 1)) % 4); InvalidateRect(hwnd, nullptr, TRUE); return 0;
                }
            }
            (void)onModRow;
            SwitchSet((g_box + (delta > 0 ? 3 : 1)) % 4, g_mod);   // 其它位置：滚轮切热盒
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_mode == M_SEARCH) {
            bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool onTabRow = false;
            for (int i = 0; i < kTabCount; ++i) {
                RECT tr = TabRect(i);
                if (mp.x >= tr.left && mp.x < tr.right && mp.y >= tr.top && mp.y < tr.bottom) { onTabRow = true; break; }
            }
            if (shiftDown || onTabRow) {   // Shift+滚轮（任意位置）或滚轮落在页签行上：切页签
                int pos = 0;
                for (int i = 0; i < kTabCount; ++i) if (g_tabOrder[i] == g_tab) { pos = i; break; }
                pos = (pos + (delta > 0 ? kTabCount - 1 : 1)) % kTabCount;
                g_tab = g_tabOrder[pos];
                g_sel = 0; g_scroll = 0;
                UpdateResults();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            g_scroll -= (delta / WHEEL_DELTA) * 3;
            UpdateScrollInfo();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_mode == M_SETTINGS) {   // 设置页：滚轮翻页签（页签行上或按住 Shift）
            bool onTabs2 = (SetTabPosFromPoint(mp) >= 0);
            if (onTabs2 || (GetKeyState(VK_SHIFT) & 0x8000)) {
                int pos = 0;
                for (int i = 0; i < kTabCount; ++i) if (g_tabOrder[i] == g_tab) { pos = i; break; }
                pos = (pos + (delta > 0 ? kTabCount - 1 : 1)) % kTabCount;
                g_tab = g_tabOrder[pos];
                g_selSearch = 0; g_scroll = 0;
                UpdateResults();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            return 0;
        }
        break;
    }
    case WM_KEYUP:
        if (g_mode == M_PIE && wp == VK_CONTROL) {
            if (!g_ctrlUsed) SwitchSet(g_box, (g_mod == 1) ? 0 : 1);
            g_ctrlUsed = false;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        if (g_mode == M_PIE && wp == VK_MENU) { SwitchSet(g_box, (g_mod == 3) ? 0 : 3); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        break;
    case WM_KEYDOWN: {
        if (g_captureHotkey) {
            UINT vk = (UINT)wp;
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_ESCAPE) { g_captureHotkey = false; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            std::wstring nm = VkNameFromVk(vk);
            if (!nm.empty()) {
                std::wstring spec = nm;   // 扇区快捷键：单个键
                g_slots[g_sel].key = spec;
                g_swallowChar = true;
                g_captureHotkey = false;
                SaveSectors();
                Log("captured sector key " + Narrow(spec));
                SyncEditFromMode();   // 恢复输入框焦点
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        if (g_mode == M_PIE && (wp == VK_OEM_3 || wp == VK_SPACE)) { PostMessageW(hwnd, WM_APP + 7, 0, 0); return 0; }   // ` 或空格确认圆盘项
        if (g_mode == M_SEARCH) {
            bool c = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool s = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (c && wp >= '1' && wp <= '6') { g_sel = g_scroll + (int)(wp - '1'); ExecSelected(); return 0; }
            if (wp == VK_TAB) { g_tab = (g_tab + (s ? kTabCount - 1 : 1)) % kTabCount; g_sel = 0; g_scroll = 0; UpdateResults(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        }
        if (g_mode == M_SEARCH && (GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'A')) {
            if (g_edit) { SetFocus(g_edit); SendMessageW(g_edit, EM_SETSEL, (WPARAM)0, (LPARAM)-1); }
            return 0;   // 吞掉，避免系统提示音
        }
        if (g_mode == M_PIE || g_mode == M_SETTINGS) {
            if (wp >= '1' && wp <= '4') {   // 直接 1~4 切热盒（不再需要 Ctrl）
                if (g_mode == M_SETTINGS) SaveActive();
                SwitchSet(wp - '1', g_mod);
                if (g_mode == M_SETTINGS) { LoadActive(); g_sel = 0; }
                InvalidateRect(hwnd, nullptr, TRUE); return 0;
            }
            if (wp == VK_CONTROL) { g_ctrlUsed = false; return 0; }   // Ctrl：等松开时再判断是否单独按
            if (wp == VK_SHIFT)   { if (g_mode == M_SETTINGS) SaveActive(); SwitchSet(g_box, (g_mod == 2) ? 0 : 2); if (g_mode == M_SETTINGS) { LoadActive(); g_sel = 0; } InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_MENU)    { SwitchSet(g_box, (g_mod == 3) ? 0 : 3); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        }
        if (wp == VK_ESCAPE) {
            if (g_mode == M_SETTINGS) { ExitSettingsMode(hwnd); HideHotbox(hwnd); return 0; }
            if (g_binding) { g_binding = false; ShowWindow(hwnd, SW_HIDE); return 0; }
            if (g_mode == M_SEARCH && g_bindSlot >= 0) { g_mode = M_SETTINGS; g_bindSlot = -1; }
            else if (g_mode != M_PIE) { g_mode = M_PIE; }
            else { HideHotbox(hwnd); }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_mode == M_PIE) {
            if (wp == VK_TAB) { EnterSearch(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_F2) { EnterSettingsMode(hwnd); g_sel = 0; g_query.clear(); g_scroll = 0; g_selSearch = 0; UpdateResults(); SyncEditFromMode(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            // 1~8 或自定义键
            for (int i = 0; i < 8; ++i) {
                if (!g_slots[i].used) continue;
                std::wstring k = g_slots[i].key.empty() ? std::to_wstring(i + 1) : g_slots[i].key;
                if ((UINT)wp == (UINT)towupper(k[0]) || (k.size() == 1 && wp == k[0])) {
                    Item it{ g_slots[i].name, g_slots[i].kind, g_slots[i].value };
                    RunItem(it);
                    return 0;
                }
            }
            // 字母 → 搜索（且不是已绑定的扇区键）
            if (wp >= 'A' && wp <= 'Z') { EnterSearch((wchar_t)towlower(wp)); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        } else if (g_mode == M_SEARCH) {
            if (wp == VK_DOWN) { if (g_sel + 1 < (int)g_results.size()) ++g_sel; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_UP)   { if (g_sel > 0) --g_sel; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_BACK) {
                if (!g_query.empty()) g_query.pop_back();
                UpdateResults();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (wp == VK_RETURN) { ExecSelected(); return 0; }
        } else if (g_mode == M_SETTINGS) {
            {   // 页签点击（与圆盘一致）
                POINT tp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                for (int i = 0; i < 4; ++i) {
                    RECT br = TabBoxRect(i);
                    if (tp.x >= br.left && tp.x < br.right && tp.y >= br.top && tp.y < br.bottom) { SaveActive(); SwitchSet(i, g_mod); LoadActive(); g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
                    RECT mr = TabModRect(i);
                    if (tp.x >= mr.left && tp.x < mr.right && tp.y >= mr.top && tp.y < mr.bottom) { SaveActive(); SwitchSet(g_box, i); LoadActive(); g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
                }
            }
            {
                POINT mp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                RECT p = PanelRect();
                int bw = 76, bh = 26, gap = 10;
                RECT okB = { p.right - 16 - bw, p.bottom - 16 - bh, p.right - 16, p.bottom - 16 };
                RECT caB = { okB.left - gap - bw, okB.top, okB.left - gap, okB.bottom };
                if (mp.x >= okB.left && mp.x < okB.right && mp.y >= okB.top && mp.y < okB.bottom) {
                    SaveSectors(); ExitSettingsMode(hwnd); HideHotbox(hwnd); return 0;
                }
                if (mp.x >= caB.left && mp.x < caB.right && mp.y >= caB.top && mp.y < caB.bottom) {
                    LoadSectors(); ExitSettingsMode(hwnd); HideHotbox(hwnd); return 0;
                }
            }
            if (wp == VK_DOWN) { if (g_sel < 7) ++g_sel; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_UP)   { if (g_sel > 0) --g_sel; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_RETURN) { g_bindSlot = g_sel; EnterSearch(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_DELETE) {
                g_slots[g_sel] = Slot();
                SaveSectors();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
        }
        return 0;
    }
    case WM_APP + 8:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_APP + 7:
        if (g_mode == M_SEARCH) {
            ExecSelected();   // ` 执行搜索选中项
            return 0;
        }
        if (g_mode == M_PIE) {
            if (g_hover >= 0 && g_slots[g_hover].used) {
                Item it{ g_slots[g_hover].name, g_slots[g_hover].kind, g_slots[g_hover].value };
                RunItem(it);
            } else {
                ShowWindow(hwnd, SW_HIDE);
            }
        }
        return 0;
    case WM_APP + 5:
        if (g_mode == M_SEARCH && wp >= 1 && wp <= 6) {
            g_sel = g_scroll + (int)wp - 1;
            ExecSelected();
        }
        return 0;
    case WM_APP + 6:
        if (g_mode == M_SEARCH) {
            g_tab = (g_tab + ((wp == 1) ? kTabCount - 1 : 1)) % kTabCount;
            g_sel = 0; g_scroll = 0;
            UpdateResults();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_APP + 1:
        ShowAtCursor(hwnd);
        return 0;
    case WM_HOTKEY:
        if ((int)wp == 2 && g_mode != M_PIE) return 0;   // 搜索/设置里空格归输入
        Log("WM_HOTKEY id=" + std::to_string((int)wp));
        { HWND fgc = GetForegroundWindow(); if (!IsCavalryWindow(fgc) && !IsWindowVisible((HWND)(g_main ? g_main : hwnd))) return 0; }
        if (wp == 2) { PostMessageW(hwnd, WM_APP + 7, 0, 0); return 0; }   // 空格确认
        if (wp == HOTKEY_TOGGLE) {
            HWND target = g_main ? g_main : hwnd;
            bool visible = IsWindowVisible(target);
            HWND fg = GetForegroundWindow();
            bool onCavalry = IsCavalryWindow(fg) || fg == target || fg == g_edit;
            if (visible) { ShowWindow(target, SW_HIDE); }          // 已打开：热键=收起
            else if (onCavalry) { ShowAtCursor(target); }           // 仅在 Cavalry 前台时呼出
        }
        return 0;
    case WM_COMMAND: {
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE) {
            wchar_t buf[256] = {0};
            GetWindowTextW(g_edit, buf, 255);
            g_query = buf;
            g_selAll = false;   // 文本变化即取消全选高亮
            g_sel = 0; g_scroll = 0;
            UpdateResults();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (LOWORD(wp)) {
            case IDM_SETTINGS: EnterSettingsMode(hwnd); g_sel = 0; g_query.clear(); g_scroll = 0; g_selSearch = 0; UpdateResults(); SyncEditFromMode(); InvalidateRect(hwnd, nullptr, TRUE); return 0;
            case IDM_ACTIONS: OpenPath(ExeDir() + L"\\actions"); return 0;
            case IDM_RELOAD: LoadSectors(); LoadIndex(); InvalidateRect(hwnd, nullptr, TRUE); return 0;
            case IDM_LOG: OpenPath(ExeDir() + L"\\Cavalry_ShortCut.log"); return 0;
            case IDM_INI: OpenPath(ExeDir() + L"\\Cavalry_ShortCut.ini"); return 0;
            case IDM_EXIT: DestroyWindow(hwnd); return 0;
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- 入口
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    bool showOnStart = (cmdLine && wcsstr(cmdLine, L"--show") != 0);
    if (cmdLine && wcsstr(cmdLine, L"--show-force")) g_forceShow = true;
    if (cmdLine && wcsstr(cmdLine, L"--settings")) g_startSettings = true;
    {   // 单实例：已有实例则退出（避免多份窗口抢热键）
        HANDLE mtx = CreateMutexW(nullptr, TRUE, L"CavalryShortCut_Single");
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
            if (cmdLine && wcsstr(cmdLine, L"--show")) {
                HWND ex = FindWindowW(L"CavalryShortCut", L"Cavalry ShortCut");
                if (ex) PostMessageW(ex, WM_APP + 1, 0, 0);
            }
            return 0;
        }
    }
    if (cmdLine && wcsstr(cmdLine, L"--show")) {
        HWND existing = FindWindowW(L"CavalryShortCut", L"Cavalry ShortCut");
        if (existing) { PostMessageW(existing, WM_APP + 1, 0, 0); return 0; }
    }

    bool alive = BridgeAlive();
    if (!alive) alive = StartBridge();
    Log(std::string("bridge ") + (alive ? "ok" : "not connected"));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CavalryShortCut";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                L"CavalryShortCut", L"Cavalry ShortCut", WS_POPUP | WS_CLIPCHILDREN,
                                0, 0, g_winW, g_winH, nullptr, nullptr, inst, nullptr);
    g_main = hwnd;
    CreateEdit(hwnd, inst);
    ImmAssociateContext(hwnd, nullptr);   // 主窗口不吃输入法；输入框仍可用中文
    // 输入法保持启用：中文搜索需要它

    InitIcons();
    LoadConfig();
    LoadIndex();
    LoadOps();
        Log("reload: " + std::to_string(g_index.size()) + " items, ops=" + std::to_string(g_opsFiles.size()));
    {
        std::string raw = ReadFileUtf8(ExeDir() + L"\\Cavalry_ShortCut_pinned.txt");
        size_t st = 0;
        while (st < raw.size()) {
            size_t en = raw.find('\n', st);
            if (en == std::string::npos) en = raw.size();
            std::string line = raw.substr(st, en - st);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) g_pinned.push_back(Widen(line));
            st = en + 1;
        }
    }
    LoadSectors();

    HWND msgWnd = CreateWindowExW(0, L"CavalryShortCut", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    g_msgWndGlobal = msgWnd;
    BOOL hk = TRUE;   // 热键改为按需注册（仅 Cavalry 前台）
    if (g_hotkeyVk != 0) SetHotkeyEnabled(true);   // hotkey=off 时不注册（脚本呼出）
    Log("hotkey " + Narrow(g_hotkeySpec) + " -> " + (hk ? "ok" : ("FAIL err=" + std::to_string(GetLastError()))));

    SetTimer(hwnd, 1, 5, nullptr);   // 5ms
    if (showOnStart) PostMessageW(hwnd, WM_APP + 1, 0, 0);

    WNDCLASSW cc = {};
    cc.lpfnWndProc = CatcherProc;
    cc.hInstance = inst;
    cc.lpszClassName = L"CavalryShortCutCatcher";
    RegisterClassW(&cc);
    g_catcher = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"CavalryShortCutCatcher", L"", WS_POPUP,
                                0, 0, 100, 100, nullptr, nullptr, inst, nullptr);
    if (g_catcher) SetLayeredWindowAttributes(g_catcher, 0, 1, LWA_ALPHA);   // 几乎全透明但能收点击

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
