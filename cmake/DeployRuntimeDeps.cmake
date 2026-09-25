# DeployRuntimeDeps.cmake
#
# On MinGW builds, copies the toolchain dependencies of the built executable
# next to it so a build-tree run works without msys2/mingw64/bin on PATH.
#
# The closure is walked by scripts/deploy_closure.sh — the same walk that builds
# the shipped bundle — so no DLL is named here. Qt is passed as `--provided`:
# build-tree runs resolve Qt from its own install, as ctest does, and offering
# it only after the toolchain keeps Qt's older MinGW runtime from being taken
# for ours. The Explorer's own libraries are static archives inside the
# executable, so the executable is the only root.
#
# Usage:
#   deploy_runtime_deps(automotive-format-explorer)

function(deploy_runtime_deps target)
    if(NOT MINGW)
        return()
    endif()

    get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)

    # msys2's bash sits two levels above the mingw64 compiler building this
    # project: <msys2>/mingw64/bin/g++.exe -> <msys2>/usr/bin/bash.exe. Deriving
    # it from the compiler rather than PATH also keeps System32's WSL launcher
    # from answering to the name.
    get_filename_component(_msys2_root "${_toolchain_bin}/../.." ABSOLUTE)
    find_program(DEPLOY_CLOSURE_BASH
        NAMES bash
        HINTS "${_msys2_root}/usr/bin"
        NO_DEFAULT_PATH
    )
    if(NOT DEPLOY_CLOSURE_BASH)
        message(WARNING
            "deploy_runtime_deps: no msys2 bash under ${_msys2_root}, "
            "build-tree dependencies are not deployed")
        return()
    endif()

    get_filename_component(_repo_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${DEPLOY_CLOSURE_BASH}" "${_repo_root}/scripts/deploy_closure.sh"
            --dest "$<TARGET_FILE_DIR:${target}>"
            --search "${_toolchain_bin}"
            --provided "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
            "$<TARGET_FILE:${target}>"
        COMMENT "Deploying runtime dependencies"
        VERBATIM
    )
endfunction()
