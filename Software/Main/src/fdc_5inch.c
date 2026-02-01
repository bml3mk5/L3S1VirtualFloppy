/** @file fdc_5inch.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include <stdio.h>
#include <string.h>
#include <hardware/gpio.h>
#include "common.h"
#include "fdc_5inch.h"
#include "fdc_common.h"
#include "disk_d88.h"
#include "event.h"
#include "main.h"
#include "msg_bridge.h"
#include "pio_ctrls.h"
#include "simple_fifo.h"
#include "config.h"

#define HAS_MB8876 1

#ifndef OUT_DEBUG
#define OUT_DEBUG(...) printf(__VA_ARGS__)
#endif

#ifndef OUT_DEBUG_WR
#define OUT_DEBUG_WR(...)
#endif

#ifndef OUT_DEBUG_RD
#define OUT_DEBUG_RD(...)
#endif

#ifndef OUT_DEBUG_EV
#define OUT_DEBUG_EV(...)
#endif

//#define USE_LOST_EVENT

/// @brief register number
enum FDC_5INCH_REGS {
    FDC_MB8877_STR = 0,
    FDC_MB8877_CR = 0,
    FDC_MB8877_TR = 1,
    FDC_MB8877_SCR = 2,
    FDC_MB8877_DR = 3,
    FDC_5INCH_UNIT = 4,
};

/// @brief event ids
enum EVENT_IDS {
    EVENT_SEEK      = 0,
    EVENT_SEEKEND,
    EVENT_SEARCH_DATA,
    EVENT_SEARCH_ADDR,
    EVENT_SEARCH_TRACK,
//    EVENT_TYPE4,
    EVENT_MULTI1,
    EVENT_MULTI2,
    EVENT_LOST,
    EVENT_DRQ,
//    EVENT_RESTORE,
    EVENT_HEADENG,

    EVENT_MAX
};

/// @brief cmd type
enum FDC_5INCH_CMD_TYPES {
    FDC_CMD_TYPE1       = 1,
    FDC_CMD_RD_SEC      = 2,
    FDC_CMD_RD_MSEC     = 3,
    FDC_CMD_WR_SEC      = 4,
    FDC_CMD_WR_MSEC     = 5,
    FDC_CMD_RD_ADDR     = 6,
    FDC_CMD_RD_TRK      = 7,
    FDC_CMD_WR_TRK      = 8,
    FDC_CMD_TYPE4       = 0x80,
};

/// @brief status
enum FDC_5INCH_STATUS_MASKS {
    FDC_ST_BUSY         = 0x01, ///< busy
    FDC_ST_INDEX        = 0x02, ///< index hole
    FDC_ST_DRQ          = 0x02, ///< data request
    FDC_ST_TRACK00      = 0x04, ///< track0
    FDC_ST_LOSTDATA     = 0x04, ///< data lost
    FDC_ST_CRCERR       = 0x08, ///< crc error
    FDC_ST_SEEKERR      = 0x10, ///< seek error
    FDC_ST_RECNFND      = 0x10, ///< sector not found
    FDC_ST_HEADENG      = 0x20, ///< head engage
    FDC_ST_RECTYPE      = 0x20, ///< record type
    FDC_ST_WRITEFAULT   = 0x20, ///< write fault
    FDC_ST_WRITEP       = 0x40, ///< write protectdc
    FDC_ST_NOTREADY     = 0x80, ///< media not inserted
};

enum FDC_5INCH_UNIT_MASKS {
    FDC_UNIT_MOTOR      = 0x08, ///< motor on/off
    FDC_UNIT_SIDE1      = 0x10, ///< side select
    FDC_UNIT_DDEN       = 0x20, ///< double density
    FDC_UNIT_NMI_MASK   = 0x40, ///< nmi mask
    FDC_UNIT_IRQ        = 0x01,
    FDC_UNIT_DRQ        = 0x80,
};

typedef struct {
    // mb8876
    // registor
    uint8_t status;
    uint8_t cmdreg;
    uint8_t trkreg;
    uint8_t secreg;

    uint8_t datareg;
    uint8_t cmdtype;
    uint8_t clk_num;
    uint8_t pre_sts;

    uint8_t drv_sts;    // drive status b7:drive not ready b6:write protected b5:head loaded b2:track0 b1:index hole
    uint8_t now_irq;
    uint8_t now_drq;

    // status
    struct {
        uint8_t now_search : 1;
        uint8_t now_seek : 1;
        uint8_t after_seek : 1;
        uint8_t seekvct : 1;
//      // for unit sel
//      uint8_t motor : 1;
        //
//      uint8_t now_reset : 1;
        uint8_t wait_ack : 1;
    };

    // unit sel
    uint8_t drv_num;
    uint8_t sid_num;
    uint8_t density;    // 0:single density(FM) 1:double density(MFM)
    uint8_t nmi_enable;

    int data_idx;
    int sec_pos;
    int sec_nums;
    int seektrk;

    int cmd_time;   // command start time
    uint64_t drq_time;

    fdc_common_event_t event_info[EVENT_MAX];
    simple_fifo_t event_fifo;
    uint32_t event_fifo_buffer[8];
} FDC_5INCH;

static FDC_5INCH g_fdc_5inch;
static FDC_5INCH *dev = &g_fdc_5inch;

/// seek stepping rate 6msec, 12msec, 20msec, 30msec
static const int seek_wait[2][4] = {
    {6000, 12000, 20000, 30000},    // 1MHz
    {3000,  6000, 10000, 15000},    // 2MHz
};

#define FLG_DELAY_FDSEEK (config_get_seek_track() != 0)
#define FLG_DELAY_FDSEARCH (config_get_search_sector() != 0)
#define FLG_DELAY_DATAREQ (config_get_data_request() != 0)
#define FLG_ORIG_FDDRQ 0

#define USE_LOST_EVENT 1
#define LOST_TIME_AFTER_SEARCH 1000000
#define LOST_TIME_NEXT_DRQ     1000000
#define EVENT_NEXT_DRQ_TIME     1

// wait time until HLT is high
#define HEADENG_LONG_TIME       100000
// wait time enabling E flag 
#define HEADENG_WAIT_TIME        15000
// no wait time
#define HEADENG_SHORT_TIME          64

//--------------------------------------------------------------------

static void fdc_task();

static void fdc_cancel_event(int event);
static void fdc_register_event(int event, uint32_t usec);
static int64_t fdc_event_callback(alarm_id_t id, void *user_data);

static void fdc_register_seek_event();
static void fdc_event_seek(uint32_t event_data);
static void fdc_register_seekend_event();
static void fdc_event_seekend(uint32_t event_data);
static void fdc_register_search_data_event(uint32_t usec);
static void fdc_event_search_data(uint32_t event_data);
static void fdc_register_search_addr_event(uint32_t usec);
static void fdc_event_search_addr(uint32_t event_data);
static void fdc_register_search_track_event(uint32_t usec);
static void fdc_event_search_track(uint32_t event_data);
static void fdc_register_drq_event();
static void fdc_fire_drq();
static void fdc_event_drq(uint32_t event_data);
static void fdc_register_multi_event();
static void fdc_event_multi1(uint32_t event_data);
static void fdc_event_multi2(uint32_t event_data);
static void fdc_register_lost_event(uint32_t usec);
static void fdc_cancel_lost_event();
static void fdc_event_lost(uint32_t event_data);
//static void fdc_register_restore_event();
//static void fdc_event_restore(uint32_t event_data);
static void fdc_register_headeng_event(uint32_t usec);
static void fdc_event_headeng(uint32_t event_data);

static void fdc_pre_read_data();
static bool fdc_post_read_tightly(uint8_t addr);
static void fdc_post_write_tightly(uint8_t addr);

static void fdc_process_cmd();
static void fdc_cmd_restore();
static void fdc_cmd_seek();
static void fdc_cmd_step();
static void fdc_cmd_stepin();
static void fdc_cmd_stepout();
static void fdc_cmd_readdata();
static void fdc_cmd_readdata_headengd();
static void fdc_cmd_writedata();
static void fdc_cmd_writedata_headengd();
static void fdc_cmd_readaddr();
static void fdc_cmd_readaddr_headengd();
static void fdc_cmd_readtrack();
static void fdc_cmd_readtrack_headengd();
static void fdc_cmd_writetrack();
static void fdc_cmd_writetrack_headengd();
static void fdc_cmd_forceint();

// busy
static void fdc_set_busy();
static void fdc_clr_busy();

static void fdc_send_track_reg();
static void fdc_send_sector_reg();
static void fdc_send_data_reg_directly();
static void fdc_send_data_reg();
static void fdc_send_data_reg_sync();

// irq/dma
static void fdc_set_irq(uint8_t val);
static void fdc_set_drq(uint8_t val);

static inline uint8_t fdc_get_status();
static inline uint8_t fdc_add_status();
static inline void fdc_send_status();
//static void fdc_update_status(uint8_t set_flags, uint8_t clr_flags);
//static void fdc_set_status_callback(disk_drive_status_t val);
//static void fdc_clr_status_callback(disk_drive_status_t val);
static void fdc_motor_on_callback(void);
static void fdc_motor_off_callback(void);
static void fdc_index_on_callback(void);
static void fdc_index_off_callback(void);

static uint8_t fdc_verify_track(int side_number);
static uint8_t fdc_search_sector(int side_number);
static uint8_t fdc_search_addr();
static uint8_t fdc_make_track();
static int fdc_parse_track();

static bool fdc_is_ready();

//--------------------------------------------------------------------

/// @brief initialize
void fdc_5inch_init()
{
    memset(&g_fdc_5inch, 0, sizeof(g_fdc_5inch));

//  disk_drive_set_status_callback = &fdc_set_status_callback;
//  disk_drive_clr_status_callback = &fdc_clr_status_callback;
    dev->clk_num = 0;

    for(int i=0; i<EVENT_MAX; i++) {
        dev->event_info[i].id = -1;
        dev->event_info[i].callback = NULL;
    }
    dev->event_info[EVENT_SEEK].callback = &fdc_event_seek;
    dev->event_info[EVENT_SEEKEND].callback = &fdc_event_seekend;
    dev->event_info[EVENT_SEARCH_DATA].callback = &fdc_event_search_data;
    dev->event_info[EVENT_SEARCH_ADDR].callback = &fdc_event_search_addr;
    dev->event_info[EVENT_SEARCH_TRACK].callback = &fdc_event_search_track;
//    dev->event_info[EVENT_TYPE4].callback = &fdc_event_type4;
    dev->event_info[EVENT_MULTI1].callback = &fdc_event_multi1;
    dev->event_info[EVENT_MULTI2].callback = &fdc_event_multi2;
    dev->event_info[EVENT_LOST].callback = &fdc_event_lost;
    dev->event_info[EVENT_DRQ].callback = &fdc_event_drq;
    dev->event_info[EVENT_HEADENG].callback = &fdc_event_headeng;

    fifo_init(&dev->event_fifo, dev->event_fifo_buffer, sizeof(dev->event_fifo_buffer)/sizeof(dev->event_fifo_buffer[0]), sizeof(dev->event_fifo_buffer[0]));

    fdc_5inch_reset();
}

void fdc_5inch_reset()
{
    dev->drv_sts |= FDC_ST_NOTREADY;
    dev->nmi_enable = FDC_UNIT_IRQ;
    dev->cmdreg = 0;
    dev->cmdtype = 0;
    dev->secreg = 1;

    fdc_common_clr_busy();

    // send to core1
    fdc_send_status();
    fdc_send_track_reg();
    fdc_send_sector_reg();
    fdc_send_data_reg_directly();

    fdc_5inch_unitsel_reset();
}

void fdc_5inch_unitsel_reset()
{
    for(int i=0; i<EVENT_MAX; i++) {
        fdc_cancel_event(i);
    }
    fifo_clear(&dev->event_fifo);
    fdc_set_drq(0);
    fdc_set_irq(0);
    dev->drv_num = 0;
    dev->sid_num = 0;
    dev->density = 0;
    dev->nmi_enable = FDC_UNIT_IRQ;
    dev->wait_ack = 0;
    disk_drive_motor_off();
//    dev->motor = 0;
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
    OUT_DEBUG_EV(_T("FDC5: Cancel EVENT:%d\n"), event);
}

static void __no_inline_not_in_flash_func(fdc_register_event)(int event, uint32_t usec)
{
    fdc_cancel_event(event);
    dev->event_info[event].id = event_register_event(usec, fdc_event_callback, (event << 8) | dev->cmdtype);
    OUT_DEBUG_EV(_T("FDC5: Regist EVENT:%d id:%08x usec:%d\n"), event, dev->event_info[event].id, usec);
}

static int64_t __no_inline_not_in_flash_func(fdc_event_callback)(alarm_id_t id, void *user_data)
{
    uint32_t event_data = (uint32_t)user_data;
    dev->event_info[event_data >> 8].id = -1;
    fifo_push32(&dev->event_fifo, event_data);
    return 0;
}

//--------------------------------------------------------------------

static void fdc_register_seek_event()
{
    fdc_register_event(EVENT_SEEK, FLG_DELAY_FDSEEK ? 600 : seek_wait[dev->clk_num][dev->cmdreg & 3]);
    dev->now_seek = 1;
}

static void fdc_event_seek(uint32_t event_data)
{
    if((event_data & 0xff) != dev->cmdtype) {
        // cancel?
        dev->now_seek = 0;
        return;
    }
    bool trksame = false;
    if ((dev->cmdreg & 0xe0) == 0) {
        // restore or seek
        dev->seekvct = (dev->seektrk > (int)dev->trkreg) ? 0 : 1;
        trksame = (dev->seektrk == (int)dev->trkreg);
    }

//  d_fdd->write_signal(SIG_FLOPPY_STEP | channel, (trksame ? 0x80 : (seekvct ? 0xff : 0x7f)), 0xff);
    if (!trksame) {
        if (dev->seekvct) {
            disk_d88_step_out(dev->drv_num, dev->sid_num);
        } else {
            disk_d88_step_in(dev->drv_num, dev->sid_num);
        }
    }

    bool is_trk00 = disk_d88_is_track0(dev->drv_num);
    if (is_trk00) {
        dev->drv_sts |= FDC_ST_TRACK00; 
    } else {
        dev->drv_sts &= ~FDC_ST_TRACK00; 
    }

    if ((dev->cmdreg & 0xe0) == 0 || (dev->cmdreg & 0x10) != 0) {
        // restore or seek
        // step/in/out with setting u flag
        if (!dev->seekvct) {
            // priority first
            dev->trkreg++;
        } else if (is_trk00) {
            // priority second
            dev->trkreg = 0;
        } else if (!trksame) {
            dev->trkreg--;
        }
        fdc_send_track_reg();
    }
    if(dev->seektrk == (int)dev->trkreg || (dev->cmdreg & 0xe0) != 0) {
        // match track or step end
        dev->now_seek = 0;

        dev->status |= fdc_verify_track(dev->sid_num);
        fdc_clr_busy();
        fdc_send_status();
        fdc_set_irq(dev->nmi_enable);
//      dev->nmi_enable = 0;
    } else {
        // next step
        fdc_register_seek_event();
    }
}

static void fdc_register_seekend_event()
{
    fdc_register_event(EVENT_SEEKEND, 300);
}

static void fdc_event_seekend(uint32_t event_data)
{
    if((dev->cmdreg & 0xe0) == 0 && dev->seektrk == (int)dev->trkreg) {
        fdc_cancel_event(EVENT_SEEK);

        // auto update
        if((dev->cmdreg & 0xf0) == 0) {
            dev->datareg = 0;
            fdc_send_data_reg_directly();
        }
        dev->now_seek = 0;

        dev->status |= fdc_verify_track(dev->sid_num);
        fdc_clr_busy();
        fdc_send_status();
        fdc_set_irq(dev->nmi_enable);
//      dev->irq_mask = 0;
    }
}

static void fdc_register_search_data_event(uint32_t usec)
{
    if (usec < 16) usec = 16;
    fdc_register_event(EVENT_SEARCH_DATA, usec);
//  printf("FDC5: Regist EVENT_SEARCH_DATA\n");
    dev->now_search = 1;
}

static void fdc_event_search_data(uint32_t event_data)
{
    dev->now_search = 0;
    if((event_data & 0xff) != dev->cmdtype) {
        // cancel?
        return;
    }
    dev->status = dev->pre_sts;

//  printf("Read data: Sts:%02x\n", dev->status);

    // start dma
    if(!FLG_ORIG_FDDRQ) {
        if (fdc_get_status() != 0
        || (dev->status & (FDC_ST_RECNFND | FDC_ST_CRCERR)) != 0
        ) {
            // error end
            fdc_clr_busy();
            fdc_set_irq(dev->nmi_enable);
            fdc_send_status();
        } else {
            fdc_fire_drq();
//          dev->status |= FDC_ST_DRQ;
//          fdc_set_drq(FDC_UNIT_DRQ);
//          fdc_send_status();
//          printf("Read data: Sts:%02x\n", dev->status);
//#ifdef USE_LOST_EVENT
//          fdc_register_lost_event(LOST_TIME_AFTER_SEARCH);
//#endif
        }
    }
}

static void fdc_register_search_addr_event(uint32_t usec)
{
    if (usec < 16) usec = 16;
    fdc_register_event(EVENT_SEARCH_ADDR, usec);
    dev->now_search = 1;
}

static void fdc_event_search_addr(uint32_t event_data)
{
    dev->now_search = 0;
    if((event_data & 0xff) != dev->cmdtype) {
        // cancel?
        return;
    }
    dev->status = dev->pre_sts;

    // start dma
    if(!FLG_ORIG_FDDRQ) {
        if (fdc_get_status() != 0
        || (dev->status & (FDC_ST_RECNFND | FDC_ST_CRCERR)) != 0
        ) {
            // error end
            fdc_clr_busy();
            fdc_set_irq(dev->nmi_enable);
            fdc_send_status();
        } else {
            fdc_fire_drq();
//          dev->status |= FDC_ST_DRQ;
//          fdc_set_drq(FDC_UNIT_DRQ);
//          fdc_send_status();
//#ifdef USE_LOST_EVENT
//          fdc_register_lost_event(LOST_TIME_AFTER_SEARCH);
//#endif
        }
    }
}

static void fdc_register_search_track_event(uint32_t usec)
{
    if (usec < 16) usec = 16;
    fdc_register_event(EVENT_SEARCH_TRACK, usec);
    dev->now_search = 1;
}

static void fdc_event_search_track(uint32_t event_data)
{
    dev->now_search = 0;
    if((event_data & 0xff) != dev->cmdtype) {
        // cancel?
        return;
    }
    dev->status = dev->pre_sts;

    // start dma
    if(!FLG_ORIG_FDDRQ) {
        if (fdc_get_status() != 0
        || (dev->status & (FDC_ST_RECNFND | FDC_ST_CRCERR)) != 0
        ) {
            // error end
            fdc_clr_busy();
            fdc_set_irq(dev->nmi_enable);
            fdc_send_status();
        } else {
            fdc_fire_drq();
//          dev->status |= FDC_ST_DRQ;
//          fdc_set_drq(FDC_UNIT_DRQ);
//          fdc_send_status();
//#ifdef USE_LOST_EVENT
//          fdc_register_lost_event(LOST_TIME_AFTER_SEARCH);
//#endif
        }
    }
}

static void __not_in_flash_func(fdc_register_drq_event)()
{
    if (!FLG_DELAY_DATAREQ) {
        uint32_t usec = (64 >> (dev->clk_num + dev->density));  // 8us * 8bit (1MHz, FM)
//      uint32_t usec = (16384 >> (dev->clk_num + dev->density));   // 8us * 8bit (1MHz, FM)
#if EVENT_NEXT_DRQ_TIME > 1
        usec *= EVENT_NEXT_DRQ_TIME;
#endif
        uint32_t sub = (uint32_t)(to_us_since_boot(get_absolute_time()) - dev->drq_time);
        if (usec > sub + 16) {
            usec -= sub;
        } else {
            usec = 16;
        }
        fdc_cancel_lost_event();
        fdc_register_event(EVENT_DRQ, usec);
    }
}

static void __not_in_flash_func(fdc_register_drq_expand_event)()
{
    fdc_register_event(EVENT_DRQ, 16);
}

static void __not_in_flash_func(fdc_fire_drq)()
{
    dev->status |= FDC_ST_DRQ;
    fdc_set_drq(FDC_UNIT_DRQ);
    fdc_send_status();
#ifdef USE_LOST_EVENT
    fdc_register_lost_event(LOST_TIME_NEXT_DRQ);
#endif
}

static void __no_inline_not_in_flash_func(fdc_event_drq)(uint32_t event_data)
{
    if(!(dev->status & FDC_ST_BUSY)) return;

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

#if 0
static void fdc_event_type4(uint32_t event_data)
{
}
#endif

static void fdc_register_multi_event()
{
    fdc_register_event(EVENT_MULTI1, 30);
}

static void fdc_event_multi1(uint32_t event_data)
{
    dev->secreg++;
    fdc_send_sector_reg();
    fdc_register_event(EVENT_MULTI2, 30);
}

static void fdc_event_multi2(uint32_t event_data)
{
    if(dev->cmdtype == FDC_CMD_RD_MSEC) {
        fdc_cmd_readdata();
    } else if(dev->cmdtype == FDC_CMD_WR_MSEC) {
        fdc_cmd_writedata();
    }
}

static void __not_in_flash_func(fdc_register_lost_event)(uint32_t usec)
{
#if 0
    uint32_t usec = (64 >> (dev->clk_num + dev->density));  // 8us * 8bit (1MHz, FM)
    if (bytes > 1) usec *= bytes;
#endif
    fdc_register_event(EVENT_LOST, usec);
}

static void __not_in_flash_func(fdc_cancel_lost_event)()
{
    fdc_cancel_event(EVENT_LOST);
}

static void fdc_event_lost(uint32_t event_data)
{
    if(dev->status & FDC_ST_BUSY) {
        printf("FDC5: LOST DATA (idx:%d)\n", dev->data_idx);
        dev->status |= FDC_ST_LOSTDATA;
        fdc_clr_busy();
        fdc_send_status();
        fdc_set_irq(dev->nmi_enable);
    }
}

#if 0
static void fdc_register_restore_event()
{
    if (dev->register_id[EVENT_RESTORE] == -1) {
//      dev->irq_mask = 1;
        fdc_register_event(EVENT_RESTORE, 32);
    }
}

static void fdc_event_restore_callback(uint32_t event_data)
{
    dev->register_id[EVENT_RESTORE] = -1;
    fdc_cmd_restore();
}
#endif

static void fdc_register_headeng_event(uint32_t usec)
{
    fdc_register_event(EVENT_HEADENG, usec);
}

static void fdc_event_headeng(uint32_t event_data)
{
//    printf("EVENT_HEADENG: CMD:%d\n", dev->cmdtype);
    switch(dev->cmdtype) {
    case FDC_CMD_RD_SEC:
    case FDC_CMD_RD_MSEC:
        fdc_cmd_readdata_headengd();
        break;
    case FDC_CMD_WR_SEC:
    case FDC_CMD_WR_MSEC:
        fdc_cmd_writedata_headengd();
        break;
    case FDC_CMD_RD_ADDR:
        fdc_cmd_readaddr_headengd();
        break;
    case FDC_CMD_RD_TRK:
        fdc_cmd_readtrack_headengd();
        break;
    case FDC_CMD_WR_TRK:
        fdc_cmd_writetrack_headengd();
        break;
    default:
        break;
    }
}

//--------------------------------------------------------------------

static void fdc_write_io_trkreg()
{
    OUT_DEBUG_WR(_T("FDCw\tTRACKREG=%d"), dev->trkreg);
    fdc_send_track_reg();
    if((dev->status & FDC_ST_BUSY) && dev->data_idx == 0) {
        // track reg is written after command starts
        if(dev->cmdtype == FDC_CMD_RD_SEC || dev->cmdtype == FDC_CMD_RD_MSEC || dev->cmdtype == FDC_CMD_WR_SEC || dev->cmdtype == FDC_CMD_WR_MSEC) {
            fdc_process_cmd();
        }
    }
}

static void fdc_write_io_sectreg()
{
    OUT_DEBUG_WR(_T("FDCw\tSECREG=%d"), dev->secreg);
    fdc_send_sector_reg();
    if((dev->status & FDC_ST_BUSY) && dev->data_idx == 0) {
        // sector reg is written after command starts
        if(dev->cmdtype == FDC_CMD_RD_SEC || dev->cmdtype == FDC_CMD_RD_MSEC || dev->cmdtype == FDC_CMD_WR_SEC || dev->cmdtype == FDC_CMD_WR_MSEC) {
            fdc_process_cmd();
        }
    }
}

static void fdc_write_io_datareg()
{
#ifdef _DEBUG_MB8866
    if(!(status & FDC_ST_DRQ)) {
        OUT_DEBUG_WR(_T("FDCw\tDATAREG=%d"), dev->datareg);
    }
#endif
    if((dev->status & FDC_ST_DRQ) && !dev->now_search) {
        dev->status &= ~FDC_ST_DRQ;
        fdc_set_drq(0);
        if(dev->cmdtype == FDC_CMD_WR_SEC || dev->cmdtype == FDC_CMD_WR_MSEC) {
            // write or multisector write
            if(dev->drv_sts & FDC_ST_WRITEP) {
                // write protect
//                  status |= FDC_ST_WRITEFAULT;
//              dev->status |= FDC_ST_WRITEP;
                fdc_clr_busy();
                fdc_send_status();
                fdc_cancel_lost_event();
//                  cmdtype = 0;
                fdc_set_irq(dev->nmi_enable);
            } else {
                disk_d88_write_data(dev->drv_num, dev->datareg);
                // set deleted mark
                if (dev->cmdreg & 1) {
                    disk_d88_set_deleted_mark(dev->drv_num, 1);
                }
            }

            dev->data_idx++;

            if(dev->data_idx >= disk_d88_get_sector_size(dev->drv_num)) {
                fdc_send_data_reg_directly();
                if(dev->cmdtype == FDC_CMD_WR_SEC) {
                    // single sector
                    OUT_DEBUG_WR(_T("FDC\tEND OF SECTOR (%d bytes wrote)"), dev->data_idx);
                    fdc_clr_busy();
                    fdc_send_status();
                    fdc_cancel_lost_event();
//                      cmdtype = 0;
                    fdc_set_irq(dev->nmi_enable);
                    disk_d88_delay_write_event(dev->drv_num);
                } else {
                    // multisector
                    OUT_DEBUG_WR(_T("FDC\tEND OF SECTOR (SEARCH NEXT)"));
                    fdc_cancel_lost_event();
                    fdc_register_multi_event();
                }
            } else {
                // next data
                fdc_send_data_reg_sync();
                fdc_register_drq_event();
            }
            fdc_send_status();
#ifdef USE_SIG_FLOPPY_ACCESS
            d_fdd->write_signal(SIG_FLOPPY_ACCESS | channel, 1, 1);
#endif
        }
        else if(dev->cmdtype == FDC_CMD_WR_TRK) {
            // write track
            if(dev->drv_sts & FDC_ST_WRITEP) {
                // write protect
//              dev->status |= FDC_ST_WRITEFAULT;
//              dev->status |= FDC_ST_WRITEP;
                fdc_clr_busy();
                fdc_send_status();
                fdc_cancel_lost_event();
//              cmdtype = 0;
                fdc_set_irq(dev->nmi_enable);

//              fdc_parse_track();
            } else {
                if (dev->density) {
                    if (dev->datareg == 0xf5) {
                        // address/data mark (missing clock)
                        dev->datareg = 0xa1;
                    } else if (dev->datareg == 0xf6) {
                        // index mark (missing clock)
                        dev->datareg = 0xc2;
                    } else if (dev->datareg == 0xf7) {
                        // TODO: crc
//                          int crc = 0; // calc_crc();
                    }
                }
//              disk_d88_write_track(dev->drv_num, 0xff);
            }

            dev->data_idx++;

            if(dev->data_idx >= disk_d88_get_track_size(dev->drv_num, (dev->clk_num << 1) | dev->density)) {
                // last data
                OUT_DEBUG_WR(_T("FDC\tEND OF TRACK (%d bytes wrote)"), dev->data_idx);
                fdc_send_data_reg_directly();
                fdc_clr_busy();
                fdc_send_status();
                fdc_cancel_lost_event();
//                  cmdtype = 0;
                fdc_set_irq(dev->nmi_enable);

                fdc_parse_track();

//              disk_d88_delay_write_event(dev->drv_num);
            } else {
                // next data
                fdc_send_data_reg_sync();
                fdc_register_drq_event();
            }
            fdc_send_status();
#ifdef USE_SIG_FLOPPY_ACCESS
            d_fdd->write_signal(SIG_FLOPPY_ACCESS | channel, 1, 1);
#endif
        }
    } else {
        fdc_send_data_reg_directly();
    }
//  if(!(dev->status & FDC_ST_DRQ)) {
//      fdc_set_drq(0);
//  }
}

static void __not_in_flash_func(fdc_write_io_unitsel)(uint8_t data)
{
    dev->drv_num = (data & 0x03);
    dev->sid_num = (data & FDC_UNIT_SIDE1 ? 1 : 0);
    dev->density = (data & FDC_UNIT_DDEN ? 1 : 0);
    dev->nmi_enable = (data & FDC_UNIT_NMI_MASK ? 0 : FDC_UNIT_IRQ);
    if (data & FDC_UNIT_MOTOR) {
//        dev->motor = 1;
        disk_drive_motor_on(dev->drv_num);
    } else {
        disk_drive_motor_off();
//        dev->motor = 0;
    }
    if (disk_d88_is_write_protected(dev->drv_num)) {
        dev->drv_sts |= FDC_ST_WRITEP;
    } else {
        dev->drv_sts &= ~FDC_ST_WRITEP;
    }
}

/// @brief 
/// @param addr 
/// @param data 
static void __no_inline_not_in_flash_func(fdc_write_io)(uint32_t addr, uint8_t data)
{
    switch(addr & 15) {
    case FDC_MB8877_CR:
        // command reg
        {
#ifdef HAS_MB8876
            dev->cmdreg = (~data) & 0xff;
#else
            dev->cmdreg = data;
#endif
            fdc_process_cmd();
        }
        break;
    case FDC_MB8877_TR:
        // track reg
        {
#ifdef HAS_MB8876
            dev->trkreg = (~data) & 0xff;
#else
            dev->secreg = data;
#endif
            fdc_write_io_trkreg();
        }
        break;
    case FDC_MB8877_SCR:
        // sector reg
        {
#ifdef HAS_MB8876
            dev->secreg = (~data) & 0xff;
#else
            dev->secreg = data;
#endif
            fdc_write_io_sectreg();
        }
        break;
    case FDC_MB8877_DR:
        // data reg
        {
#ifdef HAS_MB8876
            dev->datareg = (~data) & 0xff;
#else
            dev->datareg = data;
#endif
            fdc_write_io_datareg();
        }
        break;
    case FDC_5INCH_UNIT:
        // unit sel
        fdc_write_io_unitsel(data);
        break;
    }
}

//--------------------------------------------------------------------

/// @brief 
/// @param addr 
/// @return 
static uint8_t fdc_read_io(uint32_t addr)
{
    uint8_t data = 0xff;
    switch(addr & 15) {
    case FDC_MB8877_STR:
        {
            // status reg
            data = fdc_add_status();
        }
        break;
    case FDC_MB8877_TR:
        {
            // track reg
#ifdef HAS_MB8876
            data = (~dev->trkreg);
#else
            data = dev->trkreg;
#endif
        }
        break;
    case FDC_MB8877_SCR:
        {
            // sector reg
#ifdef HAS_MB8876
            data = (~dev->secreg);
#else
            data = dev->secreg;
#endif
        }
        break;
    case FDC_MB8877_DR:
        {
            // data reg
#ifdef HAS_MB8876
            data = (~dev->datareg);
#else
            data = dev->datareg;
#endif
        }
        break;
    case FDC_5INCH_UNIT:
        data = (~(dev->now_irq | dev->now_drq));
        break;
    default:
        break;
    }
    return data;
}

//--------------------------------------------------------------------

static void __not_in_flash_func(fdc_pre_read_data)()
{
    // read or multisector read
    disk_d88_read_data(dev->drv_num, &dev->datareg);
    fdc_send_data_reg_sync();
    dev->data_idx++;
}

static void fdc_pre_read_addr()
{
    // read address
    disk_d88_read_address(dev->drv_num, &dev->datareg);
    fdc_send_data_reg_sync();
    dev->data_idx++;
}

static void fdc_pre_read_track()
{
    // TODO: read track is not implemented
    dev->datareg = 0xff;
    fdc_send_data_reg_sync();
    dev->data_idx++;
}

static void __not_in_flash_func(fdc_post_read_data)()
{
    dev->status &= ~FDC_ST_DRQ;
    fdc_set_drq(0);
    fdc_send_status();
    if(dev->data_idx >= disk_d88_get_sector_size(dev->drv_num)) {
        // last data
        if(dev->cmdtype == FDC_CMD_RD_SEC) {
            // single sector
            OUT_DEBUG_RD(_T("FDC\tEND OF SECTOR (%d bytes read)\n"), dev->data_idx);
            fdc_clr_busy();
            fdc_send_status();
            fdc_cancel_lost_event();
            fdc_set_irq(dev->nmi_enable);
        } else {
            // multisector
            OUT_DEBUG_RD(_T("FDC\tEND OF SECTOR (SEARCH NEXT)\n"));
            fdc_cancel_lost_event();
            fdc_register_multi_event();
        }
    } else {
        // next data
        fdc_pre_read_data();
        fdc_register_drq_event();
    }
#ifdef USE_SIG_FLOPPY_ACCESS
    d_fdd->write_signal(SIG_FLOPPY_ACCESS | channel, 1, 1);
#endif
}

static void fdc_post_read_addr()
{
    dev->status &= ~FDC_ST_DRQ;
    fdc_set_drq(0);
    fdc_send_status();
    if(dev->data_idx >= 6) {
        // last data
        OUT_DEBUG_RD(_T("FDC\tEND OF ADDRESS (%d bytes read)\n"), dev->data_idx);
        fdc_clr_busy();
        fdc_send_status();
        fdc_cancel_lost_event();
        fdc_set_irq(dev->nmi_enable);
    } else {
        // next data
        fdc_pre_read_addr();
        fdc_register_drq_event();
    }
#ifdef USE_SIG_FLOPPY_ACCESS
    d_fdd->write_signal(SIG_FLOPPY_ACCESS | channel, 1, 1);
#endif
}

static void fdc_post_read_track()
{
    dev->status &= ~FDC_ST_DRQ;
    fdc_set_drq(0);
    fdc_send_status();
    if(dev->data_idx >= disk_d88_get_track_size(dev->drv_num, (dev->clk_num << 1) | dev->density)) {
        // last data
        OUT_DEBUG_RD(_T("FDC\tEND OF TRACK (%d bytes read)\n"), dev->data_idx);
        fdc_clr_busy();
        fdc_send_status();
        fdc_cancel_lost_event();
        fdc_set_irq(dev->nmi_enable);
    } else {
        // next data
        fdc_pre_read_track();
        fdc_register_drq_event();
    }
#ifdef USE_SIG_FLOPPY_ACCESS
    d_fdd->write_signal(SIG_FLOPPY_ACCESS | channel, 1, 1);
#endif
}

static void __no_inline_not_in_flash_func(fdc_post_read)(uint32_t addr, uint8_t data)
{
    switch(addr & 15) {
    case FDC_MB8877_STR:
        fdc_set_irq(0);
//      dev->status &= ~FDC_ST_DRQ;
//      fdc_set_drq(0);
//      fdc_send_status();
        break;
    case FDC_MB8877_DR:
        switch(dev->cmdtype) {
        case FDC_CMD_RD_SEC:
        case FDC_CMD_RD_MSEC:
            fdc_post_read_data();
            break;
        case FDC_CMD_RD_ADDR:
            fdc_post_read_addr();
            break;
        case FDC_CMD_RD_TRK:
            fdc_post_read_track();
            break;
        default:
            break;
        }
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

/// @brief 
/// @param addr 
/// @return will call post_read function if true
static bool MY_CORE1_FUNC(fdc_post_read_tightly)(uint8_t addr)
{
    bool sts = false;
    switch(addr & 15) {
    case FDC_MB8877_STR:
        sts = ((g_pio_parallel_read.odata[FDC_5INCH_UNIT] & FDC_UNIT_IRQ) == 0);
        // clear IRQ flag
        g_pio_parallel_read.odata[FDC_5INCH_UNIT] |= FDC_UNIT_IRQ;
        break;
    case FDC_MB8877_DR:
        sts = true;
        // clear DRQ flag
        g_pio_parallel_read.odata[FDC_5INCH_UNIT] |= FDC_UNIT_DRQ;
        break;
    default:
        break;
    }
    return sts;
}

static void MY_CORE1_FUNC(fdc_post_write_tightly)(uint8_t addr)
{
    switch(addr & 15) {
    case FDC_MB8877_CR:
        // command
        g_pio_parallel_read.odata[FDC_MB8877_STR] |= FDC_ST_BUSY;
        break;
    case FDC_MB8877_DR:
        // clear DRQ flag
        g_pio_parallel_read.odata[FDC_5INCH_UNIT] |= FDC_UNIT_DRQ;
        break;
    default:
        break;
    }
}

static void MY_CORE1_FUNC(fdc_notice_tightly)(uint32_t data)
{
}

//--------------------------------------------------------------------

static void __no_inline_not_in_flash_func(fdc_process_cmd)()
{
#ifdef _DEBUG_MB8866
    static const _TCHAR *cmdstr[0x10] = {
        _T("RESTORE "), _T("SEEK    "), _T("STEP    "), _T("STEP    "),
        _T("STEP IN "), _T("STEP IN "), _T("STEP OUT"), _T("STEP OUT"),
        _T("RD DATA "), _T("RD DATA "), _T("WR DATA "), _T("WR DATA "),
        _T("RD ADDR "), _T("FORCEINT"), _T("RD TRACK"), _T("WR TRACK")
    };
    OUT_DEBUG(_T("FDC\tCMD=%2xh (%s) DATA=%2xh TRK=%3d SEC=%2d"), cmdreg, cmdstr[cmdreg >> 4], datareg, trkreg, secreg);
#endif
//  fdc_cancel_event(EVENT_TYPE4);

    if ((dev->cmdreg & 0xf0) == 0xd0) {
        // type-4
        fdc_cmd_forceint();
        return;
    }

    if (dev->status & FDC_ST_BUSY) {
        return;
    }

    dev->status = 0;
    fdc_set_busy();
    fdc_send_status();
    fdc_set_irq(0);

    switch(dev->cmdreg & 0xf0) {
    // type-1
    case 0x00:
        fdc_cmd_restore();
        break;
    case 0x10:
        fdc_cmd_seek();
        break;
    case 0x20:
    case 0x30:
        fdc_cmd_step();
        break;
    case 0x40:
    case 0x50:
        fdc_cmd_stepin();
        break;
    case 0x60:
    case 0x70:
        fdc_cmd_stepout();
        break;
    // type-2
    case 0x80:
    case 0x90:
        fdc_cmd_readdata();
        break;
    case 0xa0:
    case 0xb0:
        fdc_cmd_writedata();
        break;
    // type-3
    case 0xc0:
        fdc_cmd_readaddr();
        break;
    case 0xe0:
        fdc_cmd_readtrack();
        break;
    case 0xf0:
        fdc_cmd_writetrack();
        break;
//  // type-4
//  case 0xd0:
//      fdc_cmd_forceint();
//      break;
    default:
        fdc_clr_busy();
        fdc_send_status();
        break;
    }
}

static uint32_t calc_head_engage_time(uint8_t status)
{
    if ((status & FDC_ST_HEADENG) == 0) {
        // wait until HLT becomes high
        return HEADENG_LONG_TIME;
    } else if (dev->cmdreg & 0x04) {
        // wait engage time
        return HEADENG_WAIT_TIME;
    } else {
        return HEADENG_SHORT_TIME;
    }
}

static void fdc_cmd_restore()
{
    // type-1 restore
    dev->cmdtype = FDC_CMD_TYPE1;
    if (dev->cmdreg & 8) {
        dev->drv_sts |= FDC_ST_HEADENG;
    } else {
        dev->drv_sts &= ~FDC_ST_HEADENG;
    }
    fdc_send_status();

    dev->trkreg = 0xff;
    fdc_send_track_reg();

    dev->seektrk = 0;
    dev->seekvct = 1;

    fdc_register_seek_event();
    fdc_register_seekend_event();

//  printf("FDCS: Restore Cmd:0x%02x\n", dev->cmdreg);
}

static void fdc_cmd_seek()
{
    // type-1 seek
    dev->cmdtype = FDC_CMD_TYPE1;
    if (dev->cmdreg & 8) {
        dev->drv_sts |= FDC_ST_HEADENG;
        dev->after_seek = 0;
    } else {
        dev->drv_sts &= ~FDC_ST_HEADENG;
        dev->after_seek = 1;
    }
    fdc_send_status();

    dev->seektrk = dev->datareg;
    dev->seekvct = (dev->datareg > dev->trkreg) ? 0 : 1;

    fdc_register_seek_event();
    fdc_register_seekend_event();

//  printf("FDCS: Seek Cmd:0x%02x\n", dev->cmdreg);
}

static void fdc_cmd_step()
{
    // type-1 step
    if(dev->seekvct) {
        fdc_cmd_stepout();
    } else {
        fdc_cmd_stepin();
    }
}

static void fdc_cmd_stepin()
{
    // type-1 step in
    dev->cmdtype = FDC_CMD_TYPE1;
    if (dev->cmdreg & 8) {
        dev->drv_sts |= FDC_ST_HEADENG;
    } else {
        dev->drv_sts &= ~FDC_ST_HEADENG;
    }
    fdc_send_status();

    dev->after_seek = ((dev->cmdreg & 8) ? 0 : 1);
    dev->seekvct = 0;

    fdc_register_seek_event();
    fdc_register_seekend_event();
}

static void fdc_cmd_stepout()
{
    // type-1 step out
    dev->cmdtype = FDC_CMD_TYPE1;
    if (dev->cmdreg & 8) {
        dev->drv_sts |= FDC_ST_HEADENG;
    } else {
        dev->drv_sts &= ~FDC_ST_HEADENG;
    }
    fdc_send_status();

    dev->after_seek = ((dev->cmdreg & 8) ? 0 : 1);
    dev->seekvct = 1;

    fdc_register_seek_event();
    fdc_register_seekend_event();
}

static void fdc_cmd_readdata()
{
    // type-2 read data
    dev->cmdtype = (dev->cmdreg & 0x10) ? FDC_CMD_RD_MSEC : FDC_CMD_RD_SEC;
    dev->status &= ~FDC_ST_DRQ;
    dev->status |= FDC_ST_BUSY;
    uint8_t old_drv_sts = dev->drv_sts;
    dev->drv_sts |= FDC_ST_HEADENG;
    fdc_set_drq(0);
    fdc_send_status();
    fdc_register_headeng_event(calc_head_engage_time(old_drv_sts));
}

static void fdc_cmd_readdata_headengd()
{
    dev->cmd_time = disk_drive_get_current_time(0);
    if(dev->cmdreg & 2) {
        dev->pre_sts = fdc_search_sector((dev->cmdreg & 8) ? 1 : 0);
    } else {
        dev->pre_sts = fdc_search_sector(- dev->sid_num - 1);
    }
    if(!(dev->pre_sts & FDC_ST_RECNFND)) {
        dev->pre_sts |= FDC_ST_BUSY;
        fdc_pre_read_data();
    }

    uint32_t usec = disk_drive_get_time_arrival_sector(dev->sec_pos, disk_d88_get_sector_nums(dev->drv_num), dev->cmd_time);
    fdc_register_search_data_event(FLG_DELAY_FDSEARCH ? 600 : usec);
    fdc_cancel_lost_event();
//  printf("FDC5: Read data: Pre_Sts:%02x After:%u\n", dev->pre_sts, usec);
}

static void fdc_cmd_writedata()
{
    // type-2 write data
    dev->cmdtype = (dev->cmdreg & 0x10) ? FDC_CMD_WR_MSEC : FDC_CMD_WR_SEC;
    dev->status &= ~FDC_ST_DRQ;
    dev->status |= FDC_ST_BUSY;
    uint8_t old_drv_sts = dev->drv_sts;
    dev->drv_sts |= FDC_ST_HEADENG;
    fdc_set_drq(0);
    fdc_send_status();
    fdc_register_headeng_event(calc_head_engage_time(old_drv_sts));
}

static void fdc_cmd_writedata_headengd()
{
    dev->cmd_time = disk_drive_get_current_time(0);
    if(dev->cmdreg & 2) {
        dev->pre_sts = fdc_search_sector((dev->cmdreg & 8) ? 1 : 0);
    }
    else {
        dev->pre_sts = fdc_search_sector(- dev->sid_num - 1);
    }
    dev->pre_sts &= ~FDC_ST_RECTYPE;
    if(!(dev->pre_sts & FDC_ST_RECNFND)) {
        dev->pre_sts |= FDC_ST_BUSY;
    }

    uint32_t usec = disk_drive_get_time_arrival_sector(dev->sec_pos, disk_d88_get_sector_nums(dev->drv_num), dev->cmd_time);
    fdc_register_search_data_event(FLG_DELAY_FDSEARCH ? 600 : usec);
    fdc_cancel_lost_event();
}

static void fdc_cmd_readaddr()
{
    // type-3 read address
    dev->cmdtype = FDC_CMD_RD_ADDR;
    dev->status &= ~FDC_ST_DRQ;
    dev->status |= FDC_ST_BUSY;
    uint8_t old_drv_sts = dev->drv_sts;
    dev->drv_sts |= FDC_ST_HEADENG;
    fdc_set_drq(0);
    fdc_send_status();

    dev->sec_nums = disk_d88_get_sector_nums(dev->drv_num);
    dev->sec_pos = disk_drive_get_current_sector_pos(dev->sec_nums);
    dev->sec_pos = (dev->sec_pos + 1) % dev->sec_nums;
    fdc_register_headeng_event(calc_head_engage_time(old_drv_sts));
}

static void fdc_cmd_readaddr_headengd()
{
    dev->cmd_time = disk_drive_get_current_time(0);
    dev->pre_sts = fdc_search_addr();
    if(!(dev->pre_sts & FDC_ST_RECNFND)) {
        dev->pre_sts |= FDC_ST_BUSY;
        fdc_pre_read_addr();
    }

    uint32_t usec = disk_drive_get_time_arrival_sector(dev->sec_pos, dev->sec_nums, dev->cmd_time);
    fdc_register_search_addr_event(usec);
    fdc_cancel_lost_event();
}

static void fdc_cmd_readtrack()
{
    // type-3 read track
    dev->cmdtype = FDC_CMD_RD_TRK;
    dev->status &= ~FDC_ST_DRQ;
    dev->status |= FDC_ST_BUSY;
    uint8_t old_drv_sts = dev->drv_sts;
    dev->drv_sts |= FDC_ST_HEADENG;
    fdc_set_drq(0);
    fdc_send_status();
    fdc_register_headeng_event(calc_head_engage_time(old_drv_sts));
}

static void fdc_cmd_readtrack_headengd()
{
    dev->cmd_time = disk_drive_get_current_time(0);
//  d_fdd->write_signal(SIG_FLOPPY_TRACK_SIZE | channel, 1, 1);
    dev->pre_sts = fdc_make_track();
    if(!dev->pre_sts) {
        dev->pre_sts |= FDC_ST_BUSY;
        fdc_pre_read_track();
    }

    uint32_t usec = disk_drive_get_index_hole_remain_time(dev->cmd_time);
    fdc_register_search_track_event(usec);
    fdc_cancel_lost_event();
}

static void fdc_cmd_writetrack()
{
    // type-3 write track
    dev->cmdtype = FDC_CMD_WR_TRK;
    dev->status &= ~FDC_ST_DRQ;
    dev->status |= FDC_ST_BUSY;
    uint8_t old_drv_sts = dev->drv_sts;
    dev->drv_sts |= FDC_ST_HEADENG;
    fdc_set_drq(0);
    fdc_send_status();
    fdc_register_headeng_event(calc_head_engage_time(old_drv_sts));
}

static void fdc_cmd_writetrack_headengd()
{
    dev->cmd_time = disk_drive_get_current_time(0);
//  d_fdd->write_signal(SIG_FLOPPY_TRACK_SIZE | channel, 1, 1);
    dev->data_idx = 0;
    dev->pre_sts = FDC_ST_BUSY;

    uint32_t usec = disk_drive_get_index_hole_remain_time(dev->cmd_time);
    fdc_register_search_track_event(usec);
    fdc_cancel_lost_event();
}

static void fdc_cmd_forceint()
{
    // type-4 force interrupt
    if(dev->cmdtype == 0 || dev->cmdtype == FDC_CMD_WR_SEC) {
        dev->status = 0;
        dev->cmdtype = FDC_CMD_TYPE1;
    }
    dev->status &= ~FDC_ST_DRQ;
    dev->drv_sts &= ~FDC_ST_HEADENG;
    fdc_clr_busy();
    fdc_send_status();
    fdc_set_drq(0);

    // force interrupt if bit0-bit3 is high
    if(dev->cmdreg & 0x0f) {
        fdc_set_irq(dev->nmi_enable);
    } else {
        fdc_set_irq(0);
    }
    for(int i=0; i<EVENT_MAX; i++) {
        fdc_cancel_event(i);
    }
    fifo_clear(&dev->event_fifo);
    
    dev->now_seek = 0;
    dev->now_search = 0;
//  fdc_register_event(EVENT_TYPE4, 100, fdc_event_type4_callback);

    msg_send_data_to_core1(MSG_TYPE_PARALLEL_NOTICE, MSG_NOTICE_RESET, 0); // reset
}

//--------------------------------------------------------------------
// busy
static void __not_in_flash_func(fdc_set_busy)()
{
    dev->status |= FDC_ST_BUSY;
    fdc_common_set_busy();
}

static void __not_in_flash_func(fdc_clr_busy)()
{
    dev->status &= ~FDC_ST_BUSY;
    fdc_common_clr_busy();
}

//--------------------------------------------------------------------

static void __not_in_flash_func(fdc_send_track_reg)()
{
    uint8_t trk = dev->trkreg;
#ifdef HAS_MB8876
    trk = ~trk;
#endif
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_MB8877_TR, trk);
}

static void __not_in_flash_func(fdc_send_sector_reg)()
{
    uint8_t sec = dev->secreg;
#ifdef HAS_MB8876
    sec = ~sec;
#endif
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_MB8877_SCR, sec);
}

static void __not_in_flash_func(fdc_send_data_reg_directly)()
{
    uint8_t dat = dev->datareg;
#ifdef HAS_MB8876
    dat = ~dat;
#endif
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_MB8877_DR, dat);
}

static void __not_in_flash_func(fdc_send_data_reg)()
{
    uint8_t dat = dev->datareg;
#ifdef HAS_MB8876
    dat = ~dat;
#endif
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_MB8877_DR, dat);
}

static void __not_in_flash_func(fdc_send_data_reg_sync)()
{
    uint8_t dat = dev->datareg;
#ifdef HAS_MB8876
    dat = ~dat;
#endif
    dev->wait_ack = 1;
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE_SYNC, FDC_MB8877_DR, dat);
}

static void __not_in_flash_func(fdc_set_irq)(uint8_t val)
{
    if (dev->now_irq != val) {
        dev->now_irq = val;
        msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_5INCH_UNIT, ~(dev->now_irq | dev->now_drq));
        if (val) {
            fdc_common_set_irq();
        } else {
            fdc_common_clr_irq();
        }
    }
}

static void __not_in_flash_func(fdc_set_drq)(uint8_t val)
{
    if (dev->now_drq != val) {
        dev->now_drq = val;
        msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_5INCH_UNIT, ~(dev->now_irq | dev->now_drq));
        if (val) {
#ifdef USE_DRQ_PIN
            fdc_common_set_drq();
#endif
            dev->drq_time = to_us_since_boot(get_absolute_time());
#ifdef USE_DRQ_PIN
        } else {
            fdc_common_clr_drq();
#endif
        }
    }
}

static uint8_t __not_in_flash_func(fdc_get_status)()
{
    uint8_t sts = 0;
    switch(dev->cmdtype) {
    case FDC_CMD_RD_SEC:
    case FDC_CMD_RD_MSEC:
    case FDC_CMD_RD_TRK:
    case FDC_CMD_RD_ADDR:
        sts = (dev->drv_sts & FDC_ST_NOTREADY);
        break;
    case FDC_CMD_WR_SEC:
    case FDC_CMD_WR_MSEC:
    case FDC_CMD_WR_TRK:
        sts = (dev->drv_sts & (FDC_ST_NOTREADY | FDC_ST_WRITEP));
        break;
    default:
        sts = dev->drv_sts;
        break;
    }
    return sts;
}

static uint8_t __not_in_flash_func(fdc_add_status)()
{
    uint8_t sts = dev->status;
    sts |= fdc_get_status();
#ifdef HAS_MB8876
    sts = ~sts;
#endif
    return sts;
}

static void __not_in_flash_func(fdc_send_status)()
{
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, FDC_MB8877_STR, fdc_add_status());
}

#if 0
static void fdc_update_status(uint8_t set_flags, uint8_t clr_flags)
{
    dev->status |= set_flags;
    dev->status &= ~clr_flags;
    fdc_send_status();
}
#endif

void fdc_5inch_set_callback()
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
    uint8_t sts = dev->drv_sts;
    switch(val) {
    case DISK_DRIVE_MOTOR:
        if (disk_d88_is_not_ready(dev->drv_num)) {
            sts |= FDC_ST_NOTREADY;
        } else {
            sts &= ~FDC_ST_NOTREADY;
        }
    
        break;
    case DISK_DRIVE_INDEX:
        sts |= FDC_ST_INDEX;
        break;
//  case DISK_DRIVE_HEAD_LOAD:
//      dev->drv_sts |= FDC_ST_HEADENG;
//      break;
    default:
        break;
    }
    dev->drv_sts = sts;
    fdc_send_status();
}

static void fdc_clr_status_callback(disk_drive_status_t val)
{
    uint8_t sts = dev->drv_sts;
    switch(val) {
    case DISK_DRIVE_MOTOR:
        sts |= FDC_ST_NOTREADY;
        break;
    case DISK_DRIVE_INDEX:
        sts &= ~FDC_ST_INDEX;
        break;
//  case DISK_DRIVE_HEAD_LOAD:
//      dev->drv_sts &= FDC_ST_HEADENG;
//      break;
    default:
        break;
    }
    dev->drv_sts = sts;
    fdc_send_status();
}
#endif

static void fdc_motor_on_callback(void)
{
    uint8_t sts = dev->drv_sts;
    if (disk_d88_is_not_ready(dev->drv_num)) {
        sts |= FDC_ST_NOTREADY;
    } else {
        sts &= ~FDC_ST_NOTREADY;
    }
    dev->drv_sts = sts;
    fdc_send_status();
}

static void fdc_motor_off_callback(void)
{
    uint8_t sts = dev->drv_sts;
    sts |= FDC_ST_NOTREADY;
    dev->drv_sts = sts;
    fdc_send_status();
}

static void __no_inline_not_in_flash_func(fdc_index_on_callback)(void)
{
    uint8_t sts = dev->drv_sts;
    sts |= FDC_ST_INDEX;
    dev->drv_sts = sts;
    fdc_send_status();
}

static void __no_inline_not_in_flash_func(fdc_index_off_callback)(void)
{
    uint8_t sts = dev->drv_sts;
    sts &= ~FDC_ST_INDEX;
    dev->drv_sts = sts;
    fdc_send_status();
}

//--------------------------------------------------------------------

/// @brief Verify track number if need
/// @return FDC status
static uint8_t fdc_verify_track(int side_number)
{
    // verify track number
    if(!(dev->cmdreg & 4)) {
        return 0;
    }
    if (disk_d88_verify_track(dev->drv_num, dev->trkreg, side_number)) {
        return FDC_ST_SEEKERR;
    }
    return 0;
}

/// @brief 
/// @param side_number
/// @return 
static uint8_t __not_in_flash_func(fdc_search_sector)(int side_number)
{
    // scan sectors
    int stat = disk_d88_read_sector(dev->drv_num, dev->trkreg, side_number, dev->secreg, dev->density, -1, false, &dev->sec_pos);
    if (stat != 1) {
        dev->data_idx = 0;
        return ((stat & 4) ? FDC_ST_RECTYPE : 0) | ((stat & 2) ? FDC_ST_CRCERR : 0);
    }
    // sector not found
//  if (FLG_ORIG_FDDRQ) set_irq(true);
    return FDC_ST_RECNFND;
}

/// @brief 
/// @return 
static uint8_t fdc_search_addr()
{
    dev->data_idx = 0;
    int stat = disk_d88_read_sector_id(dev->drv_num, dev->sid_num, dev->sec_pos);
    return stat ? FDC_ST_RECNFND : 0;
}

static uint8_t fdc_make_track()
{
    //d_fdd->make_track(channel);
    dev->data_idx = 0;
    return 0;
}

static int fdc_parse_track()
{
    return 0; //d_fdd->parse_track(channel);
}

static bool __not_in_flash_func(fdc_is_ready)()
{
    return (!disk_d88_is_not_ready(dev->drv_num) && disk_drive_is_motor_on());
}

//--------------------------------------------------------------------

void fdc_5inch_get_info()
{
    printf("[[[FDC 5inch]]]\n");
    printf("CMDREG:%02x TRKREG:%02x SECREG:%02x STATUS:%02x\n",
        dev->cmdreg,
        dev->trkreg,
        dev->secreg,
        dev->status);
    printf("UNIT: DRV:%d SIDE:%d DENSITY:%d NMIEN:%02x\n",
        dev->drv_num,
        dev->sid_num,
        dev->density,
        dev->nmi_enable);
}

uint8_t fdc_5inch_get_side_number()
{
    return dev->sid_num;
}
