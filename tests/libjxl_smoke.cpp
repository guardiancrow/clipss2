#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

int main()
{
    try
    {
        const std::uint32_t encoder_version = JxlEncoderVersion();
        const std::uint32_t decoder_version = JxlDecoderVersion();
        if (encoder_version != 12000 || decoder_version != 12000)
            throw std::runtime_error("Expected libjxl 0.12.0 encoder and decoder.");
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
        std::cout << "libjxl version: 0.12.0 (encoder=" << encoder_version
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
