#include "platform/windows/capture_worker_protocol.h"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace clipss2::platform::windows;

namespace
{
template<class Exception, std::size_t Size>
bool rejects(const std::array<std::string_view, Size>& arguments)
{
    try
    {
        (void)parse_capture_worker_request(arguments);
    }
    catch (const Exception&)
    {
        return true;
    }
    return false;
}
}

int main()
{
    const CaptureWorkerRequest original{
        std::numeric_limits<std::uintptr_t>::max(), 4294967295U,
        L"clipss2.\u30c6\u30b9\u30c8", true, CaptureBorderPolicy::system_required,
        clipss2::capture::CaptureDestination::both, clipss2::image::ImageFileFormat::png, false};
    const auto serialized = serialize_capture_worker_request(original);
    for (const auto format : {clipss2::image::ImageFileFormat::avif, clipss2::image::ImageFileFormat::jxl})
    for (const auto profile : {clipss2::codec::LossyProfile::high, clipss2::codec::LossyProfile::balanced, clipss2::codec::LossyProfile::small_file})
    {
        auto audit = original;
        audit.file_format = format;
        audit.file_output = FileOutputOptions{std::filesystem::path{L"D:\\audit"}, FilenamePolicy::timestamp, false};
        audit.file_compression = {clipss2::capture::FileCompressionMode::lossy, profile};
        const auto args = serialize_capture_worker_request(audit);
        std::vector<std::string_view> request_views(args.begin(), args.end());
        const auto decoded = parse_capture_worker_request(request_views);
        if (decoded.file_format != format || decoded.file_compression != audit.file_compression ||
            decoded.file_output != audit.file_output || args.size() != 15) return 1;
        if (args.back() != std::string{"compression="} + clipss2::capture::file_compression_token(audit.file_compression)) return 1;
        const auto legacy_suffix = std::string{"audit-lossy="} + clipss2::codec::lossy_profile_token(profile);
        request_views.back() = legacy_suffix;
        if (parse_capture_worker_request(request_views).file_compression != audit.file_compression) return 1;
        request_views.back() = "compression=lossy-bogus";
        bool rejected{}; try { (void)parse_capture_worker_request(request_views); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) return 1;
        audit.file_compression = {};
        const auto normal = serialize_capture_worker_request(audit);
        if (normal.size() != 14 || std::any_of(normal.begin(), normal.end(), [](const auto& field) { return field.starts_with("compression="); })) return 1;
        audit.file_compression = {clipss2::capture::FileCompressionMode::lossy, profile};
        audit.file_format = clipss2::image::ImageFileFormat::png;
        rejected = false; try { (void)serialize_capture_worker_request(audit); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) return 1;
    }
    std::array<std::string_view, 8> views{};
    for (std::size_t index = 0; index < views.size(); ++index)
        views[index] = serialized[index];
    const auto parsed = parse_capture_worker_request(views);
    if (parsed.hwnd_value != original.hwnd_value ||
        parsed.expected_process_id != original.expected_process_id ||
        parsed.expected_class != original.expected_class ||
        parsed.include_cursor != original.include_cursor ||
        parsed.border_policy != original.border_policy ||
        parsed.destination != original.destination || parsed.file_format != original.file_format || parsed.quiet)
        return 1;

    const std::array<std::string_view, 8> bad_integer{
        "not-hex", "1", "0041", "off", "system-required", "file", "png", "quiet"};
    const std::array<std::string_view, 8> overflow{
        "ffffffffffffffffffffffffffffffff", "1", "0041", "off",
        "system-required", "file", "png", "quiet"};
    const std::array<std::string_view, 8> bad_destination{
        "1", "1", "0041", "off", "system-required", "network", "png", "quiet"};
    const std::array<std::string_view, 8> bad_cursor{
        "1", "1", "0041", "maybe", "system-required", "file", "png", "quiet"};
    const std::array<std::string_view, 8> bad_border{
        "1", "1", "0041", "off", "borderless", "file", "png", "quiet"};
    const std::array<std::string_view, 8> bad_format{
        "1", "1", "0041", "off", "system-required", "both", "foo", "quiet"};
    const std::array<std::string_view, 7> missing_format{
        "1", "1", "0041", "off", "system-required", "file", "quiet"};
    if (!rejects<std::invalid_argument>(bad_format) ||
        !rejects<std::invalid_argument>(missing_format)) return 1;
    auto jpeg = original;
    jpeg.file_format = clipss2::image::ImageFileFormat::jpeg;
    const auto jpeg_args = serialize_capture_worker_request(jpeg);
    for (std::size_t i = 0; i < views.size(); ++i) views[i] = jpeg_args[i];
    if (parse_capture_worker_request(views).file_format != jpeg.file_format) return 1;
    jpeg.file_format = clipss2::image::ImageFileFormat::webp;
    const auto webp_args = serialize_capture_worker_request(jpeg);
    for (std::size_t i = 0; i < views.size(); ++i) views[i] = webp_args[i];
    if (parse_capture_worker_request(views).file_format != jpeg.file_format) return 1;
    jpeg.file_format = clipss2::image::ImageFileFormat::avif;
    const auto avif_args = serialize_capture_worker_request(jpeg);
    for (std::size_t i = 0; i < views.size(); ++i) views[i] = avif_args[i];
    if (parse_capture_worker_request(views).file_format != jpeg.file_format) return 1;
    jpeg.file_format = clipss2::image::ImageFileFormat::unknown;
    auto jxl_request = original;
    jxl_request.file_format = clipss2::image::ImageFileFormat::jxl;
    const auto jxl_args = serialize_capture_worker_request(jxl_request);
    for (std::size_t i = 0; i < views.size(); ++i) views[i] = jxl_args[i];
    if (parse_capture_worker_request(views).file_format != jxl_request.file_format) return 1;
    auto audit_request = original;
    audit_request.file_format = clipss2::image::ImageFileFormat::jpeg;
    audit_request.jpeg_selection = clipss2::codec::jpeg::JpegProfileSelection{
        clipss2::codec::jpeg::JpegProfile::balanced, clipss2::codec::jpeg::JpegBackendId::jpegli};
    const auto audit_args = serialize_capture_worker_request(audit_request);
    std::array<std::string_view, 11> audit_views{};
    for (std::size_t i = 0; i < audit_views.size(); ++i) audit_views[i] = audit_args[i];
    if (parse_capture_worker_request(audit_views).jpeg_selection != audit_request.jpeg_selection) return 1;
    for (const auto profile : {clipss2::codec::jpeg::JpegProfile::fast, clipss2::codec::jpeg::JpegProfile::balanced,
                              clipss2::codec::jpeg::JpegProfile::smallest})
    for (const auto backend : {clipss2::codec::jpeg::JpegBackendId::automatic, clipss2::codec::jpeg::JpegBackendId::turbo,
                              clipss2::codec::jpeg::JpegBackendId::jpegli})
    {
        audit_request.jpeg_selection = clipss2::codec::jpeg::JpegProfileSelection{profile, backend};
        const auto args = serialize_capture_worker_request(audit_request);
        for (std::size_t i = 0; i < audit_views.size(); ++i) audit_views[i] = args[i];
        if (parse_capture_worker_request(audit_views).jpeg_selection != audit_request.jpeg_selection) return 1;
    }
    for (std::size_t i = 0; i < audit_views.size(); ++i) audit_views[i] = audit_args[i];
    audit_views[10] = "backend=unknown";
    if (!rejects<std::invalid_argument>(audit_views)) return 1;
    audit_views[10] = "backend=";
    if (!rejects<std::invalid_argument>(audit_views)) return 1;
    audit_views[10] = "backend=jpegli"; audit_views[9] = "profile=unknown";
    if (!rejects<std::invalid_argument>(audit_views)) return 1;
    audit_views[9] = "profile=Balanced";
    if (!rejects<std::invalid_argument>(audit_views)) return 1;
    audit_views[8] = "selection=current-default"; audit_views[9] = "profile=none";
    if (!rejects<std::invalid_argument>(audit_views)) return 1;
    audit_views[10] = "backend=automatic";
    if (!std::holds_alternative<clipss2::codec::jpeg::JpegCurrentDefault>(parse_capture_worker_request(audit_views).jpeg_selection)) return 1;
    std::array<std::string_view, 10> missing_selection{};
    for (std::size_t i = 0; i < missing_selection.size(); ++i) missing_selection[i] = audit_views[i];
    if (!rejects<std::invalid_argument>(missing_selection)) return 1;
    auto product_request = original;
    product_request.file_output = FileOutputOptions{std::filesystem::path{L"D:\\folder"}, FilenamePolicy::sequence, false};
    // Use a real UTF-16 Unicode path; transport remains ASCII hex, not ACP.
    product_request.file_output->directory = std::filesystem::path{L"D:\\space = \\folder"} / L"\u753b\u50cf";
    auto product_args = serialize_capture_worker_request(product_request);
    std::array<std::string_view, 14> product_views{};
    std::copy(product_args.begin(), product_args.end(), product_views.begin());
    if (product_views.size() != 14 || parse_capture_worker_request(product_views).file_output != product_request.file_output) return 1;
    for (const auto policy : {FilenamePolicy::timestamp, FilenamePolicy::sequence, FilenamePolicy::guid})
    {
        product_request.file_output->filename_policy = policy;
        product_args = serialize_capture_worker_request(product_request);
        std::copy(product_args.begin(), product_args.end(), product_views.begin());
        if (parse_capture_worker_request(product_views).file_output != product_request.file_output) return 1;
    }
    product_views[12] = "filename=unknown";
    if (!rejects<std::invalid_argument>(product_views)) return 1;
    product_views[12] = "filename=timestamp"; product_views[11] = "output=0066";
    if (!rejects<std::invalid_argument>(product_views)) return 1;
    std::copy(product_args.begin(), product_args.end(), product_views.begin()); product_views[5] = "clipboard";
    if (!rejects<std::invalid_argument>(product_views)) return 1;
    std::array<std::string_view, 13> partial_output{};
    std::copy_n(product_args.begin(), partial_output.size(), partial_output.begin());
    if (!rejects<std::invalid_argument>(partial_output)) return 1;
    bool invalid_format_rejected = false;
    CaptureWorkerRequest region;
    region.file_format = clipss2::image::ImageFileFormat::png;
    region.monitor_source = MonitorSource{L"\\\\.\\DISPLAY2",{-2560,-1440,0,0},PixelRectangle{160,100,1160,800}};
    region.file_output = product_request.file_output;
    const auto region_args = serialize_capture_worker_request(region);
    std::array<std::string_view,15> region_views{};
    std::copy(region_args.begin(),region_args.end(),region_views.begin());
    if (region_args.size()!=15 || parse_capture_worker_request(region_views).monitor_source!=region.monitor_source ||
        !std::holds_alternative<MonitorSource>(parse_capture_worker_request(region_views).capture_source())) return 1;
    for (const auto bad : {"crop=0,0,0,1","crop=-1,0,2,2","crop=0,0,2561,1","crop=0,0,1,1441", "crop=0,0,2147483648,1","crop=0,0,1", "crop=0,0,1,1,2"})
    { region_views.back()=bad; if (!rejects<std::exception>(region_views)) return 1; }
    region_views.back()=region_args.back(); region_views[1]="0041";
    if (!rejects<std::exception>(region_views)) return 1;
    region.file_output.reset(); region.destination=clipss2::capture::CaptureDestination::clipboard;
    auto clipboard_region_args=serialize_capture_worker_request(region);
    std::vector<std::string_view> clipboard_region_views(clipboard_region_args.begin(),clipboard_region_args.end());
    if (clipboard_region_args.size()!=12 || parse_capture_worker_request(clipboard_region_views).monitor_source!=region.monitor_source) return 1;
    region.monitor_source->crop.reset();
    clipboard_region_args=serialize_capture_worker_request(region);
    clipboard_region_views.assign(clipboard_region_args.begin(),clipboard_region_args.end());
    if (parse_capture_worker_request(clipboard_region_views).monitor_source->crop) return 1;
    region.destination=clipss2::capture::CaptureDestination::both;
    region.file_output=product_request.file_output;
    const auto full_args=serialize_capture_worker_request(region);
    std::vector<std::string_view> full_views(full_args.begin(),full_args.end());
    if(full_args.size()!=15 || full_args.back()!="crop=none" ||
       parse_capture_worker_request(full_views).monitor_source!=region.monitor_source ||
       parse_capture_worker_request(full_views).file_output!=region.file_output) return 1;
    region.destination=clipss2::capture::CaptureDestination::clipboard; region.file_output.reset();
    region.monitor_source->expected_topology="DISPLAY1:0,0,2560,1600:0,2560,1600,60";
    auto topology_args=serialize_capture_worker_request(region);
    std::vector<std::string_view> topology_views(topology_args.begin(),topology_args.end());
    if(topology_args.size()!=13 || parse_capture_worker_request(topology_views).monitor_source!=region.monitor_source) return 1;
    bool topology_rejected=false;
    topology_views.back()="topology=";
    try{(void)parse_capture_worker_request(topology_views);}catch(const std::invalid_argument&){topology_rejected=true;}
    if(!topology_rejected)return 1;
    try { (void)serialize_capture_worker_request(jpeg); }
    catch (const std::invalid_argument&) { invalid_format_rejected = true; }
    if (!invalid_format_rejected) return 1;
    auto invalid_destination_request = original;
    invalid_destination_request.destination =
        static_cast<clipss2::capture::CaptureDestination>(999);
    bool invalid_destination_serialization_rejected{};
    try
    {
        (void)serialize_capture_worker_request(invalid_destination_request);
    }
    catch (const std::invalid_argument&)
    {
        invalid_destination_serialization_rejected = true;
    }
    if (!rejects<std::invalid_argument>(bad_integer) ||
        !rejects<std::out_of_range>(overflow) ||
        !rejects<std::invalid_argument>(bad_destination) ||
        !rejects<std::invalid_argument>(bad_cursor) ||
        !rejects<std::invalid_argument>(bad_border) ||
        !invalid_destination_serialization_rejected)
        return 1;
}
