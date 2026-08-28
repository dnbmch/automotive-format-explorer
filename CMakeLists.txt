cmake_minimum_required(VERSION 3.21)
project(automotive_format_explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Concurrent Gui Qml Quick QuickControls2 QuickDialogs2)
find_package(Protobuf CONFIG QUIET)
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
endif()
find_package(ZLIB REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchParserLib)
include(DeployRuntimeDeps)

# Parser -lib release tags. Bumped in lockstep with the re-cut republish releases
# (the proto contracts changed). Override at configure time if the operator cuts
# different tags.
set(A2L_PARSER_VERSION "v0.4.0" CACHE STRING "a2l-parser-lib release tag")
set(DBC_PARSER_VERSION "v0.4.0" CACHE STRING "dbc-parser-lib release tag")
set(LDF_PARSER_VERSION "v0.5.0" CACHE STRING "ldf-parser-lib release tag")
set(MDF4_PARSER_VERSION "v0.1.0" CACHE STRING "mdf4-parser-lib release tag")

# Integrity pins for the fetched -lib release assets, from the published tarballs:
#   gh release download <tag> --repo dnbmch/a2l-parser-lib \
#     --pattern 'a2lparser-headers-<tag>.tar.gz' && sha256sum a2lparser-headers-<tag>.tar.gz
# The headers archive hash is platform-independent and pinned to the tags above;
# refill whenever the version pins move. The binary archive hash is per-platform,
# so BINARY_SHA256 stays empty (no check) unless you pin it at configure time for
# the platform you build on.
set(A2L_HEADERS_SHA256 "d4b1950a00d40cfef7135bf556e1fec59f0dc5eff6a5a04420f25caeb78a1f36" CACHE STRING "sha256 of a2lparser-headers-<tag>.tar.gz")
set(A2L_BINARY_SHA256  "" CACHE STRING "sha256 of the a2lparser binary archive for this platform")
set(DBC_HEADERS_SHA256 "0db8b48014358aa3f77adff2c8b2708a70d3f15948598f9f65c485ed1c30537d" CACHE STRING "sha256 of dbcparser-headers-<tag>.tar.gz")
set(DBC_BINARY_SHA256  "" CACHE STRING "sha256 of the dbcparser binary archive for this platform")
set(LDF_HEADERS_SHA256 "c6e0cfce2d98954003f315bab0ff0b5df9b77df5a1dedcf78f4bcba96d7bb1d1" CACHE STRING "sha256 of ldfparser-headers-<tag>.tar.gz")
set(LDF_BINARY_SHA256  "" CACHE STRING "sha256 of the ldfparser binary archive for this platform")
set(MDF4_HEADERS_SHA256 "6103a8e0b26113c5943b1885b8190ba48e5c656e39080eff8f6292bce01fe27a" CACHE STRING "sha256 of mdf4parser-headers-<tag>.tar.gz")
set(MDF4_BINARY_SHA256  "" CACHE STRING "sha256 of the mdf4parser binary archive for this platform")

fetch_parser_lib(
    TARGET  a2lparser
    REPO    dnbmch/a2l-parser-lib
    VERSION "${A2L_PARSER_VERSION}"
    HEADER  a2l/a2lfile.h
    HEADERS_SHA256 "${A2L_HEADERS_SHA256}"
    BINARY_SHA256  "${A2L_BINARY_SHA256}"
)

fetch_parser_lib(
    TARGET  dbcparser
    REPO    dnbmch/dbc-parser-lib
    VERSION "${DBC_PARSER_VERSION}"
    HEADER  dbc/dbcfile.h
    HEADERS_SHA256 "${DBC_HEADERS_SHA256}"
    BINARY_SHA256  "${DBC_BINARY_SHA256}"
)

fetch_parser_lib(
    TARGET  ldfparser
    REPO    dnbmch/ldf-parser-lib
    VERSION "${LDF_PARSER_VERSION}"
    HEADER  ldf/ldffile.h
    HEADERS_SHA256 "${LDF_HEADERS_SHA256}"
    BINARY_SHA256  "${LDF_BINARY_SHA256}"
)

fetch_parser_lib(
    TARGET  mdf4parser
    REPO    dnbmch/mdf4-parser-lib
    VERSION "${MDF4_PARSER_VERSION}"
    HEADER  mdf4/extract.h
    HEADERS_SHA256 "${MDF4_HEADERS_SHA256}"
    BINARY_SHA256  "${MDF4_BINARY_SHA256}"
    LINK_LIBRARIES ZLIB::ZLIB
)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_library(explorer-core SHARED
    src/core/formatregistry.cpp
    src/core/noderegistry.cpp
    src/models/detailmodel.cpp
    src/models/memorymapmodel.cpp
    src/models/signalmapmodel.cpp
    src/models/signalplotmodel.cpp
    src/models/tabmodel.cpp
    src/models/treefiltermodel.cpp
    src/models/treemodel.cpp
    src/sessions/adaptersessionbase.cpp
)

if(WIN32)
    set(_backend_lib_type SHARED)
else()
    set(_backend_lib_type STATIC)
endif()

qt_add_library(explorer-a2l-backend ${_backend_lib_type}
    src/adapters/a2ladapter.cpp
    src/sessions/a2ldocumentsession.cpp
    src/sessions/a2ldetailpresenter.cpp
    src/sessions/a2ldetailpresenter_ifdata.cpp
)

qt_add_library(explorer-dbc-backend ${_backend_lib_type}
    src/adapters/dbcadapter.cpp
    src/sessions/dbcdocumentsession.cpp
    src/sessions/dbcdetailpresenter.cpp
)

qt_add_library(explorer-ldf-backend ${_backend_lib_type}
    src/adapters/ldfadapter.cpp
    src/sessions/ldfdocumentsession.cpp
    src/sessions/ldfdetailpresenter.cpp
)

qt_add_library(explorer-mdf4-backend ${_backend_lib_type}
    src/adapters/mdf4adapter.cpp
    src/sessions/mdf4documentsession.cpp
    src/sessions/mdf4detailpresenter.cpp
)

qt_add_executable(automotive-format-explorer
    src/main.cpp
    src/core/appcontroller.cpp
    src/ui/memorygriditem.cpp
    src/ui/signalgriditem.cpp
    src/ui/signalplotitem.cpp
    $<$<PLATFORM_ID:Windows>:resources/explorer.rc>
)

target_include_directories(explorer-core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(explorer-a2l-backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(explorer-dbc-backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(explorer-ldf-backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(explorer-mdf4-backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(automotive-format-explorer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(explorer-core
    PUBLIC
        Qt6::Core
)

target_compile_definitions(explorer-core PRIVATE EXPLORER_CORE_LIBRARY)

target_link_libraries(explorer-a2l-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        a2lparser
)

target_link_libraries(explorer-dbc-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        dbcparser
)

target_link_libraries(explorer-ldf-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        ldfparser
)

target_link_libraries(explorer-mdf4-backend
    PRIVATE
        Qt6::Core
        Qt6::Concurrent
        protobuf::libprotobuf
        explorer-core
        mdf4parser
)

target_link_libraries(automotive-format-explorer PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    explorer-core
)

if(WIN32)
    target_link_libraries(automotive-format-explorer PRIVATE dwmapi)
    # On Windows, backends are shared libraries loaded at runtime via QLibrary.
    add_dependencies(automotive-format-explorer
        explorer-a2l-backend
        explorer-dbc-backend
        explorer-ldf-backend
        explorer-mdf4-backend
    )
else()
    # On Linux, backends are static — link directly and register at startup.
    target_link_libraries(automotive-format-explorer PRIVATE
        explorer-a2l-backend
        explorer-dbc-backend
        explorer-ldf-backend
        explorer-mdf4-backend
    )
    target_compile_definitions(automotive-format-explorer PRIVATE BACKENDS_STATIC)
endif()

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
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

set_target_properties(explorer-a2l-backend PROPERTIES
    AUTOMOC OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

set_target_properties(explorer-dbc-backend PROPERTIES
    AUTOMOC OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

set_target_properties(explorer-ldf-backend PROPERTIES
    AUTOMOC OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

set_target_properties(explorer-mdf4-backend PROPERTIES
    AUTOMOC OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Bundled sample files, staged next to the executable so the "open a sample"
# links resolve in dev builds and in the packaged release alike.
add_custom_command(TARGET automotive-format-explorer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/samples"
        "$<TARGET_FILE_DIR:automotive-format-explorer>/samples"
    COMMENT "Staging sample files"
)

deploy_runtime_deps(automotive-format-explorer
    explorer-core
    explorer-a2l-backend
    explorer-dbc-backend
    explorer-ldf-backend
    explorer-mdf4-backend
)

enable_testing()
add_subdirectory(tests)
