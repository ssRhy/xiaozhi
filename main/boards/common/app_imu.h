/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef _APP_IMU_H_
#define _APP_IMU_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float pitch;
    float roll;
    float yaw;
} bmi270_axis_t;

typedef struct
{
    float acc_x;
    float acc_y;
    float acc_z;
    float gyr_x;
    float gyr_y;
    float gyr_z;
} bmi270_value_t;

void app_imu_init(void);
bmi270_axis_t applyDiceInertia(float dice_x_set, float dice_y_set, float dice_z_set);

#ifdef __cplusplus
}
#endif

#endif
