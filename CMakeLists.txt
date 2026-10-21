cmake_minimum_required(VERSION 3.28)

project(
    clipss2
    VERSION 0.1.0
    LANGUAGES CXX
)

if(MSVC)
    # CMake's Japanese /showIncludes detection can decode UTF-8 as CP932.
    # Normalize only the actual Japanese locale or its known mojibake;
    # otherwise Ninja silently misses header dependencies and mixes ABIs.
    if(CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^(メモ|繝｡繝｢):")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "メモ: インクルード ファイル:  ")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
    endif()
    # The private jpegli C API uses throwing error callbacks. /EHsc assumes
    # extern-C calls cannot throw; /EHs is required for correct Release cleanup.
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

include(cmake/ShippingPrivacy.cmake)
find_package(PNG REQUIRED)
find_package(libjpeg-turbo 3 CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(libavif CONFIG REQUIRED)
include(cmake/ManagedJpegli.cmake)
include(cmake/LibjxlPackage.cmake)

function(clipss2_configure_cpp_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_23)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /utf-8
            /permissive-
            /Zc:__cplusplus
        )
        if(target_name STREQUAL "clipss2_jpeg_codec")
            target_compile_options(${target_name} PRIVATE /EHs)
        else()
            target_compile_options(${target_name} PRIVATE /EHsc)
        endif()
    endif()
endfunction()

add_library(clipss2_capture_model STATIC
    src/capture/capture_destination.cpp
    src/capture/capture_destination.h
)
target_include_directories(clipss2_capture_model PUBLIC src)
clipss2_configure_cpp_target(clipss2_capture_model)

add_library(clipss2_ui_windows STATIC
    src/platform/windows/ui_localization.cpp
    src/platform/windows/ui_localization.h
)
target_include_directories(clipss2_ui_windows PUBLIC src)
target_compile_definitions(clipss2_ui_windows PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(clipss2_ui_windows PRIVATE CLIPSS2_AUDIT_FEATURES=0)
target_link_libraries(clipss2_ui_windows PUBLIC user32 gdi32)
clipss2_configure_cpp_target(clipss2_ui_windows)

add_library(clipss2_unicode_diagnostics STATIC
    src/platform/windows/unicode_diagnostics.cpp
    src/platform/windows/unicode_diagnostics.h
)
target_include_directories(clipss2_unicode_diagnostics PUBLIC src)
target_compile_definitions(clipss2_unicode_diagnostics PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
clipss2_configure_cpp_target(clipss2_unicode_diagnostics)

add_library(clipss2_image STATIC
    src/image/alpha_diagnostics.cpp
    src/image/alpha_diagnostics.h
    src/image/srgb_transfer.cpp
    src/image/cpu_image.cpp
    src/image/cpu_image.h
    src/image/fp16_diagnostics.cpp
    src/image/fp16_diagnostics.h
    src/image/row_copy.cpp
    src/image/row_copy.h
)

target_include_directories(clipss2_image PUBLIC src)
clipss2_configure_cpp_target(clipss2_image)

add_library(clipss2_png_codec STATIC
    src/codec/png/png_codec.cpp
    src/codec/png/png_codec.h
    src/codec/png/png_validator.cpp
    src/codec/png/png_validator.h
)

target_include_directories(clipss2_png_codec PUBLIC src)
target_link_libraries(clipss2_png_codec PUBLIC clipss2_image PNG::PNG)
clipss2_configure_cpp_target(clipss2_png_codec)

add_library(clipss2_webp_codec STATIC
    src/codec/webp/webp_codec.cpp
    src/codec/webp/webp_codec.h
    src/codec/webp/webp_validator.cpp
    src/codec/webp/webp_validator.h
)
target_include_directories(clipss2_webp_codec PUBLIC src)
target_link_libraries(clipss2_webp_codec PUBLIC clipss2_image PRIVATE WebP::webp)
clipss2_configure_cpp_target(clipss2_webp_codec)

add_library(clipss2_avif_codec STATIC
    src/codec/avif/avif_codec.cpp
    src/codec/avif/avif_codec.h
    src/codec/avif/avif_validator.cpp
    src/codec/avif/avif_validator.h
    src/codec/avif/avif_lossy_validator.cpp
    src/codec/avif/avif_lossy_validator.h
)
target_include_directories(clipss2_avif_codec PUBLIC src)
target_link_libraries(clipss2_avif_codec PUBLIC clipss2_image PRIVATE avif)
clipss2_configure_cpp_target(clipss2_avif_codec)

add_library(clipss2_jxl_codec STATIC
    src/codec/jxl/jxl_codec.cpp
    src/codec/jxl/jxl_validator.cpp
    src/codec/jxl/jxl_codec.h
    src/codec/jxl/jxl_validator.h
    src/codec/jxl/jxl_lossy_validator.cpp
    src/codec/jxl/jxl_lossy_validator.h
    src/codec/jxl/jxl_internal.h
)
target_include_directories(clipss2_jxl_codec PUBLIC src)
target_link_libraries(clipss2_jxl_codec PUBLIC clipss2_image PRIVATE PkgConfig::LIBJXL PkgConfig::LIBJXL_THREADS)
clipss2_configure_cpp_target(clipss2_jxl_codec)

add_library(clipss2_jpeg_codec STATIC
    src/codec/jpeg/jpeg_codec.cpp
    src/codec/jpeg/jpeg_codec.h
    src/codec/jpeg/jpeg_flatten.cpp
    src/codec/jpeg/jpeg_validator.cpp
    src/codec/jpeg/jpeg_validator.h
    src/codec/jpeg/jpeg_backend_policy.cpp
    src/codec/jpeg/jpeg_backend_policy.h
    src/codec/jpeg/jpeg_product_selection.h
    src/codec/jpeg/jpeg_managed_backend.cpp
    src/codec/jpeg/jpeg_managed_backend.h
    src/codec/jpeg/jpeg_codec_internal.h
    src/codec/jpeg/jpegli_backend.cpp
)

set_source_files_properties(src/codec/jpeg/jpegli_backend.cpp PROPERTIES INCLUDE_DIRECTORIES
    "${CLIPSS2_JPEGLI_BUILD_SOURCE_DIR};${CMAKE_BINARY_DIR}/managed-jpegli/lib/include/jpegli")
target_link_libraries(clipss2_jpeg_codec PRIVATE jpegli-static)
target_include_directories(clipss2_jpeg_codec PUBLIC src)
target_link_libraries(clipss2_jpeg_codec PUBLIC clipss2_image)
# Confirmed against the installed manifest package's exported targets/usage.
if(TARGET libjpeg-turbo::turbojpeg)
    target_link_libraries(clipss2_jpeg_codec PRIVATE libjpeg-turbo::turbojpeg)
elseif(TARGET libjpeg-turbo::turbojpeg-static)
    target_link_libraries(clipss2_jpeg_codec PRIVATE libjpeg-turbo::turbojpeg-static)
else()
    message(FATAL_ERROR "libjpeg-turbo package has no TurboJPEG API target")
endif()
target_compile_definitions(clipss2_jpeg_codec PRIVATE
    CLIPSS2_JPEG_BACKEND_VERSION="${libjpeg-turbo_VERSION}"
)
clipss2_configure_cpp_target(clipss2_jpeg_codec)

# Compatibility target; policy/dispatch/raw codec now share one JPEG library,
# avoiding a circular static-library dependency once product uses policy.
add_library(clipss2_jpeg_backend_policy INTERFACE)
target_link_libraries(clipss2_jpeg_backend_policy INTERFACE clipss2_jpeg_codec)

add_library(clipss2_jpeg_audit STATIC
    src/audit/jpeg_output_audit.cpp
    src/audit/jpeg_output_audit.h
)
target_include_directories(clipss2_jpeg_audit PUBLIC src)
target_link_libraries(clipss2_jpeg_audit PUBLIC clipss2_jpeg_codec PRIVATE clipss2_png_codec)
clipss2_configure_cpp_target(clipss2_jpeg_audit)

set(CLIPSS2_PRODUCT_SOURCES
    src/capture/capture_image_output.cpp
    src/capture/capture_image_output.h
    src/platform/windows/clipss2.exe.manifest
    src/main.cpp
    src/platform/windows/capture_window.cpp
    src/platform/windows/clipboard_image.cpp
    src/platform/windows/clipboard_image.h
    src/platform/windows/capture_border_policy.cpp
    src/platform/windows/capture_border_policy.h
    src/platform/windows/capture_reuse_model.cpp
    src/platform/windows/capture_reuse_model.h
    src/platform/windows/capture_worker.cpp
    src/platform/windows/capture_worker.h
    src/platform/windows/capture_worker_client.cpp
    src/platform/windows/capture_worker_client.h
    src/platform/windows/capture_worker_protocol.cpp
    src/platform/windows/capture_worker_protocol.h
    src/platform/windows/capture_worker_product.cpp
    src/platform/windows/capture_worker_product.h
    src/platform/windows/foreground_capture_target.cpp
    src/platform/windows/d3d_device.cpp
    src/platform/windows/d3d_device.h
    src/platform/windows/d3d_device_internal.h
    src/platform/windows/display_color.cpp
    src/platform/windows/display_color_fallback.cpp
    src/platform/windows/display_color.h
    src/platform/windows/display_monitor.cpp
    src/platform/windows/display_monitor.h
    src/platform/windows/error_diagnostics.cpp
    src/platform/windows/error_diagnostics.h
    src/platform/windows/one_frame_capture.cpp
    src/platform/windows/one_frame_capture.h
    src/platform/windows/readback_wait.cpp
    src/platform/windows/readback_wait.h
    src/platform/windows/resident_mode.cpp
    src/platform/windows/resident_mode.h
    src/platform/windows/resident_state.cpp
    src/platform/windows/resident_state.h
    src/platform/windows/settings_ui.cpp
    src/platform/windows/settings_ui.h
    src/platform/windows/settings_location.cpp
    src/platform/windows/settings_location.h
    src/platform/windows/resource_counters.cpp
    src/platform/windows/resource_counters.h
    src/platform/windows/handle_isolation_model.cpp
    src/platform/windows/handle_isolation_model.h
    src/platform/windows/resource_lifetime_model.cpp
    src/platform/windows/resource_lifetime_model.h
    src/platform/windows/texture_readback.cpp
    src/platform/windows/texture_readback.h
    src/platform/windows/frame_geometry.h
    src/platform/windows/tray_icon.cpp
    src/platform/windows/tray_icon.h
    src/platform/windows/window_candidates.cpp
    src/platform/windows/window_target_validation.cpp
    src/platform/windows/window_candidates.h
    src/platform/windows/window_selection.cpp
    src/platform/windows/product_settings.cpp
    src/platform/windows/product_settings.h
    src/platform/windows/output_storage.cpp
    src/platform/windows/output_storage.h
    src/platform/windows/storage_preferences.h
    src/platform/windows/file_publication.cpp
    src/platform/windows/file_publication.h
    src/platform/windows/window_selection_input.cpp
    src/platform/windows/window_selection_input.h
    src/platform/windows/window_selector_ui.cpp
    src/platform/windows/window_selector_ui.h
    src/platform/windows/selector_window_resources.h
    src/platform/windows/capture_source.h
    src/platform/windows/monitor_capture_source.cpp
    src/platform/windows/monitor_capture_source.h
    src/platform/windows/region_selector_ui.cpp
    src/platform/windows/region_selector_ui.h
    src/platform/windows/monitor_selection_navigation.h
)
set(CLIPSS2_AUDIT_SOURCES
    src/platform/windows/alpha_capture_audit.cpp
    src/platform/windows/alpha_capture_audit.h
    src/platform/windows/color_self_test.cpp
    src/platform/windows/capture_border_audit.cpp
    src/platform/windows/capture_border_audit.h
    src/platform/windows/capture_worker_audit.cpp
    src/platform/windows/capture_worker_audit.h
    src/platform/windows/lossy_worker_audit.cpp
    src/platform/windows/lossy_worker_audit.h
    src/platform/windows/display_color_audit.cpp
    src/platform/windows/display_color_audit.h
    src/platform/windows/resource_lifetime_audit.cpp
    src/platform/windows/resource_lifetime_audit.h
    src/platform/windows/window_candidate_self_test.cpp
    src/platform/windows/window_candidate_self_test.h
    src/platform/windows/wgc_handle_isolation_audit.cpp
    src/platform/windows/wgc_handle_isolation_audit.h
    src/platform/windows/wgc_item_reuse_audit.cpp
    src/platform/windows/wgc_item_reuse_audit.h
)
add_executable(clipss2 ${CLIPSS2_PRODUCT_SOURCES})

target_include_directories(clipss2 PRIVATE src)
target_compile_definitions(clipss2 PRIVATE CLIPSS2_AUDIT_FEATURES=0)
clipss2_configure_cpp_target(clipss2)

target_compile_definitions(clipss2 PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(clipss2 PRIVATE
    clipss2_ui_windows
    clipss2_unicode_diagnostics
    clipss2_capture_model
    clipss2_image
    clipss2_png_codec
    clipss2_webp_codec
    clipss2_avif_codec
    clipss2_jxl_codec
    clipss2_jpeg_codec
    d3d11
    dxgi
    dwmapi
    CoreMessaging
    Shcore
    user32
    gdi32
    psapi
    shell32
    ole32
    windowsapp
)

include(CTest)

# Phase 16C0: dependency/API smoke only, independent of the product pipeline.
option(CLIPSS2_BUILD_LIBJXL_SMOKE "Build the libjxl overlay foundation smoke" ON)
if(CLIPSS2_BUILD_LIBJXL_SMOKE)
    include(cmake/LibjxlSmoke.cmake)
endif()

option(CLIPSS2_BUILD_JPEG_BENCH "Build developer-only JPEG evaluation tools" OFF)
if(CLIPSS2_BUILD_JPEG_BENCH)
    add_subdirectory(tools/jpeg_bench)
endif()

option(CLIPSS2_BUILD_LOSSY_EVAL "Build developer-only cross-codec lossy evaluation" OFF)
if(CLIPSS2_BUILD_LOSSY_EVAL)
    add_subdirectory(tools/lossy_eval)
endif()

if(BUILD_TESTING)
    # Same sources, explicit test policy; never selected by portable packaging.
    add_executable(clipss2_shipping_policy_test tests/shipping_policy_test.cpp
        src/platform/windows/capture_worker_protocol.cpp src/platform/windows/output_storage.cpp)
    target_compile_definitions(clipss2_shipping_policy_test PRIVATE
        CLIPSS2_AUDIT_FEATURES=0 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(clipss2_shipping_policy_test PRIVATE src)
    target_link_libraries(clipss2_shipping_policy_test PRIVATE clipss2_capture_model
        clipss2_jpeg_codec clipss2_ui_windows clipss2_unicode_diagnostics shell32 ole32 windowsapp)
    clipss2_configure_cpp_target(clipss2_shipping_policy_test)
    add_test(NAME shipping_policy COMMAND clipss2_shipping_policy_test)
    add_library(clipss2_ui_windows_audit STATIC
        src/platform/windows/ui_localization.cpp src/platform/windows/ui_localization.h)
    target_include_directories(clipss2_ui_windows_audit PUBLIC src)
    target_compile_definitions(clipss2_ui_windows_audit PRIVATE
        CLIPSS2_AUDIT_FEATURES=1 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(clipss2_ui_windows_audit PUBLIC user32 gdi32)
    clipss2_configure_cpp_target(clipss2_ui_windows_audit)

    add_executable(clipss2_audit_fixture ${CLIPSS2_PRODUCT_SOURCES} ${CLIPSS2_AUDIT_SOURCES})
    target_include_directories(clipss2_audit_fixture PRIVATE src)
    target_compile_definitions(clipss2_audit_fixture PRIVATE
        CLIPSS2_AUDIT_FEATURES=1 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    clipss2_configure_cpp_target(clipss2_audit_fixture)
    target_link_libraries(clipss2_audit_fixture PRIVATE
    clipss2_ui_windows_audit
    clipss2_unicode_diagnostics
    clipss2_capture_model
    clipss2_image
    clipss2_png_codec
    clipss2_webp_codec
    clipss2_avif_codec
    clipss2_jxl_codec
    clipss2_jpeg_codec
    d3d11
    dxgi
    dwmapi
    CoreMessaging
    Shcore
    user32
    gdi32
    psapi
    shell32
    ole32
    windowsapp
        clipss2_jpeg_audit)
    add_executable(clipss2_unicode_diagnostics_test tests/unicode_diagnostics_test.cpp)
    target_link_libraries(clipss2_unicode_diagnostics_test PRIVATE clipss2_unicode_diagnostics)
    clipss2_configure_cpp_target(clipss2_unicode_diagnostics_test)
    add_test(NAME unicode_diagnostics COMMAND clipss2_unicode_diagnostics_test)
    add_executable(clipss2_console_lifecycle_test tests/console_lifecycle_test.cpp)
    target_link_libraries(clipss2_console_lifecycle_test PRIVATE clipss2_unicode_diagnostics)
    clipss2_configure_cpp_target(clipss2_console_lifecycle_test)
    add_test(NAME console_lifecycle COMMAND clipss2_console_lifecycle_test $<TARGET_FILE:clipss2>)
    add_executable(clipss2_capture_stage_diagnostics_test tests/capture_stage_diagnostics_test.cpp
        src/platform/windows/error_diagnostics.cpp src/platform/windows/display_color.cpp
        src/platform/windows/display_color_fallback.cpp src/platform/windows/window_target_validation.cpp
        src/platform/windows/window_candidates.cpp src/platform/windows/window_selection.cpp
        src/platform/windows/resource_counters.cpp)
    target_include_directories(clipss2_capture_stage_diagnostics_test PRIVATE src)
    target_compile_definitions(clipss2_capture_stage_diagnostics_test PRIVATE
        CLIPSS2_AUDIT_FEATURES=1 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(clipss2_capture_stage_diagnostics_test PRIVATE clipss2_unicode_diagnostics
        clipss2_capture_model windowsapp dxgi dwmapi psapi)
    clipss2_configure_cpp_target(clipss2_capture_stage_diagnostics_test)
    add_test(NAME capture_stage_diagnostics COMMAND clipss2_capture_stage_diagnostics_test)
    add_executable(clipss2_frame_geometry_test tests/frame_geometry_test.cpp
        src/platform/windows/d3d_device.cpp src/platform/windows/texture_readback.cpp
        src/platform/windows/readback_wait.cpp src/platform/windows/error_diagnostics.cpp
        src/platform/windows/clipboard_image.cpp src/capture/capture_image_output.cpp)
    target_include_directories(clipss2_frame_geometry_test PRIVATE src)
    target_compile_definitions(clipss2_frame_geometry_test PRIVATE
        CLIPSS2_AUDIT_FEATURES=0 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(clipss2_frame_geometry_test PRIVATE clipss2_unicode_diagnostics clipss2_capture_model
        clipss2_png_codec clipss2_jpeg_codec clipss2_webp_codec clipss2_avif_codec clipss2_jxl_codec d3d11 dxgi windowsapp)
    clipss2_configure_cpp_target(clipss2_frame_geometry_test)
    add_test(NAME frame_geometry COMMAND clipss2_frame_geometry_test)
    add_executable(clipss2_ui_localization_test tests/ui_localization_test.cpp)
    target_link_libraries(clipss2_ui_localization_test PRIVATE clipss2_ui_windows_audit)
    clipss2_configure_cpp_target(clipss2_ui_localization_test)
    add_test(NAME ui_localization COMMAND clipss2_ui_localization_test)
    add_executable(clipss2_jpegli_backend_test tests/jpegli_backend_test.cpp tools/jpeg_bench/support.cpp)
    target_include_directories(clipss2_jpegli_backend_test PRIVATE tools/jpeg_bench)
    target_link_libraries(clipss2_jpegli_backend_test PRIVATE clipss2_jpeg_codec clipss2_png_codec bcrypt psapi)
    target_compile_definitions(clipss2_jpegli_backend_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    clipss2_configure_cpp_target(clipss2_jpegli_backend_test)
    add_test(NAME jpegli_backend COMMAND clipss2_jpegli_backend_test)
    add_executable(clipss2_jpeg_backend_policy_test tests/jpeg_backend_policy_test.cpp)
    target_link_libraries(clipss2_jpeg_backend_policy_test PRIVATE clipss2_jpeg_backend_policy)
    clipss2_configure_cpp_target(clipss2_jpeg_backend_policy_test)
    add_test(NAME jpeg_backend_policy COMMAND clipss2_jpeg_backend_policy_test)
    add_executable(clipss2_capture_image_output_test
        tests/capture_image_output_test.cpp src/capture/capture_image_output.cpp)
    target_link_libraries(clipss2_capture_image_output_test PRIVATE
        clipss2_capture_model clipss2_png_codec clipss2_jpeg_codec clipss2_webp_codec clipss2_avif_codec clipss2_jxl_codec)
    clipss2_configure_cpp_target(clipss2_capture_image_output_test)
    add_test(NAME capture_image_output COMMAND clipss2_capture_image_output_test)
    foreach(jpeg_test IN ITEMS alpha_flatten encoder validator)
        add_executable(clipss2_jpeg_${jpeg_test}_test tests/jpeg_${jpeg_test}_test.cpp)
        target_link_libraries(clipss2_jpeg_${jpeg_test}_test PRIVATE clipss2_jpeg_audit)
        clipss2_configure_cpp_target(clipss2_jpeg_${jpeg_test}_test)
        add_test(NAME jpeg_${jpeg_test} COMMAND clipss2_jpeg_${jpeg_test}_test)
    endforeach()

    add_executable(clipss2_tray_icon_test
        tests/tray_icon_test.cpp
        src/platform/windows/tray_icon.cpp
        src/platform/windows/error_diagnostics.cpp
    )
    target_include_directories(clipss2_tray_icon_test PRIVATE src)
    target_compile_definitions(clipss2_tray_icon_test PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_link_libraries(clipss2_tray_icon_test PRIVATE clipss2_ui_windows clipss2_unicode_diagnostics shell32 user32 windowsapp)
    clipss2_configure_cpp_target(clipss2_tray_icon_test)
    add_test(NAME tray_icon COMMAND clipss2_tray_icon_test)

    add_executable(clipss2_resident_state_test
        tests/resident_state_test.cpp
        src/platform/windows/resident_state.cpp
    )
    target_include_directories(clipss2_resident_state_test PRIVATE src)
    clipss2_configure_cpp_target(clipss2_resident_state_test)
    add_test(NAME resident_state COMMAND clipss2_resident_state_test)

    add_executable(clipss2_capture_destination_test
        tests/capture_destination_test.cpp
    )
    target_link_libraries(clipss2_capture_destination_test PRIVATE clipss2_capture_model)
    clipss2_configure_cpp_target(clipss2_capture_destination_test)
    add_test(NAME capture_destination COMMAND clipss2_capture_destination_test)
    add_executable(clipss2_capture_border_policy_test
        tests/capture_border_policy_test.cpp
        src/platform/windows/capture_border_policy.cpp
    )
    target_include_directories(clipss2_capture_border_policy_test PRIVATE src)
    clipss2_configure_cpp_target(clipss2_capture_border_policy_test)
    add_test(NAME capture_border_policy COMMAND clipss2_capture_border_policy_test)
    add_executable(clipss2_capture_window_policy_test tests/capture_window_policy_test.cpp)
    target_include_directories(clipss2_capture_window_policy_test PRIVATE src)
    clipss2_configure_cpp_target(clipss2_capture_window_policy_test)
    add_test(NAME capture_window_policy COMMAND clipss2_capture_window_policy_test)
    add_executable(clipss2_window_selection_test
        tests/window_selection_test.cpp
        src/platform/windows/window_selection.cpp
        src/platform/windows/window_selection_input.cpp
    )
    target_include_directories(clipss2_window_selection_test PRIVATE src)
    target_link_libraries(clipss2_window_selection_test PRIVATE clipss2_jpeg_codec clipss2_capture_model)
    add_executable(clipss2_product_settings_test tests/product_settings_test.cpp
        src/platform/windows/product_settings.cpp src/platform/windows/output_storage.cpp)
    target_include_directories(clipss2_product_settings_test PRIVATE src)
    target_link_libraries(clipss2_product_settings_test PRIVATE clipss2_ui_windows clipss2_unicode_diagnostics clipss2_jpeg_codec shell32 ole32 windowsapp)
    clipss2_configure_cpp_target(clipss2_product_settings_test)
    add_test(NAME product_settings COMMAND clipss2_product_settings_test)
    add_executable(clipss2_output_storage_test tests/output_storage_test.cpp
        src/platform/windows/output_storage.cpp src/platform/windows/file_publication.cpp
        src/platform/windows/product_settings.cpp)
    target_include_directories(clipss2_output_storage_test PRIVATE src)
    target_link_libraries(clipss2_output_storage_test PRIVATE clipss2_ui_windows clipss2_unicode_diagnostics clipss2_jpeg_codec shell32 ole32 windowsapp)
    target_compile_definitions(clipss2_output_storage_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    clipss2_configure_cpp_target(clipss2_output_storage_test)
    add_test(NAME output_storage COMMAND clipss2_output_storage_test)

    add_executable(clipss2_settings_location_test tests/settings_location_test.cpp
        src/platform/windows/settings_location.cpp src/platform/windows/product_settings.cpp
        src/platform/windows/output_storage.cpp)
    target_include_directories(clipss2_settings_location_test PRIVATE src)
    target_compile_definitions(clipss2_settings_location_test PRIVATE CLIPSS2_AUDIT_FEATURES=0 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(clipss2_settings_location_test PRIVATE clipss2_ui_windows
        clipss2_unicode_diagnostics clipss2_jpeg_codec shell32 ole32 windowsapp)
    clipss2_configure_cpp_target(clipss2_settings_location_test)
    add_test(NAME settings_location COMMAND clipss2_settings_location_test)

    add_executable(clipss2_region_geometry_test tests/region_geometry_test.cpp
        src/platform/windows/monitor_capture_source.cpp)
    target_include_directories(clipss2_region_geometry_test PRIVATE src)
    target_compile_definitions(clipss2_region_geometry_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(clipss2_region_geometry_test PRIVATE windowsapp)
    clipss2_configure_cpp_target(clipss2_region_geometry_test)
    add_test(NAME region_geometry COMMAND clipss2_region_geometry_test)
    clipss2_configure_cpp_target(clipss2_window_selection_test)
    add_test(NAME window_selection COMMAND clipss2_window_selection_test)
    add_executable(clipss2_srgb_transfer_test tests/srgb_transfer_test.cpp)
    target_link_libraries(clipss2_srgb_transfer_test PRIVATE clipss2_image)
    clipss2_configure_cpp_target(clipss2_srgb_transfer_test)
    add_test(NAME srgb_transfer COMMAND clipss2_srgb_transfer_test)
    add_executable(clipss2_row_copy_test
        tests/row_copy_test.cpp
    )

    target_link_libraries(clipss2_row_copy_test PRIVATE clipss2_image)
    clipss2_configure_cpp_target(clipss2_row_copy_test)

    add_test(NAME row_copy COMMAND clipss2_row_copy_test)

    add_executable(clipss2_png_codec_test
        tests/png_codec_test.cpp
    )

    target_link_libraries(clipss2_png_codec_test PRIVATE clipss2_png_codec)
    clipss2_configure_cpp_target(clipss2_png_codec_test)

    add_test(NAME png_codec COMMAND clipss2_png_codec_test)

    add_executable(clipss2_webp_codec_test tests/webp_codec_test.cpp)
    target_link_libraries(clipss2_webp_codec_test PRIVATE clipss2_webp_codec clipss2_png_codec)
    clipss2_configure_cpp_target(clipss2_webp_codec_test)
    add_test(NAME webp_codec COMMAND clipss2_webp_codec_test)

    add_executable(clipss2_jxl_codec_test tests/jxl_codec_test.cpp)
    target_link_libraries(clipss2_jxl_codec_test PRIVATE clipss2_jxl_codec clipss2_png_codec clipss2_webp_codec PkgConfig::LIBJXL)
    clipss2_configure_cpp_target(clipss2_jxl_codec_test)
    add_test(NAME jxl_codec COMMAND clipss2_jxl_codec_test)

    add_executable(clipss2_avif_codec_test tests/avif_codec_test.cpp)
    target_link_libraries(clipss2_avif_codec_test PRIVATE clipss2_avif_codec clipss2_png_codec clipss2_webp_codec)
    clipss2_configure_cpp_target(clipss2_avif_codec_test)
    add_test(NAME avif_codec COMMAND clipss2_avif_codec_test)

    add_executable(clipss2_clipboard_image_test
        tests/clipboard_image_test.cpp
        src/platform/windows/clipboard_image.cpp
    )
    target_include_directories(clipss2_clipboard_image_test PRIVATE src)
    target_compile_definitions(clipss2_clipboard_image_test PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(clipss2_clipboard_image_test PRIVATE
        clipss2_png_codec
        user32
        windowsapp
    )
    clipss2_configure_cpp_target(clipss2_clipboard_image_test)
    add_test(NAME clipboard_image COMMAND clipss2_clipboard_image_test)

    add_executable(clipss2_fp16_diagnostics_test
        tests/fp16_diagnostics_test.cpp
    )

    target_link_libraries(clipss2_fp16_diagnostics_test PRIVATE clipss2_image)
    clipss2_configure_cpp_target(clipss2_fp16_diagnostics_test)

    add_test(NAME fp16_diagnostics COMMAND clipss2_fp16_diagnostics_test)

    add_executable(clipss2_alpha_diagnostics_test
        tests/alpha_diagnostics_test.cpp
    )
    target_link_libraries(clipss2_alpha_diagnostics_test PRIVATE clipss2_image)
    clipss2_configure_cpp_target(clipss2_alpha_diagnostics_test)
    add_test(NAME alpha_diagnostics COMMAND clipss2_alpha_diagnostics_test)

    add_executable(clipss2_robustness_test
        tests/robustness_test.cpp
        src/platform/windows/error_diagnostics.cpp
        src/platform/windows/readback_wait.cpp
        src/platform/windows/resource_lifetime_model.cpp
        src/platform/windows/handle_isolation_model.cpp
        src/platform/windows/capture_reuse_model.cpp
    )
    target_include_directories(clipss2_robustness_test PRIVATE src)
    target_compile_definitions(clipss2_robustness_test PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(clipss2_robustness_test PRIVATE clipss2_unicode_diagnostics dxgi windowsapp)
    clipss2_configure_cpp_target(clipss2_robustness_test)
    add_test(NAME robustness COMMAND clipss2_robustness_test)

    add_executable(clipss2_capture_worker_protocol_test
        tests/capture_worker_protocol_test.cpp
        src/capture/capture_destination.cpp
        src/platform/windows/capture_worker_protocol.cpp
    )
    target_include_directories(clipss2_capture_worker_protocol_test PRIVATE src)
    target_link_libraries(clipss2_capture_worker_protocol_test PRIVATE clipss2_jpeg_codec)
    target_compile_definitions(clipss2_capture_worker_protocol_test PRIVATE CLIPSS2_AUDIT_FEATURES=1 NOMINMAX WIN32_LEAN_AND_MEAN)
    clipss2_configure_cpp_target(clipss2_capture_worker_protocol_test)
    add_test(NAME capture_worker_protocol COMMAND clipss2_capture_worker_protocol_test)

    add_executable(clipss2_capture_worker_product_test
        tests/capture_worker_product_test.cpp
        src/capture/capture_destination.cpp
        src/platform/windows/capture_worker_product.cpp
    )
    target_include_directories(clipss2_capture_worker_product_test PRIVATE src)
    clipss2_configure_cpp_target(clipss2_capture_worker_product_test)
    add_test(NAME capture_worker_product COMMAND clipss2_capture_worker_product_test)

    add_executable(clipss2_capture_worker_client_test
        tests/capture_worker_client_test.cpp
        src/capture/capture_destination.cpp
        src/platform/windows/capture_worker_client.cpp
        src/platform/windows/capture_worker_protocol.cpp
        src/platform/windows/error_diagnostics.cpp
    )
    target_include_directories(clipss2_capture_worker_client_test PRIVATE src)
    target_compile_definitions(clipss2_capture_worker_client_test PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(clipss2_capture_worker_client_test PRIVATE
        clipss2_unicode_diagnostics
        clipss2_jpeg_codec
        shell32
        user32
        windowsapp
    )
    target_compile_definitions(clipss2_capture_worker_client_test PRIVATE CLIPSS2_AUDIT_FEATURES=1)
    clipss2_configure_cpp_target(clipss2_capture_worker_client_test)
    add_test(NAME capture_worker_client COMMAND clipss2_capture_worker_client_test)
endif()

if(BUILD_TESTING AND CLIPSS2_SHIPPING_PRIVACY)
    add_executable(clipss2_privacy_diagnostics_test tests/privacy_diagnostics_test.cpp)
    target_include_directories(clipss2_privacy_diagnostics_test PRIVATE
        "${CLIPSS2_JPEGLI_BUILD_SOURCE_DIR}"
        "${CLIPSS2_JPEGLI_BUILD_SOURCE_DIR}/third_party/highway"
        "${CMAKE_BINARY_DIR}/managed-jpegli/lib/include/jpegli")
    target_link_libraries(clipss2_privacy_diagnostics_test PRIVATE jpegli-static hwy)
    clipss2_configure_cpp_target(clipss2_privacy_diagnostics_test)
    target_compile_options(clipss2_privacy_diagnostics_test PRIVATE /EHs /EHc-)
    add_test(NAME privacy_diagnostics COMMAND clipss2_privacy_diagnostics_test)
endif()
