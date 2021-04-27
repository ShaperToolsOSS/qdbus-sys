# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

add_library(iwdbackend STATIC
    iwd-controller.cpp
    iwd-controller.h
    ../lib/agents.cpp
    ../lib/agents.h
    ../lib/dbus-interface-common.h
)
target_include_directories(iwdbackend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
)
target_link_libraries(iwdbackend PUBLIC Qt6::Core Qt6::DBus)
target_link_libraries(iwdbackend PRIVATE qdbus_sys_project_options)

add_executable(qdbus-sys-iwd main.cpp iwd-daemon.cpp iwd-daemon.h)
target_link_libraries(qdbus-sys-iwd PRIVATE iwdbackend qdbus_sys_project_options)
qdbus_sys_apply_default_rpath(qdbus-sys-iwd)
set(QIWD_EXECUTABLE "${CMAKE_INSTALL_FULL_LIBEXECDIR}/qdbus-sys-iwd")
configure_file(resources/qdbus-sys-iwd.service.in qdbus-sys-iwd.service @ONLY)
configure_file(resources/com.shapertools.qdbussys.iwd.service.in com.shapertools.qdbussys.iwd.service @ONLY)
install(TARGETS qdbus-sys-iwd RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qdbus-sys-iwd.service" DESTINATION lib/systemd/system)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/com.shapertools.qdbussys.iwd.service" DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services)
install(FILES resources/com.shapertools.qdbussys.iwd.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
