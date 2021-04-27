# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

set(QSYSTEMD_NETWORKD_INTERFACE_HEADERS
    qsystemd-networkd.h
)

qt_add_library(qsystemd_networkd_interface SHARED
    qsystemd-networkd.cpp
    ${QSYSTEMD_NETWORKD_INTERFACE_HEADERS}
)

# The library is the QML module's backing target, so its types have one
# meta-object; the generated plugin only links it.
qt_add_qml_module(qsystemd_networkd_interface
    URI DBusSysSystemdNetworkd
    VERSION 1.0
    PLUGIN_TARGET qsystemd_networkd
    NO_PLUGIN_OPTIONAL
    TYPEINFO plugins.qmltypes
    OUTPUT_DIRECTORY "${QML_DESTDIR_PATH}/DBusSysSystemdNetworkd"
)
qdbus_sys_configure_qml_plugin(qsystemd_networkd DBusSysSystemdNetworkd)

target_include_directories(qsystemd_networkd_interface PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}"
)

set_target_properties(qsystemd_networkd_interface PROPERTIES
    OUTPUT_NAME qsystemd-networkd-interface
)

target_link_libraries(qsystemd_networkd_interface PUBLIC
    qdbus_sys_project_options
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
)

qdbus_sys_apply_default_rpath(qsystemd_networkd_interface)
qdbus_sys_configure_public_library(qsystemd_networkd_interface)
set_target_properties(qsystemd_networkd_interface PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
)

install(TARGETS qsystemd_networkd_interface
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(FILES ${QSYSTEMD_NETWORKD_INTERFACE_HEADERS}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
