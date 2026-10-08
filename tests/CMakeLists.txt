find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# On Windows the test executables resolve Qt's DLLs from the Qt install and the
# toolchain's from the compiler's directory. Hand ctest a PATH that includes
# both so the suite runs without developer shell setup.
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

function(explorer_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${_test_path}")
    endif()
endfunction()

explorer_add_test(tst_treefiltermodel explorer-core)

explorer_add_test(tst_detailmodel explorer-core)

explorer_add_test(tst_memorymapmodel explorer-core)

# The painted grid is compiled into the executable; its test compiles the same
# source and paints into an image on the offscreen platform.
explorer_add_test(tst_memorygriditem explorer-core Qt6::Quick)
target_sources(tst_memorygriditem PRIVATE ${PROJECT_SOURCE_DIR}/src/ui/memorygriditem.cpp)
set_property(TEST tst_memorygriditem APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

explorer_add_test(tst_signalgriditem explorer-core Qt6::Quick)
target_sources(tst_signalgriditem PRIVATE ${PROJECT_SOURCE_DIR}/src/ui/signalgriditem.cpp)
set_property(TEST tst_signalgriditem APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

explorer_add_test(tst_signalplotitem explorer-core Qt6::Quick)
target_sources(tst_signalplotitem PRIVATE ${PROJECT_SOURCE_DIR}/src/ui/signalplotitem.cpp)
set_property(TEST tst_signalplotitem APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The production signal plot view in a test-time ExplorerApp module:
# SignalPlotView.qml and Theme.qml are copied from qml/components, and the test
# registers the painted plot it compiles and the plot model as main.cpp does.
explorer_add_test(tst_signalplotview explorer-core Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_sources(tst_signalplotview PRIVATE ${PROJECT_SOURCE_DIR}/src/ui/signalplotitem.cpp)
set(_plotview_module "${CMAKE_CURRENT_BINARY_DIR}/plotview_module/ExplorerApp")
configure_file(${PROJECT_SOURCE_DIR}/qml/components/SignalPlotView.qml ${_plotview_module}/SignalPlotView.qml COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/qml/components/Theme.qml ${_plotview_module}/Theme.qml COPYONLY)
configure_file(plotview/qmldir ${_plotview_module}/qmldir COPYONLY)
target_compile_definitions(tst_signalplotview PRIVATE
    PLOTVIEW_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}/plotview_module")
set_property(TEST tst_signalplotview APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

explorer_add_test(tst_plotdata explorer-core)

explorer_add_test(tst_signalplotmodel explorer-core)

explorer_add_test(tst_documenttab explorer-core)

# The production nav panel in a test-time ExplorerApp module: NavPanel.qml and
# Theme.qml are copied from qml/components next to a stand-in AppController.
explorer_add_test(tst_navpanel explorer-core Qt6::Qml Qt6::Quick Qt6::QuickControls2)
set(_navpanel_module "${CMAKE_CURRENT_BINARY_DIR}/navpanel_module/ExplorerApp")
configure_file(${PROJECT_SOURCE_DIR}/qml/components/NavPanel.qml ${_navpanel_module}/NavPanel.qml COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/qml/components/Theme.qml ${_navpanel_module}/Theme.qml COPYONLY)
configure_file(navpanel/AppController.qml ${_navpanel_module}/AppController.qml COPYONLY)
configure_file(navpanel/qmldir ${_navpanel_module}/qmldir COPYONLY)
target_compile_definitions(tst_navpanel PRIVATE
    NAVPANEL_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}/navpanel_module"
    NAVPANEL_HOST="${CMAKE_CURRENT_SOURCE_DIR}/navpanel/host.qml")
set_property(TEST tst_navpanel APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

explorer_add_test(tst_appcontroller explorer-core)

explorer_add_test(tst_opensequence explorer-core)

# The production format list, opening each bundled sample through the controller.
explorer_add_test(tst_builtinformats explorer-formats)
target_compile_definitions(tst_builtinformats PRIVATE
    EXPLORER_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")

explorer_add_test(tst_a2ldetailpresenter
    explorer-core
    explorer-a2l-backend
    protobuf::libprotobuf
    a2lparser::a2lparser
)

explorer_add_test(tst_mdf4detailpresenter
    explorer-core
    explorer-mdf4-backend
    protobuf::libprotobuf
    mdf4parser::mdf4parser
)

explorer_add_test(tst_mdf4documentsession
    explorer-core
    explorer-mdf4-backend
    Qt6::Concurrent
    protobuf::libprotobuf
    mdf4parser::mdf4parser
)

# Generated recordings through the production adapter and the real reader.
explorer_add_test(tst_mdf4recording
    explorer-core
    explorer-mdf4-backend
    Qt6::Concurrent
    protobuf::libprotobuf
    mdf4parser::mdf4parser
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
    mdf4parser::mdf4parser
)
set_tests_properties(tst_mdf4writerfile PROPERTIES SKIP_RETURN_CODE 77)
set_property(TEST tst_mdf4writerfile APPEND PROPERTY ENVIRONMENT
    "MDF4_WRITER_SAMPLE=${PROJECT_SOURCE_DIR}/samples/demo_recording.mf4")
