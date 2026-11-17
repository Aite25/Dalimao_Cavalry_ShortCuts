// hotbox.cpp — Cavalry 径向热盒 v6
//
// 参考 AE 插件 DalimaoShortcuts 的热盒：
//   · 径向 8 扇区（N/NE/E/SE/S/SW/W/NW）+ 中心圆
//   · 鼠标移出中心 → 扇区高亮，中心画蓝色弧线
//   · 扇区热键（1~8 或自定义字母）+ 搜索菜单（Tab / 直接打字）+ 设置菜单（改绑定）
//
// 数据：
//   hotbox.ini   → 呼出快捷键等
//   hotbox.json  → 8 个扇区的绑定（脚本文件 或 添加图层）
//   actions/*.js → 脚本动作本体（点的时候才读 → 热更新）
//
// 触发：hotbox.ini 里的 hotkey（默认 Alt+Q）；Cavalry 脚本可用 hotbox.exe --show。

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winhttp.h>
#include <windowsx.h>
#include <shellapi.h>
#include <imm.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "imm32.lib")

// ---------------------------------------------------------------- 常量
static const int kBridgePort = 6769;
static int g_winW = 640;
static int g_winH = 560;

static const COLORREF kBg          = RGB(0, 0, 0);        // 色键透明
static const COLORREF kSectorFill  = RGB(35, 35, 35);
static const COLORREF kSectorSel    = RGB(77, 77, 77);
static const COLORREF kSectorBorder = RGB(95, 95, 95);
static const COLORREF kText         = RGB(255, 255, 255);
static const COLORREF kTextDim      = RGB(200, 200, 200);
static const COLORREF kCenterFill   = RGB(38, 38, 38);
static const COLORREF kCenterBorder = RGB(31, 31, 31);
static const COLORREF kArcColor     = RGB(56, 119, 181);
static const COLORREF kPanelFill    = RGB(30, 30, 32);
static const COLORREF kPanelBorder  = RGB(70, 70, 74);
static const COLORREF kRowSel       = RGB(56, 119, 181);

static const int kCenterR  = 36;
static const int kRectW    = 150;
static const int kRectH    = 44;
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
static Slot g_slots[8];

struct Item {
    std::wstring name;
    std::wstring kind;
    std::wstring value;
    std::wstring cat;      // 脚本 / 形状 / 行为 / 工具 / 效果
};
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
static bool g_imeOn = true;   // 输入框是否启用输入法（hotbox.ini: ime=off 可关）
static bool g_dragging = false;
static bool g_selAll = false;
static void SyncEditText();
static POINT g_dragOffset = {0, 0};

static std::vector<Item> g_results;
static void SyncEditFromMode();
static void UpdateScrollInfo();
static int g_tab = 0;
static const wchar_t* kTabs[] = { L"全部", L"脚本", L"形状", L"行为", L"工具", L"效果" };
static const int kTabCount = 6;
static HWND g_edit = nullptr;
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

static void Log(const std::string& s) {
    std::wstring path = ExeDir() + L"\\hotbox-debug.log";
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
static bool HttpCall(const wchar_t* method, const std::wstring& path, const std::string& body,
                     std::string& out, DWORD& status) {
    out.clear();
    status = 0;

    HINTERNET session = WinHttpOpen(L"cavalry-hotbox/0.6", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 3000, 3000, 8000, 60000);

    HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", (INTERNET_PORT)kBridgePort, 0);
    if (!connect) { WinHttpCloseHandle(session); return false; }

    HINTERNET request = WinHttpOpenRequest(connect, method, path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }

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
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
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

static void RunScriptText(const std::string& script) {
    if (script.empty()) return;
    std::string body = "{\"name\":\"execute_script\",\"args\":{\"script\":\"" + EscapeJson(script) + "\"}}";
    std::string out;
    DWORD st = 0;
    if (!HttpCall(L"POST", L"/call", body, out, st)) Log("bridge call failed");
    else Log("ran script (" + std::to_string(script.size()) + " bytes) -> " +
             (out.find("\"isError\":false") != std::string::npos ? "ok" : "error"));
}

static void RunItem(const Item& it) {
    if (it.kind == L"script") {
        RunScriptText(ReadFileUtf8(ExeDir() + L"\\actions\\" + it.value));
    } else if (it.kind == L"layer") {
        RunScriptText("(function(){var id=api.create(\"" + Narrow(it.value) + "\"); if(id) api.select([id]);})();");
    }
    ShowWindow(g_main, SW_HIDE);
    g_mode = M_PIE;
    SyncEditFromMode();
    g_query.clear();
}

static HFONT MakeFont(int px, bool bold = false);
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
        if (ctrl && w == 'A') { SendMessageW(h, EM_SETSEL, (WPARAM)0, (LPARAM)-1); return 0; }
        if (ctrl && w >= '1' && w <= '6') { PostMessageW(g_main, WM_APP + 5, (WPARAM)(w - '0'), 0); return 0; }
        if (w == VK_TAB) { PostMessageW(g_main, WM_APP + 6, (WPARAM)(shift ? 1 : 0), 0); return 0; }
        if (w == VK_ESCAPE || w == VK_RETURN || w == VK_UP || w == VK_DOWN) {
            PostMessageW(g_main, WM_KEYDOWN, w, 0);
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}

static void CreateEdit(HWND parent, HINSTANCE inst) {
    g_edit = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
                             0, 0, 10, 10, parent, (HMENU)ID_EDIT, inst, nullptr);
    if (!g_edit) return;
    HFONT f = MakeFont(16);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)f, TRUE);
    if (!g_imeOn) ImmAssociateContext(g_edit, nullptr);   // ime=off：字母直通，不打拼音
    SetWindowSubclass(g_edit, EditSubclass, 1, 0);

    g_scrollbar = CreateWindowExW(0, L"SCROLLBAR", nullptr,
                                  WS_CHILD | SBS_VERT,
                                  0, 0, 10, 10, parent, (HMENU)ID_SCROLL, inst, nullptr);
}

static RECT InputRect();
static void SyncEditFromMode() {
    if (!g_edit) return;
    if (g_mode == M_SEARCH) {
        RECT ir = InputRect();
        SetWindowPos(g_edit, HWND_TOP, ir.left + 8, ir.top + 6,
                     ir.right - ir.left - 16, ir.bottom - ir.top - 10, SWP_SHOWWINDOW);
        {
            RECT pr = { 40, 60, g_winW - 40, g_winH - 60 };   // 与 PanelRect 一致
            SetWindowPos(g_scrollbar, HWND_TOP, pr.right - 30, pr.top + 110,
                         14, 7 * 38 - 4, SWP_NOZORDER);
            UpdateScrollInfo();
        }
        SetFocus(g_edit);
    } else {
        ShowWindow(g_edit, SW_HIDE);
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
// ---------------------------------------------------------------- 配置 hotbox.json
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
static void LoadSectors() {
    std::wstring path = ExeDir() + L"\\hotbox.json";
    std::string raw = ReadFileUtf8(path);

    if (!raw.empty()) {
        // 极简解析：按 "name" 分段取 name/kind/value/key
        size_t pos = 0;
        int idx = 0;
        while (idx < 8 && (pos = raw.find("\"name\"", pos)) != std::string::npos) {
            size_t segEnd = raw.find("}", pos);
            std::string seg = raw.substr(pos, segEnd == std::string::npos ? std::string::npos : segEnd - pos);
            g_slots[idx].name = JsonGetStr(seg, "name");
            g_slots[idx].kind = JsonGetStr(seg, "kind");
            g_slots[idx].value = JsonGetStr(seg, "value");
            g_slots[idx].key = JsonGetStr(seg, "key");
            g_slots[idx].used = !g_slots[idx].value.empty();
            if (g_slots[idx].kind.empty()) g_slots[idx].kind = L"script";
            ++idx;
            pos = segEnd == std::string::npos ? raw.size() : segEnd;
        }
        return;
    }

    // 没有配置 → 用 actions 目录生成默认绑定
    std::wstring dir = ExeDir() + L"\\actions";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*.js").c_str(), &fd);
    std::vector<Item> scripts;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            Item it;
            it.name = name.substr(0, name.find_last_of(L'.'));
            it.kind = L"script";
            it.value = name;
            scripts.push_back(it);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(scripts.begin(), scripts.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
    for (size_t i = 0; i < scripts.size() && i < 8; ++i) {
        g_slots[i].name = scripts[i].name;
        g_slots[i].kind = scripts[i].kind;
        g_slots[i].value = scripts[i].value;
        g_slots[i].key = std::to_wstring(i + 1);
        g_slots[i].used = true;
    }
    SaveSectors();
}

static void SaveSectors() {
    std::string out = "{\n  \"sectors\": [\n";
    for (int i = 0; i < 8; ++i) {
        out += "    {\"name\":\"" + EscapeJson(Narrow(g_slots[i].name)) + "\", "
               "\"kind\":\"" + EscapeJson(Narrow(g_slots[i].kind)) + "\", "
               "\"value\":\"" + EscapeJson(Narrow(g_slots[i].value)) + "\", "
               "\"key\":\"" + EscapeJson(Narrow(g_slots[i].key)) + "\"}";
        out += (i < 7) ? ",\n" : "\n";
    }
    out += "  ]\n}\n";
    WriteFileUtf8(ExeDir() + L"\\hotbox.json", out);
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
        if (!MatchScore(it.name, g_query)) continue;
        g_results.push_back(it);
        if (g_results.size() >= 400) break;
    }
    if (g_sel >= (int)g_results.size()) g_sel = 0;
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
    int dx = p.x - CenterX(), dy = p.y - CenterY();
    if (dx * dx + dy * dy <= kCenterR * kCenterR) return -2;   // 中心
    for (int i = 0; i < 8; ++i) {
        if (!g_slots[i].used) continue;
        RECT r = SectorRect(i);
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return i;
    }
    return -1;
}

static RECT PanelRect() {
    RECT r;
    r.left = 24; r.top = 24; r.right = g_winW - 24; r.bottom = g_winH - 24;
    return r;
}

static RECT RowRect(int i);

static int VisibleRows() {
    int h = PanelRect().bottom - (RowRect(0).top);
    int n = h / 38;
    if (n < 3) n = 3;
    if (n > 12) n = 12;
    return n;
}

static RECT RowRect(int i) {
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 16;
    r.right = p.right - 34;
    r.top = p.top + 118 + i * 38;
    r.bottom = r.top + 34;
    return r;
}

static RECT TabRect(int i) {
    RECT p = PanelRect();
    int w = 74, gap = 6;
    RECT r;
    r.left = p.left + 16 + i * (w + gap);
    r.right = r.left + w;
    r.top = p.top + 32;
    r.bottom = r.top + 26;
    return r;
}

static RECT IconRect(int i) {   // 0=文件夹 1=齿轮
    RECT p = PanelRect();
    RECT r;
    r.right = p.right - 12 - i * 26;
    r.left = r.right - 20;
    r.top = p.top + 10;
    r.bottom = r.top + 20;
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
        FillRound(dc, r, 8, sel ? kSectorSel : kSectorFill, kSectorBorder);

        SetBkMode(dc, TRANSPARENT);
        HFONT f = MakeFont(15);
        HGDIOBJ of = SelectObject(dc, f);

        if (!g_slots[i].used) {
            SetTextColor(dc, RGB(90, 90, 90));
            DrawTextW(dc, L"—", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(dc, kText);
            RECT tr = r; tr.left += 8; tr.right -= 26;
            DrawTextW(dc, g_slots[i].name.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            SetTextColor(dc, kTextDim);
            SetTextAlign(dc, TA_RIGHT | TA_TOP);
            std::wstring k = g_slots[i].key.empty() ? std::to_wstring(i + 1) : g_slots[i].key;
            TextOutW(dc, r.right - 8, r.top + 6, k.c_str(), (int)k.size());
            SetTextAlign(dc, TA_LEFT | TA_TOP);
        }
        SelectObject(dc, of);
        DeleteObject(f);
    }

    // 中心圆 + 弧线
    int cx = CenterX(), cy = CenterY();
    if (g_hover >= 0) {
        RECT r = SectorRect(g_hover);
        double tx = (r.left + r.right) / 2.0 - cx, ty = (r.top + r.bottom) / 2.0 - cy;
        double ang = atan2(ty, tx);
        int ar = kCenterR + 10;
        HPEN pen = CreatePen(PS_SOLID, 4, kArcColor);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (int i = 0; i < 18; ++i) {
            double a1 = ang - 0.45 + 0.9 * i / 18.0;
            double a2 = ang - 0.45 + 0.9 * (i + 1) / 18.0;
            MoveToEx(dc, cx + (int)(cos(a1) * ar), cy + (int)(sin(a1) * ar), nullptr);
            LineTo(dc, cx + (int)(cos(a2) * ar), cy + (int)(sin(a2) * ar));
        }
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(pen);
    }

    HBRUSH b = CreateSolidBrush(kCenterFill);
    HPEN p = CreatePen(PS_SOLID, 3, kCenterBorder);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Ellipse(dc, cx - kCenterR, cy - kCenterR, cx + kCenterR, cy + kCenterR);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(150, 150, 150));
    HFONT f = MakeFont(13);
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

    SetBkMode(dc, TRANSPARENT);
    HFONT ft = MakeFont(13);
    HGDIOBJ of = SelectObject(dc, ft);
    SetTextColor(dc, RGB(140, 140, 140));
    RECT tr = { p.left + 16, p.top + 10, p.right - 16, p.top + 30 };
    DrawTextW(dc, title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, of);
    DeleteObject(ft);

    // 输入框
    RECT ir = InputRect();
    FillRound(dc, ir, 8, RGB(22, 22, 24), RGB(80, 80, 84));
    if (input.empty()) {
        HFONT fi = MakeFont(15);
        of = SelectObject(dc, fi);
        SetTextColor(dc, RGB(110, 110, 110));
        RECT irc = ir; irc.left += 10;
        DrawTextW(dc, L"输入以搜索（Tab / Esc 返回）", -1, &irc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
    r.top = r0.top + 6;
    r.bottom = r6.bottom + 6;
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

static void DrawResults(HDC dc) {
    for (int row = 0; row < VisibleRows(); ++row) {
        int i = g_scroll + row;
        if (i >= (int)g_results.size()) break;
        RECT r = RowRect(row);
        bool sel = (i == g_sel);
        bool hov = (i == g_hoverRow);
        FillRound(dc, r, 6, sel ? RGB(70, 70, 74) : (hov ? RGB(58, 110, 168) : RGB(38, 38, 40)), sel ? RGB(70, 70, 74) : (hov ? RGB(58, 110, 168) : RGB(50, 50, 54)));
        if (sel) { RECT bar = { r.left, r.top, r.left + 4, r.bottom }; HBRUSH bb2 = CreateSolidBrush(kRowSel); FillRect(dc, &bar, bb2); DeleteObject(bb2); }

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        HFONT f = MakeFont(15);
        HGDIOBJ of = SelectObject(dc, f);
        RECT tr = r; tr.left += 34; tr.right -= 90;
        if (i < 6) {
            SetTextColor(dc, RGB(120, 120, 128));
            RECT nr = r; nr.left += 12; nr.right = nr.left + 20;
            std::wstring num = std::to_wstring(i + 1);
            DrawTextW(dc, num.c_str(), -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, kText);
        }
        DrawTextW(dc, g_results[i].name.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SetTextColor(dc, RGB(210, 210, 210));
        SetTextAlign(dc, TA_RIGHT | TA_CENTER);
        std::wstring tag = (g_results[i].kind == L"script") ? L"脚本" : L"图层";
        TextOutW(dc, r.right - 12, (r.top + r.bottom) / 2, tag.c_str(), (int)tag.size());
        SetTextAlign(dc, TA_LEFT | TA_TOP);

        SelectObject(dc, of);
        DeleteObject(f);
    }

    if (g_results.empty() && !g_query.empty()) {
        RECT r = RowRect(0);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(130, 130, 130));
        HFONT f = MakeFont(14);
        HGDIOBJ of = SelectObject(dc, f);
        DrawTextW(dc, L"没有匹配项", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(f);
    }
}

static RECT SettingRowRect(int i) {
    RECT p = PanelRect();
    RECT r;
    r.left = p.left + 232;
    r.right = p.right - 16;
    r.top = p.top + 118 + i * 38;
    r.bottom = r.top + 34;
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
static void DrawSettings(HDC dc) {
    RECT p = PanelRect();
    FillRound(dc, p, 14, kPanelFill, kPanelBorder);

    SetBkMode(dc, TRANSPARENT);
    HFONT ft = MakeFont(13);
    HGDIOBJ of = SelectObject(dc, ft);
    SetTextColor(dc, RGB(140, 140, 140));
    RECT tr = { p.left + 16, p.top + 10, p.right - 16, p.top + 30 };
    DrawTextW(dc, L"设置 —— 点一行修改绑定（Delete 清除，Esc 返回）", -1, &tr, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, of);
    DeleteObject(ft);

    // 左侧：迷你圆盘预览
    RECT pv = { p.left + 16, p.top + 44, p.left + 216, p.top + 250 };
    DrawMiniPie(dc, pv, g_sel);
    {   // 右上角关闭按钮
        RECT xr = IconRect(0);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 185));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, xr.left + 4, xr.top + 4, nullptr); LineTo(dc, xr.right - 4, xr.bottom - 4);
        MoveToEx(dc, xr.right - 4, xr.top + 4, nullptr); LineTo(dc, xr.left + 4, xr.bottom - 4);
        SelectObject(dc, op);
        DeleteObject(pen);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(150, 150, 150));
    HFONT fp = MakeFont(12);
    HGDIOBJ opf = SelectObject(dc, fp);
    RECT pr = { pv.left, pv.bottom + 4, pv.right, pv.bottom + 26 };
    DrawTextW(dc, L"左侧：当前扇区预览", -1, &pr, DT_CENTER | DT_SINGLELINE);
    SelectObject(dc, opf);
    DeleteObject(fp);

    // 右侧：绑定列表
    static const wchar_t* dirs[8] = { L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW" };
    for (int i = 0; i < 8; ++i) {
        RECT r = SettingRowRect(i);
        bool sel = (i == g_sel);
        bool hov = (i == g_hoverRow);
        FillRound(dc, r, 6, sel ? RGB(70, 70, 74) : (hov ? RGB(58, 110, 168) : RGB(38, 38, 40)), sel ? RGB(70, 70, 74) : (hov ? RGB(58, 110, 168) : RGB(50, 50, 54)));
        if (sel) { RECT bar = { r.left, r.top, r.left + 4, r.bottom }; HBRUSH bb2 = CreateSolidBrush(kRowSel); FillRect(dc, &bar, bb2); DeleteObject(bb2); }

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        HFONT f = MakeFont(15);
        HGDIOBJ of2 = SelectObject(dc, f);
        std::wstring label = std::wstring(dirs[i]) + L"   " +
                             (g_slots[i].used ? g_slots[i].name : L"（空）");
        RECT tr2 = r; tr2.left += 12;
        DrawTextW(dc, label.c_str(), -1, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, of2);
        DeleteObject(f);
    }
}

static void DrawTabs(HDC dc) {
    for (int i = 0; i < kTabCount; ++i) {
        RECT r = TabRect(i);
        bool on = (i == g_tab);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, on ? RGB(255, 255, 255) : RGB(150, 150, 155));
        HFONT f = MakeFont(14);
        HGDIOBJ of = SelectObject(dc, f);
        DrawTextW(dc, kTabs[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    // 图标底色：避免落在色键透明区（那边鼠标会穿透到下面的应用）
    {
        RECT bg = IconRect(1);
        RECT bg2 = IconRect(0);
        RECT pill = { bg.left - 6, bg.top - 4, bg2.right + 6, bg2.bottom + 4 };
        FillRound(dc, pill, 8, RGB(40, 40, 43), RGB(56, 56, 60));
    }
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(140, 140, 145));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));

    // 文件夹
    RECT fr = IconRect(1);   // 文件夹在左
    RECT body = { fr.left + 2, fr.top + 8, fr.right - 2, fr.bottom - 4 };
    RoundRect(dc, body.left, body.top, body.right, body.bottom, 4, 4);
    RECT tabr = { fr.left + 4, fr.top + 3, fr.left + 14, fr.top + 9 };
    RoundRect(dc, tabr.left, tabr.top, tabr.right, tabr.bottom, 3, 3);

    // 齿轮
    RECT gr = IconRect(0);   // 设置在右
    int cx = (gr.left + gr.right) / 2, cy = (gr.top + gr.bottom) / 2;
    int r1 = 9, r2 = 5;
    Ellipse(dc, cx - r1, cy - r1, cx + r1, cy + r1);
    Ellipse(dc, cx - r2, cy - r2, cx + r2, cy + r2);
    for (int i = 0; i < 8; ++i) {
        double a = i * 3.14159265 / 4.0;
        MoveToEx(dc, cx + (int)(cos(a) * (r1 - 1)), cy + (int)(sin(a) * (r1 - 1)), nullptr);
        LineTo(dc, cx + (int)(cos(a) * (r1 + 4)), cy + (int)(sin(a) * (r1 + 4)));
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

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

    if (g_mode != M_SETTINGS) DrawTopIcons(mem);   // 设置页右上角是关闭按钮

    {   // 缩放手柄：底边中点、右边中点各三个点
        HBRUSH db = CreateSolidBrush(RGB(140, 140, 148));
        int cx = g_winW / 2, cy = g_winH / 2;
        for (int i = -1; i <= 1; ++i) {
            RECT d1 = { cx + i * 8 - 1, g_winH - 7, cx + i * 8 + 2, g_winH - 4 };
            FillRect(mem, &d1, db);
            RECT d2 = { g_winW - 7, cy + i * 8 - 1, g_winW - 4, cy + i * 8 + 2 };
            FillRect(mem, &d2, db);
        }
        DeleteObject(db);
    }
    BitBlt(dc, 0, 0, g_winW, g_winH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
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

static void LoadConfig() {
    std::wstring path = ExeDir() + L"\\hotbox.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteFileUtf8(path, "; Cavalry Hotbox\r\n; hotkey: Ctrl/Alt/Shift/Win + A-Z / 0-9 / F1-F24\r\nhotkey=Alt+Q\r\n");
    }
    std::string raw = ReadFileUtf8(path);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        std::string line = raw.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? raw.size() : eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("ime=", 0) == 0) {
            g_imeOn = (line.find("off") == std::string::npos);
            continue;
        }
        if (line.rfind("hotkey=", 0) == 0) {
            std::wstring spec = Trim(Widen(line.substr(7)));
            UINT mods = 0, vk = 0;
            if (ParseHotkey(spec, mods, vk)) {
                g_hotkeySpec = spec; g_hotkeyMods = mods; g_hotkeyVk = vk;
            }
            return;
        }
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

static void ShowAtCursor(HWND hwnd) {
    g_mode = M_PIE;
    g_query.clear();
    g_sel = 0;
    g_hover = -1;
    g_bindSlot = -1;
    LoadSectors();
    SyncEditFromMode();      // 回到圆盘：收起输入框
    UpdateScrollInfo();      // 顺手把滚动条也藏掉

    POINT c;
    GetCursorPos(&c);
    int x = c.x - g_winW / 2, y = c.y - g_winH / 2;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    if (x + g_winW > work.right) x = work.right - g_winW;
    if (y + g_winH > work.bottom) y = work.bottom - g_winH;

    SetWindowPos(hwnd, HWND_TOP, x, y, g_winW, g_winH, SWP_SHOWWINDOW);
    ForceForeground(hwnd);
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
        if (g_slots[g_bindSlot].key.empty()) g_slots[g_bindSlot].key = std::to_wstring(g_bindSlot + 1);
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
    AppendMenuW(menu, MF_STRING, IDM_ACTIONS, L"打开 actions 文件夹");
    AppendMenuW(menu, MF_STRING, IDM_RELOAD, L"重新读取 actions");
    AppendMenuW(menu, MF_STRING, IDM_LOG, L"查看调试日志");
    AppendMenuW(menu, MF_STRING, IDM_INI, L"打开 hotbox.ini");
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
    switch (msg) {
    case WM_TIMER:
        if (g_mode == M_SEARCH && IsWindowVisible(hwnd) && g_edit) {
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
        g_dragging = false;
        ReleaseCapture();
        return 0;
    case WM_SETCURSOR: {
        POINT cp; GetCursorPos(&cp);
        ScreenToClient(hwnd, &cp);
        int edges = 0;
        if (cp.x >= g_winW - 10) edges |= 1;
        if (cp.y >= g_winH - 10) edges |= 2;
        if (edges) {
            LPCWSTR cur = (edges == 3) ? IDC_SIZENWSE : (edges == 1 ? IDC_SIZEWE : IDC_SIZENS);
            SetCursor(LoadCursor(nullptr, cur));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (g_resizing) {
            POINT pt2; GetCursorPos(&pt2);
            int nw = g_winW, nh = g_winH;
            if (g_resizeEdges & 1) nw = g_resizeStartW + (pt2.x - g_resizeStart.x);
            if (g_resizeEdges & 2) nh = g_resizeStartH + (pt2.y - g_resizeStart.y);
            if (nw < 480) nw = 480;
            if (nh < 380) nh = 380;
            g_winW = nw; g_winH = nh;
            SetWindowPos(hwnd, HWND_TOP, 0, 0, nw, nh, SWP_NOMOVE | SWP_NOZORDER);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_mode == M_SEARCH) {
            POINT mp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hr = -1;
            for (int rr = 0; rr < 7; ++rr) {
                RECT rrr = RowRect(rr);
                if (mp.x >= rrr.left && mp.x < rrr.right && mp.y >= rrr.top && mp.y < rrr.bottom) { hr = g_scroll + rr; break; }
            }
            if (hr != g_hoverRow) { g_hoverRow = hr; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        if (g_dragging) {
            POINT pt; GetCursorPos(&pt);
            SetWindowPos(hwnd, HWND_TOP, pt.x - g_dragOffset.x, pt.y - g_dragOffset.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
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
    case WM_RBUTTONUP:
        ShowWindow(hwnd, SW_HIDE);
        g_mode = M_PIE;
        SyncEditFromMode();
        return 0;
    case WM_LBUTTONDOWN: {
        {
            int ex = (int)GET_X_LPARAM(lp), ey = (int)GET_Y_LPARAM(lp);
            int edges = 0;
            if (ex >= g_winW - 10) edges |= 1;
            if (ey >= g_winH - 10) edges |= 2;
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
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        // 图标点击：按 pill 的左右半区判定，命中范围更大
        {
            RECT a = IconRect(1), b2 = IconRect(0);
            RECT pill = { a.left - 8, a.top - 6, b2.right + 8, b2.bottom + 6 };
            if (p.x >= pill.left && p.x < pill.right && p.y >= pill.top && p.y < pill.bottom) {
                int mid = (a.left + b2.right) / 2;
                if (p.x < mid) ShellExecuteW(nullptr, L"open", L"C:\\Windows\\explorer.exe", (L"\"" + ExeDir() + L"\\actions\"").c_str(), nullptr, SW_SHOWNORMAL);
                else { g_mode = M_SETTINGS; g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); }
                return 0;
            }
        }

        // 搜索页签
        if (g_mode == M_SEARCH) {
            for (int i = 0; i < kTabCount; ++i) {
                RECT tr = TabRect(i);
                if (p.x >= tr.left && p.x < tr.right && p.y >= tr.top && p.y < tr.bottom) {
                    g_tab = i;
                    g_sel = 0;
                    UpdateResults();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
        }

        if (g_mode == M_PIE) {
            int h = HitTestPie(p);
            if (h == -2) { ShowWindow(hwnd, SW_HIDE); return 0; }
            if (h >= 0 && g_slots[h].used) {
                Item it{ g_slots[h].name, g_slots[h].kind, g_slots[h].value };
                RunItem(it);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        } else if (g_mode == M_SEARCH) {
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
            {   // 右上角 × ：关闭设置页
                RECT xr = IconRect(0);
                if (p.x >= xr.left - 6 && p.x < xr.right + 6 && p.y >= xr.top - 6 && p.y < xr.bottom + 6) {
                    g_mode = M_PIE;
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
            for (int i = 0; i < 8; ++i) {
                RECT r = SettingRowRect(i);
                if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) {
                    g_sel = i;
                    g_bindSlot = i;
                    EnterSearch();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
            }
        }
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
        if (g_mode == M_SEARCH) {
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
    case WM_MOUSEWHEEL:
        if (g_mode == M_SEARCH) {
            g_scroll -= (GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA) * 3;
            UpdateScrollInfo();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN: {
        if (g_mode == M_SEARCH) {
            bool c = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool s = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (c && wp >= '1' && wp <= '6') { g_sel = (int)(wp - '1'); ExecSelected(); return 0; }
            if (wp == VK_TAB) { g_tab = (g_tab + (s ? kTabCount - 1 : 1)) % kTabCount; g_sel = 0; g_scroll = 0; UpdateResults(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        }
        if (g_mode == M_SEARCH && (GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'A')) {
            if (g_edit) { SetFocus(g_edit); SendMessageW(g_edit, EM_SETSEL, (WPARAM)0, (LPARAM)-1); }
            return 0;   // 吞掉，避免系统提示音
        }
        if (wp == VK_ESCAPE) {
            if (g_mode == M_SEARCH && g_bindSlot >= 0) { g_mode = M_SETTINGS; g_bindSlot = -1; }
            else if (g_mode != M_PIE) { g_mode = M_PIE; }
            else ShowWindow(hwnd, SW_HIDE);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_mode == M_PIE) {
            if (wp == VK_TAB) { EnterSearch(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (wp == VK_F2) { g_mode = M_SETTINGS; g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
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
    case WM_APP + 5:
        if (g_mode == M_SEARCH && wp >= 1 && wp <= 6) {
            g_sel = (int)wp - 1;
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
        if (wp == HOTKEY_TOGGLE) ToggleWindow(g_main ? g_main : hwnd);
        return 0;
    case WM_COMMAND: {
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE) {
            wchar_t buf[256] = {0};
            GetWindowTextW(g_edit, buf, 255);
            g_query = buf;
            g_sel = 0; g_scroll = 0;
            UpdateResults();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (LOWORD(wp)) {
            case IDM_SETTINGS: g_mode = M_SETTINGS; g_sel = 0; InvalidateRect(hwnd, nullptr, TRUE); return 0;
            case IDM_ACTIONS: OpenPath(ExeDir() + L"\\actions"); return 0;
            case IDM_RELOAD: LoadSectors(); LoadIndex(); InvalidateRect(hwnd, nullptr, TRUE); return 0;
            case IDM_LOG: OpenPath(ExeDir() + L"\\hotbox-debug.log"); return 0;
            case IDM_INI: OpenPath(ExeDir() + L"\\hotbox.ini"); return 0;
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
    if (cmdLine && wcsstr(cmdLine, L"--show")) {
        HWND existing = FindWindowW(L"CavalryHotbox", L"Cavalry Hotbox");
        if (existing) { PostMessageW(existing, WM_APP + 1, 0, 0); return 0; }
    }

    bool alive = BridgeAlive();
    if (!alive) alive = StartBridge();
    Log(std::string("bridge ") + (alive ? "ok" : "not connected"));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CavalryHotbox";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                L"CavalryHotbox", L"Cavalry Hotbox", WS_POPUP | WS_CLIPCHILDREN,
                                0, 0, g_winW, g_winH, nullptr, nullptr, inst, nullptr);
    g_main = hwnd;
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
    CreateEdit(hwnd, inst);
    ImmAssociateContext(hwnd, nullptr);   // 主窗口不吃输入法；输入框仍可用中文
    // 输入法保持启用：中文搜索需要它

    LoadConfig();
    LoadIndex();
    LoadSectors();

    HWND msgWnd = CreateWindowExW(0, L"CavalryHotbox", L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, inst, nullptr);
    BOOL hk = RegisterHotKey(msgWnd ? msgWnd : hwnd, HOTKEY_TOGGLE, g_hotkeyMods, g_hotkeyVk);
    Log("hotkey " + Narrow(g_hotkeySpec) + " -> " + (hk ? "ok" : ("FAIL err=" + std::to_string(GetLastError()))));

    SetTimer(hwnd, 1, 25, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
