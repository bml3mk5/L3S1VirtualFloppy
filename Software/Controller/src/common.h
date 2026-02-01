/**
 * @file common.h
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-29
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#ifndef COMMON_H
#define COMMON_H

#if defined(CH32V00X)
#include <ch32v00x.h>
#elif defined(CH32V10X)
#include <ch32v10x.h>
#elif defined(CH32V20X)
#include <ch32v20x.h>
#elif defined(CH32V30X) || defined(CH32V31X)
#include <ch32v30x.h>
#elif defined (CH32X035)
#include <ch32x035.h>
#elif defined(CH32L10X)
#include <ch32l103.h>
#endif

#endif /* COMMON_H */
