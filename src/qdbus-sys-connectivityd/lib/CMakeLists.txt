# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

set(QDBUS_SYS_CONNECTIVITYD_INTERFACE_HEADERS
    qdbus-sys-connectivityd.h
)

qt_add_library(qdbus_sys_connectivityd_interface SHARED
    qdbus-sys-connectivityd.cpp
    ${QDBUS_SYS_CONNECTIVITYD_INTERFACE_HEADERS}
)

# The library is the QML module's backing target, so its types have one
# meta-object; the generated plugin only links it.
qt_add_qml_module(qdbus_sys_connectivityd_interface
    URI DBusSysConnectivity
    VERSION 1.0
    PLUGIN_TARGET qdbus_sys_connectivityd
    NO_PLUGIN_OPTIONAL
    TYPEINFO plugins.qmltypes
    OUTPUT_DIRECTORY "${QML_DESTDIR_PATH}/DBusSysConnectivity"
)
qdbus_sys_configure_qml_plugin(qdbus_sys_connectivityd DBusSysConnectivity)

target_include_directories(qdbus_sys_connectivityd_interface PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}"
)

set_target_properties(qdbus_sys_connectivityd_interface PROPERTIES
    OUTPUT_NAME qdbus-sys-connectivityd-interface
)

target_link_libraries(qdbus_sys_connectivityd_interface PUBLIC
    qdbus_sys_project_options
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
)

qdbus_sys_apply_default_rpath(qdbus_sys_connectivityd_interface)
qdbus_sys_configure_public_library(qdbus_sys_connectivityd_interface)
set_target_properties(qdbus_sys_connectivityd_interface PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
)

install(TARGETS qdbus_sys_connectivityd_interface
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(FILES ${QDBUS_SYS_CONNECTIVITYD_INTERFACE_HEADERS}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
