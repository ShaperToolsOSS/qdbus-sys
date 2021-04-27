# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

function(read_source relative_path out_var)
    file(READ "${PROJECT_SOURCE_DIR}/${relative_path}" content)
    set(${out_var} "${content}" PARENT_SCOPE)
endfunction()

foreach(relative_path IN ITEMS
    "src/iwd/lib/dbus-interfaces/generated/dbus_properties.h"
    "src/qsystemd-networkd/lib/dbus-interfaces/generated/dbus_properties.h"
    "src/qdbus-sys-connectivityd/lib/dbus-interfaces/generated/dbus_properties.h"
)
    read_source("${relative_path}" content)
    if(NOT content MATCHES "GetAll\\(const QString &interface_name\\)")
        message(FATAL_ERROR "${relative_path} does not expose the one-argument DBus Properties.GetAll signature")
    endif()
    if(content MATCHES "GetAll\\([^\\)]*changed_properties")
        message(FATAL_ERROR "${relative_path} still exposes a bogus changed_properties argument on GetAll")
    endif()
endforeach()

read_source("src/iwd/lib/dbus-interfaces/generated/iwd_objectmanager.h" objectmanager_content)
if(NOT objectmanager_content MATCHES "InterfacesAdded\\(const QDBusObjectPath &object_path, (const )?InterfacePropertyMap &?interfaces_and_properties\\)")
    message(FATAL_ERROR "iwd_objectmanager.h does not expose InterfacesAdded with InterfacePropertyMap")
endif()

read_source("src/qdbus-sys-connectivityd/lib/dbus-interfaces/generated/qdbus_sys_connectivityd.h" qdbus_sys_connectivityd_content)
if(NOT qdbus_sys_connectivityd_content MATCHES "checkConnectivity\\(\\)")
    message(FATAL_ERROR "qdbus_sys_connectivityd.h does not expose checkConnectivity")
endif()

read_source("scripts/generate-code-from-xml-interfaces.sh" generator_content)
if(generator_content MATCHES "shared/dbus-interfaces")
    message(FATAL_ERROR "generate-code-from-xml-interfaces.sh still points at removed shared/dbus-interfaces paths")
endif()
