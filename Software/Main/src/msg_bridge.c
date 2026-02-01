/** @file msg_bridge.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "msg_bridge.h"
#include <pico/multicore.h>
#include "main.h"
#include "fdc_common.h"
#include "pio_ctrls.h"
#include "simple_fifo.h"

#if defined(USE_SHARED_MEMORY)

uint8_t MY_SHARED_MEMORY(shared_c0to1_buf)[8];
uint8_t MY_SHARED_MEMORY(shared_c1to0_buf)[8];
uint32_t MY_SHARED_MEMORY(shared_c0to1_flags);
uint32_t MY_SHARED_MEMORY(shared_c1to0_wflags);
uint32_t MY_SHARED_MEMORY(shared_c1to0_rflags);

#define MY_MUTEX_LOCK(lock_num) { \
    uint32_t temp_save_status = save_and_disable_interrupts(); \
    while(!sio_hw->spinlock[lock_num]){__nop();} \
    __dmb();
#define MY_MUTEX_UNLOCK(lock_num) \
    __dmb(); \
    sio_hw->spinlock[lock_num] = 0; \
    restore_interrupts_from_disabled(temp_save_status); }

#ifdef USE_CORE1
int MY_CORE1_GROUP(lock_c0to1_core1);
int MY_CORE1_GROUP(lock_c1to0_core1);
#endif
int lock_c0to1_core0;
int lock_c1to0_core0;

#else /* !USE_SHARED_MEMORY */

#define MSG_FIFO_BUFFER_SIZE 32
uint32_t fifo_c1_to_c0_buf[MSG_FIFO_BUFFER_SIZE];
simple_fifo_t fifo_c1_to_c0;
#ifndef USE_CORE1
uint32_t fifo_c0_to_c1_buf[MSG_FIFO_BUFFER_SIZE];
simple_fifo_t fifo_c0_to_c1;
#endif

#endif /* USE_SHARED_MEMORY */

void msg_init(void)
{
#if defined(USE_SHARED_MEMORY)

    memset(shared_c0to1_buf, 0, sizeof(shared_c0to1_buf));
    memset(shared_c1to0_buf, 0, sizeof(shared_c1to0_buf));
    shared_c0to1_flags = 0;
    shared_c1to0_wflags = 0;
    shared_c1to0_rflags = 0;

    lock_c0to1_core0 = spin_lock_claim_unused(true);
    lock_c1to0_core0 = spin_lock_claim_unused(true);
#ifdef USE_CORE1
    lock_c0to1_core1 = lock_c0to1_core0;
    lock_c1to0_core1 = lock_c1to0_core0;
#endif
    printf("Lock: c0to1:%d c1to0:%d\n", lock_c0to1_core0, lock_c1to0_core0);
    sio_hw->spinlock[lock_c0to1_core0] = 1; // release lock
    sio_hw->spinlock[lock_c1to0_core0] = 1; // release lock

#else /* !USE_SHARED_MEMORY */

    fifo_init(&fifo_c1_to_c0, fifo_c1_to_c0_buf, MSG_FIFO_BUFFER_SIZE, sizeof(uint32_t));
#ifndef USE_CORE1
    fifo_init(&fifo_c0_to_c1, fifo_c0_to_c1_buf, MSG_FIFO_BUFFER_SIZE, sizeof(uint32_t));
#endif

#endif /* USE_SHARED_MEMORY */
}

#ifdef USE_CORE1
void MY_CORE1_FUNC(msg_send_data_to_core0)(uint32_t type, uint8_t addr, uint8_t data)
#else
void __no_inline_not_in_flash_func(msg_send_data_to_core0)(uint32_t type, uint8_t addr, uint8_t data)
#endif
{
#if defined(USE_SHARED_MEMORY)

    switch(type) {
    case MSG_TYPE_PARALLEL_WRITE:
        MY_MUTEX_LOCK(lock_c1to0_core1);
        shared_c1to0_buf[addr] = data;
        shared_c1to0_wflags |= (1 << addr);
        MY_MUTEX_UNLOCK(lock_c1to0_core1);
        break;
    case MSG_TYPE_PARALLEL_READ:
        MY_MUTEX_LOCK(lock_c1to0_core1);
        shared_c1to0_rflags |= (1 << addr);
        MY_MUTEX_UNLOCK(lock_c1to0_core1);
        break;
    default:
        break;
    }

#elif defined(USE_SIO_FIFO)

//  multicore_fifo_push_blocking(type | ((uint32_t)addr << 8) | data);
    if (multicore_fifo_wready()) {
        sio_hw->fifo_wr = (type | ((uint32_t)addr << 8) | data);
//      __sev();
    }
//  multicore_fifo_push_timeout_us(type | ((uint32_t)addr << 8) | data, 1);

#else

    fifo_push32(&fifo_c1_to_c0, type | ((uint32_t)addr << 8) | data);

#endif
}

void __not_in_flash_func(msg_send_data_to_core1)(uint32_t type, uint8_t addr, uint8_t data)
{
#if defined(USE_SHARED_MEMORY)

    switch(type) {
    case MSG_TYPE_PARALLEL_WRITE:
        MY_MUTEX_LOCK(lock_c0to1_core0);
        shared_c0to1_buf[addr] = data;
        shared_c0to1_flags |= (1 << addr);
        MY_MUTEX_UNLOCK(lock_c0to1_core0);
//      printf("R:%d:%d\n", addr, data);
        break;
    case MSG_TYPE_PARALLEL_NOTICE:
        if (multicore_fifo_wready()) {
        sio_hw->fifo_wr = (type | ((uint32_t)addr << 8) | data);
        }
        break;
    default:
        break;
    }

#elif defined(USE_SIO_FIFO)

//  multicore_fifo_push_blocking(type | ((uint32_t)addr << 8) | data);
    if (multicore_fifo_wready()) {
        sio_hw->fifo_wr = (type | ((uint32_t)addr << 8) | data);
//      __sev();
    }
//  multicore_fifo_push_timeout_us(type | ((uint32_t)addr << 8) | data, 1);

#else

    fifo_push32(&fifo_c0_to_c1, type | ((uint32_t)addr << 8) | data);

#endif
}

void __not_in_flash_func(msg_send_data)(uint32_t type, uint8_t addr, uint8_t data)
{
#if defined(USE_SHARED_MEMORY)

    switch(type) {
    case MSG_TYPE_PARALLEL_WRITE:
        MY_MUTEX_LOCK(lock_c1to0_core1);
        shared_c1to0_buf[addr] = data;
        shared_c1to0_wflags |= (1 << addr);
        MY_MUTEX_UNLOCK(lock_c1to0_core1);
        break;
    case MSG_TYPE_PARALLEL_READ:
        MY_MUTEX_LOCK(lock_c1to0_core1);
        shared_c1to0_rflags |= (1 << addr);
        MY_MUTEX_UNLOCK(lock_c1to0_core1);
        break;
    default:
        break;
    }

#else

    fifo_push32(&fifo_c1_to_c0, type | ((uint32_t)addr << 8) | data);

#endif
}

/// @brief Receive register data from core1 / Send data to core1
void __no_inline_not_in_flash_func(msg_task)(void)
{
#if defined(USE_SHARED_MEMORY)
    uint8_t data;
#else
    uint32_t data;
#endif

#if defined(USE_SHARED_MEMORY)

    for(int addr=4; addr>=0; addr--) {
        if (shared_c1to0_wflags & (1 << addr)) {
            data = shared_c1to0_buf[addr];
            MY_MUTEX_LOCK(lock_c1to0_core0);
            shared_c1to0_wflags &= ~(1 << addr);
            MY_MUTEX_UNLOCK(lock_c1to0_core0);
            fdc_common_write_io_callback(addr, data);
//          printf("W: %02x:%02x\n",addr, data);
        }
        __nop();
        if (shared_c1to0_rflags & (1 << addr)) {
            MY_MUTEX_LOCK(lock_c1to0_core0);
            shared_c1to0_rflags &= ~(1 << addr);
            MY_MUTEX_UNLOCK(lock_c1to0_core0);
            fdc_common_post_read_callback(addr, data);
        }
        __nop();
    }

#elif defined(USE_SIO_FIFO)

    while (multicore_fifo_rvalid()) {
        fifo_push32(&fifo_c1_to_c0, sio_hw->fifo_rd);
    }

#endif

#if !defined(USE_SHARED_MEMORY)

    while (fifo_is_not_empty(&fifo_c1_to_c0)) {
        data = fifo_pop32(&fifo_c1_to_c0);
        switch (data & MSG_TYPE_MASK) {
        case MSG_TYPE_PARALLEL_WRITE:
            fdc_common_write_io_callback((data >> 8) & 0xff, data & 0xff);
//          printf("W: %02x:%02x\n",(data >> 8) & 0xff, data & 0xff);
            break;
        case MSG_TYPE_PARALLEL_READ:
            fdc_common_post_read_callback((data >> 8) & 0xff, data & 0xff);
            break;
        case MSG_TYPE_PARALLEL_ACK:
            fdc_common_wrote_ack_callback((data >> 8) & 0xff, data & 0xff);
            break;
        default:
            break;
        }
    }

#ifndef USE_CORE1
    while(fifo_is_not_empty(&fifo_c0_to_c1)) {
        data = fifo_pop32(&fifo_c0_to_c1);
        switch (data & MSG_TYPE_MASK) {
        case MSG_TYPE_PARALLEL_WRITE:
            pio_parallel_set_data((data >> 8) & 0xff, data & 0xff);
            break;
        default:
            break;
        }
    }
#endif

#endif
}

#ifdef USE_CORE1
void MY_CORE1_FUNC(core1_msg_task)(void)
{
#if defined(USE_SHARED_MEMORY)
    uint8_t data;
#else
    uint32_t data;
#endif

#if defined(USE_SHARED_MEMORY)

        for(int addr=0; addr<5; addr++) {
            if (shared_c0to1_flags & (1 << addr)) {
                data = shared_c0to1_buf[addr];
                MY_MUTEX_LOCK(lock_c0to1_core1);
                shared_c0to1_flags &= ~(1 << addr);
                MY_MUTEX_UNLOCK(lock_c0to1_core1);
                pio_parallel_set_data(addr, data);
            }
            __nop();
            __nop();
        }

#endif

    if (multicore_fifo_rvalid()) {
        // multicore_fifo_pop_timeout_us(1, &data);
        data = sio_hw->fifo_rd;
        switch (data & MSG_TYPE_MASK) {
#if !defined(USE_SHARED_MEMORY)
        case MSG_TYPE_PARALLEL_WRITE:
            pio_parallel_set_data((data >> 8) & 0xff, data & 0xff);
            break;
        case MSG_TYPE_PARALLEL_WRITE_SYNC:
            pio_parallel_set_data((data >> 8) & 0xff, data & 0xff);
            msg_send_data_to_core0(MSG_TYPE_PARALLEL_ACK, (data >> 8) & 0xff, data & 0xff);
            break;
#endif
        case MSG_TYPE_PARALLEL_NOTICE:
            pio_parallel_notice(data);
            break;
        default:
            break;
        }
    }
}
#endif
