# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

set(IWD_UI_SCRIPT "${APP_DESTDIR_PATH}/iwd-ui")

add_custom_command(
    OUTPUT "${IWD_UI_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_DESTDIR_PATH}"
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/generate-iwd-ui.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/iwd-ui.qml"
        "${IWD_UI_SCRIPT}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/generate-iwd-ui.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/iwd-ui.qml"
    VERBATIM
)

add_custom_target(iwd-ui ALL
    DEPENDS "${IWD_UI_SCRIPT}"
)

install(PROGRAMS "${IWD_UI_SCRIPT}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
