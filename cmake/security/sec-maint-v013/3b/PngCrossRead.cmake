# Copyright (c) 2026 guardiancrow. MIT.
string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" evidence_id)
set(evidence "${evidence_root}/${fixture}-${evidence_id}")
file(MAKE_DIRECTORY "${evidence}")

foreach(encoder IN ITEMS reference candidate)
    if(encoder STREQUAL "reference")
        set(decoder candidate)
    else()
        set(decoder reference)
    endif()
    set(image "${evidence}/${encoder}.png")
    execute_process(COMMAND "${${encoder}}" write "${fixture}" "${image}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${${decoder}}" read "${fixture}" "${image}"
        COMMAND_ERROR_IS_FATAL ANY)
    file(SHA256 "${image}" digest)
    file(APPEND "${evidence}/cross-read.tsv" "${fixture}\t${encoder}\t${decoder}\t${digest}\n")
    if(split_idat)
        set(image "${evidence}/${encoder}-split.png")
        execute_process(COMMAND "${${encoder}}" write-split "${fixture}" "${image}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${${decoder}}" read "${fixture}" "${image}"
            COMMAND_ERROR_IS_FATAL ANY)
        file(SHA256 "${image}" digest)
        file(APPEND "${evidence}/split-idat.tsv" "${fixture}\t${encoder}\t${decoder}\t${digest}\n")
    endif()
endforeach()
