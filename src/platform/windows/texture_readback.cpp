#include "texture_readback.h"

#include "d3d_device_internal.h"
#include "frame_geometry.h"
#include "image/row_copy.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/readback_wait.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <thread>

namespace clipss2::platform::windows
{
namespace
{
std::size_t checked_multiply(std::size_t left, std::size_t right)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
    {
        throw std::overflow_error("Texture readback size calculation overflowed.");
    }
    return left * right;
}

struct FormatDescription
{
    image::PixelFormat pixel_format{};
    std::size_t bytes_per_pixel{};
};

FormatDescription describe_format(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return {image::PixelFormat::bgra8_unorm, 4};
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return {image::PixelFormat::rgba16_float, 8};
    default:
        throw DiagnosticError("Source texture format is not supported for readback.");
    }
}

void validate_source_description(const D3D11_TEXTURE2D_DESC& description)
{
    if (description.Width == 0 || description.Height == 0)
    {
        throw DiagnosticError("Source texture dimensions must be non-zero.");
    }
    static_cast<void>(describe_format(description.Format));
    if (description.MipLevels != 1 || description.ArraySize != 1)
    {
        throw DiagnosticError("Source texture must have one mip level and one array slice.");
    }
    if (description.SampleDesc.Count != 1 || description.SampleDesc.Quality != 0)
    {
        throw DiagnosticError("Source texture must be non-multisampled.");
    }
}

void validate_staging_description(
    const D3D11_TEXTURE2D_DESC& source,
    const D3D11_TEXTURE2D_DESC& staging)
{
    if (staging.Width != source.Width || staging.Height != source.Height ||
        staging.Format != source.Format || staging.MipLevels != source.MipLevels ||
        staging.ArraySize != source.ArraySize ||
        staging.SampleDesc.Count != source.SampleDesc.Count ||
        staging.SampleDesc.Quality != source.SampleDesc.Quality)
    {
        throw DiagnosticError("Staging texture is not copy-compatible with the source.");
    }
    if (staging.Usage != D3D11_USAGE_STAGING ||
        staging.CPUAccessFlags != D3D11_CPU_ACCESS_READ ||
        staging.BindFlags != 0 || staging.MiscFlags != 0)
    {
        throw DiagnosticError("Staging texture does not have the required readback flags.");
    }
}

class MappedResource final
{
public:
    MappedResource(
        winrt::com_ptr<ID3D11DeviceContext> context,
        winrt::com_ptr<ID3D11Resource> resource)
        : context_(std::move(context)), resource_(std::move(resource))
    {
    }

    ~MappedResource()
    {
        context_->Unmap(resource_.get(), 0);
    }

    MappedResource(const MappedResource&) = delete;
    MappedResource& operator=(const MappedResource&) = delete;

private:
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<ID3D11Resource> resource_;
};

[[noreturn]] void throw_map_failure(
    ID3D11Device* device, const HRESULT result)
{
    std::string message = format_hresult("GPU staging Map failed", result);
    if (is_device_lost_hresult(result))
    {
        const HRESULT reason = device->GetDeviceRemovedReason();
        message += "\n" + format_hresult("Device removed reason", reason);
    }
    throw DiagnosticError(std::move(message), result);
}
}

TextureReadbackResult readback_texture(
    const D3dDevice& device,
    const winrt::com_ptr<ID3D11Texture2D>& source_texture,
    std::optional<PixelRectangle> crop)
{
    if (!source_texture)
    {
        throw std::invalid_argument("Source texture is null.");
    }

    D3D11_TEXTURE2D_DESC source_description{};
    source_texture->GetDesc(&source_description);
    validate_source_description(source_description);
    const auto format = describe_format(source_description.Format);

    if (crop)
    {
        validate_pixel_bounds(*crop);
        if (crop->left < 0 || crop->top < 0 || static_cast<std::uint64_t>(crop->right) > source_description.Width ||
            static_cast<std::uint64_t>(crop->bottom) > source_description.Height)
            throw std::invalid_argument("Readback crop outside native texture.");
    }
    auto copy_description = source_description;
    if (crop)
    { copy_description.Width = static_cast<UINT>(crop->width()); copy_description.Height = static_cast<UINT>(crop->height()); }

    D3D11_TEXTURE2D_DESC staging_description = copy_description;
    staging_description.Usage = D3D11_USAGE_STAGING;
    staging_description.BindFlags = 0;
    staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_description.MiscFlags = 0;

    const auto native_device = detail::D3dDeviceAccess::native_device(device);
    winrt::com_ptr<ID3D11Texture2D> staging_texture;
    const HRESULT create_result = native_device->CreateTexture2D(
        &staging_description, nullptr, staging_texture.put());
    if (FAILED(create_result))
        throw_hresult_failure("Create D3D11 staging texture failed", create_result);

    D3D11_TEXTURE2D_DESC actual_staging_description{};
    staging_texture->GetDesc(&actual_staging_description);
    validate_staging_description(copy_description, actual_staging_description);

    winrt::com_ptr<ID3D11DeviceContext> context;
    native_device->GetImmediateContext(context.put());
    if (!context)
    {
        throw DiagnosticError("D3D11 immediate context is unavailable.");
    }

    if (crop)
    {
        const D3D11_BOX box{static_cast<UINT>(crop->left),static_cast<UINT>(crop->top),0,
            static_cast<UINT>(crop->right),static_cast<UINT>(crop->bottom),1};
        context->CopySubresourceRegion(staging_texture.get(),0,0,0,0,source_texture.get(),0,&box);
    }
    else context->CopyResource(staging_texture.get(), source_texture.get());

    const std::size_t width = static_cast<std::size_t>(copy_description.Width);
    const std::size_t height = static_cast<std::size_t>(copy_description.Height);
    const std::size_t logical_row_bytes =
        checked_multiply(width, format.bytes_per_pixel);
    const std::size_t expected_buffer_bytes = checked_multiply(logical_row_bytes, height);

    TextureReadbackResult result;
    result.image.width = copy_description.Width;
    result.image.height = copy_description.Height;
    result.image.format = format.pixel_format;
    result.logical_row_bytes = logical_row_bytes;

    {
        const auto staging_resource = staging_texture.as<ID3D11Resource>();
        D3D11_MAPPED_SUBRESOURCE mapping{};
        const auto wait_started = std::chrono::steady_clock::now();
        const auto deadline = wait_started + gpu_readback_timeout;
        while (true)
        {
            const HRESULT map_result = context->Map(
                staging_resource.get(), 0, D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT, &mapping);
            const auto now = std::chrono::steady_clock::now();
            switch (decide_readback_map_result(map_result, now >= deadline))
            {
            case ReadbackWaitAction::succeeded:
                result.wait_duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - wait_started);
                break;
            case ReadbackWaitAction::retry:
            {
                ++result.busy_retry_count;
                const auto remaining = deadline - now;
                const auto retry_delay =
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        gpu_readback_retry_delay);
                std::this_thread::sleep_for(std::min(retry_delay, remaining));
                continue;
            }
            case ReadbackWaitAction::timed_out:
                throw DiagnosticError(
                    "GPU staging Map timed out after 2000 ms "
                    "while waiting for CopyResource completion.");
            case ReadbackWaitAction::failed:
                throw_map_failure(native_device.get(), map_result);
            }
            break;
        }
        MappedResource mapped{context, staging_resource};
        if (mapping.pData == nullptr)
        {
            throw DiagnosticError("Mapped staging texture returned a null data pointer.");
        }
        const auto layout = readback_layout(width, height, format.bytes_per_pixel, mapping.RowPitch);
        const auto mapped_bytes = layout.mapped_bytes;
        if (mapped_bytes > std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(mapping.pData))
            capture_stage({"capture.geometry", "Mapped address bounds"}, [] { throw DiagnosticError("reason=size-overflow"); });
        const auto* mapped_data = static_cast<const std::byte*>(mapping.pData);
        result.image.pixels = image::copy_rows_contiguous(
            std::span<const std::byte>{mapped_data, mapped_bytes},
            mapping.RowPitch,
            logical_row_bytes,
            height);
        result.mapped_row_pitch = mapping.RowPitch;
    }

    if (result.image.pixels.size() != expected_buffer_bytes)
    {
        throw DiagnosticError("CPU buffer size does not match the texture dimensions.");
    }
    return result;
}
}
