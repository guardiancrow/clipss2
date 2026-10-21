#pragma once
#include <windows.h>
#include <winrt/base.h>
#include <string>
#include <utility>
namespace clipss2::platform::windows::selector_resources
{
class RegisteredWindowClass final
{
public:
    RegisteredWindowClass(
        HINSTANCE instance,
        std::wstring name,
        WNDPROC procedure,
        HBRUSH background)
        : instance_(instance), name_(std::move(name))
    {
        WNDCLASSEXW description{};
        description.cbSize = sizeof(description);
        description.style = CS_HREDRAW | CS_VREDRAW;
        description.lpfnWndProc = procedure;
        description.hInstance = instance_;
        description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        description.hbrBackground = background;
        description.lpszClassName = name_.c_str();
        atom_ = RegisterClassExW(&description);
        if (atom_ == 0) winrt::throw_last_error();
    }

    RegisteredWindowClass(const RegisteredWindowClass&) = delete;
    RegisteredWindowClass& operator=(const RegisteredWindowClass&) = delete;

    ~RegisteredWindowClass()
    {
        if (atom_ != 0) UnregisterClassW(name_.c_str(), instance_);
    }

    [[nodiscard]] const wchar_t* name() const noexcept { return name_.c_str(); }

private:
    HINSTANCE instance_{};
    std::wstring name_;
    ATOM atom_{};
};

class OwnedWindow final
{
public:
    OwnedWindow() = default;
    OwnedWindow(const OwnedWindow&) = delete;
    OwnedWindow& operator=(const OwnedWindow&) = delete;
    OwnedWindow(OwnedWindow&& other) noexcept : value_(other.value_)
    {
        other.value_ = nullptr;
    }
    OwnedWindow& operator=(OwnedWindow&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }

    ~OwnedWindow() { reset(); }

    void reset(HWND value = nullptr) noexcept
    {
        if (value_ != nullptr && IsWindow(value_)) DestroyWindow(value_);
        value_ = value;
    }

    [[nodiscard]] HWND get() const noexcept { return value_; }

private:
    HWND value_{};
};

class OwnedBrush final
{
public:
    explicit OwnedBrush(COLORREF color) : value_(CreateSolidBrush(color))
    {
        if (value_ == nullptr) winrt::throw_last_error();
    }

    OwnedBrush(const OwnedBrush&) = delete;
    OwnedBrush& operator=(const OwnedBrush&) = delete;

    ~OwnedBrush() { DeleteObject(value_); }

    [[nodiscard]] HBRUSH get() const noexcept { return value_; }

private:
    HBRUSH value_{};
};

class OwnedFont final
{
public:
    OwnedFont() = default;
    OwnedFont(const OwnedFont&) = delete;
    OwnedFont& operator=(const OwnedFont&) = delete;

    ~OwnedFont() { reset(); }

    void reset(HFONT value = nullptr) noexcept
    {
        if (value_ != nullptr) DeleteObject(value_);
        value_ = value;
    }

    [[nodiscard]] HFONT get() const noexcept { return value_; }

private:
    HFONT value_{};
};

class WindowDeviceContext final
{
public:
    explicit WindowDeviceContext(HWND window)
        : window_(window), context_(GetDC(window))
    {
        if (context_ == nullptr) winrt::throw_last_error();
    }

    WindowDeviceContext(const WindowDeviceContext&) = delete;
    WindowDeviceContext& operator=(const WindowDeviceContext&) = delete;

    ~WindowDeviceContext() { ReleaseDC(window_, context_); }

    [[nodiscard]] HDC get() const noexcept { return context_; }

private:
    HWND window_{};
    HDC context_{};
};

class PaintSession final
{
public:
    explicit PaintSession(HWND window) : window_(window), context_(BeginPaint(window, &paint_))
    {
        if (context_ == nullptr) winrt::throw_last_error();
    }

    PaintSession(const PaintSession&) = delete;
    PaintSession& operator=(const PaintSession&) = delete;

    ~PaintSession() { EndPaint(window_, &paint_); }

    [[nodiscard]] HDC context() const noexcept { return context_; }

private:
    HWND window_{};
    PAINTSTRUCT paint_{};
    HDC context_{};
};

}
