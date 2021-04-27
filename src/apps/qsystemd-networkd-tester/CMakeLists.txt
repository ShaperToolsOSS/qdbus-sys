# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

qt_add_executable(qsystemd_networkd_tester
    main.cpp
)

target_link_libraries(qsystemd_networkd_tester PRIVATE
    qdbus_sys_project_options
    qsystemd_networkd_interface
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
)

set_target_properties(qsystemd_networkd_tester PROPERTIES
    OUTPUT_NAME qsystemd-networkd-tester
)

qdbus_sys_apply_default_rpath(qsystemd_networkd_tester)

install(TARGETS qsystemd_networkd_tester
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
