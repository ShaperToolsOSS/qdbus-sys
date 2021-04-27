# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

qt_add_executable(iwd-ui-app
    main.cpp
    iwd-client.qrc
)

target_include_directories(iwd-ui-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(iwd-ui-app PRIVATE
    qdbus_sys_project_options
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
)

qdbus_sys_apply_default_rpath(iwd-ui-app)

install(TARGETS iwd-ui-app
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
