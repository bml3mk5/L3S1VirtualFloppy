/**
 * @file user.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-29
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "user.h"
#include <stdio.h>

static uint8_t i2c_btn = 0;
static uint8_t i2c_led = 0;

static uint8_t tmp_btn = 0;

/*------------------------------------------------------------------------------*/

void Led_Btn_Init(void)
{
    GPIO_InitTypeDef GPIO_InitSt;

    // for LED output
	GPIO_InitSt.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitSt.GPIO_Mode = GPIO_Mode_Out_PP;   // push-pull
	GPIO_InitSt.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_Init(GPIOC, &GPIO_InitSt);

    // for BUTTON input
	GPIO_InitSt.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_4;
	GPIO_InitSt.GPIO_Mode = GPIO_Mode_IPU;  // pullup
	GPIO_InitSt.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_Init(GPIOC, &GPIO_InitSt);

    PollingTimer_Init();
#ifdef USE_BUZZER_TIMER
    BuzzerTimer_Init();
#endif

    PollingTimer_Start();
}

void Led_Btn_Task(void)
{
    tmp_btn |= (uint8_t)GPIO_ReadInputData(GPIOC);
    // button pins are always set (pull-up)
    GPIO_Write(GPIOC, (~i2c_led) | 0x1f);
#ifdef USE_BUZZER_TIMER
    BuzzerTimer_Start();
#endif
}

/*------------------------------------------------------------------------------*/

void I2C_Slave_Init(void)
{
    GPIO_InitTypeDef GPIO_InitSt;
	GPIO_InitSt.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
	GPIO_InitSt.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_InitSt.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_Init(GPIOC, &GPIO_InitSt);

    I2C_InitTypeDef I2C_InitSt;
    I2C_InitSt.I2C_ClockSpeed = 100000;
    I2C_InitSt.I2C_Mode = I2C_Mode_I2C;
    I2C_InitSt.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitSt.I2C_OwnAddress1 = (0x41 << 1);
    I2C_InitSt.I2C_Ack = I2C_Ack_Enable;
    I2C_InitSt.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    // RCC reset
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);
    // I2C Reset
    I2C_SoftwareResetCmd(I2C1, ENABLE);
    I2C_SoftwareResetCmd(I2C1, DISABLE);
    I2C_Init(I2C1, &I2C_InitSt);
    // interrupt
    I2C_ITConfig(I2C1, I2C_IT_BUF | I2C_IT_EVT | I2C_IT_ERR, ENABLE);
    NVIC_SetPriority(I2C1_EV_IRQn, 0x80);
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_SetPriority(I2C1_ER_IRQn, 0x80);
    NVIC_EnableIRQ(I2C1_ER_IRQn);

    I2C_Cmd(I2C1, ENABLE);
}

/*------------------------------------------------------------------------------*/

void I2C1_EV_IRQHandler(void)
{
    uint16_t STAR1 = I2C1->STAR1;
    uint16_t STAR2 = I2C1->STAR2;
    switch(STAR2 & 0xf) {
    case (I2C_STAR2_BUSY):
        // receiver
        switch(STAR1 & 0xff) {
        case (I2C_STAR1_RXNE):
            // receive data
//          I2C_GenerateSTOP(I2C1, ENABLE);
            i2c_led = (I2C_ReceiveData(I2C1) & 0xe0);
//          printf("R\n");
            break;
        default:
            break;
        }
        break;
    case (I2C_STAR2_BUSY | I2C_STAR2_TRA):
        // transmitter
        switch(STAR1 & 0xff) {
        case (I2C_STAR1_ADDR | I2C_STAR1_TXE):
            // send current status
            i2c_btn |= (i2c_led & 0xe0);
            I2C_SendData(I2C1, i2c_btn);
//          I2C_GenerateSTOP(I2C1, ENABLE);
//          printf("W\n");
            break;
        default:
            break;
        }
        break;
    
    default:
//      printf("?%02x\n", STAR2 & 0xff);
        break;
    }
    if (STAR1 & I2C_STAR1_STOPF) {
        // end of transacion
        I2C_GenerateSTOP(I2C1, DISABLE);
//      printf("S\n");
    }
}

void I2C1_ER_IRQHandler(void)
{
    uint16_t STAR1 = I2C1->STAR1;

    // clear error flags
    I2C1->STAR1 &= ~(STAR1 & (I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_AF));
//  printf("E%04x\n", STAR1);
}

/*------------------------------------------------------------------------------*/

#define BUZZER_Grp GPIOD
#define BUZZER_Pin GPIO_Pin_0

static uint8_t buzzer_repeat;

static void Buzzer_Init(void)
{
    buzzer_repeat = 0;

    GPIO_InitTypeDef GPIO_InitSt;
	GPIO_InitSt.GPIO_Pin = BUZZER_Pin;
	GPIO_InitSt.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitSt.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_Init(BUZZER_Grp, &GPIO_InitSt);
}

/*------------------------------------------------------------------------------*/

#define tim_devide 8

void PollingTimer_Init(void)
{
#ifndef USE_BUZZER_TIMER
    Buzzer_Init();
#endif

    uint32_t count = ((SystemCoreClock / 4 / tim_devide) / 1000) - 1; /* 1KHz */

    TIM_TimeBaseInitTypeDef TIM_BaseInitSt;
    TIM_BaseInitSt.TIM_Period = (uint16_t)count;
    TIM_BaseInitSt.TIM_Prescaler = tim_devide - 1;
    TIM_BaseInitSt.TIM_ClockDivision = TIM_CKD_DIV4;
    TIM_BaseInitSt.TIM_CounterMode = TIM_CounterMode_Down;
    TIM_BaseInitSt.TIM_RepetitionCounter = 0;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    TIM_TimeBaseInit(TIM1, &TIM_BaseInitSt);
    TIM_ARRPreloadConfig(TIM1, ENABLE);

    // interrupt
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);
    NVIC_SetPriority(TIM1_UP_IRQn, 0xc0);
    NVIC_EnableIRQ(TIM1_UP_IRQn);
}

void PollingTimer_Start(void)
{
    TIM_Cmd(TIM1, ENABLE);
}

#if 0
static void PollingTimer_Restart(void)
{
}

static void PollingTimer_Stop(void)
{
    TIM_Cmd(TIM1, DISABLE);
}
#endif

void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) == SET) {
#ifndef USE_BUZZER_TIMER
        if (buzzer_repeat) {
            if (buzzer_repeat == 1) {
                i2c_led &= ~0x20;
            }
            buzzer_repeat--;
        } else {
            if (i2c_led & 0x20) {
                buzzer_repeat = 3;
            }
        }
        if (buzzer_repeat & 1) {
            GPIO_SetBits(BUZZER_Grp, BUZZER_Pin);
        } else {
            GPIO_ResetBits(BUZZER_Grp, BUZZER_Pin);
        }
#endif
        __disable_irq();
        i2c_btn = tmp_btn;
        tmp_btn = 0;
        i2c_btn = (~i2c_btn) & 0x1f;
        __enable_irq();
    }
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
}


/*------------------------------------------------------------------------------*/

#ifdef USE_BUZZER_TIMER
void BuzzerTimer_Init(void)
{
    Buzzer_Init();

    uint32_t count = ((SystemCoreClock / 4 / tim_devide) / 1000) - 1; /* 1KHz */

    TIM_TimeBaseInitTypeDef TIM_BaseInitSt;
    TIM_BaseInitSt.TIM_Period = (uint16_t)count;
    TIM_BaseInitSt.TIM_Prescaler = tim_devide - 1;
    TIM_BaseInitSt.TIM_ClockDivision = TIM_CKD_DIV4;
    TIM_BaseInitSt.TIM_CounterMode = TIM_CounterMode_Down;
    TIM_BaseInitSt.TIM_RepetitionCounter = 3;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInit(TIM2, &TIM_BaseInitSt);
    TIM_ARRPreloadConfig(TIM2, ENABLE);

    // interrupt
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    NVIC_SetPriority(TIM2_IRQn, 0x80);
    NVIC_EnableIRQ(TIM2_IRQn);
}

void BuzzerTimer_Start(void)
{
    if (buzzer_repeat || !(i2c_led & 0x20)) {
        return;
    }

    buzzer_repeat = 3;
    GPIO_SetBits(BUZZER_Grp, BUZZER_Pin);
    TIM_Cmd(TIM2, ENABLE);
}

static void BuzzerTimer_Restart(void)
{
    if (buzzer_repeat & 1) {
        GPIO_SetBits(BUZZER_Grp, BUZZER_Pin);
    } else {
        GPIO_ResetBits(BUZZER_Grp, BUZZER_Pin);
    }
}

static void BuzzerTimer_Stop(void)
{
    TIM_Cmd(TIM2, DISABLE);
    GPIO_ResetBits(BUZZER_Grp, BUZZER_Pin);
    i2c_led &= ~0x20;
}

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET) {
        buzzer_repeat--;
        if (buzzer_repeat) {
            BuzzerTimer_Restart();
        } else {
            BuzzerTimer_Stop();
        }
    }
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}
#endif /* USE_BUZZER_TIMER */

/*------------------------------------------------------------------------------*/

void NMI_Handler(void)
{

}

void HardFault_Handler(void)
{
	while (1)
	{
	}
}
