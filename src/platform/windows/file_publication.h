#pragma once
#include "platform/windows/storage_preferences.h"
#include <span>
#include <functional>
#include <cstddef>

namespace clipss2::platform::windows
{
using PayloadValidator = std::function<void(std::span<const std::byte>)>;
// Internal deterministic seams; production uses native time/GUID and no hooks.
enum class PublicationFault { none, write, flush, move };
struct PublicationHooks
{
    std::function<std::wstring()> timestamp;
    std::function<std::wstring()> guid;
    std::function<void(const std::filesystem::path&)> after_write;
    std::function<void(const std::filesystem::path&)> before_move;
    PublicationFault fault{PublicationFault::none};
};
[[nodiscard]] std::filesystem::path publish_file_payload(const FileOutputOptions& options,
    std::string_view extension, std::span<const std::byte> bytes,
    const PayloadValidator& validate, const PublicationHooks& hooks = {});
}
