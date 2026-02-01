/** @file event.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "event.h"
#include <stdio.h>
#include <string.h>

#ifndef OUT_DEBUG
#define OUT_DEBUG(...)
// printf(__VA_ARGS__)
#endif

#define EVENT_MAX_TIMERS PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS
static alarm_id_t event_indexes[EVENT_MAX_TIMERS];

void event_init()
{
    alarm_pool_init_default();
    memset(event_indexes, 0, sizeof(event_indexes));
}

alarm_id_t __no_inline_not_in_flash_func(event_register_event)(uint32_t usec, alarm_callback_t callback, uint32_t user_data)
{
    if (usec == 0) usec = 1;
    alarm_id_t alarm_id = add_alarm_in_us(usec, callback, (void *)user_data, true);
    if (alarm_id <= 0) {
        printf("EVENT: regist error: %d\n", alarm_id);
    } else {
        event_indexes[(alarm_id >> 16) & (EVENT_MAX_TIMERS - 1)] = alarm_id;
        OUT_DEBUG("EVENT: regist id:%08x usec:%u\n", alarm_id, usec);
    }
    return alarm_id;
}

void __no_inline_not_in_flash_func(event_cancel_event)(alarm_id_t *alarm_id)
{
    if (*alarm_id >= 0) {
        cancel_alarm(*alarm_id);
        OUT_DEBUG("EVENT: cancel id:%08x\n", *alarm_id);
        event_indexes[((*alarm_id) >> 16) & (EVENT_MAX_TIMERS - 1)] = 0;
        *alarm_id = -1;
    }
}

void event_info()
{
    for(int i=0; i<EVENT_MAX_TIMERS; i++) {
        printf("%02d: ID:%08x\n", i, event_indexes[i]);
    }
}
