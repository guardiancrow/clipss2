#pragma once
#include <filesystem>
namespace clipss2::platform::windows
{
// Owned fixture -> real parent/worker/capture/publication. No selector or synthetic input.
int run_lossy_worker_audit(const std::filesystem::path& new_output_directory);
}
