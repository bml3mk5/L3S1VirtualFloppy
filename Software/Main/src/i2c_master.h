/**
 * @file i2c_master.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-15
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <hardware/i2c.h>
#include "simple_fifo.h"

// 1 : using interrupt
// 2 : using DMA
#define I2C_MASTER_TX_METHOD  2
#define I2C_MASTER_RX_METHOD  2

#define I2C_MASTER_TX_BUFFER_SFT 12
#define I2C_MASTER_TX_BUFFER_USIZE (1u << (I2C_MASTER_TX_BUFFER_SFT + 1 - sizeof(uint16_t)))
#define I2C_MASTER_TX_BUFFER_BSIZE (1u << I2C_MASTER_TX_BUFFER_SFT)

#define I2C_MASTER_RX_BUFFER_SFT 4
#define I2C_MASTER_RX_BUFFER_USIZE (1u << (I2C_MASTER_RX_BUFFER_SFT + 1 - sizeof(uint16_t)))
#define I2C_MASTER_RX_BUFFER_BSIZE (1u << I2C_MASTER_RX_BUFFER_SFT)

typedef struct st_i2c_master i2c_master_t;
typedef struct st_i2c_slave i2c_slave_t;

/// @brief callback after receiving data
/// @return if true then popd data from rxf
typedef bool (*i2c_master_received_callback_t)(simple_fifo_t *rxf);

#define TRANSQUEUE_SIZE (1u << 7)

enum en_transqueue_state {
    TRANS_STATE_IDLE = 0,
    TRANS_STATE_SEND,
    TRANS_STATE_RECV,
};
enum en_transqueue_flags {
    TRANS_FLAG_TX_STOP = 0x01,
};

/// @brief queue item for transaction information
typedef struct st_transqueue {
    uint8_t      state;
    uint8_t      flags;
    uint16_t     pos;
    uint16_t     len;
    i2c_slave_t *slave;
} transqueue_t;

/// @brief transaction information
typedef struct st_transaction {
    transqueue_t    current;
    simple_fifo_t   queue;
    transqueue_t    queue_buf[TRANSQUEUE_SIZE];
} transaction_t;

/// @brief i2c master instance
///
typedef struct st_i2c_master {
#if (I2C_MASTER_TX_METHOD == 2)
    // use DMA
    // ! instance need on aligned 2^n
    uint16_t tx_buf[I2C_MASTER_TX_BUFFER_USIZE] __aligned(I2C_MASTER_TX_BUFFER_BSIZE);
#else
    // use interrupt
    uint16_t tx_buf[I2C_MASTER_TX_BUFFER_USIZE];
#endif

#if (I2C_MASTER_RX_METHOD == 2)
    // use DMA
    // ! instance need on aligned 2^n
    uint16_t rx_buf[I2C_MASTER_RX_BUFFER_USIZE] __aligned(I2C_MASTER_RX_BUFFER_BSIZE);
#else
    // use interrupt
    uint16_t rx_buf[I2C_MASTER_RX_BUFFER_USIZE];
#endif
    simple_fifo_t txf;
    simple_fifo_t rxf;
#if (I2C_MASTER_TX_METHOD == 2)
    int dma_tx;
#endif
#if (I2C_MASTER_RX_METHOD == 2)
    int dma_rx;
#endif
    i2c_inst_t *i2c;
    uint32_t irq_num;   // IRQ number
    uint32_t irq_mask;  // IRQ mask
    uint32_t baudrate;  // Speed (Hz)
    transaction_t trans;
} i2c_master_t;

/// @brief i2c slave instance
///
typedef struct st_i2c_slave {
    i2c_master_t *master;
    uint32_t baud;  // Response speed
    uint16_t addr;
    uint8_t writeable;    // writable bytes
    uint8_t readable;     // readable bytes
    uint8_t dev_type;
    uint8_t reserved1;
    int16_t gpio_num;
    i2c_master_received_callback_t recvd;
} i2c_slave_t;

void i2c_master_init(i2c_master_t *obj, int scl_pin, int sda_pin, i2c_inst_t *i2c);
void i2c_master_wait_idle(i2c_master_t *obj);

bool i2c_master_tx_buffer_is_not_full(i2c_master_t *obj, size_t len);

bool i2c_master_check_device(i2c_master_t *obj, i2c_slave_t *slave);
bool i2c_master_check_device_flexible(i2c_master_t *obj, i2c_slave_t **slave_list);
void i2c_master_send_byte(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data);
void i2c_master_send_2bytes(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data, uint8_t arg);
void i2c_master_send_byte_blocking(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data);
void i2c_master_send_2bytes_blocking(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data, uint8_t arg);
void i2c_master_send_string(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, const char *str, size_t len);
void i2c_master_send_array(i2c_slave_t *slave, enum en_transqueue_state state, const uint8_t *arr, size_t len);
void i2c_master_recv_request_byte(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl);
bool i2c_master_recv_data_arrived(i2c_slave_t *slave);
uint16_t i2c_master_recv_byte(i2c_slave_t *slave);

void i2c_slave_init(i2c_master_t *master, i2c_slave_t *slave, int address, uint8_t writeable, uint8_t readable);
void i2c_slave_set_device_type(i2c_slave_t *slave, uint8_t type);
uint8_t i2c_slave_get_device_type(i2c_slave_t *slave);

#endif /* I2C_MASTER_H */
