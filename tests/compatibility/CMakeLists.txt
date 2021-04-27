# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

cmake_minimum_required(VERSION 3.24)
project(qdbus_sys_installed_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT QDBUS_SYS_ROOT)
    message(FATAL_ERROR "Set QDBUS_SYS_ROOT to a staged installation containing usr/include and usr/lib")
endif()
set(QDBUS_SYS_LIBDIR "usr/lib" CACHE STRING "Library directory relative to the staged root")
set(QDBUS_SYS_INCLUDEDIR "usr/include" CACHE STRING "Header directory relative to the staged root")
find_package(Qt6 REQUIRED COMPONENTS Core Qml DBus)
find_path(QDBUS_SYS_INCLUDE qdbus-sys-version.h
    PATHS "${QDBUS_SYS_ROOT}/${QDBUS_SYS_INCLUDEDIR}" NO_DEFAULT_PATH REQUIRED)
find_library(IWD_LIBRARY NAMES iwdinterface
    PATHS "${QDBUS_SYS_ROOT}/${QDBUS_SYS_LIBDIR}" NO_DEFAULT_PATH REQUIRED)
find_library(NETWORKD_LIBRARY NAMES qsystemd-networkd-interface
    PATHS "${QDBUS_SYS_ROOT}/${QDBUS_SYS_LIBDIR}" NO_DEFAULT_PATH REQUIRED)
find_library(CONNECTIVITY_LIBRARY NAMES qdbus-sys-connectivityd-interface
    PATHS "${QDBUS_SYS_ROOT}/${QDBUS_SYS_LIBDIR}" NO_DEFAULT_PATH REQUIRED)
add_executable(public_api_1_0 1.0/public_api.cpp)
target_include_directories(public_api_1_0 PRIVATE "${QDBUS_SYS_INCLUDE}")
target_link_libraries(public_api_1_0 PRIVATE
    "${IWD_LIBRARY}" "${NETWORKD_LIBRARY}" "${CONNECTIVITY_LIBRARY}"
    Qt6::Core Qt6::Qml Qt6::DBus)
enable_testing()
add_test(NAME installed_public_api_1_0 COMMAND public_api_1_0)
set_tests_properties(installed_public_api_1_0 PROPERTIES
    ENVIRONMENT "DBUS_SYSTEM_BUS_ADDRESS=unix:path=${CMAKE_CURRENT_BINARY_DIR}/no-system-bus")
