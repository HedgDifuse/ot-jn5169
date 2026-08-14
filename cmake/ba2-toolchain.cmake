# Toolchain file для ba-elf-gcc (Beyond Architecture 2, NXP JN516x)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ba2)

set(CMAKE_C_COMPILER   ba-elf-gcc)
set(CMAKE_CXX_COMPILER ba-elf-g++)
set(CMAKE_ASM_COMPILER ba-elf-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Флаги из BeyondStudio для JN5169
set(JN51XX_FLAGS "-march=ba2 -mcpu=jn51xx -mredzone-size=4 -mbranch-cost=3 -fomit-frame-pointer -fshort-enums -ffunction-sections -fdata-sections -Os")

set(CMAKE_C_FLAGS_INIT   "${JN51XX_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${JN51XX_FLAGS} -fno-exceptions -fno-rtti -U__STRICT_ANSI__")
set(CMAKE_ASM_FLAGS_INIT "${JN51XX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -nostartfiles")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
