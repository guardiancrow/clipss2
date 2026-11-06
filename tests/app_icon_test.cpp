// Copyright (c) 2026 guardiancrow. MIT.
#include "platform/windows/app_icon.h"

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void require_size(HICON icon, int expected)
{
    ICONINFO info{};
    require(icon && GetIconInfo(icon, &info), "Icon must be valid");
    BITMAP color{};
    const bool sized = GetObjectW(info.hbmColor, sizeof(color), &color) != 0;
    if (info.hbmColor) (void)DeleteObject(info.hbmColor);
    if (info.hbmMask) (void)DeleteObject(info.hbmMask);
    require(sized && color.bmWidth == expected && color.bmHeight == expected,
        "Resource icon must load at the requested size");
}
}

int main()
{
    using namespace clipss2::platform::windows;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    require(FindResourceW(instance, MAKEINTRESOURCEW(IDI_CLIPSS2_APP), RT_GROUP_ICON) != nullptr,
        "Embedded application icon group is missing");
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 16})
    {
        ApplicationIcon icon;
        icon.load(size, size);
        require_size(icon.get(), size);
    }
    const HICON shared_stock = LoadIconW(nullptr, IDI_APPLICATION);
    {
        ApplicationIcon fallback;
        fallback.load(16, 16, nullptr);
        require(fallback.get() && fallback.get() != shared_stock,
            "Missing resource must use an owned copy of the shared stock icon");
    }
    require_size(shared_stock, GetSystemMetrics(SM_CXICON));

    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.hInstance = instance;
    type.lpfnWndProc = DefWindowProcW;
    type.lpszClassName = L"clipss2.AppIcon.Test";
    require(RegisterClassExW(&type) != 0, "Icon test class registration failed");
    const HWND window = CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    require(window != nullptr, "Icon test window creation failed");
    {
        WindowApplicationIcons icons;
        for (const UINT dpi : {96u, 192u, 144u, 96u})
        {
            icons.apply(window, dpi);
            require_size(reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0)),
                GetSystemMetricsForDpi(SM_CXSMICON, dpi));
            require_size(reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_BIG, 0)),
                GetSystemMetricsForDpi(SM_CXICON, dpi));
        }
        require(DestroyWindow(window) != FALSE, "Icon test window destruction failed");
    }
    (void)UnregisterClassW(type.lpszClassName, instance);
    std::cout << "Application icon resource sizes, owned fallback and DPI replacement: passed\n";
}
