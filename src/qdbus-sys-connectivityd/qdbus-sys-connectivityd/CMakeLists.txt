# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

find_package(Qt6 REQUIRED COMPONENTS Core DBus Network)

add_executable(qdbus-sys-connectivityd
    main.cpp
    qdbus-sys-connectivityd-daemon.cpp
    qdbus-sys-connectivityd-daemon.h
)

target_include_directories(qdbus-sys-connectivityd PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../qsystemd-networkd/lib"
)

target_compile_definitions(qdbus-sys-connectivityd PRIVATE
    QT_DEPRECATED_WARNINGS
)

target_link_libraries(qdbus-sys-connectivityd PRIVATE
    qdbus_sys_project_options
    qsystemd_networkd_interface
    qdbus_sys_connectivityd_interface
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Qt${QT_VERSION_MAJOR}::Network
)

qdbus_sys_apply_default_rpath(qdbus-sys-connectivityd)

set(CONNECTIVITY_EXECUTABLE "${CMAKE_INSTALL_FULL_LIBEXECDIR}/qdbus-sys-connectivityd")
set(CONNECTIVITY_LAUNCHER "${CMAKE_INSTALL_FULL_LIBEXECDIR}/qdbus-sys-connectivityd.sh")
foreach(resource IN ITEMS qdbus-sys-connectivityd.service com.shapertools.qdbussys.connectivityd.service qdbus-sys-connectivityd.sh)
    configure_file("resources/${resource}.in" "${resource}" @ONLY)
endforeach()

install(TARGETS qdbus-sys-connectivityd
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}"
)
install(FILES resources/qdbus-sys-connectivityd.conf
    DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/qdbus-sys"
    RENAME connectivityd.conf.example
)
install(FILES resources/com.shapertools.qdbussys.connectivityd.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/system.d"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/com.shapertools.qdbussys.connectivityd.service"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/system-services"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qdbus-sys-connectivityd.service"
    DESTINATION lib/systemd/system
)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/qdbus-sys-connectivityd.sh"
    DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}"
)
