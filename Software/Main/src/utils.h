/** @file utils.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-01-01
 * 
 * @copyright Copyright (c) Sasaji 2025
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>

#define USE_HARDWARE_DIVIDER_DIRECTLY

#ifdef USE_HARDWARE_DIVIDER_DIRECTLY
uint32_t div_u32(uint32_t a, uint32_t b);
uint32_t mod_u32(uint32_t a, uint32_t b);
#else
#define div_u32(a, b) ((a) / (b))
#define mod_u32(a, b) ((a) % (b))
#endif

void dec_str_6(uint32_t val, char *str);
void dec_str_3(uint32_t val, char *str);
void dec_str_2(uint32_t val, char *str);

void dump_data(const uint8_t *data, uint32_t size, uint32_t start_addr);

#define TO_LE16(val) (val)
#define TO_LE32(val) (val)

#endif /* UTILS_H */
