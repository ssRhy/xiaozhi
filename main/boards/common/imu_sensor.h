#ifndef _IMU_SENSOR_H_
#define _IMU_SENSOR_H_

#include <driver/i2c_master.h>
#include <esp_timer.h>
#include <functional>
#include "app_imu.h"

// 与app_imu.h兼容的数据结构
struct ImuAxisData {
    float pitch;
    float roll;
    float yaw;
};

struct ImuRawData {
    float acc_x, acc_y, acc_z;
    float gyr_x, gyr_y, gyr_z;
};

class ImuSensor {
public:
    using OnDataCallback = std::function<void(const ImuAxisData& axis_data)>;
    
    ImuSensor(i2c_master_bus_handle_t i2c_bus, uint8_t device_addr = 0x68);
    virtual ~ImuSensor();
    
    bool Initialize();
    void Start();
    void Stop();
    void SetDataCallback(OnDataCallback callback);
    void TriggerReading();  // 手动触发一次读取
    
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t dev_handle_;
    uint8_t device_addr_;
    esp_timer_handle_t timer_handle_;
    OnDataCallback callback_;
    
    ImuAxisData axis_last_val_;
    
    bool WriteRegister(uint8_t reg, uint8_t value);
    bool ReadRegister(uint8_t reg, uint8_t* value);
    bool ReadData(ImuRawData& data);
    ImuAxisData ProcessAxisData(const ImuRawData& raw);
    static void TimerCallback(void* arg);
    void OnTimer();
};

#endif // _IMU_SENSOR_H_
