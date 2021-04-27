# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

qt_add_executable(qdbus_sys_connectivityd_tester
    main.cpp
)

target_link_libraries(qdbus_sys_connectivityd_tester PRIVATE
    qdbus_sys_project_options
    qdbus_sys_connectivityd_interface
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
)

set_target_properties(qdbus_sys_connectivityd_tester PROPERTIES
    OUTPUT_NAME qdbus-sys-connectivityd-tester
)

qdbus_sys_apply_default_rpath(qdbus_sys_connectivityd_tester)

install(TARGETS qdbus_sys_connectivityd_tester
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
