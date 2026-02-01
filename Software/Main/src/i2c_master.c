/**
 * @file i2c_master.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-15
 * 
 * @copyright Copyright (c) Sasaji 2024
 * 
 */

#include "i2c_master.h"
#include <hardware/gpio.h>
#include <hardware/dma.h>
#include <hardware/sync.h>
#include <hardware/resets.h>
#include <pico/timeout_helper.h>
#include "i2c_lcd_1602.h"
#include "i2c_led_btn.h"

// ----------------------------------------------------------------------

#define I2C_WAIT_TIME_MS    10

#define I2C_LCD_CTRL_CONTINUE   0x80

//#define I2C_MASTER_TX_ABRT_GPIO 9

// store instance to process the I2C0_IRQ and I2C1_IRQ interrupt 
static i2c_master_t *obj_list[2] = { NULL, NULL };

// ----------------------------------------------------------------------

static void i2c_master_reset(i2c_master_t *obj);

static void i2c_master_irq_callback(void);
static void i2c_master_irq_process(i2c_master_t *obj);
static void i2c_master_data_received_event(i2c_master_t *obj, i2c_slave_t *slave);

#if (I2C_MASTER_TX_METHOD == 2)
#ifdef USE_I2C_MASTER_DMA_IRQ
static void i2c_master_dma_tx_irq_func(void);
#endif
#else
static bool i2c_master_send(i2c_master_t *obj);
#endif
#if (I2C_MASTER_RX_METHOD == 2)
#ifdef USE_I2C_MASTER_DMA_IRQ
static void i2c_master_dma_rx_irq_func(void);
#endif
#else
static bool i2c_master_recv(i2c_master_t *obj);
#endif
static inline int i2c_master_wait_tx_buffer_is_not_full(i2c_master_t *obj, size_t len, uint32_t timeout_ms);

static void i2c_master_request_transfer(i2c_master_t *obj, i2c_slave_t *slave, enum en_transqueue_state state, uint16_t pos, size_t len);
static void i2c_master_start_transfer(i2c_master_t *obj);
static void i2c_master_next_transfer(i2c_master_t *obj);

static void transaction_init(transaction_t *trans);
static void transaction_clear(transaction_t *trans);

static inline void transqueue_set(transqueue_t *queue, enum en_transqueue_state state, uint8_t flags, uint16_t pos, uint16_t len, i2c_slave_t *slave); 
static inline void transqueue_clear(transqueue_t *queue);

// ----------------------------------------------------------------------

/// @brief Initialize I2C device
/// @param[in] obj      : instance 
/// @param[in] scl_pin  : SCL pin number
/// @param[in] sda_pin  : SDA pin number
/// @param[in] i2c      : I2C hardware instance (i2c0 or i2c1)
void i2c_master_init(i2c_master_t *obj, int scl_pin, int sda_pin, i2c_inst_t *i2c)
{
    obj->i2c = i2c; // instance
    obj->irq_num = i2c == i2c0 ? I2C0_IRQ : I2C1_IRQ;
    obj_list[obj->irq_num == I2C0_IRQ ? 0 : 1] = obj;

    // This example will use I2C0 on the default SDA and SCL pins (4, 5 on a Pico)
    obj->baudrate = 400000;
    i2c_init(obj->i2c, obj->baudrate);

    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    // disable pull up/down, so set pull up on one of i2c devices at least
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
//    gpio_disable_pulls(sda_pin);
//    gpio_disable_pulls(scl_pin);

    i2c_hw_t *hw = i2c_get_hw(obj->i2c);

    transaction_init(&obj->trans);

#if (I2C_MASTER_TX_METHOD == 2)
    // Grab some unused dma channels
    obj->dma_tx = dma_claim_unused_channel(true);
    dma_channel_config c_tx = dma_channel_get_default_config(obj->dma_tx);
    channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_16); // 2bytes per one transfer 
    channel_config_set_dreq(&c_tx, i2c_get_dreq(obj->i2c, true));
    channel_config_set_ring(&c_tx, false, I2C_MASTER_TX_BUFFER_SFT);
    channel_config_set_read_increment(&c_tx, true);
    channel_config_set_write_increment(&c_tx, false);
    channel_config_set_irq_quiet(&c_tx, false);
    dma_channel_configure(obj->dma_tx, &c_tx,
        &hw->data_cmd,
        obj->tx_buf,
        0,
        false);
#ifdef USE_I2C_MASTER_DMA_IRQ
    irq_add_shared_handler(DMA_IRQ_1, i2c_master_dma_tx_irq_func, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
    dma_channel_set_irq1_enabled(obj->dma_tx, true);
#endif
#endif
#if (I2C_MASTER_RX_METHOD == 2)
    // Grab some unused dma channels
    obj->dma_rx = dma_claim_unused_channel(true);
    dma_channel_config c_rx = dma_channel_get_default_config(obj->dma_rx);
    channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_16); // 2bytes per one transfer 
    channel_config_set_dreq(&c_rx, i2c_get_dreq(obj->i2c, false));
    channel_config_set_ring(&c_rx, true, I2C_MASTER_RX_BUFFER_SFT);
    channel_config_set_read_increment(&c_rx, false);
    channel_config_set_write_increment(&c_rx, true);
    channel_config_set_irq_quiet(&c_rx, false);
    dma_channel_configure(obj->dma_rx, &c_rx,
        obj->rx_buf,
        &hw->data_cmd,
        0,
        false);
#ifdef USE_I2C_MASTER_DMA_IRQ
    irq_add_shared_handler(DMA_IRQ_0, i2c_master_dma_rx_irq_func, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_set_irq0_enabled(obj->dma_rx, true);
#endif
#endif

    hw->rx_tl = 0;
    hw->tx_tl = 0xff;

    // interrupt
    irq_set_exclusive_handler(obj->irq_num, i2c_master_irq_callback);

    obj->irq_mask = I2C_IC_INTR_MASK_M_STOP_DET_BITS | I2C_IC_INTR_MASK_M_TX_ABRT_BITS;
#if (I2C_MASTER_RX_METHOD != 2)
    obj->irq_mask |= I2C_IC_INTR_MASK_M_RX_FULL_BITS;
#endif
    hw->intr_mask = obj->irq_mask;

    fifo_init(&obj->txf, obj->tx_buf, I2C_MASTER_TX_BUFFER_USIZE, (uint16_t)sizeof(uint16_t));
    fifo_init(&obj->rxf, obj->rx_buf, I2C_MASTER_RX_BUFFER_USIZE, (uint16_t)sizeof(uint16_t));

    irq_set_enabled(obj->irq_num, true);

#if defined(I2C_MASTER_TX_ABRT_GPIO) && (I2C_MASTER_TX_ABRT_GPIO >= 0)
    gpio_init(I2C_MASTER_TX_ABRT_GPIO);
    gpio_set_dir(I2C_MASTER_TX_ABRT_GPIO, true);
    gpio_put(I2C_MASTER_TX_ABRT_GPIO, false);
#endif
}

/// @brief Reset I2C device and clear buffer
/// @param obj : instance
void i2c_master_reset(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    hw->enable = 0;
    irq_set_enabled(obj->irq_num, false);
    irq_clear(obj->irq_num);
    hw->clr_intr;
#if (I2C_MASTER_TX_METHOD == 2)
    dma_channel_abort(obj->dma_tx);
    dma_channel_set_read_addr(obj->dma_tx, obj->tx_buf, false);
#endif
#if (I2C_MASTER_RX_METHOD == 2)
    dma_channel_abort(obj->dma_rx);
    dma_channel_set_write_addr(obj->dma_rx, obj->rx_buf, false);
#endif
    fifo_clear(&obj->txf);
    fifo_clear(&obj->rxf);
    transaction_clear(&obj->trans);
    sleep_us(100);
    hw->intr_mask = obj->irq_mask;
    irq_set_enabled(obj->irq_num, true);
}

// ----------------------------------------------------------------------

/// @brief Nothing to do
/// @param obj 
void i2c_master_task(i2c_master_t *obj)
{
    (void)obj;
}

/// @brief Wait for idle on I2C bus
/// @param obj : instance
void i2c_master_wait_idle(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    if (hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
//        printf("I2C Wait IDLE ...\n");
        while(hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
            tight_loop_contents();
        }
//        printf("Done.\n");
    }
}

// ----------------------------------------------------------------------

/// @brief Callback interrupt for I2C device
void i2c_master_irq_callback(void)
{
    for(int i=0; i<2; i++) {
        if (obj_list[i]) {
            i2c_master_irq_process(obj_list[i]); 
        }
    }
}

/// @brief Process interrupt
/// @param obj : instance
void i2c_master_irq_process(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    transqueue_t *curr = &obj->trans.current;

//  printf("irq_func1: %08x\n", hw->intr_stat);
#if (I2C_MASTER_TX_METHOD != 2)
    if (hw->intr_stat & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) {
        // TX Request
        if (curr->state != TRANS_STATE_IDLE) {
            i2c_master_send(obj);
        } else {
            hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
            curr->flags &= ~TRANS_FLAG_TX_STOP;
        }
    }
//  printf("irq_func2: %08x\n", hw->intr_stat);
#endif
#if (I2C_MASTER_RX_METHOD != 2)
    if (hw->intr_stat & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        // RX Request
        if (curr->state != TRANS_STATE_IDLE) {
            i2c_master_recv(obj);
        } else {
            hw->intr_mask |= I2C_IC_INTR_MASK_M_RX_FULL_BITS;
        }
    }
//  printf("irq_func3: %08x\n", hw->intr_stat);
#endif
    if (hw->intr_stat & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        // aborted
        uint32_t reason = hw->tx_abrt_source;
#if defined(I2C_MASTER_TX_ABRT_GPIO) && (I2C_MASTER_TX_ABRT_GPIO >= 0)
        gpio_put(I2C_MASTER_TX_ABRT_GPIO, true);
#endif
        printf(" TX_ABRT: ADDR:%02x c:%3d %05x\n", curr->slave->addr, reason >> 23, reason & 0x1ffff);
        uint32_t tx_rpos = curr->pos + curr->len;
        obj->txf.rpos = tx_rpos;
        obj->txf.rpos &= (obj->txf.size - 1);
#if (I2C_MASTER_TX_METHOD == 2)
        if (dma_channel_is_busy(obj->dma_tx)) {
#ifdef USE_I2C_MASTER_DMA_IRQ
            dma_channel_set_irq1_enabled(obj->dma_tx, false);
#endif
            dma_channel_abort(obj->dma_tx);
#ifdef USE_I2C_MASTER_DMA_IRQ
            ma_channel_acknowledge_irq1(obj->dma_tx);
            dma_channel_set_irq1_enabled(obj->dma_tx, true);
#endif
//            printf(" DMA TX: RADDR:%08x CTRL:%08x\n", dma_hw->ch[obj->dma_tx].read_addr, dma_hw->ch[obj->dma_tx].ctrl_trig);
            dma_hw->ch[obj->dma_tx].read_addr = (io_rw_32)&obj->tx_buf[tx_rpos];
        }
#endif
#if (I2C_MASTER_RX_METHOD == 2)
        if (dma_channel_is_busy(obj->dma_rx)) {
#ifdef USE_I2C_MASTER_DMA_IRQ
            dma_channel_set_irq0_enabled(obj->dma_rx, false);
#endif
            dma_channel_abort(obj->dma_rx);
#ifdef USE_I2C_MASTER_DMA_IRQ
            ma_channel_acknowledge_irq0(obj->dma_rx);
            dma_channel_set_irq0_enabled(obj->dma_rx, true);
#endif
            uint32_t rx_wpos = (uint32_t)dma_hw->ch[obj->dma_rx].write_addr - (uint32_t)&obj->rx_buf;
            rx_wpos >>= 1;
            obj->rxf.wpos = (uint16_t)rx_wpos;
            obj->rxf.rpos = (uint16_t)rx_wpos;
//            printf(" DMA RX: WADDR:%08x CTRL:%08x\n", dma_hw->ch[obj->dma_rx].write_addr, dma_hw->ch[obj->dma_rx].ctrl_trig);
        }
#endif
        hw->clr_tx_abrt;
        i2c_master_wait_idle(obj);
//#if (I2C_MASTER_TX_METHOD == 2)
//        printf(" DMA TX: RADDR:%08x CTRL:%08x\n", dma_hw->ch[obj->dma_tx].read_addr, dma_hw->ch[obj->dma_tx].ctrl_trig);
//        dma_hw->ch[obj->dma_tx].ctrl_trig |= 1;
//#endif
//#if (I2C_MASTER_RX_METHOD == 2)
//        printf(" DMA RX: WADDR:%08x CTRL:%08x\n", dma_hw->ch[obj->dma_rx].write_addr, dma_hw->ch[obj->dma_rx].ctrl_trig);
//        dma_hw->ch[obj->dma_rx].ctrl_trig |= 1;
//#endif
#if defined(I2C_MASTER_TX_ABRT_GPIO) && (I2C_MASTER_TX_ABRT_GPIO >= 0)
        gpio_put(I2C_MASTER_TX_ABRT_GPIO, false);
#endif
        // gpio
        if (curr->slave->gpio_num >= 0) {
            gpio_put(curr->slave->gpio_num, false);
        }
        // need next transfer?
        i2c_master_next_transfer(obj);

    } else if (hw->intr_stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        // done
//      printf(" STOP_DEC: STATE:%d ADDR:%08x\n", curr->state, curr->slave);
        // post process
        switch(curr->state) {
        case TRANS_STATE_SEND:
            // sent data
#if (I2C_MASTER_TX_METHOD == 2)
            uint32_t tx_rpos = (uint32_t)dma_hw->ch[obj->dma_tx].read_addr - (uint32_t)&obj->tx_buf;
            tx_rpos >>= 1;
            obj->txf.rpos = (uint16_t)tx_rpos;
#endif
            break;
        case TRANS_STATE_RECV:
            // receive data
#if (I2C_MASTER_RX_METHOD == 2)
            uint32_t rx_wpos = (uint32_t)dma_hw->ch[obj->dma_rx].write_addr - (uint32_t)&obj->rx_buf;
            rx_wpos >>= 1;
            obj->rxf.wpos = (uint16_t)rx_wpos;
#endif
            i2c_master_data_received_event(obj, curr->slave);
            break;
        default:
            break;
        }
        // gpio
        if (curr->slave->gpio_num >= 0) {
            gpio_put(curr->slave->gpio_num, false);
        }
        // need next transfer?
        i2c_master_next_transfer(obj);
        hw->clr_stop_det;
    }
}

/// @brief Callback slave function after receiving data if need
/// @param obj : instance
/// @param slave : slave instance
void i2c_master_data_received_event(i2c_master_t *obj, i2c_slave_t *slave)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    if (slave && slave->recvd) {
        if (slave->recvd(&obj->rxf)) {
#if (I2C_MASTER_RX_METHOD != 2)
            hw->intr_mask |= I2C_IC_INTR_MASK_M_RX_FULL_BITS;
#endif
        }
    }
}

#if (I2C_MASTER_TX_METHOD == 2)
#ifdef USE_I2C_MASTER_DMA_IRQ
/// @brief Interrupt on TX DMA
void i2c_master_dma_tx_irq_func(void)
{
    for(int i=0; i<2; i++) {
        i2c_master_t *obj = obj_list[i];
        if (obj && dma_channel_get_irq1_status(obj->dma_tx)) {
            // update position on txfifo
            uint32_t tx_rpos = (uint32_t)dma_hw->ch[obj->dma_tx].read_addr - (uint32_t)&obj->tx_buf;
            tx_rpos >>= 1;
            obj->txf.rpos = (uint16_t)tx_rpos;
//            printf("dma tx done: %d\n",tx_rpos);
            dma_channel_acknowledge_irq1(obj->dma_tx);
        }
    }
}
#endif
#endif

#if (I2C_MASTER_RX_METHOD == 2)
#ifdef USE_I2C_MASTER_DMA_IRQ
/// @brief Interrupt on RX DMA
void i2c_master_dma_rx_irq_func(void)
{
    for(int i=0; i<2; i++) {
        i2c_master_t *obj = obj_list[i];
        if (obj && dma_channel_get_irq0_status(obj->dma_rx)) {
            // update position on rxfifo
            uint32_t rx_wpos = (uint32_t)dma_hw->ch[obj->dma_rx].write_addr - (uint32_t)&obj->rx_buf;
            rx_wpos >>= 1;
            obj->rxf.wpos = (uint16_t)rx_wpos;
//            printf("dma rx done: %d\n",rx_wpos);
            dma_channel_acknowledge_irq0(obj->dma_rx);
        }
    }
}
#endif
#endif

// ----------------------------------------------------------------------

#if (I2C_MASTER_TX_METHOD != 2)
/// @brief Send data to I2C device (call on TX_EMPTY interrupt)
/// @param obj : instance
/// @return true if finish transferring or txfifo in I2C device is full
/// @note The I2C device keeps asserting TX_EMPTY flag if the tx_fifo is not full.
bool i2c_master_send(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);

    uint16_t data = 0;
    bool stop = false;
//  printf("Tx:W:%02d R:%02d", txfifo->wpos, txfifo->rpos);
    while (fifo_is_not_empty(&obj->txf) && (hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS)) {
        // more stored data exists.
        data = fifo_pop16(&obj->txf);
//      printf(" %03X", data);
        hw->data_cmd = data;
        if (fifo_is_empty(&obj->txf) || (data & I2C_IC_DATA_CMD_STOP_BITS)) {
            // stop bit
            stop = true;
            break;            
        }
    }
    if (stop) {
        // disable interrupt
        hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
        obj->trans.current.flags |= TRANS_FLAG_TX_STOP;
    }
//  printf("\n");
    return stop;
}
#endif

#if (I2C_MASTER_RX_METHOD != 2)
/// @brief Receive data from I2C device (call on RX_FULL interrupt)
/// @param obj : instance 
/// @return true if receive buffer is full
/// @note The I2C device keeps asserting the RX_FULL flag if the rx_fifo is not empty.
bool i2c_master_recv(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);

    uint16_t data = 0;
    bool stop = false;
//  printf("Rx:W:%02d R:%02d", rxfifo->wpos, rxfifo->rpos);
    while (fifo_is_not_full(&obj->rxf) && (hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_RX_FULL_BITS)) {
        // more stored data exists.
        data = hw->data_cmd;
//      printf(" %03X", fifo_peek16(txfifo));
        fifo_push16(&obj->rxf, data);
        if (fifo_is_full(&obj->rxf)) {
            // stop bit
            stop = true;
            break;            
        }

    }
    if (stop) {
        // disable interrupt
        hw->intr_mask &= ~I2C_IC_INTR_MASK_M_RX_FULL_BITS;
    }
//  printf("\n");
    return stop;
}
#endif

// ----------------------------------------------------------------------

/// @brief Start transfer when the I2C bus is idle. Or store in queue when now transferring.
/// @param obj : instance
/// @param slave : slave instance
/// @param state : send or receive 
/// @param pos : data start position
/// @param len : data length
void __not_in_flash_func(i2c_master_request_transfer)(i2c_master_t *obj, i2c_slave_t *slave, enum en_transqueue_state state, uint16_t pos, size_t len)
{
    transqueue_t *curr = &obj->trans.current;

    if (curr->state == TRANS_STATE_IDLE) {
        // start transfer
        transqueue_set(curr, state, 0, pos, len, slave);
        i2c_master_start_transfer(obj);
//      printf("StartC: %d %08x\n", curr->state, curr->slave);
        return;
    }

    // now transferring

    // store in queue
    if (fifo_is_full(&obj->trans.queue)) {
        // drop out stored data
        printf("Transqueue is full! P:%u W:%u L:%u\n", pos, obj->txf.wpos, len);
        obj->txf.wpos += (obj->txf.size - len);
        obj->txf.wpos &= (obj->txf.size - 1);
        return;
    }
    transqueue_t q;
    transqueue_set(&q, state, 0, pos, len, slave);
    fifo_push_data(&obj->trans.queue, &q);
//  transqueue_t *qq = (transqueue_t *)fifo_latest_data(&obj->trans.queue);
//  printf("AddQ: %d %d %08x\n", fifo_count(&obj->trans.queue), qq->state, qq->slave);
}

/// @brief Set the slave address
/// @param addr : slave address (7bits)
/// @attention The I2C bus should be idle.
static void i2c_master_set_address(i2c_master_t *obj, int addr)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);

    i2c_master_wait_idle(obj);
    hw->enable = 0;
    hw->tar = addr;
    hw->enable = 1;
    i2c_master_wait_idle(obj);
}

/// @brief Start transfer
/// @param obj : instance
void __not_in_flash_func(i2c_master_start_transfer)(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    transqueue_t *curr = &obj->trans.current;
//  printf("start_transfer: %02x\n", cond);
    switch(curr->state) {
    case TRANS_STATE_SEND:
    case TRANS_STATE_RECV:
        // gpio
        if (curr->slave->gpio_num >= 0) {
            gpio_put(curr->slave->gpio_num, true);
        }
        // send and receive
        i2c_master_set_address(obj, curr->slave->addr);
#if (I2C_MASTER_TX_METHOD == 2)
        dma_channel_set_trans_count(obj->dma_tx, curr->len, true);
#else
        if (!(curr->flags & TRANS_FLAG_TX_STOP)) {
            hw->intr_mask |= I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
        }
#endif
        break;
    default:
        break;
    }
}

/// @brief Start next transfer if data is storeing in queue.
/// @param obj : instance 
void i2c_master_next_transfer(i2c_master_t *obj)
{
    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    transqueue_t *curr = &obj->trans.current;

    transqueue_clear(curr);

    if (fifo_is_not_empty(&obj->trans.queue)) {
        transqueue_t *q = fifo_peek_data(&obj->trans.queue);
//      printf("next_transfer: %04x\n", q);
        i2c_master_request_transfer(obj, q->slave, q->state, q->pos, q->len);
        fifo_inc_rpos(&obj->trans.queue);
    } else {
        // no more queue, so go idle
#if (I2C_MASTER_TX_METHOD != 2)
//      printf("next_transfer: none\n");
        hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
#endif
    }
}

// ----------------------------------------------------------------------

/// @brief Check a slave device is active on the bus 
/// @param slave 
/// @return device is active if true 
bool i2c_master_check_device(i2c_master_t *obj, i2c_slave_t *slave)
{
    if (!obj) return false;

    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    while(hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
        tight_loop_contents();
    }

    irq_set_enabled(obj->irq_num, false);
    hw->intr_mask = I2C_IC_INTR_MASK_M_STOP_DET_BITS;
#if (I2C_MASTER_RX_METHOD != 2)
    hw->intr_mask |= I2C_IC_INTR_MASK_M_RX_FULL_BITS;
#endif
    irq_set_enabled(obj->irq_num, true);

    uint8_t readable = slave->readable;
    uint8_t writable = slave->writeable;
    uint16_t wpos = obj->txf.wpos;
    if (readable > 0) {
        i2c_master_wait_tx_buffer_is_not_full(obj, 1, 10000);
        fifo_push16(&obj->txf, I2C_IC_DATA_CMD_STOP_BITS);
        i2c_master_request_transfer(obj, slave, TRANS_STATE_RECV, wpos, 1);
    } else {
        i2c_master_wait_tx_buffer_is_not_full(obj, writable, 10000);
        for(uint8_t i=1; i<=writable; i++) {
            uint16_t data = 0;
            if (i == writable) {
                // last data
                data |= I2C_IC_DATA_CMD_STOP_BITS;
            }
            fifo_push16(&obj->txf, data);
        }
        i2c_master_request_transfer(obj, slave, TRANS_STATE_SEND, wpos, writable);
    }
    uint32_t abort_reason = 0;
    bool abort = false;
    bool timeout = false;
    timeout_state_t ts;
    check_timeout_fn timeout_check = init_per_iteration_timeout_us(&ts, 1000000);
    do {
        abort_reason = hw->tx_abrt_source;
        abort = (bool)hw->clr_tx_abrt;
        if (timeout_check) {
            timeout = timeout_check(&ts, false);
            abort |= timeout;
        }
    } while (!abort && (hw->status & I2C_IC_STATUS_ACTIVITY_BITS));
    while(hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
        tight_loop_contents();
    }
    int rval = 0;
    if (abort) {
        if (timeout) {
            rval = PICO_ERROR_TIMEOUT;
        } else {
            rval = PICO_ERROR_GENERIC;
        }
    }
    i2c_master_reset(obj);
    while(hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
        tight_loop_contents();
    }
    if (rval < 0) {
        printf("I2C slave failed: addr:0x%02x AR:%08x RI:%08x RV:%d\n", slave->addr, abort_reason, hw->raw_intr_stat, rval);
    }
    return (rval >= 0);
}

/// @brief Check a slave device is active on the bus
/// @param obj
/// @param slave_list
/// @return device is active if true 
bool i2c_master_check_device_flexible(i2c_master_t *obj, i2c_slave_t **slave_list)
{
    const uint32_t baud[1] = { 400000 };
//    const uint32_t baud[5] = { 400000, 100000, 50000, 33000, 25000 };
//    const uint32_t baud[5] = { 100000, 50000, 33000, 25000, 10000 };

    if (!obj) return false;
    if (!slave_list) return false;

    for(int d=0; slave_list[d]; d++) {
        slave_list[d]->baud = 0;
    }

    i2c_hw_t *hw = i2c_get_hw(obj->i2c);
    for(int i=0; i<(int)(sizeof(baud)/sizeof(baud[0])); i++) {
        while(hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
            tight_loop_contents();
        }
        i2c_set_baudrate(obj->i2c, baud[i]);
        sleep_us(100);
        printf("Check on baud:%d\n", baud[i]);

        for(int d=0; slave_list[d]; d++) {
            if (slave_list[d]->baud) {
                continue;
            }
            printf(" D%d...", d);
            bool rc = i2c_master_check_device(obj, slave_list[d]);
            printf("%s\n", rc ? "OK" : "NG");
            if (rc) {
                if (baud[i] > slave_list[d]->baud) {
                    slave_list[d]->baud = baud[i];
                }
            }
        }
    }

    // decide speed
    obj->baudrate = baud[0];
    for(int d=0; slave_list[d]; d++) {
        if (slave_list[d]->baud) {
            if (slave_list[d]->baud < obj->baudrate) {
                obj->baudrate = slave_list[d]->baud;
            }
        }
    }

    while(hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {
        tight_loop_contents();
    }
    i2c_set_baudrate(obj->i2c, obj->baudrate);
    sleep_us(100);
    printf("I2C baud:%u\n", obj->baudrate);
    return true;
}

/// @brief Is there free space in the tx buffer?
/// @param obj : instance
/// @param len : length
bool i2c_master_tx_buffer_is_not_full(i2c_master_t *obj, size_t len)
{
    return (fifo_remain(&obj->txf) >= (int)len);
}

/// @brief Wait until becomes space in the tx buffer.
/// @param obj : instance
/// @param len : length
/// @param timeout_ms : timeout (ms)
/// @return : 0:normal -2:timeout
int i2c_master_wait_tx_buffer_is_not_full(i2c_master_t *obj, size_t len, uint32_t timeout_ms)
{
    if(fifo_remain(&obj->txf) >= (int)len) {
        return PICO_ERROR_NONE;
    }
    if (timeout_ms == 0) timeout_ms = 10000;
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);
    do {
//    printf("I2C Wait Txf State:%d W:%d R:%d L:%d...\n", obj->trans.current.state, obj->txf.wpos, obj->txf.rpos, len);
#if (I2C_MASTER_TX_METHOD == 2)
        uint32_t tx_rpos = (uint32_t)dma_hw->ch[obj->dma_tx].read_addr - (uint32_t)&obj->tx_buf;
        tx_rpos >>= 1;
        obj->txf.rpos = tx_rpos;
#endif
        tight_loop_contents();
        if(fifo_remain(&obj->txf) >= (int)len) {
            return PICO_ERROR_NONE;
        }
    } while(timeout < get_absolute_time());
//    printf("Done. W:%d R:%d\n", obj->txf.wpos, obj->txf.rpos);
    return PICO_ERROR_TIMEOUT;
}

/// @brief Send one data to the I2C device.
/// @param slave : slave instance
/// @param state : send 
/// @param ctrl  : control code
/// @param data  : any data
void __not_in_flash_func(i2c_master_send_byte)(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    uint16_t wpos = obj->txf.wpos;
    if (i2c_master_wait_tx_buffer_is_not_full(obj, 2, I2C_WAIT_TIME_MS) < 0) {
        return;
    }
    fifo_push16(&obj->txf, ctrl);
    fifo_push16(&obj->txf, (uint16_t)data | I2C_IC_DATA_CMD_STOP_BITS);
    i2c_master_request_transfer(obj, slave, state, wpos, 2);
}

/// @brief Send two datas to the I2C device.
/// @param slave : slave instance
/// @param state : send 
/// @param ctrl  : control code
/// @param data  : any data
/// @param arg   : any arg
void i2c_master_send_2bytes(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data, uint8_t arg)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    uint16_t wpos = obj->txf.wpos;
    if (i2c_master_wait_tx_buffer_is_not_full(obj, 2, I2C_WAIT_TIME_MS) < 0) {
        return;
    }
    fifo_push16(&obj->txf, ctrl | I2C_LCD_CTRL_CONTINUE);
    fifo_push16(&obj->txf, (uint16_t)data);
    if (i2c_master_wait_tx_buffer_is_not_full(obj, 2, I2C_WAIT_TIME_MS) < 0) {
        obj->txf.wpos = wpos;
        return;
    }
    fifo_push16(&obj->txf, ctrl);
    fifo_push16(&obj->txf, (uint16_t)arg | I2C_IC_DATA_CMD_STOP_BITS);
    i2c_master_request_transfer(obj, slave, state, wpos, 4);
}

/// @brief Send one data to the I2C device and wait until sent all.
/// @param slave : slave instance
/// @param state : send 
/// @param ctrl  : control code
/// @param data  : any data
void i2c_master_send_byte_blocking(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    i2c_master_send_byte(slave, state, ctrl, data);
    i2c_master_wait_idle(obj);
}

/// @brief Send two datas to the I2C device and wait until sent all.
/// @param slave : slave instance
/// @param state : send 
/// @param ctrl  : control code
/// @param data  : any data
/// @param arg   : any arg
void i2c_master_send_2bytes_blocking(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, uint8_t data, uint8_t arg)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    i2c_master_send_2bytes(slave, state, ctrl, data, arg);
    i2c_master_wait_idle(obj);
}

/// @brief Send a string to the I2C device (for LCD device)
/// @param slave : slave instance
/// @param state : send 
/// @param ctrl  : control code
/// @param str   : string
/// @param len   : length of str
void i2c_master_send_string(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl, const char *str, size_t len)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    uint16_t wpos = obj->txf.wpos;
    for(size_t i=0; i<len; i++) {
        if (i2c_master_wait_tx_buffer_is_not_full(obj, 2, I2C_WAIT_TIME_MS) < 0) {
            obj->txf.wpos = wpos;
            return;
        }
        bool last = (i == (len - 1));
        if (last)
            ctrl &= ~I2C_LCD_CTRL_CONTINUE;
        else
            ctrl |= I2C_LCD_CTRL_CONTINUE;
        fifo_push16(&obj->txf, ctrl);
        uint16_t data = (uint16_t)*str;
        if (last)
            data |= I2C_IC_DATA_CMD_STOP_BITS;
        fifo_push16(&obj->txf, data);
        str++;
    }
    i2c_master_request_transfer(obj, slave, state, wpos, len << 1);
}

/// @brief Send array of control and data pair to the I2C device
/// @param slave : slave instance
/// @param state : send 
/// @param ctrl  : control code
/// @param arr   : array
/// @param len   : length of arr
void i2c_master_send_array(i2c_slave_t *slave, enum en_transqueue_state state, const uint8_t *arr, size_t len)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    uint16_t wpos = obj->txf.wpos;
    for(size_t i=0; i<len; i++) {
        if (i2c_master_wait_tx_buffer_is_not_full(obj, 2, I2C_WAIT_TIME_MS) < 0) {
            obj->txf.wpos = wpos;
            return;
        }
        bool last = (i == (len - 1));
        uint16_t ctrl = (uint16_t)*arr;
        if (last)
            ctrl &= ~I2C_LCD_CTRL_CONTINUE;
        else
            ctrl |= I2C_LCD_CTRL_CONTINUE;
        fifo_push16(&obj->txf, ctrl);
        arr++;
        uint16_t data = (uint16_t)*arr;
        if (last)
            data |= I2C_IC_DATA_CMD_STOP_BITS;
        fifo_push16(&obj->txf, data);
        arr++;
    }
    i2c_master_request_transfer(obj, slave, state, wpos, len);
}

/// @brief Receive one data from I2C device
/// @param slave : slave instance
/// @param state : receive
/// @param ctrl  : control code
void __not_in_flash_func(i2c_master_recv_request_byte)(i2c_slave_t *slave, enum en_transqueue_state state, uint8_t ctrl)
{
    if (!slave->baud) return;

    i2c_master_t *obj = slave->master;
    uint16_t wpos = obj->txf.wpos;
    if (i2c_master_wait_tx_buffer_is_not_full(obj, 1, I2C_WAIT_TIME_MS) < 0) {
        return;
    }
    fifo_push16(&obj->txf, (uint16_t)ctrl | I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_STOP_BITS);
#if (I2C_MASTER_RX_METHOD == 2)
    dma_channel_set_trans_count(obj->dma_rx, 1, true);
#endif
    i2c_master_request_transfer(obj, slave, state, wpos, 1);
}

/// @brief data is whether arrived or not
/// @param slave : slave instance
/// @return true if data arrived and stored in rx buffer
bool __not_in_flash_func(i2c_master_recv_data_arrived)(i2c_slave_t *slave)
{
    i2c_master_t *obj = slave->master;
//#if (I2C_MASTER_RX_METHOD == 2)
//    uint32_t rx_wpos = (uint32_t)dma_hw->ch[obj->dma_rx].write_addr - (uint32_t)&obj->rx_buf;
//    rx_wpos >>= 1;
//    obj->rxf.wpos = rx_wpos;
//    return (obj->rxf.rpos != obj->rxf.wpos);
//#else
    return fifo_is_not_empty(&obj->rxf);
//#endif
}

/// @brief Get a data from rx buffer
/// @param slave : slave instance
/// @return a data if rx buffer is not empty
uint16_t __not_in_flash_func(i2c_master_recv_byte)(i2c_slave_t *slave)
{
    if (!slave->baud) return 0;

    i2c_master_t *obj = slave->master;
    uint8_t data = 0;
    irq_set_enabled(obj->irq_num, false);
    if (fifo_is_not_empty(&obj->rxf)) {
        data = fifo_pop16(&obj->rxf);
#if (I2C_MASTER_RX_METHOD != 2)
        // accept to receive data from rxfifo
        i2c_hw_t *hw = i2c_get_hw(obj->i2c);
        hw->intr_mask |= I2C_IC_INTR_MASK_M_RX_FULL_BITS;
#endif
//      printf("Recv: %02x\n", data);
    }
    irq_set_enabled(obj->irq_num, true);
    return data;
}

// ----------------------------------------------------------------------

void i2c_slave_init(i2c_master_t *master, i2c_slave_t *slave, int address, uint8_t writeable, uint8_t readable)
{
    slave->master = master;
    slave->addr = address;
    slave->baud = 0;
	slave->writeable = writeable;
	slave->readable = readable;
    slave->dev_type = 0;
    slave->reserved1 = 0;
    slave->gpio_num = -1;
    slave->recvd = NULL;
}

void i2c_slave_set_device_type(i2c_slave_t *slave, uint8_t type)
{
    slave->dev_type = type;
}

uint8_t i2c_slave_get_device_type(i2c_slave_t *slave)
{
    return slave->dev_type;
}

// ----------------------------------------------------------------------

/// @brief Initialize transaction data
/// @param trans 
void transaction_init(transaction_t *trans)
{
    transqueue_clear(&trans->current);
    fifo_init(&trans->queue, trans->queue_buf, TRANSQUEUE_SIZE, (uint16_t)sizeof(transqueue_t));
}

/// @brief Clear transaction data
/// @param trans 
void transaction_clear(transaction_t *trans)
{
    transqueue_clear(&trans->current);
    fifo_clear(&trans->queue);
}

/// @brief Set a queue item
/// @param queue : queue item 
/// @param state : send or receive
/// @param flags : normally set 0
/// @param pos   : data start position
/// @param len   : data length
/// @param slave : slave instance
void transqueue_set(transqueue_t *queue, enum en_transqueue_state state, uint8_t flags, uint16_t pos, uint16_t len, i2c_slave_t *slave)
{
    queue->state = state;
    queue->flags = flags;
    queue->len   = pos;
    queue->len   = len;
    queue->slave = slave;
}

/// @brief Clear a queue item
/// @param queue : queue item
void transqueue_clear(transqueue_t *queue)
{
    queue->state = TRANS_STATE_IDLE;
    queue->flags = 0;
    queue->pos   = 0;
    queue->len   = 0;
    queue->slave = NULL;
}
