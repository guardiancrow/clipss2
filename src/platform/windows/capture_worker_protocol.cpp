#include "platform/windows/capture_worker_protocol.h"
#include "audit/audit_policy.h"

#include <charconv>
#include <limits>
#include <stdexcept>

namespace clipss2::platform::windows
{
namespace
{
template<class Integer>
Integer parse_integer(std::string_view text, const int base, const char* label)
{
    if (text.empty()) throw std::invalid_argument(std::string{label} + " is empty.");
    Integer value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, base);
    if (error == std::errc::result_out_of_range)
        throw std::out_of_range(std::string{label} + " overflows its integer type.");
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument(std::string{label} + " is not a valid integer.");
    return value;
}

unsigned int hex_digit(const char value)
{
    if (value >= '0' && value <= '9') return static_cast<unsigned int>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned int>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned int>(value - 'A' + 10);
    throw std::invalid_argument("Worker class contains a non-hex character.");
}

PixelRectangle parse_rectangle(std::string_view value)
{
    std::int32_t coordinates[4]{};
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto comma = value.find(',');
        if ((i < 3 && comma == value.npos) || (i == 3 && comma != value.npos))
            throw std::invalid_argument("Malformed worker rectangle.");
        coordinates[i] = parse_integer<std::int32_t>(value.substr(0,comma),10,"coordinate");
        if (i < 3) value.remove_prefix(comma + 1);
    }
    return {coordinates[0],coordinates[1],coordinates[2],coordinates[3]};
}
std::string rectangle_token(const PixelRectangle& r)
{ return std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom); }

std::wstring decode_worker_class(const std::string_view value)
{
    if (value.empty() || value.size() % 4 != 0)
        throw std::invalid_argument("Worker class encoding has an invalid length.");
    std::wstring result;
    result.reserve(value.size() / 4);
    for (std::size_t offset = 0; offset < value.size(); offset += 4)
    {
        unsigned int unit{};
        for (std::size_t index = 0; index < 4; ++index)
            unit = unit * 16 + hex_digit(value[offset + index]);
        if (unit == 0)
            throw std::invalid_argument("Worker class encoding contains NUL.");
        result.push_back(static_cast<wchar_t>(unit));
    }
    return result;
}

capture::CaptureDestination parse_destination(const std::string_view value)
{
    if (value == "file") return capture::CaptureDestination::file;
    if (value == "clipboard") return capture::CaptureDestination::clipboard;
    if (value == "both") return capture::CaptureDestination::both;
    throw std::invalid_argument("Worker destination must be file, clipboard, or both.");
}

const char* serialize_destination(const capture::CaptureDestination value)
{
    switch (value)
    {
    case capture::CaptureDestination::file: return "file";
    case capture::CaptureDestination::clipboard: return "clipboard";
    case capture::CaptureDestination::both: return "both";
    }
    throw std::invalid_argument("Worker destination enum is invalid.");
}
}

std::string encode_worker_class(const std::wstring_view value)
{
    if (value.empty()) throw std::invalid_argument("Worker class must not be empty.");
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 4);
    for (const wchar_t character : value)
    {
        const auto unit = static_cast<unsigned int>(character);
        if (unit == 0 || unit > std::numeric_limits<std::uint16_t>::max())
            throw std::invalid_argument("Worker class is not valid UTF-16.");
        result.push_back(digits[(unit >> 12) & 0xf]);
        result.push_back(digits[(unit >> 8) & 0xf]);
        result.push_back(digits[(unit >> 4) & 0xf]);
        result.push_back(digits[unit & 0xf]);
    }
    return result;
}

CaptureWorkerRequest parse_capture_worker_request(
    std::span<const std::string_view> arguments)
{
    capture::FileCompression compression;
    if (!arguments.empty() && arguments.back().starts_with("compression="))
    {
        compression = capture::parse_file_compression(arguments.back().substr(12));
        arguments = arguments.first(arguments.size() - 1);
    }
    else if (!arguments.empty() && arguments.back().starts_with("audit-lossy="))
    {
        compression = {capture::FileCompressionMode::lossy, codec::parse_lossy_profile(arguments.back().substr(12))};
        arguments = arguments.first(arguments.size() - 1);
    }
    std::optional<MonitorSource> monitor;
    if (!arguments.empty() && arguments[0] == "monitor")
    {
        std::string topology;
        if (arguments.back().starts_with("topology="))
        {
            topology=arguments.back().substr(9);
            if(topology.empty())throw std::invalid_argument("Empty display topology snapshot.");
            arguments=arguments.first(arguments.size()-1);
        }
        if (arguments.size() != 12 && arguments.size() != 15)
            throw std::invalid_argument("Incomplete private monitor request.");
        if (!arguments.back().starts_with("crop=")) throw std::invalid_argument("Missing monitor crop field.");
        MonitorSource source{decode_worker_class(arguments[1]),parse_rectangle(arguments[2]),{}};
        source.expected_topology=std::move(topology);
        if (arguments.back() != "crop=none") source.crop = parse_rectangle(arguments.back().substr(5));
        validate_monitor_source(source);
        monitor=std::move(source);
        arguments=arguments.first(arguments.size()-1);
    }
    if (arguments.size() != 8 && arguments.size() != 11 && arguments.size() != 14)
        throw std::invalid_argument("Capture worker requires 8, 11 or 14 complete private arguments.");
    CaptureWorkerRequest result;
    result.file_compression = compression;
    result.monitor_source=std::move(monitor);
    if (!result.monitor_source)
    {
    result.hwnd_value = parse_integer<std::uintptr_t>(arguments[0], 16, "HWND");
    if (result.hwnd_value == 0) throw std::invalid_argument("HWND must not be zero.");
    result.expected_process_id =
        parse_integer<std::uint32_t>(arguments[1], 10, "PID");
    if (result.expected_process_id == 0) throw std::invalid_argument("PID must not be zero.");
    result.expected_class = decode_worker_class(arguments[2]);
    }
    if (arguments[3] == "on") result.include_cursor = true;
    else if (arguments[3] == "off") result.include_cursor = false;
    else throw std::invalid_argument("Worker cursor option must be on or off.");
    if (arguments[4] != "system-required")
        throw std::invalid_argument("Worker border option must be system-required.");
    result.destination = parse_destination(arguments[5]);
    result.file_format = image::parse_image_file_format(arguments[6]);
    if (arguments[7] == "quiet") result.quiet = true;
    else if (arguments[7] == "verbose") result.quiet = false;
    else throw std::invalid_argument("Worker verbosity must be quiet or verbose.");
    if (arguments.size() >= 11)
    {
        using namespace codec::jpeg;
        JpegBackendId backend;
        if (arguments[10] == "backend=automatic") backend = JpegBackendId::automatic;
        else if (arguments[10] == "backend=turbo") backend = JpegBackendId::turbo;
        else if (arguments[10] == "backend=jpegli") backend = JpegBackendId::jpegli;
        else if (arguments[10] == "backend=mozjpeg") backend = JpegBackendId::mozjpeg;
        else if (arguments[10] == "backend=wic") backend = JpegBackendId::wic;
        else throw std::invalid_argument("Unknown private JPEG backend token.");
        if (arguments[8] == "selection=current-default")
        {
            if (arguments[9] != "profile=none" || backend != JpegBackendId::automatic)
                throw std::invalid_argument("CurrentDefault requires profile=none/backend=automatic.");
            result.jpeg_selection = JpegCurrentDefault{};
        }
        else if (arguments[8] == "selection=profile")
        {
            JpegProfile profile;
            if (arguments[9] == "profile=fast") profile = JpegProfile::fast;
            else if (arguments[9] == "profile=balanced") profile = JpegProfile::balanced;
            else if (arguments[9] == "profile=smallest") profile = JpegProfile::smallest;
            else throw std::invalid_argument("Unknown private JPEG profile token.");
            result.jpeg_selection = JpegProfileSelection{profile, backend};
        }
        else throw std::invalid_argument("Unknown JPEG selection mode.");
        validate_jpeg_product_selection(result.jpeg_selection);
    }
    if (arguments.size() == 14)
    {
        if (!arguments[11].starts_with("output=") || !arguments[12].starts_with("filename=") ||
            (arguments[13] != "directory=existing" && arguments[13] != "directory=create"))
            throw std::invalid_argument("Invalid private output fields.");
        FileOutputOptions output{std::filesystem::path{decode_worker_class(arguments[11].substr(7))},
            parse_filename_policy(arguments[12].substr(9)), arguments[13] == "directory=create"};
        validate_output_path(output.directory);
        if (!capture::capture_output_plan(result.destination).file)
            throw std::invalid_argument("Clipboard request must not carry file storage.");
        result.file_output = std::move(output);
    }
    if (result.monitor_source && capture::capture_output_plan(result.destination).file && !result.file_output)
        throw std::invalid_argument("Monitor File/Both request requires resolved output storage.");
    capture::validate_file_compression(result.file_format, result.file_compression);
    if (compression.mode == capture::FileCompressionMode::lossy &&
        capture::capture_output_plan(result.destination).file && !result.file_output)
        throw std::invalid_argument("Lossy File/Both requires atomic product output storage.");
#if !CLIPSS2_AUDIT_FEATURES
    if (capture::capture_output_plan(result.destination).file && !result.file_output)
        throw std::invalid_argument("File/Both request requires resolved output storage.");
#endif
    return result;
}

std::vector<std::string> serialize_capture_worker_request(
    const CaptureWorkerRequest& request)
{
    std::vector<std::string> result;
    if (request.monitor_source)
    {
        if (request.hwnd_value || request.expected_process_id || !request.expected_class.empty())
            throw std::invalid_argument("Mixed Window and Monitor identity.");
        validate_monitor_source(*request.monitor_source);
        result={"monitor",encode_worker_class(request.monitor_source->device_name),rectangle_token(request.monitor_source->expected_bounds)};
    }
    else
    {
    if (request.hwnd_value == 0 || request.expected_process_id == 0)
        throw std::invalid_argument("Worker identity contains a zero HWND or PID.");
    char hwnd[2 * sizeof(std::uintptr_t) + 1]{};
    const auto [hwnd_end, hwnd_error] = std::to_chars(
        hwnd, hwnd + sizeof(hwnd), request.hwnd_value, 16);
    if (hwnd_error != std::errc{})
        throw std::runtime_error("Could not serialize worker HWND.");
    result={
        std::string{hwnd, hwnd_end},
        std::to_string(request.expected_process_id),
        encode_worker_class(request.expected_class)};
    }
    if (request.border_policy != CaptureBorderPolicy::system_required)
        throw std::invalid_argument("Only the system-required border policy is supported.");
    result.insert(result.end(),{
        request.include_cursor ? "on" : "off",
        "system-required",
        serialize_destination(request.destination),
        image::image_file_format_token(request.file_format),
        request.quiet ? "quiet" : "verbose"});
    codec::jpeg::validate_jpeg_product_selection(request.jpeg_selection);
    if (const auto* selection = std::get_if<codec::jpeg::JpegProfileSelection>(&request.jpeg_selection))
    {
        result.emplace_back("selection=profile");
        result.emplace_back(std::string{"profile="} + codec::jpeg::jpeg_profile_token(selection->profile));
        switch (selection->backend)
        {
        case codec::jpeg::JpegBackendId::automatic: result.emplace_back("backend=automatic"); break;
        case codec::jpeg::JpegBackendId::turbo: result.emplace_back("backend=turbo"); break;
        case codec::jpeg::JpegBackendId::jpegli: result.emplace_back("backend=jpegli"); break;
        case codec::jpeg::JpegBackendId::mozjpeg: result.emplace_back("backend=mozjpeg"); break;
        case codec::jpeg::JpegBackendId::wic: result.emplace_back("backend=wic"); break;
        default: throw std::invalid_argument("Invalid private JPEG backend enum.");
        }
    }
    else
    {
        result.emplace_back("selection=current-default");
        result.emplace_back("profile=none");
        result.emplace_back("backend=automatic");
    }
    if (request.file_output)
    {
        if (!capture::capture_output_plan(request.destination).file)
            throw std::invalid_argument("Clipboard request must not carry file storage.");
        validate_output_path(request.file_output->directory);
        result.emplace_back("output=" + encode_worker_class(request.file_output->directory.native()));
        result.emplace_back(std::string{"filename="} + filename_policy_token(request.file_output->filename_policy));
        result.emplace_back(request.file_output->create_directory ? "directory=create" : "directory=existing");
    }
    if (request.monitor_source)
    {
        if (capture::capture_output_plan(request.destination).file && !request.file_output)
            throw std::invalid_argument("Monitor File/Both request requires resolved output storage.");
        result.emplace_back("crop=" + (request.monitor_source->crop ? rectangle_token(*request.monitor_source->crop) : std::string{"none"}));
        if(!request.monitor_source->expected_topology.empty())result.emplace_back("topology="+request.monitor_source->expected_topology);
    }
    capture::validate_file_compression(request.file_format, request.file_compression);
    if (request.file_compression.mode == capture::FileCompressionMode::lossy)
    {
        if (capture::capture_output_plan(request.destination).file && !request.file_output)
            throw std::invalid_argument("Lossy File/Both requires atomic product output storage.");
        result.emplace_back(std::string{"compression="} + capture::file_compression_token(request.file_compression));
    }
#if !CLIPSS2_AUDIT_FEATURES
    if (capture::capture_output_plan(request.destination).file && !request.file_output)
        throw std::invalid_argument("File/Both request requires resolved output storage.");
#endif
    return result;
}

const char* capture_worker_exit_name(const CaptureWorkerExit value) noexcept
{
    switch (value)
    {
    case CaptureWorkerExit::success: return "success";
    case CaptureWorkerExit::stale_target: return "stale target";
    case CaptureWorkerExit::capture_failure: return "capture failure";
    case CaptureWorkerExit::file_failure: return "file failure";
    case CaptureWorkerExit::clipboard_failure: return "clipboard failure";
    case CaptureWorkerExit::partial_success: return "partial success";
    case CaptureWorkerExit::timeout: return "timeout";
    case CaptureWorkerExit::invalid_argument: return "invalid argument";
    }
    return "unknown";
}
}
