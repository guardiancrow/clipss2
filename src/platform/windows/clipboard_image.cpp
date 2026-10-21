#include "platform/windows/clipboard_image.h"

#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"
#include "image/alpha_diagnostics.h"

#include <windows.h>
#include <winrt/base.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

namespace clipss2::platform::windows
{
namespace
{
constexpr std::size_t bytes_per_pixel = 4;
constexpr unsigned int open_attempts = 10;
constexpr auto open_retry_interval = std::chrono::milliseconds{20};

std::size_t checked_product(std::size_t left, std::size_t right, const char* message)
{
    if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right)
        throw std::overflow_error(message);
    return left * right;
}

class GlobalMemory final
{
public:
    explicit GlobalMemory(std::span<const std::byte> bytes)
    {
        if (bytes.empty()) throw std::invalid_argument("Clipboard payload is empty.");
        handle_ = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (handle_ == nullptr) winrt::throw_last_error();

        void* destination = GlobalLock(handle_);
        if (destination == nullptr)
        {
            const DWORD error = GetLastError();
            GlobalFree(handle_);
            handle_ = nullptr;
            winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        }
        locked_ = true;
        std::memcpy(destination, bytes.data(), bytes.size());
        try
        {
            unlock();
        }
        catch (...)
        {
            if (locked_)
            {
                SetLastError(ERROR_SUCCESS);
                (void)GlobalUnlock(handle_);
                locked_ = false;
            }
            GlobalFree(handle_);
            handle_ = nullptr;
            throw;
        }
    }

    ~GlobalMemory()
    {
        if (locked_)
        {
            SetLastError(ERROR_SUCCESS);
            if (!GlobalUnlock(handle_) && GetLastError() != ERROR_SUCCESS)
                OutputDebugStringW(L"clipss2: GlobalUnlock failed during cleanup.\n");
        }
        if (handle_ != nullptr) GlobalFree(handle_);
    }

    GlobalMemory(const GlobalMemory&) = delete;
    GlobalMemory& operator=(const GlobalMemory&) = delete;

    [[nodiscard]] HGLOBAL get() const noexcept { return handle_; }
    [[nodiscard]] HGLOBAL release() noexcept
    {
        HGLOBAL result = handle_;
        handle_ = nullptr;
        return result;
    }

private:
    void unlock()
    {
        SetLastError(ERROR_SUCCESS);
        const BOOL result = GlobalUnlock(handle_);
        const DWORD error = GetLastError();
        if (!result && error != ERROR_SUCCESS) winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        locked_ = false;
    }

    HGLOBAL handle_{};
    bool locked_{};
};

class OpenClipboardScope final
{
public:
    explicit OpenClipboardScope(HWND owner)
    {
        for (unsigned int attempt = 1; attempt <= open_attempts; ++attempt)
        {
            if (OpenClipboard(owner))
            {
                open_ = true;
                return;
            }
            if (attempt != open_attempts) std::this_thread::sleep_for(open_retry_interval);
        }
        throw std::runtime_error(
            "Clipboard busy: OpenClipboard failed after 10 attempts (maximum wait 180 ms).");
    }

    ~OpenClipboardScope()
    {
        if (open_ && !CloseClipboard())
            OutputDebugStringW(L"clipss2: CloseClipboard failed during cleanup.\n");
    }

    OpenClipboardScope(const OpenClipboardScope&) = delete;
    OpenClipboardScope& operator=(const OpenClipboardScope&) = delete;

    void close()
    {
        if (!open_) return;
        if (!CloseClipboard()) winrt::throw_last_error();
        open_ = false;
    }

private:
    bool open_{};
};

class ClipboardOwnerWindow final
{
public:
    ClipboardOwnerWindow()
    {
        // EmptyClipboard assigns ownership to the HWND passed to OpenClipboard.
        // The documented nullptr path leaves no owner and can make
        // SetClipboardData fail, so use a transaction-only message-only STATIC.
        // Immediate rendering needs no message pump and the window is destroyed
        // after both payloads have transferred to the system.
        window_ = CreateWindowExW(
            0, L"STATIC", L"clipss2 clipboard owner", 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window_ == nullptr) winrt::throw_last_error();
    }

    ~ClipboardOwnerWindow()
    {
        if (window_ != nullptr && !DestroyWindow(window_))
            OutputDebugStringW(L"clipss2: clipboard owner window cleanup failed.\n");
    }

    ClipboardOwnerWindow(const ClipboardOwnerWindow&) = delete;
    ClipboardOwnerWindow& operator=(const ClipboardOwnerWindow&) = delete;
    [[nodiscard]] HWND get() const noexcept { return window_; }

private:
    HWND window_{};
};

[[noreturn]] void throw_set_clipboard_error(
    const char* format,
    DWORD error,
    bool partial)
{
    std::string message = "SetClipboardData(" + std::string{format} + ") failed";
    if (partial)
        message += "; CF_DIBV5 was already transferred, so partial publication may remain";
    message += " (Win32 error " + std::to_string(error) + ").";
    throw std::runtime_error(message);
}
}

DibV5Payload make_dibv5_payload(const image::CpuImage& image)
{
    static_assert(sizeof(BITMAPV5HEADER) == 124);
    if (image.format != image::PixelFormat::bgra8_unorm)
        throw std::invalid_argument("CF_DIBV5 requires a BGRA8 UNORM image.");
    if (image.width == 0 || image.height == 0 ||
        image.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
        image.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
        throw std::invalid_argument("CF_DIBV5 dimensions are invalid or exceed LONG limits.");

    const std::size_t row_bytes = checked_product(
        static_cast<std::size_t>(image.width), bytes_per_pixel,
        "CF_DIBV5 row byte calculation overflowed.");
    const std::size_t pixel_bytes = checked_product(
        row_bytes, static_cast<std::size_t>(image.height),
        "CF_DIBV5 pixel byte calculation overflowed.");
    if (pixel_bytes > std::numeric_limits<DWORD>::max())
        throw std::overflow_error("CF_DIBV5 pixel payload exceeds the header limit.");
    if (image.pixels.size() != pixel_bytes)
        throw std::invalid_argument("CPU image buffer size does not match its dimensions.");
    if (pixel_bytes > std::numeric_limits<std::size_t>::max() - sizeof(BITMAPV5HEADER))
        throw std::overflow_error("CF_DIBV5 total payload calculation overflowed.");

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(image.width);
    header.bV5Height = -static_cast<LONG>(image.height);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(pixel_bytes);
    header.bV5RedMask = 0x00ff0000U;
    header.bV5GreenMask = 0x0000ff00U;
    header.bV5BlueMask = 0x000000ffU;
    header.bV5AlphaMask = 0xff000000U;
    header.bV5CSType = LCS_sRGB;

    DibV5Payload result{
        std::vector<std::byte>(sizeof(header) + pixel_bytes), row_bytes, pixel_bytes};
    std::memcpy(result.bytes.data(), &header, sizeof(header));
    std::memcpy(result.bytes.data() + sizeof(header), image.pixels.data(), pixel_bytes);
    return result;
}

ClipboardPublishResult publish_image_to_clipboard(const image::CpuImage& image)
{
    const auto png_image = image::unpremultiply_bgra8_for_png(image);
    return publish_prepared_image_to_clipboard(prepare_image_for_clipboard(
        image, png_image, codec::png::encode_srgb_png_bytes(png_image)));
}

PreparedClipboardImage prepare_image_for_clipboard(
    const image::CpuImage& dibv5_image,
    const image::CpuImage& png_reference,
    std::vector<std::byte> png)
{
    // Everything capable of failing due to dimensions, encoding, metadata, or
    // pixels completes before OpenClipboard/EmptyClipboard mutates shared state.
    auto dibv5 = make_dibv5_payload(dibv5_image);
    if (dibv5_image.width != png_reference.width ||
        dibv5_image.height != png_reference.height)
        throw std::invalid_argument(
            "Clipboard DIB and PNG reference dimensions do not match.");
    const auto validation = codec::png::validate_screenshot_png(png, png_reference);
    if (validation.file_bytes != png.size())
        throw std::runtime_error("PNG memory validation returned an inconsistent size.");
    return {std::move(dibv5), std::move(png)};
}

ClipboardPublishResult publish_prepared_image_to_clipboard(
    const PreparedClipboardImage& prepared)
{
    const auto& dibv5 = prepared.dibv5;
    const auto& png = prepared.png;

    const UINT png_format = RegisterClipboardFormatW(L"PNG");
    if (png_format == 0) winrt::throw_last_error();
    GlobalMemory dibv5_memory{dibv5.bytes};
    GlobalMemory png_memory{png};

    ClipboardOwnerWindow owner;
    OpenClipboardScope clipboard{owner.get()};
    if (!EmptyClipboard()) winrt::throw_last_error();

    if (SetClipboardData(CF_DIBV5, dibv5_memory.get()) == nullptr)
        throw_set_clipboard_error("CF_DIBV5", GetLastError(), false);
    (void)dibv5_memory.release();

    if (SetClipboardData(png_format, png_memory.get()) == nullptr)
        throw_set_clipboard_error("PNG", GetLastError(), true);
    (void)png_memory.release();
    clipboard.close();

    return ClipboardPublishResult{
        dibv5.bytes.size(),
        png.size(),
        IsClipboardFormatAvailable(CF_DIBV5) != FALSE,
        IsClipboardFormatAvailable(png_format) != FALSE};
}
}
