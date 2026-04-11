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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchParserLib)
include(DeployMsys2Deps)

set(A2L_PARSER_VERSION "v0.2.0" CACHE STRING "a2l-parser-lib release tag")
set(DBC_PARSER_VERSION "v0.2.0" CACHE STRING "dbc-parser-lib release tag")
set(LDF_PARSER_VERSION "v0.3.0" CACHE STRING "ldf-parser-lib release tag")

fetch_parser_lib(
    TARGET  a2lextract
    REPO    dnbmch/a2l-parser-lib
    VERSION "${A2L_PARSER_VERSION}"
    HEADER  a2l/a2lfile.h
)

fetch_parser_lib(
    TARGET  dbcextract
    REPO    dnbmch/dbc-parser-lib
    VERSION "${DBC_PARSER_VERSION}"
    HEADER  dbc/dbcfile.h
)

fetch_parser_lib(
    TARGET  ldfextract
    REPO    dnbmch/ldf-parser-lib
    VERSION "${LDF_PARSER_VERSION}"
    HEADER  ldf/ldffile.h
)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_library(explorer-core SHARED
    src/core/formatregistry.cpp
    src/core/noderegistry.cpp
    src/models/detailmodel.cpp
    src/models/memorymapmodel.cpp
    src/models/signalmapmodel.cpp
    src/models/tabmodel.cpp
    src/models/treemodel.cpp
    src/sessions/adaptersessionbase.cpp
)

qt_add_library(explorer-a2l-backend SHARED
    src/adapters/a2ladapter.cpp
    src/sessions/a2ldocumentsession.cpp
)

qt_add_library(explorer-dbc-backend SHARED
    src/adapters/dbcadapter.cpp
    src/sessions/dbcdocumentsession.cpp
)

qt_add_library(explorer-ldf-backend SHARED
    src/adapters/ldfadapter.cpp
    src/sessions/ldfdocumentsession.cpp
)

qt_add_executable(automotive-format-explorer
    src/main.cpp
    src/core/appcontroller.cpp
    src/ui/memorygriditem.cpp
    src/ui/signalgriditem.cpp
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

target_include_directories(automotive-format-explorer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(explorer-core
    PUBLIC
        Qt6::Core
)

target_link_libraries(explorer-a2l-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        a2lextract
)

target_link_libraries(explorer-dbc-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        dbcextract
)

target_link_libraries(explorer-ldf-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        ldfextract
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
endif()

add_dependencies(automotive-format-explorer
    explorer-a2l-backend
    explorer-dbc-backend
    explorer-ldf-backend
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
        qml/components/MemoryView.qml
        qml/components/SignalMapView.qml
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

deploy_msys2_deps(automotive-format-explorer)
