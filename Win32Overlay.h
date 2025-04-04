#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <vector>

std::vector<HWND> overlayWindows;
bool blueLightFilterEnabled = false;

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateOverlayForMonitor(const RECT& monitorRect, HINSTANCE hInstance)
{
    static const wchar_t* className = L"BlueLightOverlayClass";
    static bool classRegistered = false;

    if (!classRegistered)
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = className;
        RegisterClass(&wc);
        classRegistered = true;
    }

    int width = monitorRect.right - monitorRect.left;
    int height = monitorRect.bottom - monitorRect.top;

    HWND hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        className, L"", WS_POPUP,
        monitorRect.left, monitorRect.top,
        width, height,
        nullptr, nullptr, hInstance, nullptr);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    SelectObject(hdcMem, hBitmap);

    UINT32* pixels = reinterpret_cast<UINT32*>(pvBits);

    // correct orange BGRA
    auto MakeBGRA = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return (a << 24) | (r << 16) | (g << 8) | b;
        };

    const UINT32 color = MakeBGRA(0, 0, 0, 200);

    
    for (int i = 0; i < width * height; ++i)
    {
        pixels[i] = color;
    }

    SIZE size = { width, height };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(hwnd, hdcScreen, nullptr, &size, hdcMem, &src, 0, &blend, ULW_ALPHA);

    ReleaseDC(NULL, hdcScreen);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);

    ShowWindow(hwnd, SW_SHOW);
    overlayWindows.push_back(hwnd);
}


BOOL CALLBACK MonitorEnumProc(HMONITOR, HDC, LPRECT lprcMonitor, LPARAM lParam)
{
    auto hInstance = reinterpret_cast<HINSTANCE>(lParam);
    CreateOverlayForMonitor(*lprcMonitor, hInstance);
    return TRUE;
}

void ToggleMultiMonitorOverlay()
{
    //std::cout << "[Info] Toggling blue light filter overlay.\n";

    if (!blueLightFilterEnabled)
    {
        overlayWindows.clear();
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(GetModuleHandle(nullptr)));
    }
    else
    {
        for (auto hwnd : overlayWindows)
            DestroyWindow(hwnd);
        overlayWindows.clear();
    }
    blueLightFilterEnabled = !blueLightFilterEnabled;
}
