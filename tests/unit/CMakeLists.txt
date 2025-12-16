# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(unit_tests)

# Include the source code we're testing
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src
)

# Source files to test
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/regs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test_regs.c
)

