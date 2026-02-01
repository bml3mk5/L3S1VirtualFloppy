/** @file fdc_3inch.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-01-01
 * 
 * @copyright Copyright (c) Sasaji 2025
 */

#include <stdio.h>
#include <string.h>
#include <hardware/gpio.h>
#include "common.h"
#include "fdc_3inch.h"
#include "fdc_common.h"
#include "disk_d88.h"
#include "event.h"
#include "main.h"
#include "msg_bridge.h"
#include "pio_ctrls.h"
#include "display.h"
#include "display_disk.h"
#include "config.h"

#define HAS_HD6843 1

#ifndef OUT_DEBUG
#define OUT_DEBUG(...) printf(__VA_ARGS__)
#endif

#ifndef OUT_DEBUG_WR
#define OUT_DEBUG_WR(...)
#endif

#ifndef OUT_DEBUG_RD
#define OUT_DEBUG_RD(...)
#endif

#ifndef OUT_DEBUG_SK
#define OUT_DEBUG_SK(...)
#endif

#ifndef OUT_DEBUG_EV
#define OUT_DEBUG_EV(...)
#endif

#ifndef OUT_DEBUG_HL
#define OUT_DEBUG_HL(...)
#endif

#define DRIVE_MASK  (USE_FLOPPY_DISKS - 1)

#define SEEK_TIMEOUT    600000

#define MC6843_PARSE_BUFFER 8

/// @brief register number
enum FDC_3INCH_REGS {
    FDC_HD6843_DOR = 0,
    FDC_HD6843_DIR = 0,
    FDC_HD6843_CTAR = 1,
    FDC_HD6843_CMR = 2,
    FDC_HD6843_ISR = 2,
    FDC_HD6843_SUR = 3,
    FDC_HD6843_STRA = 3,
    FDC_HD6843_SAR = 4,
    FDC_HD6843_STRB = 4,
    FDC_HD6843_GCR = 5,
    FDC_HD6843_CCR = 6,
    FDC_HD6843_LTAR = 7,
    FDC_3INCH_UNIT = 8,
};

/// @brief event ids
enum EVENT_IDS {
    EVENT_SEEK_ZERO = 0,
    EVENT_SEEK,
    EVENT_SEEK_END,
    EVENT_SEARCH_DATA,
    EVENT_MULTI,
    EVENT_LOST,
    EVENT_DRQ,

    EVENT_MAX
};

/// @brief interrupt status
enum ISR_MASKS {
    FDC_ISR_STRB        = 0x08, ///< STRB
    FDC_ISR_STSREQ      = 0x04, ///< status sense request
    FDC_ISR_SEKCOMP     = 0x02, ///< seek complete
    FDC_ISR_RWCCOMP     = 0x01, ///< read/write command complete
    FDC_ISR_ALL_MASK    = 0x0f,
};

/// @brief status code A
enum STA_MASKS {
    FDC_STA_BUSY        = 0x80, ///< busy
    FDC_STA_INDEX       = 0x40, ///< index hole
    FDC_STA_TRACKNE     = 0x20, ///< track not equal
    FDC_STA_WRITEP      = 0x10, ///< write protect
    FDC_STA_TRACK00     = 0x08, ///< track zero
    FDC_STA_DREADY      = 0x04, ///< drive ready
    FDC_STA_DELETE      = 0x02, ///< delete data mark detected
    FDC_STA_DRQ         = 0x01, ///< data transfar request
};

/// @brief status code B
enum STB_MASKS {
    FDC_STB_HARDERR     = 0x80, ///< hard error
    FDC_STB_WRITEERR    = 0x40, ///< write error
    FDC_STB_FILEINO     = 0x20, ///< file inoperable
    FDC_STB_SEEKERR     = 0x10, ///< seek error
    FDC_STB_SECTNF      = 0x08, ///< sector address undeteced
    FDC_STB_DATANF      = 0x04, ///< data mark undetected
    FDC_STB_CRCERR      = 0x02, ///< crc error
    FDC_STB_DATAERR     = 0x01, ///< data transfar error
    FDC_STB_ALL_MASK    = 0xdf,
};

/// @brief command register
enum CMR_MASKS {
    FDC_CMR_FWF         = 0x10, ///< free format write flag
    FDC_CMR_DMA         = 0x20, ///< DMA flag
    FDC_CMR_ISR3MASK    = 0x40, ///< ISR3 interrupt mask
    FDC_CMR_FUNCMASK    = 0x80, ///< Function interrupt mask
};

/// @brief macro commands
enum CMD_MASKS {
    FDC_CMD_FFW_END = 0x00, ///< stop of multi sector write
    FDC_CMD_STZ     = 0x02, ///< seek track zero
    FDC_CMD_SEK     = 0x03, ///< seek
    FDC_CMD_SSR     = 0x04, ///< single sector read
    FDC_CMD_SSW     = 0x05, ///< single sector write
    FDC_CMD_RCR     = 0x06, ///< read CRC
    FDC_CMD_SWD     = 0x07, ///< single sector write with delete data mark
    FDC_CMD_FFR     = 0x0a, ///< free format read
    FDC_CMD_FFW     = 0x0b, ///< free format write
    FDC_CMD_MSR     = 0x0c, ///< multi sector read
    FDC_CMD_MSW     = 0x0d, ///< multi sector write
};

// @brief unit sel register
enum UNITSEL_MASKS {
    FDC_UNIT_MOTOR = 0x80,  ///< motor on
    FDC_UNIT_DRIVES = 0x0f, ///< select drive
};

typedef struct {
    // mc6843
    // registor
//  uint8_t dor;        // reg0   w: disk write operation
    uint8_t dir;        // reg0 r  : disk read operation
    uint8_t ctar;       // reg1 r/w: current track (8bit)
    uint8_t cmr;        // reg2   w: command
    uint8_t isr;        // reg2 r  : interrupt status
    uint8_t sur;        // reg3   w: set-up
    uint8_t stra;       // reg3 r  : status a
    uint8_t strb;       // reg4 r  : status b
    uint8_t sar;        // reg4   w: sector address (5bit)
    uint8_t gcr;        // reg5   w: general count (track  or  sector) (7bit)
    uint8_t ccr;        // reg6   w: crc control (2bit)
    uint8_t ltar;       // reg7   w: logical address track (=track destination) (7bit)

    uint8_t pre_stra;
    uint8_t pre_strb;

    uint8_t drv_sts;    // drive status b7:drive not ready b6:write protected b5:head loaded b2:track0 b1:index hole
    uint8_t now_irq;
//    uint8_t now_drq;
    uint8_t clk_num;

    // status
    struct {
        uint8_t now_search : 1; // now searching?
        uint8_t now_seek : 1;       // now seeking?
        uint8_t after_seek : 1;
        uint8_t seekvct : 1;
        uint8_t head_load : 1;
//      // for unit sel
//      uint8_t motor : 1;
        //
        uint8_t now_reset : 1;
        uint8_t wait_ack : 1;
    };

    // unit sel
    uint8_t drv_num;
    uint8_t density;    // 0:single density(FM) 1:double density(MFM)
    uint8_t sid_num[MAX_DRIVES];
//  uint8_t nmi_mask;

    int stepcnt;    // step count
//  bool now_seek;
//  bool now_search;
//  bool head_load;     // now head load

    int data_idx;    // current read/write position in data
    int sec_pos;
    int seektrk;

    int cmd_time;
    uint64_t drq_time;

    fdc_common_event_t event_info[EVENT_MAX];
    simple_fifo_t event_fifo;
    uint32_t event_fifo_buffer[8];

//  // for parse format
//  uint8_t parse_clk_buf[MC6843_PARSE_BUFFER];
//  uint8_t parse_dat_buf[MC6843_PARSE_BUFFER];
//  uint8_t parse_dat;
//  int   parse_idx;
//  int   ffw_phase;

} FDC_3INCH;

static FDC_3INCH g_fdc_3inch;
static FDC_3INCH *dev = &g_fdc_3inch;

#define FLG_DELAY_FDSEEK (config_get_seek_track() != 0)
#define FLG_DELAY_FDSEARCH (config_get_search_sector() != 0)
#define FLG_DELAY_DATAREQ (config_get_data_request() != 0)

#define USE_LOST_EVENT 1

#define LOST_TIME_AFTER_SEARCH 1000000
#define LOST_TIME_NEXT_DRQ     1000000
#define EVENT_NEXT_DRQ_TIME     1

//--------------------------------------------------------------------

static void fdc_task();

static void fdc_cancel_event(int event);
static void fdc_register_event(int event, uint32_t usec);
static int64_t fdc_event_callback(alarm_id_t id, void *user_data);

static void fdc_register_seek_zero_event();
static void fdc_event_seek_zero(uint32_t event_data);
static void fdc_register_seek_event();
static void fdc_event_seek(uint32_t event_data);
static void fdc_register_seek_end_event();
static void fdc_event_seek_end(uint32_t event_data);
static void fdc_register_search_data_event(uint32_t usec);
static void fdc_event_search_data(uint32_t event_data);
static void fdc_register_multi_event(uint32_t usec);
static void fdc_event_multi(uint32_t event_data);
static void fdc_register_drq_event();
static void fdc_event_drq(uint32_t event_data);
static void fdc_register_lost_event(uint32_t usec);
static void fdc_event_lost(uint32_t event_data);

static void fdc_irq_update();
static void fdc_rwcmd_end();

static void fdc_cmd_STZ();
static void fdc_cmd_SEK();
static void fdc_cmd_SSR();
static void fdc_cmd_SSW();
static void fdc_cmd_RCR();
static void fdc_cmd_SWD();
static void fdc_cmd_FFR();
static void fdc_cmd_FFW();
static void fdc_cmd_MSR();
static void fdc_cmd_MSW();
static void fdc_cmd_FFW_END();

static void fdc_cmd_read();
static void fdc_cmd_write();

static uint32_t fdc_set_delay(uint8_t);

//static void fdc_event_seek(int);
//static void fdc_event_search(int);
//static void fdc_event_multi(int);
//static void fdc_event_lost(int);
//static void fdc_event_drq(int);

static void fdc_write_data_reg(uint8_t);
static void fdc_write_io_unitsel(uint8_t data);

static void fdc_post_read_data();
//static void fdc_update_stra();

//static void fdc_find_track();
static bool fdc_search_sector();

//static void fdc_find_sector(int);

static void fdc_send_dir();
static void fdc_send_dir_sync();
static void fdc_send_ctar();
static void fdc_send_isr();
static void fdc_send_stra();
static void fdc_send_strb();
static void fdc_send_status();

//static void fdc_set_status_callback(disk_drive_status_t val);
//static void fdc_clr_status_callback(disk_drive_status_t val);
static void fdc_motor_on_callback(void);
static void fdc_motor_off_callback(void);
static void fdc_index_on_callback(void);
static void fdc_index_off_callback(void);

// irq
static void fdc_set_irq(uint8_t val);
// drq (data transfer request)
static void fdc_set_drq();
static void fdc_clr_drq();

// for free format write
static void fdc_parse_twice_format(uint8_t data);
static void fdc_parse_plane_format(uint8_t data);
//static void fdc_parse_ibm3740_format(uint8_t data);
//static void fdc_write_ibm3740_format();
//--------------------------------------------------------------------

/// @brief initialize
void fdc_3inch_init()
{
    memset(&g_fdc_3inch, 0, sizeof(g_fdc_3inch));

//  disk_drive_set_status_callback = &fdc_set_status_callback;
//  disk_drive_clr_status_callback = &fdc_clr_status_callback;
//  dev->sid_num = 0;

    for(int i=0; i<EVENT_MAX; i++) {
        dev->event_info[i].id = -1;
        dev->event_info[i].callback = NULL;
    }
    dev->event_info[EVENT_SEEK_ZERO].callback = &fdc_event_seek_zero;
    dev->event_info[EVENT_SEEK].callback = &fdc_event_seek;
    dev->event_info[EVENT_SEEK_END].callback = &fdc_event_seek_end;
    dev->event_info[EVENT_SEARCH_DATA].callback = &fdc_event_search_data;
    dev->event_info[EVENT_MULTI].callback = &fdc_event_multi;
    dev->event_info[EVENT_LOST].callback = &fdc_event_lost;
    dev->event_info[EVENT_DRQ].callback = &fdc_event_drq;

    fifo_init(&dev->event_fifo, dev->event_fifo_buffer, sizeof(dev->event_fifo_buffer)/sizeof(dev->event_fifo_buffer[0]), sizeof(dev->event_fifo_buffer[0]));

    fdc_3inch_reset();
}

void fdc_3inch_reset()
{
    dev->stepcnt = 0;
    dev->now_seek = 0;
    dev->now_search = 0;
    dev->head_load = 0;

    //
//  memset(dev->parse_clk_buf, 0, MC6843_PARSE_BUFFER);
//  memset(dev->parse_dat_buf, 0, MC6843_PARSE_BUFFER);
//  dev->parse_dat = 0;
//  dev->parse_idx = 0;
//  dev->ffw_phase = 0;

    // reset registers
    dev->cmr &= 0xf0;
    dev->isr = 0;
    dev->stra &= 0x5c;
    dev->sar = 0;
    dev->strb &= 0x20;

    fdc_irq_update();
    fdc_common_clr_busy();

    // send to core1
    fdc_send_status();
    fdc_send_ctar();
    fdc_send_dir();

    fdc_3inch_unitsel_reset();
}

void fdc_3inch_unitsel_reset()
{
    for(int i=0; i<EVENT_MAX; i++) {
        fdc_cancel_event(i);
    }
    fifo_clear(&dev->event_fifo);
    dev->drv_num = 0;
    dev->density = 0;
    dev->wait_ack = 0;
    disk_drive_motor_off();
}

static void __no_inline_not_in_flash_func(fdc_task)()
{
    while (fifo_is_not_empty(&dev->event_fifo)) {
        uint32_t event_data = fifo_pop32(&dev->event_fifo);
        if (dev->event_info[event_data >> 8].callback) {
            dev->event_info[event_data >> 8].callback(event_data);
        }
    }
}

//--------------------------------------------------------------------

/// @brief cancel event
static void __no_inline_not_in_flash_func(fdc_cancel_event)(int event)
{
    event_cancel_event(&dev->event_info[event].id);
    OUT_DEBUG_EV(_T("FDC3: Cancel EVENT:%d\n"), event);
}

static void __no_inline_not_in_flash_func(fdc_register_event)(int event, uint32_t usec)
{
    fdc_cancel_event(event);
    dev->event_info[event].id = event_register_event(usec, fdc_event_callback, (event << 8));
    OUT_DEBUG_EV(_T("FDC3: Regist EVENT:%d id:%08x usec:%d\n"), event, dev->register_id[event], usec);
}

static int64_t __no_inline_not_in_flash_func(fdc_event_callback)(alarm_id_t id, void *user_data)
{
    uint32_t event_data = (uint32_t)user_data;
    dev->event_info[event_data >> 8].id = -1;
    fifo_push32(&dev->event_fifo, event_data);
    return 0;
}

//--------------------------------------------------------------------

static void fdc_register_seek_zero_event()
{
    fdc_register_event(EVENT_SEEK_ZERO, FLG_DELAY_FDSEEK ? 600 : fdc_set_delay(0xf0));
    dev->now_seek = 1;
}

static void fdc_event_seek_zero(uint32_t event_data)
{
    int trk00 = disk_d88_is_track0(dev->drv_num);
    if (trk00) {
        dev->stra |= FDC_STA_TRACK00;
    } else {
        dev->stra &= ~FDC_STA_TRACK00;
    }

    dev->stepcnt--;
    if (dev->stepcnt < 0) {
        // seek error
        if (!trk00) {
            dev->strb |= FDC_STB_SEEKERR;
        }
        OUT_DEBUG_SK(_T("MC6843: seek_zero stepcnt0 strb:%02x\n"), dev->strb);
    } else {
        // seek
        if (trk00) {
            dev->ctar = 0;
        } else {
            disk_d88_step_out(dev->drv_num, dev->sid_num[dev->drv_num]);
            dev->ctar--;
        }
        OUT_DEBUG_SK(_T("MC6843: seek_zero gcr:%02x ctar:%02x\n"), dev->gcr, dev->ctar);
        // seek next track
        fdc_register_seek_zero_event();
        fdc_send_stra();
        fdc_send_ctar();
        return;
    }

    // seek complete
    dev->now_seek = 0;

    // update state
    dev->ctar = 0;
    dev->gcr = 0;
    dev->sar = 0;

    fdc_register_seek_end_event();
    fdc_send_ctar();
}

static void fdc_register_seek_event()
{
    fdc_register_event(EVENT_SEEK, FLG_DELAY_FDSEEK ? 600 : fdc_set_delay(0xf0));
    dev->now_seek = 1;
}

static void fdc_event_seek(uint32_t event_data)
{
    int trk00 = disk_d88_is_track0(dev->drv_num);
    if (trk00) {
        dev->stra |= FDC_STA_TRACK00;
    } else {
        dev->stra &= ~FDC_STA_TRACK00;
    }

    dev->stepcnt--;
    if (dev->stepcnt < 0) {
        // seek error
        dev->strb |= FDC_STB_SEEKERR;
        OUT_DEBUG_SK(_T("MC6843: seek stepcnt0 strb:%02x\n"), dev->strb);
    } else {
        // seek
        if (dev->gcr > dev->ctar) {
            disk_d88_step_in(dev->drv_num, dev->sid_num[dev->drv_num]);
            dev->ctar++;
        } else if (dev->gcr < dev->ctar) {
            disk_d88_step_out(dev->drv_num, dev->sid_num[dev->drv_num]);
            dev->ctar--;
        }
        OUT_DEBUG_SK(_T("MC6843: seek gcr:%02x ctar:%02x\n"), dev->gcr, dev->ctar);
        if(dev->gcr != dev->ctar) {
            // seek next track
            fdc_register_seek_event();
            fdc_send_stra();
            fdc_send_ctar();
            return;
        }
    }

    // update state
    dev->ctar = dev->gcr;
    dev->gcr = 0;
    dev->sar = 0;

    fdc_register_seek_end_event();
    fdc_send_ctar();
}

static void fdc_register_seek_end_event()
{
    fdc_register_event(EVENT_SEEK_END, FLG_DELAY_FDSEEK ? 600 : fdc_set_delay(0xff));
}

static void fdc_event_seek_end(uint32_t event_data)
{
    // seek complete
    dev->now_seek = 0;

    dev->isr |= FDC_ISR_SEKCOMP;    // set Settling Time Complete
    dev->stra &= ~FDC_STA_BUSY; // clear Busy
    fdc_common_clr_busy();
    dev->cmr  &=  0xf0; // clear command

    fdc_send_status();
    fdc_irq_update();
}

static void fdc_register_search_data_event(uint32_t usec)
{
    fdc_register_event(EVENT_SEARCH_DATA, usec);
    dev->now_search = 1;
}

static void fdc_event_search_data(uint32_t event_data)
{
    dev->now_search = 0;
    dev->stra = dev->pre_stra;
    dev->strb = dev->pre_strb;

    // track not found
    if ((dev->stra & FDC_STA_TRACKNE) || dev->strb != 0) {
        fdc_rwcmd_end();
        return;
    }

    // start dma
    if ((dev->cmr & 0x0f) != FDC_CMD_RCR) {
        fdc_set_drq();
        // if no DMA then Status Sense is set.
        if (!(dev->cmr & FDC_CMR_DMA)) {
            dev->isr |= FDC_ISR_STSREQ;
        }
        fdc_send_stra();
        fdc_irq_update();
#ifdef USE_LOST_EVENT
        fdc_register_lost_event(LOST_TIME_AFTER_SEARCH);
#endif
    } else {
        // RCR command
        fdc_post_read_data();
    }
}

static void fdc_register_multi_event(uint32_t usec)
{
    fdc_register_event(EVENT_MULTI, usec);
}

static void fdc_event_multi(uint32_t event_data)
{
    dev->sar++;
    dev->gcr--;
    if((dev->cmr & 0x0f) == FDC_CMD_MSW) {
        fdc_cmd_write();
    } else {
        fdc_cmd_read();
    }
}

static void __not_in_flash_func(fdc_register_drq_event)()
{
    if (!FLG_DELAY_DATAREQ) {
        int usec = 64;
        uint32_t sub = (uint32_t)(to_us_since_boot(get_absolute_time()) - dev->drq_time);
        if (usec > sub + 16) {
            usec -= sub;
        } else {
            usec = 16;
        }
#ifdef USE_LOST_EVENT
        fdc_cancel_event(EVENT_LOST);
#endif
        fdc_register_event(EVENT_DRQ, usec);
    }
}

static void __not_in_flash_func(fdc_register_drq_expand_event)()
{
    fdc_register_event(EVENT_DRQ, 16);
}

static void __not_in_flash_func(fdc_fire_drq)()
{
    fdc_set_drq();
    fdc_send_stra();
#ifdef USE_LOST_EVENT
    fdc_register_lost_event(LOST_TIME_NEXT_DRQ);
#endif
}

static void __no_inline_not_in_flash_func(fdc_event_drq)(uint32_t event_data)
{
    if(!(dev->stra & FDC_STA_BUSY)) return;

    if (dev->wait_ack) {
//#ifdef PIO_PARALLEL_DEBUG_PIN
//        gpio_set_mask(1 << PIO_PARALLEL_DEBUG_PIN);
//#endif
        // wait until the data is stored in the register 
        fdc_register_drq_expand_event();
    } else {
        fdc_fire_drq();
//#ifdef PIO_PARALLEL_DEBUG_PIN
//        gpio_clr_mask(1 << PIO_PARALLEL_DEBUG_PIN);
//#endif
    }
}

static void __not_in_flash_func(fdc_register_lost_event)(uint32_t usec)
{
    fdc_register_event(EVENT_LOST, usec);
}

static void fdc_event_lost(uint32_t event_data)
{
    if((dev->cmr & 0xf) != FDC_CMD_FFW && (dev->stra & FDC_STA_BUSY)) {
        dev->strb |= FDC_STB_DATAERR;
        OUT_DEBUG_EV(_T("MC6843: lost data strb:%02x\n"),dev->strb);
        fdc_rwcmd_end();
    }
}

//--------------------------------------------------------------------

static void __no_inline_not_in_flash_func(fdc_write_io)(uint32_t addr, uint8_t data)
{
#ifdef _DEBUG
    static const _TCHAR *cmdname[16]={ _T("?"), _T("?"), _T("STZ"), _T("SEK"), _T("SSR"), _T("SSW"), _T("RCR"), _T("SWD")
                                    , _T("?"), _T("?"), _T("FFR"), _T("FFW"), _T("MSR"), _T("MSW"), _T("?"), _T("?") };
#endif
    switch(addr & 15) {
    case FDC_HD6843_DOR:    // DOR
        OUT_DEBUG_WR(_T("MC6843: write DOR  d:%02x\n"), data);
        fdc_write_data_reg(data);
        break;
    case FDC_HD6843_CTAR:   // CTAR : current track (8bit)
//      ctar = data & 0x7f;
        OUT_DEBUG_WR(_T("MC6843: write CTAR d:%02x -> ctar:%02x\n"),data,dev->ctar);
        dev->ctar = data;
//      fdc_send_ctar();
        break;
    case FDC_HD6843_CMR:    // CMR : command
        dev->stra &= ~(FDC_STA_BUSY);
        fdc_clr_drq();

#ifdef _DEBUG
        OUT_DEBUG_WR(_T("MC6843: write CMR  %s d:%02x ctar:%02x cmr:%02x sur:%02x sar:%02x gcr:%02x ccr:%02x ltar:%02x isr:%02x stra:%02x strb:%02x")
        ,cmdname[data & 15]
        ,data
        ,ctar,cmr,sur,sar,gcr,ccr,ltar,isr,stra,strb);
#endif
        switch(data & 15) {
        case FDC_CMD_FFW_END:   // stop ffw command
            fdc_cmd_FFW_END();
            break;
        case FDC_CMD_STZ:   // seek track zero
            fdc_cmd_STZ();
            break;
        case FDC_CMD_SEK:   // seek
            fdc_cmd_SEK();
            break;
        case FDC_CMD_SSR:   // single sector read
            fdc_cmd_SSR();
            break;
        case FDC_CMD_SSW:   // single sector write
            fdc_cmd_SSW();
            break;
        case FDC_CMD_RCR:   // read CRC
            fdc_cmd_RCR();
            break;
        case FDC_CMD_SWD:   // single sector write with delete data mark
            fdc_cmd_SWD();
            break;
        case FDC_CMD_FFR:   // free format read
            fdc_cmd_FFR();
            break;
        case FDC_CMD_FFW:   // free format write
            fdc_cmd_FFW();
            break;
        case FDC_CMD_MSR:   // multi sector read
            fdc_cmd_MSR();
            break;
        case FDC_CMD_MSW:   // multi sector write
            fdc_cmd_MSW();
            break;
        default:
            // unknown
            fdc_send_stra();
            break;
        }
        dev->cmr = data;
        break;
    case FDC_HD6843_SUR:    // SUR
        OUT_DEBUG_WR(_T("MC6843: write SUR  d:%02x sur:%02x\n"),data,dev->sur);
        dev->sur = data & 0xff;
        break;
    case FDC_HD6843_SAR:    // SAR
        OUT_DEBUG_WR(_T("MC6843: write SAR  d:%02x sar:%02x\n"),data,dev->sar);
        dev->sar = data & 0x1f;
        break;
    case FDC_HD6843_GCR:    // GCR
        OUT_DEBUG_WR(_T("MC6843: write GCR  d:%02x gcr:%02x\n"),data,dev->gcr);
        dev->gcr = data & 0x7f;
        break;
    case FDC_HD6843_CCR:    // CCR
        OUT_DEBUG_WR(_T("MC6843: write CCR  d:%02x ccr:%02x\n"),data,dev->ccr);
        dev->ccr = data & 0x03;
        break;
    case FDC_HD6843_LTAR:   // LTAR
        OUT_DEBUG_WR(_T("MC6843: write LTAR d:%02x ltar:%02x\n"),data,dev->ltar);
        dev->ltar = data & 0x7f;
        break;
    case FDC_3INCH_UNIT:    // unit sel
        fdc_write_io_unitsel(data);
        break;
    }
}

static uint8_t fdc_read_io(uint32_t addr)
{
    uint8_t data = 0;

    switch(addr & 15) {
    case FDC_HD6843_DIR:    // DIR
        data = dev->dir;
        break;
    case FDC_HD6843_CTAR:   // CTAR : current track (8bit)
        data = dev->ctar;
        break;
    case FDC_HD6843_ISR:    // ISR
        data = dev->isr;
        break;
    case FDC_HD6843_STRA:   // STRA
        data = dev->stra;
        break;
    case FDC_HD6843_STRB:   // STRB
        data = dev->strb;
        break;
    }

    return data;
}

//--------------------------------------------------------------------

static void __not_in_flash_func(fdc_pre_read_data)()
{
    // read or multisector read
    disk_d88_read_data(dev->drv_num, &dev->dir);
    fdc_send_dir_sync();
    dev->data_idx++;
}

static void fdc_pre_read_track()
{
    // TODO: free sector read is not implemented.
    dev->dir = 0xff;
    fdc_send_dir_sync();
    dev->data_idx++;
}

static void __not_in_flash_func(fdc_post_read_data)()
{
    int cmd = dev->cmr & 0x0f;

    fdc_clr_drq();
    if(dev->data_idx >= 128) {
        // last data
#ifdef USE_LOST_EVENT
        fdc_cancel_event(EVENT_LOST);
#endif
        if(cmd == FDC_CMD_SSR || cmd == FDC_CMD_RCR || (cmd == FDC_CMD_MSR && dev->gcr == 0)) {
            // single sector
            OUT_DEBUG_RD(_T("MC6843: READ : END OF SECTOR\n"));
            dev->isr &= ~FDC_ISR_STSREQ;
            fdc_rwcmd_end();
        } else {
            // multisector
            OUT_DEBUG_RD(_T("MC6843: READ : END OF SECTOR (SEARCH NEXT)\n"));
            fdc_send_stra();
            fdc_register_multi_event(64);
        }
    } else {
        // next data
        fdc_send_stra();
        fdc_pre_read_data();
        fdc_register_drq_event();
    }
}

static void fdc_post_read_track()
{
    int cmd = dev->cmr & 0x0f;

    // next data
    fdc_clr_drq();
    fdc_send_stra();
    fdc_pre_read_track();
    fdc_register_drq_event();
}

static void __no_inline_not_in_flash_func(fdc_post_read)(uint32_t addr, uint8_t data)
{
    int cmd = dev->cmr & 0x0f;

    switch(addr) {
    case FDC_HD6843_DIR:
        if(cmd == FDC_CMD_SSR || cmd == FDC_CMD_MSR) {
            fdc_post_read_data();
        } else if (cmd == FDC_CMD_FFR) {
            fdc_post_read_track();
        }
        break;
    case FDC_HD6843_ISR:
        // clear status without strb
        dev->isr &= FDC_ISR_STRB;
        OUT_DEBUG_EV(_T("MC6843: read isr a:%04x d:%02x isr:%02x\n")
            ,addr,data,dev->isr);
        fdc_irq_update();
        break;
    case FDC_HD6843_STRB:
        // clear status b
        dev->strb &= ~FDC_STB_ALL_MASK;
        OUT_DEBUG_EV(_T("MC6843: read strb a:%04x d:%02x strb:%02x\n")
            ,addr,data,dev->strb);
        fdc_irq_update();
        break;
    default:
        break;
    }
    
}

static void __no_inline_not_in_flash_func(fdc_wrote_ack)(uint32_t addr, uint8_t data)
{
    dev->wait_ack = 0;
    if (FLG_DELAY_DATAREQ && !dev->now_search) {
        fdc_fire_drq();
    }
}

static bool MY_CORE1_FUNC(fdc_post_read_tightly)(uint8_t addr)
{
    bool sts = false;
    switch(addr) {
    case FDC_HD6843_DIR:
        sts = true;
        // clear DRQ
        g_pio_parallel_read.odata[FDC_HD6843_STRA] &= ~FDC_STA_DRQ;
        break;
    case FDC_HD6843_ISR:
        sts = true;
        // clear status without strb
        g_pio_parallel_read.odata[FDC_HD6843_ISR] &= FDC_ISR_STRB;
        break;
    case FDC_HD6843_STRB:
        sts = true;
        // clear status b
        g_pio_parallel_read.odata[FDC_HD6843_STRB] &= ~FDC_STB_ALL_MASK;
        // clear ISR flag
        g_pio_parallel_read.odata[FDC_HD6843_ISR]  &= ~FDC_ISR_STRB;
        break;
    default:
        break;
    }
    return sts;
}

static void MY_CORE1_FUNC(fdc_post_write_tightly)(uint8_t addr)
{
    switch(addr) {
    case FDC_HD6843_DOR:
        // clear DRQ
        g_pio_parallel_read.odata[FDC_HD6843_STRA] &= ~FDC_STA_DRQ;
        break;
    case FDC_HD6843_CMR:
        // command
        g_pio_parallel_read.odata[FDC_HD6843_STRA] |= FDC_STA_BUSY;
        break;
    case FDC_HD6843_CTAR:
        // copy data to register
        g_pio_parallel_read.odata[FDC_HD6843_CTAR] = g_pio_parallel_write.idata;
        break;
    default:
        break;
    }
}

static void MY_CORE1_FUNC(fdc_notice_tightly)(uint32_t data)
{
}

// ----------------------------------------------------------------------------
static void fdc_write_data_reg(uint8_t data)
{
//  uint8_t datareg = data;
    int cmd = dev->cmr & 0x0f;

//  if (cmd == 0) {
//      // free format write end
//      dev->stra &= ~FDC_STA_BUSY;
//      dev->stra &= ~FDC_STA_DRQ;
//      return;
//  }
    if((dev->stra & FDC_STA_DRQ) && !dev->now_search) {
        if(cmd == FDC_CMD_SSW || cmd == FDC_CMD_MSW || cmd == FDC_CMD_SWD) {

            fdc_clr_drq();

            // write or multisector write
            // write sector 128 bytes max
            if(disk_d88_is_write_protected(dev->drv_num)) {
                // write protect
                // set error flag but continues executing the command
                dev->strb |= FDC_STB_WRITEERR;
//              OUT_DEBUG_WR(_T("MC6843: chg_stat stra:%02x strb:%02x\n"),dev->stra,dev->strb);
            } else {
                disk_d88_write_data(dev->drv_num, data);
                // set deleted mark
                if (cmd == FDC_CMD_SWD) {
                    disk_d88_set_deleted_mark(dev->drv_num, 1);
                }
            }

            dev->data_idx++;
            if(dev->data_idx >= 128) {
                // last data
#ifdef USE_LOST_EVENT
                fdc_cancel_event(EVENT_LOST);
#endif
                if(cmd == FDC_CMD_SSW || cmd == FDC_CMD_SWD || (cmd == FDC_CMD_MSW && dev->gcr == 0)) {
                    // single sector
                    OUT_DEBUG_WR(_T("MC6843: WRITE : END OF SECTOR\n"));
                    dev->isr &= ~FDC_ISR_STSREQ;
                    fdc_rwcmd_end();
                } else {
                    // multisector
                    OUT_DEBUG_WR(_T("MC6843: WRITE : END OF SECTOR (SEARCH NEXT)\n"));
                    fdc_send_stra();
                    fdc_register_multi_event(64);
                }
            } else {
                // next data
                fdc_send_stra();
                fdc_send_dir_sync();
                fdc_register_drq_event();
            }

        } else if (cmd == FDC_CMD_FFW) {

            fdc_clr_drq();

            if(disk_d88_is_write_protected(dev->drv_num)) {
                // write protect
                // set error flag but continues executing the command
                dev->strb |= FDC_STB_WRITEERR;
//              OUT_DEBUG_WR(_T("MC6843: chg_stat stra:%02x strb:%02x\n"),dev->stra,dev->strb);
            }

//          // free format write
//          if (dev->cmr & 0x10) {
//              fdc_parse_twice_format(data);
//          } else {
//              fdc_parse_plane_format(data);
//          }
//          parse_ibm3740_format(data);
//          write_ibm3740_format();

            fdc_send_dir_sync();
            fdc_register_drq_event();
        }
    }
    return;
}

static void __not_in_flash_func(fdc_write_io_unitsel)(uint8_t data)
{
    uint8_t drv_bits = 0x08;
    for(int i=3; i>=0; i--) {
        if (data & drv_bits) {
            dev->drv_num = (uint8_t)i;
        }
        drv_bits >>= 1;
    }
//    dev->sid_num = 0;
//    dev->density = 0;
//  dev->nmi_mask = 0;
    if (data & FDC_UNIT_MOTOR) {
//        dev->motor = 1;
        disk_drive_motor_on(dev->drv_num);
    } else {
        disk_drive_motor_off();
//        dev->motor = 0;
    }
    if (disk_d88_is_write_protected(dev->drv_num)) {
        dev->stra |= FDC_STA_WRITEP;
    } else {
        dev->stra &= ~FDC_STA_WRITEP;
    }
    fdc_send_stra();
}

// ----------------------------------------------------------------------------
#if 0
static void fdc_update_stra()
{
//      int cmd = cmr & 0x0f;

        // status reg
        // disk not inserted, motor stop
        if(disk_d88_is_not_ready(dev->drv_num)) {
            dev->stra &= ~FDC_STA_DREADY;
        } else {
            dev->stra |= FDC_STA_DREADY;
        }
        // write protect
        if(disk_d88_is_write_protected(dev->drv_num)) {
            dev->stra |= FDC_STA_WRITEP;
        } else {
            dev->stra &= ~FDC_STA_WRITEP;
        }

        // track0
        if(disk_d88_is_track0(dev->drv_num)) {
            dev->stra |= FDC_STA_TRACK00;
        }
        else {
            dev->stra &= ~FDC_STA_TRACK00;
        }

        // index hole
        if(disk_drive_is_index_hole()) {
            dev->stra |= FDC_STA_INDEX;
        } else {
            dev->stra &= ~FDC_STA_INDEX;
        }

        return;
}
#endif

// ----------------------------------------------------------------------------
// irq
// ----------------------------------------------------------------------------
static void __not_in_flash_func(fdc_set_irq)(uint8_t val)
{
    if (dev->now_irq != val) {
        dev->now_irq = val;
        if (val) {
            fdc_common_set_irq();
        } else {
            fdc_common_clr_irq();
        }
    }
    OUT_DEBUG_EV(_T("MC6843: set_irq:%d\n"),val ? 1 : 0);
}

static void __not_in_flash_func(fdc_set_drq)()
{
    dev->stra |= FDC_STA_DRQ;
#ifdef USE_DRQ_PIN
    fdc_common_set_drq();
#endif
    dev->drq_time = to_us_since_boot(get_absolute_time());
}

static void __not_in_flash_func(fdc_clr_drq)()
{
    dev->stra &= ~FDC_STA_DRQ;
#ifdef USE_DRQ_PIN
    fdc_common_clr_drq();
#endif
}

// ----------------------------------------------------------------------------
// called at end of command
static void __not_in_flash_func(fdc_rwcmd_end)()
{
    dev->isr  |= FDC_ISR_RWCCOMP;   // set Macro Command Complete
    dev->stra &= ~FDC_STA_BUSY; // clear Busy
    fdc_common_clr_busy();
    dev->cmr  &=  0xf0; // clear command
    fdc_irq_update();
    fdc_send_status();
    OUT_DEBUG_HL(_T("MC6843: event_search HEAD UNLOAD\n"));
    dev->head_load = 0;
}

// called after ISR or STRB has changed
static void __not_in_flash_func(fdc_irq_update)()
{
    uint8_t irq = 0;
    uint8_t isr_mask = 0x87;

    // ISR bit3
    if (dev->strb)
        dev->isr |= FDC_ISR_STRB;
    else
        dev->isr &= ~FDC_ISR_STRB;
#ifdef HAS_HD6843
    if (dev->strb & ~FDC_STB_DATAERR)
        dev->isr |= (FDC_ISR_STRB << 4);
    else
        dev->isr &= ~(FDC_ISR_STRB << 4);
#endif
    // interrupts
    if (dev->cmr & FDC_CMR_FUNCMASK) isr_mask = 0;
#ifdef HAS_HD6843
    if (dev->cmr & FDC_CMR_ISR3MASK) isr_mask &= ~(FDC_ISR_STRB << 4);
#else
    if (dev->cmr & FDC_CMR_ISR3MASK) isr_mask &= ~FDC_ISR_STRB;
#endif
    if (dev->cmr & FDC_CMR_DMA) isr_mask &= ~FDC_ISR_STSREQ;
    if (dev->isr & isr_mask) irq = 1;

    dev->isr &= FDC_ISR_ALL_MASK;

    // IRQ interrupt
    fdc_send_isr();
    fdc_set_irq(irq);
}

// set delay
static uint32_t __not_in_flash_func(fdc_set_delay)(uint8_t mask)
{
    uint8_t setupreg = dev->sur & mask;
//  uint32_t delay = FLG_DELAY_FDSEEK ? 0 : ((setupreg & 0xf0) >> 4) * 1024 + (setupreg & 0x0f) * 4096;
    uint32_t delay = FLG_DELAY_FDSEEK ? 0 : ((uint32_t)(setupreg & 0xf0) << 6) + ((uint32_t)(setupreg & 0x0f) << 12);
    if (delay < 64) delay = 64;

    OUT_DEBUG_EV(_T("MC6843: set_delay:%u\n"), delay);

    return delay;
}

// ----------------------------------------------------------------------------
/// Seek Track Zero
static void fdc_cmd_STZ()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    dev->gcr = 0;
#ifdef HAS_HD6843
    dev->ctar = 82;
    dev->stepcnt = 82;
#else
    dev->ctar = 83;
    dev->stepcnt = 83;
#endif

    OUT_DEBUG_HL(_T("MC6843: cmd_STZ HEAD UNLOAD\n"));
    dev->head_load = 0;

    fdc_send_stra();
    fdc_register_seek_zero_event();
}

/// Seek
static void fdc_cmd_SEK()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
#ifdef HAS_HD6843
    dev->stepcnt = 82;
#else
    dev->stepcnt = 83;
#endif

    OUT_DEBUG_HL(_T("MC6843: cmd_SEK HEAD UNLOAD\n"));
    dev->head_load = 0;

    fdc_send_stra();
    fdc_register_seek_event();
}

/// Single Sector Read
static void fdc_cmd_SSR()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    dev->gcr = 0;
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);

    fdc_cmd_read();
}

/// Multi Sector Read
static void fdc_cmd_MSR()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);

    fdc_cmd_read();
}

static void fdc_cmd_read()
{
    uint32_t usec = fdc_set_delay(dev->head_load ? 0x00 : 0x0f);
    dev->cmd_time = disk_drive_get_current_time(-(int)usec);

    // clear status
    dev->strb &= ~FDC_STB_ALL_MASK;
    dev->pre_stra = dev->stra;
    dev->pre_strb = dev->strb;

#ifdef USE_LOST_EVENT
    fdc_cancel_event(EVENT_LOST);
#endif
    fdc_send_status();
    fdc_irq_update();

    if (fdc_search_sector()) {
        fdc_pre_read_data();
    }

    OUT_DEBUG_HL("MC6843: cmd_read HEAD LOAD\n");
    dev->head_load = 1;
    if (dev->sec_pos >= 0) {
        usec += disk_drive_get_time_arrival_sector(dev->sec_pos, disk_d88_get_sector_nums(dev->drv_num), dev->cmd_time);
    } else {
        // sector not found (The searching continues until the disk has rotated 3 times.)
        usec += disk_drive_get_index_hole_remain_time(dev->cmd_time) + disk_drive_get_one_round_time() * 3;
    }
    fdc_register_search_data_event(FLG_DELAY_FDSEARCH ? 600 : usec);
}

/// Free Format Read
/// TODO: This command does not read data from the disk.
static void fdc_cmd_FFR()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);

    uint32_t usec = fdc_set_delay(dev->head_load ? 0x00 : 0x0f);
    dev->cmd_time = disk_drive_get_current_time(-(int)usec);

    // clear status
    dev->strb &= ~FDC_STB_ALL_MASK;
    dev->pre_stra = dev->stra;
    dev->pre_strb = dev->strb;

#ifdef USE_LOST_EVENT
    fdc_cancel_event(EVENT_LOST);
#endif
    fdc_send_status();
    fdc_irq_update();

    // free format read
    fdc_pre_read_track();

    OUT_DEBUG_HL("MC6843: cmd_FFW HEAD LOAD\n");
    dev->head_load = 1;
//  if (!FLG_DELAY_FDSEARCH) time += d_fdd->get_index_hole_remain_clock();
//  usec += disk_drive_get_index_hole_remain_time();
    fdc_register_search_data_event(FLG_DELAY_FDSEARCH ? 600 : usec);
}

/// Read CRC
static void fdc_cmd_RCR()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    dev->gcr = 0;

    fdc_cmd_read();
}

/// Single Sector Write
static void fdc_cmd_SSW()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    dev->gcr = 0;
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);

    fdc_cmd_write();
}

/// Multi Sector Write
static void fdc_cmd_MSW()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);

    fdc_cmd_write();
}

static void fdc_cmd_write()
{
    uint32_t usec = fdc_set_delay(dev->head_load ? 0x00 : 0x0f);
    dev->cmd_time = disk_drive_get_current_time(-(int)usec);

    // clear status
    dev->strb &= ~FDC_STB_ALL_MASK;
    dev->pre_stra = dev->stra;
    dev->pre_strb = dev->strb;

#ifdef USE_LOST_EVENT
    fdc_cancel_event(EVENT_LOST);
#endif
    fdc_send_status();
    fdc_irq_update();

    fdc_search_sector();

    OUT_DEBUG_HL("MC6843: cmd_write HEAD LOAD\n");
    dev->head_load = 1;
    if (dev->sec_pos >= 0) {
        usec += disk_drive_get_time_arrival_sector(dev->sec_pos, disk_d88_get_sector_nums(dev->drv_num), dev->cmd_time);
    } else {
        // sector not found (The searching continues until the disk has rotated 3 times.)
        usec += disk_drive_get_index_hole_remain_time(dev->cmd_time) + disk_drive_get_one_round_time() * 3;
    }
    fdc_register_search_data_event(FLG_DELAY_FDSEARCH ? 600 : usec);
}

/// Single Sector Write with Delete Data Mark
static void fdc_cmd_SWD()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    dev->gcr = 0;
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);

    fdc_cmd_write();
}

/// Free Format Write
static void fdc_cmd_FFW()
{
    dev->stra |= FDC_STA_BUSY;  // busy
    fdc_common_set_busy();
    fdc_clr_drq();
    dev->stra &= ~(FDC_STA_TRACKNE | FDC_STA_DELETE);
//  dev->sar = 1;

    //
//  memset(dev->parse_clk_buf, 0, MC6843_PARSE_BUFFER);
//  memset(dev->parse_dat_buf, 0, MC6843_PARSE_BUFFER);
//  dev->parse_idx = 0;
//  dev->ffw_phase = 0;

    uint32_t usec = fdc_set_delay(dev->head_load ? 0x00 : 0x0f);
    dev->cmd_time = disk_drive_get_current_time(-(int)usec);

    // clear status
    dev->strb &= ~FDC_STB_ALL_MASK;
    dev->pre_stra = dev->stra;
    dev->pre_strb = dev->strb;

#ifdef USE_LOST_EVENT
    fdc_cancel_event(EVENT_LOST);
#endif
    fdc_send_status();
    fdc_irq_update();

    OUT_DEBUG_HL("MC6843: cmd_FFW HEAD LOAD\n");
    dev->head_load = 1;
//  if (!FLG_DELAY_FDSEARCH) time += d_fdd->get_index_hole_remain_clock();
//  usec += disk_drive_get_index_hole_remain_time();
    fdc_register_search_data_event(FLG_DELAY_FDSEARCH ? 600 : usec);
}

/// stop of Free Format Write
static void fdc_cmd_FFW_END()
{
    int cmd = dev->cmr & 0x0f;

    if (cmd != FDC_CMD_FFR && cmd != FDC_CMD_FFW)  return;

#ifdef USE_LOST_EVENT
    fdc_cancel_event(EVENT_LOST);
#endif
    fdc_clr_drq();
    dev->stra &= ~FDC_STA_BUSY; // clear Busy
    fdc_common_clr_busy();
    dev->cmr  &=  0xf0; // clear command
    fdc_irq_update();
    fdc_send_status();

//  d_fdd->parse_track(0);
}

// ----------------------------------------------------------------------------
// media handler
// ----------------------------------------------------------------------------
#if 0
static void fdc_find_track()
{
    if(!d_fdd->search_track(0)) {
        dev->strb |= FDC_STB_SEEKERR;
        OUT_DEBUG(_T("MC6843: chg_stat strb:%02x"),strb);
    }

    return;
}
#endif

/// @brief 
/// @return 
static bool __not_in_flash_func(fdc_search_sector)()
{
    // scan sectors
    int side_number = dev->sid_num[dev->drv_num];
    int stat = disk_d88_read_sector(dev->drv_num, dev->ltar, - side_number - 1, dev->sar, dev->density, -1, false, &dev->sec_pos);
    if (stat != 1) {
        dev->data_idx = 0;
        if (stat & 4) dev->pre_stra |= FDC_STA_DELETE;
        if (stat & 2) dev->pre_strb |= FDC_STB_CRCERR;
        return true;
    }
    // sector not found
//  if (FLG_ORIG_FDDRQ) set_irq(true);
    dev->pre_strb = FDC_STB_SECTNF;
    return false;   // record not found
}

#if 0
static void fdc_find_sector(int sect)
{
    int cmd = dev->cmr & 0x0f;

    // clear status
    dev->strb &= ~(FDC_STB_CRCERR | FDC_STB_SECTNF);

    if (dev->sar > 26) {
        // address error
        dev->strb |= FDC_STB_SECTNF;
        OUT_DEBUG(_T("MC6843: chg_stat strb:%02x"),strb);
        return;
    }

    // check track number
//  ctar = d_fdd->read_signal(SIG_FLOPPY_CURRENTTRACK);
    if(!disk_d88_search_track(0)) {
        dev->stra |= FDC_STA_TRACKNE;
        dev->dir = d_fdd->get_current_track_number(0);
        OUT_DEBUG(_T("MC6843: chg_stat stra:%02x"),stra);
        return;
    }

    if (cmd != FDC_CMD_FFR && cmd != FDC_CMD_FFW) {
        // verify track number
        if (!d_fdd->verify_track(0, dev->ltar)) {
//      if (ltar != ctar) {
            dev->stra |= FDC_STA_TRACKNE;
            dev->dir = d_fdd->get_current_track_number(0);
            OUT_DEBUG(_T("MC6843: chg_stat stra:%02x"),stra);
            return;
        }
    }

    // search sector when sector read or write
    if (cmd != FDC_CMD_FFR && cmd != FDC_CMD_FFW) {
        int stat = disk_d88_read_sector(0, dev->ltar, dev->sid_num, sect, false, 0);
        if (stat & 1) {
            dev->strb |= FDC_STB_SECTNF;
            OUT_DEBUG(_T("MC6843: chg_stat strb:%02x"),strb);
        }
        if (stat & 2) {
            dev->strb |= FDC_STB_CRCERR;
            OUT_DEBUG(_T("MC6843: chg_stat strb:%02x"),strb);
        }
        // delete mark ?
        if (stat & 4) {
            dev->stra |= FDC_STA_DELETE;
            OUT_DEBUG(_T("MC6843: chg_stat stra:%02x"),stra);
        }
    }

    dev->data_idx = 0;

    return;
}
#endif

// ----------------------------------------------------------------------------

static void __not_in_flash_func(fdc_send_dir)()
{
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_HD6843_DIR, dev->dir);
}

static void __not_in_flash_func(fdc_send_dir_sync)()
{
    dev->wait_ack = 1;
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE_SYNC, FDC_HD6843_DIR, dev->dir);
}

static void __not_in_flash_func(fdc_send_ctar)()
{
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_HD6843_CTAR, dev->ctar);
}

static void __not_in_flash_func(fdc_send_isr)()
{
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_HD6843_ISR, dev->isr);
}

static void __not_in_flash_func(fdc_send_stra)()
{
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_HD6843_STRA, dev->stra);
}

static void __not_in_flash_func(fdc_send_strb)()
{
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_HD6843_STRB, dev->strb);
}

static void __not_in_flash_func(fdc_send_status)()
{
    fdc_send_stra();
    fdc_send_strb();
}

// ----------------------------------------------------------------------------

void fdc_3inch_set_callback()
{
//  disk_drive_set_status_callback = &fdc_set_status_callback;
//  disk_drive_clr_status_callback = &fdc_clr_status_callback;
    disk_drive_motor_on_callback = &fdc_motor_on_callback;
    disk_drive_motor_off_callback = &fdc_motor_off_callback;
    disk_drive_index_on_callback = &fdc_index_on_callback;
    disk_drive_index_off_callback = &fdc_index_off_callback;
    fdc_common_task_callback = &fdc_task;
    fdc_common_write_io_callback = &fdc_write_io;
    fdc_common_post_read_callback = &fdc_post_read;
    fdc_common_wrote_ack_callback = &fdc_wrote_ack;
    fdc_common_read_io_callback = &fdc_read_io;
    fdc_common_post_read_tightly_callback = &fdc_post_read_tightly;
    fdc_common_post_write_tightly_callback = &fdc_post_write_tightly;
    fdc_common_notice_tightly_callback = &fdc_notice_tightly;
}

#if 0
static void fdc_set_status_callback(disk_drive_status_t val)
{
    switch(val) {
    case DISK_DRIVE_MOTOR:
        if (disk_d88_is_not_ready(dev->drv_num)) {
            dev->stra &= ~FDC_STA_DREADY;
        } else {
            dev->stra |= FDC_STA_DREADY;
        }
    
        break;
    case DISK_DRIVE_INDEX:
        dev->stra |= FDC_STA_INDEX;
        break;

//  case DISK_DRIVE_HEAD_LOAD:
//      dev->drv_sts |= FDC_ST_HEADENG;
//      break;

    default:
        break;
    }
    fdc_send_stra();
}

static void fdc_clr_status_callback(disk_drive_status_t val)
{
    switch(val) {
    case DISK_DRIVE_MOTOR:
        dev->stra &= ~FDC_STA_DREADY;
        break;
    case DISK_DRIVE_INDEX:
        dev->stra &= ~FDC_STA_INDEX;
        break;
//  case DISK_DRIVE_HEAD_LOAD:
//      dev->drv_sts &= FDC_ST_HEADENG;
//      break;
    default:
        break;
    }
    fdc_send_stra();
}
#endif

static void fdc_motor_on_callback(void)
{
    if (disk_d88_is_not_ready(dev->drv_num)) {
        dev->stra &= ~FDC_STA_DREADY;
    } else {
        dev->stra |= FDC_STA_DREADY;
    }
    fdc_send_stra();
}

static void fdc_motor_off_callback(void)
{
    dev->stra &= ~FDC_STA_DREADY;
    fdc_send_stra();
}

static void __no_inline_not_in_flash_func(fdc_index_on_callback)(void)
{
    dev->stra |= FDC_STA_INDEX;
    fdc_send_stra();
}

static void __no_inline_not_in_flash_func(fdc_index_off_callback)(void)
{
    dev->stra &= ~FDC_STA_INDEX;
    fdc_send_stra();
}

// ----------------------------------------------------------------------------
// for free format write
// ----------------------------------------------------------------------------

#if 0
static const uint8_t address_mark_dat_tbl[8] =
    { 0xcf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#endif

static void fdc_parse_twice_format(uint8_t data)
{
#if 0
    uint8_t clk = 0;
    uint8_t dat = 0;

    dev->parse_idx = 1 - dev->parse_idx;

    // shift to 4bit right
    for (int i=(MC6843_PARSE_BUFFER-2); i >= 0; i--) {
        dev->parse_clk_buf[i+1] = (dev->parse_clk_buf[i+1] >> 4);
        dev->parse_dat_buf[i+1] = (dev->parse_dat_buf[i+1] >> 4);
        dev->parse_clk_buf[i+1] |= ((dev->parse_clk_buf[i] & 0x0f) << 4);
        dev->parse_dat_buf[i+1] |= ((dev->parse_dat_buf[i] & 0x0f) << 4);
    }
    dev->parse_clk_buf[0] = (dev->parse_clk_buf[0] >> 4);
    dev->parse_dat_buf[0] = (dev->parse_dat_buf[0] >> 4);

    clk = (data & 0x80) | ((data & 0x20) << 1) | ((data & 0x08) << 2) | ((data & 0x02) << 3);
    dat = ((data & 0x40) << 1) | ((data & 0x10) << 2) | ((data & 0x04) << 3) | ((data & 0x01) << 4);

    dev->parse_clk_buf[0] |= clk;
    dev->parse_dat_buf[0] |= dat;

    dev->parse_dat = ((parse_dat_buf[0] & 0x0f) << 4) | ((parse_dat_buf[0] & 0xf0) >> 4);

    // index mark check
    if (memcmp(parse_dat_buf, address_mark_dat_tbl, 7) == 0) {
        dev->parse_idx = 1;
    }

    if (dev->parse_idx == 1) {
        d_fdd->write_signal(SIG_FLOPPY_WRITE_TRACK, parse_dat, 0xff);
    }
#endif
//  OUT_DEBUG("fdcffw c:%02x d:%02x",clk,dat);
}

static void fdc_parse_plane_format(uint8_t data)
{
//  d_fdd->write_signal(SIG_FLOPPY_WRITE_TRACK, data, 0xff);
}

#if 0
// reverse
static uint8_t address_mark_clk_tbl[3][8]={
    { 0x7d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 },
    { 0x7c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 },
    { 0x7c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 }
};
static uint8_t address_mark_dat_tbl[3][8]={
    { 0xcf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

static void fdc_parse_ibm3740_format(uint8_t data)
{
    uint8_t clk = 0;
    uint8_t dat = 0;

    parse_idx = 1 - parse_idx;

    // shift to 4bit right
    for (int i=(MC6843_PARSE_BUFFER-2); i >= 0; i--) {
        parse_clk_buf[i+1] = (parse_clk_buf[i+1] >> 4);
        parse_dat_buf[i+1] = (parse_dat_buf[i+1] >> 4);
        parse_clk_buf[i+1] |= ((parse_clk_buf[i] & 0x0f) << 4);
        parse_dat_buf[i+1] |= ((parse_dat_buf[i] & 0x0f) << 4);
    }
    parse_clk_buf[0] = (parse_clk_buf[0] >> 4);
    parse_dat_buf[0] = (parse_dat_buf[0] >> 4);

    clk = ((data & 0x80) >> 4) | ((data & 0x20) >> 3) | ((data & 0x08) >> 2) | ((data & 0x02) >> 1);
    dat = ((data & 0x40) >> 3) | ((data & 0x10) >> 2) | ((data & 0x04) >> 1) | (data & 0x01);

    parse_clk_buf[0] |= (clk << 4);
    parse_dat_buf[0] |= (dat << 4);

    parse_dat = ((parse_dat_buf[0] & 0x0f) << 4) | ((parse_dat_buf[0] & 0xf0) >> 4);

    for (int i=0; i < 3; i++) {
        if (memcmp(parse_clk_buf, address_mark_clk_tbl[i], 7) == 0
            && memcmp(parse_dat_buf, address_mark_dat_tbl[i], 7) == 0) {
            ffw_phase = i + 1;
            parse_idx = 0;
            break;
        }
    }

//  OUT_DEBUG("fdcffw c:%02x d:%02x",clk,dat);
}

static void fdc_write_ibm3740_format()
{
    if (parse_idx == 0) {
        if (ffw_phase == 0) {
            data_idx = 0;
        } else if (ffw_phase == 1) { // index mark
            ffw_phase = 0;
        } else if (ffw_phase == 2) {    // id mark
            if (data_idx == 3) {
                // search sector
                if(d_fdd->search_sector(0, parse_dat)) {
                    ffw_phase = 0;
                }
            }
            data_idx++;
            if (data_idx >= 4) ffw_phase = 0;
        } else if (ffw_phase == 3) {    // data mark
            if (data_idx > 0) {
                if(d_fdd->read_signal(SIG_FLOPPY_WRITEPROTECT)) {
                    // write protect
                    strb |= FDC_STB_WRITEERR;
                    stra &= ~FDC_STA_BUSY;
                    stra &= ~FDC_STA_DRQ;

                    ffw_phase = 0;
                } else {
                    d_fdd->write_signal(SIG_FLOPPY_WRITE, parse_dat, 0xff);
                }
            }
            data_idx++;
            if (data_idx > 128) ffw_phase = 0;
        }
    }
}
#endif

void fdc_3inch_get_info()
{
    printf("[[[FDC 3inch]]]\n");
    // registor
    printf("DIR:%02x CTAR:%02x CMR:%02x ISR:%02x SUR:%02x STRA:%02x STRB:%02x\n",
        dev->dir,
        dev->ctar,
        dev->cmr,
        dev->isr,
        dev->sur,
        dev->stra,
        dev->strb
    );
    printf("SAR:%02x GCR:%02x CCR:%02x LTAR:%02x\n",
        dev->sar,
        dev->gcr,
        dev->ccr,
        dev->ltar
    );
    printf("(Drive:%u Sec_pos:%d)\n",
        dev->drv_num, dev->sec_pos
    );
    for(int drv=0; drv<MAX_DRIVES; drv++) {
        printf("(Side on Drive %d: %u)\n",
            drv, dev->sid_num[drv]
        );
    }
}

uint8_t fdc_3inch_toggle_side_number(uint8_t drv, uint8_t sid)
{
    sid = 1 - sid;
    dev->sid_num[drv] = sid;
    lcd_disk_sid_number(drv, sid);
    return sid;
}

void fdc_3inch_set_side_number(uint8_t drv, uint8_t sid)
{
    dev->sid_num[drv] = sid;
}

uint8_t fdc_3inch_get_side_number(uint8_t drv)
{
    return dev->sid_num[drv];
}
