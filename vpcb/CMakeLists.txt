# SPDX-License-Identifier: Apache-2.0
#
# Host build for the virtual PCB. Deliberately independent of Zephyr:
# no ZEPHYR_BASE, no west, no devicetree. These binaries are plain C and
# are reusable against any MCU or RTOS.
cmake_minimum_required(VERSION 3.20)
project(vpcb C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -g -O1)

add_executable(vpcb-board   board/main.c)
add_executable(vpcb-dac7578 ics/dac7578/main.c)

foreach(t vpcb-board vpcb-dac7578)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endforeach()

add_executable(vpcb-fake-mcu tools/fake_mcu.c)
target_include_directories(vpcb-fake-mcu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
