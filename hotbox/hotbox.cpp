// hotbox.cpp — Cavalry 外部热盒第一步：最小可跑的 Win32 面板
//
// 三个按钮 → 通过 cavalry_bridge 的本地接口 → 让 Cavalry 建一个图层。
// 依赖：cavalry_bridge.mjs 正在跑（http://127.0.0.1:6769）。
//
// 编译（MSVC 2019 BuildTools）：
//   "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
//   cl /nologo /EHsc /utf-8 hotbox.cpp /link winhttp.lib user32.lib gdi32.lib
//
// 跑：hotbox.exe

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static const wchar_t* kHost = L"127.0.0.1";
static const int        kPort = 6769;
static const wchar_t* kPath = L"/call";

static HBRUSH g_bgBrush = nullptr;
static HWND   g_status = nullptr;

// ---------------------------------------------------------------- HTTP
// 把一段 JSON 用 POST 发到桥的 /call，返回响应正文（失败时返回空 + err）
static std::string PostJson(const std::string& body, std::string& err) {
    std::string out;
    HINTERNET session = WinHttpOpen(L"cavalry-hotbox/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { err = "WinHttpOpen failed"; return out; }

    HINTERNET connect = WinHttpConnect(session, kHost, (INTERNET_PORT)kPort, 0);
    if (!connect) { err = "WinHttpConnect failed (bridge 没在跑？)"; WinHttpCloseHandle(session); return out; }

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", kPath, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) { err = "WinHttpOpenRequest failed"; WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return out; }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(request, headers, (DWORD)-1L,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!ok) { err = "SendRequest failed"; }
    else if (!WinHttpReceiveResponse(request, nullptr)) { err = "ReceiveResponse failed"; }
    else {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        if (code != 200) err = "HTTP " + std::to_string(code);

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
    return out;
}

// ---------------------------------------------------------------- 发脚本
static void RunScript(const char* layerType) {
    // 目标 JSON：
    // {"name":"execute_script","args":{"script":"api.create(\"duplicator\")"}}
    std::string body = "{\"name\":\"execute_script\",\"args\":{\"script\":\"api.create(\\\"";
    body += layerType;
    body += "\\\")\"}}";

    std::string err;
    std::string resp = PostJson(body, err);

    std::string msg;
    if (!err.empty()) {
        msg = std::string("失败: ") + err;
    } else {
        // 响应是一段 JSON，只截前面一点显示
        msg = resp.substr(0, 150);
    }

    // UTF-8 -> 宽字符
    int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
    std::wstring wmsg(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, &wmsg[0], wlen);

    SetWindowTextW(g_status, wmsg.c_str());
}

// ---------------------------------------------------------------- 窗口
#define ID_DUP   1001
#define ID_NR    1002
#define ID_MATH  1003
#define HOTKEY_TOGGLE 1

static void ToggleWindow(HWND hwnd) {
    if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_bgBrush = CreateSolidBrush(RGB(32, 32, 34));

        CreateWindowW(L"BUTTON", L"Duplicator", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            16, 16, 110, 34, hwnd, (HMENU)ID_DUP, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Number Range", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            134, 16, 110, 34, hwnd, (HMENU)ID_NR, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Math", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            252, 16, 110, 34, hwnd, (HMENU)ID_MATH, nullptr, nullptr);

        g_status = CreateWindowW(L"STATIC", L"就绪 —— Alt+H 呼出/隐藏，Esc 收起",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            16, 62, 346, 40, hwnd, nullptr, nullptr, nullptr);
        return 0;
    }
    case WM_HOTKEY: {
        if (wp == HOTKEY_TOGGLE) ToggleWindow(hwnd);
        return 0;
    }
    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case ID_DUP:  RunScript("duplicator"); break;
        case ID_NR:   RunScript("numberRange"); break;
        case ID_MATH: RunScript("math"); break;
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(220, 220, 220));
        SetBkColor(dc, RGB(32, 32, 34));
        return (LRESULT)g_bgBrush;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_bgBrush);
        return 1;
    }
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CavalryHotbox";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"CavalryHotbox", L"Cavalry Hotbox",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        200, 200, 394, 150, nullptr, nullptr, inst, nullptr);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    // 全局热键：Alt+H 呼出/隐藏（MOD_NOREPEAT 防止按住时反复触发）
    if (!RegisterHotKey(hwnd, HOTKEY_TOGGLE, MOD_ALT | MOD_NOREPEAT, 'H')) {
        MessageBoxW(hwnd, L"Alt+H 注册失败（可能被别的程序占用）。", L"Cavalry Hotbox", MB_ICONWARNING);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
