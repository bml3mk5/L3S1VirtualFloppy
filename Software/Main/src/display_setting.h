/**
 * @file display_setting.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-23
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#ifndef DISPLAY_SETTING_H
#define DISPLAY_SETTING_H

void display_setting_init(void);
void display_setting_change_phase(void);
void display_setting_move(int dir);
void display_setting_exit(void);
void display_setting_confirm(void);
void display_setting_task(void);

void display_reset_init(void);
void display_reset_change_phase(void);
void display_reset_task(void);

#endif /* DISPLAY_SETTING_H */
