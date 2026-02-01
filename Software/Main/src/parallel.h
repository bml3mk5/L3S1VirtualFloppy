/** @file parallel.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef PARALLEL_H
#define PARALLEL_H

#include <stdint.h>
#include <stdbool.h>

uint8_t parallel_read(uint8_t addr);
uint16_t parallel_read16(uint8_t addr);
void parallel_write(uint8_t addr, uint8_t data);

#endif /* PARALLEL_H */


