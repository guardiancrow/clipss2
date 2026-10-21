#pragma once

#include <exception>
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <utility>

#include <windows.h>

namespace clipss2::platform::windows
{
// Text owned by this diagnostic layer is UTF-8. Arbitrary what() is not.
class DiagnosticError : public std::runtime_error
{
public:
    explicit DiagnosticError(std::string text, std::optional<HRESULT> result = {})
        : std::runtime_error(std::move(text)), result_(result) {}
    [[nodiscard]] std::optional<HRESULT> result() const noexcept { return result_; }
private:
    std::optional<HRESULT> result_;
};

struct CaptureStage
{
    std::string_view name;
    std::string_view operation;
    std::string_view requested_interface{}; // Static names/IIDs only, never target data.
};

class CaptureStageError final : public DiagnosticError
{
public:
    CaptureStageError(std::string text, std::optional<HRESULT> result)
        : DiagnosticError(std::move(text), result) {}
};

[[noreturn]] void throw_capture_stage_failure(CaptureStage stage, std::exception_ptr error);
// Shipping is a no-op; injection is compiled only into owned test fixtures.
void inject_capture_stage_failure_for_audit(std::string_view stage);

template<class Function>
decltype(auto) capture_stage(CaptureStage stage, Function&& action)
{
    try
    {
        inject_capture_stage_failure_for_audit(stage.name);
        return std::forward<Function>(action)();
    }
    catch (const CaptureStageError&) { throw; } // Keep the innermost operation/HRESULT.
    catch (...) { throw_capture_stage_failure(stage, std::current_exception()); }
}

[[nodiscard]] const char* known_hresult_name(HRESULT result) noexcept;
[[nodiscard]] std::string format_hresult(
    std::string_view operation, HRESULT result);
[[nodiscard]] std::string format_win32_error(
    std::string_view operation, DWORD error);
[[nodiscard]] std::string describe_exception(std::exception_ptr error);
void require_win32_success(
    bool succeeded, DWORD error, std::string_view operation);
[[noreturn]] void throw_hresult_failure(
    std::string_view operation, HRESULT result);
}
