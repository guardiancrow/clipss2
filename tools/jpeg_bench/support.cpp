#include "support.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace bench
{
namespace
{
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
int byte(std::span<const std::byte> data, std::size_t at)
{ require(at < data.size(), "Truncated JPEG structure"); return std::to_integer<int>(data[at]); }
int word(std::span<const std::byte> data, std::size_t at)
{ return byte(data, at) * 256 + byte(data, at + 1); }
struct Crypto
{
    BCRYPT_ALG_HANDLE provider{};
    BCRYPT_HASH_HANDLE hash{};
    ~Crypto() { if (hash) BCryptDestroyHash(hash); if (provider) BCryptCloseAlgorithmProvider(provider, 0); }
};
}

std::string sha256(std::span<const std::byte> bytes)
{
    require(bytes.size() <= std::numeric_limits<ULONG>::max(), "Hash input too large");
    std::vector<unsigned char> object; // Outlives the hash, including exceptions.
    Crypto crypto;
    require(BCryptOpenAlgorithmProvider(&crypto.provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
        "SHA256 provider failure");
    DWORD size{}, written{};
    require(BCryptGetProperty(crypto.provider, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&size), sizeof(size), &written, 0) == 0, "SHA256 property failure");
    object.resize(size);
    require(BCryptCreateHash(crypto.provider, &crypto.hash, object.data(), size, nullptr, 0, 0) == 0,
        "SHA256 create failure");
    std::array<unsigned char, 32> digest{};
    require(BCryptHashData(crypto.hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
        static_cast<ULONG>(bytes.size()), 0) == 0 &&
        BCryptFinishHash(crypto.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0,
        "SHA256 calculation failure");
    // The BCrypt hash must close before its backing object buffer.
    BCryptDestroyHash(crypto.hash); crypto.hash = nullptr;
    std::ostringstream result;
    for (auto v : digest) result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(v);
    return result.str();
}

std::vector<Source> corpus()
{
    std::vector<Source> result;
    const std::array<std::string, 7> categories{"ui_text", "mixed_ui", "gradient", "high_frequency",
        "photo_like", "ui_large", "mixed_large"};
    for (std::size_t kind = 0; kind < categories.size(); ++kind)
    {
        const unsigned w = kind == 5 ? 1920U : kind == 6 ? 2560U : 768U;
        const unsigned h = kind == 5 ? 1080U : kind == 6 ? 1440U : 512U;
        clipss2::image::CpuImage bgra{w, h, clipss2::image::PixelFormat::bgra8_unorm,
            std::vector<std::byte>(static_cast<std::size_t>(w) * h * 4)};
        std::uint32_t seed = 0x5a17c9e3;
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
        {
            int r{}, g{}, b{}, a = 255;
            const auto mode = kind == 5 ? 0 : kind == 6 ? 1 : kind;
            if (mode == 0)
            {
                const bool dark = x < w / 2;
                r = g = b = dark ? 22 : 238;
                if (y % 32 < 14 && x % 18 < 2) r = g = b = dark ? 240 : 12;
                if (y % 64 == 20 || x % 160 == 100) { r = 20; g = 145; b = 235; }
                if ((x / 96 + y / 64) % 7 == 0 && y % 64 > 32) { r = 230; g = 40; b = 55; }
            }
            else if (mode == 1)
            {
                r = 30 + static_cast<int>(x * 180 / w); g = 35 + static_cast<int>(y * 180 / h); b = 155;
                if (x % 180 < 90 && y % 100 < 55) { r = 230; g = 225; b = 205; }
                if (x % 180 < 75 && y % 100 < 2) { r = 10; g = 45; b = 190; }
                // Translucent canonicalization exercised once, outside all encoders.
                if (x > w * 3 / 4) { a = 128; r /= 2; g /= 2; b /= 2; }
            }
            else if (mode == 2)
            {
                r = static_cast<int>(x * 255 / (w - 1)); g = static_cast<int>(y * 255 / (h - 1));
                b = y < h / 2 ? 120 + static_cast<int>(x * 20 / w) : static_cast<int>((x + y) * 255 / (w + h));
            }
            else if (mode == 3)
            {
                const auto cell = y < h / 2 ? 1U : 2U;
                const bool on = (x / cell + y / cell) % 2 == 0;
                r = on ? 240 : 10; g = on ? 25 : 225; b = x % 8 < 4 ? 255 : 5;
            }
            else
            {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                const int noise = static_cast<int>(seed & 31U) - 15;
                const int hill = static_cast<int>(h / 2) + static_cast<int>((x / 60 % 5) * 12);
                if (static_cast<int>(y) < hill)
                { r = 110 + static_cast<int>(y * 90 / h); g = 160 + static_cast<int>(y * 60 / h); b = 220; }
                else
                { r = 60 + noise; g = 100 + noise + static_cast<int>(y * 40 / h); b = 35 + noise; }
                if ((static_cast<int>(x) - 580) * (static_cast<int>(x) - 580) +
                    (static_cast<int>(y) - 90) * (static_cast<int>(y) - 90) < 1800)
                { r = 250; g = 220; b = 120; }
            }
            const auto at = (static_cast<std::size_t>(y) * w + x) * 4;
            bgra.pixels[at] = std::byte{static_cast<unsigned char>(b)};
            bgra.pixels[at + 1] = std::byte{static_cast<unsigned char>(g)};
            bgra.pixels[at + 2] = std::byte{static_cast<unsigned char>(r)};
            bgra.pixels[at + 3] = std::byte{static_cast<unsigned char>(a)};
        }
        result.push_back({"s" + std::to_string(kind + 1), categories[kind],
            clipss2::codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(bgra), false});
    }
    return result;
}

Metrics measure(const Rgb& reference, const Rgb& decoded)
{
    const auto count = clipss2::codec::jpeg::checked_rgb_image_bytes(reference.width, reference.height);
    require(reference.width == decoded.width && reference.height == decoded.height &&
        reference.pixels.size() == count && decoded.pixels.size() == count, "Metric dimension/buffer mismatch");
    double sum{}, squares{}, edge{}, flat{}, colored{};
    Metrics result;
    for (unsigned y = 0; y < reference.height; ++y) for (unsigned x = 0; x < reference.width; ++x)
    {
        const auto at = (static_cast<std::size_t>(y) * reference.width + x) * 3;
        int difference{};
        for (const auto [nx, ny] : std::array<std::pair<unsigned, unsigned>, 4>{{
            {x ? x - 1 : x, y}, {std::min(x + 1, reference.width - 1), y},
            {x, y ? y - 1 : y}, {x, std::min(y + 1, reference.height - 1)}}})
        {
            const auto neighbor = (static_cast<std::size_t>(ny) * reference.width + nx) * 3;
            for (std::size_t c = 0; c < 3; ++c)
                difference = std::max(difference, std::abs(byte(reference.pixels, at + c) - byte(reference.pixels, neighbor + c)));
        }
        const int chroma = std::max({byte(reference.pixels, at), byte(reference.pixels, at + 1), byte(reference.pixels, at + 2)}) -
            std::min({byte(reference.pixels, at), byte(reference.pixels, at + 1), byte(reference.pixels, at + 2)});
        const bool is_edge = difference >= 32, is_flat = difference == 0, is_colored = is_edge && chroma >= 48;
        for (std::size_t c = 0; c < 3; ++c)
        {
            const int error = std::abs(byte(reference.pixels, at + c) - byte(decoded.pixels, at + c));
            sum += error; squares += static_cast<double>(error) * error;
            result.maximum = std::max(result.maximum, error);
            if (is_edge) edge += error;
            if (is_flat) flat += error;
            if (is_colored) colored += error;
        }
        result.edge_count += is_edge; result.flat_count += is_flat; result.colored_count += is_colored;
    }
    result.mae = sum / static_cast<double>(count); result.rmse = std::sqrt(squares / static_cast<double>(count));
    result.psnr = result.rmse == 0 ? std::numeric_limits<double>::infinity() : 20 * std::log10(255 / result.rmse);
    const auto average = [](double total, std::size_t n) { return n ? total / (3 * static_cast<double>(n)) : std::numeric_limits<double>::quiet_NaN(); };
    result.edge = average(edge, result.edge_count); result.flat = average(flat, result.flat_count); result.colored = average(colored, result.colored_count);
    return result;
}

Structure inspect(std::span<const std::byte> data)
{
    require(data.size() >= 4 && data.size() <= clipss2::codec::jpeg::maximum_jpeg_bytes &&
        byte(data, 0) == 255 && byte(data, 1) == 216, "Invalid JPEG SOI/size");
    Structure result;
    std::size_t at = 2; bool frame{}, scan{}, entropy{};
    while (at < data.size())
    {
        require(byte(data, at++) == 255, "Invalid JPEG marker boundary");
        int marker = byte(data, at++);
        while (marker == 255) marker = byte(data, at++);
        if (entropy && marker == 0) continue;
        if (entropy && marker >= 208 && marker <= 215) continue;
        entropy = false;
        std::ostringstream name; name << std::hex << marker;
        result.markers += (result.markers.empty() ? "" : ";") + name.str();
        if (marker == 217)
        { require(frame && scan && at == data.size(), "JPEG missing frame/scan or trailing data"); return result; }
        require(marker != 0 && marker != 216 && marker != 1 && (marker < 208 || marker > 215), "Unexpected JPEG marker");
        const auto length = static_cast<std::size_t>(word(data, at));
        require(length >= 2 && length <= data.size() - at, "Truncated JPEG segment");
        const auto payload = at + 2;
        if ((marker >= 224 && marker <= 239) || marker == 254)
        {
            result.application_markers += (result.application_markers.empty() ? "" : ";") + name.str() + ":";
            for (auto i = payload; i < std::min(at + length, payload + 32); ++i)
            { std::ostringstream octet; octet << std::hex << std::setw(2) << std::setfill('0') << byte(data, i); result.application_markers += octet.str(); }
        }
        if (marker == 192 || marker == 194)
        {
            require(!frame && length == 17 && byte(data, payload) == 8 && byte(data, payload + 5) == 3,
                "Benchmark requires 8-bit three-component JPEG");
            frame = true; result.progressive = marker == 194;
            result.height = static_cast<unsigned>(word(data, payload + 1)); result.width = static_cast<unsigned>(word(data, payload + 3));
            const int y_sampling = byte(data, payload + 7);
            require(byte(data, payload + 10) == 17 && byte(data, payload + 13) == 17, "Unsupported chroma sampling");
            result.sampling = y_sampling == 17 ? "444" : y_sampling == 34 ? "420" : y_sampling == 33 ? "422" : "other";
        }
        at += length;
        if (marker == 218)
        {
            require(frame && length >= 6, "Invalid JPEG scan"); scan = true; entropy = true;
            while (at < data.size())
            {
                if (byte(data, at) != 255) { ++at; continue; }
                const auto next = byte(data, at + 1);
                if (next == 0 || (next >= 208 && next <= 215)) { at += 2; continue; }
                break;
            }
        }
    }
    throw std::runtime_error("JPEG missing EOI");
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path)
{
    const auto size = std::filesystem::file_size(path);
    require(size > 0 && size <= clipss2::codec::jpeg::maximum_jpeg_bytes, "Input file outside byte limit");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size));
    require(static_cast<bool>(input), "Input read failure"); return result;
}
void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); output.close();
    require(static_cast<bool>(output), "Artifact write failure");
}
void write_ppm(const std::filesystem::path& path, const Rgb& rgb)
{
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << rgb.width << ' ' << rgb.height << "\n255\n";
    output.write(reinterpret_cast<const char*>(rgb.pixels.data()), static_cast<std::streamsize>(rgb.pixels.size())); output.close();
    require(static_cast<bool>(output), "PPM write failure");
}
Rgb read_ppm(const std::filesystem::path& path)
{
    // Deliberately strict canonical P6, no comments/profiles/ambiguous gamma.
    std::ifstream input(path, std::ios::binary); std::string magic; unsigned w{}, h{}, max{};
    input >> magic >> w >> h >> max;
    require(input && magic == "P6" && max == 255 && input.get() == '\n', "Expected canonical P6 RGB8 PPM");
    Rgb result{w, h, std::vector<std::byte>(clipss2::codec::jpeg::checked_rgb_image_bytes(w, h))};
    input.read(reinterpret_cast<char*>(result.pixels.data()), static_cast<std::streamsize>(result.pixels.size()));
    require(input && input.peek() == EOF, "Truncated/extra PPM data"); return result;
}
std::string number(double value)
{
    if (std::isnan(value)) return "NA";
    if (std::isinf(value)) return "INF";
    std::ostringstream out; out << std::setprecision(10) << value; return out.str();
}
}
