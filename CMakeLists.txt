cmake_minimum_required(VERSION 3.16)
project(ot-jn5169 VERSION 0.1.0 LANGUAGES C CXX ASM)

set(NXP_SDK_ROOT "" CACHE PATH "Путь к JN516x IEEE 802.15.4 SDK (JN-SW-4163)")
if(NOT NXP_SDK_ROOT)
    message(FATAL_ERROR "Укажите -DNXP_SDK_ROOT=<путь к JN-SW-4163>")
endif()

# RCP-only конфигурация OpenThread
set(OT_PLATFORM "external" CACHE STRING "" FORCE)
set(OT_RCP ON  CACHE BOOL "" FORCE)
set(OT_FTD OFF CACHE BOOL "" FORCE)
set(OT_MTD OFF CACHE BOOL "" FORCE)
set(OT_APP_CLI OFF CACHE BOOL "" FORCE)
set(OT_APP_NCP ON  CACHE BOOL "" FORCE)
set(OT_APP_RCP ON  CACHE BOOL "" FORCE)
set(OT_COMPILE_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)

set(OT_PLATFORM_LIB "openthread-jn5169" CACHE STRING "" FORCE)
set(OT_PLATFORM_LIB_RCP "openthread-jn5169" CACHE STRING "" FORCE)

add_subdirectory(openthread)

target_compile_definitions(ot-config INTERFACE
    OPENTHREAD_CONFIG_FILE="openthread-core-jn5169-config.h"
)
target_include_directories(ot-config INTERFACE ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(src)

add_custom_target(ot-rcp-bin ALL
    COMMAND ba-elf-objcopy -j .version -j .bir -j .flashheader -j .vsr_table
            -j .vsr_handlers -j .rodata -j .text -j .data -j .ro_mac_address
            -S -O binary $<TARGET_FILE:ot-rcp> ${PROJECT_BINARY_DIR}/ot-rcp.bin
    DEPENDS ot-rcp
    COMMENT "Генерация ot-rcp.bin для JN-загрузчика"
)
