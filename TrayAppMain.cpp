// TrayAppMain.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <commctrl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include "Win32Overlay.h"
#include "StatConfiguration.h"
#include "ClientSetup.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TOGGLE_BRIGHTNESS 1002
#define ID_TRAY_SENSITIVITY_TOGGLE 1003
#define ID_TRAY_USER_TOKEN 1004
//#define ID_TRAY_DISABLE_CONNECTION 1005
#define ID_TRAY_TOGGLE_CONNECTION 1005
#define ID_TRAY_UUID_BASE 3000


NOTIFYICONDATA nid = {};
HMENU hTrayMenu = nullptr;
HWND g_hwnd = nullptr;
HWND hTokenInput = nullptr;

WebSocketClientGlobal GlobalBeastClient{};

std::vector<std::string> trayUUIDIndex; // index 0 = UUID for ID_TRAY_UUID_BASE + 0
std::set<std::string> webClientUUIDs;

bool IsClientRunning()
{
    return GlobalBeastClient.IsClientRunning();
}

void UpdateConnectionMenuCheckmark() {
    const UINT checkFlag = IsClientRunning() ? MF_UNCHECKED : MF_CHECKED;
    CheckMenuItem(hTrayMenu, ID_TRAY_TOGGLE_CONNECTION, MF_BYCOMMAND | checkFlag);
}

void ShowBalloonMessage(const std::wstring& title, const std::wstring& message, DWORD iconType = NIIF_INFO, UINT timeoutMs = 1000)
{
    nid.uFlags = NIF_INFO;

    // Clamp the title to 63 characters + null terminator
    std::wstring safeTitle = title.substr(0, ARRAYSIZE(nid.szInfoTitle) - 1);
    wcsncpy_s(nid.szInfoTitle, safeTitle.c_str(), ARRAYSIZE(nid.szInfoTitle));

    // Clamp the message to 255 characters + null terminator
    std::wstring safeMessage = message.substr(0, ARRAYSIZE(nid.szInfo) - 1);
    wcsncpy_s(nid.szInfo, safeMessage.c_str(), ARRAYSIZE(nid.szInfo));
    
    nid.dwInfoFlags = iconType | NIIF_NOSOUND;
    nid.uTimeout = timeoutMs;

    Shell_NotifyIcon(NIM_MODIFY, &nid);
}


void UpdateTrayTooltip(const std::wstring& tip) {
    wcsncpy_s(nid.szTip, tip.c_str(), sizeof(nid.szTip) / sizeof(WCHAR));
    nid.uFlags = NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void InitTrayIcon(HWND hwnd) {

    HMENU hUUIDMenu = CreatePopupMenu();
    trayUUIDIndex.clear();

    for (const auto& uuid : connectedClientUUIDs) {
        trayUUIDIndex.push_back(uuid);
        UINT id = ID_TRAY_UUID_BASE + static_cast<UINT>(trayUUIDIndex.size() - 1);
        AppendMenuA(hUUIDMenu,
            MF_STRING | (trustedClientUUIDs.contains(uuid) ? MF_CHECKED : MF_UNCHECKED),
            id,
            uuid.c_str());
    }

    hTrayMenu = CreatePopupMenu();
    AppendMenuW(hTrayMenu, MF_POPUP, (UINT_PTR)hUUIDMenu, L"Allowed Web Clients");

    AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hTrayMenu, MF_STRING, ID_TRAY_TOGGLE_BRIGHTNESS, L"Toggle Brightness Level");
    AppendMenuW(hTrayMenu, MF_STRING, ID_TRAY_SENSITIVITY_TOGGLE, L"Toggle Mouse Sensitivity");
    AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hTrayMenu, MF_STRING, ID_TRAY_USER_TOKEN, L"Set User Token");
    AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hTrayMenu, MF_STRING, ID_TRAY_TOGGLE_CONNECTION, L"Disable WebSocket Connection");
    AppendMenuW(hTrayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hTrayMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    static bool ranOnce = false;

    if (!ranOnce)
    {
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(nullptr, IDI_INFORMATION);
        wcscpy_s(nid.szTip, L"ARC Client running in tray");

        Shell_NotifyIcon(NIM_ADD, &nid);

        // Show welcome balloon
        nid.uFlags = NIF_INFO;

        wcscpy_s(nid.szInfoTitle, L"ARC Client");

        std::string token = ReadSessionToken();
        std::wstring tokenW(token.begin(), token.end());
        std::wstring message = L"Sitting in system tray. Right-click for options.\nToken: " + tokenW;

        wcscpy_s(nid.szInfo, message.c_str());
        nid.dwInfoFlags = NIIF_INFO;
        nid.uTimeout = 1000; // (in milliseconds)
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        ranOnce = true;
    }
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hTrayMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
}

void ShowUserTokenInput(HWND parent) {
    const wchar_t* className = L"TokenInputWindow";

    // Register the window class once
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            static HWND hEdit = nullptr;
            static HWND hButton = nullptr;

            switch (msg) {
            case WM_CREATE: {
                CreateWindowW(L"STATIC", L"Enter Session Token:", WS_CHILD | WS_VISIBLE,
                    10, 10, 260, 20, hwnd, nullptr, nullptr, nullptr);

                hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                    10, 35, 260, 24, hwnd, (HMENU)1, nullptr, nullptr);

                hButton = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    100, 70, 80, 25, hwnd, (HMENU)2, nullptr, nullptr);

                // Load current session token and set as default text
                std::string currentToken = ReadSessionToken();
                std::wstring tokenW(currentToken.begin(), currentToken.end());
                SetWindowTextW(hEdit, tokenW.c_str());

                SetFocus(hEdit);
                return 0;
            }

            case WM_COMMAND:
                if (LOWORD(wParam) == 2) {  // OK button
                    wchar_t buffer[512]{};
                    GetWindowTextW(hEdit, buffer, 512);
                    std::wstring newToken(buffer);
                    std::string utf8Token(newToken.begin(), newToken.end());

                    GlobalBeastClient.UpdateSessionToken(utf8Token);

                    try {
                        SaveSessionToken(utf8Token);
                        //MessageBoxW(hwnd, L"Session token updated.", L"Success", MB_OK);
                    }
                    catch (const std::exception& ex) {
                        MessageBoxA(hwnd, ex.what(), "Write Error", MB_OK | MB_ICONERROR);
                    }

                    DestroyWindow(hwnd);
                    return 0;
                }
                break;

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            }

            return DefWindowProc(hwnd, msg, wParam, lParam);
            };

        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = className;
        RegisterClass(&wc);
        classRegistered = true;
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        className, L"Session Token Input",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 140,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowTrayMenu(hwnd);
        return 0;
    case WM_APP + 1:
        DestroyMenu(hTrayMenu);
        InitTrayIcon(hwnd);
        return 0;
    case WM_COMMAND:

        if (LOWORD(wParam) >= ID_TRAY_UUID_BASE && LOWORD(wParam) < ID_TRAY_UUID_BASE + trayUUIDIndex.size()) {
            size_t index = LOWORD(wParam) - ID_TRAY_UUID_BASE;
            const std::string& uuid = trayUUIDIndex[index];

            if (trustedClientUUIDs.contains(uuid)) {
                trustedClientUUIDs.erase(uuid);
            }
            else {
                trustedClientUUIDs.insert(uuid);
            }

            //ShowBalloonMessage(L"Trusted Clients Updated", std::wstring(uuid.begin(), uuid.end()).c_str());

            // Optional: Rebuild menu immediately to reflect checkbox change
            DestroyMenu(hTrayMenu);
            InitTrayIcon(g_hwnd);
            return 0;
        }

        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT:
            GlobalBeastClient.StopClientThread();
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        case ID_TRAY_TOGGLE_BRIGHTNESS:
            ToggleSingleMonitorOverlay();
            break;
        case ID_TRAY_SENSITIVITY_TOGGLE:
            GetSensitivityTogglerInstance().Toggle();
            break;
        case ID_TRAY_USER_TOKEN:
            ShowUserTokenInput(hwnd);
            break;
        case ID_TRAY_TOGGLE_CONNECTION:
            if (IsClientRunning()) {
                GlobalBeastClient.StopClientThread();
                ShowBalloonMessage(L"Disconnected", L"The WebSocket client was stopped.");
            }
            else {
                GlobalBeastClient.Init(ReadSessionToken(), g_hwnd);
            }
            UpdateConnectionMenuCheckmark();
            break;

        }
        return 0;

    case WM_DESTROY:
        GlobalBeastClient.StopClientThread();
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) 
{
    const wchar_t* className = L"ArcTrayWindow";
    auto isTypeLocal = [&]()
        {
            int argc{};
            LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                if (arg.find(L"type=local") != std::wstring::npos) {
                    return true;
                }
            }
            return false;
        };
    const bool useLocalServer = isTypeLocal();
    const std::string serverUrl = useLocalServer
        ? "localhost"
        : "arcserver.cloud";

    WNDCLASS wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(0, className, L"", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, nullptr, nullptr, hInstance, nullptr);

    InitTrayIcon(g_hwnd);

    GlobalBeastClient.Callbacks.OnError = [](const std::string& errorMessage)
        {
            std::wstring wideError(errorMessage.begin(), errorMessage.end());
            ShowBalloonMessage(L"WebSocket Error", wideError, NIIF_ERROR);
        };
    GlobalBeastClient.Callbacks.OnConnect = []()
        {
            ShowBalloonMessage(L"Connected", L"WebSocket session started.");
        };
    GlobalBeastClient.Callbacks.OnClientListChanged = [](std::set<std::string> clients) 
        {
            webClientUUIDs = clients;

            if (trustedClientUUIDs.empty() && !clients.empty()) {
                const auto& firstUUID = *clients.begin();
                trustedClientUUIDs.insert(firstUUID);
                std::cout << "[INFO] Auto-trusted first client: " << firstUUID << "\n";
            }

            PostMessage(g_hwnd, WM_APP + 1, 0, 0);
        };

    GlobalBeastClient.Callbacks.OnFailure = []()
        {
            if (IsClientRunning()) {
                GlobalBeastClient.StopClientThread();
                ShowBalloonMessage(L"Disconnected After Retry", L"The WebSocket client was stopped.");
                UpdateConnectionMenuCheckmark();
            }
        };

    GlobalBeastClient.ServerAddress = serverUrl;
    GlobalBeastClient.Init(ReadSessionToken(), g_hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
