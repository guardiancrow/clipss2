# Copyright (c) 2026 guardiancrow. MIT.
if(check STREQUAL "convert")
    foreach(probe IN ITEMS reference candidate)
        execute_process(COMMAND "${${probe}}" convert
            COMMAND_ERROR_IS_FATAL ANY OUTPUT_VARIABLE ${probe}_result)
        message(STATUS "${probe}: ${${probe}_result}")
    endforeach()
    if(NOT reference_result STREQUAL candidate_result)
        message(FATAL_ERROR "SEC-MAINT-3E: libyuv conversion or scaling output changed")
    endif()
    return()
endif()

file(MAKE_DIRECTORY "${evidence}")
foreach(fixture IN ITEMS odd alpha)
    foreach(mode IN ITEMS lossless lossy)
        foreach(encoder IN ITEMS reference candidate)
            set(decoder candidate)
            if(encoder STREQUAL "candidate")
                set(decoder reference)
            endif()
            set(output "${evidence}/${encoder}-${fixture}-${mode}.avif")
            execute_process(COMMAND "${${encoder}}" encode "${fixture}" "${mode}" "${output}"
                COMMAND_ERROR_IS_FATAL ANY)
            execute_process(COMMAND "${${decoder}}" decode "${fixture}" "${mode}" "${output}"
                COMMAND_ERROR_IS_FATAL ANY)
        endforeach()
    endforeach()
endforeach()
