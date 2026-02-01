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

void __not_in_flash_func(dec_str_2)(uint32_t val, char *str)
{
//    str[0] = ((val / 10) % 10) + 0x30;
//    str[1] = (val % 10) + 0x30;
    uint32_t n = 0;
    if (val > 99) {
        val = 99;
    }
    if (val >= 80) {
        val -= 80;
        n+=8;
    }
    if (val >= 40) {
        val -= 40;
        n+=4;
    }
    if (val >= 20) {
        val -= 20;
        n+=2;
    }
    if (val >= 10) {
        val -= 10;
        n++;
    }
    str[0] = n + '0';
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
