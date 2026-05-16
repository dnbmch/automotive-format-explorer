# FetchParserLib.cmake
#
# Downloads precompiled parser library release artifacts from GitHub
# and creates an imported static library target.
#
# Usage:
#   fetch_parser_lib(
#       TARGET      dbcparser
#       REPO        dnbmch/dbc-parser-lib
#       VERSION     v0.1.0
#       HEADER      dbcfile.h
#   )

function(fetch_parser_lib)
    set(options "")
    set(oneValueArgs TARGET REPO VERSION HEADER)
    set(multiValueArgs "")
    cmake_parse_arguments(FPL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach(required TARGET REPO VERSION HEADER)
        if(NOT FPL_${required})
            message(FATAL_ERROR "fetch_parser_lib: ${required} is required")
        endif()
    endforeach()

    # Determine platform suffix for the binary artifact
    if(WIN32)
        if(MSVC)
            set(platform_suffix "x86_64-windows-msvc")
            set(lib_filename "${FPL_TARGET}.lib")
        else()
            set(platform_suffix "x86_64-windows-mingw")
            set(lib_filename "lib${FPL_TARGET}.a")
        endif()
    elseif(APPLE)
        # macOS builds use the linux-gnu naming if cross-compiled or native aarch64
        execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(HOST_ARCH STREQUAL "arm64" OR HOST_ARCH STREQUAL "aarch64")
            set(platform_suffix "aarch64-linux-gnu")
        else()
            set(platform_suffix "x86_64-linux-gnu")
        endif()
        set(lib_filename "lib${FPL_TARGET}.a")
    else()
        execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(HOST_ARCH STREQUAL "aarch64")
            set(platform_suffix "aarch64-linux-gnu")
        else()
            set(platform_suffix "x86_64-linux-gnu")
        endif()
        set(lib_filename "lib${FPL_TARGET}.a")
    endif()

    set(base_url "https://github.com/${FPL_REPO}/releases/download/${FPL_VERSION}")
    set(headers_archive "${FPL_TARGET}-headers-${FPL_VERSION}.tar.gz")
    set(binary_archive  "${FPL_TARGET}-${platform_suffix}-${FPL_VERSION}.tar.gz")

    # Download into a cache directory under the build tree
    set(cache_dir "${CMAKE_BINARY_DIR}/_parser_deps/${FPL_TARGET}-${FPL_VERSION}")
    set(headers_dir "${cache_dir}/include")
    set(lib_dir     "${cache_dir}/lib")

    # Download and extract headers (skip if sentinel exists)
    if(NOT EXISTS "${headers_dir}/${FPL_HEADER}")
        message(STATUS "Fetching ${headers_archive} ...")
        set(headers_tarball "${cache_dir}/${headers_archive}")
        file(DOWNLOAD
            "${base_url}/${headers_archive}"
            "${headers_tarball}"
            STATUS download_status
            TIMEOUT 60
        )
        list(GET download_status 0 status_code)
        if(NOT status_code EQUAL 0)
            list(GET download_status 1 status_msg)
            message(FATAL_ERROR
                "Failed to download ${headers_archive}:\n  ${status_msg}\n"
                "URL: ${base_url}/${headers_archive}")
        endif()
        file(MAKE_DIRECTORY "${headers_dir}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${headers_tarball}"
            WORKING_DIRECTORY "${headers_dir}"
            RESULT_VARIABLE extract_result
        )
        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract ${headers_archive}")
        endif()
        file(REMOVE "${headers_tarball}")
    endif()

    # Download and extract binary (skip if sentinel exists)
    if(NOT EXISTS "${lib_dir}/${lib_filename}")
        message(STATUS "Fetching ${binary_archive} ...")
        set(binary_tarball "${cache_dir}/${binary_archive}")
        file(DOWNLOAD
            "${base_url}/${binary_archive}"
            "${binary_tarball}"
            STATUS download_status
            TIMEOUT 120
        )
        list(GET download_status 0 status_code)
        if(NOT status_code EQUAL 0)
            list(GET download_status 1 status_msg)
            message(FATAL_ERROR
                "Failed to download ${binary_archive}:\n  ${status_msg}\n"
                "URL: ${base_url}/${binary_archive}")
        endif()
        file(MAKE_DIRECTORY "${lib_dir}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${binary_tarball}"
            WORKING_DIRECTORY "${lib_dir}"
            RESULT_VARIABLE extract_result
        )
        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract ${binary_archive}")
        endif()
        file(REMOVE "${binary_tarball}")
    endif()

    # Verify
    if(NOT EXISTS "${headers_dir}/${FPL_HEADER}")
        message(FATAL_ERROR "Header ${FPL_HEADER} not found after extraction in ${headers_dir}")
    endif()
    set(lib_path "${lib_dir}/${lib_filename}")
    if(NOT EXISTS "${lib_path}")
        message(FATAL_ERROR "Library ${lib_filename} not found after extraction in ${lib_dir}")
    endif()

    # Regenerate .pb.h files from .proto sources using the local protoc.
    # The release artifacts contain .pb.h generated by CI's protoc version, which
    # may not match the local protobuf headers. Regenerating ensures compatibility.
    set(gencode_dir "${cache_dir}/gencode")
    file(GLOB_RECURSE proto_files "${headers_dir}/*.proto")
    if(proto_files)
        # Use a sentinel to avoid re-running protoc on every configure
        set(gencode_sentinel "${gencode_dir}/.generated")
        if(NOT EXISTS "${gencode_sentinel}")
            # Resolve protoc from the same Protobuf package that CMake found,
            # to guarantee the generated code matches the installed headers.
            set(PROTOC_EXECUTABLE "")
            if(TARGET protobuf::protoc)
                get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION)
                if(NOT PROTOC_EXECUTABLE)
                    get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION_RELEASE)
                endif()
                if(NOT PROTOC_EXECUTABLE)
                    get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION_NOCONFIG)
                endif()
            endif()
            if(NOT PROTOC_EXECUTABLE AND Protobuf_PROTOC_EXECUTABLE)
                set(PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}")
            endif()
            if(NOT PROTOC_EXECUTABLE)
                find_program(PROTOC_EXECUTABLE protoc)
            endif()
            if(NOT PROTOC_EXECUTABLE)
                message(FATAL_ERROR "protoc not found — needed to regenerate .pb.h for local protobuf version")
            endif()
            message(STATUS "Using protoc: ${PROTOC_EXECUTABLE}")
            file(MAKE_DIRECTORY "${gencode_dir}")
            foreach(proto_file ${proto_files})
                message(STATUS "Generating protobuf code: ${proto_file}")
                execute_process(
                    COMMAND "${PROTOC_EXECUTABLE}"
                        "--proto_path=${headers_dir}"
                        "--cpp_out=${gencode_dir}"
                        "${proto_file}"
                    RESULT_VARIABLE protoc_result
                )
                if(NOT protoc_result EQUAL 0)
                    message(FATAL_ERROR "protoc failed on ${proto_file}")
                endif()
            endforeach()
            file(TOUCH "${gencode_sentinel}")
        endif()
        # Replace pre-generated .pb.h in lib_dir with locally generated ones
        # (preserving subdirectory structure)
        file(GLOB_RECURSE generated_headers "${gencode_dir}/*.pb.h")
        foreach(gen_header ${generated_headers})
            file(RELATIVE_PATH rel_path "${gencode_dir}" "${gen_header}")
            get_filename_component(rel_dir "${rel_path}" DIRECTORY)
            if(rel_dir)
                file(MAKE_DIRECTORY "${lib_dir}/${rel_dir}")
            endif()
            file(COPY_FILE "${gen_header}" "${lib_dir}/${rel_path}" ONLY_IF_DIFFERENT)
        endforeach()
    endif()

    # Create imported target
    add_library(${FPL_TARGET} STATIC IMPORTED GLOBAL)
    set_target_properties(${FPL_TARGET} PROPERTIES
        IMPORTED_LOCATION "${lib_path}"
    )
    target_include_directories(${FPL_TARGET} INTERFACE
        "${headers_dir}"
        "${lib_dir}"
    )
    target_link_libraries(${FPL_TARGET} INTERFACE protobuf::libprotobuf)

    message(STATUS "${FPL_TARGET} ${FPL_VERSION} ready (${platform_suffix})")
endfunction()
