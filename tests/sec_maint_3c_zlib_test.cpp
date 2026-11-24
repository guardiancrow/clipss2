// Copyright (c) 2026 guardiancrow. MIT.
#include <windows.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path module_dir(HMODULE module)
{
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    require(length > 0 && length < std::size(path), "Cannot identify zlib module.");
    return std::filesystem::path(path).parent_path();
}

struct Streams
{
    z_stream encoder{};
    z_stream decoder{};
    ~Streams()
    {
        if (encoder.state)
            (void)deflateEnd(&encoder);
        if (decoder.state)
            (void)inflateEnd(&decoder);
    }
};

// This is test data only. Expose small input and output blocks independently.
void supply(z_stream& stream, std::span<const Bytef> input, std::size_t& supplied, uInt block)
{
    if (stream.avail_in == 0 && supplied < input.size())
    {
        const auto count = std::min<std::size_t>(block, input.size() - supplied);
        stream.next_in = const_cast<Bytef*>(input.data() + supplied);
        stream.avail_in = static_cast<uInt>(count);
        supplied += count;
    }
}

void set_output(z_stream& stream, std::span<Bytef> block)
{
    block.front() = 91;
    block.back() = 37;
    stream.next_out = block.data() + 1;
    stream.avail_out = static_cast<uInt>(block.size() - 2);
}

void collect(z_stream& stream, std::span<Bytef> block, std::vector<Bytef>& output)
{
    require(block.front() == 91 && block.back() == 37 && stream.avail_out <= block.size() - 2,
            "zlib wrote beyond the offered output window.");
    output.insert(output.end(), block.begin() + 1, block.end() - 1 - stream.avail_out);
}

std::vector<Bytef> encode(z_stream& stream, std::span<const Bytef> input)
{
    std::vector<Bytef> compressed;
    std::array<Bytef, 113> block{};
    std::size_t supplied = 0;
    for (;;)
    {
        supply(stream, input, supplied, 509);
        set_output(stream, block);
        const auto status = deflate(&stream, supplied == input.size() ? Z_FINISH : Z_NO_FLUSH);
        collect(stream, block, compressed);
        if (status == Z_STREAM_END)
            break;
        require(status == Z_OK, "Streaming deflate failed.");
    }
    require(stream.total_in == input.size() && stream.total_out == compressed.size(),
            "Unexpected deflate byte totals.");
    return compressed;
}

bool decode(z_stream& stream, std::span<const Bytef> input, std::vector<Bytef>& decoded)
{
    std::array<Bytef, 259> block{};
    std::size_t supplied = 0;
    for (;;)
    {
        supply(stream, input, supplied, 23);
        set_output(stream, block);
        const auto status = inflate(&stream, Z_NO_FLUSH);
        collect(stream, block, decoded);
        if (status == Z_STREAM_END)
            return stream.total_in == input.size() && stream.total_out == decoded.size();
        if (status == Z_BUF_ERROR && supplied == input.size() && stream.avail_in == 0)
            return false;
        require(status == Z_OK, "Streaming inflate failed.");
    }
}
}

int main()
{
    try
    {
        const auto module = GetModuleHandleW(L"libz.dll");
        require(std::string_view(ZLIB_VERSION) == "1.4" && std::string_view(zlibVersion()) == "1.4" &&
                    module && std::filesystem::equivalent(module_dir(module), module_dir(nullptr)) &&
                    !GetModuleHandleW(L"z.dll"), "Unexpected zlib build or loaded DLL.");

        std::vector<Bytef> input(65537);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<Bytef>((i * 29U + (i / 127U) * 41U) & 255U);
        Streams streams;
        require(deflateInit2(&streams.encoder, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS, 8,
                             Z_DEFAULT_STRATEGY) == Z_OK &&
                    inflateInit2(&streams.decoder, MAX_WBITS) == Z_OK, "zlib initialization failed.");
        const auto compressed = encode(streams.encoder, input);
        std::vector<Bytef> decoded;
        require(decode(streams.decoder, compressed, decoded) && decoded == input, "Round trip differs.");
        require(inflateReset2(&streams.decoder, MAX_WBITS) == Z_OK, "inflateReset2 failed.");
        decoded.clear();
        require(!decode(streams.decoder, std::span{compressed}.first(compressed.size() - 1), decoded),
                "Truncated stream reported completion.");
        require(inflateReset(&streams.decoder) == Z_OK && deflateReset(&streams.encoder) == Z_OK,
                "Stream reset after incomplete input failed.");
        input.front() ^= 7;
        const auto second = encode(streams.encoder, input);
        decoded.clear();
        require(decode(streams.decoder, second, decoded) && decoded == input, "Reset round trip differs.");
        require(deflateEnd(&streams.encoder) == Z_OK && inflateEnd(&streams.decoder) == Z_OK,
                "Stream cleanup failed.");
        require(deflateInit(&streams.encoder, Z_DEFAULT_COMPRESSION) == Z_OK &&
                    inflateInit(&streams.decoder) == Z_OK, "Stream reinitialization failed.");
        decoded.clear();
        const auto third = encode(streams.encoder, input);
        require(decode(streams.decoder, third, decoded) && decoded == input, "Reinitialized round trip differs.");
        std::cout << "zlib 1.4 app-local stream completion, boundaries, reset and truncation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
