cmake_minimum_required(VERSION 3.21)
project(automotive_format_explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt's OpenGL link resolves to libGL.so.1 on Linux, as Qt's own libraries link.
# CMake's GLVND default would name libOpenGL.so.0 instead, which a desktop
# without the libopengl0 package lacks, and the executable would not start there.
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Concurrent Gui Qml Quick QuickControls2 QuickDialogs2)
# Explorer uses protobuf reflection and JSON utilities directly.
find_package(Protobuf CONFIG QUIET)
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DeployRuntimeDeps)

# Workspace composition supplies source targets; standalone builds consume
# complete installed packages via CMAKE_PREFIX_PATH. Configure is offline.
set(AFF_PARSER_MODE "PACKAGE" CACHE STRING "Parser dependencies: SOURCE or PACKAGE")
set_property(CACHE AFF_PARSER_MODE PROPERTY STRINGS SOURCE PACKAGE)
if(NOT AFF_PARSER_MODE MATCHES "^(SOURCE|PACKAGE)$")
    message(FATAL_ERROR "AFF_PARSER_MODE must be SOURCE or PACKAGE")
endif()
foreach(_parser a2lparser dbcparser ldfparser mdf4parser)
    if(AFF_PARSER_MODE STREQUAL "PACKAGE")
        find_package(${_parser} CONFIG REQUIRED)
    elseif(NOT TARGET ${_parser}::${_parser})
        message(FATAL_ERROR "SOURCE mode requires ${_parser}::${_parser}; configure the workspace with AFF_BUILD_EXPLORER=ON")
    endif()
endforeach()

qt_standard_project_setup(REQUIRES 6.5)

# Every Explorer library is static and linked into the one executable. Each
# backend stays its own target so focused tests link a single format.
qt_add_library(explorer-core STATIC
    src/core/appcontroller.cpp
    src/core/documenttab.cpp
    src/core/formatlist.cpp
    src/core/opensequence.cpp
    src/models/detailmodel.cpp
    src/models/memorymapmodel.cpp
    src/models/plotdata.cpp
    src/models/signalmapmodel.cpp
    src/models/signalplotmodel.cpp
    src/models/tabmodel.cpp
    src/models/treefiltermodel.cpp
    src/models/treemodel.cpp
    src/sessions/adaptersessionbase.cpp
)

qt_add_library(explorer-a2l-backend STATIC
    src/adapters/a2ladapter.cpp
    src/sessions/a2ldocumentsession.cpp
    src/sessions/a2ldetailpresenter.cpp
    src/sessions/a2ldetailpresenter_ifdata.cpp
)

qt_add_library(explorer-dbc-backend STATIC
    src/adapters/dbcadapter.cpp
    src/sessions/dbcdocumentsession.cpp
    src/sessions/dbcdetailpresenter.cpp
)

qt_add_library(explorer-ldf-backend STATIC
    src/adapters/ldfadapter.cpp
    src/sessions/ldfdocumentsession.cpp
    src/sessions/ldfdetailpresenter.cpp
)

qt_add_library(explorer-mdf4-backend STATIC
    src/adapters/mdf4adapter.cpp
    src/sessions/mdf4documentsession.cpp
    src/sessions/mdf4detailpresenter.cpp
)

# The application's format list: the one place concrete adapters are constructed.
qt_add_library(explorer-formats STATIC
    src/builtinformats.cpp
)

qt_add_executable(automotive-format-explorer
    src/main.cpp
    src/ui/memorygriditem.cpp
    src/ui/signalgriditem.cpp
    src/ui/signalplotitem.cpp
    $<$<PLATFORM_ID:Windows>:resources/explorer.rc>
)

target_include_directories(explorer-core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(explorer-core
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::Concurrent
)

target_link_libraries(explorer-a2l-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        a2lparser::a2lparser
)

target_link_libraries(explorer-dbc-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        dbcparser::dbcparser
)

target_link_libraries(explorer-ldf-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        ldfparser::ldfparser
)

target_link_libraries(explorer-mdf4-backend
    PRIVATE
        Qt6::Core
        Qt6::Concurrent
        protobuf::libprotobuf
        explorer-core
        mdf4parser::mdf4parser
)

target_link_libraries(explorer-formats
    PUBLIC
        explorer-core
    PRIVATE
        explorer-a2l-backend
        explorer-dbc-backend
        explorer-ldf-backend
        explorer-mdf4-backend
)

target_link_libraries(automotive-format-explorer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    explorer-formats
    $<$<PLATFORM_ID:Windows>:dwmapi>
)

set_source_files_properties(qml/components/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(automotive-format-explorer
    URI ExplorerApp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/Theme.qml
        qml/components/SplashOverlay.qml
        qml/components/NavPanel.qml
        qml/components/Toast.qml
        qml/components/DiagnosticsPopup.qml
        qml/components/MemoryView.qml
        qml/components/SignalMapView.qml
        qml/components/SignalPlotView.qml
)

qt_add_resources(automotive-format-explorer "icons"
    PREFIX "/"
    FILES
        resources/icons/explorer_16.png
        resources/icons/explorer_24.png
        resources/icons/explorer_32.png
        resources/icons/explorer_48.png
        resources/icons/explorer_64.png
        resources/icons/explorer_96.png
        resources/icons/explorer_128.png
        resources/icons/explorer_256.png
        resources/icons/explorer_512.png
)

set_target_properties(automotive-format-explorer PROPERTIES
    AUTOMOC_COMPILER_PREDEFINES OFF
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

set_target_properties(explorer-core PROPERTIES
    AUTOMOC_COMPILER_PREDEFINES OFF
)

set_target_properties(
    explorer-a2l-backend
    explorer-dbc-backend
    explorer-ldf-backend
    explorer-mdf4-backend
    explorer-formats
    PROPERTIES AUTOMOC OFF
)

# Bundled sample files, staged next to the executable so the "open a sample"
# links resolve in dev builds and in the packaged release alike.
add_custom_command(TARGET automotive-format-explorer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/samples"
        "$<TARGET_FILE_DIR:automotive-format-explorer>/samples"
    COMMENT "Staging sample files"
)

deploy_runtime_deps(automotive-format-explorer)

option(EXPLORER_BUILD_TESTS "Build explorer tests" ON)
if(EXPLORER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
