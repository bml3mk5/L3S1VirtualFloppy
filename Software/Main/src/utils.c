/** @file utils.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2025-01-01
 * 
 * @copyright Copyright (c) Sasaji 2025
 */

#include "utils.h"
#include <stdio.h>
#include <pico/sync.h>
#ifdef USE_HARDWARE_DIVIDER_DIRECTLY
#include <hardware/divider.h>
#endif
#include <pico/divider.h>

#ifdef USE_HARDWARE_DIVIDER_DIRECTLY
uint32_t __not_in_flash_func(div_u32)(uint32_t a, uint32_t b)
{
    hw_divider_state_t save;
    hw_divider_save_state(&save);
    hw_divider_divmod_u32_start(a, b);
    uint32_t result = to_quotient_u32(hw_divider_result_wait());
    hw_divider_restore_state(&save);
    return result;
}
#endif

#ifdef USE_HARDWARE_DIVIDER_DIRECTLY
uint32_t __not_in_flash_func(mod_u32)(uint32_t a, uint32_t b)
{
    hw_divider_state_t save;
    hw_divider_save_state(&save);
    hw_divider_divmod_u32_start(a, b);
    uint32_t result = to_remainder_u32(hw_divider_result_wait());
    hw_divider_restore_state(&save);
    return result;
}
#endif

//#define USE_HARDWARE_DIVIDER_FOR_CALC

#define SPLIT_DIGI(val, deci, arr) \
    if (val >= (8 * deci)) { \
        val -= (8 * deci); \
        arr+=8; \
    } \
    if (val >= (4 * deci)) { \
        val -= (4 * deci); \
        arr+=4; \
    } \
    if (val >= (2 * deci)) { \
        val -= (2 * deci); \
        arr+=2; \
    } \
    if (val >= (1 * deci)) { \
        val -= (1 * deci); \
        arr++; \
    }

void __not_in_flash_func(dec_str_6)(uint32_t val, char *str)
{
    uint32_t n[5] = {0, 0, 0, 0, 0};
    if (val > 999999) {
        val = 999999;
    }
#ifdef USE_HARDWARE_DIVIDER_FOR_CALC
    n[4] = divmod_u32u32_rem(val, 100000, &val);
    n[3] = divmod_u32u32_rem(val, 10000, &val);
    n[2] = divmod_u32u32_rem(val, 1000, &val);
    n[1] = divmod_u32u32_rem(val, 100, &val);
    n[0] = divmod_u32u32_rem(val, 10, &val);
#else
    SPLIT_DIGI(val, 100000, n[4])
    SPLIT_DIGI(val, 10000, n[3])
    SPLIT_DIGI(val, 1000, n[2])
    SPLIT_DIGI(val, 100, n[1])
    SPLIT_DIGI(val, 10, n[0])
#endif
    str[0] = n[4] + '0';
    str[1] = n[3] + '0';
    str[2] = n[2] + '0';
    str[3] = n[1] + '0';
    str[4] = n[0] + '0';
    str[5] = val + '0';
}

void __not_in_flash_func(dec_str_3)(uint32_t val, char *str)
{
    uint32_t n[2] = {0, 0};
    if (val > 999) {
        val = 999;
    }
#ifdef USE_HARDWARE_DIVIDER_FOR_CALC
    n[1] = divmod_u32u32_rem(val, 100, &val);
    n[0] = divmod_u32u32_rem(val, 10, &val);
#else
    SPLIT_DIGI(val, 100, n[1])
    SPLIT_DIGI(val, 10, n[0])
#endif
    str[0] = n[1] + '0';
    str[1] = n[0] + '0';
    str[2] = val + '0';
}

void __not_in_flash_func(dec_str_2)(uint32_t val, char *str)
{
    uint32_t n[1] = {0};
    if (val > 99) {
        val = 99;
    }
#ifdef USE_HARDWARE_DIVIDER_FOR_CALC
    n[0] = divmod_u32u32_rem(val, 10, &val);
#else
    SPLIT_DIGI(val, 10, n[0])
#endif
    str[0] = n[0] + '0';
    str[1] = val + '0';
}

void dump_data(const uint8_t *data, uint32_t size, uint32_t start_addr)
{
    uint32_t pos = 0;
    uint32_t n;
    for(; pos<size; pos+=16) {
        printf("0x%04x:", start_addr + pos);
        for(n=0; n<16; n++) {
            printf(" %02x", data[pos + n]);
        }
        printf(" ");
        for(n=0; n<16; n++) {
            uint8_t c = data[pos + n];
            printf("%c", c >= 0x20 && c < 0x80 ? c : '.');
        }
        printf("\n");
    }

}
