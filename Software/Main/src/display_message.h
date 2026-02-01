/**
 * @file display_message.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2026-01-05
 * 
 * @copyright Copyright (c) Sasaji 2026
 * 
 */

#ifndef DISPLAY_MESSAGE_H
#define DISPLAY_MESSAGE_H

enum en_display_error_number {
    ERR_NONE = 0,
    ERR_CANNOT_SAVE_FLASH,
    ERR_UNKNOWN,
    ERR_MESSAGE_MAX
};

void display_message_init(void);
void display_message_change_phase(void);
void display_message_task(void);

void display_message_confirm(void);

void display_error_message(int err_num);

#endif /* DISPLAY_MESSAGE_H */
