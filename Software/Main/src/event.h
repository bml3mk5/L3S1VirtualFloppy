/** @file event.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>
#include "pico/time.h"

void event_init();
alarm_id_t event_register_event(uint32_t usec, alarm_callback_t callback, uint32_t user_data);
void event_cancel_event(alarm_id_t *alarm_id);

void event_info();

#endif /* EVENT_H */
