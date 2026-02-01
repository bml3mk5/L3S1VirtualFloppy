/** @file main.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>

void main_loop_contents_in_shell_cmd(void);

enum en_halt_signals {
    HALT_SIGNAL_POR = 0x01,
    HALT_SIGNAL_8INCH = 0x02,
};

void halt_signal_on(uint8_t mask);
void halt_signal_off(uint8_t mask);
void halt_signal_on_core1();
bool is_halt_signal_on();

bool some_signals_fdc_is_enable(void);

#endif /* MAIN_H */
