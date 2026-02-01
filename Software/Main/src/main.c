/** @file main.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 * @copyright Copyright (c) 2019 Ha Thach (tinyusb.org)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "main.h"
#include "msg_bridge.h"
#include <pico/multicore.h>
#include <hardware/structs/bus_ctrl.h>
#include <bsp/board.h>
#include <tusb.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <pico/sync.h>
#include <pico/binary_info.h>
#include "config.h"
#include "pio_ctrls.h"
#include "msc_app.h"
#include "shell_cmd.h"
#include "disk_drive.h"
#include "disk_d88.h"
#include "fdc_common.h"
#include "event.h"
#include "display.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

#define HALT_SIGNAL_PIN 22

static void halt_signal_init(void);
//static void halt_signal_init_core1(void);

//--------------------------------------------------------------------

#ifdef USE_CORE1
// irq vectors on core1
static uint32_t MY_CORE1_GROUP(core1_vector_table)[32] __aligned(256);

static void core1_main(void);
#endif

//--------------------------------------------------------------------

static void led_blinking_task(void);

//--------------------------------------------------------------------

static void __no_inline_not_in_flash_func(main_loop)(void)
{
    while (1) {
        // tinyusb host task
        tuh_task();
//        msc_app_task();
        shell_cmd_task();
        msg_task();
        disk_d88_task();
        display_task();
        fdc_common_task();
        led_blinking_task();
    }
}

void main_loop_contents_in_shell_cmd(void)
{
    tuh_task();
//    msc_app_task();
    msg_task();
    disk_d88_task();
    display_task();
    fdc_common_task();
    led_blinking_task();
}

//--------------------------------------------------------------------
/*------------- MAIN -------------*/
int main(void)
{
//  stdio_init_all();
//  setup_default_uart();

//  // set highly priority for core1 to be able to access tightly
//  bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    board_init();
    config_init();
    event_init();
    halt_signal_init();
    msc_app_init();
    shell_cmd_init();
    msg_init();
    disk_drive_init();
    disk_d88_init();
    display_init();

#ifdef USE_CORE1
    // copy irq vector table for core1
    io_rw_32 vtor_bkup = scb_hw->vtor;
    for(int i=0; i<32; i++) {
        core1_vector_table[i] = ((uint32_t *)vtor_bkup)[i];
    }
    scb_hw->vtor = (io_rw_32)(intptr_t)&core1_vector_table;
    multicore_launch_core1(core1_main);
    scb_hw->vtor = vtor_bkup;

    // wait initialization on core1
    if (multicore_fifo_pop_blocking() != MSG_TYPE_INITIALIZE_DONE) {
        printf("Init Error\n");
        lcd_locate_string(0, 0, "Init Error");
    }
#else
    pio_ctrls_init();
#endif
#if 0
    io_rw_32 vtor_base = scb_hw->vtor;
    printf("Core0: VTOR:%08x\n", vtor_base);
    for(int i=0; i<32; i++) {
        printf(" %02d:%08x", i, ((uint32_t *)vtor_bkup)[i]);
        if ((i & 3) == 3) printf("\n");
    }
#endif

    fdc_common_init();
    // init host stack on configured roothub port
    tuh_init(BOARD_TUH_RHPORT);

    printf("%s Ver.%s\n", APPLICATION, VERSION);

    main_loop();

    return 0;
}

//--------------------------------------------------------------------

#ifdef USE_CORE1
static void MY_CORE1_FUNC(core1_main_loop)(void)
{
    while (1) {
        core1_msg_task();
    }
}

void core1_main(void)
{
    // set irq vector table for core1
    scb_hw->vtor = (uintptr_t)&core1_vector_table;

//    halt_signal_init_core1();
    pio_ctrls_init();

#if 0
    printf("Core1: VTOR:%08x\n", scb_hw->vtor);
    for(int i=0; i<32; i++) {
        printf(" %02d:%08x", i, core1_vector_table[i]);
        if ((i & 3) == 3) printf("\n");
    }
#endif

    multicore_fifo_push_blocking(MSG_TYPE_INITIALIZE_DONE);

    core1_main_loop();
}
#endif

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t dev_addr)
{
    (void) dev_addr;
}

void tuh_umount_cb(uint8_t dev_addr)
{
    (void) dev_addr;
}

//--------------------------------------------------------------------+

static uint8_t halt_signals;

static int64_t halt_signal_off_cb(alarm_id_t id, void *user_data)
{
    uint8_t signal = (uint8_t)(uint32_t)user_data;
    halt_signal_off(signal);
    return 0;
}

void __not_in_flash_func(halt_signal_off)(uint8_t mask)
{
    halt_signals &= ~mask;
    gpio_put(HALT_SIGNAL_PIN, halt_signals == 0); // negative
}

void __not_in_flash_func(halt_signal_on)(uint8_t mask)
{
    halt_signals |= mask;
    gpio_put(HALT_SIGNAL_PIN, halt_signals == 0); // negative
}

void MY_CORE1_FUNC(halt_signal_on_core1)()
{
    // forcely
    gpio_put(HALT_SIGNAL_PIN, false); // negative
}

bool __not_in_flash_func(is_halt_signal_on)()
{
    return !gpio_get(HALT_SIGNAL_PIN);
}

void halt_signal_init(void)
{
    halt_signals = 0;
    gpio_init(HALT_SIGNAL_PIN);
    gpio_set_dir(HALT_SIGNAL_PIN, true);
    gpio_pull_up(HALT_SIGNAL_PIN);
    halt_signal_on(HALT_SIGNAL_POR);
    event_register_event(4000000, halt_signal_off_cb, HALT_SIGNAL_POR);
    bi_decl(bi_pin_mask_with_name(1u << HALT_SIGNAL_PIN, "HALT# (Out)"));
}

#if 0
void MY_CORE1_FUNC(halt_signal_init_core1)(void)
{
    gpio_init(HALT_SIGNAL_PIN);
    gpio_set_dir(HALT_SIGNAL_PIN, true);
}
#endif

//--------------------------------------------------------------------+

bool some_signals_fdc_is_enable(void)
{
 	return gpio_get(MSC_APP_ENABLE_PIN);
}

//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+
void __no_inline_not_in_flash_func(led_blinking_task)(void)
{
    const uint32_t interval_ms[2] = { 950, 50 };
    static uint32_t start_ms = 0;
    static uint8_t led_state = 0;

    // Blink every interval ms
    if ( board_millis() - start_ms < interval_ms[led_state & 1]) return; // not enough time
    start_ms += interval_ms[led_state & 1];

    led_state = (led_state + 1) & 3; // toggle
    board_led_write(led_state & 1);
    display_led_state(led_state);
}
