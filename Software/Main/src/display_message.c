/**
 * @file display_message.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2026-01-05
 * 
 * @copyright Copyright (c) Sasaji 2026
 * 
 */

#include "display_message.h"
#include <string.h>
#include <stdint.h>
#include "display.h"
#include "display_storage.h"
#include "config.h"
#include "event.h"
#include "common.h"

static struct {
    int prev_phase;
    alarm_id_t event_id;
    bool updated;
    char buff[2][16];
} message_info;

void display_message_init(void)
{
    message_info.prev_phase = -1;
    message_info.event_id = -1;
    message_info.updated = false;
}

static void display_message_exit(void)
{
    display_info.phase = message_info.prev_phase;
    message_info.prev_phase = -1;
    display_lcd_change_phase();
}

static int64_t display_message_event_callback(alarm_id_t id, void *user_data)
{
    display_message_exit();
    return 0;
}

void display_message_change_phase(void)
{
    if (display_info.phase != PHASE_MESSAGE) {
        message_info.prev_phase = display_info.phase;
    }
    message_info.updated = true;
    display_info.phase = PHASE_MESSAGE;
    event_cancel_event(&message_info.event_id);
    message_info.event_id = event_register_event(3000000, display_message_event_callback, 0);
}

void display_message_task(void)
{
    if (!message_info.updated) return;
    lcd_locate_substring(0, 0, message_info.buff[0], 16);
    lcd_locate_substring(0, 1, message_info.buff[1], 16);
    message_info.updated = false;
}

void display_message_confirm(void)
{
    event_cancel_event(&message_info.event_id);
    display_message_exit();
}

//--------------------------------------------------------------------

static const char *err_msgs[] = {
    // --------------
    "No error",
    "Save to flash",
    "Unknown",
    NULL
};

void display_error_message(int err_num)
{
    if (err_num >= ERR_MESSAGE_MAX) {
        err_num = ERR_UNKNOWN;
    }
    size_t len = strlen(err_msgs[err_num]);
    if (len > 16) len = 16;

    sprintf(message_info.buff[0], "ERROR%02d:        ", err_num);
    memset(message_info.buff[1], 0x20, sizeof(message_info.buff[1]));
    memcpy(message_info.buff[1], err_msgs[err_num], len);

    display_message_change_phase();
}