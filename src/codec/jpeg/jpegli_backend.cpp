// jpegli's private jpeglib types never cross this translation unit.
#include "codec/jpeg/jpeg_codec_internal.h"
#include "lib/jpegli/encode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace clipss2::codec::jpeg::detail
{
namespace
{
struct EncodeError {};
struct Compressor final
{
    jpeg_compress_struct info{};
    jpeg_error_mgr error{};
    Compressor()
    {
        info.err = jpegli_std_error(&error);
        error.error_exit = [](j_common_ptr) { throw EncodeError{}; };
        error.emit_message = [](j_common_ptr, int level) { if (level < 0) throw EncodeError{}; };
    }
    ~Compressor() { jpegli_destroy_compress(&info); }
    Compressor(const Compressor&) = delete;
    Compressor& operator=(const Compressor&) = delete;
};
struct Destination final
{
    jpeg_destination_mgr api{}; // First member: codec borrows this object.
    std::vector<std::byte> bytes;
    std::size_t written{};
    std::size_t limit{};
    explicit Destination(std::size_t bound) : limit{bound}
    {
        bytes.resize(std::min<std::size_t>(65536, limit));
        api.init_destination = [](j_compress_ptr info) {
            auto& self = *static_cast<Destination*>(info->client_data);
            self.api.next_output_byte = reinterpret_cast<JOCTET*>(self.bytes.data());
            self.api.free_in_buffer = self.bytes.size();
        };
        api.empty_output_buffer = [](j_compress_ptr info) -> boolean {
            auto& self = *static_cast<Destination*>(info->client_data);
            const auto used = self.bytes.size();
            if (used >= self.limit) throw EncodeError{};
            self.bytes.resize(std::min(self.limit, used * 2));
            self.api.next_output_byte = reinterpret_cast<JOCTET*>(self.bytes.data() + used);
            self.api.free_in_buffer = self.bytes.size() - used;
            return TRUE;
        };
        api.term_destination = [](j_compress_ptr info) {
            auto& self = *static_cast<Destination*>(info->client_data);
            self.written = self.bytes.size() - self.api.free_in_buffer;
        };
    }
};
}
std::vector<std::byte> encode_jpegli_bytes(CanonicalJpegRgb8View source, float distance,
    std::size_t output_limit, long pool_limit)
{
    validate_canonical_jpeg_rgb8(source);
    if (!std::isfinite(distance) || distance < 0 || distance > 25)
        throw std::invalid_argument("Invalid jpegli distance.");
    if (!output_limit || output_limit > maximum_jpeg_bytes || pool_limit <= 0 || pool_limit > 1024L * 1024L * 1024L)
        throw std::invalid_argument("Invalid jpegli internal resource bound.");
    Destination destination{output_limit};
    Compressor compressor;
    try
    {
        auto& info = compressor.info;
        jpegli_create_compress(&info);
        info.mem->max_memory_to_use = pool_limit;
        info.image_width = source.width; info.image_height = source.height;
        info.input_components = 3; info.in_color_space = JCS_RGB;
        jpegli_set_defaults(&info);
        jpegli_set_colorspace(&info, JCS_YCbCr);
        jpegli_use_standard_quant_tables(&info);
        jpegli_set_distance(&info, distance, TRUE);
        jpegli_enable_adaptive_quantization(&info, FALSE);
        jpegli_set_progressive_level(&info, 0);
        jpegli_set_input_format(&info, JPEGLI_TYPE_UINT8, JPEGLI_NATIVE_ENDIAN);
        info.optimize_coding = FALSE; info.arith_code = FALSE;
        info.write_JFIF_header = FALSE; info.write_Adobe_marker = FALSE;
        for (int i = 0; i < 3; ++i) { info.comp_info[i].h_samp_factor = 1; info.comp_info[i].v_samp_factor = 1; }
        info.dest = &destination.api;
        info.client_data = &destination;
        jpegli_start_compress(&info, TRUE);
        while (info.next_scanline < info.image_height)
        {
            auto* row = reinterpret_cast<JSAMPLE*>(const_cast<std::byte*>(
                source.pixels.data() + static_cast<std::size_t>(info.next_scanline) * source.stride));
            if (jpegli_write_scanlines(&info, &row, 1) != 1) throw EncodeError{};
        }
        jpegli_finish_compress(&info);
    }
    catch (const EncodeError&) { throw std::runtime_error("jpegli RGB8 encode failed (native error/warning/output bound)."); }
    destination.bytes.resize(destination.written);
    return std::move(destination.bytes);
}
}
