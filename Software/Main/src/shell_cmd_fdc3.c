/** @file shell_cmd_fdc3.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "shell_cmd_fdc3.h"
#include <stdio.h>
#include <string.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include "parallel.h"
#include "main.h"
#include "fdc_common.h"

#define FDC_VALUE(x) (x)
#define FDC_WAIT 4
#define FDC_WAIT_T 3

//======================================================================

static uint32_t convert_status(uint16_t sts)
{
    uint32_t nsts = 0;
    if (sts & FDC_3INCH_STA_BUSY) nsts |= FDCC_ST_BUSY;
    if (sts & FDC_3INCH_STA_WRITEP) nsts |= FDCC_ST_WRITEP;
    if (!(sts & FDC_3INCH_STA_DREADY)) nsts |= FDCC_ST_NOTREADY;
    if (sts & FDC_3INCH_STA_TRACKNE) nsts |= FDCC_ST_RECNFND;
    if (sts & FDC_3INCH_STA_DELETE) nsts |= FDCC_ST_DELETED;

    if (sts & (FDC_3INCH_STB_SEEKERR << 8)) nsts |= FDCC_ST_SEEKERR;
    if (sts & (FDC_3INCH_STB_CRCERR << 8)) nsts |= FDCC_ST_CRCERR;
    if (sts & (FDC_3INCH_STB_SECTNF << 8)) nsts |= FDCC_ST_RECNFND;
    if (sts & (FDC_3INCH_STB_DATANF << 8)) nsts |= FDCC_ST_LOSTDATA;
    return nsts;
}

static inline uint8_t conv_to_drive(uint8_t drv)
{
    return (1 << (drv & 0xf));
}

void fdc3inch_motor_off(uint8_t drv)
{
    parallel_write(FDC_3INCH_UNIT, conv_to_drive(drv));
}

bool fdc3inch_motor_on(uint8_t drv, uint32_t sid_num, bool dden)
{
    uint8_t stsa;
    uint8_t data = conv_to_drive(drv) | FDC_3INCH_UNIT_MOTOR;
    if (!some_signals_fdc_is_enable()) {
        printf("CMD_FDC3: Device is not active.\n");
        return false;
    }
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Motor On: 0x%02x\n", data);
    parallel_write(FDC_3INCH_UNIT, data);
    for(int i=0; i< (5 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        stsa = FDC_VALUE(parallel_read(FDC_3INCH_STRA));
        if (stsa & FDC_3INCH_STA_DREADY) break;
    }
    if (!(stsa & FDC_3INCH_STA_DREADY)) {
        printf("CMD_FDC3: Drive not ready: Sts:0x%02x\n", stsa);
        fdc3inch_motor_off(data);
        return false;
    }
    return true;
}

static uint8_t fdc_wait_busy()
{
    uint8_t stsa;
    for(int i=0; i< (5 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        stsa = FDC_VALUE(parallel_read(FDC_3INCH_STRA));
        if (stsa & FDC_3INCH_STA_BUSY) break;
    }
    if (!(stsa & FDC_3INCH_STA_BUSY)) {
        printf("CMD_FDC3: Cannot start command: Sts:0x%02x\n", stsa);
    }
    return stsa;
}

static uint16_t fdc_wait_idle(uint32_t trk, uint32_t sid, uint32_t sec)
{
    uint8_t stsa, stsb;
    for(int i=0; i< (1 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        stsa = FDC_VALUE(parallel_read(FDC_3INCH_STRA));
        if (!(stsa & FDC_3INCH_STA_BUSY)) break;
    }
    if (stsa & FDC_3INCH_STA_BUSY) {
        printf("CMD_FDC3: Cannot stop command: Stsa:0x%02x Trk:%u Sid:%u Sec:%u\n", stsa, trk, sid, sec);
    }
    wait_spin_us(2);
    stsb = FDC_VALUE(parallel_read(FDC_3INCH_STRB));
    return ((uint16_t)stsb << 8) | stsa;
}

void fdc3inch_forceint()
{
    printf("FDC 3inch: Forceint is not supported.\n");
}

static uint16_t fdc_wait_seek_end()
{
    uint8_t stsa, stsb;
    for(int i=0; i< (5 * 1000 * 1000 / FDC_WAIT); i++) {
        wait_spin_us(FDC_WAIT);
        stsa = FDC_VALUE(parallel_read(FDC_3INCH_STRA));
        if (!(stsa & FDC_3INCH_STA_BUSY)) break;
    }
    wait_spin_us(2);
    stsb = FDC_VALUE(parallel_read(FDC_3INCH_STRB));
    return ((uint16_t)stsb << 8) | stsa;
}

void fdc3inch_after_command(void)
{
    FDC_VALUE(parallel_read(FDC_3INCH_ISR));
}

static void fdc_set_step_rate(uint32_t step_rate)
{
    step_rate = (step_rate & 0xf) << 4;
    if (step_rate == 0) step_rate = 0x10;
    step_rate |= 1;
    parallel_write(FDC_3INCH_SUR, (uint8_t)FDC_VALUE(step_rate));
}

void fdc3inch_step_rate_usage(void)
{
    printf(" Step rate: 0 - 15\n");
}

// command STZ
bool fdc3inch_restore(uint8_t drv, uint32_t step_rate, pftime_t *ptime)
{
    bool rc = false;
    uint16_t sts;
    uint8_t data = 0xc2;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Restore: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    fdc_set_step_rate(step_rate);
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CTAR, (uint8_t)FDC_VALUE(g_shell_cmd_fdc.trk[drv]));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    sts = fdc_wait_seek_end();
    if (sts & (0xff00 | FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    rc = true;
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status(sts);
    g_shell_cmd_fdc.trk[drv] = 0;
    return rc;
}

// command SEK
bool fdc3inch_seek(uint8_t drv, uint32_t trk_num, uint32_t step_rate, pftime_t *ptime)
{
    bool rc = false;
    uint16_t sts;
    uint8_t data = 0xc3;
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Seek: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    fdc_set_step_rate(step_rate);
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CTAR, (uint8_t)FDC_VALUE(g_shell_cmd_fdc.trk[drv]));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_GCR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy();
    if (!(sts & FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    sts = fdc_wait_seek_end();
    if (sts & (0xff00 | FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    rc = true;
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status(sts);
    g_shell_cmd_fdc.trk[drv] = (uint8_t)trk_num;
    return rc;
}

// command SEK 1step
bool fdc3inch_step(uint8_t drv, int dir, uint32_t step_rate, pftime_t *ptime)
{
    bool rc = false;
    uint16_t sts;
    uint8_t data = 0xc3;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Step: 0x%02x\n", data);
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
    fdc_set_step_rate(step_rate);
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CTAR, (uint8_t)FDC_VALUE(trk_num));
    if (dir > 0 && trk_num < 40) trk_num++;
    else if (dir < 0 && trk_num > 0) trk_num--;
    wait_spin_us(2);
    parallel_write(FDC_3INCH_GCR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, (uint8_t)FDC_VALUE(data));
    sts = fdc_wait_busy(); 
    if (!(sts & FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    sts = fdc_wait_seek_end();
    if (sts & (0xff00 | FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    rc = true;
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status(sts);
    g_shell_cmd_fdc.trk[drv] = trk_num;
    return rc;
}

static uint8_t fdc_wait_isr()
{
    uint8_t irq = 0;
    do {
        wait_spin_us(FDC_WAIT_T);
        irq = FDC_VALUE(parallel_read(FDC_3INCH_ISR));
    } while((irq & (FDC_3INCH_ISR_CMDCOMP | FDC_3INCH_ISR_STRB | FDC_3INCH_ISR_STSREQ)) == 0);
    return irq;
}

static uint8_t fdc_wait_irq()
{
    int cnt = 0;
    uint8_t irq = 0;
    do {
        wait_spin_us(FDC_WAIT_T);
        if (fdc_common_now_irq()) {
            cnt++;
        } else {
            cnt = 0;
        }
    } while(cnt < 3);
    wait_spin_us(FDC_WAIT_T);
    irq = FDC_VALUE(parallel_read(FDC_3INCH_ISR));
    return irq;
}

static uint8_t fdc_wait_drq()
{
    uint8_t drq = 0;
    for(int i=0; i< (5 * 1000 * 1000 / FDC_WAIT_T); i++) {
        wait_spin_us(FDC_WAIT_T);
        drq = FDC_VALUE(parallel_read(FDC_3INCH_STRA));
        if ((drq & (FDC_3INCH_STA_BUSY | FDC_3INCH_STA_DREADY)) != (FDC_3INCH_STA_BUSY | FDC_3INCH_STA_DREADY)) {
//            printf("CMD_FDC3: clear BUSY or READY: Stra:0x%02x\n", drq);
            break;
        }
        if ((drq & (FDC_3INCH_STA_DRQ | FDC_3INCH_STA_TRACKNE)) != 0) {
            break;
        }
    }
//    printf("CMD_FDC3: Drq Stra:0x%02x\n", drq);
    return drq;
}

// command SSR
bool fdc3inch_read_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint16_t sts;
    uint32_t drqirq;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint8_t data;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t command = (drv & 0xe0) | 0x04;
    drv &= 3;
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
//    trk_num = FDC_VALUE(parallel_read(FDC_3INCH_CTAR));
//    wait_spin_us(2);
    parallel_write(FDC_3INCH_LTAR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_SAR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, (uint8_t)FDC_VALUE(command));
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Read Data: cmd:0x%02x trk:%u sec:%u\n", command, trk_num, sec_num);
    sts = fdc_wait_busy();
    if (!(sts & FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
//    printf("CMD_FDC3: Stra:0x%02x\n", sts);
    wait_spin_us(2);
    if (!(command & FDC_3INCH_CMR_DMA)) {
        if (command & FDC_3INCH_CMR_FUNCMASK) {
            // wait to change flags on ISR
            drqirq = fdc_wait_isr();
        } else {
            // wait interrupt
            drqirq = fdc_wait_irq();
        }
        if (!(drqirq & FDC_3INCH_ISR_STSREQ)) {
            // data not found ?
            printf("CMD_FDC3: Read Data Err: ISR:0x%02x\n", drqirq);
            goto eof2;
//        } else {
//            printf("CMD_FDC3: ISR:0x%02x\n", drqirq);
        }
    }
    wait_spin_us(2);
    while(g_shell_cmd_fdc.buffer.count < 2048) {
        drqirq = fdc_wait_drq();
        if (!(drqirq & FDC_3INCH_STA_BUSY)) {
            // end of command
            rc = true;
            break;
        } else if (drqirq & FDC_3INCH_STA_DRQ) {
            // read data
            wait_spin_us(2);
            data = FDC_VALUE(parallel_read(FDC_3INCH_DIR));
            if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
            }
            if (ptime) {
                ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                ptime->count++;
            }
            g_shell_cmd_fdc.buffer.count++;
        } else {
            // error ?
            break;
        }
    }
eof2:
    sts = fdc_wait_idle(trk_num, sid_num, sec_num);
    wait_spin_us(2);
    drqirq = FDC_VALUE(parallel_read(FDC_3INCH_ISR));
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status(sts);
    return rc;
}

bool fdc3inch_write_data(uint8_t drv, uint32_t sid_num, uint32_t sec_num, pftime_t *ptime)
{
    bool rc = false;
    uint16_t sts;
    uint32_t drqirq;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint8_t data;

    // clear position of buffer
    g_shell_cmd_fdc.buffer.count = 0;
    // write data
    uint8_t command = (drv & 0xe0) | 0x05;
    drv &= 3;
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
//    trk_num = FDC_VALUE(parallel_read(FDC_3INCH_CTAR));
//    wait_spin_us(2);
    parallel_write(FDC_3INCH_LTAR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_SAR, (uint8_t)FDC_VALUE(sec_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, (uint8_t)FDC_VALUE(command));
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Write Data: cmd:0x%02x trk:%u sec:%u\n", command, trk_num, sec_num);
    sts = fdc_wait_busy();
    if (!(sts & FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
    wait_spin_us(2);
    if (!(command & FDC_3INCH_CMR_DMA)) {
        if (command & FDC_3INCH_CMR_FUNCMASK) {
            // wait to change flags on ISR
            drqirq = fdc_wait_isr();
        } else {
            // wait interrupt
            drqirq = fdc_wait_irq();
        }
        if (!(drqirq & FDC_3INCH_ISR_STSREQ)) {
            // data not found ?
            if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Write Data Err: ISR:0x%02x\n", drqirq);
            goto eof2;
        }
    }
    wait_spin_us(2);
    while(g_shell_cmd_fdc.buffer.count < 2048) {
        drqirq = fdc_wait_drq();
        if (!(drqirq & FDC_3INCH_STA_BUSY)) {
            // end of command
            rc = true;
            break;
        } else if (drqirq & FDC_3INCH_STA_DRQ) {
            // write data
            if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                data = g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count];
            }
            wait_spin_us(2);
            parallel_write(FDC_3INCH_DOR, FDC_VALUE(data));
            if (ptime) {
                ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                ptime->count++;
            }
            g_shell_cmd_fdc.buffer.count++;
        } else {
            // error ?
            break;
        }
    }
eof2:
    sts = fdc_wait_idle(trk_num, sid_num, sec_num);
    wait_spin_us(2);
    drqirq = FDC_VALUE(parallel_read(FDC_3INCH_ISR));
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status(sts);
    return rc;
}

// command FFR
bool fdc3inch_read_track(uint8_t drv, uint32_t sid_num, pftime_t *ptime)
{
    bool rc = false;
    uint16_t sts;
    uint32_t drqirq;
    uint8_t trk_num = g_shell_cmd_fdc.trk[drv];
    uint8_t data;

    // clear buffer
    memset(&g_shell_cmd_fdc.buffer, 0, sizeof(g_shell_cmd_fdc.buffer));
    // read data
    uint8_t command = (drv & 0xe0) | 0x0a;
    drv &= 3;
    if (ptime) {
        ptime->start = to_us_since_boot(get_absolute_time());
        ptime->count = 0;
    }
//    trk_num = FDC_VALUE(parallel_read(FDC_3INCH_CTAR));
//    wait_spin_us(2);
    parallel_write(FDC_3INCH_LTAR, (uint8_t)FDC_VALUE(trk_num));
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, (uint8_t)FDC_VALUE(command));
    if (g_shell_cmd_fdc.verbose) printf("CMD_FDC3: Read Track: cmd:0x%02x trk:%u\n", command, trk_num);
    sts = fdc_wait_busy();
    if (!(sts & FDC_3INCH_STA_BUSY)) {
        goto eof;
    }
//    printf("CMD_FDC3: Stra:0x%02x\n", sts);
    wait_spin_us(2);
    if (!(command & FDC_3INCH_CMR_DMA)) {
        if (command & FDC_3INCH_CMR_FUNCMASK) {
            // wait to change flags on ISR
            drqirq = fdc_wait_isr();
        } else {
            // wait interrupt
            drqirq = fdc_wait_irq();
        }
        if (!(drqirq & FDC_3INCH_ISR_STSREQ)) {
            // data not found ?
            printf("CMD_FDC3: Read Data Err: ISR:0x%02x\n", drqirq);
            goto eof2;
//        } else {
//            printf("CMD_FDC3: ISR:0x%02x\n", drqirq);
        }
    }
    wait_spin_us(2);
    while(g_shell_cmd_fdc.buffer.count < 3125) {
        drqirq = fdc_wait_drq();
        if (!(drqirq & FDC_3INCH_STA_BUSY)) {
            // end of command
            rc = true;
            break;
        } else if (drqirq & FDC_3INCH_STA_DRQ) {
            // read data
            wait_spin_us(2);
            data = FDC_VALUE(parallel_read(FDC_3INCH_DIR));
            if (g_shell_cmd_fdc.buffer.count < sizeof(g_shell_cmd_fdc.buffer.data)) {
                g_shell_cmd_fdc.buffer.data[g_shell_cmd_fdc.buffer.count] = data;
            }
            if (ptime) {
                ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
                ptime->count++;
            }
            g_shell_cmd_fdc.buffer.count++;
        } else {
            // error ?
            break;
        }
    }
    // send command 0
    wait_spin_us(2);
    parallel_write(FDC_3INCH_CMR, 0);
eof2:
    sts = fdc_wait_idle(trk_num, sid_num, 0);
    wait_spin_us(2);
    drqirq = FDC_VALUE(parallel_read(FDC_3INCH_ISR));
eof:
    if (ptime) {
        ptime->data[ptime->count] = (uint32_t)(to_us_since_boot(get_absolute_time()) - ptime->start);
        ptime->count++;
    }
    g_shell_cmd_fdc.status = convert_status(sts);
    return rc;
}

void fdc3inch_read_status()
{
    uint8_t sts;
    for(int i=0; i<1000; i++) {
        wait_spin_us(3);
        sts = FDC_VALUE(parallel_read(FDC_3INCH_STRA));
    }
    printf("CMD_FDC3: Status: 0x%02x\n", sts);
}

void fdc3inch_unitsel(uint8_t opts)
{
    printf("CMD_FDC3: Write to unit sel 0x%02x\n", opts);
    parallel_write(FDC_3INCH_UNIT, opts);
}

//======================================================================

#define SIDES_PER_DISK 1
#define SECTORS_PER_GROUP 4
#define SECTORS_PER_TRACK 16
#define SECTOR_SIZE 128

static const bas_fat_table_t fat_table_inf = {
    .trk_num = 20,
    .start_sec_num = 1,
    .end_sec_num = 2,
    .step_rate = 1,
    .dden = false
};

static const bas_dir_table_t dir_table_inf = {
    .secs_per_trk = SECTORS_PER_TRACK,
    .sector_size = SECTOR_SIZE,
    .trk_num = 20,
    .start_sec_num = 6,
    .end_sec_num = 16,
    .secs_per_grp = SECTORS_PER_GROUP,
    .dden = false
};

typedef struct st_directory_l3_1s {
	uint8_t  name[8];
	uint8_t  ext[3];
	uint8_t  type;
	uint8_t  type2;
	uint8_t  type3;
	uint8_t  start_group;
	char reserved[17];
} directory_l3_1s_t;

#define BE_TO_LE_16(x) (((x) & 0xff) << 8 | ((x) >> 8) & 0xff)

static char file_name[16];

static const char type2_tbl[] = "BA";
static const char type3_tbl[] = "SR";

static bool read_fat_table(uint8_t drv)
{
    return bas_read_fat_table(drv, &fat_table_inf, bas_fat_table, sizeof(bas_fat_table));
}

static bool read_directory(uint8_t drv, directory_entry_cb_t callback_e, directory_finish_cb_t callback_f, void *user_data)
{
    return bas_read_directory(drv, &dir_table_inf, callback_e, callback_f, user_data);
}

static uint8_t next_group(uint8_t cur_group)
{
    return bas_fat_table[(cur_group + 5) & 0xff];
}

static bool count_groups_and_size(const bas_dir_table_t *inf, uint8_t start_group, int *group_count, int *sector_count)
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
            sec_count+=(int)inf->secs_per_grp;
        } else {
            sec_count+=((grp & 0xf) + 1);
            break;
        }
    }
    if (group_count) *group_count = grp_count;
    if (sector_count) *sector_count = sec_count;
    return sts;
}

/// @brief 
/// @return true is last
static bool files_entry_cb(int row, void *p_entry, void *user_data)
{
    const bas_dir_table_t *inf = (const bas_dir_table_t *)user_data;
    directory_l3_1s_t *entry = (directory_l3_1s_t *)p_entry;
    memcpy(file_name, entry->name, 8);
    file_name[8] = '\0';
//  memcpy(&file_name[9], entry->ext, 3);
//  file_name[12]=0;
    int grps, secs;
    count_groups_and_size(inf, entry->start_group, &grps, &secs);
    uint32_t size = SECTOR_SIZE * secs;
    printf("%s %d %c %c %2d (%6uB)  "
        ,file_name
        ,entry->type & 3
        ,type2_tbl[entry->type2 & 1]
        ,type3_tbl[entry->type3 & 1]
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

void fdc3inch_cmd_files(uint8_t drv)
{
    g_shell_cmd_fdc.verbose = g_shell_cmd_fdc.verbose_force;

    // read FAT
    if (!read_fat_table(drv)) {
        goto eof;
    }
    // read DIRECTORY
    if (!read_directory(drv, files_entry_cb, files_finish_cb, (void *)&dir_table_inf)) {
        goto eof;
    }
eof:
    fdc3inch_motor_off(drv);
}

/// @brief 
/// @return true is last
static bool load_entry_cb(int row, void *p_entry, void *user_data)
{
    bas_file_access_t *target = (bas_file_access_t *)user_data;
    directory_l3_1s_t *entry = (directory_l3_1s_t *)p_entry;
    if (strncasecmp(target->name, entry->name, 8) == 0
//   && strncasecmp(target->ext, entry->ext, 3) == 0
    ) {
        // match file
        target->match = 1;
        target->start_group = entry->start_group;
//      target->end_bytes = BE_TO_LE_16(entry->end_bytes);
        target->end_bytes = 0;
        return true;
    }
    return false;
}

static void get_track_and_sector_from_group(uint8_t group, int *track_number, int *side_number, int *sector_number)
{
    int sec = group * SECTORS_PER_GROUP;
    int trk = (sec / SECTORS_PER_TRACK);
    int sid = (trk % SIDES_PER_DISK);
    trk = (trk / SIDES_PER_DISK) + 1;
    if (trk >= 20) trk++;
    sec = (sec % SECTORS_PER_TRACK) + 1;

    if (track_number) *track_number = trk;
    if (side_number) *side_number = sid;
    if (sector_number) *sector_number = sec;
}

void fdc3inch_cmd_load(uint8_t drv, const char *file_name)
{
    bool rc;
    bas_file_access_t target;

    g_shell_cmd_fdc.verbose = g_shell_cmd_fdc.verbose_force;

    memset(&target, ' ', sizeof(target));
    if (file_name) {
        int len = (int)strlen(file_name);
        if (len > 8) len = 8;
        memcpy(target.name, file_name, len);
    }
    target.name[8] = 0;
    target.ext[3] = 0;
    target.match = 0;

//    split_filename(file_name, target.name, target.ext);

    // motor on
//    if (!fdc3inch_motor_on(drv, 0, false)) {
//        return;
//    }
    // read FAT
    if (!read_fat_table(drv)) {
        goto eof;
    }
    // search DIRECTORY
    if (!read_directory(drv, load_entry_cb, NULL, &target)) {
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
        next_grp = next_group(grp);
        if (grp == next_grp) {
            // Error
            printf("Dupricate Group Number.\n");
            break;
        }

        secs = SECTORS_PER_GROUP;
        if (next_grp >= 0xc0 && next_grp < 0xd0) {
            // last group
            secs = (next_grp & 0xf);
        }

        // access sector
        get_track_and_sector_from_group(grp, &trk, &sid, &start_sec);
        // seek
        rc = fdc3inch_seek(drv, trk, 1, NULL);
        if (!rc) {
            break;
        }
        // set side1 signal
        fdc3inch_motor_on(drv, sid, false);
        // read data
        for(int sec = start_sec; sec < start_sec + secs && !last; sec++) {
            rc = fdc3inch_read_data(drv, sid, sec, NULL);
            if (!rc) {
                last = true;
                break;
            }
        }

        grp = next_grp;
    }

eof:
    fdc3inch_motor_off(drv);
}

bool fdc3inch_cmd_boottest(int boot_type)
{
    uint8_t sts;
    uint8_t data;
    uint8_t ltar = 0;
    uint8_t sar = 0;
    uint8_t buffer[260];
    int pos = 0;

    if (boot_type != 0) {
        printf("CMD_FDC3: Boot type %d is not supported.\n", boot_type);
        return false;
    }

    g_shell_cmd_fdc.verbose = g_shell_cmd_fdc.verbose_force;

    // CTAR check
    bool rc = true;
    do {
        for(uint32_t i = 0; i < 256; i++) {
            wait_spin_us(3);
            parallel_write(FDC_3INCH_CTAR, (uint8_t)i);
            wait_spin_us(3);
            data = parallel_read(FDC_3INCH_CTAR);
            if (data != (uint8_t)i) {
                // unmatch
                rc = false;
                break;
            }
        }
        if (!rc) {
            printf("CTAR read and write failed.\n");
            break;
        }
        // motor on
        wait_spin_us(3);
        parallel_write(FDC_3INCH_UNIT, 0x81);
        for(int i=0; i<65536*4; i++) {
            wait_spin_us(7);
            sts = parallel_read(FDC_3INCH_STRA);
            if (sts & FDC_3INCH_STA_DREADY) break;
        }
        if (!(sts & FDC_3INCH_STA_DREADY)) {
            printf("Motor on but drive not ready.\n");
            rc = false;
            break;
        }
        // wait busy off
        do {
            wait_spin_us(3);
            sts = parallel_read(FDC_3INCH_STRA);
        } while(sts & FDC_3INCH_STA_BUSY);
        // restore
        wait_spin_us(5);
        parallel_write(FDC_3INCH_SUR, 0x66);
        wait_spin_us(3);
        parallel_write(FDC_3INCH_CMR, 0xc2);    // STZ
        uint32_t xaddr = 0x7000;
        ltar = 0;
        while(xaddr < 0x8000 && rc) {
            // wait busy off
            sar++;
            if (sar > 16) {
                ltar++;
                sar = 1;
                // seek
                wait_spin_us(5);
                parallel_write(FDC_3INCH_GCR, ltar);
                wait_spin_us(3);
                parallel_write(FDC_3INCH_CMR, 0xc3);    // SEK
            }
            pos = 0;
            do {
                wait_spin_us(3);
                sts = parallel_read(FDC_3INCH_STRA);
            } while(sts & FDC_3INCH_STA_BUSY);
            wait_spin_us(5);
            parallel_write(FDC_3INCH_LTAR, ltar);
            wait_spin_us(3);
            parallel_write(FDC_3INCH_SAR, sar);
            wait_spin_us(3);
            parallel_write(FDC_3INCH_CMR, 0xe4);    // SSR
            while(pos < 128) {
                wait_spin_us(3);
                sts = parallel_read(FDC_3INCH_STRA);
                if (!(sts & FDC_3INCH_STA_BUSY)) {
                    rc = false;
                    break;
                } else if (sts & FDC_3INCH_STA_DRQ) {
                    wait_spin_us(4);
                    data = parallel_read(FDC_3INCH_DIR);
                    buffer[pos] = data;
                    xaddr++;
                    pos++;
                }
            } 
            if (!rc) {
                printf("Read failed. trk:%u sec:%u sts:0x%02x\n", ltar, sar, sts);
                break;
            }
            // wait busy off
            do {
                wait_spin_us(3);
                sts = parallel_read(FDC_3INCH_STRA);
            } while(sts & FDC_3INCH_STA_BUSY);
            data = parallel_read(FDC_3INCH_ISR);
            sts = parallel_read(FDC_3INCH_STRA);
            if ((sts & 0x22) != 0) {
                printf("Read failed. trk:%u sec:%u sts:0x%02x isr:%02x\n", ltar, sar, sts, data);
                rc = false;
                break;
            } else if ((data & 0x08) != 0) {
                wait_spin_us(2);
                data = parallel_read(FDC_3INCH_STRB);
                printf("Read failed. trk:%u sec:%u sts:0x%02x stsb:%02x\n", ltar, sar, sts, data);
                rc = false;
                break;
            }
            if (rc) {
                rc = cli_d88_check_data(0, buffer, ltar, sar, pos, sts, xaddr);
            }
            if (ltar == 0 && sar >= 2) {
                ltar = 2;
                sar = 16;
            }
        }
    } while(0);

    // motor off
    wait_spin_us(2);
    fdc3inch_motor_off(0);

    printf("CMD_FDC3: Boot test %s.\n", rc ? "succeeded" : "failed");
    return rc;
}
