/**
 * @file config.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-11-24
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#ifndef CONFIG_H
#define CONFIG_H

#define USE_FATFS 1

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

void config_init(void);
bool config_load(void);
bool config_save(void);

void config_flash_save();

void config_set_path(int drv, const char *path);
void config_get_path(int drv, char *path, size_t size);
char *config_get_path_ptr(int drv);
size_t config_get_path_size(void);
void config_set_side_number(int drv, uint8_t val);
uint8_t config_get_side_number(int drv);
void config_set_disk_type(uint8_t val);
uint8_t config_get_disk_type(void);
void config_set_seek_track(uint8_t val);
uint8_t config_get_seek_track(void);
void config_set_search_sector(uint8_t val);
uint8_t config_get_search_sector(void);
void config_set_data_request(uint8_t val);
uint8_t config_get_data_request(void);
void config_set_i2c_ssd1306_type(uint8_t val);
uint8_t config_get_i2c_ssd1306_type(void);

void config_debug_info(void);

#endif /* CONFIG_H */
