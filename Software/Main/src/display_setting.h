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

extern const char *setting_list_disk_type[];

void display_setting_init(void);
void display_setting_change_phase(void);
void display_setting_move(int dir);
void display_setting_confirm(void);
void display_setting_confirm_long(void);
void display_setting_task(void);

#endif /* DISPLAY_SETTING_H */
