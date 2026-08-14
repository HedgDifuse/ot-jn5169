add_library(openthread-jn5169
    alarm.c
    startup.c
    syscalls.c
    entropy.c
    misc.c
    radio.c
    jn_macframe.cpp
    system.c
    uart.c
    ${PROJECT_SOURCE_DIR}/openthread/examples/platforms/utils/soft_source_match_table.c
    ${PROJECT_SOURCE_DIR}/openthread/examples/platforms/utils/mac_frame.cpp
)

target_include_directories(openthread-jn5169
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/openthread/examples/platforms
    PRIVATE
        ${PROJECT_SOURCE_DIR}/openthread/examples/platforms/utils
        # Раскладка JN-SW-4163; уточнить после распаковки SDK
        ${NXP_SDK_ROOT}/Components/MMAC/Include
        ${NXP_SDK_ROOT}/Components/HardwareApi/Include
        ${NXP_SDK_ROOT}/Components/Common/Include
        ${NXP_SDK_ROOT}/Components/MicroSpecific/Include
)

target_compile_definitions(openthread-jn5169 PRIVATE
    JENNIC_CHIP=JN5169
    JENNIC_CHIP_NAME=_JN5169
    JENNIC_CHIP_FAMILY=JN516x
    JENNIC_CHIP_FAMILY_NAME=_JN516x
    EMBEDDED
)

target_link_libraries(openthread-jn5169
    PUBLIC
        ot-config
    PRIVATE
        # Библиотеки SDK; имена сверить с содержимым Components/Library
        -Wl,--start-group
        ${NXP_SDK_ROOT}/Components/Library/libMMAC_JN5169.a
        ${NXP_SDK_ROOT}/Components/Library/libHardwareApi_JN5169.a
        ${NXP_SDK_ROOT}/Components/Library/libMicroSpecific_JN516x.a
        ${NXP_SDK_ROOT}/Components/Library/libJPT_JN5169.a
        ${NXP_SDK_ROOT}/Components/Library/libAppApi_JN5169.a
        ${NXP_SDK_ROOT}/Components/Library/libMAC_JN5169.a
        ${NXP_SDK_ROOT}/Components/Library/libAes_JN516x.a
        ${NXP_SDK_ROOT}/Components/Library/libBoot_JN516x.a
        -Wl,--end-group
)

# Линкер-скрипт и стартап из SDK (сверить точное имя для MMAC-приложений)
set(JN5169_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/jn5169-rcp.ld
    CACHE FILEPATH "Linker script")
target_link_options(openthread-jn5169 INTERFACE -T${JN5169_LINKER_SCRIPT} -L${NXP_SDK_ROOT}/Chip/JN5169/Build -Wl,--defsym=__stack_size=3072 -Wl,-u,AppColdStart -Wl,-u,AppWarmStart)

set_target_properties(openthread-jn5169 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)
