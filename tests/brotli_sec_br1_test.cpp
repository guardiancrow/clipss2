#include <brotli/decode.h>
#include <brotli/encode.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try
    {
        constexpr std::uint32_t expected_version = 0x01002001; // 1.2.1
        if (BrotliDecoderVersion() != expected_version || BrotliEncoderVersion() != expected_version)
            throw std::runtime_error("Expected Brotli 1.2.1 encoder and decoder DLLs.");
        std::vector<std::uint8_t> source(128 * 1024);
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<std::uint8_t>((i * 17 + i / 251) & 255);
        std::vector<std::uint8_t> compressed(BrotliEncoderMaxCompressedSize(source.size()));
        auto compressed_size = compressed.size();
        if (!BrotliEncoderCompress(5, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
                source.size(), source.data(), &compressed_size, compressed.data()))
            throw std::runtime_error("Brotli compression failed.");
        std::vector<std::uint8_t> decoded(source.size());
        auto decoded_size = decoded.size();
        if (BrotliDecoderDecompress(compressed_size, compressed.data(), &decoded_size, decoded.data()) !=
                BROTLI_DECODER_RESULT_SUCCESS || decoded_size != source.size() || decoded != source)
            throw std::runtime_error("Brotli round trip failed.");
        // Invalid window header. This checks rejection, not a CVE exploit PoC.
        constexpr std::array<std::uint8_t, 4> invalid{255, 255, 255, 255};
        decoded_size = decoded.size();
        if (BrotliDecoderDecompress(invalid.size(), invalid.data(), &decoded_size, decoded.data()) !=
                BROTLI_DECODER_RESULT_ERROR)
            throw std::runtime_error("Invalid Brotli stream accepted.");
        std::cout << "Brotli 1.2.1: DLL versions, round trip and invalid header rejection PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
