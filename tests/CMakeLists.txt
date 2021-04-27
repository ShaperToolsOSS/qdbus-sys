# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

add_test(NAME dbus_source_contracts
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/dbus_source_contracts.cmake"
)

find_package(Qt6 REQUIRED COMPONENTS Test Network Xml)

add_executable(version_test version_test.cpp)
target_include_directories(version_test PRIVATE "${QDBUS_SYS_GENERATED_INCLUDE_DIR}")
target_compile_definitions(version_test PRIVATE
    EXPECTED_MAJOR=${PROJECT_VERSION_MAJOR}
    EXPECTED_MINOR=${PROJECT_VERSION_MINOR}
    EXPECTED_PATCH=${PROJECT_VERSION_PATCH}
    EXPECTED_VERSION="${PROJECT_VERSION}")
add_test(NAME version_test COMMAND version_test)

# This application source is frozen at the initial supported API release.
if(PROJECT_VERSION_MAJOR EQUAL 1)
    add_executable(public_api_1_0_test compatibility/1.0/public_api.cpp)
    target_link_libraries(public_api_1_0_test PRIVATE
        iwdinterface qsystemd_networkd_interface qdbus_sys_connectivityd_interface)
    add_test(NAME public_api_1_0_test COMMAND public_api_1_0_test)
    set_tests_properties(public_api_1_0_test PROPERTIES
        ENVIRONMENT "DBUS_SYSTEM_BUS_ADDRESS=unix:path=${CMAKE_CURRENT_BINARY_DIR}/no-system-bus")
endif()

add_executable(networkd_state_test networkd_state_test.cpp)
target_link_libraries(networkd_state_test PRIVATE qsystemd_networkd_interface Qt6::Test)
add_dependencies(networkd_state_test qsystemd_networkd)

add_executable(connectivity_client_test connectivity_client_test.cpp)
target_link_libraries(connectivity_client_test PRIVATE qdbus_sys_connectivityd_interface Qt6::Test)
add_dependencies(connectivity_client_test qdbus_sys_connectivityd)

add_executable(iwd_model_test iwd_model_test.cpp)
target_link_libraries(iwd_model_test PRIVATE iwdinterface iwdbackend Qt6::Test)
add_dependencies(iwd_model_test iwd)

set(CONNECTIVITY_DAEMON_DIR "${PROJECT_SOURCE_DIR}/src/qdbus-sys-connectivityd/qdbus-sys-connectivityd")
add_executable(connectivity_daemon_test
    connectivity_daemon_test.cpp
    "${CONNECTIVITY_DAEMON_DIR}/qdbus-sys-connectivityd-daemon.cpp"
    "${CONNECTIVITY_DAEMON_DIR}/qdbus-sys-connectivityd-daemon.h"
)
target_include_directories(connectivity_daemon_test PRIVATE
    "${CONNECTIVITY_DAEMON_DIR}"
    "${PROJECT_SOURCE_DIR}/src/qdbus-sys-connectivityd/lib"
)
target_link_libraries(connectivity_daemon_test PRIVATE qdbus_sys_connectivityd_interface qsystemd_networkd_interface Qt6::Network Qt6::Test)

foreach(test_target IN ITEMS networkd_state_test connectivity_client_test iwd_model_test connectivity_daemon_test)
    add_test(NAME "${test_target}" COMMAND "${test_target}")
    # Keep tests independent of host services, credentials, and network devices.
    set_tests_properties("${test_target}" PROPERTIES
        ENVIRONMENT "DBUS_SYSTEM_BUS_ADDRESS=unix:path=${CMAKE_CURRENT_BINARY_DIR}/no-system-bus"
    )
endforeach()

set_property(TEST iwd_model_test APPEND PROPERTY ENVIRONMENT "QML_IMPORT_PATH=${QML_DESTDIR_PATH}")
set_property(TEST networkd_state_test APPEND PROPERTY ENVIRONMENT "QML_IMPORT_PATH=${QML_DESTDIR_PATH}")
set_property(TEST connectivity_client_test APPEND PROPERTY ENVIRONMENT "QML_IMPORT_PATH=${QML_DESTDIR_PATH}")

find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_executable(iwd_lifecycle_test iwd_lifecycle_test.cpp mock_iwd.h)
    target_link_libraries(iwd_lifecycle_test PRIVATE iwdbackend Qt6::Test Qt6::Xml)
    add_test(NAME iwd_lifecycle_test
        COMMAND "${DBUS_RUN_SESSION}" -- sh -c
            "DBUS_SYSTEM_BUS_ADDRESS=\"$DBUS_SESSION_BUS_ADDRESS\" exec \"$1\""
            sh "$<TARGET_FILE:iwd_lifecycle_test>"
    )
    set_tests_properties(iwd_lifecycle_test PROPERTIES TIMEOUT 45)

    add_executable(iwd_broker_test iwd_broker_test.cpp mock_iwd.h)
    target_link_libraries(iwd_broker_test PRIVATE iwdinterface Qt6::Test)
    target_compile_definitions(iwd_broker_test PRIVATE QIWD_EXECUTABLE_PATH="$<TARGET_FILE:qdbus-sys-iwd>")
    add_dependencies(iwd_broker_test qdbus-sys-iwd)
    add_test(NAME iwd_broker_test
        COMMAND "${DBUS_RUN_SESSION}" -- sh -c
            "DBUS_SYSTEM_BUS_ADDRESS=\"$DBUS_SESSION_BUS_ADDRESS\" exec \"$1\""
            sh "$<TARGET_FILE:iwd_broker_test>"
    )
    set_tests_properties(iwd_broker_test PROPERTIES TIMEOUT 60)
endif()

find_program(DBUS_DAEMON dbus-daemon)
if(DBUS_DAEMON)
    add_executable(connectivity_activation_service connectivity_activation_service.cpp)
    target_link_libraries(connectivity_activation_service PRIVATE Qt6::Core Qt6::DBus)
    add_executable(connectivity_activation_test connectivity_activation_test.cpp)
    target_link_libraries(connectivity_activation_test PRIVATE qdbus_sys_connectivityd_interface Qt6::Test)
    target_compile_definitions(connectivity_activation_test PRIVATE
        DBUS_DAEMON_PATH="${DBUS_DAEMON}"
        ACTIVATION_MANIFEST_PATH="${PROJECT_BINARY_DIR}/src/qdbus-sys-connectivityd/qdbus-sys-connectivityd/com.shapertools.qdbussys.connectivityd.service"
    )
    add_dependencies(connectivity_activation_test connectivity_activation_service)
    add_test(NAME connectivity_activation_test COMMAND connectivity_activation_test)
    set_tests_properties(connectivity_activation_test PROPERTIES TIMEOUT 30)
endif()
