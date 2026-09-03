# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Out-of-tree devicetree bindings (zephyr,i2c-vpcb). Must precede find_package.
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(adc_sampler)

# Shared sources (target-agnostic)
target_sources(app PRIVATE
    src/main.c
    src/regs.c
    src/cmd_read_regs.c
)

# Target-specific sources
if(CONFIG_APP_TARGET_SIM)
    message(STATUS "Building for SIMULATOR target")
    target_sources(app PRIVATE
        targets/sim/adc_backend.c
        targets/sim/cmd_inject_adc.c
    )
elseif(CONFIG_APP_TARGET_HW)
    message(STATUS "Building for HARDWARE target")
    target_sources(app PRIVATE
        targets/hw/adc_backend.c
    )
elseif(CONFIG_APP_TARGET_VPCB)
    message(STATUS "Building for VIRTUAL PCB target (out-of-process ICs)")
    target_sources(app PRIVATE
        targets/vpcb/adc_backend.c
        targets/vpcb/cmd_dac.c
        drivers/i2c_vpcb.c
    )
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers
    )
    # Host-context half: sockets live here, compiled against host headers.
    target_sources(native_simulator INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/i2c_vpcb_bottom.c
    )
    # The native_simulator INTERFACE target honours COMPILE_OPTIONS but NOT
    # INTERFACE_INCLUDE_DIRECTORIES (see arch/posix/CMakeLists.txt), so include
    # paths for host-context sources have to be passed as -I.
    target_compile_options(native_simulator INTERFACE
        "-I${CMAKE_CURRENT_SOURCE_DIR}/drivers"
        "-I${CMAKE_CURRENT_SOURCE_DIR}/../vpcb/include"
    )
endif()

# The DACx578 driver is not tied to any one target: it talks to whichever I2C
# controller devicetree hands it, so it builds for hardware as readily as for
# the virtual PCB. CONFIG_DAC_DACX578 follows the devicetree, so adding a node
# on any board is enough to pull the driver in.
if(CONFIG_DAC_DACX578)
    target_sources(app PRIVATE drivers/dac_dacx578.c)
endif()

if(NOT CONFIG_APP_TARGET_SIM AND NOT CONFIG_APP_TARGET_HW AND NOT CONFIG_APP_TARGET_VPCB)
    message(WARNING "No target selected! Set CONFIG_APP_TARGET_SIM, CONFIG_APP_TARGET_HW or CONFIG_APP_TARGET_VPCB")
endif()

