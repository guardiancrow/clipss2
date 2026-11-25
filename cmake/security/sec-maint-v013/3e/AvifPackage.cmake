# Copyright (c) 2026 guardiancrow. MIT.
find_package(AOM CONFIG REQUIRED)
find_package(libyuv CONFIG REQUIRED)
clipss2_check_3d_shared(AOM::aom aom.lib aom.dll)
clipss2_check_3d_shared(yuv libyuv.lib libyuv.dll)
clipss2_check_3d_shared(avif avif.lib avif.dll)
