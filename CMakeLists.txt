cmake_minimum_required(VERSION 3.16)
project(conwatch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(yaml-cpp REQUIRED)

add_executable(conwatch-tray
    src/conwatch-tray/conwatch-tray.cpp
    src/conwatch-tray/ping_monitor.cpp
    src/conwatch-tray/ping_monitor_addr.cpp
    src/conwatch-tray/ping_monitor_socket4.cpp
    src/conwatch-tray/ping_monitor_socket6.cpp
    src/conwatch-tray/ping_monitor_tick.cpp
    src/conwatch-tray/ping_monitor_status_text.cpp
    src/conwatch-tray/ping_monitor_icon.cpp
    src/shared/target_resolve.cpp
    src/shared/gateway_resolver.cpp
    src/shared/gateway_override.cpp
)
target_link_libraries(conwatch-tray PRIVATE Qt6::Widgets)

add_executable(conwatch
    src/watcher/main.cpp
    src/watcher/watcher.cpp
    src/watcher/watcher_iface_state.cpp
    src/watcher/netlink.cpp
    src/watcher/config.cpp
    src/watcher/config_load.cpp
    src/watcher/config_resolve.cpp
    src/watcher/process_manager.cpp
)
target_link_libraries(conwatch PRIVATE yaml-cpp::yaml-cpp)
# Deliberately no Qt linkage here -- the watcher is a lean POSIX daemon
# that spawns the Qt-based conwatch-tray binary as a child process.

option(CONWATCH_BUILD_TESTS "Build the gtest unit test suite" OFF)
if(CONWATCH_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(conwatch_tests
        tests/config_test.cpp
        tests/target_resolve_test.cpp
        tests/gateway_resolve_test.cpp
        tests/gateway_override_test.cpp
        src/watcher/config.cpp
        src/watcher/config_load.cpp
        src/watcher/config_resolve.cpp
        src/shared/target_resolve.cpp
        src/shared/gateway_resolver.cpp
        src/shared/gateway_override.cpp
    )
    target_link_libraries(conwatch_tests PRIVATE yaml-cpp::yaml-cpp GTest::gtest GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(conwatch_tests)
endif()
