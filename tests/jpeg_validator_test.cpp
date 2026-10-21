#include "audit/jpeg_output_audit.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/jpeg/jpeg_backend_policy.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
using Bytes = std::vector<std::byte>;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const char* name)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    if (!rejected) throw std::runtime_error(std::string{"Validator accepted: "} + name);
}
std::size_t find_marker(const Bytes& bytes, unsigned int code)
{
    for (std::size_t i = 2; i + 1 < bytes.size(); ++i)
        if (bytes[i] == std::byte{255} && std::to_integer<unsigned int>(bytes[i + 1]) == code) return i;
    throw std::runtime_error("Fixture marker missing.");
}
Bytes with_segment(const Bytes& jpeg, unsigned int marker, std::string_view payload)
{
    require(payload.size() <= 65533, "Negative-test payload too long.");
    const auto length = payload.size() + 2;
    Bytes result{jpeg[0], jpeg[1], std::byte{255}, static_cast<std::byte>(marker),
        static_cast<std::byte>(length >> 8), static_cast<std::byte>(length & 255)};
    for (const auto c : payload) result.push_back(static_cast<std::byte>(c));
    result.insert(result.end(), jpeg.begin() + 2, jpeg.end());
    return result;
}
}

int main()
{
    using namespace clipss2;
    try
    {
        const auto rgb = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(audit::make_jpeg_sdr_fixture());
        const auto jpeg = codec::jpeg::encode_srgb_jpeg_bytes(rgb);
        (void)codec::jpeg::validate_screenshot_jpeg(jpeg, rgb.width, rgb.height);
        const auto metadata_reject = [&](const Bytes& bytes, const char* name) {
            rejects([&] { (void)codec::jpeg::validate_jpeg_markers(bytes); }, name);
        };
        metadata_reject(with_segment(jpeg, 0xfe, "software/user/path/time"), "COM");
        metadata_reject(with_segment(jpeg, 0xe1, std::string_view{"Exif\0\0", 6}), "Exif APP1");
        metadata_reject(with_segment(jpeg, 0xe1, "http://ns.adobe.com/xap/1.0/"), "XMP APP1");
        metadata_reject(with_segment(jpeg, 0xe2, "ICC_PROFILE"), "ICC APP2");
        metadata_reject(with_segment(jpeg, 0xee, "Adobe"), "APP14");
        for (unsigned int marker = 0xe0; marker <= 0xef; ++marker)
            metadata_reject(with_segment(jpeg, marker, "arbitrary private bytes"), "arbitrary APP");
        auto mutated = jpeg;
        const auto app = find_marker(jpeg, 0xe0);
        auto no_jfif = jpeg;
        no_jfif.erase(no_jfif.begin() + static_cast<std::ptrdiff_t>(app),
            no_jfif.begin() + static_cast<std::ptrdiff_t>(app + 18));
        const auto no_jfif_inspection = codec::jpeg::inspect_jpeg_structure(no_jfif);
        codec::jpeg::validate_jpeg_common_privacy(no_jfif_inspection);
        rejects([&] { codec::jpeg::validate_jpeg_backend_technical(no_jfif_inspection,
            codec::jpeg::JpegBackendId::turbo); }, "missing JFIF technical profile");
        auto density = jpeg; density[app + 11] = std::byte{1};
        const auto density_inspection = codec::jpeg::inspect_jpeg_structure(density);
        codec::jpeg::validate_jpeg_common_privacy(density_inspection);
        require(density_inspection.jfif() && density_inspection.jfif()->density_unit == 1,
            "JFIF density inspection lost diagnostic fields.");
        rejects([&] { codec::jpeg::validate_jpeg_backend_technical(density_inspection,
            codec::jpeg::JpegBackendId::turbo); }, "nonneutral JFIF technical profile");
        codec::jpeg::validate_jpeg_backend_technical(no_jfif_inspection, codec::jpeg::JpegBackendId::jpegli);
        for (const auto backend : {codec::jpeg::JpegBackendId::mozjpeg, codec::jpeg::JpegBackendId::wic})
            rejects([&] { codec::jpeg::validate_jpeg_backend_technical(no_jfif_inspection, backend); },
                "unadmitted technical profile");
        std::string thumbnail_payload{"JFIF\0\1\1\0\0\1\0\1\1\1", 14};
        thumbnail_payload.append(3, '\0');
        const auto thumbnail = with_segment(no_jfif, 0xe0, thumbnail_payload);
        const auto thumbnail_inspection = codec::jpeg::inspect_jpeg_structure(thumbnail);
        rejects([&] { codec::jpeg::validate_jpeg_common_privacy(thumbnail_inspection); },
            "well-formed thumbnail common privacy");
        const auto arbitrary_app0 = with_segment(jpeg, 0xe0, "12345678901234");
        const auto arbitrary_inspection = codec::jpeg::inspect_jpeg_structure(arbitrary_app0);
        rejects([&] { codec::jpeg::validate_jpeg_common_privacy(arbitrary_inspection); },
            "unrecognized APP0 cannot borrow later JFIF identity");
        for (const auto marker : {0xe1U, 0xe2U, 0xfeU, 0xefU})
        {
            const auto private_bytes = with_segment(jpeg, marker, "private metadata");
            const auto inspection = codec::jpeg::inspect_jpeg_structure(private_bytes);
            rejects([&] { codec::jpeg::validate_jpeg_common_privacy(inspection); }, "common privacy gate");
        }
        const auto jfif_payload = std::string_view{
            reinterpret_cast<const char*>(jpeg.data() + app + 4), 14};
        metadata_reject(with_segment(jpeg, 0xe0, jfif_payload), "duplicate neutral JFIF");
        mutated[0] = std::byte{0};
        metadata_reject(mutated, "bad SOI");
        mutated = jpeg; mutated[app + 4] = std::byte{'X'};
        metadata_reject(mutated, "bad JFIF identifier");
        mutated = jpeg;
        mutated[app + 4 + 7] = std::byte{1}; // Physical DPI unit.
        metadata_reject(mutated, "JFIF physical density");
        mutated = jpeg; mutated[app + 4 + 12] = std::byte{1};
        metadata_reject(mutated, "JFIF thumbnail");
        mutated = jpeg; mutated[app + 2] = std::byte{0}; mutated[app + 3] = std::byte{1};
        metadata_reject(mutated, "bad marker length");
        mutated = jpeg; mutated[app + 2] = std::byte{255}; mutated[app + 3] = std::byte{255};
        metadata_reject(mutated, "truncated segment");
        const auto sof = find_marker(jpeg, 0xc0);
        mutated = jpeg; mutated[sof + 1] = std::byte{0xc2};
        const auto progressive_inspection = codec::jpeg::inspect_jpeg_structure(mutated);
        codec::jpeg::validate_jpeg_common_privacy(progressive_inspection);
        rejects([&] { codec::jpeg::validate_jpeg_backend_technical(progressive_inspection,
            codec::jpeg::JpegBackendId::turbo); }, "SOF2 diagnostic envelope technical rejection");
        metadata_reject(mutated, "progressive SOF");
        mutated = jpeg; mutated[sof + 4 + 3] = std::byte{0}; mutated[sof + 4 + 4] = std::byte{0};
        metadata_reject(mutated, "zero width");
        mutated = jpeg; mutated[sof + 4 + 3] = std::byte{255}; mutated[sof + 4 + 4] = std::byte{255};
        metadata_reject(mutated, "oversized width");
        mutated = jpeg; mutated[sof + 4 + 5] = std::byte{4};
        metadata_reject(mutated, "component count");
        mutated = jpeg; mutated[sof + 4 + 7] = std::byte{0x22};
        metadata_reject(mutated, "non-444 sampling");
        const auto dqt = find_marker(jpeg, 0xdb);
        mutated = jpeg; mutated[dqt + 5] = std::byte{0};
        metadata_reject(mutated, "zero quantizer");
        const auto dht = find_marker(jpeg, 0xc4);
        mutated = jpeg; mutated[dht + 5] = std::byte{255};
        metadata_reject(mutated, "oversubscribed Huffman table");
        mutated = jpeg; mutated.pop_back();
        metadata_reject(mutated, "missing EOI");
        mutated = jpeg; mutated.push_back(std::byte{0});
        metadata_reject(mutated, "trailing private bytes");
        rejects([&] { (void)codec::jpeg::validate_screenshot_jpeg(jpeg, rgb.width + 1, rgb.height); },
                "expected dimension mismatch");
        // All prefixes must reject, including truncation inside entropy and FF.
        for (std::size_t length = 0; length < jpeg.size(); ++length)
            rejects([&] { (void)codec::jpeg::validate_jpeg_markers(std::span{jpeg}.first(length)); },
                    "truncated prefix");
        const auto sos = find_marker(jpeg, 0xda);
        mutated = jpeg; mutated[sos + 4 + 8] = std::byte{62};
        metadata_reject(mutated, "invalid baseline scan range");
        mutated = jpeg;
        mutated.insert(mutated.end() - 2, {std::byte{255}, std::byte{0xfe},
            std::byte{0}, std::byte{3}, std::byte{'X'}});
        metadata_reject(mutated, "COM after SOS");
        const auto scan_start = sos + 2 +
            (std::to_integer<unsigned int>(jpeg[sos + 2]) << 8) +
            std::to_integer<unsigned int>(jpeg[sos + 3]);
        mutated.assign(jpeg.begin(), jpeg.begin() + static_cast<std::ptrdiff_t>(scan_start));
        mutated.insert(mutated.end(), {std::byte{0}, std::byte{255}, std::byte{0xd9}});
        // Marker parser alone cannot validate DCT entropy. Strict decode must
        // reject premature entropy completion rather than quietly fill pixels.
        (void)codec::jpeg::validate_jpeg_markers(mutated);
        rejects([&] { (void)codec::jpeg::validate_screenshot_jpeg(mutated, rgb.width, rgb.height); },
                "corrupt entropy decode");
        rejects([&] { (void)codec::jpeg::decode_srgb_jpeg_bytes(mutated); }, "direct strict decode");
        std::cout << "JPEG metadata/bounds/header/entropy negative tests: passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "JPEG validator test failed: " << error.what() << '\n';
        return 1;
    }
}
