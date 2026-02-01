/**
 * @file main.c
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-29
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "common.h"
#include "user.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
#ifdef NVIC_PriorityGroup_2
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
#else
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
#endif
	SystemCoreClockUpdate();
	Delay_Init();
    USART_Printf_Init(19200);

    // use GPIO Port C
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    // use GPIO Port D
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    // for LED output and BUTTON input
    Led_Btn_Init();

    // i2c slave
    I2C_Slave_Init();

    __enable_irq();

//  printf("Start\n");

	while (1)
	{
        Led_Btn_Task();
        Delay_Us(1);
	}

    return 0;
}
