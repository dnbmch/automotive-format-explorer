find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# On Windows the test executables resolve explorer-core / the backend DLLs from
# their own directory, but Qt's DLLs live in the Qt install. Hand ctest a PATH
# that includes them so the suite runs without developer shell setup.
#
# The compiler's own bin comes first: a standalone Qt mingw distribution bundles
# its own libgcc/libstdc++, built by an older MinGW than the one compiling here.
# If Qt's bin wins the search the loader binds the older runtime and the process
# dies with STATUS_ENTRYPOINT_NOT_FOUND before main(). The C++ runtime has to
# come from the toolchain that produced the binaries — the same rule
# scripts/package_windows.sh applies when packaging.
if(WIN32)
    get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_test_path "${_compiler_bin};${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS};$ENV{PATH}")
    string(REPLACE "/" "\\" _test_path "${_test_path}")
    string(REPLACE ";" "\\;" _test_path "${_test_path}")
endif()

# Test executables are emitted next to the libraries they link so the shared
# explorer-core / backend binaries resolve at runtime.
function(explorer_add_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        WIN32_EXECUTABLE FALSE
    )
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${_test_path}")
    endif()
endfunction()

explorer_add_test(tst_treefiltermodel explorer-core)

explorer_add_test(tst_memorymapmodel explorer-core)

explorer_add_test(tst_signalplotmodel explorer-core)

explorer_add_test(tst_a2ldetailpresenter
    explorer-core
    explorer-a2l-backend
    protobuf::libprotobuf
    a2lparser
)

explorer_add_test(tst_mdf4detailpresenter
    explorer-core
    explorer-mdf4-backend
    protobuf::libprotobuf
    mdf4parser
)

explorer_add_test(tst_mdf4documentsession
    explorer-core
    explorer-mdf4-backend
    Qt6::Concurrent
    protobuf::libprotobuf
    mdf4parser
)

# Runs against the bundled writer-authored recording; MDF4_WRITER_SAMPLE
# overrides it for a different file, and the executable exits 77 when the
# variable resolves to nothing so the case reports as Skipped, never as a
# silent pass.
explorer_add_test(tst_mdf4writerfile
    explorer-core
    explorer-mdf4-backend
    Qt6::Concurrent
    protobuf::libprotobuf
    mdf4parser
)
set_tests_properties(tst_mdf4writerfile PROPERTIES SKIP_RETURN_CODE 77)
set_property(TEST tst_mdf4writerfile APPEND PROPERTY ENVIRONMENT
    "MDF4_WRITER_SAMPLE=${CMAKE_SOURCE_DIR}/samples/demo_recording.mf4")
