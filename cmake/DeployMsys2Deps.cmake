# DeployMsys2Deps.cmake
#
# On MinGW builds, copies required msys2 DLLs next to the executable so it
# can find them at runtime without msys2/mingw64/bin on PATH.
#
# Usage:
#   deploy_msys2_deps(automotive-format-explorer)

function(deploy_msys2_deps target)
    if(NOT MINGW)
        return()
    endif()

    # Locate the msys2 mingw64 bin directory from the protobuf DLL
    find_file(_protobuf_dll
        NAMES libprotobuf.dll
        PATHS "C:/msys64/mingw64/bin"
        NO_DEFAULT_PATH
    )
    if(NOT _protobuf_dll)
        message(WARNING "deploy_msys2_deps: libprotobuf.dll not found in msys2, skipping DLL deployment")
        return()
    endif()
    get_filename_component(_msys2_bin "${_protobuf_dll}" DIRECTORY)

    # Collect all msys2 DLLs needed by protobuf + abseil.
    # This list was derived from objdump -p on the dependency chain.
    file(GLOB _absl_dlls "${_msys2_bin}/libabsl_*.dll")
    set(_dlls
        "${_msys2_bin}/libprotobuf.dll"
        "${_msys2_bin}/libutf8_validity.dll"
        "${_msys2_bin}/libutf8_range.dll"
        "${_msys2_bin}/zlib1.dll"
        "${_msys2_bin}/libgcc_s_seh-1.dll"
        "${_msys2_bin}/libstdc++-6.dll"
        "${_msys2_bin}/libwinpthread-1.dll"
        ${_absl_dlls}
    )

    foreach(_dll ${_dlls})
        if(EXISTS "${_dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Deploying ${_dll}"
            )
        endif()
    endforeach()
endfunction()
