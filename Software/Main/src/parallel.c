/** @file parallel.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "parallel.h"
#include "main.h"
#include "msg_bridge.h"
#include "fdc_common.h"

static const uint8_t c_parallel_reg_map[8][16] = {
    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2HD
   { 0, 1, 2, 3, 4, 4, 4, 4,14,14,14,14, 5, 5, 5, 5, },
    // 3inch 1S
   { 8, 8, 8, 8, 8, 8, 8, 8, 0, 1, 2, 3, 4, 5, 6, 7, },

    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2D
   { 0, 1, 2, 3, 4, 4, 4, 4,15,15,15,15,15,15,15,15, },
    // 5inch 2HD (masked)
   {15,15, 6, 3, 4, 4, 4, 4,14,14,14,14, 5, 5, 5, 5, },
    // 3inch 1S
   { 8, 8, 8, 8, 8, 8, 8, 8, 0, 1, 2, 3, 4, 5, 6, 7, },
};

uint8_t parallel_read(uint8_t addr)
{
    uint8_t type = fdc_common_get_disk_type_and_shift();
    addr = c_parallel_reg_map[type][addr & 15];
    uint8_t data = fdc_common_read_io_callback(addr);
    msg_send_data(MSG_TYPE_PARALLEL_READ, addr, data);
    main_loop_contents_in_shell_cmd();
    return data;
}

uint16_t parallel_read16(uint8_t addr)
{
    uint8_t type = fdc_common_get_disk_type_and_shift();
    uint8_t addr0 = c_parallel_reg_map[type][addr & 15];
    uint8_t data0 = fdc_common_read_io_callback(addr0);
    msg_send_data(MSG_TYPE_PARALLEL_READ, addr0, data0);
    addr++;
    uint8_t addr1 = c_parallel_reg_map[type][addr & 15];
    uint8_t data1 = fdc_common_read_io_callback(addr1);
    msg_send_data(MSG_TYPE_PARALLEL_READ, addr1, data1);
    main_loop_contents_in_shell_cmd();
    return (((uint16_t)data0 << 8) & 0xff00) | (data1 & 0xff);
}

void parallel_write(uint8_t addr, uint8_t data)
{
    uint8_t type = fdc_common_get_disk_type_and_shift();
    addr = c_parallel_reg_map[type][addr & 15];
    msg_send_data(MSG_TYPE_PARALLEL_WRITE, addr, data);
    msg_send_data_to_core1(MSG_TYPE_PARALLEL_WRITE, addr, data);
    main_loop_contents_in_shell_cmd();
}
