# Copyright (c) 2026 guardiancrow. MIT.
string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
set(evidence "${evidence_root}/${check}-${id}")
file(MAKE_DIRECTORY "${evidence}")
if(check STREQUAL "jpeg")
    foreach(encoder IN ITEMS reference candidate)
        if(encoder STREQUAL "reference")
            set(decoder candidate)
        else()
            set(decoder reference)
        endif()
        execute_process(COMMAND "${${encoder}}" encode "${encoder}.jpg"
            WORKING_DIRECTORY "${evidence}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${${decoder}}" decode "${encoder}.jpg"
            WORKING_DIRECTORY "${evidence}" OUTPUT_VARIABLE metrics
            COMMAND_ERROR_IS_FATAL ANY)
        message("${encoder} -> ${decoder}: ${metrics}")
        file(APPEND "${evidence}/metrics.tsv" "${encoder}\t${decoder}\t${metrics}")
    endforeach()
else()
    execute_process(COMMAND "${candidate}" fixture
        WORKING_DIRECTORY "${evidence}" COMMAND_ERROR_IS_FATAL ANY)
    foreach(loader IN ITEMS reference candidate)
        execute_process(COMMAND "${${loader}}" load
            WORKING_DIRECTORY "${evidence}" OUTPUT_VARIABLE result
            COMMAND_ERROR_IS_FATAL ANY)
        message("${loader}: ${result}")
        file(APPEND "${evidence}/loader.tsv" "${loader}\t${result}")
    endforeach()
endif()
