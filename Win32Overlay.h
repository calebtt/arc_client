#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <unordered_map>


enum class BrightnessLevel { Off, Dark, Darker };

class BrightnessStateMachine {
private:
    BrightnessLevel state = BrightnessLevel::Off;
public:
    [[nodiscard]] BrightnessLevel Current() const {
        return state;
    }

    BrightnessLevel NextState() {
        state = static_cast<BrightnessLevel>((static_cast<int>(state) + 1) % 3);
        return state;
    }
};

struct BrightnessRenderInfo {
    uint8_t alpha;
    std::wstring label;
};

BrightnessRenderInfo GetBrightnessLevelRenderInfo(BrightnessLevel level) {
    switch (level) {
    case BrightnessLevel::Dark:
        return { 128, L"50%" };
    case BrightnessLevel::Darker:
        return { 200, L"78%" };
    case BrightnessLevel::Off:
    default:
        return { 0, L"" };
    }
}

static HWND overlayWindow = nullptr;
BrightnessStateMachine brightnessState;

RECT GetPrimaryMonitorRect() {
    RECT rc;
    //SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);
    // Or use full screen:
    GetWindowRect(GetDesktopWindow(), &rc);
    return rc;
}


LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND CreateOverlayForMonitor(const RECT& monitorRect, HINSTANCE hInstance, uint8_t alpha, std::wstring_view labelText = L"")
{
    static const wchar_t* className = L"BlueLightOverlayClass";
    static bool classRegistered = false;

    if (!classRegistered) {
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
        monitorRect.left, monitorRect.top, width, height,
        nullptr, nullptr, hInstance, nullptr);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    SelectObject(hdcMem, hBitmap);

    UINT32* pixels = reinterpret_cast<UINT32*>(pvBits);
    UINT32 bgra = (alpha << 24) | (0 << 16) | (0 << 8) | 0;
    for (int i = 0; i < width * height; ++i) {
        pixels[i] = bgra;
    }

    // Draw text
    if (!labelText.empty()) {
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        HFONT hFont = CreateFontW(64, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        SelectObject(hdcMem, hFont);
        TextOutW(hdcMem, width / 2 - 60, height / 2 - 32, labelText.data(), static_cast<int>(labelText.size()));
        DeleteObject(hFont);
    }

    SIZE size = { width, height };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(hwnd, hdcScreen, nullptr, &size, hdcMem, &src, 0, &blend, ULW_ALPHA);

    ReleaseDC(NULL, hdcScreen);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);

    ShowWindow(hwnd, SW_SHOW);

    if (!labelText.empty()) {
        std::thread([hwnd, monitorRect, hInstance, alpha]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            HDC hdcScreen = GetDC(NULL);
            HDC hdcMem = CreateCompatibleDC(hdcScreen);

            int width = monitorRect.right - monitorRect.left;
            int height = monitorRect.bottom - monitorRect.top;

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* pvBits = nullptr;
            HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
            SelectObject(hdcMem, hBitmap);

            UINT32* pixels = reinterpret_cast<UINT32*>(pvBits);
            UINT32 bgra = (alpha << 24) | (0 << 16) | (0 << 8) | 0;
            for (int i = 0; i < width * height; ++i) {
                pixels[i] = bgra;
            }

            SIZE size = { width, height };
            POINT src = { 0, 0 };
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            UpdateLayeredWindow(hwnd, hdcScreen, nullptr, &size, hdcMem, &src, 0, &blend, ULW_ALPHA);
            ReleaseDC(NULL, hdcScreen);
            DeleteObject(hBitmap);
            DeleteDC(hdcMem);
            }).detach();
    }

    return hwnd;

}

void ToggleSingleMonitorOverlay()
{
    static std::mutex overlayMutex;
    std::scoped_lock lock(overlayMutex);

    // Destroy previous overlay
    if (overlayWindow) {
        DestroyWindow(overlayWindow);
        overlayWindow = nullptr;
    }

    const auto nextBrightness = brightnessState.NextState();
    const auto info = GetBrightnessLevelRenderInfo(nextBrightness);

    if (nextBrightness == BrightnessLevel::Off)
        return;

    RECT monitorRect = GetPrimaryMonitorRect();
    overlayWindow = CreateOverlayForMonitor(monitorRect, GetModuleHandle(nullptr), info.alpha, info.label);
}


