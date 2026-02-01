/**
 * @file display_reset.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-23
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "display_reset.h"
#include <string.h>
#include "display.h"
#include "display_storage.h"
#include "display_setting.h"
#include "config.h"
#include "event.h"
#include "common.h"

static int reset_phase;
static alarm_id_t reset_id;
static bool reset_disp;

void display_reset_init(void)
{
    reset_phase = -1;
    reset_id = -1;
    reset_disp = false;
}

static int64_t display_reset_event_callback(alarm_id_t id, void *user_data)
{
    display_info.phase = reset_phase;
    reset_phase = -1;
    display_lcd_change_phase();
    return 0;
}

void display_reset_change_phase(void)
{
    if (display_info.phase != PHASE_RESET) {
        reset_phase = display_info.phase;
    }
    reset_disp = false;
    display_info.phase = PHASE_RESET;
    event_cancel_event(&reset_id);
    reset_id = event_register_event(1000000, display_reset_event_callback, 0);
}

void display_reset_task(void)
{
    char str[20];

    if (reset_disp) return;

    uint8_t type = config_get_disk_type();

    strcpy(str, "Reset  FDC is:  ");
    lcd_locate_substring(0, 0, str, 16);

    sprintf(str, "%u ", type);
    strcat(str, setting_list_disk_type[type]);
    lcd_locate_substring(0, 1, str, 16);

    reset_disp = true;
}
