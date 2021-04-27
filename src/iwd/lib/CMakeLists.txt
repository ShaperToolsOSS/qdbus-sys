# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

set(IWD_INTERFACE_HEADERS
    iwd-model.h
    iwd.h
)

set(IWD_PRIVATE_HEADERS
    iwd-ipc_p.h
)

qt_add_library(iwdinterface SHARED
    iwd-model.cpp
    iwd.cpp
    ${IWD_INTERFACE_HEADERS}
    ${IWD_PRIVATE_HEADERS}
)

# The library is the QML module's backing target, so its types have one
# meta-object; the generated plugin only links it.
qt_add_qml_module(iwdinterface
    URI DBusSysIWD
    VERSION 1.0
    PLUGIN_TARGET iwd
    NO_PLUGIN_OPTIONAL
    TYPEINFO plugins.qmltypes
    OUTPUT_DIRECTORY "${QML_DESTDIR_PATH}/DBusSysIWD"
)
qdbus_sys_configure_qml_plugin(iwd DBusSysIWD)

target_include_directories(iwdinterface PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${QDBUS_SYS_GENERATED_INCLUDE_DIR}"
)

target_link_libraries(iwdinterface PUBLIC
    qdbus_sys_project_options
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
)

qdbus_sys_apply_default_rpath(iwdinterface)
qdbus_sys_configure_public_library(iwdinterface)
set_target_properties(iwdinterface PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
)

install(TARGETS iwdinterface
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(FILES ${IWD_INTERFACE_HEADERS}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
