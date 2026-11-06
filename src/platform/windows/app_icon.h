// Copyright (c) 2026 guardiancrow. MIT.
#pragma once

#include "platform/windows/app_resources.h"

#include <windows.h>

#include <utility>

namespace clipss2::platform::windows
{
class ApplicationIcon final
{
public:
    ApplicationIcon() = default;
    ~ApplicationIcon() { reset(); }
    ApplicationIcon(const ApplicationIcon&) = delete;
    ApplicationIcon& operator=(const ApplicationIcon&) = delete;

    // Every stored handle is owned. Resource icons are deliberately not shared:
    // LR_SHARED can return a cached icon with a different requested DPI/size.
    void load(int width, int height, HINSTANCE instance = GetModuleHandleW(nullptr)) noexcept
    {
        HICON loaded = instance ? static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_CLIPSS2_APP), IMAGE_ICON, width, height, 0)) : nullptr;
        if (!loaded)
        {
            // Copy the shared stock icon; never destroy the shared original.
            const HICON stock = LoadIconW(nullptr, IDI_APPLICATION);
            if (stock) loaded = CopyIcon(stock);
        }
        if (loaded)
        {
            reset();
            value_ = loaded;
        }
    }

    void reset() noexcept
    {
        if (value_) (void)DestroyIcon(value_);
        value_ = nullptr;
    }
    void swap(ApplicationIcon& other) noexcept { std::swap(value_, other.value_); }
    [[nodiscard]] HICON get() const noexcept { return value_; }

private:
    HICON value_{};
};

class WindowApplicationIcons final
{
public:
    // The HWND must be destroyed before this object. Replace its borrowed
    // handles before releasing the previous pair when the monitor DPI changes.
    void apply(HWND window, UINT dpi) noexcept
    {
        if (!dpi) dpi = 96;
        ApplicationIcon small;
        ApplicationIcon large;
        small.load(GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi));
        large.load(GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi));
        if (small.get())
        {
            (void)SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small.get()));
            small_.swap(small);
        }
        if (large.get())
        {
            (void)SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large.get()));
            large_.swap(large);
        }
    }

private:
    ApplicationIcon small_;
    ApplicationIcon large_;
};
}
