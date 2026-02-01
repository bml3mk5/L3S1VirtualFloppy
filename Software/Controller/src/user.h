/**
 * @file user.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-29
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#ifndef USER_H
#define USER_H

#include "common.h"

void Led_Btn_Init(void);
void Led_Btn_Task(void);

void I2C_Slave_Init(void);
void I2C1_EV_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void I2C1_ER_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void PollingTimer_Init(void);
void PollingTimer_Start(void);
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

#ifdef USE_BUZZER_TIMER
void BuzzerTimer_Init(void);
void BuzzerTimer_Start(void);
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
#endif

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

#endif /* USER_H */
