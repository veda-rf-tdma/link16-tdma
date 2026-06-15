cmake_minimum_required(VERSION 3.16)
project(tdma_radio C)

set(CMAKE_C_STANDARD 99)
include_directories(include)

add_library(tdma_common
    common/cc1101_regs.c
    common/radio_metrics.c
    common/protocol.c
    common/tdma.c
    common/tdma_runtime.c
    common/node_table.c
    common/ekf.c
)
if(UNIX)
    target_link_libraries(tdma_common m)
endif()

add_executable(tdma_desktop
    desktop_win/main_desktop.c
    desktop_win/stm32_bridge_link.c
    desktop_win/serial_win.c
)
target_link_libraries(tdma_desktop tdma_common)



add_executable(tdma_ekf_tracker
    analysis/ekf_tracker.c
)
target_link_libraries(tdma_ekf_tracker tdma_common)
if(UNIX)
    target_link_libraries(tdma_ekf_tracker m)
endif()
