# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

cmake_minimum_required(VERSION 3.24)

project(qdbus-sys VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Install into the standard system prefix unless explicitly overridden.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()
include(GNUInstallDirs)

set(QDBUS_SYS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file(cmake/qdbus-sys-version.h.in
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}/qdbus-sys-version.h" @ONLY)
configure_file(cmake/qdbus-sys-global.h
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}/qdbus-sys-global.h" COPYONLY)
install(FILES
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}/qdbus-sys-version.h"
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}/qdbus-sys-global.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/qdbus-sys"
)

set(APP_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}")
set(LIB_INSTALL_PATH "${CMAKE_INSTALL_FULL_LIBDIR}")
set(INCLUDE_INSTALL_PATH "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
set(QML_INSTALL_PATH "${LIB_INSTALL_PATH}/qml")

set(APP_DESTDIR_PATH "${CMAKE_BINARY_DIR}${APP_INSTALL_PATH}")
set(LIB_DESTDIR_PATH "${CMAKE_BINARY_DIR}${LIB_INSTALL_PATH}")
set(INCLUDE_DESTDIR_PATH "${CMAKE_BINARY_DIR}${INCLUDE_INSTALL_PATH}")
set(QML_DESTDIR_PATH "${CMAKE_BINARY_DIR}${QML_INSTALL_PATH}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${APP_DESTDIR_PATH}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${LIB_DESTDIR_PATH}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${LIB_DESTDIR_PATH}")
set(QT_QML_OUTPUT_DIRECTORY "${QML_DESTDIR_PATH}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
qt_policy(SET QTP0001 NEW)
qt_standard_project_setup()

include(CTest)

add_library(qdbus_sys_project_options INTERFACE)

target_compile_definitions(qdbus_sys_project_options INTERFACE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
)

target_compile_options(qdbus_sys_project_options INTERFACE
    -Wall
    -Wextra
    -fno-omit-frame-pointer
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(qdbus_sys_project_options INTERFACE
        -Wno-unknown-pragmas
        -Wno-unused-private-field
        -Wno-mismatched-tags
        -Wno-deprecated-copy
    )
    option(QDBUS_SYS_USE_LLD "Link with lld when building with Clang" OFF)
    if(QDBUS_SYS_USE_LLD)
        target_link_options(qdbus_sys_project_options INTERFACE -fuse-ld=lld)
    endif()
else()
    target_compile_options(qdbus_sys_project_options INTERFACE
        -Wno-unknown-pragmas
        -Wno-psabi
        -Wno-error=maybe-uninitialized
        -Wno-error=deprecated-declarations
        -Wno-error=unused-but-set-variable
        -Wno-error=deprecated-copy
        -Wno-error=redundant-move
    )
endif()

# ARM builds target devices, whose binaries use installed library paths only.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" QDBUS_SYS_SYSTEM_PROCESSOR)
set(QDBUS_SYS_ARM_DEVICE FALSE)
if(QDBUS_SYS_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|^arm|armv[0-9]")
    set(QDBUS_SYS_ARM_DEVICE TRUE)
endif()

if(DEFINED ENV{QT_SYSROOT} AND NOT "$ENV{QT_SYSROOT}" STREQUAL "")
    set(QDBUS_SYS_SYSROOT_LIB_PATH "$ENV{QT_SYSROOT}${LIB_INSTALL_PATH}")
else()
    set(QDBUS_SYS_SYSROOT_LIB_PATH "${LIB_INSTALL_PATH}")
endif()

function(qdbus_sys_apply_default_rpath target)
    if(QDBUS_SYS_ARM_DEVICE)
        set(_build_rpath "${LIB_INSTALL_PATH}")
    else()
        set(_build_rpath "${LIB_DESTDIR_PATH};${QDBUS_SYS_SYSROOT_LIB_PATH}")
    endif()

    set_target_properties(${target} PROPERTIES
        BUILD_RPATH "${_build_rpath}"
        INSTALL_RPATH "${LIB_INSTALL_PATH}"
    )
endfunction()

# Public libraries export only the classes marked QDBUS_SYS_EXPORT.
function(qdbus_sys_configure_public_library target)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_definitions(${target} PRIVATE QDBUS_SYS_BUILD_LIBRARY)
    set(_symbol_map "${PROJECT_SOURCE_DIR}/cmake/qdbus-sys.map")
    target_link_options(${target} PRIVATE "LINKER:--version-script=${_symbol_map}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_symbol_map}")
endfunction()

# Places a generated QML plugin in its module directory and installs it with
# the module's qmldir and type information. The plugin finds its backing
# library in the library directory two levels up.
function(qdbus_sys_configure_qml_plugin plugin_target module_uri)
    set(_module_build_dir "${QML_DESTDIR_PATH}/${module_uri}")

    set_target_properties(${plugin_target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_module_build_dir}"
        BUILD_RPATH "\$ORIGIN/../.."
        INSTALL_RPATH "\$ORIGIN/../.."
    )
    target_link_libraries(${plugin_target} PRIVATE qdbus_sys_project_options)
    set_target_properties(${plugin_target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    set(_plugin_map "${PROJECT_SOURCE_DIR}/cmake/qdbus-sys-plugin.map")
    target_link_options(${plugin_target} PRIVATE "LINKER:--version-script=${_plugin_map}")
    set_property(TARGET ${plugin_target} APPEND PROPERTY LINK_DEPENDS "${_plugin_map}")

    install(TARGETS ${plugin_target}
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qml/${module_uri}"
    )

    install(FILES
        "${_module_build_dir}/qmldir"
        "${_module_build_dir}/plugins.qmltypes"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/qml/${module_uri}"
    )
endfunction()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(COMMAND qt_finalize_project)
    qt_finalize_project()
endif()
