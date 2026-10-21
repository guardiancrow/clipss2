#include "codec/jpeg/jpeg_validator.h"
#include "codec/jpeg/jpeg_codec.h"
#include "codec/jpeg/jpeg_backend_policy.h"
#include "codec/jpeg/jpeg_codec_internal.h"

#include <array>
#include <stdexcept>
#include <string>

namespace clipss2::codec::jpeg
{
namespace
{
[[noreturn]] void reject(const char* reason)
{
    throw std::runtime_error(std::string{"JPEG metadata/header validation failed: "} + reason);
}

unsigned int octet(std::span<const std::byte> data, std::size_t index)
{
    if (index >= data.size()) reject("truncated structure");
    return std::to_integer<unsigned int>(data[index]);
}

unsigned int word(std::span<const std::byte> data, std::size_t index)
{
    return (octet(data, index) << 8) | octet(data, index + 1);
}

void check_jfif(std::span<const std::byte> payload)
{
    // Exactly the neutral technical JFIF emitted by this encoder: v1.01,
    // no physical density units, 1:1 aspect ratio, no thumbnail or extra bytes.
    constexpr std::array<unsigned int, 14> expected{
        'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    if (payload.size() != expected.size()) reject("APP0 length/thumbnail");
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (octet(payload, i) != expected[i]) reject("APP0 is not neutral thumbnail-free JFIF");
}

void check_quantization(std::span<const std::byte> payload, std::array<bool, 4>& tables)
{
    if (payload.empty()) reject("empty DQT");
    std::size_t position = 0;
    while (position < payload.size())
    {
        const auto selector = octet(payload, position++);
        if (selector > 3 || tables[selector] || payload.size() - position < 64)
            reject("invalid/duplicate baseline DQT");
        for (std::size_t i = 0; i < 64; ++i)
            if (octet(payload, position + i) == 0) reject("zero quantization coefficient");
        position += 64;
        tables[selector] = true;
    }
}

void check_huffman(std::span<const std::byte> payload, std::array<bool, 8>& tables)
{
    if (payload.empty()) reject("empty DHT");
    std::size_t position = 0;
    while (position < payload.size())
    {
        const auto selector = octet(payload, position++);
        const auto kind = selector >> 4;
        const auto id = selector & 15U;
        if (kind > 1 || id > 3 || payload.size() - position < 16)
            reject("invalid DHT selector/length");
        const auto index = kind * 4 + id;
        if (tables[index]) reject("duplicate DHT");
        std::size_t symbols = 0;
        int slots = 1;
        for (std::size_t i = 0; i < 16; ++i)
        {
            const auto count = octet(payload, position + i);
            symbols += count;
            slots = slots * 2 - static_cast<int>(count);
            if (slots <= 0) reject("oversubscribed/all-ones Huffman code");
        }
        position += 16;
        if (symbols == 0 || symbols > 256 || symbols > payload.size() - position)
            reject("invalid DHT symbol count");
        std::array<bool, 256> used{};
        for (std::size_t i = 0; i < symbols; ++i)
        {
            const auto symbol = octet(payload, position + i);
            if (used[symbol]) reject("duplicate Huffman symbol");
            used[symbol] = true;
            if (kind == 0 && symbol > 11) reject("invalid baseline DC symbol");
            if (kind == 1 && ((symbol & 15U) > 10 ||
                ((symbol & 15U) == 0 && symbol != 0 && symbol != 0xf0)))
                reject("invalid baseline AC symbol");
        }
        position += symbols;
        tables[index] = true;
    }
}
}

JpegInspection inspect_jpeg_structure(std::span<const std::byte> bytes)
{
    if (bytes.size() < 4 || bytes.size() > maximum_jpeg_bytes)
        reject("encoded size outside safety limit");
    if (octet(bytes, 0) != 0xff || octet(bytes, 1) != 0xd8) reject("missing SOI");
    JpegInspection result;
    result.bytes_ = bytes;
    result.summary_ = "SOI";
    bool jfif = false;
    bool frame = false;
    std::array<bool, 4> quantization{};
    std::array<bool, 8> huffman{};
    std::array<unsigned int, 3> component_tables{};
    std::array<unsigned int, 3> component_ids{};
    std::size_t position = 2;
    while (position < bytes.size())
    {
        if (octet(bytes, position++) != 0xff) reject("expected marker prefix");
        while (position < bytes.size() && octet(bytes, position) == 0xff) ++position;
        const auto marker = octet(bytes, position++);
        if (!(marker >= 0xe0 && marker <= 0xef) && marker != 0xfe &&
            marker != 0xdb && marker != 0xc0 && marker != 0xc2 && marker != 0xc4 && marker != 0xda)
            reject("forbidden or unexpected marker (APP/COM/progressive/restart/etc.)");
        const auto length = word(bytes, position);
        if (length < 2 || length > bytes.size() - position) reject("segment bounds/length");
        const auto payload = bytes.subspan(position + 2, length - 2);
        if (result.markers_.size() >= 64) reject("too many marker segments");
        result.markers_.push_back({marker, position + 2, length - 2});
        position += length;
        switch (marker)
        {
        case 0xe0:
            if (jfif || frame) reject("duplicate or late APP0");
            if (payload.size() >= 14 && octet(payload, 0) == 'J' && octet(payload, 1) == 'F' &&
                octet(payload, 2) == 'I' && octet(payload, 3) == 'F' && octet(payload, 4) == 0)
            {
                JfifInspection info{octet(payload, 5), octet(payload, 6), octet(payload, 7),
                    word(payload, 8), word(payload, 10), octet(payload, 12), octet(payload, 13)};
                if (payload.size() != 14 + static_cast<std::size_t>(info.thumbnail_width) * info.thumbnail_height * 3)
                    reject("JFIF thumbnail/extension bounds");
                result.jfif_ = info;
                jfif = true;
                result.summary_ += " APP0(JFIF)";
            }
            else result.summary_ += " APP0";
            break;
        case 0xdb:
            check_quantization(payload, quantization);
            result.summary_ += " DQT";
            break;
        case 0xc4:
            check_huffman(payload, huffman);
            result.summary_ += " DHT";
            break;
        case 0xc0:
        case 0xc2: // Inspect its envelope only; no progressive product technical policy is admitted.
            if (frame || payload.size() != 15 || octet(payload, 0) != 8 ||
                octet(payload, 5) != 3) reject("invalid baseline SOF0/components");
            result.height_ = word(payload, 1);
            result.width_ = word(payload, 3);
            result.frame_marker_ = marker;
            (void)checked_rgb_image_bytes(result.width_, result.height_);
            for (std::size_t component = 0; component < 3; ++component)
            {
                const auto offset = 6 + component * 3;
                const auto id = octet(payload, offset);
                const auto sampling = octet(payload, offset + 1);
                if (id == 0 || (sampling >> 4) == 0 || (sampling >> 4) > 4 ||
                    (sampling & 15) == 0 || (sampling & 15) > 4 || octet(payload, offset + 2) > 3)
                    reject("invalid component/sampling/table structure");
                for (std::size_t prior = 0; prior < component; ++prior)
                    if (component_ids[prior] == id) reject("duplicate component id");
                component_ids[component] = id;
                if (id != component + 1 || sampling != 0x11) result.turbo_components_ = false;
                component_tables[component] = octet(payload, offset + 2);
            }
            frame = true;
            result.summary_ += marker == 0xc0 ? " SOF0" : " SOF2";
            break;
        case 0xda:
            if (!frame || payload.size() != 10 || octet(payload, 0) != 3 ||
                octet(payload, 7) != 0 || octet(payload, 8) != 63 || octet(payload, 9) != 0)
                reject("invalid baseline single SOS");
            for (std::size_t component = 0; component < 3; ++component)
            {
                const auto selector = octet(payload, 2 + component * 2);
                const auto dc = selector >> 4;
                const auto ac = selector & 15U;
                if (octet(payload, 1 + component * 2) != component_ids[component] ||
                    dc > 3 || ac > 3 || !huffman[dc] || !huffman[4 + ac] ||
                    !quantization[component_tables[component]]) reject("undefined scan tables/components");
            }
            result.summary_ += " SOS entropy";
            {
                const auto start = position;
                while (position < bytes.size())
                {
                    if (octet(bytes, position++) != 0xff) continue;
                    auto next = octet(bytes, position++);
                    if (next == 0) continue; // Stuffed entropy byte, not metadata.
                    while (next == 0xff) next = octet(bytes, position++);
                    if (next != 0xd9 || position != bytes.size() || position - start <= 2)
                        reject("unexpected entropy marker, trailing bytes or empty scan");
                    result.summary_ += " EOI";
                    return result;
                }
                reject("missing EOI");
            }
        default: // Bounded APP/COM envelopes; common privacy always rejects these.
            result.summary_ += " metadata";
            break;
        }
    }
    reject("missing SOS/EOI");
}

void validate_jpeg_common_privacy(const JpegInspection& inspection)
{
    for (const auto& marker : inspection.markers())
    {
        if (marker.code == 0xe0)
        {
            const auto payload = inspection.bytes().subspan(marker.payload_offset, marker.payload_size);
            if (!inspection.jfif() || marker.payload_size != 14 || inspection.jfif()->thumbnail_width ||
                inspection.jfif()->thumbnail_height || octet(payload, 0) != 'J' || octet(payload, 1) != 'F' ||
                octet(payload, 2) != 'I' || octet(payload, 3) != 'F' || octet(payload, 4) != 0)
                throw std::runtime_error("JPEG common privacy validation failed: unapproved APP0/thumbnail/private payload.");
        }
        else if ((marker.code >= 0xe1 && marker.code <= 0xef) || marker.code == 0xfe)
            throw std::runtime_error("JPEG common privacy validation failed: forbidden APP/COM (Exif/XMP/ICC/private metadata).");
    }
    // JFIF absence and density/version fields are not privacy failures.
}
void validate_jpeg_backend_technical(const JpegInspection& inspection, JpegBackendId backend)
{
    if (backend != JpegBackendId::turbo && backend != JpegBackendId::jpegli)
        throw std::runtime_error("JPEG technical validation failed: backend profile is not product-admitted.");
    if (backend == JpegBackendId::turbo && !inspection.jfif()) reject("Turbo technical profile requires neutral JFIF");
    for (const auto& marker : inspection.markers())
    {
        if (marker.code == 0xe0) check_jfif(inspection.bytes().subspan(marker.payload_offset, marker.payload_size));
        else if (marker.code != 0xdb && marker.code != 0xc0 && marker.code != 0xc4 && marker.code != 0xda)
            reject("selected technical profile: unexpected marker family");
    }
    if (inspection.frame_marker() != 0xc0 || !inspection.turbo_components())
        reject("selected technical profile requires baseline 3-component 4:4:4 YCbCr");
}
namespace
{
JpegValidationResult validation_result(const JpegInspection& inspection)
{ return {inspection.width(), inspection.height(), inspection.bytes().size(), inspection.summary()}; }
}
JpegValidationResult validate_jpeg_markers(std::span<const std::byte> bytes)
{
    const auto inspection = inspect_jpeg_structure(bytes);
    validate_jpeg_common_privacy(inspection);
    validate_jpeg_backend_technical(inspection, JpegBackendId::turbo);
    return validation_result(inspection);
}

JpegValidationResult validate_screenshot_jpeg(
    std::span<const std::byte> bytes,
    std::uint32_t expected_width, std::uint32_t expected_height)
{
    return validate_managed_screenshot_jpeg(bytes, JpegBackendId::turbo, expected_width, expected_height);
}
JpegValidationResult validate_managed_screenshot_jpeg(std::span<const std::byte> bytes,
    JpegBackendId backend, std::uint32_t expected_width, std::uint32_t expected_height)
{
    (void)checked_rgb_image_bytes(expected_width, expected_height);
    const auto inspection = inspect_jpeg_structure(bytes);
    validate_jpeg_common_privacy(inspection);
    validate_jpeg_backend_technical(inspection, backend);
    auto result = validation_result(inspection);
    if (result.width != expected_width || result.height != expected_height)
        reject("dimensions differ from expected image");
    const auto decoded = detail::decode_validated_jpeg_bytes(inspection);
    if (decoded.width != expected_width || decoded.height != expected_height ||
        decoded.pixels.size() != checked_rgb_image_bytes(expected_width, expected_height))
        throw std::runtime_error("JPEG decode validation failed: RGB structure/dimensions");
    return result;
}
}
