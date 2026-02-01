/** @file common.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef COMMON_H
#define COMMON_H

#ifdef _MBS1
#define APPLICATION "S1VirtualFloppy"
#else
#define APPLICATION "L3VirtualFloppy"
#endif
#define VERSION "0.2.2"

#define MAX_DRIVES 2

#define USE_CORE1 1
//#define USE_SHARED_MEMORY 1
#ifdef USE_CORE1
#define USE_SIO_FIFO      1
#endif

#ifdef USE_CORE1
// place all core1 data to scratch_x (SRAM4):
// instruction codes to operate on core1
// data to access from core1
#define MY_CORE1_FUNC(name) __noinline __scratch_x(#name) name
#define MY_CORE1_GROUP(name) __scratch_x(#name) name
#else
#define MY_CORE1_FUNC(name) __no_inline_not_in_flash_func(name)
#define MY_CORE1_GROUP(name) __scratch_x(#name) name
#endif

#if defined(USE_SHARED_MEMORY)
#define MY_SHARED_MEMORY(name) __scratch_y(#name) name
#else
#define MY_SHARED_MEMORY(name) name
#endif

//#define debug_printf(...) printf(__VA_ARGS__)
//#define debug_puts(...) puts(__VA_ARGS__)
#define debug_printf(...)
#define debug_puts(...)

#define BIT_ONOFF(var, exp, bit) var = ((exp) ? (var | (bit)) : (var & ~(bit)))

#endif /* COMMON_H */
