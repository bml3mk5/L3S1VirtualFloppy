/** @file pio_ctrls.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "common.h"
#include "pio_ctrls.h"
#include <stdio.h>

#include <pico/stdlib.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <pico/sync.h>
#include <pico/binary_info.h>
#include "pio_ctrls.pio.h"
#include "main.h"
#include "msg_bridge.h"
#include "fdc_common.h"

#define PIO_PARALLEL_START_GPIO 6

// these are constant value but stay in RAM
static const uint8_t MY_CORE1_GROUP(c_pio_parallel_reg_map)[8][16] = {
    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2HD
   { 0, 1, 2, 3, 4, 4, 4, 4,14,14,14,14, 5, 5, 5, 5, },
    // 3inch 1S
   { 8, 8, 8, 8, 8, 8, 8, 8, 0, 1, 2, 3, 4, 5, 6, 7, },

    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2HD (masked)
   {15,15, 6, 3, 4, 4, 4, 4,14,14,14,14, 5, 5, 5, 5, },
    // 3inch 1S
   { 8, 8, 8, 8, 8, 8, 8, 8, 0, 1, 2, 3, 4, 5, 6, 7, },
};

static uint8_t MY_CORE1_GROUP(disk_type);

pio_parallel_read_t MY_CORE1_GROUP(g_pio_parallel_read);
pio_parallel_write_t MY_CORE1_GROUP(g_pio_parallel_write);

static void pio_parallel_program_init(PIO pio);
static void pio_parallel_read_program_init(PIO pio);
static void pio_parallel_write_program_init(PIO pio);
#ifdef USE_PIO_BUZZER
static void pio_buzzer_program_init(PIO pio);
#endif

//--------------------------------------------------------------------

void pio_ctrls_init()
{
    disk_type = 0;

    PIO pio = pio0;
    pio_parallel_program_init(pio);
    pio_parallel_read_program_init(pio);
    pio_parallel_write_program_init(pio);
#ifdef USE_PIO_BUZZER
    pio_buzzer_program_init(pio);
#endif
    bi_decl(bi_pin_mask_with_name(((1u << PARALLEL_PIN_D_COUNT) - 1) << PIO_PARALLEL_START_GPIO, "DATA BUS (InOut)"));
    bi_decl(bi_pin_mask_with_name(((1u << PARALLEL_PIN_A_COUNT) - 1) << (PIO_PARALLEL_START_GPIO + PARALLEL_PIN_D_COUNT), "ADDR BUS (In)"));
    bi_decl(bi_pin_mask_with_name(1u << (PIO_PARALLEL_START_GPIO + PARALLEL_PIN_WE_N), "WE# (In)"));
    bi_decl(bi_pin_mask_with_name(1u << (PIO_PARALLEL_START_GPIO + PARALLEL_PIN_RE_N), "RE# (In)"));

    pio_sm_set_enabled(pio, PIO_PARALLEL_READ_SM, true);
    pio_sm_set_enabled(pio, PIO_PARALLEL_WRITE_SM, true);
#ifdef USE_PIO_BUZZER
    pio_sm_set_enabled(pio, PIO_BUZZER_SM, true);
#endif

#ifdef PIO_PARALLEL_FDTYPE0_PIN
    // for fd type
    for(int i=PIO_PARALLEL_FDTYPE0_PIN; i<PIO_PARALLEL_FDTYPE0_PIN + PIO_PARALLEL_FDTYPE_COUNT; i++) {
        gpio_init(i);
        gpio_pull_down(i);
        gpio_set_dir(i, true);
    }
    gpio_clr_mask(PIO_PARALLEL_FDTYPE_MASK);
    gpio_put_masked(PIO_PARALLEL_FDTYPE_MASK, (disk_type << PIO_PARALLEL_FDTYPE0_PIN) & PIO_PARALLEL_FDTYPE_MASK);
    bi_decl(bi_pin_mask_with_name(((1u << PIO_PARALLEL_FDTYPE_COUNT) - 1) << PIO_PARALLEL_FDTYPE0_PIN, "FDTYPE (Out)"));
#endif

#ifdef PIO_PARALLEL_DEBUG_PIN
    // for debug
	gpio_init(PIO_PARALLEL_DEBUG_PIN);
	gpio_pull_down(PIO_PARALLEL_DEBUG_PIN);
	gpio_set_dir(PIO_PARALLEL_DEBUG_PIN, true);
	gpio_put(PIO_PARALLEL_DEBUG_PIN, false);
#endif
}

void pio_ctrls_set_disk_type(uint8_t type)
{
    disk_type = type;
    // output to gpio
    gpio_put_masked(PIO_PARALLEL_FDTYPE_MASK, ((uint32_t)disk_type << PIO_PARALLEL_FDTYPE0_PIN) & PIO_PARALLEL_FDTYPE_MASK);
}

void MY_CORE1_FUNC(pio_ctrls_set_disk_type_shift)(bool shift_on)
{
    disk_type = (shift_on ? (disk_type | 0x04) : (disk_type & ~0x04));
}

bool MY_CORE1_FUNC(pio_ctrls_get_disk_type_shift)()
{
    return ((disk_type & 0x04) != 0);
}

//--------------------------------------------------------------------

void pio_parallel_program_init(PIO pio)
{
    uint pin = PIO_PARALLEL_START_GPIO;
    uint pincnt = PARALLEL_PIN_COUNT;

    for(uint i=pin; i<pin+pincnt; i++) {
        gpio_init(i);
        // high impedance
        gpio_set_pulls(i, false, false);
    }
#ifdef PIO_PARALLEL_DEBUG_USE_SIDESET
    gpio_init(pin + PARALLEL_PIN_SIDE);
    gpio_pull_down(pin + PARALLEL_PIN_SIDE);
    gpio_set_dir(pin + PARALLEL_PIN_SIDE, true);
#endif

    for(uint i=pin; i<pin+pincnt; i++) {
        pio_gpio_init(pio, i);
    }
#ifdef PIO_PARALLEL_DEBUG_USE_SIDESET
        pio_gpio_init(pio, pin + PARALLEL_PIN_SIDE);
#endif
}

//--------------------------------------------------------------------

static void MY_CORE1_FUNC(pio_parallel_read_irq_func_sub)(PIO pio, uint8_t addr)
{
#ifdef PIO_PARALLEL_DEBUG_PIN
    gpio_set_mask(1 << PIO_PARALLEL_DEBUG_PIN);
#endif
    if (fdc_common_post_read_tightly_callback(addr)) {
        msg_send_data_to_core0(MSG_TYPE_PARALLEL_READ, addr, 0);
    }
    irq_clear(g_pio_parallel_read.irq);
    pio_interrupt_clear(pio, g_pio_parallel_read.irq_idx);
#ifdef PIO_PARALLEL_DEBUG_PIN
    gpio_clr_mask(1 << PIO_PARALLEL_DEBUG_PIN);
#endif
}

/// @brief irq function of the parallel bus
static void MY_CORE1_FUNC(pio_parallel_read_irq_func)(void)
{
    PIO pio = pio0;
//    while((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + PIO_PARALLEL_READ_SM))) != 0) {}
    uint32_t data = pio->rxf[PIO_PARALLEL_READ_SM];   // state machine
    uint8_t addr = ((data >> PARALLEL_PIN_D_COUNT) & ((1u << PARALLEL_PIN_A_COUNT) - 1));
    addr = c_pio_parallel_reg_map[disk_type][addr];
    // read from bus master, so output data
    pio->txf[PIO_PARALLEL_READ_SM] = g_pio_parallel_read.odata[addr];
    pio_parallel_read_irq_func_sub(pio, addr);
}

/// @brief initialize of the parallel bus
/// * data bus has 8bits: base + 0 ... + 7
/// * address bus has 5bits: base + 8 ... + 13
/// * chip select signal is 1bit: base + 14
/// * read/write signal is 1bit: base + 15 
/// @param [in] pio    : PIO instance
void pio_parallel_read_program_init(PIO pio)
{
    uint pin = PIO_PARALLEL_START_GPIO;
    uint pincnt = PARALLEL_PIN_COUNT;
    uint sm = PIO_PARALLEL_READ_SM;
    uint offset = pio_add_program(pio, &pio_parallel_read_program);

    for(int i=0; i<PIO_PARALLEL_REGS; i++) {
        g_pio_parallel_read.odata[i] = 0;
    }
    g_pio_parallel_read.odata[15] = 0xff;

#ifndef PIO_PARALLEL_DEBUG_USE_SIDESET
    // all input
    // (dirs on data bus set in pio program)
    pio_sm_set_consecutive_pindirs(pio, sm, pin, pincnt, false);
#else
    // side pin is output
    pio_sm_set_pindirs_with_mask(pio, sm, (1 << (pin + PARALLEL_PIN_SIDE)), ((1 << (pincnt + 1)) - 1) << pin);
#endif

    pio_sm_config cr = pio_parallel_read_program_get_default_config(offset);
    sm_config_set_in_pins(&cr, pin);
    sm_config_set_out_pins(&cr, pin, PARALLEL_PIN_D_COUNT); // data bus
    sm_config_set_jmp_pin(&cr, pin + PARALLEL_PIN_RE_N); // read enable signal
    sm_config_set_in_shift(&cr, true, false, PARALLEL_PIN_COUNT);
    sm_config_set_out_shift(&cr, true, false, PARALLEL_PIN_COUNT);
#ifdef PIO_PARALLEL_DEBUG_USE_SIDESET
    sm_config_set_sideset_pins(&cr, pin + PARALLEL_PIN_SIDE);
#endif
    pio_sm_init(pio, sm, offset, &cr);

    // Find a free irq
    uint pio_irq = PIO0_IRQ_0;
    g_pio_parallel_read.irq = pio_irq;

    // Enable interrupt
    irq_set_exclusive_handler(pio_irq, pio_parallel_read_irq_func); // Add a shared IRQ handler
    irq_set_enabled(pio_irq, true); // Enable the IRQ
    irq_set_priority(pio_irq, PICO_HIGHEST_IRQ_PRIORITY);

    // enable irq on sm
    g_pio_parallel_read.irq_idx = PARALLEL_RE_IRQ_NUM;
    pio_set_irq0_source_enabled(pio, pis_interrupt0 + sm, true);
}

//--------------------------------------------------------------------

/// @brief irq function of the parallel bus
static void MY_CORE1_FUNC(pio_parallel_write_irq_func_sub)(PIO pio, uint8_t addr)
{
#ifdef PIO_PARALLEL_DEBUG_PIN
    gpio_set_mask(1 << PIO_PARALLEL_DEBUG_PIN);
#endif
    fdc_common_post_write_tightly_callback(addr);
    msg_send_data_to_core0(MSG_TYPE_PARALLEL_WRITE, addr, g_pio_parallel_write.idata);
    if (addr == 14) {
        while((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + PIO_PARALLEL_WRITE_SM))) != 0) {}
        g_pio_parallel_write.idata = pio->rxf[PIO_PARALLEL_WRITE_SM];
    }
    irq_clear(g_pio_parallel_write.irq);
    pio_interrupt_clear(pio, g_pio_parallel_write.irq_idx);
#ifdef PIO_PARALLEL_DEBUG_PIN
   	gpio_clr_mask(1 << PIO_PARALLEL_DEBUG_PIN);
#endif
}

/// @brief irq function of the parallel bus
static void MY_CORE1_FUNC(pio_parallel_write_irq_func)(void)
{
    PIO pio = pio0;
//    while((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + PIO_PARALLEL_WRITE_SM))) != 0) {}
    uint32_t data = pio->rxf[PIO_PARALLEL_WRITE_SM];   // state machine
    uint8_t addr = ((data >> PARALLEL_PIN_D_COUNT) & ((1u << PARALLEL_PIN_A_COUNT) - 1));
    addr = c_pio_parallel_reg_map[disk_type][addr & 0xf];
    // write from bus master, so input data
    // if addr is 14 or 15, no wait and process immediately. 
    if (addr != 14) {
        // wait until rx is not empty
        while((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + PIO_PARALLEL_WRITE_SM))) != 0) {}
        g_pio_parallel_write.idata = pio->rxf[PIO_PARALLEL_WRITE_SM];
    } else {
        halt_signal_on_core1();
    }
    pio_parallel_write_irq_func_sub(pio, addr);
}

/// @brief initialize of the parallel bus
/// * data bus has 8bits: base + 0 ... + 7
/// * address bus has 5bits: base + 8 ... + 13
/// * chip select signal is 1bit: base + 14
/// * read/write signal is 1bit: base + 15 
/// @param [in] pio    : PIO instance
void pio_parallel_write_program_init(PIO pio)
{
    uint pin = PIO_PARALLEL_START_GPIO;
    uint pincnt = PARALLEL_PIN_COUNT;
    uint sm = PIO_PARALLEL_WRITE_SM;
    uint offset = pio_add_program(pio, &pio_parallel_write_program);

    g_pio_parallel_write.idata = 0;

#ifndef PIO_PARALLEL_DEBUG_USE_SIDESET
    // all input
    // (dirs on data bus set in pio program)
    pio_sm_set_consecutive_pindirs(pio, sm, pin, pincnt, false);
#else
    // side pin is output
    pio_sm_set_pindirs_with_mask(pio, sm, (1 << (pin + PARALLEL_PIN_SIDE)), ((1 << (pincnt + 1)) - 1) << pin);
#endif

    pio_sm_config cw = pio_parallel_write_program_get_default_config(offset);
    sm_config_set_in_pins(&cw, pin);
    sm_config_set_out_pins(&cw, pin, PARALLEL_PIN_D_COUNT); // data bus
    sm_config_set_jmp_pin(&cw, pin + PARALLEL_PIN_WE_N); // write enable signal
    sm_config_set_in_shift(&cw, true, false, PARALLEL_PIN_COUNT);
    sm_config_set_out_shift(&cw, true, false, PARALLEL_PIN_COUNT);
#ifdef PIO_PARALLEL_DEBUG_USE_SIDESET
    sm_config_set_sideset_pins(&cw, pin + PARALLEL_PIN_SIDE);
#endif
    pio_sm_init(pio, sm, offset, &cw);

    // Find a free irq
    uint pio_irq = PIO0_IRQ_1;
    g_pio_parallel_write.irq = pio_irq;

    // Enable interrupt
    irq_set_exclusive_handler(pio_irq, pio_parallel_write_irq_func); // Add a shared IRQ handler
    irq_set_enabled(pio_irq, true); // Enable the IRQ
    irq_set_priority(pio_irq, PICO_HIGHEST_IRQ_PRIORITY);

    // enable irq on sm
    g_pio_parallel_write.irq_idx = PARALLEL_WE_IRQ_NUM;
    pio_set_irq1_source_enabled(pio, pis_interrupt0 + sm, true);
}

//--------------------------------------------------------------------

void MY_CORE1_FUNC(pio_parallel_set_data)(uint8_t addr, uint8_t data)
{
    g_pio_parallel_read.odata[addr & 0xf] = data;
//    g_pio_parallel_read.odata[c_pio_parallel_reg_map[disk_type][addr & 0xf]] = data;
//    printf("C1:%02x:%02x\n",addr,data);
}

//--------------------------------------------------------------------

static void MY_CORE1_FUNC(pio_parallel_reset)(void)
{
    PIO pio = pio0;
    uint32_t save = save_and_disable_interrupts();
    while((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + PIO_PARALLEL_WRITE_SM))) == 0) {
        (void)pio->rxf[PIO_PARALLEL_WRITE_SM];
    }
    restore_interrupts_from_disabled(save);
}

void MY_CORE1_FUNC(pio_parallel_notice)(uint32_t data)
{
    switch((data >> 8) & 0xff) {
    case MSG_NOTICE_DISK_TYPE_SHIFT:
        disk_type = ((data & 0xff) != 0 ? (disk_type | 0x04) : (disk_type & ~0x04));
        break;
    case MSG_NOTICE_IRQ:
        // fire irq
        gpio_put(FDC_COMMON_IRQ_PIN, false);
        break;
    case MSG_NOTICE_FDC_COMMON:
        fdc_common_notice_tightly_callback(data);
        break;
    default:
        pio_parallel_reset();
        break;
    }
}

//--------------------------------------------------------------------

void pio_ctrls_debug_read_regs()
{
    uint8_t odata[PIO_PARALLEL_REGS];
    for(int i=0; i<PIO_PARALLEL_REGS; i++) {
        // critical
        odata[i] = g_pio_parallel_read.odata[i];
        //
    }
    for(int i=0; i<PIO_PARALLEL_REGS; i++) {
        printf("%2d: 0x%02x(%3d)\n", i, odata[i], odata[i]);
    }
}

//--------------------------------------------------------------------

#ifdef USE_PIO_BUZZER
void pio_buzzer_program_init(PIO pio)
{
    uint pin = pio_buzzer_start_pin;
    uint pincnt = 1;
    uint sm = PIO_BUZZER_SM;
    uint offset = pio_add_program(pio, &pio_buzzer_program);

    for(uint i=pin; i<pin+pincnt; i++) {
        gpio_init(i);
        gpio_pull_down(i);
    }
    for(uint i=pin; i<pin+pincnt; i++) {
        pio_gpio_init(pio, i);
    }

    // all output
    pio_sm_set_consecutive_pindirs(pio, sm, pin, pincnt, true);

    pio_sm_config c = pio_buzzer_program_get_default_config(offset);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_out_pins(&c, pin, pincnt);
    sm_config_set_set_pins(&c, pin, pincnt);
    sm_config_set_sideset_pins(&c, pin);
    uint32_t clk_div = clock_get_hz(clk_sys) / 1000 / (pio_buzzer_clk_latency + 1); 
    if (clk_div > 65535) {
        clk_div = 65535;
    }
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)clk_div, 0);
    pio_sm_init(pio, sm, offset, &c);
}

void pio_buzzer_out()
{
    PIO pio = pio0;
    pio->txf[PIO_BUZZER_SM] = 2;
}
#endif
