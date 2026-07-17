cmake_minimum_required(VERSION 3.16)
project(conwatch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(yaml-cpp REQUIRED)

add_executable(conwatch-tray conwatch-tray.cpp)
target_link_libraries(conwatch-tray PRIVATE Qt6::Widgets)

add_executable(conwatch
    watcher/main.cpp
    watcher/netlink.cpp
    watcher/config.cpp
    watcher/process_manager.cpp
)
target_link_libraries(conwatch PRIVATE yaml-cpp::yaml-cpp)
# Deliberately no Qt linkage here -- the watcher is a lean POSIX daemon
# that spawns the Qt-based conwatch-tray binary as a child process.
