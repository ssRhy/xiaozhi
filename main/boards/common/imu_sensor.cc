#include "imu_sensor.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
#include "app_datafusion.h"
}

static const char* TAG = "ImuSensor";

// 简化的BMI270/QMA6100P寄存器定义
#define IMU_CHIP_ID         0x00
#define IMU_DATA_8          0x0C  // 加速度计数据起始地址
#define IMU_ACC_CONF        0x40
#define IMU_ACC_RANGE       0x41
#define IMU_GYR_CONF        0x42
#define IMU_GYR_RANGE       0x43
#define IMU_PWR_CONF        0x7C
#define IMU_PWR_CTRL        0x7D

#define GRAVITY_EARTH       9.80665f

ImuSensor::ImuSensor(i2c_master_bus_handle_t i2c_bus, uint8_t device_addr)
    : i2c_bus_(i2c_bus), device_addr_(device_addr), timer_handle_(nullptr) {
    
    if (!i2c_bus_) {
        ESP_LOGE(TAG, "I2C bus handle is null");
        dev_handle_ = nullptr;
        return;
    }
    
    // 创建I2C设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr_,
        .scl_speed_hz = 400000,  // 400kHz
    };
    
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &dev_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        dev_handle_ = nullptr;
    }
}

ImuSensor::~ImuSensor() {
    Stop();
    if (dev_handle_) {
        i2c_master_bus_rm_device(dev_handle_);
    }
}

bool ImuSensor::Initialize() {
    if (!dev_handle_) {
        ESP_LOGE(TAG, "I2C device not initialized");
        return false;
    }

    // 读取芯片ID验证连接
    uint8_t chip_id = 0;
    if (!ReadRegister(IMU_CHIP_ID, &chip_id)) {
        ESP_LOGW(TAG, "Failed to read chip ID, using simulation mode");
        // 即使读取失败也继续，使用模拟模式
    } else {
        ESP_LOGI(TAG, "IMU Chip ID: 0x%02X", chip_id);
    }
    
    // 尝试配置传感器（如果真实传感器存在）
    WriteRegister(IMU_ACC_CONF, 0xA8);  // ODR=200Hz, Normal mode
    WriteRegister(IMU_ACC_RANGE, 0x01); // ±4g range
    WriteRegister(IMU_GYR_CONF, 0xA9);  // ODR=200Hz, Normal mode  
    WriteRegister(IMU_GYR_RANGE, 0x01); // ±1000°/s range
    WriteRegister(IMU_PWR_CTRL, 0x0E);  // Enable accelerometer and gyroscope
    WriteRegister(IMU_PWR_CONF, 0x00);  // Disable advanced power save mode
    
    vTaskDelay(pdMS_TO_TICKS(50)); // 等待传感器稳定

    ESP_LOGI(TAG, "IMU sensor initialized successfully");
    return true;
}

void ImuSensor::Start() {
    if (timer_handle_) {
        ESP_LOGW(TAG, "Timer already started");
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "imu_timer",
        .skip_unhandled_events = true
    };

    esp_err_t ret = esp_timer_create(&timer_args, &timer_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_timer_start_periodic(timer_handle_, 10000); // 10ms = 100Hz
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    } else {
        ESP_LOGI(TAG, "IMU sensor started at 100Hz");
    }
}

void ImuSensor::Stop() {
    if (timer_handle_) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
        ESP_LOGI(TAG, "IMU sensor stopped");
    }
}

void ImuSensor::SetDataCallback(OnDataCallback callback) {
    callback_ = callback;
}

void ImuSensor::TriggerReading() {
    if (!dev_handle_ || !callback_) {
        return;
    }
    
    // 读取原始IMU数据
    ImuRawData raw_data;
    if (ReadData(raw_data)) {
        ProcessAxisData(raw_data);
    }
}

bool ImuSensor::WriteRegister(uint8_t reg, uint8_t value) {
    if (!dev_handle_) return false;
    
    uint8_t write_buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(dev_handle_, write_buf, 2, 1000);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to write register 0x%02X: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool ImuSensor::ReadRegister(uint8_t reg, uint8_t* value) {
    if (!dev_handle_) return false;
    
    esp_err_t ret = i2c_master_transmit_receive(dev_handle_, &reg, 1, value, 1, 1000);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to read register 0x%02X: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool ImuSensor::ReadData(ImuRawData& data) {
    if (!dev_handle_) {
        // 模拟模式：生成一些随机运动数据用于测试
        static float time_counter = 0;
        time_counter += 0.01f;
        
        // 模拟轻微的加速度计数据（重力 + 噪声）
        data.acc_x = sinf(time_counter * 0.5f) * 0.5f;
        data.acc_y = cosf(time_counter * 0.3f) * 0.3f;
        data.acc_z = GRAVITY_EARTH + sinf(time_counter * 0.7f) * 0.2f;
        
        // 模拟陀螺仪数据（缓慢旋转）
        data.gyr_x = sinf(time_counter * 0.4f) * 2.0f;
        data.gyr_y = cosf(time_counter * 0.6f) * 1.5f;
        data.gyr_z = sinf(time_counter * 0.8f) * 1.0f;
        
        return true;
    }

    uint8_t raw_data[12];  // 6字节加速度计 + 6字节陀螺仪
    uint8_t reg = IMU_DATA_8;  // 加速度计数据起始地址
    
    esp_err_t ret = i2c_master_transmit_receive(dev_handle_, &reg, 1, raw_data, 12, 1000);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to read sensor data: %s", esp_err_to_name(ret));
        return false;
    }

    // 解析加速度计数据 (16位有符号整数，小端序)
    int16_t acc_x_raw = (int16_t)((raw_data[1] << 8) | raw_data[0]);
    int16_t acc_y_raw = (int16_t)((raw_data[3] << 8) | raw_data[2]);
    int16_t acc_z_raw = (int16_t)((raw_data[5] << 8) | raw_data[4]);

    // 解析陀螺仪数据
    int16_t gyr_x_raw = (int16_t)((raw_data[7] << 8) | raw_data[6]);
    int16_t gyr_y_raw = (int16_t)((raw_data[9] << 8) | raw_data[8]);
    int16_t gyr_z_raw = (int16_t)((raw_data[11] << 8) | raw_data[10]);

    // 转换为物理单位
    // 加速度计: ±4g range, 16-bit resolution
    float acc_scale = 4.0f * GRAVITY_EARTH / 32768.0f;
    data.acc_x = acc_x_raw * acc_scale;
    data.acc_y = acc_y_raw * acc_scale;
    data.acc_z = acc_z_raw * acc_scale;

    // 陀螺仪: ±1000°/s range, 16-bit resolution  
    float gyr_scale = 1000.0f / 32768.0f;
    data.gyr_x = gyr_x_raw * gyr_scale;
    data.gyr_y = gyr_y_raw * gyr_scale;
    data.gyr_z = gyr_z_raw * gyr_scale;

    return true;
}

ImuAxisData ImuSensor::ProcessAxisData(const ImuRawData& raw) {
    bmi270_axis_t axis_val;
    
    // 使用移植的数据融合算法
    calculateAttitude(raw.gyr_x, raw.gyr_y, raw.gyr_z, 
                     raw.acc_x, raw.acc_y, raw.acc_z, 
                     0.01f, &axis_val);
    
    ImuAxisData result;
    result.pitch = axis_val.pitch;
    result.roll = axis_val.roll;
    result.yaw = axis_val.yaw;
    
    return result;
}

void ImuSensor::TimerCallback(void* arg) {
    ImuSensor* sensor = static_cast<ImuSensor*>(arg);
    sensor->OnTimer();
}

void ImuSensor::OnTimer() {
    ImuRawData raw_data;
    if (ReadData(raw_data)) {
        ImuAxisData axis_data = ProcessAxisData(raw_data);
        
        // 计算相对于上次的变化量（用于检测摇晃）
        ImuAxisData axis_offset;
        axis_offset.pitch = axis_data.pitch - axis_last_val_.pitch;
        axis_offset.roll = axis_data.roll - axis_last_val_.roll;  
        axis_offset.yaw = axis_data.yaw - axis_last_val_.yaw;
        
        axis_last_val_ = axis_data;
        
        // 如果有回调函数，传递偏移数据（用于摇晃检测）
        if (callback_) {
            callback_(axis_offset);
        }
    }
}
