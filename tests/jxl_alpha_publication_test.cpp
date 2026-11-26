#include "capture/capture_image_output.h"
#include "codec/jxl/jxl_internal.h"
#include "platform/windows/file_publication.h"

#include <jxl/decode.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
using namespace clipss2;
using namespace clipss2::platform::windows;
namespace fs = std::filesystem;

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

class ScratchDirectory final
{
public:
    ScratchDirectory() : path{fs::temp_directory_path() /
        ("clipss2-jxl-alpha-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))}
    { require(fs::create_directory(path), "Could not create JXL test directory"); }

    ~ScratchDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;
    const fs::path path;
};

enum class Pattern { ui, photo, mixed, transparent };

image::CpuImage fixture(unsigned width, unsigned height, Pattern pattern)
{
    image::CpuImage image{width, height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4)};
    for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x)
    {
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
        const unsigned alpha = pattern == Pattern::transparent ? 0 :
            pattern == Pattern::mixed ? (x % 3 == 0 ? 0 : x % 3 == 1 ? 128 : 255) : 255;
        for (unsigned c = 0; c != 3; ++c)
        {
            const unsigned color = pattern == Pattern::ui
                ? ((y % 13 == 0 || x % 11 < 2) ? 25 + c * 15 : 235 - c * 10)
                : (x / (c + 2) + (y + c * 71) / 3 + ((x / 11) ^ (y / 7))) % 256;
            image.pixels[i + c] = static_cast<std::byte>((color * alpha + 127) / 255);
        }
        image.pixels[i + 3] = static_cast<std::byte>(alpha);
    }
    return image;
}

void check_alpha_header(std::span<const std::byte> bytes)
{
    const std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> decoder{JxlDecoderCreate(nullptr), JxlDecoderDestroy};
    require(bool(decoder), "Could not create decoder for alpha header");
    require(JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO) == JXL_DEC_SUCCESS,
        "Could not subscribe to basic info");
    require(JxlDecoderSetInput(decoder.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()) ==
        JXL_DEC_SUCCESS, "Could not supply JXL codestream");
    JxlDecoderCloseInput(decoder.get());
    require(JxlDecoderProcessInput(decoder.get()) == JXL_DEC_BASIC_INFO, "Could not read JXL basic info");
    JxlBasicInfo info{};
    require(JxlDecoderGetBasicInfo(decoder.get(), &info) == JXL_DEC_SUCCESS &&
        info.num_extra_channels == 1 && info.alpha_bits == 8 && !info.alpha_premultiplied,
        "The codestream does not declare one straight 8-bit alpha channel");
}

void exercise(const fs::path& directory, const image::CpuImage& input, capture::FileCompression compression)
{
    require(fs::create_directory(directory), "Could not create case directory");
    constexpr auto format = image::ImageFileFormat::jxl;
    const auto output = capture::prepare_capture_image_output(input, capture::CaptureDestination::file, format, {}, compression);
    require(output.lossless_source.has_value() && !output.jxl_bytes.empty(), "No JXL output prepared");
    const auto& source = *output.lossless_source;
    const auto validate = [&](std::span<const std::byte> bytes)
    { capture::validate_capture_file_payload(format, compression, bytes, source); };

    check_alpha_header(output.jxl_bytes);
    const bool lossless = compression.mode == capture::FileCompressionMode::lossless;
    const auto policy = lossless ? codec::jxl::detail::DecodePolicy::lossless : codec::jxl::detail::DecodePolicy::lossy;
    const auto decoded = codec::jxl::detail::decode_and_inspect(output.jxl_bytes, policy, &source).pixels;
    require(decoded.pixels.size() == source.pixels.size(), "Decoded pixel count changed");
    for (std::size_t i = 3; i < source.pixels.size(); i += 4)
        require(decoded.pixels[i] == source.pixels[i], "A decoded alpha pixel changed");
    if (lossless) require(decoded.pixels == source.pixels, "Lossless RGB changed");

    const FileOutputOptions storage{directory, FilenamePolicy::timestamp, false};
    const fs::path final = publish_file_payload(storage, ".jxl", output.jxl_bytes, validate);
    require(fs::is_regular_file(final) && fs::file_size(final) == output.jxl_bytes.size(), "No complete JXL file was published");
    bool checked = false;
    bool rejected = false;
    const auto truncated = std::span{output.jxl_bytes}.first(output.jxl_bytes.size() / 2);
    try
    {
        (void)publish_file_payload(storage, ".jxl", truncated, [&](std::span<const std::byte> bytes)
        {
            checked = true;
            validate(bytes);
        });
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(checked && rejected, "Truncated JXL was not rejected at validation");
    for (const auto& entry : fs::directory_iterator(directory))
        require(entry.path() == final, "Temporary or unintended file remains after publication");
}
}

int main()
{
    try
    {
        ScratchDirectory scratch;
        using clipss2::capture::FileCompressionMode;
        using clipss2::codec::LossyProfile;
        exercise(scratch.path / "opaque-tiny", fixture(1, 1, Pattern::ui), {FileCompressionMode::lossy, LossyProfile::high});
        exercise(scratch.path / "opaque-ui", fixture(65, 37, Pattern::ui), {FileCompressionMode::lossy, LossyProfile::balanced});
        exercise(scratch.path / "opaque-photo", fixture(79, 53, Pattern::photo), {FileCompressionMode::lossy, LossyProfile::small_file});
        exercise(scratch.path / "mixed", fixture(31, 17, Pattern::mixed), {FileCompressionMode::lossy, LossyProfile::balanced});
        exercise(scratch.path / "transparent", fixture(17, 11, Pattern::transparent), {FileCompressionMode::lossy, LossyProfile::balanced});
        exercise(scratch.path / "opaque-lossless", fixture(35, 21, Pattern::ui), {});
        std::cout << "JXL alpha headers, pixels, and file publication: 6 cases passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
