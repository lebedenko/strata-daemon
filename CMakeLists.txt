cmake_minimum_required(VERSION 3.25)
project(strata-daemon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_SANITIZERS "Enable Address and Undefined sanitizers" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Compiler warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

find_package(nlohmann_json 3.2.0 REQUIRED)

# Common internal library
add_library(strata_common STATIC
    src/common/logger.cpp
    src/cache/keymap_cache.cpp
    src/device/zmk_raw_hid_device.cpp
    src/device/device_manager.cpp
    src/ipc/dbus_server.cpp
)

target_include_directories(strata_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(strata_common PUBLIC
    PkgConfig::SYSTEMD
    PkgConfig::UDEV
    nlohmann_json::nlohmann_json
)

# stratad daemon executable
add_executable(stratad
    src/daemon_main.cpp
)

target_link_libraries(stratad PRIVATE
    strata_common
)

# strata-cli client executable
add_executable(strata-cli
    src/cli_main.cpp
)

target_include_directories(strata-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(strata-cli PRIVATE
    PkgConfig::SYSTEMD
    nlohmann_json::nlohmann_json
)

# Testing
enable_testing()

add_executable(test_protocol
    tests/test_protocol.cpp
)
target_include_directories(test_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(test_cache
    tests/test_cache.cpp
)
target_link_libraries(test_cache PRIVATE
    strata_common
)

add_test(NAME test_protocol COMMAND test_protocol)
add_test(NAME test_cache COMMAND test_cache)

# Installation rules
include(GNUInstallDirs)

install(TARGETS stratad strata-cli
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES systemd/stratad.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user
)

install(FILES systemd/org.freedesktop.Strata.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)

install(FILES udev/99-strata.rules
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d
)
