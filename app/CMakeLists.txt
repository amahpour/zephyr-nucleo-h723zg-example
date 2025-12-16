# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
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
else()
    message(WARNING "No target selected! Set CONFIG_APP_TARGET_SIM or CONFIG_APP_TARGET_HW")
endif()

