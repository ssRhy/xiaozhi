#include "app_imu.h"
#include "app_datafusion.h"
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "bmi270.h"
#include <driver/i2c_master.h>

#define GRAVITY_EARTH       (9.80665f)
#define ACCEL               UINT8_C(0x00)
#define GYRO                UINT8_C(0x01)

static bmi270_handle_t bmi_handle = NULL;
static bmi270_axis_t axis_last_val = {0.0f, 0.0f, 0.0f};

// 全局IMU句柄 - 与ESP SparkBot保持一致

// 外部C接口函数来获取I2C总线
extern i2c_master_bus_handle_t get_board_i2c_bus(void);

// BMI270硬件初始化
static void i2c_sensor_bmi270_init(void)
{
    i2c_master_bus_handle_t i2c_bus_handle = get_board_i2c_bus();
    if(!i2c_bus_handle){
        return;
    }

    bmi270_i2c_config_t i2c_bmi270_conf = {
        .i2c_handle = i2c_bus_handle,
        .i2c_addr = BMI270_I2C_ADDRESS,
    };

    bmi270_sensor_create(&i2c_bmi270_conf, &bmi_handle);
}

static float lsb_to_mps2(int16_t val, float g_range, uint8_t bit_width)
{
    double power = 2;
    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));
    return (GRAVITY_EARTH * val * g_range) / half_scale;
}

static float lsb_to_dps(int16_t val, float dps, uint8_t bit_width)
{
    double power = 2;
    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));
    return (dps / (half_scale)) * (val);
}

static int8_t set_accel_gyro_config(struct bmi2_dev *bmi)
{

    int8_t rslt;
    struct bmi2_sens_config config[2];

    config[ACCEL].type = BMI2_ACCEL;
    config[GYRO].type = BMI2_GYRO;

    rslt = bmi2_get_sensor_config(config, 2, bmi);

    rslt = bmi2_map_data_int(BMI2_DRDY_INT, BMI2_INT1, bmi);

    if (rslt == BMI2_OK) {

        config[ACCEL].cfg.acc.odr = BMI2_ACC_ODR_200HZ;
        config[ACCEL].cfg.acc.range = BMI2_ACC_RANGE_2G;
        config[ACCEL].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        config[ACCEL].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
        config[GYRO].cfg.gyr.odr = BMI2_GYR_ODR_200HZ;
        config[GYRO].cfg.gyr.range = BMI2_GYR_RANGE_2000;
        config[GYRO].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        config[GYRO].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
        config[GYRO].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

        rslt = bmi2_set_sensor_config(config, 2, bmi);
    }

    return rslt;
}

static void bmi270_enable_accel_gyro(struct bmi2_dev *bmi2_dev)
{
    int8_t rslt;

    /* Assign accel and gyro sensor to variable. */
    uint8_t sensor_list[2] = { BMI2_ACCEL, BMI2_GYRO };

    struct bmi2_sens_config config;
    /* Accel and gyro configuration settings. */
    rslt = set_accel_gyro_config(bmi2_dev);

    if (rslt == BMI2_OK) {
        rslt = bmi2_sensor_enable(sensor_list, 2, bmi2_dev);

        if (rslt == BMI2_OK) {
            config.type = BMI2_ACCEL;
            rslt = bmi2_get_sensor_config(&config, 1, bmi2_dev);
        }
    }
}

static void bmi270_get_accel_gyro_value(struct bmi2_dev *bmi2_dev, bmi270_value_t *accel_gyro_val)
{
    int8_t rslt;
    struct bmi2_sens_data sensor_data;
    rslt = bmi2_get_sensor_data(&sensor_data, bmi2_dev);

    if ((rslt == BMI2_OK) && (sensor_data.status & BMI2_DRDY_ACC) &&
            (sensor_data.status & BMI2_DRDY_GYR)) {
        /* Converting lsb to meter per second squared for 16 bit accelerometer at 2G range. */
        accel_gyro_val->acc_x = lsb_to_mps2(sensor_data.acc.x, (float)2, bmi2_dev->resolution);
        accel_gyro_val->acc_y = lsb_to_mps2(sensor_data.acc.y, (float)2, bmi2_dev->resolution);
        accel_gyro_val->acc_z = lsb_to_mps2(sensor_data.acc.z, (float)2, bmi2_dev->resolution);

        /* Converting lsb to degree per second for 16 bit gyro at 2000dps range. */
        accel_gyro_val->gyr_x = lsb_to_dps(sensor_data.gyr.x, (float)2000, bmi2_dev->resolution);
        accel_gyro_val->gyr_y = lsb_to_dps(sensor_data.gyr.y, (float)2000, bmi2_dev->resolution);
        accel_gyro_val->gyr_z = lsb_to_dps(sensor_data.gyr.z, (float)2000, bmi2_dev->resolution);
    }
}

static bmi270_axis_t app_imu_read(void)
{
    bmi270_value_t accel_gyro_val = {0};
    bmi270_axis_t axis_val = {0.0f, 0.0f, 0.0f};
    bmi270_axis_t axis_offset = {0.0f, 0.0f, 0.0f};

    if (!bmi_handle) {
        // 如果没有真实硬件，返回零偏移
        return axis_offset;
    }

    bmi270_get_accel_gyro_value(bmi_handle, &accel_gyro_val);
    calculateAttitude(accel_gyro_val.gyr_x, accel_gyro_val.gyr_y, accel_gyro_val.gyr_z, 
                     accel_gyro_val.acc_x, accel_gyro_val.acc_y, accel_gyro_val.acc_z, 0.01f, &axis_val);

    // 计算相对于上次的偏移量（这是摇晃检测的关键）
    axis_offset.pitch = axis_val.pitch - axis_last_val.pitch;
    axis_offset.roll = axis_val.roll - axis_last_val.roll;
    axis_offset.yaw = axis_val.yaw - axis_last_val.yaw;

    axis_last_val = axis_val;
    
    // 移除调试信息输出，保持最简单的IMU骰子功能
    
    return axis_offset;
}

// 移植ESP SparkBot的骰子惯性算法
bmi270_axis_t applyDiceInertia(float dice_x_set, float dice_y_set, float dice_z_set)
{
    float left;

    static float stop_x = 0;
    static float stop_y = 0;
    static float stop_z = 0;

    static float dice_x_rotation = 0.0f;
    static float dice_y_rotation = 0.0f;
    static float dice_z_rotation = 0.0f;

    static float dice_dst_x = 0.0f;
    static float dice_dst_y = 0.0f;
    static float dice_dst_z = 0.0f;

    dice_x_rotation += dice_x_set;
    dice_y_rotation += dice_y_set;
    dice_z_rotation += dice_z_set;

    // Check if the velocities are close to zero, and stop the rotation
    if (fabsf(dice_x_set - stop_x) < 2.5f && fabsf(dice_y_set - stop_y) < 2.5f && fabsf(dice_z_set - stop_z) < 2.5f) {
        if (fabsf(dice_x_set - stop_x) < 2.5f) {
            if (dice_x_set != 0) {
                left = fmodf(dice_x_rotation, 90.0f);
                if (dice_x_rotation > 0) {
                    dice_dst_x = fabsf(left) < 45 ? dice_x_rotation - left : dice_x_rotation + (90 - fabsf(left));
                } else {
                    dice_dst_x = fabsf(left) < 45 ? dice_x_rotation + fabsf(left) : dice_x_rotation - (90 - fabsf(left));
                }
            }
        }

        if (fabsf(dice_y_set - stop_y) < 2.5f) {
            if (dice_y_set != 0) {
                left = fmodf(dice_y_rotation, 90.0f);
                if (dice_y_rotation > 0) {
                    dice_dst_y = fabsf(left) < 45 ? dice_y_rotation - left : dice_y_rotation + (90 - fabsf(left));
                } else {
                    dice_dst_y = fabsf(left) < 45 ? dice_y_rotation + fabsf(left) : dice_y_rotation - (90 - fabsf(left));
                }
            }
        }

        if (fabsf(dice_z_set - stop_z) < 2.5f) {
            if (dice_z_set != 0) {
                left = fmodf(dice_z_rotation, 90.0f);
                if (dice_z_rotation > 0) {
                    dice_dst_z = fabsf(left) < 45 ? dice_z_rotation - left : dice_z_rotation + (90 - fabsf(left));
                } else {
                    dice_dst_z = fabsf(left) < 45 ? dice_z_rotation + fabsf(left) : dice_z_rotation - (90 - fabsf(left));
                }
            }
        }

        dice_x_set = 0.0f;
        dice_y_set = 0.0f;
        dice_z_set = 0.0f;
        stop_x = 0.0f;
        stop_y = 0.0f;
        stop_z = 0.0f;

    } else {
        stop_x = dice_x_set;
        stop_y = dice_y_set;
        stop_z = dice_z_set;
    }

    if (dice_dst_x != 0) {
        if (fabsf(dice_dst_x - dice_x_rotation) < 0.1f) {
            dice_x_rotation = dice_dst_x;
            dice_dst_x = 0;
        } else {
            dice_x_rotation += (dice_dst_x > dice_x_rotation ? 1 : -1) * fminf(8.05f, fabsf(dice_dst_x - dice_x_rotation) * 0.2f);
        }
    }

    if (dice_dst_y != 0) {
        if (fabsf(dice_dst_y - dice_y_rotation) < 0.1f) {
            dice_y_rotation = dice_dst_y;
            dice_dst_y = 0;
        } else {
            dice_y_rotation += (dice_dst_y > dice_y_rotation ? 1 : -1) * fminf(8.05f, fabsf(dice_dst_y - dice_y_rotation) * 0.2f);
        }
    }

    if (dice_dst_z != 0) {
        if (fabsf(dice_dst_z - dice_z_rotation) < 0.1f) {
            dice_z_rotation = dice_dst_z;
            dice_dst_z = 0;
        } else {
            dice_z_rotation += (dice_dst_z > dice_z_rotation ? 1 : -1) * fminf(8.05f, fabsf(dice_dst_z - dice_z_rotation) * 0.2f);
        }
    }

    bmi270_axis_t dice_axis;
    dice_axis.pitch = dice_x_rotation;
    dice_axis.roll = dice_y_rotation;
    dice_axis.yaw = dice_z_rotation;

    return dice_axis;
}

// 检查骰子模式是否激活的外部函数声明
extern bool is_dice_mode_active(void);

// IMU数据读取任务 - 完全按照ESP SparkBot模式实现
static void app_imu_task(void *arg)
{
    while (1) {
        bmi270_axis_t axis_offset = app_imu_read();
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // 检查当前是否在骰子模式（对应ESP SparkBot的ui_dice检查）
        if (is_dice_mode_active()) {
            // 调用骰子事件处理（对应ESP SparkBot的app_dice_event）
            extern void trigger_dice_with_imu_data(float pitch, float roll, float yaw);
            trigger_dice_with_imu_data(axis_offset.pitch, axis_offset.roll, axis_offset.yaw);
        }
    }
    vTaskDelete(NULL);
}

void app_imu_init(void)
{
    // 测试版本：暂时禁用所有硬件初始化
    // i2c_sensor_bmi270_init();
    // bmi270_enable_accel_gyro(bmi_handle);
    
    BaseType_t res;
    res = xTaskCreate(app_imu_task, "imu task", 4 * 1024, NULL, 5, NULL);
    if (res != pdPASS) {
        // 任务创建失败
    }
}

// ESP SparkBot没有deinit函数，IMU任务持续运行
// void app_imu_deinit(void) - 不需要此函数

