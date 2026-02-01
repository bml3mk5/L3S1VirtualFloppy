/**
 * @file display_menu.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2026-01-12
 * 
 * @copyright Copyright (c) Sasaji 2026
 * 
 */

#ifndef DISPLAY_MENU_H
#define DISPLAY_MENU_H

#include <stdint.h>

void display_menu_init(void);
void display_menu_change_phase(void);
void display_menu_move(int dir);
void display_menu_exit(void);
void display_menu_confirm(void);
void display_menu_confirm_long(void);
void display_menu_task(void);

void display_menu_yesno_message(const char *msg, int16_t yesno);

#endif /* DISPLAY_MENU_H */
