# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

set(PROJECT L3VirtualFloppy)
project(${PROJECT} C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ====================================================================================
set(TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

# Add executable. Default name is the project name, version 0.1
add_executable(${PROJECT})

pico_set_program_name(${PROJECT} "L3VirtualFloppy")
pico_set_program_version(${PROJECT} "0.2")

target_compile_definitions(${PROJECT} PRIVATE
  PICO_STACK_SIZE=4096
  PICO_CORE1_STACK_SIZE=2048
  _BML3
)

target_sources(${PROJECT} PRIVATE
  ${SOURCE_DIR}/main.c
  ${SOURCE_DIR}/parallel.c
  ${SOURCE_DIR}/msg_bridge.c
  ${SOURCE_DIR}/msc_app.c
  ${SOURCE_DIR}/event.c
  ${SOURCE_DIR}/disk_ctl.c
  ${SOURCE_DIR}/disk_d88.c
  ${SOURCE_DIR}/disk_drive.c
  ${SOURCE_DIR}/fdc_common.c
  ${SOURCE_DIR}/fdc_5inch.c
  ${SOURCE_DIR}/fdc_3inch.c
  ${SOURCE_DIR}/fdc_8inch.c
  ${SOURCE_DIR}/pio_ctrls.c
  ${SOURCE_DIR}/i2c_lcd_1602.c
  ${SOURCE_DIR}/i2c_ssd1306.c
  ${SOURCE_DIR}/i2c_led_btn.c
  ${SOURCE_DIR}/i2c_master.c
  ${SOURCE_DIR}/l3font.c
  ${SOURCE_DIR}/display.c
  ${SOURCE_DIR}/display_btn.c
  ${SOURCE_DIR}/display_storage.c
  ${SOURCE_DIR}/display_setting.c
  ${SOURCE_DIR}/simple_fifo.c
  ${SOURCE_DIR}/simple_list.c
  ${SOURCE_DIR}/config.c
  ${SOURCE_DIR}/utils.c
  ${SOURCE_DIR}/shell_cmd.c
  ${SOURCE_DIR}/shell_cmd_fdc.c
  ${SOURCE_DIR}/shell_cmd_fdc5.c
  ${SOURCE_DIR}/shell_cmd_fdc3.c
  ${SOURCE_DIR}/shell_cmd_fdc8.c
  ${SOURCE_DIR}/shell_cmd_d88.c
  ${SOURCE_DIR}/shell_cmd_cfg.c
  ${SOURCE_DIR}/shell_cmd_dbg.c
  ${SOURCE_DIR}/fatfs/ff.c
  ${TINYUSB_PATH}/lib/fatfs/source/ffsystem.c
  ${TINYUSB_PATH}/lib/fatfs/source/ffunicode.c
)

# for pio
pico_generate_pio_header(${PROJECT} ${SOURCE_DIR}/pio_ctrls.pio)

# Add the standard library to the build
target_link_libraries(${PROJECT} PRIVATE
  pico_stdlib
  pico_multicore
  hardware_pio
  hardware_i2c
  hardware_dma
  tinyusb_host
  tinyusb_board
)

# Add the standard include files to the build
target_include_directories(${PROJECT} PRIVATE
  ${SOURCE_DIR}
  ${SOURCE_DIR}/fatfs
  ${TINYUSB_PATH}/lib/fatfs/source
  ${TINYUSB_PATH}/lib/embedded-cli
#  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROJECT} 1)
pico_enable_stdio_usb(${PROJECT} 0)

pico_add_extra_outputs(${PROJECT})

