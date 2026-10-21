// Copyright (c) 2026 guardiancrow. MIT.
#include "lib/jpegli/encode.h"
#include "lib/jpegli/error.h"
#include "hwy/base.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct CallbackError {};
int cleanup = 0, warnings = 0, expected_line = 0, expected_jpeg_line = 0;
int assertion = 0; int observed_jpeg_line = 0;
void require(bool ok) { ++assertion; if (!ok) throw std::runtime_error("Privacy assertion " + std::to_string(assertion) + " cleanup=" + std::to_string(cleanup) + " warnings=" + std::to_string(warnings) + " expected-jpeg-line=" + std::to_string(expected_jpeg_line) + " observed-jpeg-line=" + std::to_string(observed_jpeg_line)); }
void verify(j_common_ptr info) {
    const std::string message = info->err->msg_parm.s;
    require(message.starts_with("jpegli:"));
    require(message.find('\\') == std::string::npos);
    const int line = std::atoi(message.c_str() + 7);
    observed_jpeg_line = line;
    require(line > 0 && (expected_jpeg_line == 0 || line == expected_jpeg_line));
}
struct Compressor {
    jpeg_compress_struct info{};
    jpeg_error_mgr error{};
    Compressor() {
        info.err = jpegli_std_error(&error);
        error.error_exit = [](j_common_ptr info) { verify(info); throw CallbackError{}; };
        error.emit_message = [](j_common_ptr info, int level) {
            verify(info); require(level == -1 || level == 2); ++warnings;
            const std::string message = info->err->msg_parm.s;
            require(message.find(level < 0 ? "synthetic warning 17" : "synthetic trace 23") != std::string::npos);
            if (level < 0) throw CallbackError{};
        };
        jpegli_create_compress(&info);
    }
    ~Compressor() { jpegli_destroy_compress(&info); ++cleanup; }
};
void check_jpegli(int mode) {
    Compressor owner;
    auto* cinfo = &owner.info;
    expected_jpeg_line = 0;
    if (mode == 0) { jpegli_write_scanlines(cinfo, nullptr, 0); }
    if (mode == 1) { expected_jpeg_line = __LINE__ + 1;
        JPEGLI_WARN("synthetic warning %d", 17);
    }
    if (mode == 2) { cinfo->err->trace_level = 2; expected_jpeg_line = __LINE__ + 1;
        JPEGLI_TRACE(2, "synthetic trace %d", 23);
    }
    if (mode == 3) { expected_jpeg_line = __LINE__ + 1;
        JPEGLI_ERROR("synthetic error %d", 31);
    }
}
void warn(const char* file, int line, const char* message) {
    require(std::string(file) == "highway" && line == expected_line);
    require(std::string(message) == "synthetic warning 41"); ++warnings;
}
void abort_callback(const char* file, int line, const char* message) {
    require(std::string(file) == "highway" && line == expected_line);
    require(std::string(message) == "synthetic abort 43"); throw CallbackError{};
}
struct Guard { ~Guard() { ++cleanup; } };
void check_abort() { Guard g; expected_line = __LINE__ + 1;
    HWY_ABORT("synthetic abort %d", 43);
}
}
int main() {
    try {
        for (int mode : {0, 1, 3}) {
            bool rejected = false;
            try { check_jpegli(mode); } catch (const CallbackError&) { rejected = true; }
            require(rejected);
        }
        check_jpegli(2); require(cleanup == 4 && warnings == 2);
        const auto prior_warn = hwy::SetWarnFunc(warn);
        expected_line = __LINE__ + 1;
        HWY_WARN("synthetic warning %d", 41);
        hwy::SetWarnFunc(prior_warn);
        const auto prior_abort = hwy::SetAbortFunc(abort_callback);
        bool aborted = false;
        try { check_abort(); } catch (const CallbackError&) { aborted = true; }
        hwy::SetAbortFunc(prior_abort);
        require(aborted && cleanup == 5 && warnings == 3);
        std::cout << "Privacy identifiers / format / line / callback / exception cleanup PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
