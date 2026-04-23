if(NOT DEFINED DST_DIR)
    message(FATAL_ERROR "CopyRuntimeDlls.cmake requires DST_DIR.")
endif()

if(NOT DEFINED RUNTIME_DLLS OR RUNTIME_DLLS STREQUAL "")
    return()
endif()

foreach(runtime_dll IN LISTS RUNTIME_DLLS)
    if(runtime_dll STREQUAL "")
        continue()
    endif()

    if(EXISTS "${runtime_dll}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_dll}" "${DST_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endforeach()
