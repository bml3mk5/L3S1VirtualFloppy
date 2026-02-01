/** @file shell_cmd_fdc8.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-01-01
 * 
 * @copyright Copyright (c) Sasaji 2025
 */

#include "shell_cmd_fdc8.h"
#include "shell_cmd_fdc5.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include "fdc_common.h"
#include "parallel.h"
#include "main.h"

#define FDC_VALUE(x) (~(x))
#define FDC_WAIT 5

static uint8_t unit_sel;

//======================================================================

static uint8_t fdc_wait_drqirq();
static uint8_t fdc_wait_drqirq1_in_masking();
static uint8_t fdc_wait_drqirq2_in_masking();
static uint8_t fdc_wait_irq();
static uint8_t fdc_wait_halt_off();

//======================================================================

static uint32_t convert_status_type1(uint8_t sts)
{
    uint32_t nsts = 0;
    if (sts & FDC_8INCH_ST_BUSY) nsts |= FDCC_ST_BUSY;
    if (sts & FDC_8INCH_ST_SEEKERR) nsts |= FDCC_ST_SEEKERR;
    if (sts & FDC_8INCH_ST_CRCERR) nsts |= FDCC_ST_CRCERR;
    if (sts & FDC_8INCH_ST_WRITEP) nsts |= FDCC_ST_WRITEP;
    if (sts & FDC_8INCH_ST_NOTREADY) nsts |= FDCC_ST_NOTREADY;
    return nsts;
}

static uint32_t convert_status_read_sector(uint8_t sts)
{
    uint32_t nsts = 0;
    if (sts & FDC_8INCH_ST_BUSY) nsts |= FDCC_ST_BUSY;
    if (sts & FDC_8INCH_ST_LOSTDATA) nsts |= FDCC_ST_LOSTDATA;
    if (sts & FDC_8INCH_ST_CRCERR) nsts |= FDCC_ST_CRCERR;
    if (sts & FDC_8INCH_ST_RECNFND) nsts |= FDCC_ST_RECNFND;
    if (sts & FDC_8INCH_ST_RECTYPE) nsts |= FDCC_ST_DELETED;
    if (sts & FDC_8INCH_ST_NOTREADY) nsts |= FDCC_ST_NOTREADY;
    return nsts;
}

static uint32_t convert_status_write_sector(uint8_t sts)
{
    uint32_t nsts = 0;
    if (sts & FDC_8INCH_ST_BUSY) nsts |= FDCC_ST_BUSY;
    if (sts & FDC_8INCH_ST_LOSTDATA) nsts |= FDCC_ST_LOSTDATA;
    if (sts & FDC_8INCH_ST_CRCERR) nsts |= FDCC_ST_CRCERR;
    if (sts & FDC_8INCH_ST_RECNFND) nsts |= FDCC_ST_RECNFND;
    if (sts & FDC_8INCH_ST_WRITEFAULT) nsts |= FDCC_ST_WRITEP;
    if (sts & FDC_8INCH_ST_WRITEP) nsts |= FDCC_ST_WRITEP;
    if (sts & FDC_8INCH_ST_NOTREADY) nsts |= FDCC_ST_NOTREADY;
    return nsts;
}

void fdc8inch_motor_off(uint8_t drv)
{
    unit_sel = (drv & 3);
    parallel_write(FDC_8INCH_UNIT, unit_sel);
}

bool fdc8inch_motor_on(uint8_t drv, uint32_t sid_num, bool dden)
{
    uint8_t sts;
    unit_sel = (drv & 3) | FDC_8INCH_UNIT_MOTOR;
    if (dden) unit_sel |= FDC_8INCH_UNIT_DDEN;
    if (sid_num) unit_sel |= FDC_8INCH_UNIT_SIDE1;
    if (!some_signals_fdc_is_enable()) {
        printf("CMD_FDC8: Device is not active.\n");
        return false;
    }
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Motor On: 0x%02x\n", unit_sel);
    parallel_write(FDC_8INCH_UNIT, unit_sel);
    for(int i=0; i< (5 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
        if (!(sts & FDC_8INCH_ST_NOTREADY)) break;
    }
    if (sts & FDC_8INCH_ST_NOTREADY) {
        printf("CMD_FDC8: Drive not ready: Sts:0x%02x\n", sts);
        fdc8inch_motor_off(unit_sel);
        return false;
    }
    return true;
}

static uint8_t fdc_wait_busy()
{
    uint8_t sts;
    for(int i=0; i< (5 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
        if (sts & FDC_8INCH_ST_BUSY) break;
    }
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        printf("CMD_FDC8: Cannot start command: Sts:0x%02x\n", sts);
    }
    return sts;
}

static uint8_t fdc_wait_idle()
{
    uint8_t sts;
    for(int i=0; i< (1 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
        if (!(sts & FDC_8INCH_ST_BUSY)) break;
    }
    if (sts & FDC_8INCH_ST_BUSY) {
        printf("CMD_FDC8: Cannot stop command: Sts:0x%02x\n", sts);
    }
    return sts;
}

void fdc8inch_forceint()
{
    // forceint
    uint8_t data = 0xd0;
    printf("CMD_FDC8: Forceint: 0x%02x\n", data);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sleep_us(500);
    printf("CMD_FDC8: Done.\n");
}

static uint8_t fdc_wait_seek_end()
{
    uint8_t sts;
    for(int i=0; i< (20 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
        if (!(sts & FDC_8INCH_ST_BUSY)) break;
        if (sts & (FDC_8INCH_ST_SEEKERR | FDC_8INCH_ST_CRCERR)) break;
    }
    return sts;
}

void fdc8inch_after_command(void)
{
    FDC_VALUE(parallel_read(FDC_8INCH_STR));
}

void fdc8inch_step_rate_usage(void)
{
    printf(" Step rate: 0 - 3\n");
}

bool fdc8inch_restore(uint8_t drv, uint32_t step_rate, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t data = (step_rate & 0x03);
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Restore: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(g_shell_cmd_fdc.trk[drv]));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    sts = fdc_wait_seek_end();
    if (sts & (FDC_8INCH_ST_BUSY | FDC_8INCH_ST_SEEKERR | FDC_8INCH_ST_CRCERR)) {
        goto eof;
    }
    rc = true;
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_type1(sts);
    g_shell_cmd_fdc.trk[drv] = 0;
    return rc;
}

bool fdc8inch_seek(uint8_t drv, uint32_t trk_num, uint32_t step_rate, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t data = 0x10 | (step_rate & 0x03);
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Seek: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(g_shell_cmd_fdc.trk[drv]));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_DR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    sts = fdc_wait_seek_end();
    if (sts & (FDC_8INCH_ST_BUSY | FDC_8INCH_ST_SEEKERR | FDC_8INCH_ST_CRCERR)) {
        goto eof;
    }
    rc = true;
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_type1(sts);
    g_shell_cmd_fdc.trk[drv] = (uint8_t)trk_num;
    return rc;
}

bool fdc8inch_step(uint8_t drv, int dir, uint32_t step_rate, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t data = dir > 0 ? 0x50 : (dir < 0 ? 0x70 : 0x30);
    data |= (step_rate & 0x03);
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Step: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy(); 
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    sts = fdc_wait_seek_end();
    if (sts & (FDC_8INCH_ST_BUSY | FDC_8INCH_ST_SEEKERR | FDC_8INCH_ST_CRCERR)) {
        goto eof;
    }
    rc = true;
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_type1(sts);
    g_shell_cmd_fdc.trk[drv] = dir > 0 ? g_shell_cmd_fdc.trk[drv] + 1 : g_shell_cmd_fdc.trk[drv] - 1;
    return rc;
}

/// @brief Read data using HALT
/// @param drv 
/// @param sid_num 
/// @param sec_num 
/// @param ptime 
/// @return 
bool fdc8inch_read_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t data = sid_num != 0 ? 0x8a : 0x82;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Read Data: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_SCR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Reading: sts:0x%02x\n", sts);
        wait_spin_us(2);
//        parallel_write(FDC_8INCH_UNIT, unit_sel | FDC_8INCH_UNIT_FDC_MASK);
        while(g_shell_cmd_fdc.buffer.count < 2048) {
            wait_spin_us(2);
            parallel_write(FDC_8INCH_HALT, 0);
            fdc_wait_halt_off();
            irqdrq = fdc_wait_drqirq();
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // read data
                wait_spin_us(2);
                data = FDC_VALUE(parallel_read(FDC_8INCH_DR));
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
                }
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
        wait_spin_us(2);
//        parallel_write(FDC_8INCH_UNIT, unit_sel & ~FDC_8INCH_UNIT_FDC_MASK);
        wait_spin_us(2);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    } else {
        irqdrq = fdc_wait_irq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

/// @brief Read data using POLLING 1
/// @param drv 
/// @param sid_num 
/// @param sec_num 
/// @param ptime 
/// @return 
bool fdc8inch_read_data_2(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t data = sid_num != 0 ? 0x8a : 0x82;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Read Data: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_SCR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Reading: sts:0x%02x\n", sts);
        wait_spin_us(2);
        parallel_write(FDC_8INCH_UNIT, (unit_sel | FDC_8INCH_UNIT_FDC_MASK) & ~FDC_8INCH_UNIT_MOTOR);
        while(g_shell_cmd_fdc.buffer.count < 2048) {
            wait_spin_us(2);
            irqdrq = fdc_wait_drqirq1_in_masking();
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // read data
                wait_spin_us(2);
                data = FDC_VALUE(parallel_read(FDC_8INCH_DR));
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
                }
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
        wait_spin_us(2);
        parallel_write(FDC_8INCH_UNIT, unit_sel & ~FDC_8INCH_UNIT_FDC_MASK);
        wait_spin_us(2);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    } else {
        irqdrq = fdc_wait_irq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

/// @brief Read data using POLLING 2
/// @param drv 
/// @param sid_num 
/// @param sec_num 
/// @param ptime 
/// @return 
bool fdc8inch_read_data_3(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t data = sid_num != 0 ? 0x8a : 0x82;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Read Data: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_SCR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Reading: sts:0x%02x\n", sts);
        wait_spin_us(2);
        parallel_write(FDC_8INCH_UNIT, unit_sel | FDC_8INCH_UNIT_FDC_MASK);
        while(g_shell_cmd_fdc.buffer.count < 2048) {
            wait_spin_us(2);
            irqdrq = fdc_wait_drqirq2_in_masking();
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // read data
                wait_spin_us(8);
                data = FDC_VALUE(parallel_read(FDC_8INCH_DR));
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
                }
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
        wait_spin_us(2);
        parallel_write(FDC_8INCH_UNIT, unit_sel & ~FDC_8INCH_UNIT_FDC_MASK);
        wait_spin_us(2);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    } else {
        irqdrq = fdc_wait_irq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

/// @brief Write data using HALT
/// @param drv 
/// @param sid_num 
/// @param sec_num 
/// @param ptime 
/// @return 
bool fdc8inch_write_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear count
    g_shell_cmd_fdc.buffer.count = 0;
    // write data
    uint8_t data = sid_num != 0 ? 0xaa : 0xa2;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Write Data: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_SCR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        return sts;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_WRITEP | FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Writing: sts:0x%02x\n", sts);
        wait_spin_us(2);
//        parallel_write(FDC_8INCH_UNIT, unit_sel | FDC_8INCH_UNIT_FDC_MASK);
        while(g_shell_cmd_fdc.buffer.count < 2048) {
            wait_spin_us(2);
            parallel_write(FDC_8INCH_HALT, 0);
            fdc_wait_halt_off();
            irqdrq = fdc_wait_drqirq();
//          printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // write data
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    data = g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count];
                }
                wait_spin_us(2);
                parallel_write(FDC_8INCH_DR, FDC_VALUE(data));
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
        wait_spin_us(2);
//        parallel_write(FDC_8INCH_UNIT, unit_sel & ~FDC_8INCH_UNIT_FDC_MASK);
        wait_spin_us(2);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    } else {
        irqdrq = fdc_wait_irq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

/// @brief Write data using POLLING
/// @param drv 
/// @param sid_num 
/// @param sec_num 
/// @param ptime 
/// @return 
bool fdc8inch_write_data_2(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear count
    g_shell_cmd_fdc.buffer.count = 0;
    // write data
    uint8_t data = sid_num != 0 ? 0xaa : 0xa2;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Write Data: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_SCR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        return sts;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_WRITEP | FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Writing: sts:0x%02x\n", sts);
        wait_spin_us(2);
        parallel_write(FDC_8INCH_UNIT, unit_sel | FDC_8INCH_UNIT_FDC_MASK);
        while(g_shell_cmd_fdc.buffer.count < 2048) {
            wait_spin_us(2);
            irqdrq = fdc_wait_drqirq1_in_masking();
//          printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // write data
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    data = g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count];
                }
                wait_spin_us(2);
                parallel_write(FDC_8INCH_DR, FDC_VALUE(data));
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
        wait_spin_us(2);
        parallel_write(FDC_8INCH_UNIT, unit_sel & ~FDC_8INCH_UNIT_FDC_MASK);
        wait_spin_us(2);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    } else {
        irqdrq = fdc_wait_irq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

/// @brief 
/// @param drv 
/// @param sid_num 
/// @param ptime 
/// @return 
bool fdc8inch_read_track(uint8_t drv, uint32_t sid_num, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t data = 0xe0;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Read Track: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Reading: sts:0x%02x\n", sts);
        while(1) {
            irqdrq = fdc_wait_drqirq();
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                wait_spin_us(2);
                sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // read data
                wait_spin_us(2);
                data = FDC_VALUE(parallel_read(FDC_8INCH_DR));
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
                }
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
    } else {
        irqdrq = fdc_wait_drqirq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

/// @brief 
/// @param drv 
/// @param ptime 
/// @return 
bool fdc8inch_read_addr(uint8_t drv, pftime_t *ptime)
{
    bool rc = false;
    uint8_t sts;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint32_t irqdrq;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t data = 0xc0;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC8: Read Address: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    parallel_write(FDC_8INCH_TR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_8INCH_CR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_8INCH_ST_BUSY)) {
        goto eof;
    }
    wait_spin_us(2);
    sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    if (!(sts & (FDC_8INCH_ST_RECNFND | FDC_8INCH_ST_CRCERR))) {
//      printf("CMD_FDC8: Reading: sts:0x%02x\n", sts);
        while(g_shell_cmd_fdc.buffer.count < 16) {
            irqdrq = fdc_wait_drqirq();
            if (irqdrq & FDC_8INCH_UNIT_IRQ) {
                wait_spin_us(2);
                sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
                rc = true;
                break;
            } else if (irqdrq & FDC_8INCH_UNIT_DRQ) {
                // read data
                wait_spin_us(2);
                data = FDC_VALUE(parallel_read(FDC_8INCH_DR));
                if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                    g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
                }
                if (ptime) {
                    ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                    ptime->count++;
                }
                g_shell_cmd_fdc.buffer.count++;
            }
        }
    } else {
        irqdrq = fdc_wait_drqirq();
        if (ptime) {
            ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
            ptime->count++;
        }
        printf("CMD_FDC8: irq_drq:0x%02x\n", irqdrq);
    }
    sts = fdc_wait_idle();
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status_read_sector(sts);
    return rc;
}

//======================================================================

/// @brief 
/// @return 
static uint8_t fdc_wait_drqirq()
{
    uint8_t irqdrq = 0;
    do {
        wait_spin_us(3);
        tight_loop_contents();
        if (~parallel_read(FDC_8INCH_STR) & FDC_8INCH_ST_DRQ) {
            irqdrq |= FDC_8INCH_UNIT_DRQ;
        } else if (fdc_common_now_irq()) {
            irqdrq |= FDC_8INCH_UNIT_IRQ;
        }
    } while((irqdrq & (FDC_8INCH_UNIT_IRQ | FDC_8INCH_UNIT_DRQ)) == 0);
    return irqdrq;
}

static uint8_t fdc_wait_drqirq1_in_masking()
{
    uint8_t irqdrq = 0;
    do {
        wait_spin_us(3);
        tight_loop_contents();
        if (~parallel_read(FDC_8INCH_DRQ) & FDC_8INCH_UNIT_DRQ) {
            irqdrq |= FDC_8INCH_UNIT_DRQ;
        } else if (fdc_common_now_irq()) {
            irqdrq |= FDC_8INCH_UNIT_IRQ;
        }
    } while((irqdrq & (FDC_8INCH_UNIT_IRQ | FDC_8INCH_UNIT_DRQ)) == 0);
    return irqdrq;
}

static uint8_t fdc_wait_drqirq2_in_masking()
{
    uint8_t irqdrq = 0;
    uint16_t data;
    do {
        wait_spin_us(3);
        tight_loop_contents();
        data = parallel_read16(FDC_8INCH_DRQ);
        if (~data & 0x8000) {
            irqdrq |= FDC_8INCH_UNIT_DRQ;
        } else if (fdc_common_now_irq()) {
            irqdrq |= FDC_8INCH_UNIT_IRQ;
        }
    } while((irqdrq & (FDC_8INCH_UNIT_IRQ | FDC_8INCH_UNIT_DRQ)) == 0);
    return irqdrq;
}

static uint8_t fdc_wait_irq()
{
    uint8_t irqdrq = 0;
    do {
        wait_spin_us(3);
        tight_loop_contents();
        if (fdc_common_now_irq()) irqdrq |= FDC_8INCH_UNIT_IRQ;
    } while((irqdrq & FDC_8INCH_UNIT_IRQ) == 0);
    return irqdrq;
}

static uint8_t __no_inline_not_in_flash_func(fdc_wait_halt_off)()
{
    bool rc;
    wait_spin_us(3);
    do {
        tight_loop_contents();
        rc = is_halt_signal_on();
    } while(rc);
    return 0;
}

void fdc8inch_read_status()
{
    uint8_t sts;
    for(int i=0; i<1000; i++) {
        wait_spin_us(3);
        sts = FDC_VALUE(parallel_read(FDC_8INCH_STR));
    }
    printf("CMD_FDC8: Status: 0x%02x\n", sts);
}

void fdc8inch_unitsel(uint8_t opts)
{
    printf("CMD_FDC8: Write to unit sel 0x%02x\n", opts);
    parallel_write(FDC_8INCH_UNIT, opts);
}

//======================================================================

#define SIDES_PER_DISK 2
#define SECTORS_PER_GROUP 26
#define SECTORS_PER_TRACK 26
#define SECTOR_SIZE 256

static const bas_fat_table_t fat_table_inf = {
    .trk_num = 37,
    .start_sec_num = 2,
    .end_sec_num = 2,
    .step_rate = 1,
    .dden = true
};

static const bas_dir_table_t dir_table_inf = {
    .secs_per_trk = SECTORS_PER_TRACK,
    .sector_size = SECTOR_SIZE,
    .trk_num = 37,
    .start_sec_num = 4,
    .end_sec_num = 22,
    .secs_per_grp = SECTORS_PER_GROUP,
    .dden = true
};

//#define BE_TO_LE_16(x) (((x) & 0xff) << 8 | ((x) >> 8) & 0xff)

//static char file_name[16];

//static const char type2_tbl[] = "B?RA";

static bool read_fat_table(uint8_t drv)
{
    return bas_read_fat_table(drv, &fat_table_inf, bas_fat_table, sizeof(bas_fat_table));
}

static bool read_directory(uint8_t drv, directory_entry_cb_t callback_e, directory_finish_cb_t callback_f, void *user_data)
{
    return bas_read_directory(drv, &dir_table_inf, callback_e, callback_f, user_data);
}

#if 0
static uint8_t next_group(uint8_t cur_group)
{
    return bas_fat_table[(cur_group + 1) & 0xff];
}

static bool count_groups_and_size(uint8_t start_group, int *group_count, int *sector_count)
{
    bool sts = true;
    int grp_count = 0;
    int sec_count = 0;
    uint8_t grp = start_group;
    uint8_t next_grp;
    while(grp < 0xc0) {
        grp_count++;
        next_grp = next_group(grp);
        if (next_grp == grp) {
            // Error: same group number
            sts = false;
            break;
        }
        grp = next_grp;
        if (grp < 0xc0) {
            sec_count+=SECTORS_PER_GROUP;
        } else {
            sec_count+=(grp & 0x1f);
            break;
        }
    }
    if (group_count) *group_count = grp_count;
    if (sector_count) *sector_count = sec_count;
    return sts;
}
#endif

#if 0
/// @brief 
/// @return true is last
static bool files_entry_cb(int row, void *p_entry, void *user_data)
{
    const bas_dir_table_t *inf = (const bas_dir_table_t *)user_data;
    directory_l3_2d_t *entry = (directory_l3_2d_t *)p_entry;
    memcpy(file_name, entry->name, 8);
    file_name[8] = ' ';
    memcpy(&file_name[9], entry->ext, 3);
    file_name[12]=0;
    int grps, secs;
    fdc5inch_count_groups_and_size(inf, entry->start_group, &grps, &secs);
    uint32_t size = SECTOR_SIZE * secs;
    if (entry->end_bytes) {
        uint16_t eb = BE_TO_LE_16(entry->end_bytes);
        size = size + eb - SECTOR_SIZE;
    }
    printf("%s %d%c %2d (%6uB)  "
        ,file_name
        ,entry->type & 7
        ,type2_tbl[entry->type2 & 3]
        ,grps
        ,size
    );
    if ((row & 1) == 1) printf("\n");
    return false;
}

static void files_finish_cb(int row, void *user_data)
{
    if ((row & 1) != 0) printf("\n");
}
#endif

void fdc8inch_cmd_files(uint8_t drv)
{
    g_shell_cmd_fdc.verbose = g_shell_cmd_fdc.verbose_force;

    // read FAT
    if (!read_fat_table(drv)) {
        goto eof;
    }
    // read DIRECTORY
    if (!read_directory(drv, fdc5inch_files_entry_cb, fdc5inch_files_finish_cb, (void *)&dir_table_inf)) {
        goto eof;
    }
eof:
    fdc8inch_motor_off(drv);
}

#if 0
/// @brief 
/// @return true is last
static bool load_entry_cb(int row, void *p_entry, void *user_data)
{
    bas_file_access_t *target = (bas_file_access_t *)user_data;
    directory_l3_2d_t *entry = (directory_l3_2d_t *)p_entry;
    if (strncasecmp(target->name, entry->name, 8) == 0
        && strncasecmp(target->ext, entry->ext, 3) == 0) {
        // match file
        target->match = 1;
        target->start_group = entry->start_group;
        target->end_bytes = BE_TO_LE_16(entry->end_bytes);
        return true;
    }
    return false;
}
#endif

static void get_track_and_sector_from_group(uint8_t group, int *track_number, int *side_number, int *sector_number)
{
    int sec = group * SECTORS_PER_GROUP;
    int trk = (sec / SECTORS_PER_TRACK);
    int sid = (trk % SIDES_PER_DISK);
    trk = (trk / SIDES_PER_DISK) + 1;
    if (trk >= 37) trk++;
    sec = (sec % SECTORS_PER_TRACK) + 1;

    if (track_number) *track_number = trk;
    if (side_number) *side_number = sid;
    if (sector_number) *sector_number = sec;
}

void fdc8inch_cmd_load(uint8_t drv, const char *file_name)
{
    uint8_t rc;
    bas_file_access_t target;

    g_shell_cmd_fdc.verbose = g_shell_cmd_fdc.verbose_force;

    memset(&target, ' ', sizeof(target));
    target.name[8] = 0;
    target.ext[3] = 0;
    target.match = 0;

    split_filename(file_name, target.name, target.ext);

    // motor on
    if (!fdc8inch_motor_on(drv, 0, true)) {
        return;
    }
    // read FAT
    if (!read_fat_table(drv)) {
        goto eof;
    }
    // search DIRECTORY
    if (!read_directory(drv, fdc5inch_load_entry_cb, NULL, &target)) {
        goto eof;
    }
    if (!target.match) {
        printf("File Not Found.\n");
        goto eof;
    }
    // follow the group chain
    bool last = false;
    uint8_t grp = target.start_group;
    uint8_t next_grp;
    int trk, sid, start_sec, secs;
    while(grp < 0xc0 && !last) {
        next_grp = fdc5inch_next_group(grp);
        if (grp == next_grp) {
            // Error
            printf("Dupricate Group Number.\n");
            break;
        }

        secs = SECTORS_PER_GROUP;
        if (next_grp >= 0xc0 && next_grp < 0xe0) {
            // last group
            secs = (next_grp & 0xf);
        }

        // access sector
        get_track_and_sector_from_group(grp, &trk, &sid, &start_sec);
        // seek
        rc = fdc8inch_seek(drv, trk, 1, NULL);
        if (!rc) {
            break;
        }
        // set side1 signal
        fdc8inch_motor_on(drv, sid, true);
        // read data
        for(int sec = start_sec; sec < start_sec + secs && !last; sec++) {
            rc = fdc8inch_read_data(drv, sid, sec, NULL);
            if (!rc) {
                last = true;
                break;
            }
        }

        grp = next_grp;
    }
    
eof:
    fdc8inch_motor_off(drv);
}

bool fdc8inch_cmd_boottest(int boot_type)
{
    uint8_t drv = 0;
    uint8_t fdctyp;
    uint8_t data;
    uint8_t sts;
    uint8_t trk_num = 0;
    uint8_t sec_num = 0;
    uint8_t buffer[260];
    int pos = 0;
    int i;

    if (boot_type != 1) {
        printf("CMD_FDC8: Boot type %d is not supported.\n", boot_type);
        return false;
    }

    g_shell_cmd_fdc.verbose = g_shell_cmd_fdc.verbose_force;

    // 2D or 2HD
    bool rc = true;
    do {
        parallel_write(FDC_8INCH_SCR, 0);
        wait_spin_us(6);
        parallel_write(FDC_8INCH_UNIT, 0xa8);
        wait_spin_us(16);
        fdctyp = parallel_read(FDC_8INCH_SCR);
        wait_spin_us(6);
        parallel_write(FDC_8INCH_UNIT, 0x28);
        wait_spin_us(4);
        parallel_write(FDC_8INCH_CR, 0x2f); // $D0 Force int
        printf("FDC type is 0x%02x.\n", fdctyp);
        wait_spin_us(1000000);  // wait
        sts = parallel_read(FDC_8INCH_STR);
        if ((sts & 0x80) == 0) {
            // not ready ?
            printf("FDC is not ready.\n");
            rc = false;
            break;
        }
        sts = fdc_wait_idle();
        if (sts & FDC_8INCH_ST_BUSY) {
            rc = false;
            break;
        }
        wait_spin_us(22);
        parallel_write(FDC_8INCH_CR, 0xfe); // $01 Restore
        wait_spin_us(1000000);
        if (!fdc_common_now_irq()) {
            // nmi 
            printf("NMI is not fired.\n");
            rc = false;
            break;
        }
        sts = parallel_read(FDC_8INCH_STR);
        do {
            pos = 0;
            wait_spin_us(18);
            parallel_write(FDC_8INCH_SCR, ~(sec_num + 1)); // sector
            wait_spin_us(8);
            parallel_write(FDC_8INCH_CR, 0x7f); // $80 Read sector
            wait_spin_us(4);
            parallel_write(FDC_8INCH_UNIT, 0x80); //
            do {
                sts = fdc_wait_drqirq1_in_masking();
                if (sts & FDC_8INCH_UNIT_DRQ) {
                    wait_spin_us(3);
                    data = parallel_read(FDC_8INCH_DR);
                    buffer[pos] = ~data;
                    pos++;
                } 
            } while((sts & FDC_8INCH_UNIT_IRQ) == 0);
            wait_spin_us(4);
            parallel_write(FDC_8INCH_UNIT, 0x28);
            wait_spin_us(16);
            sts = parallel_read(FDC_8INCH_STR);
            sts = ~sts;
            if (sts & 0xdc) {
                // error
                printf("Read failed. trk:%u sec:%u sts:0x%02x\n", trk_num, sec_num + 1, sts);
                rc = false;
                break;
            }
            cli_d88_check_data(drv, buffer, trk_num, sec_num + 1, pos, sts, 0);
            sec_num++;
        } while(sec_num < 2);
        if (buffer[127] == 0xee) {
            printf("Sector 2 is ok.\n");
        } else {
            printf("Sector 2 is not equal.\n");
        }

        // next
        wait_spin_us(4);
        parallel_write(0x14, 0x28);
        wait_spin_us(4);
        parallel_write(0x10, 0x2f);
        wait_spin_us(1000000);  // wait
        sts = parallel_read(0x10);
        sts = ~sts;
        wait_spin_us(4);
        data = parallel_read(0x13);
        data = ~data;
        printf("ExFDC: sts:0x%02x data:0x%02x.\n", sts, data);
        uint32_t xaddr = 0x2200;
        bool last = false;
        for(trk_num=1; trk_num<2; trk_num++) {
            wait_spin_us(3);
            parallel_write(FDC_8INCH_DR, ~trk_num);
            wait_spin_us(3);
            parallel_write(FDC_8INCH_CR, 0xec);    // $13 Seek
            fdc_wait_irq();
            wait_spin_us(16);
            sts = parallel_read(FDC_8INCH_STR);
            sts = ~sts;
            if (sts & 0x90) {
                   rc = false;
                   break; 
            }
            for(sec_num=0; sec_num<52; sec_num++) {
                wait_spin_us(3);
                parallel_write(FDC_8INCH_TR, ~trk_num);
                pos = 0;
                wait_spin_us(10);
                parallel_write(FDC_8INCH_UNIT, 0x28 | (sec_num >= 26 ? 0x10: 0));
                wait_spin_us(2);
                parallel_write(FDC_8INCH_SCR, ~((sec_num % 26) + 1));
                wait_spin_us(6);
                parallel_write(FDC_8INCH_CR, 0x7f); // $80 Read sector
//                wait_spin_us(2);
//                parallel_write(FDC_8INCH_UNIT, 0xa8 | (sec_num >= 26 ? 0x10: 0));
                do {
                    wait_spin_us(2);
                    parallel_write(FDC_8INCH_HALT, 0x28);   // HALT
                    wait_spin_us(2);
                    fdc_wait_halt_off();
                    if (fdc_common_now_irq()) {
                        break;
                    }
                    data = parallel_read(FDC_8INCH_DR);
                    buffer[pos] = ~data;
                    xaddr++;
                    pos++;
                } while(1);
//                wait_spin_us(2);
//                parallel_write(FDC_8INCH_UNIT, 0x28 | (sec_num >= 26 ? 0x10: 0));
                wait_spin_us(12);
                sts = parallel_read(FDC_8INCH_STR);
                wait_spin_us(3);
                parallel_write(FDC_8INCH_UNIT, 0x30);   // Motor off
                sts = ~sts;
                if (sts & 0xfc) {
                    printf("Read failed. trk:%u sec:%u sts:0x%02x addr:0x%04x\n", trk_num, sec_num + 1, sts, xaddr);
                    rc = false;
                    break;
                }
                if ((xaddr & 0xff) != 0) {
                    printf("Read unmatch. trk:%u sec:%u sts:0x%02x addr:0x%04x\n", trk_num, sec_num + 1, sts, xaddr);
                    for(i=0; i<pos; i++) {
                        printf(" %02x", buffer[i]);
                        if ((i & 0xf) == 0xf) printf("\n");
                    }
                    if ((i & 0xf) != 0) printf("\n");
                    rc = false;
                    break;
                }
                if (!cli_d88_check_data(drv, buffer, trk_num, sec_num + 1, pos, sts, xaddr)) {
                    rc = false;
                    break;
                }
                if (xaddr >= 0x4800) {
                    last = true;
                    break;
                }
            }
            if (last || !rc) {
                break;
            }
        }
        if (rc) {
            printf("Read ok. trk:%u sec:%u sts:0x%02x addr:0x%04x\n", trk_num, sec_num + 1, sts, xaddr);
        }
    } while(0);

    // motor off
    wait_spin_us(2);
    fdc8inch_motor_off(drv);

    printf("CMD_FDC8: Boot test %s.\n", rc ? "succeeded" : "failed");
    return rc;
}
