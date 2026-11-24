#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/version.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main()
{
    try
    {
        constexpr std::uint32_t expected_version =
            JPEGXL_MAJOR_VERSION * 1000000U + JPEGXL_MINOR_VERSION * 1000U + JPEGXL_PATCH_VERSION;
        const std::string version = std::to_string(JPEGXL_MAJOR_VERSION) + "." +
            std::to_string(JPEGXL_MINOR_VERSION) + "." + std::to_string(JPEGXL_PATCH_VERSION);
        const std::uint32_t encoder_version = JxlEncoderVersion();
        const std::uint32_t decoder_version = JxlDecoderVersion();
        if (encoder_version != expected_version || decoder_version != expected_version)
            throw std::runtime_error("Expected libjxl " + version + " encoder and decoder.");
        {
            // Runner outlives both codec objects that borrow its address.
            const std::unique_ptr<void, decltype(&JxlThreadParallelRunnerDestroy)> runner{
                JxlThreadParallelRunnerCreate(nullptr, 1), &JxlThreadParallelRunnerDestroy};
            const std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> encoder{
                JxlEncoderCreate(nullptr), &JxlEncoderDestroy};
            const std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> decoder{
                JxlDecoderCreate(nullptr), &JxlDecoderDestroy};
            if (!encoder || !decoder || !runner)
                throw std::runtime_error("libjxl encoder/decoder/runner allocation failed.");
            if (JxlEncoderSetParallelRunner(encoder.get(), JxlThreadParallelRunner, runner.get()) != JXL_ENC_SUCCESS ||
                JxlDecoderSetParallelRunner(decoder.get(), JxlThreadParallelRunner, runner.get()) != JXL_DEC_SUCCESS)
                throw std::runtime_error("libjxl parallel runner API failed.");
        }
        std::cout << "libjxl version: " << version << " (encoder=" << encoder_version
                  << ", decoder=" << decoder_version << ")\n"
                  << "Encoder / decoder / thread runner creation, API and destruction: succeeded\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
