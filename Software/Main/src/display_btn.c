/**
 * @file display_btn.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-24
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "display_btn.h"
#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "display.h"
#include "display_storage.h"
#include "display_menu.h"
#include "display_setting.h"
#include "display_message.h"
#include "i2c_led_btn.h"

#define BTN_LEFT 0
#define BTN_RIGHT 4
#define BTN_DESIDE 3
#define BTN_MASK ((1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_DESIDE))

#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
#define BTN_SCAN_INTERVAL I2C_BUTTON_INTERVAL_MS
// 1st repeat count (500ms)
#define BTN_1ST_REPEAT (600 / BTN_SCAN_INTERVAL)
// 2nd repeat count
#define BTN_2ND_REPEAT (120 / BTN_SCAN_INTERVAL)
// long click count (2000ms)
#define BTN_LONG_CLICK (2100 / BTN_SCAN_INTERVAL)
#else
#define BTN_SCAN_INTERVAL 125
// 1st repeat count (500ms)
#define BTN_1ST_REPEAT (500 / BTN_SCAN_INTERVAL)
// 2nd repeat count
#define BTN_2ND_REPEAT (125 / BTN_SCAN_INTERVAL)
// long click count (2000ms)
#define BTN_LONG_CLICK (2000 / BTN_SCAN_INTERVAL)
#endif

static struct {
    uint8_t btn;
    uint8_t nbtn;
    uint8_t rptcnt;
    uint8_t longclk;
} display_btn = {
    0, 0, 0, 0
};

void display_btn_init(void)
{
}

static void display_btn_press_left(void)
{
    switch(display_info.phase) {
    case PHASE_MENU:
        display_menu_move(-1);
        break;
    case PHASE_SETTING:
        display_setting_move(-1);
        break;
    case PHASE_STORAGE:
        display_filelist_move(-1);
        break;
    default:
        break;
    }
}

static void display_btn_press_right(void)
{
    switch(display_info.phase) {
    case PHASE_MENU:
        display_menu_move(1);
        break;
    case PHASE_SETTING:
        display_setting_move(1);
        break;
    case PHASE_STORAGE:
        display_filelist_move(1);
        break;
    default:
        break;
    }
}

static void display_btn_press_confirm_left(void)
{
    switch(display_info.phase) {
    case PHASE_STORAGE:
        display_filelist_confirm_move(-1);
        break;
    default:
        break;
    }
}

static void display_btn_press_confirm_right(void)
{
    switch(display_info.phase) {
    case PHASE_STORAGE:
        display_filelist_confirm_move(1);
        break;
    default:
        break;
    }
}

static void display_btn_press_confirm_short(void)
{
    switch(display_info.phase) {
    case PHASE_MENU:
        display_menu_confirm();
        break;
    case PHASE_SETTING:
        display_setting_confirm();
        break;
    case PHASE_STORAGE:
        display_filelist_confirm();
        break;
    case PHASE_MESSAGE:
        display_message_confirm();
        break;
    default:
        break;
    }
}

static void display_btn_press_confirm_long(void)
{
    switch(display_info.phase) {
    case PHASE_MENU:
        display_menu_confirm_long();
        break;
    case PHASE_SETTING:
        display_setting_confirm_long();
        break;
    case PHASE_STORAGE:
        display_filelist_confirm_long();
        break;
    default:
        break;
    }
}

/// @brief Check status of I2C Button and process pressing a button
void __not_in_flash_func(display_btn_task)(void)
{
#if (I2C_BUTTON_DOUBLE_BUFFERING == 1)
    if (!i2c_led_btn_btn_arrived(&i2c_led_btn)) {
        return;
    }
    uint8_t nbtn = i2c_led_btn_get_btn(&i2c_led_btn);
    if ((nbtn & BTN_MASK) != BTN_MASK) {
        display_btn.nbtn = nbtn & BTN_MASK;
    }
#else
    const uint32_t interval_ms = BTN_SCAN_INTERVAL;
    static uint32_t start_ms = 0;

    if (i2c_led_btn_btn_arrived(&i2c_led_btn)) {
        uint8_t nbtn = i2c_led_btn_get_btn(&i2c_led_btn);
        if ((nbtn & BTN_MASK) != BTN_MASK) {
            display_btn.nbtn = nbtn & BTN_MASK;
        }
    }

    // Blink every interval ms
    if (g_c0_current_time_ms - start_ms < interval_ms) {
        return;
    }
    start_ms += interval_ms;

    i2c_led_btn_request_btn(&i2c_led_btn);
#endif

//  printf("BTN: N%02x B%02x C%d\n",display_btn.nbtn,display_btn.btn,display_btn.cnt);

    // left or right button

    uint8_t btn_diff = ((display_btn.nbtn ^ display_btn.btn) & ((1 << BTN_LEFT) | (1 << BTN_RIGHT)));
    if (btn_diff != 0 || display_btn.rptcnt == 0) {
        // button status is changed or repeat count is zero
        if (display_btn.nbtn & (1 << BTN_LEFT)) {
            if (display_btn.nbtn & (1 << BTN_DESIDE)) {
                // with pressing deside
                display_btn_press_confirm_left();
                display_btn.longclk = BTN_LONG_CLICK + 1;
            } else {
                display_btn_press_left();
            }
        }
        else if (display_btn.nbtn & (1 << BTN_RIGHT)) {
            if (display_btn.nbtn & (1 << BTN_DESIDE)) {
                // with pressing deside
                display_btn_press_confirm_right();
                display_btn.longclk = BTN_LONG_CLICK + 1;
            } else {
                display_btn_press_right();
            }
        }
        if (btn_diff != 0) {
            display_btn.rptcnt = BTN_1ST_REPEAT;
        } else {
            display_btn.rptcnt = BTN_2ND_REPEAT;
        }
    } else {
        if (display_btn.nbtn & ((1 << BTN_LEFT) | (1 << BTN_RIGHT))) {
            // pressing
            if (display_btn.rptcnt) {
                display_btn.rptcnt--;
            }
        } else {
            display_btn.rptcnt = 0;
        }
    }

    // confirm button

//    if (display_btn.nbtn & (~display_btn.btn) & (1 << BTN_DESIDE)) {
//        // pressed
//        display_btn.longclk = 0;
    if (display_btn.longclk == BTN_LONG_CLICK) {
        // long pressed
        display_btn.longclk++;
        display_btn_press_confirm_long();
    } else if ((~display_btn.nbtn) & display_btn.btn & (1 << BTN_DESIDE)) {
        // released
        if (display_btn.longclk > BTN_LONG_CLICK) {

        } else if (display_btn.longclk == BTN_LONG_CLICK) {
            display_btn.longclk++;
            display_btn_press_confirm_long();
        } else {
            // short pressed
            display_btn_press_confirm_short();
        }
    } else if (display_btn.nbtn & display_btn.btn & (1 << BTN_DESIDE)) {
        // pressing
        if (display_btn.longclk < BTN_LONG_CLICK) {
            display_btn.longclk++;
        }
    } else {
        display_btn.longclk = 0;
    }

    display_btn.btn = display_btn.nbtn;
}
