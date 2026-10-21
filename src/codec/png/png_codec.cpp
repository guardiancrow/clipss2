#include "codec/png/png_codec.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include <png.h>

namespace clipss2::codec::png
{
namespace
{
constexpr std::size_t rgba8_bytes_per_pixel = 4;

std::FILE* open_output_file(const std::filesystem::path& path) noexcept
{
#if defined(_MSC_VER)
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0)
    {
        return nullptr;
    }
    return file;
#else
    return std::fopen(path.string().c_str(), "wb");
#endif
}

std::size_t checked_image_bytes(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("PNG image dimensions must be non-zero.");
    }

    const auto width_size = static_cast<std::size_t>(width);
    const auto height_size = static_cast<std::size_t>(height);
    if (width_size > std::numeric_limits<std::size_t>::max() / height_size ||
        width_size * height_size >
            std::numeric_limits<std::size_t>::max() / rgba8_bytes_per_pixel)
    {
        throw std::overflow_error("PNG image dimensions overflow the address space.");
    }
    return width_size * height_size * rgba8_bytes_per_pixel;
}

class FileHandle final
{
public:
    explicit FileHandle(const std::filesystem::path& path)
        : file_(open_output_file(path))
    {
        if (file_ == nullptr)
        {
            throw std::runtime_error("Could not open the PNG output file.");
        }
    }

    ~FileHandle()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    [[nodiscard]] std::FILE* get() const noexcept
    {
        return file_;
    }

    void close()
    {
        std::FILE* file = file_;
        file_ = nullptr;
        if (std::fclose(file) != 0)
        {
            throw std::runtime_error("Could not finalize the PNG output file.");
        }
    }

private:
    std::FILE* file_{};
};

struct PngErrorState
{
    char message[256]{};
};

void PNGAPI png_error_callback(png_structp png, png_const_charp message)
{
    auto* state = static_cast<PngErrorState*>(png_get_error_ptr(png));
    if (state != nullptr)
    {
        const char* source = message != nullptr ? message : "unknown libpng error";
        std::snprintf(state->message, sizeof(state->message), "%s", source);
    }
    png_longjmp(png, 1);
}

class PngWriteHandle final
{
public:
    explicit PngWriteHandle(PngErrorState& error_state)
        : png_(png_create_write_struct(
              PNG_LIBPNG_VER_STRING, &error_state, png_error_callback, nullptr))
    {
        if (png_ == nullptr)
        {
            throw std::runtime_error("png_create_write_struct failed.");
        }

        info_ = png_create_info_struct(png_);
        if (info_ == nullptr)
        {
            png_destroy_write_struct(&png_, nullptr);
            throw std::runtime_error("png_create_info_struct failed.");
        }
    }

    ~PngWriteHandle()
    {
        png_destroy_write_struct(&png_, &info_);
    }

    PngWriteHandle(const PngWriteHandle&) = delete;
    PngWriteHandle& operator=(const PngWriteHandle&) = delete;

    [[nodiscard]] png_structp png() const noexcept
    {
        return png_;
    }

    [[nodiscard]] png_infop info() const noexcept
    {
        return info_;
    }

private:
    png_structp png_{};
    png_infop info_{};
};

class PngImageHandle final
{
public:
    PngImageHandle()
    {
        image_.version = PNG_IMAGE_VERSION;
    }

    ~PngImageHandle()
    {
        png_image_free(&image_);
    }

    PngImageHandle(const PngImageHandle&) = delete;
    PngImageHandle& operator=(const PngImageHandle&) = delete;

    [[nodiscard]] png_image& get() noexcept
    {
        return image_;
    }

private:
    png_image image_{};
};

#if defined(_MSC_VER)
#pragma warning(push)
// This is the only setjmp boundary. It deliberately contains no non-trivial
// automatic objects, so libpng's longjmp cannot bypass C++ destructors.
#pragma warning(disable : 4611)
#endif
struct MemoryWriteState
{
    std::span<std::byte> destination;
    std::size_t written{};
};

void PNGAPI write_to_memory(png_structp png, png_bytep data, png_size_t size) noexcept
{
    auto* state = static_cast<MemoryWriteState*>(png_get_io_ptr(png));
    if (state == nullptr || state->written > state->destination.size() ||
        size > state->destination.size() - state->written)
    {
        png_error(png, "PNG memory output exceeded its checked capacity.");
        return;
    }
    std::memcpy(state->destination.data() + state->written, data, size);
    state->written += size;
}

void PNGAPI flush_memory(png_structp) noexcept
{
}

bool write_png_body(
    png_structp png,
    png_infop info,
    MemoryWriteState* output,
    png_const_bytep pixels,
    png_uint_32 width,
    png_uint_32 height,
    png_size_t row_bytes) noexcept
{
    if (setjmp(png_jmpbuf(png)) != 0)
    {
        return false;
    }

    png_set_write_fn(png, output, write_to_memory, flush_memory);
    png_set_IHDR(
        png,
        info,
        width,
        height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB_gAMA_and_cHRM(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_write_info(png, info);

    // Input rows are B,G,R,A. This swaps B/R while leaving alpha last.
    png_set_bgr(png);
    for (png_uint_32 y = 0; y < height; ++y)
    {
        png_write_row(png, pixels + static_cast<png_size_t>(y) * row_bytes);
    }
    png_write_end(png, info);
    return true;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

std::string png_error_message(const char* prefix, const PngErrorState& state)
{
    std::string message{prefix};
    if (state.message[0] != '\0')
    {
        message += ": ";
        message += state.message;
    }
    return message;
}

std::size_t checked_png_capacity(std::uint32_t width, std::uint32_t height)
{
    // Each filtered row has one extra byte. The zlib compressBound formula is
    // followed by deliberately conservative allowance for libpng chunk framing
    // (12 bytes per 1 KiB) and all fixed chunks emitted by this encoder.
    const auto row_bytes = checked_image_bytes(width, 1);
    if (row_bytes == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("PNG filtered row size overflowed.");
    const auto filtered_row = row_bytes + 1;
    const auto height_size = static_cast<std::size_t>(height);
    if (filtered_row > std::numeric_limits<std::size_t>::max() / height_size)
        throw std::overflow_error("PNG filtered image size overflowed.");
    const auto source = filtered_row * height_size;
    auto add_checked = [](std::size_t left, std::size_t right) {
        if (right > std::numeric_limits<std::size_t>::max() - left)
            throw std::overflow_error("PNG memory capacity overflowed.");
        return left + right;
    };
    std::size_t bound = source;
    bound = add_checked(bound, source >> 12);
    bound = add_checked(bound, source >> 14);
    bound = add_checked(bound, source >> 25);
    bound = add_checked(bound, 13);
    const auto chunk_count = bound / 1024 + 1;
    if (chunk_count > std::numeric_limits<std::size_t>::max() / 12)
        throw std::overflow_error("PNG chunk framing capacity overflowed.");
    bound = add_checked(bound, chunk_count * 12);
    return add_checked(bound, 1024);
}
}

PngEncodeResult encode_srgb_png(
    const image::CpuImage& image,
    const std::filesystem::path& output_path)
{
    const auto bytes = encode_srgb_png_bytes(image);
    return write_png_bytes(bytes, output_path);
}

PngEncodeResult write_png_bytes(
    std::span<const std::byte> bytes,
    const std::filesystem::path& output_path)
{
    if (bytes.empty())
        throw std::invalid_argument("PNG byte payload is empty.");

    const auto parent = output_path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent);
    }

    bool file_created = false;
    try
    {
        FileHandle file{output_path};
        file_created = true;
        if (std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size())
        {
            throw std::runtime_error("Could not write the complete PNG output file.");
        }
        file.close();
    }
    catch (...)
    {
        if (file_created)
        {
            std::error_code ignored;
            std::filesystem::remove(output_path, ignored);
        }
        throw;
    }

    return PngEncodeResult{std::filesystem::absolute(output_path), bytes.size()};
}

std::vector<std::byte> encode_srgb_png_bytes(const image::CpuImage& image)
{
    if (image.format != image::PixelFormat::bgra8_unorm)
    {
        throw std::invalid_argument("PNG encoder requires a BGRA8 UNORM image.");
    }
    const std::size_t expected_bytes = checked_image_bytes(image.width, image.height);
    if (image.pixels.size() != expected_bytes)
    {
        throw std::invalid_argument("CPU image buffer size does not match its dimensions.");
    }
    if (image.width > PNG_UINT_31_MAX || image.height > PNG_UINT_31_MAX)
    {
        throw std::invalid_argument("PNG dimensions exceed the libpng limit.");
    }

    PngErrorState error_state{};
    std::vector<std::byte> bytes(checked_png_capacity(image.width, image.height));
    MemoryWriteState output{bytes, 0};
    bool write_succeeded = false;
    {
        PngWriteHandle writer{error_state};
        write_succeeded = write_png_body(
            writer.png(),
            writer.info(),
            &output,
            reinterpret_cast<png_const_bytep>(image.pixels.data()),
            image.width,
            image.height,
            static_cast<png_size_t>(image.width) * rgba8_bytes_per_pixel);
    }

    if (!write_succeeded)
    {
        throw std::runtime_error(png_error_message("PNG encoding failed", error_state));
    }
    if (output.written == 0)
    {
        throw std::runtime_error("PNG memory output is empty.");
    }
    bytes.resize(output.written);
    return bytes;
}

image::CpuImage decode_rgba8_png(const std::filesystem::path& input_path)
{
    PngImageHandle reader;
    auto& png_image = reader.get();
    if (png_image_begin_read_from_file(&png_image, input_path.string().c_str()) == 0)
    {
        throw std::runtime_error(std::string{"PNG decode initialization failed: "} +
                                 png_image.message);
    }

    const std::size_t expected_bytes = checked_image_bytes(png_image.width, png_image.height);
    png_image.format = PNG_FORMAT_RGBA;

    image::CpuImage decoded{
        png_image.width,
        png_image.height,
        image::PixelFormat::rgba8_unorm,
        std::vector<std::byte>(expected_bytes)};
    if (png_image_finish_read(
            &png_image, nullptr, decoded.pixels.data(), 0, nullptr) == 0)
    {
        throw std::runtime_error(std::string{"PNG decode failed: "} + png_image.message);
    }
    return decoded;
}

image::CpuImage decode_rgba8_png(std::span<const std::byte> input)
{
    if (input.empty())
    {
        throw std::invalid_argument("PNG memory input is empty.");
    }
    PngImageHandle reader;
    auto& png_image = reader.get();
    if (png_image_begin_read_from_memory(
            &png_image, input.data(), input.size()) == 0)
    {
        throw std::runtime_error(std::string{"PNG memory decode initialization failed: "} +
                                 png_image.message);
    }

    const std::size_t expected_bytes = checked_image_bytes(png_image.width, png_image.height);
    png_image.format = PNG_FORMAT_RGBA;
    image::CpuImage decoded{
        png_image.width,
        png_image.height,
        image::PixelFormat::rgba8_unorm,
        std::vector<std::byte>(expected_bytes)};
    if (png_image_finish_read(
            &png_image, nullptr, decoded.pixels.data(), 0, nullptr) == 0)
    {
        throw std::runtime_error(std::string{"PNG memory decode failed: "} +
                                 png_image.message);
    }
    return decoded;
}
}
